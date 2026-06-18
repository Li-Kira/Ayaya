#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include <fstream>
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
        if (m_GDR_SetLayout)     vkDestroyDescriptorSetLayout(device, m_GDR_SetLayout, nullptr);
        if (m_GDR_Pool)          vkDestroyDescriptorPool(device, m_GDR_Pool, nullptr);
        if (m_Cull_Set3Layout)   vkDestroyDescriptorSetLayout(device, m_Cull_Set3Layout, nullptr);
        if (m_Cull_Set3Pool)     vkDestroyDescriptorPool(device, m_Cull_Set3Pool, nullptr);
        if (m_Cull_DummyLayout)  vkDestroyDescriptorSetLayout(device, m_Cull_DummyLayout, nullptr);
        if (m_Cull_PipelineLayout) vkDestroyPipelineLayout(device, m_Cull_PipelineLayout, nullptr);
        if (m_Cull_Pipeline)     vkDestroyPipeline(device, m_Cull_Pipeline, nullptr);
        if (m_Cull_ShaderModule) vkDestroyShaderModule(device, m_Cull_ShaderModule, nullptr);
    }

    void VulkanGBufferPass::OnAttach() {
        // Create format reference FBO (1280×720 placeholder — actual size from RenderGraph)
        FramebufferSpecification refSpec;
        refSpec.Width = 1280; refSpec.Height = 720; refSpec.Samples = 1;
        refSpec.Attachments = {
            FramebufferTextureFormat::RG16F, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RGBA8,
            FramebufferTextureFormat::Depth
        };
        m_RefFBO = Framebuffer::Create(refSpec);

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        // ── GPU-Driven Rendering (GDR) — SSBO-based instance & material data ──
        {
            uint32_t fiCount = vkCtx->GetFramesInFlight();

            // Set=2 layout: 4 bindings for GPU-Driven Rendering
            //   0 = GPUInstance[]      (VERTEX | FRAGMENT)
            //   1 = GeometryRange[]    (VERTEX)
            //   2 = GPUMaterial[]      (VERTEX | FRAGMENT)
            //   3 = GeometryBuffer     (VERTEX) — uint g_Data[] for vertex pulling
            VkDescriptorSetLayoutBinding gdrBindings[4] = {};
            gdrBindings[0].binding = 0; gdrBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            gdrBindings[0].descriptorCount = 1; gdrBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            gdrBindings[1].binding = 1; gdrBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            gdrBindings[1].descriptorCount = 1; gdrBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
            gdrBindings[2].binding = 2; gdrBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            gdrBindings[2].descriptorCount = 1; gdrBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            gdrBindings[3].binding = 3; gdrBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            gdrBindings[3].descriptorCount = 1; gdrBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

            VkDescriptorSetLayoutCreateInfo layoutCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutCI.bindingCount = 4;
            layoutCI.pBindings = gdrBindings;
            vkCreateDescriptorSetLayout(device, &layoutCI, nullptr, &m_GDR_SetLayout);

            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fiCount * 4 };
            VkDescriptorPoolCreateInfo poolCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            poolCI.maxSets = fiCount;
            poolCI.poolSizeCount = 1;
            poolCI.pPoolSizes = &poolSize;
            vkCreateDescriptorPool(device, &poolCI, nullptr, &m_GDR_Pool);

            m_GDR_DescriptorSets.resize(fiCount);
            std::vector<VkDescriptorSetLayout> layouts(fiCount, m_GDR_SetLayout);
            VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            alloc.descriptorPool = m_GDR_Pool;
            alloc.descriptorSetCount = fiCount;
            alloc.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &alloc, m_GDR_DescriptorSets.data());

            m_GDR_InstanceSSBO  = std::make_unique<VulkanStorageBuffer>(
                kGDRMaxInstances * sizeof(GPUInstance));
            m_GDR_GeometryRangeSSBO = std::make_unique<VulkanStorageBuffer>(
                kGDRMaxMeshes * sizeof(GeometryRange));
            m_GDR_MaterialSSBO  = std::make_unique<VulkanStorageBuffer>(
                kGDRMaxMaterials * sizeof(GPUMaterial));

            // Pre-bind descriptor sets: VkBuffer handles are fixed per frame,
            // only the mapped data changes via SetData(). Write once at init.
            VkBuffer geoBuf = vkCtx->GetGeometryPool().GetBuffer();
            for (uint32_t i = 0; i < fiCount; i++) {
                VkDescriptorBufferInfo instI{}, rangeI{}, matI{}, geoI{};
                instI.buffer  = m_GDR_InstanceSSBO->GetBuffer(i);
                instI.offset  = 0; instI.range  = VK_WHOLE_SIZE;
                rangeI.buffer = m_GDR_GeometryRangeSSBO->GetBuffer(i);
                rangeI.offset = 0; rangeI.range = VK_WHOLE_SIZE;
                matI.buffer   = m_GDR_MaterialSSBO->GetBuffer(i);
                matI.offset   = 0; matI.range   = VK_WHOLE_SIZE;
                geoI.buffer   = geoBuf;
                geoI.offset   = 0; geoI.range   = VK_WHOLE_SIZE;

                VkWriteDescriptorSet w[4]{};
                w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[0].dstSet = m_GDR_DescriptorSets[i]; w[0].dstBinding = 0;
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[0].descriptorCount = 1; w[0].pBufferInfo = &instI;
                w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[1].dstSet = m_GDR_DescriptorSets[i]; w[1].dstBinding = 1;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[1].descriptorCount = 1; w[1].pBufferInfo = &rangeI;
                w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[2].dstSet = m_GDR_DescriptorSets[i]; w[2].dstBinding = 2;
                w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[2].descriptorCount = 1; w[2].pBufferInfo = &matI;
                w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[3].dstSet = m_GDR_DescriptorSets[i]; w[3].dstBinding = 3;
                w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w[3].descriptorCount = 1; w[3].pBufferInfo = &geoI;
                vkUpdateDescriptorSets(device, 4, w, 0, nullptr);
            }

            // Create the GDR pipeline with empty vertex layout (SSBO vertex pulling)
            m_GDRShader = Shader::Create("Deferred/gbuffer_gdr.vert", "Deferred/gbuffer_gdr_bindless.frag");
            PipelineSpecification gdrSpec;
            gdrSpec.Shader = m_GDRShader;
            gdrSpec.TargetFramebuffer = m_RefFBO;
            gdrSpec.Layout = {};  // empty — no VBO inputs, vertices pulled from SSBO
            gdrSpec.DepthTest = true; gdrSpec.DepthWrite = true;
            gdrSpec.Blend = false;
            gdrSpec.BackfaceCulling = CullMode::None;
            gdrSpec.UseBindlessTextures = true;
            VulkanPipeline::s_ExtraSetLayouts = { m_GDR_SetLayout };
            m_GDRPipeline = Pipeline::Create(gdrSpec);
            VulkanPipeline::s_ExtraSetLayouts.clear();
        }

        // ── Compute Culling pipeline (Step 3) — frustum cull on GPU ──
        {
            uint32_t fiCount = vkCtx->GetFramesInFlight();

            // Set=3: single DrawCommands[] SSBO (fixed-slot: Commands[gID], no atomic counter)
            VkDescriptorSetLayoutBinding cullB{};
            cullB.binding = 0; cullB.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            cullB.descriptorCount = 1; cullB.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo cullLCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            cullLCI.bindingCount = 1; cullLCI.pBindings = &cullB;
            vkCreateDescriptorSetLayout(device, &cullLCI, nullptr, &m_Cull_Set3Layout);

            VkDescriptorPoolSize cullPS{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fiCount };
            VkDescriptorPoolCreateInfo cullPCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            cullPCI.maxSets = fiCount; cullPCI.poolSizeCount = 1; cullPCI.pPoolSizes = &cullPS;
            vkCreateDescriptorPool(device, &cullPCI, nullptr, &m_Cull_Set3Pool);
            m_Cull_Set3Descriptors.resize(fiCount);
            std::vector<VkDescriptorSetLayout> cullLayouts(fiCount, m_Cull_Set3Layout);
            VkDescriptorSetAllocateInfo cullAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            cullAI.descriptorPool = m_Cull_Set3Pool; cullAI.descriptorSetCount = fiCount;
            cullAI.pSetLayouts = cullLayouts.data();
            vkAllocateDescriptorSets(device, &cullAI, m_Cull_Set3Descriptors.data());

            // Indirect draw buffer (triple-buffered)
            m_DrawIndirectBuffer = std::make_unique<VulkanStorageBuffer>(
                kGDRMaxInstances * sizeof(VkDrawIndexedIndirectCommand),
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);

            // Pre-bind set=3 descriptor (single binding, VkBuffer handles never change)
            for (uint32_t i = 0; i < fiCount; i++) {
                VkDescriptorBufferInfo cmdI{};
                cmdI.buffer = m_DrawIndirectBuffer->GetBuffer(i); cmdI.range = VK_WHOLE_SIZE;
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                w.dstSet = m_Cull_Set3Descriptors[i]; w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.descriptorCount = 1; w.pBufferInfo = &cmdI;
                vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
            }

            // Build compute shader module from SPIR-V
            VkShaderModule compModule = VK_NULL_HANDLE;
            {
                // Load SPIR-V binary directly (bypass Shader factory which expects vert+frag pair)
                // The shader compiler outputs: cache/vulkan/Deferred/cull.comp.spv
                auto exePath = std::filesystem::current_path();
                std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Deferred/cull.comp.spv").string();
                std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
                if (file.is_open()) {
                    size_t fileSize = (size_t)file.tellg();
                    std::vector<char> buffer(fileSize);
                    file.seekg(0);
                    file.read(buffer.data(), fileSize);
                    file.close();
                    VkShaderModuleCreateInfo smCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                    smCI.codeSize = buffer.size();
                    smCI.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
                    vkCreateShaderModule(device, &smCI, nullptr, &m_Cull_ShaderModule);
                }
            }

            // Create empty descriptor set layouts for unused sets 0 and 1
            // (MoltenVK requires valid handles — VK_NULL_HANDLE needs gfxPipelineLibrary ext)
            VkDescriptorSetLayoutCreateInfo dummyCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            dummyCI.bindingCount = 0;
            vkCreateDescriptorSetLayout(device, &dummyCI, nullptr, &m_Cull_DummyLayout);

            // Build compute pipeline and layout
            // Indices must match shader set=N: [0]=dummy, [1]=dummy, [2]=GDR, [3]=Cull
            VkDescriptorSetLayout compSetLayouts[] = { m_Cull_DummyLayout, m_Cull_DummyLayout,
                m_GDR_SetLayout, m_Cull_Set3Layout };
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 112 };
            VkPipelineLayoutCreateInfo plCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plCI.setLayoutCount = 4; plCI.pSetLayouts = compSetLayouts;
            plCI.pushConstantRangeCount = 1; plCI.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plCI, nullptr, &m_Cull_PipelineLayout);

            VkComputePipelineCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpCI.stage.module = m_Cull_ShaderModule;
            cpCI.stage.pName = "main";
            cpCI.layout = m_Cull_PipelineLayout;
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_Cull_Pipeline);
        }

    }

    // (Phase 1-3 retired — replaced by GDR Phase 4)

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
    auto fbo = context.GetFramebuffer("GBuffer");
    if (!fbo) return;

    auto* queue = context.RenderQueue;
    if (!queue || queue->Packets.empty()) return;

    // ── GPU-Driven Rendering — build SSBO data, compute cull, indirect draw ──
    {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());

        // Build GDR data vectors
        std::vector<GPUInstance> gdrInstances;
        std::vector<GPUMaterial> gdrMaterials;
        std::vector<GeometryRange> gdrRanges;
        gdrInstances.reserve(queue->Packets.size());
        gdrMaterials.reserve(64);
        gdrRanges.reserve(64);

        if (vkCtx && m_GDRPipeline) {
            uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();
            VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
            auto& pool = vkCtx->GetGeometryPool();

            // Resource Staging: ensure all meshes are in the geometry pool
            for (const auto& pkt : queue->Packets) {
                if (pkt.MeshAsset) pool.GetOrUploadMesh(pkt.MeshAsset.get());
            }

            std::unordered_map<Mesh*, uint32_t> meshToRange;
            std::unordered_map<uint64_t, uint32_t> matMap;

            for (const auto& pkt : queue->Packets) {
                SortKey k; k.Value = pkt.SortKey;
                if (k.Bits.BucketID > 1) continue;
                if (!pkt.MeshAsset || !pkt.MaterialAsset) continue;

                uint32_t rangeIdx;
                auto rit = meshToRange.find(pkt.MeshAsset.get());
                if (rit != meshToRange.end()) {
                    rangeIdx = rit->second;
                } else {
                    rangeIdx = (uint32_t)gdrRanges.size();
                    auto range = pool.GetOrUploadMesh(pkt.MeshAsset.get());
                    gdrRanges.push_back(range);
                    meshToRange[pkt.MeshAsset.get()] = rangeIdx;
                }

                uint64_t matHash = k.Bits.MaterialHash;
                uint32_t matIdx;
                auto mit = matMap.find(matHash);
                if (mit != matMap.end()) {
                    matIdx = mit->second;
                } else {
                    matIdx = (uint32_t)gdrMaterials.size();
                    matMap[matHash] = matIdx;
                    GPUMaterial gm{};
                    auto& b = pkt.MaterialAsset->GetBakedPC();
                    gm.albedo = b.Albedo; gm.metallic = b.Metallic;
                    gm.roughness = b.Roughness; gm.ao = b.AO; gm.alpha = b.Alpha;
                    gm.albedoBindless   = (int)b.AlbedoMapIndex;
                    gm.normalBindless   = (int)b.NormalMapIndex;
                    gm.ormBindless      = (int)b.ORMMapIndex;
                    gm.metallicBindless  = (int)b.MetallicMapIndex;
                    gm.roughnessBindless = (int)b.RoughnessMapIndex;
                    gm.aoBindless        = (int)b.AOMapIndex;
                    gm.useORMMap   = (int)b.UseORMMap;
                    gm.alphaCutoff = pkt.MaterialAsset->GetAlphaCutoff();
                    gm.blendMode   = (int)pkt.MaterialAsset->GetBlendMode();
                    gdrMaterials.push_back(gm);
                }

                GPUInstance gi{};
                gi.transform = pkt.Transform;
                AABB aabb = pkt.MeshAsset->GetAABB();
                glm::vec3 center = (aabb.Min + aabb.Max) * 0.5f;
                glm::vec3 worldCenter = glm::vec3(pkt.Transform * glm::vec4(center, 1.0f));
                float radius = glm::length(aabb.Max - aabb.Min) * 0.5f;
                gi.boundingSphere = glm::vec4(worldCenter, radius);
                gi.geometryRangeIdx = rangeIdx;
                gi.materialIdx = matIdx;
                gi.entityId = (uint32_t)(k.Bits.EntityID);
                gi._pad = 0;
                gdrInstances.push_back(gi);
            }

            // Upload scene data to SSBOs (memcpy to persistent-mapped buffers)
            m_GDR_InstanceSSBO->SetData(gdrInstances.data(),
                (uint32_t)(gdrInstances.size() * sizeof(GPUInstance)));
            m_GDR_GeometryRangeSSBO->SetData(gdrRanges.data(),
                (uint32_t)(gdrRanges.size() * sizeof(GeometryRange)));
            m_GDR_MaterialSSBO->SetData(gdrMaterials.data(),
                (uint32_t)(gdrMaterials.size() * sizeof(GPUMaterial)));

            uint32_t instanceCount = (uint32_t)gdrInstances.size();

            if (instanceCount > 0) {
                // ── Compute culling (outside render pass) ──
                vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Cull_Pipeline);
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_Cull_PipelineLayout, 2, 1, &m_GDR_DescriptorSets[frameIdx], 0, nullptr);
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_Cull_PipelineLayout, 3, 1, &m_Cull_Set3Descriptors[frameIdx], 0, nullptr);

                struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t _pad[3]; } fpc;
                glm::mat4 vp = context.ProjectionMatrix * context.ViewMatrix;
                glm::vec4 rows[4] = { vp[0], vp[1], vp[2], vp[3] };
                fpc.planes[0] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[0]), rows[3].w + rows[0].w));
                fpc.planes[1] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[0]), rows[3].w - rows[0].w));
                fpc.planes[2] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[1]), rows[3].w + rows[1].w));
                fpc.planes[3] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[1]), rows[3].w - rows[1].w));
                fpc.planes[4] = glm::normalize(glm::vec4(glm::vec3(rows[3] + rows[2]), rows[3].w + rows[2].w));
                fpc.planes[5] = glm::normalize(glm::vec4(glm::vec3(rows[3] - rows[2]), rows[3].w - rows[2].w));
                fpc.count = instanceCount;
                vkCmdPushConstants(vkCmd, m_Cull_PipelineLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fpc), &fpc);

                uint32_t groupCount = (instanceCount + 63) / 64;
                vkCmdDispatch(vkCmd, groupCount, 1, 1);

                VkBufferMemoryBarrier indirectBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
                indirectBarrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
                    0, nullptr, 1, &indirectBarrier, 0, nullptr);

                // ── Render pass + indirect draw ──
                cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
                vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                cmd.BindPipeline(m_GDRPipeline);
                auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDRPipeline);
                if (gdrPipe) {
                    vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        gdrPipe->GetVulkanPipelineLayout(),
                        2, 1, &m_GDR_DescriptorSets[frameIdx], 0, nullptr);
                }
                vkCmdDrawIndexedIndirect(vkCmd,
                    m_DrawIndirectBuffer->GetBuffer(frameIdx), 0,
                    instanceCount,
                    sizeof(VkDrawIndexedIndirectCommand));
            } else {
                // No instances: still clear GBuffer to prevent ghost rendering
                cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
            }
        }
    }

    cmd.EndRenderPass();
    context.Framebuffers["GBuffer"] = fbo;
}
}
