#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Asset/AssetManager.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    void VulkanGBufferPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        FramebufferSpecification spec;
        spec.Width = width; spec.Height = height; spec.Samples = 1;
        spec.Attachments = {
            FramebufferTextureFormat::RG16F, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8,   FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::Depth
        };
        builder.WriteTexture("GBuffer", spec);
    }

    VulkanGBufferPass::~VulkanGBufferPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!context) return;
        VkDevice device = context->GetDevice();
        if (m_InstanceSetLayout) vkDestroyDescriptorSetLayout(device, m_InstanceSetLayout, nullptr);
        if (m_InstancePool)      vkDestroyDescriptorPool(device, m_InstancePool, nullptr);
    }

    void VulkanGBufferPass::OnAttach() {
        m_GBufferShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer.frag");
        m_PipeSpec.Shader = m_GBufferShader;
        m_PipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        m_PipeSpec.DepthTest = true;
        m_PipeSpec.DepthWrite = true;
        m_PipeSpec.Blend = false;
        m_PipeSpec.BackfaceCulling = CullMode::None;

        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720; refSpec.Samples = 1;
        refSpec.Attachments = {
            FramebufferTextureFormat::RG16F, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::Depth
        };
        m_RefFBO = Framebuffer::Create(refSpec);
        m_PipeSpec.TargetFramebuffer = m_RefFBO;
        m_Pipeline = Pipeline::Create(m_PipeSpec);

        // ── Instanced pipeline (same gbuffer.frag, SSBO at set=2) ──
        m_InstancedShader = Shader::Create("Deferred/gbuffer_instanced.vert", "Deferred/gbuffer.frag");

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;
            vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_InstanceSetLayout);
        }

        VulkanPipeline::s_ExtraSetLayouts = { m_InstanceSetLayout };
        m_InstancedSpec = m_PipeSpec;
        m_InstancedSpec.Shader = m_InstancedShader;
        m_InstancedPipeline = Pipeline::Create(m_InstancedSpec);

        m_InstanceBuffer = std::make_unique<VulkanStorageBuffer>(
            kMaxInstances * sizeof(glm::mat4));
        m_InstanceDescriptorSets.resize(fiCount);

        {
            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fiCount };
            VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            poolInfo.maxSets = fiCount;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_InstancePool);

            std::vector<VkDescriptorSetLayout> layouts(fiCount, m_InstanceSetLayout);
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocInfo.descriptorPool = m_InstancePool;
            allocInfo.descriptorSetCount = fiCount;
            allocInfo.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &allocInfo, m_InstanceDescriptorSets.data());
        }

        // ── GPU-Driven Rendering (GDR) — deferred pipeline creation ──
        // Pipeline is NOT created here because gbuffer.frag uses set=1 texture
        // bindings that conflict with the GDR vertex shader's set=2 SSBO layout.
        // Will be enabled once a bindless fragment shader replaces set=1.
        m_GDRShader = Shader::Create("Deferred/gbuffer_gdr.vert", "Deferred/gbuffer_bindless.frag");

        // ── Bindless pipelines (UseBindlessTextures=true) ──
        // Non-instanced bindless pipeline (no extra set layouts)
        VulkanPipeline::s_ExtraSetLayouts.clear();
        m_BindlessShader = Shader::Create("Deferred/gbuffer.vert", "Deferred/gbuffer_bindless.frag");
        PipelineSpecification bindlessSpec = m_PipeSpec;
        bindlessSpec.Shader = m_BindlessShader;
        bindlessSpec.UseBindlessTextures = true;
        m_BindlessPipeline = Pipeline::Create(bindlessSpec);

        // Instanced bindless pipeline (SSBO at set=2)
        m_BindlessInstancedShader = Shader::Create("Deferred/gbuffer_instanced.vert", "Deferred/gbuffer_bindless.frag");
        PipelineSpecification bindlessInstancedSpec = m_InstancedSpec;
        bindlessInstancedSpec.Shader = m_BindlessInstancedShader;
        bindlessInstancedSpec.UseBindlessTextures = true;
        VulkanPipeline::s_ExtraSetLayouts = { m_InstanceSetLayout };
        m_BindlessInstancedPipeline = Pipeline::Create(bindlessInstancedSpec);
        VulkanPipeline::s_ExtraSetLayouts.clear();
    }

    static inline uint64_t GetBatchKey(const DrawPacket& packet) {
        SortKey k; k.Value = packet.SortKey;
        return ((uint64_t)packet.MeshAsset.get() << 32) | (k.Bits.MaterialHash & 0xFFFFFFFF);
    }

    static bool IsInstancable(const DrawPacket& packet) {
        SortKey k; k.Value = packet.SortKey;
        return k.Bits.BucketID <= 1 && packet.MeshAsset && packet.MaterialAsset;
    }

    static void FillPC(GBufferPushConstants& pc, const DrawPacket& packet) {
        // Defaults: white/black/normal bindless indices for missing textures
        pc.Albedo_ReceiveShadows = glm::vec4(1.0f, 1.0f, 1.0f, packet.ReceiveShadows ? 1.0f : 0.0f);
        pc.Metallic_Roughness_AO_Alpha = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
        pc.AlphaCutoff_BlendMode_UseORMMap = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f);
        pc.AlbedoMapIndex = 1; pc.NormalMapIndex = 3; pc.ORMMapIndex = 2;
        pc.MetallicMapIndex = 1; pc.RoughnessMapIndex = 1; pc.AOMapIndex = 1;
        pc.AlphaMapIndex = 1; pc.IsSelected = 0;

        if (packet.MaterialAsset) {
            auto& b = packet.MaterialAsset->GetBakedPC();
            float finalMetallic, finalRoughness, finalAO;
            b.GetRenderScalars(finalMetallic, finalRoughness, finalAO);
            pc.Albedo_ReceiveShadows = glm::vec4(b.Albedo.r, b.Albedo.g, b.Albedo.b,
                                                  packet.ReceiveShadows ? 1.0f : 0.0f);
            pc.Metallic_Roughness_AO_Alpha = glm::vec4(finalMetallic, finalRoughness, finalAO, b.Alpha);
            pc.AlphaCutoff_BlendMode_UseORMMap = glm::vec4(
                packet.MaterialAsset->GetAlphaCutoff(),
                (float)(int)packet.MaterialAsset->GetBlendMode(),
                (float)b.UseORMMap, 0.0f);
            pc.AlbedoMapIndex = b.AlbedoMapIndex;
            pc.NormalMapIndex = b.NormalMapIndex;
            pc.ORMMapIndex = b.ORMMapIndex;
            pc.MetallicMapIndex = b.MetallicMapIndex;
            pc.RoughnessMapIndex = b.RoughnessMapIndex;
            pc.AOMapIndex = b.AOMapIndex;
        }
    }

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto fbo = context.GetFramebuffer("GBuffer");
        if (!fbo) return;

        auto* queue = context.RenderQueue;
        cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));

        if (!queue || queue->Packets.empty()) {
            cmd.EndRenderPass();
            return;
        }

        auto whiteTex = context.GetTexture("WhiteTexture");

        // ── Phase 1: Linear run-length batching ──
        // Packets are pre-sorted by SortKey → same-key packets are contiguous.
        // Build batches and transform array in a single linear O(N) pass.
        struct Batch { const DrawPacket* firstPkt; uint32_t first; uint32_t count; };
        std::vector<Batch> batches;
        std::vector<glm::mat4> transforms;
        batches.reserve(64);
        transforms.reserve(queue->Packets.size());

        std::unordered_set<const DrawPacket*> instancedSet;  // for Phase 3 skip

        for (size_t i = 0; i < queue->Packets.size(); ++i) {
            const auto& p = queue->Packets[i];
            if (!IsInstancable(p)) continue;

            // Single-instance or selected/hovered → skip instancing
            bool isSingle = (i == 0 || !IsInstancable(queue->Packets[i-1])
                             || GetBatchKey(p) != GetBatchKey(queue->Packets[i-1]))
                         && (i+1 >= queue->Packets.size() || !IsInstancable(queue->Packets[i+1])
                             || GetBatchKey(p) != GetBatchKey(queue->Packets[i+1]));
            if (isSingle) continue;

            uint64_t key = GetBatchKey(p);
            if (batches.empty() || key != GetBatchKey(*batches.back().firstPkt)) {
                batches.push_back({&p, (uint32_t)transforms.size(), 1});
            } else {
                batches.back().count++;
            }
            transforms.push_back(p.Transform);
            instancedSet.insert(&p);
        }

        // ── Phase 2: Issue instanced draws ──
        if (!batches.empty()) {
            auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            uint32_t fiCount = vkCtx->GetFramesInFlight();
            uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % fiCount;

            m_InstanceBuffer->SetData(transforms.data(),
                (uint32_t)(transforms.size() * sizeof(glm::mat4)));

            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = m_InstanceBuffer->GetBuffer(frameIdx);
            bufInfo.offset = 0;
            bufInfo.range = m_InstanceBuffer->GetSize();
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = m_InstanceDescriptorSets[frameIdx];
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &bufInfo;
            vkUpdateDescriptorSets(vkCtx->GetDevice(), 1, &write, 0, nullptr);

            cmd.BindPipeline(m_BindlessInstancedPipeline);
            // Bindless: no BindTexture2D calls needed — texture indices in push constants,
            // global bindless descriptor set bound automatically by BindPipeline.

            // Bind SSBO descriptor set (set=2, instance transforms) after BindPipeline
            // so the pipeline layout is the bindless one.
            {
                auto instVkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_BindlessInstancedPipeline);
                VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
                if (vkCmd && instVkPipe) {
                    vkCmdBindDescriptorSets(vkCmd,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        instVkPipe->GetVulkanPipelineLayout(), 2, 1,
                        &m_InstanceDescriptorSets[frameIdx], 0, nullptr);
                }
            }

            uint64_t lastMatHash = 0xFFFFFFFFFFFFFFFF;

            for (auto& batch : batches) {
                const DrawPacket* pkt = batch.firstPkt;
                GBufferPushConstants pc{};
                pc.Transform = pkt->Transform;
                FillPC(pc, *pkt);

                cmd.PushConstantData(m_BindlessInstancedPipeline, &pc, sizeof(pc));
                cmd.DrawIndexedInstanced(pkt->MeshAsset,
                    pkt->MeshAsset->GetIndexCount(), batch.count, batch.first);
            }
        }

        // ── Phase 3: Non-instanced fallback (bindless) ──
        {
            cmd.BindPipeline(m_BindlessPipeline);
            // Bindless: no BindTexture2D calls needed. Texture indices in push constants,
            // global bindless descriptor set bound automatically by BindPipeline.

            for (const auto& packet : queue->Packets) {
                SortKey key; key.Value = packet.SortKey;
                if (key.Bits.BucketID > 1) continue;

                // Skip instanced packets
                if (instancedSet.count(&packet)) continue;

                GBufferPushConstants pc{};
                pc.Transform = packet.Transform;
                FillPC(pc, packet);

                cmd.PushConstantData(m_BindlessPipeline, &pc, sizeof(GBufferPushConstants));
                cmd.DrawIndexed(packet.MeshAsset, packet.MeshAsset->GetIndexCount());
            }
        }

        cmd.EndRenderPass();
        context.Framebuffers["GBuffer"] = fbo;
    }
}
