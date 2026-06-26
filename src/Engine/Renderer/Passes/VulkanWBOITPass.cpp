#include "ayapch.h"
#include "VulkanWBOITPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Material.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Platform/Vulkan/VulkanTextureCube.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    // ---- helpers ----------------------------------------------------------

    static VkDescriptorSetLayout CreateSSBOSetLayout() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = ctx->GetDevice();

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        ci.bindingCount = 1;
        ci.pBindings = &binding;

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout);
        return layout;
    }

    static void AllocSSBODescriptorSets(VkDescriptorSetLayout layout, uint32_t count,
                                         VkDescriptorSet* outSets, VkDescriptorPool* outPool) {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = ctx->GetDevice();

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = count;

        VkDescriptorPoolCreateInfo poolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolCI.maxSets = count;
        poolCI.poolSizeCount = 1;
        poolCI.pPoolSizes = &poolSize;

        vkCreateDescriptorPool(device, &poolCI, nullptr, outPool);

        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc.descriptorPool = *outPool;
        alloc.descriptorSetCount = count;
        alloc.pSetLayouts = layouts.data();
        vkAllocateDescriptorSets(device, &alloc, outSets);
    }

    static void UpdateSSBODescriptor(VkDescriptorSet set, VkBuffer buffer, uint32_t size) {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = ctx->GetDevice();

        VkDescriptorBufferInfo info{};
        info.buffer = buffer;
        info.offset = 0;
        info.range  = size;

        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // ========================================================================

    VulkanWBOITPass::~VulkanWBOITPass() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (ctx) {
            VkDevice device = ctx->GetDevice();
            if (m_InstanceSetLayout) vkDestroyDescriptorSetLayout(device, m_InstanceSetLayout, nullptr);
            if (m_InstancePool) vkDestroyDescriptorPool(device, m_InstancePool, nullptr);
            if (m_IBLSetLayout) vkDestroyDescriptorSetLayout(device, m_IBLSetLayout, nullptr);
            if (m_IBLPool) vkDestroyDescriptorPool(device, m_IBLPool, nullptr);
        }
    }

    void VulkanWBOITPass::OnAttach() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = ctx->GetDevice();

        m_GatherShader  = Shader::Create("WBOIT/wboit_gather.vert",  "WBOIT/wboit_gather.frag");
        m_ResolveShader = Shader::Create("PostProcess/postprocess.vert", "WBOIT/wboit_resolve.frag");
        m_InstancedShader = Shader::Create("WBOIT/wboit_gather_instanced.vert", "WBOIT/wboit_gather.frag");

        // Ref FBO for pipeline creation — must include Depth format so
        // VkPipelineRenderingCreateInfo gets a valid depthAttachmentFormat,
        // otherwise depthTestEnable is ignored by Vulkan dynamic rendering.
        FramebufferSpecification gatherRef;
        gatherRef.Width = 1280; gatherRef.Height = 720; gatherRef.Samples = 1;
        gatherRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RG16F,
                                  FramebufferTextureFormat::Depth };
        m_GatherRefFBO = Framebuffer::Create(gatherRef);

        // Non-instanced gather pipeline (fallback)
        m_GatherSpec.Shader = m_GatherShader;
        m_GatherSpec.TargetFramebuffer = m_GatherRefFBO;
        m_GatherSpec.Layout = {
            {ShaderDataType::Float3, "a_Position"}, {ShaderDataType::Float3, "a_Normal"},
            {ShaderDataType::Float2, "a_TexCoord"}, {ShaderDataType::Float3, "a_Tangent"}
        };
        m_GatherSpec.DepthTest = true;
        m_GatherSpec.DepthWrite = false;
        m_GatherSpec.DepthOperator = DepthCompareOperator::LEqual;  // WBOIT: avoid z-fight with GBuffer
        m_GatherSpec.Blend = true;
        m_GatherSpec.BackfaceCulling = CullMode::None;
        m_GatherSpec.PerAttachmentBlend = { BlendModeType::Additive, BlendModeType::WBOITRevealage };
        m_GatherPipeline = Pipeline::Create(m_GatherSpec);

        // --- Instanced pipeline ---
        // SSBO descriptor set layout is injected via VulkanPipeline::s_ExtraSetLayouts
        // so the VkPipeline + VkPipelineLayout are created with all 3 sets (0+1+2).
        m_InstanceSetLayout = CreateSSBOSetLayout();
        VulkanPipeline::s_ExtraSetLayouts = { m_InstanceSetLayout };
        m_InstancedSpec = m_GatherSpec;
        m_InstancedSpec.Shader = m_InstancedShader;
        m_InstancedPipeline = Pipeline::Create(m_InstancedSpec);
        // s_ExtraSetLayouts is auto-cleared by VulkanPipeline constructor

        // SSBO + descriptor sets
        m_InstanceBuffer = std::make_unique<VulkanStorageBuffer>(
            static_cast<uint32_t>(kMaxInstances * sizeof(glm::mat4)));
        m_InstanceDescriptorSets.resize(ctx->GetFramesInFlight());
        AllocSSBODescriptorSets(m_InstanceSetLayout, ctx->GetFramesInFlight(),
                                m_InstanceDescriptorSets.data(), &m_InstancePool);

        // ── IBL descriptor set (set=3) — cube/cube/2D, bound once per frame ──
        {
            VkDescriptorSetLayoutBinding iblBindings[3] = {};
            iblBindings[0].binding = 0; iblBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            iblBindings[0].descriptorCount = 1; iblBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            iblBindings[1].binding = 1; iblBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            iblBindings[1].descriptorCount = 1; iblBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            iblBindings[2].binding = 2; iblBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            iblBindings[2].descriptorCount = 1; iblBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

            VkDescriptorSetLayoutCreateInfo iblLayoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            iblLayoutCI.bindingCount = 3;
            iblLayoutCI.pBindings = iblBindings;
            vkCreateDescriptorSetLayout(device, &iblLayoutCI, nullptr, &m_IBLSetLayout);

            uint32_t fiCount = ctx->GetFramesInFlight();
            VkDescriptorPoolSize iblPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fiCount * 3 };
            VkDescriptorPoolCreateInfo iblPoolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            iblPoolCI.maxSets = fiCount;
            iblPoolCI.poolSizeCount = 1;
            iblPoolCI.pPoolSizes = &iblPoolSize;
            vkCreateDescriptorPool(device, &iblPoolCI, nullptr, &m_IBLPool);

            m_IBLDescriptorSets.resize(fiCount);
            std::vector<VkDescriptorSetLayout> iblLayouts(fiCount, m_IBLSetLayout);
            VkDescriptorSetAllocateInfo iblAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            iblAlloc.descriptorPool = m_IBLPool;
            iblAlloc.descriptorSetCount = fiCount;
            iblAlloc.pSetLayouts = iblLayouts.data();
            vkAllocateDescriptorSets(device, &iblAlloc, m_IBLDescriptorSets.data());
        }

        // ── Bindless gather pipeline (UseBindlessTextures=true, SSBO set=2, IBL set=3) ──
        m_BindlessInstancedShader = Shader::Create("WBOIT/wboit_gather_instanced.vert", "WBOIT/wboit_gather_bindless.frag");
        {
            PipelineSpecification bindlessSpec;
            bindlessSpec.Shader = m_BindlessInstancedShader;
            bindlessSpec.TargetFramebuffer = m_GatherRefFBO;
            bindlessSpec.Layout = {
                {ShaderDataType::Float3, "a_Position"}, {ShaderDataType::Float3, "a_Normal"},
                {ShaderDataType::Float2, "a_TexCoord"}, {ShaderDataType::Float3, "a_Tangent"}
            };
            bindlessSpec.DepthTest = true;
            bindlessSpec.DepthWrite = false;
            bindlessSpec.DepthOperator = DepthCompareOperator::LEqual;
            bindlessSpec.Blend = true;
            bindlessSpec.BackfaceCulling = CullMode::None;
            bindlessSpec.UseBindlessTextures = true;
            bindlessSpec.PerAttachmentBlend = { BlendModeType::Additive, BlendModeType::WBOITRevealage };
            VulkanPipeline::s_ExtraSetLayouts = { m_InstanceSetLayout, m_IBLSetLayout };
            m_BindlessInstancedPipeline = Pipeline::Create(bindlessSpec);
            VulkanPipeline::s_ExtraSetLayouts.clear();
        }

        // Resolve pipeline
        FramebufferSpecification resolveRef;
        resolveRef.Width = 1280; resolveRef.Height = 720; resolveRef.Samples = 1;
        resolveRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_ResolveRefFBO = Framebuffer::Create(resolveRef);

        m_ResolveSpec.Shader = m_ResolveShader;
        m_ResolveSpec.TargetFramebuffer = m_ResolveRefFBO;
        m_ResolveSpec.Layout = {};
        m_ResolveSpec.Topology = PrimitiveTopology::TriangleStrip;
        m_ResolveSpec.DepthTest = false; m_ResolveSpec.DepthWrite = false;
        m_ResolveSpec.Blend = true; m_ResolveSpec.BlendMode = BlendModeType::Alpha;
        m_ResolveSpec.BackfaceCulling = CullMode::None;
        m_ResolvePipeline = Pipeline::Create(m_ResolveSpec);
    }

    // ========================================================================

    void VulkanWBOITPass::OnResize(uint32_t, uint32_t) {}

    void VulkanWBOITPass::DeclareGatherResources(RGBuilder& builder,
                                                  uint32_t width, uint32_t height) {
        FramebufferSpecification gatherSpec;
        gatherSpec.Width = width; gatherSpec.Height = height; gatherSpec.Samples = 1;
        gatherSpec.Attachments = { FramebufferTextureFormat::RGBA16F,
                                   FramebufferTextureFormat::RG16F };
        builder.WriteTexture("WBOIT_Gather", gatherSpec);
        builder.ReadTexture("GBuffer");
    }

    void VulkanWBOITPass::DeclareResolveResources(RGBuilder& builder,
                                                   uint32_t width, uint32_t height) {
        builder.ReadTexture("WBOIT_Gather");
        FramebufferSpecification hdrSpec;
        hdrSpec.Width = width; hdrSpec.Height = height; hdrSpec.Samples = 1;
        hdrSpec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.ReadWriteTexture("Lighting", hdrSpec);
    }

    // ========================================================================

    void VulkanWBOITPass::ExecuteGather(RenderContext& context, RenderCommandBuffer& cmd) {
        auto gatherFBO = context.GetFramebuffer("WBOIT_Gather");
        if (!gatherFBO) return;

        auto* queue = context.RenderQueue;
        if (!queue) return;

        // Count translucent packets
        std::vector<const DrawPacket*> tpackets;
        for (const auto& p : queue->Packets) {
            SortKey k; k.Value = p.SortKey;
            if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent)) {
                // SRP: skip objects with custom LightModeMask — rendered by their own passes.
                // Only draw objects whose LightModeMask includes Forward(4).
                if (p.MaterialAsset) {
                    uint32_t lmMask = p.MaterialAsset->GetLightModeMask();
                    if (!(lmMask & (uint32_t)LightModeFlags::Forward))
                        continue;
                }
                tpackets.push_back(&p);
            }
        }
        if (tpackets.empty()) return;

        auto whiteTex = context.GetTexture("WhiteTexture");
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        uint32_t frameIdx = ctx->GetCurrentFrameIndex() % ctx->GetFramesInFlight();
        VkCommandBuffer vkCmd = ctx->GetCurrentCommandBuffer();

        // ==== Shared depth from GBuffer (read-only, LOAD, never CLEAR) ====
        auto gbufferFBO = context.GetFramebuffer("GBuffer");
        auto vkGbuffer = std::dynamic_pointer_cast<VulkanFramebuffer>(gbufferFBO);
        auto vkGather  = std::dynamic_pointer_cast<VulkanFramebuffer>(gatherFBO);
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());

        // Color attachments — clear to zero / revealage-start
        uint32_t cCount = vkGather->GetColorAttachmentCount();
        std::vector<VkRenderingAttachmentInfo> colorAtts(cCount);
        glm::vec4 clearColors[2] = { {0,0,0,0}, {1,0,0,0} };
        for (uint32_t i = 0; i < cCount; i++) {
            colorAtts[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAtts[i].imageView = vkGather->GetColorAttachmentImageView(i);
            colorAtts[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAtts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAtts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtts[i].clearValue.color = {
                {clearColors[i].r, clearColors[i].g, clearColors[i].b, clearColors[i].a}};
        }

        // Depth attachment — shared from GBuffer, LOAD only, read-only layout
        VkRenderingAttachmentInfo depthAtt{};
        bool hasSharedDepth = vkGbuffer && vkGbuffer->HasDepthAttachment();
        if (hasSharedDepth) {
            depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAtt.imageView = vkGbuffer->GetDepthAttachmentImageView();
            depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = {{0, 0}, {vkGather->GetSpecification().Width,
                                              vkGather->GetSpecification().Height}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = cCount;
        renderingInfo.pColorAttachments = colorAtts.data();
        renderingInfo.pDepthAttachment = hasSharedDepth ? &depthAtt : nullptr;

        vkCmdBeginRendering(vkCmd, &renderingInfo);

        // Viewport + scissor
        float vpW = (float)vkGather->GetSpecification().Width;
        float vpH = (float)vkGather->GetSpecification().Height;
        VkViewport viewport{ 0, vpH, vpW, -vpH, 0, 1 };
        vkCmdSetViewport(vkCmd, 0, 1, &viewport);
        VkRect2D scissor{ {0, 0}, {(uint32_t)vpW, (uint32_t)vpH} };
        vkCmdSetScissor(vkCmd, 0, 1, &scissor);

        // ==== Instanced path: batch by (Mesh, Material) ====
        // Pack all instance transforms contiguously into the SSBO.
        // Group: map (Mesh*, MaterialHash) → { firstInstance, instanceCount }
        struct Batch { uint32_t first, count; const DrawPacket* firstPkt; };
        std::unordered_map<uint64_t, Batch> batches;  // key = meshPtr<<32 | matHash
        std::vector<glm::mat4> transforms;
        transforms.reserve(tpackets.size());

        for (auto* pkt : tpackets) {
            SortKey k; k.Value = pkt->SortKey;
            uint64_t meshKey = (uint64_t)pkt->MeshAsset.get();
            uint64_t key = (meshKey << 32) | (k.Bits.MaterialHash & 0xFFFFFFFF);
            auto it = batches.find(key);
            if (it == batches.end()) {
                batches[key] = { (uint32_t)transforms.size(), 1, pkt };
            } else {
                it->second.count++;
            }
            transforms.push_back(pkt->Transform);
        }

        // Upload transforms to SSBO (persistent mapped — memcpy, zero API overhead)
        m_InstanceBuffer->SetData(transforms.data(),
                                   (uint32_t)(transforms.size() * sizeof(glm::mat4)));

        // Update descriptor: bind SSBO for this frame
        UpdateSSBODescriptor(m_InstanceDescriptorSets[frameIdx],
                             m_InstanceBuffer->GetBuffer(frameIdx),
                             m_InstanceBuffer->GetSize());

        // Bind IBL descriptor set (set=3) once — written per frame
        {
            auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
            auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
            auto brdfLUT = context.GetTexture("BRDFLUT");
            auto fallback = context.GetTexture("WhiteTexture");

            auto irrVk = irrMap ? std::dynamic_pointer_cast<VulkanTextureCube>(irrMap) : nullptr;
            auto preVk = preMap ? std::dynamic_pointer_cast<VulkanTextureCube>(preMap) : nullptr;
            auto brdfVk = std::dynamic_pointer_cast<VulkanTexture2D>(brdfLUT);
            auto fallVk = std::dynamic_pointer_cast<VulkanTexture2D>(fallback);

            auto getCubeInfo = [](auto& vkCube) -> VkDescriptorImageInfo {
                VkDescriptorImageInfo info{};
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (vkCube) { info.imageView = vkCube->GetImageView(); info.sampler = vkCube->GetSampler(); }
                return info;
            };
            auto get2DInfo = [](auto& vkTex) -> VkDescriptorImageInfo {
                VkDescriptorImageInfo info{};
                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (vkTex) { info.imageView = vkTex->GetImageView(); info.sampler = vkTex->GetSampler(); }
                return info;
            };

            VkWriteDescriptorSet writes[3];
            VkDescriptorImageInfo irrInfo = getCubeInfo(irrVk);
            VkDescriptorImageInfo preInfo = getCubeInfo(preVk);
            VkDescriptorImageInfo lutInfo = brdfVk ? get2DInfo(brdfVk) : get2DInfo(fallVk);

            writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[0].dstSet = m_IBLDescriptorSets[frameIdx]; writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].descriptorCount = 1; writes[0].pImageInfo = &irrInfo;

            writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[1].dstSet = m_IBLDescriptorSets[frameIdx]; writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1; writes[1].pImageInfo = &preInfo;

            writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            writes[2].dstSet = m_IBLDescriptorSets[frameIdx]; writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].descriptorCount = 1; writes[2].pImageInfo = &lutInfo;

            vkUpdateDescriptorSets(ctx->GetDevice(), 3, writes, 0, nullptr);
        }

        // Bind bindless pipeline — auto-binds global bindless set (set=1)
        cmd.BindPipeline(m_BindlessInstancedPipeline);

        // Bind SSBO descriptor set (set=2) and IBL descriptor set (set=3)
        {
            auto instVkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_BindlessInstancedPipeline);
            VkPipelineLayout layout = instVkPipe->GetVulkanPipelineLayout();
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout, 2, 1, &m_InstanceDescriptorSets[frameIdx], 0, nullptr);
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                layout, 3, 1, &m_IBLDescriptorSets[frameIdx], 0, nullptr);
        }

        // Issue batched instanced draws (bindless: no per-material BindTexture2D calls)
        for (auto& kv : batches) {
            auto& batch = kv.second;
            const DrawPacket* pkt = batch.firstPkt;

            WBOITGatherPushConstants pc{};
            pc.Transform = pkt->Transform;  // unused by instanced shader
            if (pkt->MaterialAsset) {
                auto& baked = pkt->MaterialAsset->GetBakedPC();
                float finalMetallic, finalRoughness, finalAO;
                baked.GetRenderScalars(finalMetallic, finalRoughness, finalAO);
                pc.Albedo = baked.Albedo;   pc.Metallic = finalMetallic;
                pc.Roughness = finalRoughness; pc.AO = finalAO;
                pc.Alpha = baked.Alpha;
                pc.UseORMMap = baked.UseORMMap;
                pc.AlbedoMapIndex = baked.AlbedoMapIndex;
                pc.NormalMapIndex = baked.NormalMapIndex;
                pc.ORMMapIndex = baked.ORMMapIndex;
                pc.MetallicMapIndex = baked.MetallicMapIndex;
                pc.RoughnessMapIndex = baked.RoughnessMapIndex;
                pc.AOMapIndex = baked.AOMapIndex;
            } else {
                pc.Albedo = glm::vec4(1); pc.Metallic = 0;
                pc.Roughness = 0.5f; pc.AO = 1; pc.Alpha = 0.5f;
            }

            cmd.PushConstantData(m_BindlessInstancedPipeline, &pc, sizeof(pc));
            cmd.DrawIndexedInstanced(pkt->MeshAsset, pkt->MeshAsset->GetIndexCount(),
                                      batch.count, batch.first);
        }

        vkCmdEndRendering(vkCmd);
    }

    // ========================================================================

    void VulkanWBOITPass::ExecuteResolve(RenderContext& context, RenderCommandBuffer& cmd) {
        auto hdrFBO    = context.GetFramebuffer("Lighting");
        auto gatherFBO = context.GetFramebuffer("WBOIT_Gather");
        if (!hdrFBO || !gatherFBO) return;

        bool hasTranslucent = false;
        if (auto* queue = context.RenderQueue) {
            for (const auto& p : queue->Packets) {
                SortKey k; k.Value = p.SortKey;
                if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent))
                { hasTranslucent = true; break; }
            }
        }
        if (!hasTranslucent) return;

        cmd.BeginRenderPass(hdrFBO, false);
        cmd.BindPipeline(m_ResolvePipeline);
        cmd.BindTexture2D(m_ResolvePipeline, "u_Accumulation", 0, gatherFBO, 0);
        cmd.BindTexture2D(m_ResolvePipeline, "u_Revealage",    1, gatherFBO, 1);
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["Lighting"] = hdrFBO;
    }

}
