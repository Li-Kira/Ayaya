#include "ayapch.h"
#include "VulkanIBLBuilder.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanTexture2D.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <vk_mem_alloc.h>

namespace Ayaya {

    // 内存泄漏追踪器：记录 IBLBuilder 自己创建的原生对象，防止退出时报 VMA 错误
    struct IBLResource { VkImage Image; VmaAllocation Alloc; VkImageView View; };
    static std::vector<IBLResource> s_TrackedIBLResources;

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
        // 4. 定义 6 个面的视图矩阵 (注意 Vulkan 的 Y 轴翻转)
        // ==========================================
        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        captureProjection[1][1] *= -1.0f; // Vulkan Y 轴翻转魔法
        
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

        for (int i = 0; i < 6; i++) {
            pc.View = captureViews[i];

            // 5.b 开启 FBO 渲染通道
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = vulkanFbo->GetVulkanRenderPass();
            renderPassInfo.framebuffer = vulkanFbo->GetVulkanFramebuffer();
            renderPassInfo.renderArea.offset = { 0, 0 };
            renderPassInfo.renderArea.extent = { dim, dim };
            VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipelineLayout(), 1, 1, &descSet, 0, nullptr);
            vkCmdPushConstants(cmd, vulkanPipeline->GetVulkanPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pc);
            
            // ==========================================
            // 【核心修复 2】：补充动态视口与裁剪指令！
            // ==========================================
            VkViewport viewport{ 0.0f, 0.0f, (float)dim, (float)dim, 0.0f, 1.0f };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{ {0, 0}, {dim, dim} };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, cubeMesh->GetIndexCount(), 1, 0, 0, 0);
            
            vkCmdEndRenderPass(cmd);

            // 5.c 设置屏障，准备把刚刚画好的 FBO 画面拷贝出去
            VkImageMemoryBarrier copyBarrier{};
            copyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            // 【核心修复 3.1】：匹配真实的引擎状态
            copyBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copyBarrier.image = fboImage;
            copyBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyBarrier.subresourceRange.baseMipLevel = 0;
            copyBarrier.subresourceRange.levelCount = 1;
            copyBarrier.subresourceRange.baseArrayLayer = 0;
            copyBarrier.subresourceRange.layerCount = 1;
            // 【核心修复 3.2】：调整访问掩码
            copyBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; 
            copyBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);

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
            // 【核心修复 3.3】：还原为着色器只读，以匹配 RenderPass 开始时的预期
            copyBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
            copyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            copyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
        }

        // 6. 最终将填满 6 面的 Cubemap 设置为只读模式供给场景采样
        cubeBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        cubeBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cubeBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        cubeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &cubeBarrier);

        context->EndSingleTimeCommands(cmd);
        AYAYA_CORE_INFO("VulkanIBLBuilder: HDR to Cubemap baked successfully!");

        // 返回生成的 ImageView 句柄，外部的 TextureCube 会自动包装它！
        return (void*)cubeView;
    }

    // （其他三个 Stub 方法暂时保留原样，返回 0）
    void* VulkanIBLBuilder::CreateIrradianceMap(void* envCubemap, const std::shared_ptr<Mesh>& cubeMesh, const std::shared_ptr<Shader>& irradianceShader) { return 0; }
    void* VulkanIBLBuilder::CreatePrefilterMap(void* envCubemap, const std::shared_ptr<Mesh>& cubeMesh, const std::shared_ptr<Shader>& prefilterShader) { return 0; }
    void* VulkanIBLBuilder::CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, void* emptyVAO) { return 0; }
    
    void VulkanIBLBuilder::ClearResources() {
        if (s_TrackedIBLResources.empty()) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!context) return;

        VkDevice device = context->GetDevice();
        auto allocator = context->GetAllocator();

        // ==========================================
        // 【核心修复 2B】：把我们当时埋下的显存炸弹拆掉！
        // ==========================================
        for (auto& res : s_TrackedIBLResources) {
            vkDestroyImageView(device, res.View, nullptr);
            vmaDestroyImage(allocator, res.Image, res.Alloc);
        }
        s_TrackedIBLResources.clear();
        AYAYA_CORE_INFO("VulkanIBLBuilder: Baked resources cleared.");
    }
}