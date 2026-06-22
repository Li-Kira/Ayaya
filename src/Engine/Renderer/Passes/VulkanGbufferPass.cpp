#include "ayapch.h"
#include "VulkanGbufferPass.hpp"
#include <fstream>
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include "Asset/AssetManager.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"

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
        CleanupHiZ();
        // GDR set=2 layout/pool owned by GDRContext — no need to destroy here
        if (m_Cull_Set3Layout)   vkDestroyDescriptorSetLayout(device, m_Cull_Set3Layout, nullptr);
        if (m_Cull_Set3Pool)     vkDestroyDescriptorPool(device, m_Cull_Set3Pool, nullptr);
        if (m_Cull_DummyLayout)  vkDestroyDescriptorSetLayout(device, m_Cull_DummyLayout, nullptr);
        if (m_Cull_PipelineLayout) vkDestroyPipelineLayout(device, m_Cull_PipelineLayout, nullptr);
        if (m_Cull_Pipeline)     vkDestroyPipeline(device, m_Cull_Pipeline, nullptr);
        if (m_Cull_ShaderModule) vkDestroyShaderModule(device, m_Cull_ShaderModule, nullptr);
    }

    void VulkanGBufferPass::CleanupHiZ() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!context) return;
        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        for (auto& hz : m_HiZFrames) {
            if (hz.sampler) vkDestroySampler(device, hz.sampler, nullptr);
            if (hz.view)    vkDestroyImageView(device, hz.view, nullptr);
            for (auto& v : hz.mipSrcViews) if (v) vkDestroyImageView(device, v, nullptr);
            for (auto& v : hz.mipDstViews) if (v) vkDestroyImageView(device, v, nullptr);
            if (hz.image)   vmaDestroyImage(allocator, hz.image, hz.alloc);
        }
        m_HiZFrames = {};

        if (m_HiZBuildSetLayout) vkDestroyDescriptorSetLayout(device, m_HiZBuildSetLayout, nullptr);
        if (m_HiZBuildPool)      vkDestroyDescriptorPool(device, m_HiZBuildPool, nullptr);
        if (m_HiZBuildLayout)    vkDestroyPipelineLayout(device, m_HiZBuildLayout, nullptr);
        if (m_HiZBuildPipeline)  vkDestroyPipeline(device, m_HiZBuildPipeline, nullptr);
        if (m_HiZBuildShader)    vkDestroyShaderModule(device, m_HiZBuildShader, nullptr);
        if (m_HiZDownsampleShader) vkDestroyShaderModule(device, m_HiZDownsampleShader, nullptr);

        if (m_HiZCullSet4Layout) vkDestroyDescriptorSetLayout(device, m_HiZCullSet4Layout, nullptr);
        if (m_HiZCullSet4Pool)   vkDestroyDescriptorPool(device, m_HiZCullSet4Pool, nullptr);
        if (m_HiZCullLayout)     vkDestroyPipelineLayout(device, m_HiZCullLayout, nullptr);
        if (m_HiZCullPipeline)   vkDestroyPipeline(device, m_HiZCullPipeline, nullptr);
        if (m_HiZCullShader)     vkDestroyShaderModule(device, m_HiZCullShader, nullptr);
    }

    void VulkanGBufferPass::InitHiZResources(VkDevice device, VmaAllocator allocator,
                                              uint32_t framesInFlight, uint32_t vpW, uint32_t vpH) {
        m_HiZViewportW = vpW; m_HiZViewportH = vpH;
        m_HiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(vpW, vpH)))) + 1;

        for (uint32_t i = 0; i < framesInFlight; i++) {
            auto& hz = m_HiZFrames[i];

            VkImageCreateInfo imgCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            imgCI.imageType = VK_IMAGE_TYPE_2D;
            imgCI.format = VK_FORMAT_R32_SFLOAT;
            imgCI.extent = { vpW, vpH, 1 };
            imgCI.mipLevels = m_HiZMipLevels;
            imgCI.arrayLayers = 1;
            imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
            imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imgCI.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            imgCI.samples = VK_SAMPLE_COUNT_1_BIT;

            VmaAllocationCreateInfo allocCI{};
            allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            vmaCreateImage(allocator, &imgCI, &allocCI, &hz.image, &hz.alloc, nullptr);

            VkImageViewCreateInfo viewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewCI.image = hz.image;
            viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewCI.format = VK_FORMAT_R32_SFLOAT;
            viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewCI.subresourceRange.baseMipLevel = 0;
            viewCI.subresourceRange.levelCount = m_HiZMipLevels;
            viewCI.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &viewCI, nullptr, &hz.view);

            VkSamplerCreateInfo sampCI{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            sampCI.magFilter = VK_FILTER_NEAREST;
            sampCI.minFilter = VK_FILTER_NEAREST;
            sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampCI.maxLod = static_cast<float>(m_HiZMipLevels);
            vkCreateSampler(device, &sampCI, nullptr, &hz.sampler);

            // Initialize to 0.0 (near plane) — no occlusion on first use
            auto vkCtx2 = std::dynamic_pointer_cast<VulkanContext>(
                Application::Get().GetWindow().GetContext());
            VkCommandBuffer initCmd = vkCtx2->BeginSingleTimeCommands();
            VkImageMemoryBarrier initBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            initBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            initBarrier.image = hz.image;
            initBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            initBarrier.subresourceRange.levelCount = m_HiZMipLevels;
            initBarrier.subresourceRange.layerCount = 1;
            initBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(initCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &initBarrier);

            VkClearColorValue clearVal{ 0.0f, 0.0f, 0.0f, 0.0f };
            VkImageSubresourceRange clearRange{};
            clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clearRange.levelCount = m_HiZMipLevels;
            clearRange.layerCount = 1;
            vkCmdClearColorImage(initCmd, hz.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &clearVal, 1, &clearRange);

            VkImageMemoryBarrier finalBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            finalBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            finalBarrier.image = hz.image;
            finalBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            finalBarrier.subresourceRange.levelCount = m_HiZMipLevels;
            finalBarrier.subresourceRange.layerCount = 1;
            finalBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(initCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &finalBarrier);

            // Pre-create per-mip image views and descriptor sets for this frame
            hz.mipSrcViews.resize(m_HiZMipLevels);
            hz.mipDstViews.resize(m_HiZMipLevels);
            for (uint32_t mip = 0; mip < m_HiZMipLevels; mip++) {
                VkImageViewCreateInfo mipCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                mipCI.image = hz.image;
                mipCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
                mipCI.format = VK_FORMAT_R32_SFLOAT;
                mipCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                mipCI.subresourceRange.baseMipLevel = mip;
                mipCI.subresourceRange.levelCount = 1;
                mipCI.subresourceRange.layerCount = 1;
                vkCreateImageView(device, &mipCI, nullptr, &hz.mipSrcViews[mip]);

                // DST views are the same as SRC — just separate view objects for clarity
                VkImageViewCreateInfo dstCI = mipCI;
                vkCreateImageView(device, &dstCI, nullptr, &hz.mipDstViews[mip]);
            }

            vkCtx2->EndSingleTimeCommands(initCmd);
        }

        // ── Hi-Z Build pipeline (copy + downsample) ──
        {
            // Set0 layout: binding0 = sampler2D (source), binding1 = storage image (destination)
            VkDescriptorSetLayoutBinding sBind[2]{};
            sBind[0].binding = 0; sBind[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            sBind[0].descriptorCount = 1; sBind[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            sBind[1].binding = 1; sBind[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            sBind[1].descriptorCount = 1; sBind[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo sLCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            sLCI.bindingCount = 2; sLCI.pBindings = sBind;
            vkCreateDescriptorSetLayout(device, &sLCI, nullptr, &m_HiZBuildSetLayout);

            // Pool: m_HiZMipLevels sets × framesInFlight (one per mip per FIF slot)
            uint32_t totalSets = m_HiZMipLevels * framesInFlight;
            VkDescriptorPoolSize poolSz[2]{};
            poolSz[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSz[0].descriptorCount = totalSets;
            poolSz[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; poolSz[1].descriptorCount = totalSets;
            VkDescriptorPoolCreateInfo pCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pCI.maxSets = totalSets; pCI.poolSizeCount = 2; pCI.pPoolSizes = poolSz;
            vkCreateDescriptorPool(device, &pCI, nullptr, &m_HiZBuildPool);

            // Load build shaders
            auto loadSPV = [&](const char* relPath) -> VkShaderModule {
                auto exePath = std::filesystem::current_path();
                std::string spvPath = (exePath / relPath).string();
                std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
                if (!file.is_open()) return VK_NULL_HANDLE;
                size_t sz = (size_t)file.tellg();
                std::vector<char> buf(sz);
                file.seekg(0); file.read(buf.data(), sz);
                VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
                VkShaderModule m;
                vkCreateShaderModule(device, &sm, nullptr, &m);
                return m;
            };
            m_HiZBuildShader = loadSPV("assets/Editor/shaders/cache/vulkan/Deferred/hiz_build.comp.spv");
            m_HiZDownsampleShader = loadSPV("assets/Editor/shaders/cache/vulkan/Deferred/hiz_downsample.comp.spv");

            VkPipelineLayoutCreateInfo plCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plCI.setLayoutCount = 1; plCI.pSetLayouts = &m_HiZBuildSetLayout;
            vkCreatePipelineLayout(device, &plCI, nullptr, &m_HiZBuildLayout);

            if (m_HiZBuildShader) {
                VkComputePipelineCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpCI.stage.module = m_HiZBuildShader;
                cpCI.stage.pName = "main";
                cpCI.layout = m_HiZBuildLayout;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_HiZBuildPipeline);
            }
        }

        // ── Pre-allocate per-mip descriptor sets for each frame-in-flight ──
        for (uint32_t i = 0; i < framesInFlight; i++) {
            auto& hz = m_HiZFrames[i];
            hz.mipDescSets.resize(m_HiZMipLevels);
            std::vector<VkDescriptorSetLayout> layouts(m_HiZMipLevels, m_HiZBuildSetLayout);
            VkDescriptorSetAllocateInfo setAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            setAI.descriptorPool = m_HiZBuildPool;
            setAI.descriptorSetCount = m_HiZMipLevels;
            setAI.pSetLayouts = layouts.data();
            vkAllocateDescriptorSets(device, &setAI, hz.mipDescSets.data());

            // Pre-write static descriptors (level 0 DST, all downsample mips)
            for (uint32_t mip = 0; mip < m_HiZMipLevels; mip++) {
                VkDescriptorImageInfo dstInfo{};
                dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                dstInfo.imageView = hz.mipDstViews[mip];

                VkWriteDescriptorSet dstWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                dstWrite.dstSet = hz.mipDescSets[mip]; dstWrite.dstBinding = 1;
                dstWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                dstWrite.descriptorCount = 1; dstWrite.pImageInfo = &dstInfo;
                vkUpdateDescriptorSets(device, 1, &dstWrite, 0, nullptr);

                if (mip > 0) {
                    // Downsample mips: SRC = previous mip level
                    VkDescriptorImageInfo srcInfo{};
                    srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    srcInfo.imageView = hz.mipSrcViews[mip - 1];
                    srcInfo.sampler = hz.sampler;

                    VkWriteDescriptorSet srcWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    srcWrite.dstSet = hz.mipDescSets[mip]; srcWrite.dstBinding = 0;
                    srcWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    srcWrite.descriptorCount = 1; srcWrite.pImageInfo = &srcInfo;
                    vkUpdateDescriptorSets(device, 1, &srcWrite, 0, nullptr);
                }
                // Level 0 SRC (GBuffer depth) is written per-frame in BuildHiZ
            }
        }

        // ── Hi-Z Cull pipeline (Set0=dummy, Set1=dummy, Set2=GDR, Set3=Indirect, Set4=HiZ) ──
        {
            VkDescriptorSetLayoutBinding hizB{};
            hizB.binding = 0; hizB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            hizB.descriptorCount = 1; hizB.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo hizLCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            hizLCI.bindingCount = 1; hizLCI.pBindings = &hizB;
            vkCreateDescriptorSetLayout(device, &hizLCI, nullptr, &m_HiZCullSet4Layout);

            VkDescriptorPoolSize hizPS{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, framesInFlight };
            VkDescriptorPoolCreateInfo hizPCI{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            hizPCI.maxSets = framesInFlight; hizPCI.poolSizeCount = 1; hizPCI.pPoolSizes = &hizPS;
            vkCreateDescriptorPool(device, &hizPCI, nullptr, &m_HiZCullSet4Pool);

            m_HiZCullSet4s.resize(framesInFlight);
            std::vector<VkDescriptorSetLayout> hizLayouts(framesInFlight, m_HiZCullSet4Layout);
            VkDescriptorSetAllocateInfo hizAI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            hizAI.descriptorPool = m_HiZCullSet4Pool;
            hizAI.descriptorSetCount = framesInFlight;
            hizAI.pSetLayouts = hizLayouts.data();
            vkAllocateDescriptorSets(device, &hizAI, m_HiZCullSet4s.data());

            for (uint32_t i = 0; i < framesInFlight; i++) {
                VkDescriptorImageInfo imgInfo{};
                imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imgInfo.imageView = m_HiZFrames[i].view;
                imgInfo.sampler = m_HiZFrames[i].sampler;
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                w.dstSet = m_HiZCullSet4s[i]; w.dstBinding = 0;
                w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.descriptorCount = 1; w.pImageInfo = &imgInfo;
                vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
            }

            // Load cull_hiz SPIR-V
            auto exePath = std::filesystem::current_path();
            std::string spvPath = (exePath / "assets/Editor/shaders/cache/vulkan/Deferred/cull_hiz.comp.spv").string();
            std::ifstream file(spvPath, std::ios::ate | std::ios::binary);
            if (file.is_open()) {
                size_t sz = (size_t)file.tellg();
                std::vector<char> buf(sz);
                file.seekg(0); file.read(buf.data(), sz); file.close();
                VkShaderModuleCreateInfo sm{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                sm.codeSize = buf.size(); sm.pCode = reinterpret_cast<const uint32_t*>(buf.data());
                vkCreateShaderModule(device, &sm, nullptr, &m_HiZCullShader);
            }

            VkDescriptorSetLayout cullSetLayouts[] = { m_Cull_DummyLayout, m_Cull_DummyLayout,
                m_GDRCtx ? m_GDRCtx->Set2Layout : VK_NULL_HANDLE, m_Cull_Set3Layout, m_HiZCullSet4Layout };
            VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 144 }; // 136 bytes, padded to 144 for alignment
            VkPipelineLayoutCreateInfo plCI{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            plCI.setLayoutCount = 5; plCI.pSetLayouts = cullSetLayouts;
            plCI.pushConstantRangeCount = 1; plCI.pPushConstantRanges = &pcRange;
            vkCreatePipelineLayout(device, &plCI, nullptr, &m_HiZCullLayout);

            if (m_HiZCullShader) {
                VkComputePipelineCreateInfo cpCI{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
                cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                cpCI.stage.module = m_HiZCullShader;
                cpCI.stage.pName = "main";
                cpCI.layout = m_HiZCullLayout;
                vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_HiZCullPipeline);
            }
        }
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

        // ── GPU-Driven Rendering (GDR) — SSBO data is in shared GDRContext ──
        if (!m_GDRCtx) {
            AYAYA_CORE_ERROR("VulkanGBufferPass: GDRContext is null! GPU-Driven rendering disabled.");
        } else {
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
            VulkanPipeline::s_ExtraSetLayouts = { m_GDRCtx->Set2Layout };
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
                m_GDRCtx->Set2Layout, m_Cull_Set3Layout };
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

        // ── Hi-Z Occlusion Culling resources ──
        InitHiZResources(device, vkCtx->GetAllocator(), fiCount,
            refSpec.Width, refSpec.Height);  // initial size; rebuilt on viewport resize

    }

    // (Phase 1-3 retired — replaced by GDR Phase 4)

    void VulkanGBufferPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
    auto fbo = context.GetFramebuffer("GBuffer");
    if (!fbo) return;

    auto* queue = context.RenderQueue;
    // Check GDR instance count (blind submit) rather than RenderQueue (translucent-only now).
    if (!queue || (!m_GDRCtx || m_GDRCtx->InstanceCount == 0)) {
        // Unconditionally clear GBuffer to prevent ghost rendering from previous frame
        cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
        cmd.EndRenderPass();
        return;
    }

    // ── GPU-Driven Rendering — build SSBO data, compute cull, indirect draw ──
    {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());

        // Guard: if context or pipeline is unavailable, clear GBuffer and bail
        if (!vkCtx || !m_GDRPipeline || !m_GDRCtx) {
            cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
            cmd.EndRenderPass();
            context.Framebuffers["GBuffer"] = fbo;
            return;
        }

        {
            uint32_t frameIdx = vkCtx->GetCurrentFrameIndex() % vkCtx->GetFramesInFlight();
            VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
            auto& pool = vkCtx->GetGeometryPool();

            // ── Shared GDR scene data already built by SceneRenderer::RenderScene() ──
            uint32_t instanceCount = m_GDRCtx->InstanceCount;

            if (instanceCount > 0) {
                // ── Host → Device barrier: ensure all CPU writes (SetData, pool uploads) ──
                VkMemoryBarrier hostBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
                hostBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                                           | VK_ACCESS_INDEX_READ_BIT;
                vkCmdPipelineBarrier(vkCmd,
                    VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                        | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
                    1, &hostBarrier, 0, nullptr, 0, nullptr);

                // ── Compute culling (outside render pass) ──
                vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_Cull_Pipeline);
                m_GDRCtx->BindSet2(vkCmd, m_Cull_PipelineLayout,
                    VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
                vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_Cull_PipelineLayout, 3, 1, &m_Cull_Set3Descriptors[frameIdx], 0, nullptr);

                struct FrustumPush { glm::vec4 planes[6]; uint32_t count; uint32_t _pad[3]; } fpc;
                glm::mat4 vp = context.ProjectionMatrix * context.ViewMatrix;
                // Gribb-Hartmann requires ROWS of VP matrix, but glm::mat4[i] returns COLUMNS.
                // Transpose first so vpT[i] gives row i as a vec4.
                glm::mat4 vpT = glm::transpose(vp);
                glm::vec4 rows[4] = { vpT[0], vpT[1], vpT[2], vpT[3] };
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
                indirectBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
                indirectBarrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                    0, nullptr, 1, &indirectBarrier, 0, nullptr);

                // ── Hi-Z Occlusion Culling (temporal: uses previous frame's depth pyramid) ──
                // Skip on frame 0: no previous Hi-Z data available yet
                if (m_HiZFrameCount > 0 && m_HiZCullPipeline != VK_NULL_HANDLE) {
                    uint32_t readIdx = (m_HiZFrameCount - 1) % vkCtx->GetFramesInFlight();

                    vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HiZCullPipeline);
                    m_GDRCtx->BindSet2(vkCmd, m_HiZCullLayout,
                        VK_PIPELINE_BIND_POINT_COMPUTE, frameIdx);
                    vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                        m_HiZCullLayout, 3, 1, &m_Cull_Set3Descriptors[frameIdx], 0, nullptr);
                    vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                        m_HiZCullLayout, 4, 1, &m_HiZCullSet4s[readIdx], 0, nullptr);

                    struct HiZCullPC { glm::mat4 prevView; glm::mat4 prevProj;
                                       uint32_t count; uint32_t mipCount; } hizPC;
                    hizPC.prevView = m_HiZPrevView[readIdx];
                    hizPC.prevProj = m_HiZPrevProj[readIdx];
                    hizPC.count = instanceCount;
                    hizPC.mipCount = m_HiZMipLevels;
                    vkCmdPushConstants(vkCmd, m_HiZCullLayout,
                        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(hizPC), &hizPC);
                    uint32_t hizGroupCount = (instanceCount + 63) / 64;
                    vkCmdDispatch(vkCmd, hizGroupCount, 1, 1);

                    // Compute → Indirect barrier (Hi-Z cull also wrote IndirectBuffer)
                    indirectBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    indirectBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
                    indirectBarrier.buffer = m_DrawIndirectBuffer->GetBuffer(frameIdx);
                    indirectBarrier.size = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0,
                        0, nullptr, 1, &indirectBarrier, 0, nullptr);
                }

                // ── Render pass + indirect draw ──
                cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
                vkCmdBindIndexBuffer(vkCmd, pool.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                cmd.BindPipeline(m_GDRPipeline);
                auto gdrPipe = std::dynamic_pointer_cast<VulkanPipeline>(m_GDRPipeline);
                if (gdrPipe) {
                    vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        gdrPipe->GetVulkanPipelineLayout(),
                        2, 1, &m_GDRCtx->Set2Descriptors[frameIdx], 0, nullptr);
                }
                vkCmdDrawIndexedIndirect(vkCmd,
                    m_DrawIndirectBuffer->GetBuffer(frameIdx), 0,
                    instanceCount,
                    sizeof(VkDrawIndexedIndirectCommand));

                // ── GDR debug stats: indirect draws bypass cmd.DrawIndexed() ──
                if (m_GDRCtx) {
                    context.Stats.DrawCalls += instanceCount;
                    context.Stats.ShaderBinds += 1;
                    context.Stats.TriangleCount += m_GDRCtx->TotalTriangles;
                    context.Stats.VertexCount += m_GDRCtx->TotalVertices;
                    context.RecordAndCheckDrawCall("GBuffer Pass", "GDR (GPU-Driven)",
                        "gbuffer_gdr_bindless", m_GDRCtx->TotalTriangles);
                    cmd.RecordIndirectDraw(instanceCount, m_GDRCtx->TotalTriangles);
                }
            } else {
                // No instances: still clear GBuffer to prevent ghost rendering
                cmd.BeginRenderPass(fbo, true, glm::vec4(0.0f));
            }
        }
    }

    cmd.EndRenderPass();
    context.Framebuffers["GBuffer"] = fbo;

    // ── Build Hi-Z for NEXT frame (MUST be outside render pass) ──
    if (m_GDRCtx && m_GDRCtx->InstanceCount > 0) {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (vkCtx && m_HiZBuildPipeline != VK_NULL_HANDLE) {
            VkCommandBuffer vkCmd = vkCtx->GetCurrentCommandBuffer();
            uint32_t writeIdx = m_HiZFrameCount % vkCtx->GetFramesInFlight();
            auto gbufferFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(fbo);
            BuildHiZ(vkCmd, writeIdx, gbufferFBO);

            m_HiZPrevView[writeIdx] = context.ViewMatrix;
            m_HiZPrevProj[writeIdx] = context.ProjectionMatrix;
            m_HiZFrameCount++;
        }
    }
}

void VulkanGBufferPass::BuildHiZ(VkCommandBuffer vkCmd, uint32_t writeIdx,
                                  std::shared_ptr<VulkanFramebuffer> gbufferFBO) {
    auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(
        Application::Get().GetWindow().GetContext());
    VkDevice device = vkCtx->GetDevice();
    auto& hz = m_HiZFrames[writeIdx];

    // ── Transition GBuffer depth: ATTACHMENT → READ_ONLY for compute sampling ──
    // RenderGraph's InsertTileResolveBarrier skips depth for mixed color+depth textures,
    // so the depth is still in DEPTH_STENCIL_ATTACHMENT_OPTIMAL after EndRenderPass.
    {
        VkImageMemoryBarrier depthBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthBarrier.image = gbufferFBO->GetDepthAttachmentImage();
        depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &depthBarrier);
    }

    // ── Transition Hi-Z image to GENERAL for compute write ──
    VkImageMemoryBarrier preHiZ{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    preHiZ.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    preHiZ.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    preHiZ.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    preHiZ.image = hz.image;
    preHiZ.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    preHiZ.subresourceRange.levelCount = m_HiZMipLevels;
    preHiZ.subresourceRange.layerCount = 1;
    preHiZ.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &preHiZ);

    // ── Step 1: Copy GBuffer depth → Hi-Z level 0 ──
    {
        // Update level-0 descriptor with current frame's depth attachment
        VkDescriptorImageInfo srcInfo{};
        srcInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        srcInfo.imageView = gbufferFBO->GetDepthAttachmentImageView();
        srcInfo.sampler = gbufferFBO->GetSampler();

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = hz.mipDescSets[0]; writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1; writes[0].pImageInfo = &srcInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = hz.mipDescSets[0]; writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1; writes[1].pImageInfo = nullptr;  // pre-written at init

        // Only update binding 0 (depth source changes every frame); binding 1 is static
        vkUpdateDescriptorSets(device, 1, writes, 0, nullptr);

        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_HiZBuildPipeline);
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_HiZBuildLayout, 0, 1, &hz.mipDescSets[0], 0, nullptr);
        vkCmdDispatch(vkCmd, (m_HiZViewportW + 15) / 16, (m_HiZViewportH + 15) / 16, 1);
    }

    // ── Step 2: Downsample chain (mip 1 → mip N-1) using pre-created views/descs ──
    uint32_t mipW = m_HiZViewportW, mipH = m_HiZViewportH;
    for (uint32_t mip = 1; mip < m_HiZMipLevels; mip++) {
        uint32_t dstW = mipW > 1 ? mipW / 2 : 1;
        uint32_t dstH = mipH > 1 ? mipH / 2 : 1;

        VkImageMemoryBarrier srcBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        srcBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        srcBarrier.image = hz.image;
        srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        srcBarrier.subresourceRange.baseMipLevel = mip - 1;
        srcBarrier.subresourceRange.levelCount = 1;
        srcBarrier.subresourceRange.layerCount = 1;
        srcBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &srcBarrier);

        // Pre-created descriptor sets — no allocation per frame
        vkCmdBindDescriptorSets(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_HiZBuildLayout, 0, 1, &hz.mipDescSets[mip], 0, nullptr);
        vkCmdDispatch(vkCmd, (dstW + 15) / 16, (dstH + 15) / 16, 1);

        mipW = dstW; mipH = dstH;
    }

    // ── Final barrier: Hi-Z → SHADER_READ_ONLY for next frame's cull ──
    VkImageMemoryBarrier finalHiZ{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    finalHiZ.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    finalHiZ.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    finalHiZ.image = hz.image;
    finalHiZ.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    finalHiZ.subresourceRange.levelCount = m_HiZMipLevels;
    finalHiZ.subresourceRange.layerCount = 1;
    finalHiZ.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalHiZ.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &finalHiZ);

    // ── Restore GBuffer depth to ATTACHMENT layout for downstream RenderGraph passes ──
    // The RenderGraph tracks DepthLayout as ATTACHMENT_OPTIMAL after GBufferPass.
    // Since we transitioned it to READ_ONLY for our compute read, we must put it back.
    {
        VkImageMemoryBarrier restoreBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        restoreBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        restoreBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        restoreBarrier.image = gbufferFBO->GetDepthAttachmentImage();
        restoreBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        restoreBarrier.subresourceRange.baseMipLevel = 0;
        restoreBarrier.subresourceRange.levelCount = 1;
        restoreBarrier.subresourceRange.baseArrayLayer = 0;
        restoreBarrier.subresourceRange.layerCount = 1;
        restoreBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        restoreBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(vkCmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
            0, nullptr, 0, nullptr, 1, &restoreBarrier);
    }
}
}
