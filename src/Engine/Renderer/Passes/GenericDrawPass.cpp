#include "ayapch.h"
#include "GenericDrawPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/PassRegistry.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/Frustum.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Core/Application.hpp"
#include "Core/VFS.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Renderer/Material.hpp"

namespace Ayaya {

    static constexpr uint32_t kMaxInstances = 65536;

    // ── Unified push constant struct (matches HLSL FrustumPC + GLSL cull.comp) ──
    struct FrustumPush {
        glm::vec4 planes[6];           // 96 bytes
        uint32_t  instanceCount;       // 4
        uint32_t  lightModeMask;       // 4
        uint32_t  overrideInstanceID;  // 4
        uint32_t  _pad;               // 4
        glm::vec4 texelSize;          // 16 — x=1/tw, y=1/th, z=tw, w=th
        float     exposureInverse;    //  4 — 1.0/PhysicalExposure (matches ForwardBlend pass)
    };

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
        if (m_SceneInputPool) vkDestroyDescriptorPool(device, m_SceneInputPool, nullptr);
        if (m_SceneInputLayout) vkDestroyDescriptorSetLayout(device, m_SceneInputLayout, nullptr);
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
        VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FrustumPush) };
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

        // ── Scene input descriptor set (Set 3, graphics stage) ──
        // 4 combined image samplers: SceneColor, SceneDepth, PrefilterMap(IBL), BRDFLUT.
        VkDescriptorSetLayoutBinding sceneBinds[4]{};
        for (uint32_t i = 0; i < 4; i++) {
            sceneBinds[i].binding = i;
            sceneBinds[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sceneBinds[i].descriptorCount = 1;
            sceneBinds[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            sceneBinds[i].pImmutableSamplers = nullptr;
        }
        VkDescriptorSetLayoutCreateInfo sceneLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        sceneLayoutInfo.bindingCount = 4;
        sceneLayoutInfo.pBindings = sceneBinds;
        vkCreateDescriptorSetLayout(device, &sceneLayoutInfo, nullptr, &m_SceneInputLayout);

        VkDescriptorPoolSize scenePoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * fiCount };
        VkDescriptorPoolCreateInfo scenePoolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        scenePoolInfo.maxSets = fiCount;
        scenePoolInfo.poolSizeCount = 1;
        scenePoolInfo.pPoolSizes = &scenePoolSize;
        vkCreateDescriptorPool(device, &scenePoolInfo, nullptr, &m_SceneInputPool);

        m_SceneInputDescriptors.resize(fiCount);
        std::vector<VkDescriptorSetLayout> sceneLayouts(fiCount, m_SceneInputLayout);
        VkDescriptorSetAllocateInfo sceneAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        sceneAI.descriptorPool = m_SceneInputPool;
        sceneAI.descriptorSetCount = fiCount;
        sceneAI.pSetLayouts = sceneLayouts.data();
        vkAllocateDescriptorSets(device, &sceneAI, m_SceneInputDescriptors.data());
    }

    void GenericDrawPass::DeclareResources(RGBuilder& builder, uint32_t w, uint32_t h,
                                            const PassBakedParams& params) {
        // Explicit writes declared by PipelineBuilder from Lua
    }

    std::shared_ptr<Shader> GenericDrawPass::LoadShader(
        const std::string& vertPath, const std::string& fragPath) {
        return Shader::Create(vertPath, fragPath);
    }

    std::shared_ptr<Pipeline> GenericDrawPass::GetOrCreatePipeline(const PipelineKey& key, const FramebufferSpecification& fboSpec) {
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
        auto vkShader = std::dynamic_pointer_cast<VulkanShader>(shader);
        if (!vkShader || !vkShader->IsValid()) {
            AYAYA_CORE_ERROR("[GenericDraw] Shader SPIR-V missing or invalid: {}", key.shader);
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
        spec.UseBindlessTextures = true;   // Set 1 = global bindless array — enables VS/PS sampling by index
        spec.ColorWrite = key.colorWrite;
        switch (key.depthFunc) {
            case 1: spec.DepthOperator = DepthCompareOperator::LEqual;  break;
            case 2: spec.DepthOperator = DepthCompareOperator::Never;   break;
            case 3: spec.DepthOperator = DepthCompareOperator::Greater; break;
            default: spec.DepthOperator = DepthCompareOperator::Less;   break; // 0
        }

        // Reference FBO for dynamic rendering format matching.
        // Must use the FULL attachment layout from the runtime FBO — not just the first color.
        // Otherwise VkPipelineRenderingCreateInfo colorAttachmentCount/format won't match
        // the render pass → VUID-colorAttachmentCount-06179 / VUID-dynamicRendering-08910.
        FramebufferSpecification refSpec = fboSpec;
        refSpec.Width = 1280; refSpec.Height = 720;
        spec.TargetFramebuffer = Framebuffer::Create(refSpec);

        // Inject GDR Set 2 (SSBO: Instance/Range/Material/GeometryPool) into pipeline layout
        size_t extraLayoutIdx = VulkanPipeline::s_ExtraSetLayouts.size();
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            VulkanPipeline::s_ExtraSetLayouts.push_back(m_GDRCtx->Set2Layout);
        }
        // Scene input set (Set 3) — always appended; shaders that don't declare it leave it unused.
        VulkanPipeline::s_ExtraSetLayouts.push_back(m_SceneInputLayout);

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
        // Prefer the named LightMode tag (resolved to a unique bit by LightModeTagRegistry);
        // fall back to the raw LightModeMask int for backward compatibility.
        uint32_t lightModeMask = (uint32_t)context.Get<int>(prefix + ".LightMode", 0);
        if (lightModeMask == 0)
            lightModeMask = (uint32_t)context.Get<int>(prefix + ".LightModeMask", 1);
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
        int depthFunc   = context.Get<int>(prefix + ".DepthFunc", 0);   // 0=LESS, 1=LEQUAL
        bool colorWrite = context.Get<int>(prefix + ".ColorWrite", 1) != 0;
        bool clearColor = context.Get<int>(prefix + ".ClearColor", 1) != 0;
        bool clearDepth = context.Get<int>(prefix + ".ClearDepth", 1) != 0;
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");
        std::string depthTargetName = context.Get<std::string>(prefix + ".DepthTarget", "");

        if (shaderPath.empty() || targetName.empty()) {
            AYAYA_CORE_WARN("[GenericDraw] Missing Shader or Target param");
            return;
        }

        // Get color FBO + optional external depth FBO
        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) {
            AYAYA_CORE_WARN("[GenericDraw] Target FBO '{}' not found", targetName);
            return;
        }
        auto depthFBO = depthTargetName.empty() ? nullptr : context.GetFramebuffer(depthTargetName);

        // Extract color format from runtime FBO. Depth comes from colorFBO or external depthFBO.
        FramebufferTextureFormat fmt = FramebufferTextureFormat::RGBA16F;
        bool hasDepth = (depthFBO != nullptr);
        auto& fboSpec = fbo->GetSpecification();
        for (auto& att : fboSpec.Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                fmt = att.TextureFormat;
            } else {
                hasDepth = true; // colorFBO自带深度
            }
        }

        // Pipeline refSpec must include depth format if depth test is enabled
        FramebufferSpecification pipeFboSpec = fboSpec;
        if (hasDepth && depthFBO) {
            // Color from fbo, depth from external — append depth to refSpec for pipeline creation
            bool refHasDepth = false;
            for (auto& att : pipeFboSpec.Attachments.Attachments) {
                if (att.TextureFormat == FramebufferTextureFormat::Depth ||
                    att.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8)
                    refHasDepth = true;
            }
            if (!refHasDepth)
                pipeFboSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
        }

        // Get or create graphics pipeline (keyed by format to match dynamic rendering)
        PipelineKey key{ shaderPath, depthTest, depthWrite, cullMode, blendMode, fmt, hasDepth, depthFunc, colorWrite };
        auto pipeline = GetOrCreatePipeline(key, pipeFboSpec);
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

        FrustumPush fpc{};
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
        fpc.instanceCount = m_GDRCtx->InstanceCount;
        fpc.lightModeMask = lightModeMask;
        fpc.overrideInstanceID = 0xFFFFFFFF;  // GDR path: use SV_InstanceID
        // Populate _TexelSize from target Framebuffer dimensions
        float tw = (float)fbo->GetSpecification().Width;
        float th = (float)fbo->GetSpecification().Height;
        fpc.texelSize = glm::vec4(1.0f / tw, 1.0f / th, tw, th);
        fpc.exposureInverse = 1.0f / context.Get<float>("PhysicalExposure", 1.0f);
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
        // Depth layout is managed by RenderGraph:
        //   - EnsureWritable (DepthReadTextures) transitions to ATTACHMENT before the pass
        //   - InsertTileResolveBarrier (DepthReadTextures) transitions back to READ_ONLY after
        if (depthFBO)
            cmd.BeginRenderPass(fbo, depthFBO, clearColor, clearDepth);
        else
            cmd.BeginRenderPass(fbo, clearColor, clearDepth);
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
                // Push FrustumPush to graphics pipeline — pc.overrideInstanceID
                // (GetAyayaVertex) and pc._TexelSize (TA shader) need this.
                vkCmdPushConstants(vkCmd, gdrPipe->GetVulkanPipelineLayout(),
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(fpc), &fpc);
            }
        }

        // Scene inputs (Set 3) — optional SceneColor/SceneDepth/IBL for forward effects (water refraction)
        bool readColor = context.Get<int>(prefix + ".ReadSceneColor", 0) != 0;
        bool readDepth = context.Get<int>(prefix + ".ReadSceneDepth", 0) != 0;
        bool readIBL   = context.Get<int>(prefix + ".ReadIBL", 0) != 0;
        if (readColor || readDepth || readIBL) {
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
            if (gdrPipe) {
                BindSceneInputs(context, vkCmd, frameIdx, gdrPipe->GetVulkanPipelineLayout(),
                                readColor, readDepth, readIBL);
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

        // Resolve the target + clear flags first — needed to clear stale output even when
        // the RenderQueue is empty (e.g. scene switch to a scene without the water entity).
        bool clearColor = context.Get<int>(prefix + ".ClearColor", 1) != 0;
        bool clearDepth = context.Get<int>(prefix + ".ClearDepth", 1) != 0;
        std::string targetName = context.Get<std::string>(prefix + ".Target", "");
        auto fbo = context.GetFramebuffer(targetName);
        if (!fbo) return;

        // CPU filter: LightModeMask + RenderBucket::Translucent
        std::vector<const DrawPacket*> packets;
        if (queue) {
            for (auto& p : queue->Packets) {
                SortKey k; k.Value = p.SortKey;
                if (k.Bits.BucketID != (uint64_t)RenderBucket::Translucent) continue;
                if (!p.MaterialAsset) continue;
                uint32_t lm = p.MaterialAsset->GetLightModeMask();
                if ((lm & mask) == 0) { /*AYAYA_CORE_INFO("[Transparent:'{}'] skip packet mask={}, passMask={}", prefix, lm, mask);*/ continue; }
                packets.push_back(&p);
            }
        }

        if (packets.empty()) {
            // No matching packets (or empty queue) — clear the target so stale content
            // doesn't linger (e.g. hidden water / scene switch without the water entity).
            if (clearColor) {
                cmd.BeginRenderPass(fbo, clearColor, clearDepth);
                cmd.EndRenderPass();
            }
            return;
        }
        //AYAYA_CORE_INFO("[Transparent:'{}'] Found {} packets", prefix, (int)packets.size());

        bool depthTest  = context.Get<int>(prefix + ".DepthTest", 1) != 0;
        bool depthWrite = context.Get<int>(prefix + ".DepthWrite", 1) != 0;
        int cullMode    = context.Get<int>(prefix + ".CullMode", 2);
        int blendMode   = context.Get<int>(prefix + ".BlendMode", 0);
        int depthFunc   = context.Get<int>(prefix + ".DepthFunc", 0);
        bool colorWrite = context.Get<int>(prefix + ".ColorWrite", 1) != 0;
        std::string shaderPath = context.Get<std::string>(prefix + ".Shader", "");
        std::string depthTargetName = context.Get<std::string>(prefix + ".DepthTarget", "");

        auto depthFBO = depthTargetName.empty() ? nullptr : context.GetFramebuffer(depthTargetName);

        FramebufferTextureFormat fmt = FramebufferTextureFormat::RGBA16F;
        bool hasDepth = (depthFBO != nullptr);
        auto pipeFboSpec = fbo->GetSpecification();
        for (auto& att : pipeFboSpec.Attachments.Attachments) {
            if (att.TextureFormat != FramebufferTextureFormat::Depth &&
                att.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                fmt = att.TextureFormat;
            } else { hasDepth = true; }
        }
        if (hasDepth && depthFBO) {
            bool refHasDepth = false;
            for (auto& att : pipeFboSpec.Attachments.Attachments)
                if (att.TextureFormat == FramebufferTextureFormat::Depth ||
                    att.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8) refHasDepth = true;
            if (!refHasDepth)
                pipeFboSpec.Attachments.Attachments.push_back({FramebufferTextureFormat::Depth});
        }

        PipelineKey key{shaderPath, depthTest, depthWrite, cullMode, blendMode, fmt, hasDepth, depthFunc, colorWrite};
        auto pipeline = GetOrCreatePipeline(key, pipeFboSpec);
        if (!pipeline) { AYAYA_CORE_INFO("[Transparent:'{}'] Pipeline FAILED", prefix); return; }

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        // ── Host → Device barrier: ensure SSBO data written by BuildFromScene
        //     (InstanceSSBO, GeometryRangeSSBO, MaterialSSBO, GeometryPool) is
        //     visible to the vertex shader. Same pattern as ExecuteOpaque.
        VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
            1, &hostBarrier, 0, nullptr, 0, nullptr);

        if (depthFBO)
            cmd.BeginRenderPass(fbo, depthFBO, clearColor, clearDepth);
        else
            cmd.BeginRenderPass(fbo, clearColor, clearDepth);
        cmd.BindPipeline(pipeline);

        // Bind GDR Set 2 — SSBO vertex pulling (same data source as Opaque path)
        if (m_GDRCtx && m_GDRCtx->Set2Layout != VK_NULL_HANDLE) {
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
            if (gdrPipe)
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gdrPipe->GetVulkanPipelineLayout(),
                    2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
        }

        // Scene inputs (Set 3) — optional SceneColor/SceneDepth/IBL
        bool readColor = context.Get<int>(prefix + ".ReadSceneColor", 0) != 0;
        bool readDepth = context.Get<int>(prefix + ".ReadSceneDepth", 0) != 0;
        bool readIBL   = context.Get<int>(prefix + ".ReadIBL", 0) != 0;
        if (readColor || readDepth || readIBL) {
            auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
            if (gdrPipe)
                BindSceneInputs(context, vkCmd, frameIdx, gdrPipe->GetVulkanPipelineLayout(),
                                readColor, readDepth, readIBL);
        }

        // Bind global geometry pool once
        auto& pool = vkCtx->GetGeometryPool();
        vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Populate _TexelSize from target FBO
        float tw = (float)fbo->GetSpecification().Width;
        float th = (float)fbo->GetSpecification().Height;
        // CPU-sorted submission: push instanceID, draw each matching packet
        FrustumPush fpc{};
        fpc.lightModeMask = mask;
        fpc.texelSize = glm::vec4(1.0f / tw, 1.0f / th, tw, th);
        fpc.exposureInverse = 1.0f / context.Get<float>("PhysicalExposure", 1.0f);
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

    void GenericDrawPass::BindSceneInputs(RenderContext& context, VkCommandBuffer vkCmd, uint32_t frameIdx,
                                          VkPipelineLayout layout, bool color, bool depth, bool ibl) {
        VkDescriptorImageInfo infos[4]{};
        if (color) {
            auto fbo = context.GetFramebuffer("Lighting");
            if (auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(fbo)) {
                infos[0].imageView = vkFBO->GetColorAttachmentImageView(0);
                infos[0].sampler = vkFBO->GetSampler();
                infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }
        if (depth) {
            auto fbo = context.GetFramebuffer("SceneDepth");
            if (auto vkFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(fbo)) {
                infos[1].imageView = vkFBO->GetDepthAttachmentImageView();
                infos[1].sampler = vkFBO->GetShadowSampler() ? vkFBO->GetShadowSampler() : vkFBO->GetSampler();
                infos[1].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }
        }
        if (ibl) {
            auto prefilter = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
            if (auto vkCube = std::dynamic_pointer_cast<VulkanTextureCube>(prefilter)) {
                infos[2].imageView = vkCube->GetImageView();
                infos[2].sampler = vkCube->GetSampler();
                infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            auto brdfLUT = context.GetTexture("BRDFLUT");
            if (auto vkTex = std::dynamic_pointer_cast<VulkanTexture2D>(brdfLUT)) {
                infos[3].imageView = vkTex->GetImageView();
                infos[3].sampler = vkTex->GetSampler();
                infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
        }

        VkWriteDescriptorSet writes[4]{};
        uint32_t writeCount = 0;
        for (uint32_t i = 0; i < 4; i++) {
            if (infos[i].imageView == VK_NULL_HANDLE || infos[i].sampler == VK_NULL_HANDLE) continue;
            writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[writeCount].dstSet = m_SceneInputDescriptors[frameIdx];
            writes[writeCount].dstBinding = i;
            writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[writeCount].descriptorCount = 1;
            writes[writeCount].pImageInfo = &infos[i];
            writeCount++;
        }
        if (writeCount == 0) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        vkUpdateDescriptorSets(vkCtx->GetDevice(), writeCount, writes, 0, nullptr);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
            3, 1, &m_SceneInputDescriptors[frameIdx], 0, nullptr);
    }

} // namespace Ayaya
