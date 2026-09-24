#include "ayapch.h"
#include "VulkanVolumetricFogPass.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Core/Application.hpp"
#include <fstream>
#include <cmath>

namespace Ayaya {

    void VulkanVolumetricFogPass::DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height) {
        builder.ReadTexture("Lighting");
        builder.ReadTexture("SceneDepth");
        FramebufferSpecification s;
        s.Width = width; s.Height = height; s.Samples = 1;
        s.Attachments = { FramebufferTextureFormat::RGBA16F };
        builder.WriteTexture("Fog", s);
    }

    VkShaderModule VulkanVolumetricFogPass::LoadComputeShader(VkDevice device, const char* relPath) {
        auto exePath = std::filesystem::current_path();
        std::string spvPath = (exePath / relPath).string();
        std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) return VK_NULL_HANDLE;
        size_t sz = (size_t)file.tellg();
        std::vector<char> buf(sz);
        file.seekg(0); file.read(buf.data(), sz); file.close();
        VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
        VkShaderModule m;
        vkCreateShaderModule(device, &sm, nullptr, &m);
        return m;
    }

    void VulkanVolumetricFogPass::CreateVolumes(VkDevice device, VmaAllocator allocator) {
        for (auto& pair : m_Volumes) {
            pair.scattering.Init(device, allocator, kFroxelX, kFroxelY, kFroxelZ, VK_FORMAT_R16G16B16A16_SFLOAT);
            pair.integrated.Init(device, allocator, kFroxelX, kFroxelY, kFroxelZ, VK_FORMAT_R16G16B16A16_SFLOAT);
        }
    }

    void VulkanVolumetricFogPass::ReleaseVolumes() {
        for (auto& pair : m_Volumes) {
            pair.scattering.Release();
            pair.integrated.Release();
        }
    }

    void VulkanVolumetricFogPass::CreateComputeResources(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight) {
        // ── Injection pipeline (Set 0: params SSBO + volume A storage image + point-light SSBO + local-fog SSBO) ──
        {
            VkDescriptorSetLayoutBinding binds[4]{};
            binds[0].binding = 0; binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[0].descriptorCount = 1; binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binds[1].binding = 1; binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binds[1].descriptorCount = 1; binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binds[2].binding = 2; binds[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[2].descriptorCount = 1; binds[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binds[3].binding = 3; binds[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[3].descriptorCount = 1; binds[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lci.bindingCount = 4; lci.pBindings = binds;
            vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_InjectionSetLayout);

            VkDescriptorPoolSize ps[2]{};
            ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[0].descriptorCount = framesInFlight * 3;
            ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[1].descriptorCount = framesInFlight;
            VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pci.maxSets = framesInFlight; pci.poolSizeCount = 2; pci.pPoolSizes = ps;
            vkCreateDescriptorPool(device, &pci, nullptr, &m_InjectionPool);

            std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_InjectionSetLayout);
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = m_InjectionPool; ai.descriptorSetCount = framesInFlight;
            ai.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &ai, m_InjectionSets.data());

            m_InjectionShader = LoadComputeShader(device, "assets/Editor/shaders/cache/vulkan/Volumetric/fog_injection.comp.spv");
            VkPipelineLayoutCreateInfo pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pl.setLayoutCount = 1; pl.pSetLayouts = &m_InjectionSetLayout;
            vkCreatePipelineLayout(device, &pl, nullptr, &m_InjectionLayout);
            if (m_InjectionShader) {
                VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cp.stage.module = m_InjectionShader; cp.stage.pName = "main";
                cp.layout = m_InjectionLayout;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_InjectionPipeline);
            }
        }

        // ── Accumulation pipeline (Set 0: params SSBO + volume A sampler + volume B storage image) ──
        {
            VkDescriptorSetLayoutBinding binds[3]{};
            binds[0].binding = 0; binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[0].descriptorCount = 1; binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binds[1].binding = 1; binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binds[1].descriptorCount = 1; binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            binds[2].binding = 2; binds[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binds[2].descriptorCount = 1; binds[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lci.bindingCount = 3; lci.pBindings = binds;
            vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_AccumSetLayout);

            VkDescriptorPoolSize ps[3]{};
            ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; ps[0].descriptorCount = framesInFlight;
            ps[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; ps[1].descriptorCount = framesInFlight;
            ps[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; ps[2].descriptorCount = framesInFlight;
            VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pci.maxSets = framesInFlight; pci.poolSizeCount = 3; pci.pPoolSizes = ps;
            vkCreateDescriptorPool(device, &pci, nullptr, &m_AccumPool);

            std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_AccumSetLayout);
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = m_AccumPool; ai.descriptorSetCount = framesInFlight;
            ai.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &ai, m_AccumSets.data());

            m_AccumShader = LoadComputeShader(device, "assets/Editor/shaders/cache/vulkan/Volumetric/fog_accumulation.comp.spv");
            VkPipelineLayoutCreateInfo pl{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pl.setLayoutCount = 1; pl.pSetLayouts = &m_AccumSetLayout;
            vkCreatePipelineLayout(device, &pl, nullptr, &m_AccumLayout);
            if (m_AccumShader) {
                VkComputePipelineCreateInfo cp{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cp.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cp.stage.module = m_AccumShader; cp.stage.pName = "main";
                cp.layout = m_AccumLayout;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_AccumPipeline);
            }
        }
    }

    void VulkanVolumetricFogPass::DestroyComputeResources() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = ctx ? ctx->GetDevice() : VK_NULL_HANDLE;
        if (!device) return;
        if (m_InjectionPipeline) vkDestroyPipeline(device, m_InjectionPipeline, nullptr);
        if (m_InjectionLayout)   vkDestroyPipelineLayout(device, m_InjectionLayout, nullptr);
        if (m_InjectionShader)   vkDestroyShaderModule(device, m_InjectionShader, nullptr);
        if (m_InjectionSetLayout) vkDestroyDescriptorSetLayout(device, m_InjectionSetLayout, nullptr);
        if (m_InjectionPool)     vkDestroyDescriptorPool(device, m_InjectionPool, nullptr);
        if (m_AccumPipeline)     vkDestroyPipeline(device, m_AccumPipeline, nullptr);
        if (m_AccumLayout)       vkDestroyPipelineLayout(device, m_AccumLayout, nullptr);
        if (m_AccumShader)       vkDestroyShaderModule(device, m_AccumShader, nullptr);
        if (m_AccumSetLayout)    vkDestroyDescriptorSetLayout(device, m_AccumSetLayout, nullptr);
        if (m_AccumPool)         vkDestroyDescriptorPool(device, m_AccumPool, nullptr);
        m_InjectionPipeline = VK_NULL_HANDLE;
        m_InjectionLayout = VK_NULL_HANDLE;
        m_InjectionShader = VK_NULL_HANDLE;
        m_InjectionSetLayout = VK_NULL_HANDLE;
        m_InjectionPool = VK_NULL_HANDLE;
        m_AccumPipeline = VK_NULL_HANDLE;
        m_AccumLayout = VK_NULL_HANDLE;
        m_AccumShader = VK_NULL_HANDLE;
        m_AccumSetLayout = VK_NULL_HANDLE;
        m_AccumPool = VK_NULL_HANDLE;
    }

    void VulkanVolumetricFogPass::CreateApplyResources(VkDevice device, uint32_t framesInFlight) {
        // Set 2 (sampler3D volume B) — injected into the apply pipeline via s_ExtraSetLayouts.
        {
            VkDescriptorSetLayoutBinding b{};
            b.binding = 0; b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b.descriptorCount = 1; b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            lci.bindingCount = 1; lci.pBindings = &b;
            vkCreateDescriptorSetLayout(device, &lci, nullptr, &m_ApplySet2Layout);

            VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, framesInFlight };
            VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pci.maxSets = framesInFlight; pci.poolSizeCount = 1; pci.pPoolSizes = &ps;
            vkCreateDescriptorPool(device, &pci, nullptr, &m_ApplySet2Pool);

            std::vector<VkDescriptorSetLayout> layouts(framesInFlight, m_ApplySet2Layout);
            VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            ai.descriptorPool = m_ApplySet2Pool; ai.descriptorSetCount = framesInFlight;
            ai.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &ai, m_ApplySet2.data());
        }

        m_ApplyShader = Shader::Create("PostProcess/postprocess.vert", "Volumetric/fog_apply.frag");
        if (!m_ApplyShader) { AYAYA_CORE_ERROR("[VolumetricFog] failed to create apply shader"); return; }

        FramebufferSpecification ref;
        ref.Width = 1280; ref.Height = 720; ref.Samples = 1;
        ref.Attachments = { FramebufferTextureFormat::RGBA16F };
        m_ApplyRefFBO = Framebuffer::Create(ref);

        PipelineSpecification ps;
        ps.Shader = m_ApplyShader; ps.TargetFramebuffer = m_ApplyRefFBO;
        ps.Layout = {}; ps.Topology = PrimitiveTopology::TriangleStrip;
        ps.DepthTest = false; ps.DepthWrite = false; ps.Blend = false;
        ps.BackfaceCulling = CullMode::None;
        VulkanPipeline::s_ExtraSetLayouts.push_back(m_ApplySet2Layout);
        m_ApplyPipeline = Pipeline::Create(ps);
    }

    void VulkanVolumetricFogPass::DestroyApplyResources() {
        auto ctx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = ctx ? ctx->GetDevice() : VK_NULL_HANDLE;
        if (!device) return;
        if (m_ApplySet2Layout) vkDestroyDescriptorSetLayout(device, m_ApplySet2Layout, nullptr);
        if (m_ApplySet2Pool)   vkDestroyDescriptorPool(device, m_ApplySet2Pool, nullptr);
        m_ApplySet2Layout = VK_NULL_HANDLE; m_ApplySet2Pool = VK_NULL_HANDLE;
        m_ApplyPipeline.reset();
        m_ApplyShader.reset();
        m_ApplyRefFBO.reset();
    }

    void VulkanVolumetricFogPass::OnAttach() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        VmaAllocator allocator = vkCtx->GetAllocator();
        uint32_t fiCount = vkCtx->GetFramesInFlight();

        m_ParamsSSBO = std::make_unique<VulkanStorageBuffer>(sizeof(FogUniforms), 0);
        m_DummySSBO = std::make_unique<VulkanStorageBuffer>(64, 0);

        CreateVolumes(device, allocator);
        CreateComputeResources(device, allocator, fiCount);
        CreateApplyResources(device, fiCount);
        RewriteDescriptors(device, fiCount);

        m_Initialized = true;
    }

    void VulkanVolumetricFogPass::RewriteDescriptors(VkDevice device, uint32_t fiCount) {
        for (uint32_t i = 0; i < fiCount; i++) {
            // Injection set: b0 = params SSBO, b1 = volume A storage, b2 = dummy point-light SSBO (updated per frame)
            VkDescriptorBufferInfo paramsInfo{ m_ParamsSSBO->GetBuffer(i), 0, VK_WHOLE_SIZE };
            VkDescriptorImageInfo scatterStore{};
            scatterStore.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            scatterStore.imageView = m_Volumes[i].scattering.GetImageView();
            VkDescriptorBufferInfo dummyInfo{ m_DummySSBO->GetBuffer(i), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet w[4]{};
            w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[0].dstSet = m_InjectionSets[i]; w[0].dstBinding = 0;
            w[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[0].descriptorCount = 1; w[0].pBufferInfo = &paramsInfo;
            w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[1].dstSet = m_InjectionSets[i]; w[1].dstBinding = 1;
            w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[1].descriptorCount = 1; w[1].pImageInfo = &scatterStore;
            w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[2].dstSet = m_InjectionSets[i]; w[2].dstBinding = 2;
            w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[2].descriptorCount = 1; w[2].pBufferInfo = &dummyInfo;
            w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[3].dstSet = m_InjectionSets[i]; w[3].dstBinding = 3;
            w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[3].descriptorCount = 1; w[3].pBufferInfo = &dummyInfo;
            vkUpdateDescriptorSets(device, 4, w, 0, nullptr);

            // Accumulation set: b0 = params SSBO, b1 = volume A sampler, b2 = volume B storage
            VkDescriptorImageInfo scatterSamp{};
            scatterSamp.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            scatterSamp.imageView = m_Volumes[i].scattering.GetImageView();
            scatterSamp.sampler = m_Volumes[i].scattering.GetSampler();
            VkDescriptorImageInfo integStore{};
            integStore.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            integStore.imageView = m_Volumes[i].integrated.GetImageView();
            VkWriteDescriptorSet aw[3]{};
            aw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            aw[0].dstSet = m_AccumSets[i]; aw[0].dstBinding = 0;
            aw[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; aw[0].descriptorCount = 1; aw[0].pBufferInfo = &paramsInfo;
            aw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            aw[1].dstSet = m_AccumSets[i]; aw[1].dstBinding = 1;
            aw[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; aw[1].descriptorCount = 1; aw[1].pImageInfo = &scatterSamp;
            aw[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            aw[2].dstSet = m_AccumSets[i]; aw[2].dstBinding = 2;
            aw[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; aw[2].descriptorCount = 1; aw[2].pImageInfo = &integStore;
            vkUpdateDescriptorSets(device, 3, aw, 0, nullptr);

            // Apply set (Set 2): b0 = volume B sampler
            VkDescriptorImageInfo integSamp{};
            integSamp.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            integSamp.imageView = m_Volumes[i].integrated.GetImageView();
            integSamp.sampler = m_Volumes[i].integrated.GetSampler();
            VkWriteDescriptorSet fw{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            fw.dstSet = m_ApplySet2[i]; fw.dstBinding = 0;
            fw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; fw.descriptorCount = 1; fw.pImageInfo = &integSamp;
            vkUpdateDescriptorSets(device, 1, &fw, 0, nullptr);
        }
    }

    VulkanVolumetricFogPass::~VulkanVolumetricFogPass() {
        ReleaseVolumes();
        DestroyComputeResources();
        DestroyApplyResources();
    }

    void VulkanVolumetricFogPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        auto lightingFBO = context.GetFramebuffer("Lighting");
        auto depthFBO = context.GetFramebuffer("SceneDepth");
        auto fogFBO = context.GetFramebuffer("Fog");
        if (!lightingFBO || !depthFBO || !fogFBO) return;

        uint32_t vpW = fogFBO->GetSpecification().Width;
        uint32_t vpH = fogFBO->GetSpecification().Height;
        if (!vpW || !vpH) return;

        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;
        VkDevice device = vkCtx->GetDevice();
        VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
        uint32_t fi = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();
        if (!m_Initialized) return;

        // ── Resize froxel volumes on viewport change ──
        if (vpW != m_LastW || vpH != m_LastH) {
            m_LastW = vpW; m_LastH = vpH;
            // In-flight frames may still reference the old volumes — wait before destroying.
            vkDeviceWaitIdle(device);
            ReleaseVolumes();
            CreateVolumes(device, vkCtx->GetAllocator());
            RewriteDescriptors(device, vkCtx->GetFramesInFlight());
        }

        // ── Derive near/far from the projection matrix (zero-to-one depth) ──
        glm::mat4 proj = context.ProjectionMatrix;
        float zz = proj[2][2];
        float zw = proj[3][2];
        float nearP = (std::abs(zz) > 1e-6f) ? zw / zz : 0.1f;
        float farP = (std::abs(zz + 1.0f) > 1e-6f) ? zw / (zz + 1.0f) : 1000.0f;
        if (nearP <= 0.0f || !std::isfinite(nearP)) nearP = 0.1f;
        if (farP <= nearP || !std::isfinite(farP)) farP = 1000.0f;

        // ── Upload fog uniforms ──
        FogUniforms u{};
        u.InvViewProj = context.Get<glm::mat4>("InverseViewProj", glm::mat4(1.0f));
        u.CameraPosNear = glm::vec4(context.CameraPosition, nearP);
        u.ScreenFar = glm::vec4((float)vpW, (float)vpH, farP, 0.0f);
        u.DirLightDir = glm::vec4(context.Get<glm::vec3>("DirLightDir", glm::vec3(0.0f, -1.0f, 0.0f)), 0.0f);
        u.DirLightColor = glm::vec4(context.Get<glm::vec3>("DirLightColor", glm::vec3(1.0f)), 0.0f);
        u.Fog = glm::vec4(
            context.Get<float>("FogDensity", 0.0f),   // 0 = fog off (pass-through)
            context.Get<float>("FogHeightFalloff", 0.5f),
            context.Get<float>("FogPhaseG", 0.3f),
            context.Get<float>("FogMaxDistance", 200.0f));
        u.ScatterColor = glm::vec4(context.Get<glm::vec3>("FogScatterColor", glm::vec3(0.6f, 0.7f, 0.8f)), 0.0f);
        int lightCount = context.Get<int>("PointLightCount", 0);
        uint64_t ptSSBO = context.Get<uint64_t>("PointLightSSBO", 0);
        u.PointCount = glm::vec4((float)lightCount, 0.0f, 0.0f, 0.0f);
        u.FogHeight = glm::vec4(context.Get<float>("FogHeight", 0.0f), 0.0f, 0.0f, 0.0f);
        int lfvCount = context.Get<int>("LocalFogVolumeCount", 0);
        uint64_t lfvSSBO = context.Get<uint64_t>("LocalFogVolumeSSBO", 0);
        u.LocalFogCount = glm::vec4((float)lfvCount, 0.0f, 0.0f, 0.0f);
        bool hasFog = (u.Fog.x > 0.0f) || (lfvCount > 0);
        m_ParamsSSBO->SetData(&u, sizeof(u));

        // ── Update point-light SSBO descriptor (binding 2) for the current frame ──
        {
            VkBuffer ptBuf = ptSSBO ? (VkBuffer)ptSSBO : m_DummySSBO->GetBuffer(fi);
            VkDescriptorBufferInfo ptInfo{ ptBuf, 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_InjectionSets[fi]; w.dstBinding = 2;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.descriptorCount = 1; w.pBufferInfo = &ptInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // ── Update local-fog-volume SSBO descriptor (binding 3) ──
        {
            VkBuffer lfvBuf = lfvSSBO ? (VkBuffer)lfvSSBO : m_DummySSBO->GetBuffer(fi);
            VkDescriptorBufferInfo lfvInfo{ lfvBuf, 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_InjectionSets[fi]; w.dstBinding = 3;
            w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.descriptorCount = 1; w.pBufferInfo = &lfvInfo;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // ── 1. Light injection + 2. transmittance integration (compute) ──
        // Skip the compute entirely when there's no fog (density 0 and no local volumes);
        // the apply below then does a pass-through (NoFog). Ensure volume B is still in
        // SHADER_READ_ONLY so the apply's Set 2 descriptor layout matches.
        auto& vol = m_Volumes[fi];
        if (hasFog) {
            vol.scattering.TransitionToGeneral(vkCmd);
            vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_InjectionPipeline);
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_InjectionLayout,
                0, 1, &m_InjectionSets[fi], 0, nullptr);
            vkCmdDispatch(vkCmd, kFroxelX / 8, kFroxelY / 8, kFroxelZ / 8);

            vol.scattering.TransitionToReadOnly(vkCmd);
            vol.integrated.TransitionToGeneral(vkCmd);
            vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_AccumPipeline);
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_AccumLayout,
                0, 1, &m_AccumSets[fi], 0, nullptr);
            vkCmdDispatch(vkCmd, kFroxelX / 8, kFroxelY / 8, 1);
            vol.integrated.TransitionToReadOnly(vkCmd);
        } else {
            vol.integrated.TransitionToReadOnly(vkCmd);
        }

        // ── 3. Composite onto HDR scene color (fullscreen) ──
        cmd.BeginRenderPass(fogFBO, true, glm::vec4(0.0f));
        cmd.BindPipeline(m_ApplyPipeline);
        cmd.BindTexture2D(m_ApplyPipeline, "u_SceneColor", 0, lightingFBO, 0);
        cmd.BindTexture2D(m_ApplyPipeline, "u_SceneDepth", 1, depthFBO, 0, true);
        auto vkPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_ApplyPipeline);
        if (vkPipe) {
            vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                vkPipe->GetVulkanPipelineLayout(), 2, 1, &m_ApplySet2[fi], 0, nullptr);
        }
        struct { alignas(16) glm::vec4 NearFar; } pc;
        pc.NearFar = glm::vec4(nearP, farP, hasFog ? 0.0f : 1.0f, 0.0f);  // .z = NoFog flag
        cmd.PushConstantData(m_ApplyPipeline, &pc, sizeof(pc));
        cmd.DrawArrays(3);
        cmd.EndRenderPass();

        context.Framebuffers["Fog"] = fogFBO;
    }

} // namespace Ayaya
