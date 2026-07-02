#include "ayapch.h"
#include "VulkanLightingPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/TextureCube.hpp"
#include "Asset/AssetManager.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    void VulkanLightingPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("GBuffer");
        builder.ReadTexture("SceneDepth");
        builder.ReadTexture("ShadowMap");
        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        builder.WriteTexture("Lighting", s);
    }

    VulkanLightingPass::~VulkanLightingPass() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (vkCtx) {
            VkDevice device = vkCtx->GetDevice();
            if (m_Set2Layout)    { vkDestroyDescriptorSetLayout(device, m_Set2Layout, nullptr);    m_Set2Layout = VK_NULL_HANDLE; }
            if (m_Set2Pool)      { vkDestroyDescriptorPool(device, m_Set2Pool, nullptr);          m_Set2Pool = VK_NULL_HANDLE; }
            if (m_LV_Set2Layout)      { vkDestroyDescriptorSetLayout(device, m_LV_Set2Layout, nullptr); m_LV_Set2Layout = VK_NULL_HANDLE; }
            if (m_LV_Set2Pool)        { vkDestroyDescriptorPool(device, m_LV_Set2Pool, nullptr);       m_LV_Set2Pool = VK_NULL_HANDLE; }
            if (m_SphereRangeBuffer)  { vmaDestroyBuffer(vkCtx->GetAllocator(), m_SphereRangeBuffer, m_SphereRangeAlloc); m_SphereRangeBuffer = VK_NULL_HANDLE; }
        }
    }

    void VulkanLightingPass::OnAttach() {
        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        m_RefFBO = Framebuffer::Create(ref);

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        // ── 1. Deferred fullscreen pipeline (directional + ambient) ──
        bool hasHWPCF = vkCtx->GetCapabilities().HasHardwarePCF;
        std::string fragName = hasHWPCF ? "Deferred/deferred_lighting.frag" : "Deferred/deferred_lighting_nohwpc.frag";
        m_DeferredShader = Shader::Create("Deferred/deferred_lighting.vert", fragName);
        m_DeferredPipeSpec.Shader = m_DeferredShader; m_DeferredPipeSpec.Layout = {};
        m_DeferredPipeSpec.Topology = PrimitiveTopology::TriangleStrip;
        m_DeferredPipeSpec.TargetFramebuffer = m_RefFBO;
        m_DeferredPipeSpec.DepthTest = true; m_DeferredPipeSpec.DepthWrite = true;
        m_DeferredPipeSpec.Blend = false; m_DeferredPipeSpec.BackfaceCulling = CullMode::None;

        // Set 2 for deferred pass: PointLight SSBO (b=0)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0; binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1; binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lci.bindingCount = 1; lci.pBindings = &binding;
            vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_Set2Layout);

            VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fiCount };
            VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pci.maxSets = fiCount; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
            vkCreateDescriptorPool(device, &pci, nullptr, &m_Set2Pool);

            m_Set2Descriptors.resize(fiCount);
            std::vector<VkDescriptorSetLayout> layouts(fiCount, m_Set2Layout);
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = m_Set2Pool; ai.descriptorSetCount = fiCount;
            ai.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &ai, m_Set2Descriptors.data());

            VulkanPipeline::s_ExtraSetLayouts.push_back(m_Set2Layout);
        }
        m_DeferredPipeline = Pipeline::Create(m_DeferredPipeSpec);

        // ── 2. Light Volume pipeline (instanced sphere, additive) ──
        m_SphereMesh = Mesh::CreateSphere(1.0f, 16, 16);
        // Upload sphere to geometry pool (shared, cached)
        auto& geoPool = vkCtx->GetGeometryPool();
        auto sphereRange = geoPool.GetOrUploadMesh(m_SphereMesh.get());
        m_SphereIndexCount  = sphereRange.indexCount;
        m_SphereIndexOffset = sphereRange.indexOffset;
        m_SphereVertexOffset = sphereRange.vertexOffset;

        // Create dedicated sphere range SSBO (single GeometryRange, 16 bytes)
        {
            VkBufferCreateInfo bufInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufInfo.size = sizeof(GeometryRange); // 16 bytes
            bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            vmaCreateBuffer(vkCtx->GetAllocator(), &bufInfo, &allocInfo,
                &m_SphereRangeBuffer, &m_SphereRangeAlloc, nullptr);
            // Upload range data
            void* mapped;
            vmaMapMemory(vkCtx->GetAllocator(), m_SphereRangeAlloc, &mapped);
            memcpy(mapped, &sphereRange, sizeof(GeometryRange));
            vmaUnmapMemory(vkCtx->GetAllocator(), m_SphereRangeAlloc);
        }

        m_LightVolumeShader = Shader::Create("Deferred/light_volume.vert", "Deferred/light_volume.frag");

        if (m_LightVolumeShader) {
            PipelineSpecification lvSpec;
            lvSpec.Shader = m_LightVolumeShader;
            lvSpec.Layout = {};  // SSBO vertex pulling (no VBO vertex attributes)
            lvSpec.Topology = PrimitiveTopology::Triangles;
            lvSpec.TargetFramebuffer = m_RefFBO;
            // CullMode::Front is CORRECT here — not a bug.
            // The vulkanCorrection matrix (Y-flip) inverts the projected winding:
            //   world-space CCW → screen-space CW (after Y-flip)
            // CullMode::Back would cull all visible faces. CullMode::Front keeps them.
            // No depth test — fragment shader distance check (d2 > lr*lr) handles culling.
            lvSpec.DepthTest = false;
            lvSpec.DepthWrite = false;
            lvSpec.Blend = true;
            lvSpec.BlendMode = BlendModeType::Additive;
            lvSpec.BackfaceCulling = CullMode::Front;
            lvSpec.NoGlobalUBOs = false;
            lvSpec.NoTextureDescriptors = false;

            // Set 2 for light volume: InstanceSSBO(b=0,V) + LightSSBO(b=1,F) + GeoPool(b=2,V) + GeoRanges(b=3,V)
            VkDescriptorSetLayoutBinding lvBindings[4] = {};
            lvBindings[0].binding = 0; lvBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lvBindings[0].descriptorCount = 1; lvBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            lvBindings[1].binding = 1; lvBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lvBindings[1].descriptorCount = 1; lvBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lvBindings[2].binding = 2; lvBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lvBindings[2].descriptorCount = 1; lvBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            lvBindings[3].binding = 3; lvBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lvBindings[3].descriptorCount = 1; lvBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            VkDescriptorSetLayoutCreateInfo lvLCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lvLCI.bindingCount = 4; lvLCI.pBindings = lvBindings;

            // Keep Set 2 layout alive and pre-allocate descriptor sets
            vkCreateDescriptorSetLayout(device, &lvLCI, nullptr, &m_LV_Set2Layout);
            VulkanPipeline::s_ExtraSetLayouts.push_back(m_LV_Set2Layout);
            m_LightVolumePipeline = Pipeline::Create(lvSpec);

            // Pre-allocate 3 descriptor sets (one per frame-in-flight, 4 SSBO bindings each)
            VkDescriptorPoolSize lvPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, fiCount * 4 };
            VkDescriptorPoolCreateInfo lvPCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            lvPCI.maxSets = fiCount; lvPCI.poolSizeCount = 1; lvPCI.pPoolSizes = &lvPoolSize;
            vkCreateDescriptorPool(device, &lvPCI, nullptr, &m_LV_Set2Pool);

            m_LV_Set2Descriptors.resize(fiCount);
            std::vector<VkDescriptorSetLayout> lvLayouts(fiCount, m_LV_Set2Layout);
            VkDescriptorSetAllocateInfo lvAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            lvAI.descriptorPool = m_LV_Set2Pool; lvAI.descriptorSetCount = fiCount;
            lvAI.pSetLayouts = lvLayouts.data();
            vkAllocateDescriptorSets(device, &lvAI, m_LV_Set2Descriptors.data());
        }
    }

    void VulkanLightingPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto gbufferFBO = context.GetFramebuffer("GBuffer");
        auto sceneDepthFBO = context.GetFramebuffer("SceneDepth");
        auto lightingFBO = context.GetFramebuffer("Lighting");
        if (!gbufferFBO || !sceneDepthFBO || !lightingFBO) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        VkDevice device = vkCtx->GetDevice();
        uint32_t fi = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();

        int lightCount = context.Get<int>("PointLightCount", 0);
        uint64_t ptSSBO = context.Get<uint64_t>("PointLightSSBO", 0);
        uint64_t instSSBO = context.Get<uint64_t>("InstanceSSBO", 0);

        // ── Single RenderPass: directional CLEAR → point light spheres ADDITIVE ──
        cmd.BeginRenderPass(lightingFBO, true, glm::vec4(0.0f));

        // =====================================================================
        // Phase 1: Directional + Ambient + IBL (fullscreen triangle)
        // =====================================================================
        cmd.BindPipeline(m_DeferredPipeline);
        cmd.BindTexture2D(m_DeferredPipeline, "u_DepthMap",   0, sceneDepthFBO, 0, true);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Albedo",     1, gbufferFBO, 1);
        cmd.BindTexture2D(m_DeferredPipeline, "g_PBR",        2, gbufferFBO, 2);
        cmd.BindTexture2D(m_DeferredPipeline, "g_CustomData", 3, gbufferFBO, 3);
        cmd.BindTexture2D(m_DeferredPipeline, "g_Normal",     4, gbufferFBO, 0);

        auto irrMap = context.Get<std::shared_ptr<TextureCube>>("IrradianceMap");
        auto preMap = context.Get<std::shared_ptr<TextureCube>>("PrefilterMap");
        cmd.BindTextureCube(m_DeferredPipeline, "u_IrradianceMap", 8, irrMap);
        cmd.BindTextureCube(m_DeferredPipeline, "u_PrefilteredMap", 9, preMap);
        auto brdf = context.GetTexture("BRDFLUT");
        auto whiteTex = context.GetTexture("WhiteTexture");
        if (brdf) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, brdf);
        else if (whiteTex) cmd.BindTexture2D(m_DeferredPipeline, "u_BRDFLUT", 10, whiteTex);

        auto shadowFBO = context.GetFramebuffer("ShadowMap");
        if (shadowFBO) cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, shadowFBO, 0, true);
        else if (whiteTex) cmd.BindTexture2D(m_DeferredPipeline, "u_ShadowMap", 5, whiteTex);

        bool enableSSAO = context.Get<bool>("EnableSSAO", false);
        auto ssaoFBO = context.GetFramebuffer("SSAO_Final");
        if (enableSSAO && ssaoFBO) cmd.BindTexture2D(m_DeferredPipeline, "u_SSAO", 11, ssaoFBO, 0);
        else if (whiteTex) cmd.BindTexture2D(m_DeferredPipeline, "u_SSAO", 11, whiteTex);

        DeferredLightingPushConstants defPC{};
        defPC.LightSpaceMatrix = context.Get<glm::mat4>("LightSpaceMatrix", glm::mat4(1.0f));
        defPC.AmbientColor = context.Get<glm::vec3>("EnvironmentAmbientColor", glm::vec3(0.1f));
        defPC.Intensity = context.Get<float>("EnvironmentIntensity", 1.0f);
        defPC.EnvMapEnabled = (irrMap && preMap) ? 1 : 0;
        defPC.EnableSSAO = (enableSSAO && ssaoFBO != nullptr) ? 1 : 0;
        defPC.InverseViewProj = glm::inverse(context.ProjectionMatrix * context.ViewMatrix);
        cmd.PushConstantData(m_DeferredPipeline, &defPC, sizeof(DeferredLightingPushConstants));

        // Bind Set 2: PointLight SSBO (for deferred pass fullscreen lighting)
        if (m_Set2Layout && ptSSBO) {
            auto vkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_DeferredPipeline);
            if (vkPipe && fi < m_Set2Descriptors.size()) {
                VkDescriptorBufferInfo info{ (VkBuffer)ptSSBO, 0, VK_WHOLE_SIZE };
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                w.dstSet = m_Set2Descriptors[fi]; w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.descriptorCount = 1; w.pBufferInfo = &info;
                vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vkPipe->GetVulkanPipelineLayout(), 2, 1, &m_Set2Descriptors[fi], 0, nullptr);
            }
        }
        cmd.DrawArrays(3);

        // =====================================================================
        // Phase 2: Point Lights (instanced spheres, additive blend)
        // =====================================================================
        if (lightCount > 0 && m_LightVolumePipeline && ptSSBO && instSSBO) {
            cmd.BindPipeline(m_LightVolumePipeline);
            auto vkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_LightVolumePipeline);
            if (vkPipe) {
                VkPipelineLayout layout = vkPipe->GetVulkanPipelineLayout();

                // Bind Set 1: GBuffer textures (manual, same as deferred pass)
                cmd.BindTexture2D(m_LightVolumePipeline, "u_DepthMap",   0, sceneDepthFBO, 0, true);
                cmd.BindTexture2D(m_LightVolumePipeline, "g_Albedo",     1, gbufferFBO, 1);
                cmd.BindTexture2D(m_LightVolumePipeline, "g_PBR",        2, gbufferFBO, 2);
                cmd.BindTexture2D(m_LightVolumePipeline, "g_CustomData", 3, gbufferFBO, 3);
                cmd.BindTexture2D(m_LightVolumePipeline, "g_Normal",     4, gbufferFBO, 0);

                // Sphere mesh pre-uploaded in OnAttach

                // Bind Set 2: 4 SSBOs (b0=Instance, b1=Light, b2=GeoPool, b3=SphereRange)
                VkBuffer geoBuf = vkCtx->GetGeometryPool().GetBuffer();
                VkDescriptorBufferInfo s2info[4] = {
                    { (VkBuffer)instSSBO,          0, VK_WHOLE_SIZE },
                    { (VkBuffer)ptSSBO,            0, VK_WHOLE_SIZE },
                    { geoBuf,                      0, VK_WHOLE_SIZE },
                    { m_SphereRangeBuffer,         0, VK_WHOLE_SIZE },
                };
                VkWriteDescriptorSet s2writes[4] = {};
                for (int j = 0; j < 4; j++) {
                    s2writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    s2writes[j].dstSet = m_LV_Set2Descriptors[fi];
                    s2writes[j].dstBinding = (uint32_t)j;
                    s2writes[j].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    s2writes[j].descriptorCount = 1;
                    s2writes[j].pBufferInfo = &s2info[j];
                }
                vkUpdateDescriptorSets(device, 4, s2writes, 0, nullptr);
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    layout, 2, 1, &m_LV_Set2Descriptors[fi], 0, nullptr);

                // Push constants: InverseViewProj + ScreenParams
                struct LightVolumePC {
                    glm::mat4 InverseViewProj;
                    glm::vec4 ScreenParams;
                } lvPC;
                lvPC.InverseViewProj = defPC.InverseViewProj;
                float vpW = (float)lightingFBO->GetSpecification().Width;
                float vpH = (float)lightingFBO->GetSpecification().Height;
                lvPC.ScreenParams = glm::vec4(1.0f / vpW, 1.0f / vpH, vpW, vpH);
                vkCmdPushConstants(vkCmd, layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(lvPC), &lvPC);

                // Flush pending Set 1 texture bindings before draw
                cmd.FlushDescriptorSets();

                // Instanced sphere draw (SSBO vertex pulling)
                vkCmdBindIndexBuffer(vkCmd, geoBuf, m_SphereIndexOffset, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(vkCmd, m_SphereIndexCount, (uint32_t)lightCount, 0, 0, 0);
            }
        }

        cmd.EndRenderPass();
        context.Framebuffers["Lighting"] = lightingFBO;
    }
}
