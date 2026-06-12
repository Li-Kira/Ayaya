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
            FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA8,
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
            FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA8,
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
        m_GDRShader = Shader::Create("Deferred/gbuffer_gdr.vert", "Deferred/gbuffer.frag");
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
        pc.Albedo  = glm::vec3(1.0f); pc.Metallic = 0.0f; pc.Roughness = 0.5f;
        pc.AO = 1.0f; pc.AlphaMultiplier = 1.0f; pc.AlphaCutoff = 0.5f;
        pc.BlendMode = 0; pc.IsSelected = 0;
        pc.UseAlbedoMap = 0; pc.UseNormalMap = 0; pc.UseORMMap = 0;
        pc.UseMetallicMap = 0; pc.UseRoughnessMap = 0; pc.UseAOMap = 0; pc.UseAlphaMap = 0;
        pc.ReceiveShadows = packet.ReceiveShadows ? 1.0f : 0.0f;

        if (packet.MaterialAsset) {
            auto& b = packet.MaterialAsset->GetBakedPC();
            pc.Albedo = b.Albedo; pc.Metallic = b.Metallic;
            pc.Roughness = b.Roughness; pc.AO = b.AO;
            pc.AlphaMultiplier = b.Alpha; pc.AlphaCutoff = packet.MaterialAsset->GetAlphaCutoff();
            pc.BlendMode = (int)packet.MaterialAsset->GetBlendMode();
            pc.UseAlbedoMap = b.UseAlbedoMap; pc.UseNormalMap = b.UseNormalMap;
            pc.UseORMMap = b.UseORMMap;
            pc.UseMetallicMap = b.UseORMMap ? 0 : b.UseMetallicMap;
            pc.UseRoughnessMap = b.UseORMMap ? 0 : b.UseRoughnessMap;
            pc.UseAOMap = b.UseORMMap ? 0 : b.UseAOMap;
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
        Entity selected = context.Get<Entity>("SelectedEntity", Entity{});
        uint64_t selHandle = selected ? (uint64_t)selected.GetEntityHandle() : 0;
        Entity hovered  = context.Get<Entity>("HoveredEntity", Entity{});
        uint64_t hovHandle = hovered ? (uint64_t)hovered.GetEntityHandle() : 0;

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
            bool isSpecial = (selHandle && p.EntityHandle == selHandle)
                          || (hovHandle && p.EntityHandle == hovHandle);
            if (isSingle || isSpecial) continue;

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

            auto instVkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_InstancedPipeline);
            VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
            if (vkCmd && instVkPipe) {
                vkCmdBindDescriptorSets(vkCmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    instVkPipe->GetVulkanPipelineLayout(), 2, 1,
                    &m_InstanceDescriptorSets[frameIdx], 0, nullptr);
            }

            cmd.BindPipeline(m_InstancedPipeline);

            // White fallbacks once (must be AFTER BindPipeline which clears PendingImageInfos)
            if (whiteTex) {
                cmd.BindTexture2D(m_InstancedPipeline, "u_AlbedoMap",    1, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_MetallicMap",  2, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_RoughnessMap", 3, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_AOMap",        4, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_NormalMap",    5, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_AlphaMap",     6, whiteTex);
                cmd.BindTexture2D(m_InstancedPipeline, "u_ORMMap",       7, whiteTex);
            }
            uint64_t lastMatHash = 0xFFFFFFFFFFFFFFFF;

            for (auto& batch : batches) {
                const DrawPacket* pkt = batch.firstPkt;
                GBufferPushConstants pc{};
                pc.Transform = pkt->Transform;
                FillPC(pc, *pkt);

                uint64_t matKey = GetBatchKey(*pkt) & 0xFFFFFFFF;
                if (matKey != lastMatHash) {
                    lastMatHash = matKey;
                    if (pkt->MaterialAsset)
                        pkt->MaterialAsset->Bind(cmd, m_InstancedPipeline, whiteTex);
                }

                cmd.PushConstantData(m_InstancedPipeline, &pc, sizeof(pc));
                cmd.DrawIndexedInstanced(pkt->MeshAsset,
                    pkt->MeshAsset->GetIndexCount(), batch.count, batch.first);
            }
        }

        // ── Phase 3: Non-instanced fallback ──
        {
            cmd.BindPipeline(m_Pipeline);

            if (whiteTex) {
                cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap",    1, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_MetallicMap",  2, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_AOMap",        4, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_NormalMap",    5, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_AlphaMap",     6, whiteTex);
                cmd.BindTexture2D(m_Pipeline, "u_ORMMap",       7, whiteTex);
            }
            uint64_t currentMaterialHash = 0xFFFFFFFFFFFFFFFF;

            for (const auto& packet : queue->Packets) {
                SortKey key; key.Value = packet.SortKey;
                if (key.Bits.BucketID > 1) continue;

                // Skip instanced packets
                if (instancedSet.count(&packet)) continue;

                GBufferPushConstants pc{};
                pc.Transform = packet.Transform;
                FillPC(pc, packet);
                pc.IsSelected = (selHandle && packet.EntityHandle == selHandle) ? 1 : 0;

                if (key.Bits.MaterialHash != currentMaterialHash) {
                    currentMaterialHash = key.Bits.MaterialHash;
                    if (packet.MaterialAsset) {
                        for (auto& prop : packet.MaterialAsset->Properties) {
                            if (prop.Type == MaterialPropertyType::Texture2D) {
                                bool hasTex = (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) || (prop.RuntimeTexture != nullptr);
                                if (hasTex) {
                                    auto tex = prop.RuntimeTexture ? prop.RuntimeTexture : AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                                    if (prop.UniformName == "u_AlbedoMap")    { cmd.BindTexture2D(m_Pipeline, "u_AlbedoMap", 1, tex); pc.UseAlbedoMap = 1; }
                                    else if (prop.UniformName == "u_NormalMap")    { cmd.BindTexture2D(m_Pipeline, "u_NormalMap", 5, tex); pc.UseNormalMap = 1; }
                                    else if (prop.UniformName == "u_ORMMap")       { cmd.BindTexture2D(m_Pipeline, "u_ORMMap", 7, tex); pc.UseORMMap = 1; pc.UseMetallicMap = 0; pc.UseRoughnessMap = 0; pc.UseAOMap = 0; }
                                    else if (prop.UniformName == "u_MetallicMap")  { cmd.BindTexture2D(m_Pipeline, "u_MetallicMap", 2, tex); pc.UseMetallicMap = 1; }
                                    else if (prop.UniformName == "u_RoughnessMap") { cmd.BindTexture2D(m_Pipeline, "u_RoughnessMap", 3, tex); pc.UseRoughnessMap = 1; }
                                    else if (prop.UniformName == "u_AOMap")        { cmd.BindTexture2D(m_Pipeline, "u_AOMap", 4, tex); pc.UseAOMap = 1; }
                                    else if (prop.UniformName == "u_AlphaMap")     { cmd.BindTexture2D(m_Pipeline, "u_AlphaMap", 6, tex); pc.UseAlphaMap = 1; }
                                }
                            }
                        }
                    }
                }

                cmd.PushConstantData(m_Pipeline, &pc, sizeof(GBufferPushConstants));
                cmd.DrawIndexed(packet.MeshAsset, packet.MeshAsset->GetIndexCount());
            }
        }

        cmd.EndRenderPass();
        context.Framebuffers["GBuffer"] = fbo;
    }
}
