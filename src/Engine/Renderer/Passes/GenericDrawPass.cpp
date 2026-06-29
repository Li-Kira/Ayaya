#include "ayapch.h"
#include "GenericDrawPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Frustum.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"
#include "Core/VFS.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Renderer/Material.hpp"

namespace Ayaya {

    static constexpr uint32_t kMaxInstances = 65536;

    GenericDrawPass::~GenericDrawPass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();

        // Wait for GPU to finish before destroying pipelines.
        // GenericDrawPass is recreated during SRP rebuild — the old instance's
        // pipelines may still be referenced by in-flight command buffers.
        vkDeviceWaitIdle(device);

        if (m_CullPipeline)  vkDestroyPipeline(device, m_CullPipeline, nullptr);
        if (m_CullLayout)    vkDestroyPipelineLayout(device, m_CullLayout, nullptr);
        if (m_CullShader)    vkDestroyShaderModule(device, m_CullShader, nullptr);
        if (m_CullSet3Pool)  vkDestroyDescriptorPool(device, m_CullSet3Pool, nullptr);
        if (m_CullSet3Layout) vkDestroyDescriptorSetLayout(device, m_CullSet3Layout, nullptr);
        if (m_CullDummyLayout) vkDestroyDescriptorSetLayout(device, m_CullDummyLayout, nullptr);
    }

    void GenericDrawPass::OnAttach() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        // ── Load cull.comp SPIR-V (shared compute shader) ──
        // Priority: project-local override → engine default
        auto exePath = std::filesystem::current_path();
        std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Deferred/cull.comp.spv").string();
        std::string projLocal = VFS::ResolveString("project://Shaders/Cache/Deferred/cull.comp.spv");
        if (std::filesystem::exists(projLocal)) spvPath = projLocal;
        std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            AYAYA_CORE_ERROR("[GenericDraw] Failed to open cull.comp.spv at {}", spvPath);
            return;
        }
        size_t sz = (size_t)file.tellg();
        std::vector<char> buf(sz);
        file.seekg(0); file.read(buf.data(), sz); file.close();
        VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
        vkCreateShaderModule(device, &sm, nullptr, &m_CullShader);

        // ── Indirect draw buffer (triple-buffered) ──
        m_DrawIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
            kMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

        // ── Set 3: DrawIndirectBuffer descriptor ──
        VkDescriptorSetLayoutBinding set3Bind{};
        set3Bind.binding = 0;
        set3Bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        set3Bind.descriptorCount = 1;
        set3Bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo set3Info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        set3Info.bindingCount = 1; set3Info.pBindings = &set3Bind;
        vkCreateDescriptorSetLayout(device, &set3Info, nullptr, &m_CullSet3Layout);

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.maxSets = 10; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &poolSize;
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_CullSet3Pool);

        m_CullSet3Descriptors.resize(fiCount);
        std::vector<VkDescriptorSetLayout> layouts(fiCount, m_CullSet3Layout);
        VkDescriptorSetAllocateInfo setAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        setAI.descriptorPool = m_CullSet3Pool; setAI.descriptorSetCount = fiCount;
        setAI.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &setAI, m_CullSet3Descriptors.data());

        for (uint32_t i = 0; i < fiCount; i++) {
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = m_DrawIndirectBuffer->GetBuffer(i);
            bufInfo.range = VK_WHOLE_SIZE;
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_CullSet3Descriptors[i]; w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.descriptorCount = 1; w.pBufferInfo = &bufInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // ── Dummy Set 0/1 layout (cull.comp uses GDR set=2, not per-pipeline set=0/1).
        //     Must be a member variable — Vulkan spec requires DS layouts referenced by a
        //     VkPipelineLayout to outlive the layout (and all pipelines derived from it).
        VkDescriptorSetLayoutCreateInfo dummyInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        vkCreateDescriptorSetLayout(device, &dummyInfo, nullptr, &m_CullDummyLayout);

        // ── Compute pipeline layout ──
        VkDescriptorSetLayout cullLayouts[] = { m_CullDummyLayout, m_CullDummyLayout,
            m_GDRCtx ? m_GDRCtx->Set2Layout : VK_NULL_HANDLE, m_CullSet3Layout };
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
        VkPipelineLayoutCreateInfo plInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        plInfo.setLayoutCount = 4; plInfo.pSetLayouts = cullLayouts;
        plInfo.pushConstantRangeCount = 1; plInfo.pPushConstantRanges = &pcRange;
        vkCreatePipelineLayout(device, &plInfo, nullptr, &m_CullLayout);

        VkComputePipelineCreateInfo cpInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cpInfo.stage.module = m_CullShader;
        cpInfo.stage.pName = "main";
        cpInfo.layout = m_CullLayout;
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfo, nullptr, &m_CullPipeline);
    }

    void GenericDrawPass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                            const PassBakedParams& params) {
        // Explicit writes declared by PipelineBuilder from Lua
    }

    std::shared_ptr<Shader> GenericDrawPass::LoadShader(
        const std::string& vertPath, const std::string& fragPath) {
        return Shader::Create(vertPath, fragPath);
    }

    std::shared_ptr<Pipeline> GenericDrawPass::GetOrCreatePipeline(const PipelineKey& key) {
        auto it = m_PipelineCache.find(key);
        if (it != m_PipelineCache.end()) return it->second;

        std::string stem = key.shader;
        if (stem.size() > 5 && stem.substr(stem.size()-5) == ".frag") stem = stem.substr(0, stem.size()-5);
        else if (stem.size() > 5 && stem.substr(stem.size()-5) == ".vert") stem = stem.substr(0, stem.size()-5);
        auto shader = LoadShader(stem + ".vert", stem + ".frag");
        if (!shader) {
            AYAYA_CORE_ERROR("[GenericDraw] Failed to load shader: {}", key.shader);
            return nullptr;
        }

        PipelineSpecification spec;
        spec.Shader = shader;
        spec.DepthTest = key.depthTest;
        spec.DepthWrite = key.depthWrite;
        spec.BackfaceCulling = (key.cullMode == 0) ? CullMode::None :
                               (key.cullMode == 2) ? CullMode::Back :
                               CullMode::Front;
        spec.Blend = (key.blendMode != 0);
        spec.BlendMode = (key.blendMode == 1) ? BlendModeType::Additive :
                         (key.blendMode == 2) ? BlendModeType::Alpha :
                         BlendModeType::None;
        spec.NoTextureDescriptors = false;

        // Reference FBO for dynamic rendering format matching.
        // Must match the runtime target: color format + depth format if present.
        // Otherwise VkPipelineRenderingCreateInfo depthAttachmentFormat won't match
        // the render pass's depth attachment → VUID-dynamicRenderingUnusedAttachments-08914.
        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720;
        refSpec.Attachments = { key.colorFormat };
        if (key.hasDepth)
            refSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
        spec.TargetFramebuffer = Framebuffer::Create(refSpec);

        // Inject GDR Set 2 (SSBO: Instance/Range/Material/GeometryPool) into pipeline layout
        size_t extraLayoutIdx = VulkanPipeline::s_ExtraSetLayouts.size();
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            VulkanPipeline::s_ExtraSetLayouts.push_back(m_GDRCtx->Set2Layout);
        }

        std::shared_ptr<Pipeline> pipeline;
        try {
            pipeline = Pipeline::Create(spec);
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("[GenericDraw] Pipeline creation failed for '{}': {}", key.shader, e.what());
            VulkanPipeline::s_ExtraSetLayouts.resize(extraLayoutIdx);
            return nullptr;
        }
        VulkanPipeline::s_ExtraSetLayouts.resize(extraLayoutIdx);
        if (pipeline) m_PipelineCache[key] = pipeline;
        return pipeline;
    }

    void GenericDrawPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        std::string prefix = m_NodeName.empty() ? m_PassName : m_NodeName;
        uint32_t lightModeMask = (uint32_t)context.Get<int>(prefix + ".LightModeMask", 1);
        std::string queueType = context.Get<std::string>(prefix + ".Queue", "Opaque");

        if (queueType == "Transparent") {
            //AYAYA_CORE_INFO("[GenericDraw:'{}'] Queue=Transparent, dispatching to ExecuteTransparent", prefix);
            ExecuteTransparent(context, cmd, prefix, lightModeMask);
        } else {
            ExecuteOpaque(context, cmd, prefix, lightModeMask);
        }
    }

    void GenericDrawPass::ExecuteOpaque(RenderContext& context, RenderCommandBuffer& cmd,
                                         const std::string& prefix, uint32_t lightModeMask) {
        if (!m_GDRCtx || m_GDRCtx->InstanceCount == 0) return;

        bool depthTest  = context.Get<int>(prefix + ".DepthTest", 1) != 0;
        bool depthWrite = context.Get<int>(prefix + ".DepthWrite", 1) != 0;
        int cullMode    = context.Get<int>(prefix + ".CullMode", 2);
        int blendMode   = context.Get<int>(prefix + ".BlendMode", 0);
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");

        if (shaderPath.empty() || targetName.empty()) {
            AYAYA_CORE_WARN("[GenericDraw] Missing Shader or Target param");
            return;
        }

        // Get target FBO first — need its color format for pipeline creation
        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) {
            AYAYA_CORE_WARN("[GenericDraw] Target FBO '{}' not found", targetName);
            return;
        }

        // Extract color + depth format from the runtime FBO for pipeline format matching
        FramebufferTextureFormat fmt = FramebufferTextureFormat::RGBA16F;
        bool hasDepth = false;
        auto& fboSpec = fbo->GetSpecification();
        for (auto& att : fboSpec.Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                fmt = att.TextureFormat;
            } else {
                hasDepth = true;
            }
        }

        // Get or create graphics pipeline (keyed by format to match dynamic rendering)
        PipelineKey key{ shaderPath, depthTest, depthWrite, cullMode, blendMode, fmt, hasDepth };
        auto pipeline = GetOrCreatePipeline(key);
        if (!pipeline) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        // ── Compute culling (outside render pass) ──
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                 | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            1, &hostBarrier, 0, nullptr, 0, nullptr);

        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline);
        m_GDRCtx->BindSet2(vkCmd, m_CullLayout, VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_CullLayout, 3, 1, &m_CullSet3Descriptors[frameIdx], 0, nullptr);

        struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t lightModeMask; uint32_t overrideInstanceID; uint32_t _pad; } fpc;
        // Gribb-Hartmann frustum plane extraction from VP matrix (same as VulkanGBufferPass)
        glm::mat4 vp = context.ProjectionMatrix * context.ViewMatrix;
        glm::mat4 vpT = glm::transpose(vp);
        glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
        fpc.planes[0] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[0]), rows[3].w + rows[0].w));
        fpc.planes[1] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[0]), rows[3].w - rows[0].w));
        fpc.planes[2] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[1]), rows[3].w + rows[1].w));
        fpc.planes[3] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[1]), rows[3].w - rows[1].w));
        fpc.planes[4] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[2]), rows[3].w + rows[2].w));
        fpc.planes[5] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[2]), rows[3].w - rows[2].w));
        fpc.count = m_GDRCtx->InstanceCount;
        fpc.lightModeMask = lightModeMask;
        fpc.overrideInstanceID = 0xFFFFFFFF;  // GDR path: use SV_InstanceID
        vkCmdPushConstants(vkCmd, m_CullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);

        uint32_t groupCount = (m_GDRCtx->InstanceCount + 63) / 64;
        vkCmdDispatch(vkCmd, groupCount, 1, 1);

        VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
        indirectBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
            0, nullptr, 1, &indirectBarrier, 0, nullptr);

        // ── Graphics pass ──
        bool clear = context.Get<int>(prefix + ".ClearColor", 1) != 0;
        cmd.BeginRenderPass(fbo, clear);
        cmd.BindPipeline(pipeline);

        // Bind GDR Set 2 (Instance/Range/Material/GeometryPool SSBOs)
        // Required by AyayaGDR.hlsl vertex-pulling shaders.
        // VulkanRenderCommandBuffer::BindPipeline only binds sets 0+1;
        // set 2 must be explicitly bound after, matching VulkanGBufferPass pattern.
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
            if (gdrPipe) {
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gdrPipe->GetVulkanPipelineLayout(),
                    2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
            }
        }

        uint32_t instanceCount = std::min(m_GDRCtx->InstanceCount, kMaxInstances);
        // Issue indirect draw using the global geometry pool
        auto& pool = vkCtx->GetGeometryPool();
        VkDeviceSize vbOffsets[] = { 0 };
        VkBuffer vb = pool.GetBuffer();
        vkCmdBindVertexBuffers(vkCmd, 0, 1, &vb, vbOffsets);
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(vkCmd, m_DrawIndirectBuffer->GetBuffer(frameIdx), 0,
            instanceCount, sizeof(VkDrawIndexedIndirectCommand));

        cmd.EndRenderPass();
    }

    void GenericDrawPass::ExecuteTransparent(RenderContext& context, RenderCommandBuffer& cmd,
                                              const std::string& prefix, uint32_t mask) {
        auto* queue = context.RenderQueue;
        if (!queue) { /*AYAYA_CORE_INFO("[Transparent:'{}'] No RenderQueue", prefix);*/ return; }
        if (queue->Packets.empty()) { /*AYAYA_CORE_INFO("[Transparent:'{}'] RenderQueue empty", prefix);*/ return; }

        // CPU filter: LightModeMask + RenderBucket::Translucent
        std::vector<const DrawPacket*> packets;
        for (auto& p : queue->Packets) {
            SortKey k; k.Value = p.SortKey;
            if (k.Bits.BucketID != (uint64_t)RenderBucket::Translucent) continue;
            if (!p.MaterialAsset) continue;
            uint32_t lm = p.MaterialAsset->GetLightModeMask();
            if ((lm & mask) == 0) { /*AYAYA_CORE_INFO("[Transparent:'{}'] skip packet mask={}, passMask={}", prefix, lm, mask);*/ continue; }
            packets.push_back(&p);
        }
        if (packets.empty()) { /*AYAYA_CORE_INFO("[Transparent:'{}'] No matching packets", prefix);*/ return; }
        //AYAYA_CORE_INFO("[Transparent:'{}'] Found {} packets", prefix, (int)packets.size());

        bool depthTest  = context.Get<int>(prefix + ".DepthTest", 1) != 0;
        bool depthWrite = context.Get<int>(prefix + ".DepthWrite", 1) != 0;
        int cullMode    = context.Get<int>(prefix + ".CullMode", 2);
        int blendMode   = context.Get<int>(prefix + ".BlendMode", 0);
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");

        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) return;

        FramebufferTextureFormat fmt = FramebufferTextureFormat::RGBA16F;
        bool hasDepth = false;
        for (auto& att : fbo->GetSpecification().Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                fmt = att.TextureFormat;
            } else { hasDepth = true; }
        }

        PipelineKey key{shaderPath, depthTest, depthWrite, cullMode, blendMode, fmt, hasDepth};
        auto pipeline = GetOrCreatePipeline(key);
        if (!pipeline) { AYAYA_CORE_INFO("[Transparent:'{}'] Pipeline FAILED", prefix); return; }

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        bool clear = context.Get<int>(prefix + ".ClearColor", 1) != 0;

        // ── Host → Device barrier: ensure SSBO data written by BuildFromScene
        //     (InstanceSSBO, GeometryRangeSSBO, MaterialSSBO, GeometryPool) is
        //     visible to the vertex shader. Same pattern as ExecuteOpaque.
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            1, &hostBarrier, 0, nullptr, 0, nullptr);

        cmd.BeginRenderPass(fbo, clear);
        cmd.BindPipeline(pipeline);

        // Bind GDR Set 2 — SSBO vertex pulling (same data source as Opaque path)
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
            if (gdrPipe)
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gdrPipe->GetVulkanPipelineLayout(),
                    2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
        }

        // Bind global geometry pool once
        auto& pool = vkCtx->GetGeometryPool();
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // CPU-sorted submission: push instanceID, draw each matching packet
        struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t lightModeMask; uint32_t overrideInstanceID; uint32_t _pad; } fpc;
        memset(&fpc, 0, sizeof(fpc));
        fpc.lightModeMask = mask;
        auto plLayout = std::dynamic_pointer_cast<VulkanPipeline>(pipeline)->GetVulkanPipelineLayout();

        int drawCount = 0;
        for (auto* pkt : packets) {
            fpc.overrideInstanceID = pkt->GPUInstanceIndex;
            vkCmdPushConstants(vkCmd, plLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(fpc), &fpc);

            auto range = pool.GetOrUploadMesh(pkt->MeshAsset.get());
            // vertexOffset MUST be 0 — the GDR vertex shader (AyayaGDR.hlsl)
            // already adds range.vertexOffset internally via the SSBO GeometryRange.
            // Vulkan adds vertexOffset to SV_VertexID for indexed draws.
            // Passing non-zero here would double-count: SV_VertexID would be
            // (indexVal + range.vertexOffset), then the shader adds range.vertexOffset
            // again → reads from the wrong location in the geometry pool.
            vkCmdDrawIndexed(vkCmd, range.indexCount, 1,
                range.indexOffset / 4, 0, 0);
            drawCount++;
        }
        //AYAYA_CORE_INFO("[Transparent:'{}'] Submitted {} draws, target={}, fmt={}, hasDepth={}, blend={}",
        //    prefix, drawCount, targetName, (int)fmt, hasDepth, blendMode);

        cmd.EndRenderPass();
    }

} // namespace Ayaya
