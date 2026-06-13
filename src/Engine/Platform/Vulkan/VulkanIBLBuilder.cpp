#include "ayapch.h"
#include "VulkanIBLBuilder.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Core/Application.hpp"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>

namespace Ayaya {

    // 内存泄漏追踪器：记录 IBLBuilder 自己创建的原生对象，防止退出时报 VMA 错误
    struct IBLResource { VkImage Image; VmaAllocation Alloc; VkImageView View; };
    static std::vector<IBLResource> s_TrackedIBLResources;
    VkSampler VulkanIBLBuilder::s_SourceCubemapSampler = VK_NULL_HANDLE;

    void VulkanIBLBuilder::SetSourceCubemapSampler(void* sampler) {
        s_SourceCubemapSampler = (VkSampler)sampler;
    }

    void* VulkanIBLBuilder::ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                               const std::shared_ptr<Mesh>& cubeMesh, 
                                                               const std::shared_ptr<Shader>& convertShader) {
        
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        uint32_t dim = 1024;
        VkFormat fbFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

        // ==========================================
        // 1. 创建最终目标的 TextureCube 显存与视图
        // ==========================================
        VkImage cubeImage;
        VmaAllocation cubeAlloc;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = fbFormat;
        imageInfo.extent = { dim, dim, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        // ==========================================
        // 【核心修复】：必须告诉 VMA 怎么分配这块内存，不能传 nullptr！
        // ==========================================
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // 分配在速度最快的纯显存(VRAM)中

        // 把 nullptr 换成 &allocInfo
        vmaCreateImage(allocator, &imageInfo, &allocInfo, &cubeImage, &cubeAlloc, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cubeImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = fbFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        VkImageView cubeView;
        vkCreateImageView(device, &viewInfo, nullptr, &cubeView);

        s_TrackedIBLResources.push_back({cubeImage, cubeAlloc, cubeView});

        // ==========================================
        // 2. 利用引擎 API 创建临时的 2D FBO 和 Pipeline
        // ==========================================
        FramebufferSpecification fboSpec;
        fboSpec.Width = dim; fboSpec.Height = dim;
        fboSpec.Attachments = { FramebufferTextureFormat::RGBA32F };
        auto tempFbo = Framebuffer::Create(fboSpec);
        auto vulkanFbo = std::dynamic_pointer_cast<VulkanFramebuffer>(tempFbo);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = convertShader;
        pipeSpec.TargetFramebuffer = tempFbo;
        pipeSpec.DepthTest = false; 
        // pipeSpec.BackfaceCulling = CullMode::Front; // 我们在盒子内部
        pipeSpec.BackfaceCulling = CullMode::None;
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        auto pipeline = Pipeline::Create(pipeSpec);
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);

        // ==========================================
        // 3. 将 HDR 原图绑定到 Pipeline 的描述符集
        // ==========================================
        VkDescriptorSet descSet = vulkanPipeline->GetNextTextureDescriptorSet();
        auto vkHdrTex = std::dynamic_pointer_cast<VulkanTexture2D>(hdrTexture);

        VkDescriptorImageInfo descImageInfo{};
        descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descImageInfo.imageView = vkHdrTex->GetImageView();
        descImageInfo.sampler = vkHdrTex->GetSampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet;
        write.dstBinding = 0; 
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &descImageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // ==========================================
        // 4. 定义 6 个面的视图矩阵 (engine flipped viewport, no proj Y-flip)
        // ==========================================
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        captureProjection[1][1] *= -1.0f; // + neg-height viewport = net Y-flip vs standard Vulkan
        
        // Proj Y-flip and neg-height viewport are equivalent — both flip Y.
        // Up vectors are unchanged from Vulkan default (neg-Y for ±X/±Z, ±Z for ±Y).
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        struct PushConstants {
            glm::mat4 Proj;
            glm::mat4 View;
            float Roughness;
            float _pad[3];
        } pc;
        pc.Proj = captureProjection;
        pc.Roughness = 0.0f;

        // ==========================================
        // 5. 开启一次性指令，狂轰滥炸 6 遍管线
        // ==========================================
        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        // 5.a 先把目标 Cubemap 调整为 准备接收拷贝 的状态
        VkImageMemoryBarrier cubeBarrier{};
        cubeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.image = cubeImage;
        cubeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cubeBarrier.subresourceRange.baseMipLevel = 0;
        cubeBarrier.subresourceRange.levelCount = 1;
        cubeBarrier.subresourceRange.baseArrayLayer = 0;
        cubeBarrier.subresourceRange.layerCount = 6;
        cubeBarrier.srcAccessMask = 0;
        cubeBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        VkImage fboImage = vulkanFbo->GetColorAttachmentImage(0);

        // ==========================================
        // 提取顶点和索引缓冲 (安全向下转型法)
        // ==========================================
        auto baseVertexBuffer = cubeMesh->GetVertexBuffer();
        auto baseIndexBuffer = cubeMesh->GetIndexBuffer();

        auto vkVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(baseVertexBuffer);
        auto vkIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(baseIndexBuffer);

        if (!vkVertexBuffer || !vkIndexBuffer) {
            AYAYA_CORE_ERROR("VulkanIBLBuilder: Failed to cast Vertex/Index buffer to Vulkan implementation!");
            return 0;
        }

        // 提取原生的 VkBuffer 句柄 (安全的 64 位提取)
        VkBuffer vertexBuffer = vkVertexBuffer->GetVulkanBuffer();
        VkBuffer indexBuffer = vkIndexBuffer->GetVulkanBuffer();

        // Dynamic Rendering: FBO starts in SHADER_READ_ONLY (from Invalidate),
        // transition to COLOR_ATTACHMENT before first vkCmdBeginRendering
        {
            VkImageMemoryBarrier preRender{};
            preRender.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            preRender.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            preRender.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            preRender.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            preRender.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            preRender.image = fboImage;
            preRender.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            preRender.subresourceRange.levelCount = 1;
            preRender.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &preRender);
        }

        for (int i = 0; i < 6; i++) {
            pc.View = captureViews[i];

            // 5.b Dynamic Rendering (no depth attachment for IBL FBO)
            VkRenderingAttachmentInfo colorAttach{};
            colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttach.imageView = vulkanFbo->GetColorAttachmentImageView(0);
            colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttach.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { {0, 0}, {dim, dim} };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttach;

            vkCmdBeginRendering(cmd, &renderingInfo);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 1, 1, &descSet, 0, nullptr);
            vkCmdPushConstants(cmd, vulkanPipeline->GetVulkanPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

            VkViewport viewport{ 0.0f, (float)dim, (float)dim, -(float)dim, 0.0f, 1.0f };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, {dim, dim} };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, cubeMesh->GetIndexCount(), 1, 0, 0, 0);

            vkCmdEndRendering(cmd);

            // 5.c 设置屏障，准备把刚刚画好的 FBO 画面拷贝出去
            VkImageMemoryBarrier copyBarrier{};
            copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // Dynamic rendering leaves image in COLOR_ATTACHMENT_OPTIMAL
            copyBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyBarrier.image = fboImage;
            copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyBarrier.subresourceRange.baseMipLevel = 0;
            copyBarrier.subresourceRange.levelCount = 1;
            copyBarrier.subresourceRange.baseArrayLayer = 0;
            copyBarrier.subresourceRange.layerCount = 1;
            copyBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

            // 5.d 物理拷贝！将 FBO 的画面拷入 Cube 对应的 Layer (i) 中
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.mipLevel = 0;
            copyRegion.srcSubresource.baseArrayLayer = 0;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.mipLevel = 0;
            copyRegion.dstSubresource.baseArrayLayer = i; // 关键：指定飞入哪一层
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent.width = dim;
            copyRegion.extent.height = dim;
            copyRegion.extent.depth = 1;
            vkCmdCopyImage(cmd, fboImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            // 5.e 将 FBO 还原，以便下一次循环渲染
            copyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            copyBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
        }

        // 6. 最终将填满 6 面的 Cubemap 设置为只读模式供给场景采样
        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cubeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        context->EndSingleTimeCommands(cmd);
        vkDeviceWaitIdle(device); // 确保 validation 层完全同步，避免临时 pipeline 析构时 descriptor set 报错
        AYAYA_CORE_INFO("VulkanIBLBuilder: HDR to Cubemap baked successfully!");

        // 返回生成的 ImageView 句柄，外部的 TextureCube 会自动包装它！
        return (void*)cubeView;
    }

    // （其他三个 Stub 方法暂时保留原样，返回 0）
    void* VulkanIBLBuilder::CreateIrradianceMap(void* envCubemap, const std::shared_ptr<Mesh>& cubeMesh, const std::shared_ptr<Shader>& irradianceShader) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        uint32_t dim = 32; // 辐照度图极度模糊，32x32 足够
        VkFormat fbFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

        // 1. 创建最终目标的 TextureCube 显存与视图
        VkImage cubeImage;
        VmaAllocation cubeAlloc;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = fbFormat;
        imageInfo.extent = { dim, dim, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(allocator, &imageInfo, &allocInfo, &cubeImage, &cubeAlloc, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cubeImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = fbFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        VkImageView cubeView;
        vkCreateImageView(device, &viewInfo, nullptr, &cubeView);

        s_TrackedIBLResources.push_back({cubeImage, cubeAlloc, cubeView});

        // 2. 创建临时 FBO 和 Pipeline
        FramebufferSpecification fboSpec;
        fboSpec.Width = dim; fboSpec.Height = dim;
        fboSpec.Attachments = { FramebufferTextureFormat::RGBA32F };
        auto tempFbo = Framebuffer::Create(fboSpec);
        auto vulkanFbo = std::dynamic_pointer_cast<VulkanFramebuffer>(tempFbo);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = irradianceShader;
        pipeSpec.TargetFramebuffer = tempFbo;
        pipeSpec.DepthTest = false; 
        pipeSpec.BackfaceCulling = CullMode::None;
        pipeSpec.DepthWrite = false;
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        auto pipeline = Pipeline::Create(pipeSpec);
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (vulkanPipeline->GetVulkanPipeline() == VK_NULL_HANDLE) {
            AYAYA_CORE_ERROR("VulkanIBLBuilder: Failed to create Pipeline! Check Shader compilation and Pipeline specs.");
            return nullptr;
        }

        // 3. 绑定源环境 Cubemap (使用 TextureCube 的原生采样器，避免临时采样器兼容问题)
        VkDescriptorSet descSet = vulkanPipeline->GetNextTextureDescriptorSet();

        VkDescriptorImageInfo descImageInfo{};
        descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descImageInfo.imageView = (VkImageView)envCubemap;
        descImageInfo.sampler = s_SourceCubemapSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &descImageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // 4. 定义视图矩阵
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        captureProjection[1][1] *= -1.0f; // + neg-height viewport = net Y-flip vs standard Vulkan
        // Proj Y-flip and neg-height viewport are equivalent — both flip Y.
        // Up vectors are unchanged from Vulkan default (neg-Y for ±X/±Z, ±Z for ±Y).
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        struct PushConstants {
            glm::mat4 Proj; glm::mat4 View; float Roughness; float _pad[3];
        } pc;
        pc.Proj = captureProjection; pc.Roughness = 0.0f;

        // 5. 渲染 6 个面
        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier cubeBarrier{};
        cubeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.image = cubeImage;
        cubeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cubeBarrier.subresourceRange.levelCount = 1;
        cubeBarrier.subresourceRange.layerCount = 6;
        cubeBarrier.srcAccessMask = 0;
        cubeBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        VkImage fboImage = vulkanFbo->GetColorAttachmentImage(0);

        auto vkVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(cubeMesh->GetVertexBuffer());
        auto vkIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(cubeMesh->GetIndexBuffer());
        VkBuffer vertexBuffer = vkVertexBuffer->GetVulkanBuffer();
        VkBuffer indexBuffer = vkIndexBuffer->GetVulkanBuffer();

        // Dynamic Rendering: FBO starts in SHADER_READ_ONLY (from Invalidate),
        // transition to COLOR_ATTACHMENT before first vkCmdBeginRendering
        {
            VkImageMemoryBarrier preRender{};
            preRender.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            preRender.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            preRender.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            preRender.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            preRender.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            preRender.image = fboImage;
            preRender.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            preRender.subresourceRange.levelCount = 1;
            preRender.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &preRender);
        }

        for (int i = 0; i < 6; i++) {
            pc.View = captureViews[i];

            VkRenderingAttachmentInfo colorAttach{};
            colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttach.imageView = vulkanFbo->GetColorAttachmentImageView(0);
            colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttach.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = { {0, 0}, {dim, dim} };
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttach;

            vkCmdBeginRendering(cmd, &renderingInfo);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 1, 1, &descSet, 0, nullptr);
            vkCmdPushConstants(cmd, vulkanPipeline->GetVulkanPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

            VkViewport viewport{ 0.0f, (float)dim, (float)dim, -(float)dim, 0.0f, 1.0f };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, {dim, dim} };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, cubeMesh->GetIndexCount(), 1, 0, 0, 0);

            vkCmdEndRendering(cmd);

            VkImageMemoryBarrier copyBarrier{};
            copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // Dynamic rendering leaves image in COLOR_ATTACHMENT_OPTIMAL
            copyBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyBarrier.image = fboImage;
            copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyBarrier.subresourceRange.levelCount = 1;
            copyBarrier.subresourceRange.layerCount = 1;
            copyBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

            VkImageCopy copyRegion{};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.baseArrayLayer = i;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent = { dim, dim, 1 };
            vkCmdCopyImage(cmd, fboImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            copyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            copyBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
        }

        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cubeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        context->EndSingleTimeCommands(cmd);
        vkDeviceWaitIdle(device);
        AYAYA_CORE_INFO("VulkanIBLBuilder: Irradiance Map convoluted successfully!");
        return (void*)cubeView;
    }

    void* VulkanIBLBuilder::CreatePrefilterMap(void* envCubemap, const std::shared_ptr<Mesh>& cubeMesh, const std::shared_ptr<Shader>& prefilterShader) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        uint32_t dim = 128;
        uint32_t maxMipLevels = 5;
        VkFormat fbFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

        // 1. 创建包含 5 级 Mipmap 的目标 TextureCube
        VkImage cubeImage;
        VmaAllocation cubeAlloc;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = fbFormat;
        imageInfo.extent = { dim, dim, 1 };
        imageInfo.mipLevels = maxMipLevels; // 【关键】：开启多级 Mipmap
        imageInfo.arrayLayers = 6;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(allocator, &imageInfo, &allocInfo, &cubeImage, &cubeAlloc, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cubeImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = fbFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = maxMipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        VkImageView cubeView;
        vkCreateImageView(device, &viewInfo, nullptr, &cubeView);

        s_TrackedIBLResources.push_back({cubeImage, cubeAlloc, cubeView});

        // 2. 创建临时 FBO 管线 (以最高分辨率 128x128 创建即可)
        FramebufferSpecification fboSpec;
        fboSpec.Width = dim; fboSpec.Height = dim;
        fboSpec.Attachments = { FramebufferTextureFormat::RGBA32F };
        auto tempFbo = Framebuffer::Create(fboSpec);
        auto vulkanFbo = std::dynamic_pointer_cast<VulkanFramebuffer>(tempFbo);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = prefilterShader;
        pipeSpec.TargetFramebuffer = tempFbo;
        pipeSpec.DepthTest = false; 
        pipeSpec.BackfaceCulling = CullMode::None;
        pipeSpec.DepthWrite = false;
        pipeSpec.Layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float3, "a_Tangent" }
        };
        auto pipeline = Pipeline::Create(pipeSpec);
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (vulkanPipeline->GetVulkanPipeline() == VK_NULL_HANDLE) {
            AYAYA_CORE_ERROR("VulkanIBLBuilder: Failed to create Pipeline! Check Shader compilation and Pipeline specs.");
            return nullptr;
        }

        // 3. 绑定源环境 Cubemap (使用 TextureCube 的原生采样器)
        VkDescriptorSet descSet = vulkanPipeline->GetNextTextureDescriptorSet();

        VkDescriptorImageInfo descImageInfo{};
        descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descImageInfo.imageView = (VkImageView)envCubemap; descImageInfo.sampler = s_SourceCubemapSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet; write.dstBinding = 0; 
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1; write.pImageInfo = &descImageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // 4. 定义视图矩阵
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        captureProjection[1][1] *= -1.0f; // + neg-height viewport = net Y-flip vs standard Vulkan
        // Proj Y-flip and neg-height viewport are equivalent — both flip Y.
        // Up vectors are unchanged from Vulkan default (neg-Y for ±X/±Z, ±Z for ±Y).
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        struct PushConstants {
            glm::mat4 Proj; glm::mat4 View; float Roughness; float _pad[3];
        } pc;
        pc.Proj = captureProjection;

        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier cubeBarrier{};
        cubeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.image = cubeImage;
        cubeBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cubeBarrier.subresourceRange.levelCount = maxMipLevels; // 关键：转换所有层级
        cubeBarrier.subresourceRange.layerCount = 6;
        cubeBarrier.srcAccessMask = 0;
        cubeBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        VkImage fboImage = vulkanFbo->GetColorAttachmentImage(0);

        auto vkVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(cubeMesh->GetVertexBuffer());
        auto vkIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(cubeMesh->GetIndexBuffer());
        VkBuffer vertexBuffer = vkVertexBuffer->GetVulkanBuffer();
        VkBuffer indexBuffer = vkIndexBuffer->GetVulkanBuffer();

        // Dynamic Rendering: FBO starts in SHADER_READ_ONLY (from Invalidate),
        // transition to COLOR_ATTACHMENT before first vkCmdBeginRendering
        {
            VkImageMemoryBarrier preRender{};
            preRender.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            preRender.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            preRender.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            preRender.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            preRender.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            preRender.image = fboImage;
            preRender.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            preRender.subresourceRange.levelCount = 1;
            preRender.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &preRender);
        }

        // 5. 【核心逻辑】：双重循环，外层 Mipmap，内层 6 个面
        for (uint32_t mip = 0; mip < maxMipLevels; ++mip) {
            uint32_t mipWidth  = static_cast<uint32_t>(dim * std::pow(0.5, mip));
            uint32_t mipHeight = static_cast<uint32_t>(dim * std::pow(0.5, mip));
            
            // 通过 Push Constant 告诉 Shader 当前的粗糙度
            pc.Roughness = (float)mip / (float)(maxMipLevels - 1);

            for (int i = 0; i < 6; i++) {
                pc.View = captureViews[i];


                VkRenderingAttachmentInfo colorAttach{};
                colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttach.imageView = vulkanFbo->GetColorAttachmentImageView(0);
                colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                colorAttach.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

                VkRenderingInfo renderingInfo{};
                renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                renderingInfo.renderArea = { {0, 0}, {dim, dim} }; // FBO 本身大小不变
                renderingInfo.layerCount = 1;
                renderingInfo.colorAttachmentCount = 1;
                renderingInfo.pColorAttachments = &colorAttach;

                vkCmdBeginRendering(cmd, &renderingInfo);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 1, 1, &descSet, 0, nullptr);
                vkCmdPushConstants(cmd, vulkanPipeline->GetVulkanPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);

                // 【绝妙技巧】：通过动态改变视口大小，将低分辨率图像渲染到大 FBO 的左下角！
                VkViewport viewport{ 0.0f, (float)mipHeight, (float)mipWidth, -(float)mipHeight, 0.0f, 1.0f };
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{ {0, 0}, {mipWidth, mipHeight} };
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
                vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, cubeMesh->GetIndexCount(), 1, 0, 0, 0);

                vkCmdEndRendering(cmd);

                // 拷贝逻辑：只拷贝 FBO 左下角我们刚画好的那个极小区域
                VkImageMemoryBarrier copyBarrier{};
                copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                // Dynamic rendering leaves image in COLOR_ATTACHMENT_OPTIMAL
                copyBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                copyBarrier.image = fboImage;
                copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyBarrier.subresourceRange.levelCount = 1;
                copyBarrier.subresourceRange.layerCount = 1;
                copyBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

                VkImageCopy copyRegion{};
                copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.srcSubresource.layerCount = 1;
                copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.dstSubresource.mipLevel = mip;     // 【关键】：放入目标的对应 mip 坑位
                copyRegion.dstSubresource.baseArrayLayer = i;
                copyRegion.dstSubresource.layerCount = 1;
                copyRegion.extent = { mipWidth, mipHeight, 1 }; // 【关键】：只拷贝有效数据大小
                vkCmdCopyImage(cmd, fboImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

                copyBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                copyBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                copyBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
            }
        }

        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cubeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        context->EndSingleTimeCommands(cmd);
        vkDeviceWaitIdle(device);
        AYAYA_CORE_INFO("VulkanIBLBuilder: Prefilter Map generated successfully!");
        return (void*)cubeView;
    }

    void* VulkanIBLBuilder::CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, void* emptyVAO) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        uint32_t dim = 512;
        // Mac 完美支持 R16G16 的浮点数格式，对应 Vulkan 的 R16G16_SFLOAT
        VkFormat fbFormat = VK_FORMAT_R16G16_SFLOAT;

        // 1. 创建最终目标的 2D 显存与视图
        VkImage lutImage;
        VmaAllocation lutAlloc;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = fbFormat;
        imageInfo.extent = { dim, dim, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(allocator, &imageInfo, &allocInfo, &lutImage, &lutAlloc, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = lutImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // 只是普通的 2D 图
        viewInfo.format = fbFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView lutView;
        vkCreateImageView(device, &viewInfo, nullptr, &lutView);

        s_TrackedIBLResources.push_back({lutImage, lutAlloc, lutView});

        // 2. 创建临时 FBO 和 Pipeline
        FramebufferSpecification fboSpec;
        fboSpec.Width = dim; fboSpec.Height = dim;
        // 注意，FBO 也得是两通道的浮点数格式
        fboSpec.Attachments = { FramebufferTextureFormat::RG16F };
        auto tempFbo = Framebuffer::Create(fboSpec);
        auto vulkanFbo = std::dynamic_pointer_cast<VulkanFramebuffer>(tempFbo);

        PipelineSpecification pipeSpec;
        pipeSpec.Shader = brdfShader;
        pipeSpec.TargetFramebuffer = tempFbo;
        pipeSpec.DepthTest = false; 
        pipeSpec.BackfaceCulling = CullMode::None;
        pipeSpec.DepthWrite = false;
        
        // 【核心魔法】：空输入！完全依赖 brdf.vert 里的 gl_VertexIndex 虚空生成全屏三角形！
        pipeSpec.Layout = {}; 
        
        auto pipeline = Pipeline::Create(pipeSpec);
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        if (vulkanPipeline->GetVulkanPipeline() == VK_NULL_HANDLE) {
            AYAYA_CORE_ERROR("VulkanIBLBuilder: Failed to create Pipeline! Check Shader compilation and Pipeline specs.");
            return nullptr;
        }

        // 3. 开始渲染！
        VkCommandBuffer cmd = context->BeginSingleTimeCommands();

        VkImageMemoryBarrier lutBarrier{};
        lutBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        lutBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        lutBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        lutBarrier.image = lutImage;
        lutBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        lutBarrier.subresourceRange.levelCount = 1;
        lutBarrier.subresourceRange.layerCount = 1;
        lutBarrier.srcAccessMask = 0;
        lutBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &lutBarrier);

        // Dynamic Rendering: FBO starts in SHADER_READ_ONLY, transition to COLOR_ATTACHMENT before rendering
        {
            VkImage fboImage = vulkanFbo->GetColorAttachmentImage(0);
            VkImageMemoryBarrier preRender{};
            preRender.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            preRender.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            preRender.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            preRender.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            preRender.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            preRender.image = fboImage;
            preRender.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            preRender.subresourceRange.levelCount = 1;
            preRender.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, 0, nullptr, 0, nullptr, 1, &preRender);
        }

        VkRenderingAttachmentInfo colorAttach{};
        colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttach.imageView = vulkanFbo->GetColorAttachmentImageView(0);
        colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttach.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea = { {0, 0}, {dim, dim} };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttach;

        vkCmdBeginRendering(cmd, &renderingInfo);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());

        VkViewport viewport{ 0.0f, (float)dim, (float)dim, -(float)dim, 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{ {0, 0}, {dim, dim} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // 【最精简的绘制指令】：画 3 个点，不需要绑定任何 VBO/IBO，也不需要推常量和描述符！
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        // 4. 将 FBO 的数据拷贝到 2D 贴图显存中
        VkImage fboImage = vulkanFbo->GetColorAttachmentImage(0);

        VkImageMemoryBarrier copyBarrier{};
        copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        // Dynamic rendering leaves image in COLOR_ATTACHMENT_OPTIMAL
        copyBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copyBarrier.image = fboImage;
        copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyBarrier.subresourceRange.levelCount = 1;
        copyBarrier.subresourceRange.layerCount = 1;
        copyBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;
        copyRegion.extent = { dim, dim, 1 };
        vkCmdCopyImage(cmd, fboImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lutImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        lutBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        lutBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lutBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        lutBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &lutBarrier);

        context->EndSingleTimeCommands(cmd);
        vkDeviceWaitIdle(device);
        AYAYA_CORE_INFO("VulkanIBLBuilder: BRDF LUT generated successfully!");
        return (void*)lutView;
    }
    
    void VulkanIBLBuilder::ClearResources() {
        if (s_TrackedIBLResources.empty()) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!context) return;

        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        vkDeviceWaitIdle(device);

        for (auto& res : s_TrackedIBLResources) {
            vkDestroyImageView(device, res.View, nullptr);
            vmaDestroyImage(allocator, res.Image, res.Alloc);
        }
        s_TrackedIBLResources.clear();
        AYAYA_CORE_INFO("VulkanIBLBuilder: Baked resources cleared.");
    }
}