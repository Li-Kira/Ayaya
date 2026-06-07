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
        }
    }

    void VulkanWBOITPass::OnAttach() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = ctx->GetDevice();

        m_GatherShader  = Shader::Create("WBOIT/wboit_gather.vert",  "WBOIT/wboit_gather.frag");
        m_ResolveShader = Shader::Create("PostProcess/postprocess.vert", "WBOIT/wboit_resolve.frag");
        m_InstancedShader = Shader::Create("WBOIT/wboit_gather_instanced.vert", "WBOIT/wboit_gather.frag");

        // Ref FBOs
        FramebufferSpecification gatherRef;
        gatherRef.Width = 1280; gatherRef.Height = 720; gatherRef.Samples = 1;
        gatherRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RG16F };
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

        // Resolve pipeline
        FramebufferSpecification resolveRef;
        resolveRef.Width = 1280; resolveRef.Height = 720; resolveRef.Samples = 1;
        resolveRef.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_ResolveRefFBO = Framebuffer::Create(resolveRef);

        m_ResolveSpec.Shader = m_ResolveShader;
        m_ResolveSpec.TargetFramebuffer = m_ResolveRefFBO;
        m_ResolveSpec.Layout = {};
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
            if (k.Bits.BucketID == static_cast<uint64_t>(RenderBucket::Translucent))
                tpackets.push_back(&p);
        }
        if (tpackets.empty()) return;

        auto whiteTex = context.GetTexture("WhiteTexture");
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        uint32_t frameIdx = ctx->GetCurrentFrameIndex() % ctx->GetFramesInFlight();
        VkCommandBuffer vkCmd = ctx->GetCurrentCommandBuffer();

        cmd.SetPerAttachmentClearColors({
            glm::vec4(0,0,0,0), glm::vec4(1,0,0,0)
        });
        cmd.BeginRenderPass(gatherFBO, true, glm::vec4(0));

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

        // Bind IBL (shared)
        cmd.BindPipeline(m_InstancedPipeline);
        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        auto brdfLUT = context.GetTexture("BRDFLUT");
        auto wt2 = context.GetTexture("WhiteTexture");
        if (irrMap)  cmd.BindTextureCube(m_InstancedPipeline, "u_IrradianceMap",  3, irrMap);
        if (preMap)  cmd.BindTextureCube(m_InstancedPipeline, "u_PrefilteredMap", 4, preMap);
        if (brdfLUT) cmd.BindTexture2D(m_InstancedPipeline, "u_BRDFLUT", 5, brdfLUT);
        else if (wt2) cmd.BindTexture2D(m_InstancedPipeline, "u_BRDFLUT", 5, wt2);

        // Bind SSBO descriptor set (set=2 — baked into pipeline layout at creation)
        {
            auto instVkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_InstancedPipeline);
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                instVkPipe->GetVulkanPipelineLayout(), 2, 1,
                &m_InstanceDescriptorSets[frameIdx], 0, nullptr);
        }

        // Issue batched instanced draws
        uint64_t currentMatHash = 0xFFFFFFFFFFFFFFFF;
        for (auto& kv : batches) {
            auto& batch = kv.second;
            const DrawPacket* pkt = batch.firstPkt;

            // Material push constants
            WBOITGatherPushConstants pc{};
            pc.Transform = pkt->Transform;  // unused by instanced shader
            if (pkt->MaterialAsset) {
                auto& baked = pkt->MaterialAsset->GetBakedPC();
                pc.Albedo = baked.Albedo;   pc.Metallic = baked.Metallic;
                pc.Roughness = baked.Roughness; pc.AO = baked.AO;
                pc.Alpha = baked.Alpha;
                pc.UseAlbedoMap = baked.UseAlbedoMap;
                pc.UseMetallicMap = baked.UseMetallicMap;
                pc.UseRoughnessMap = baked.UseRoughnessMap;
            } else {
                pc.Albedo = glm::vec4(1); pc.Metallic = 0;
                pc.Roughness = 0.5f; pc.AO = 1; pc.Alpha = 0.5f;
            }

            // Textures — only rebind on material change
            if (kv.first != currentMatHash) {
                currentMatHash = kv.first;
                if (pkt->MaterialAsset) {
                    auto& baked = pkt->MaterialAsset->GetBakedPC();
                    cmd.BindTexture2D(m_InstancedPipeline, "u_AlbedoMap",    0,
                        baked.Textures[0] ? baked.Textures[0] : whiteTex);
                    cmd.BindTexture2D(m_InstancedPipeline, "u_MetallicMap",  1,
                        baked.Textures[1] ? baked.Textures[1] : whiteTex);
                    cmd.BindTexture2D(m_InstancedPipeline, "u_RoughnessMap", 2,
                        baked.Textures[2] ? baked.Textures[2] : whiteTex);
                } else if (whiteTex) {
                    cmd.BindTexture2D(m_InstancedPipeline, "u_AlbedoMap",    0, whiteTex);
                    cmd.BindTexture2D(m_InstancedPipeline, "u_MetallicMap",  1, whiteTex);
                    cmd.BindTexture2D(m_InstancedPipeline, "u_RoughnessMap", 2, whiteTex);
                }
            }

            cmd.PushConstantData(m_InstancedPipeline, &pc, sizeof(pc));
            cmd.DrawIndexedInstanced(pkt->MeshAsset, pkt->MeshAsset->GetIndexCount(),
                                      batch.count, batch.first);
        }

        cmd.EndRenderPass();
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
