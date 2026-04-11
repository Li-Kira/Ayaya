#include "ayapch.h"
#include "VulkanRenderCommandBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Ayaya {

    VulkanRenderCommandBuffer::VulkanRenderCommandBuffer() {
        // AYAYA_CORE_WARN("VulkanRenderCommandBuffer created (Stub)");
    }

    VulkanRenderCommandBuffer::~VulkanRenderCommandBuffer() {
    }

    void VulkanRenderCommandBuffer::Begin() {
        // 占位逻辑：未来我们会在这里调用 vkBeginCommandBuffer (如果使用的是 Secondary Command Buffers)
    }

    void VulkanRenderCommandBuffer::End() {
        // 占位逻辑：未来我们会在这里调用 vkEndCommandBuffer
    }

    void VulkanRenderCommandBuffer::BeginRenderPass(const std::shared_ptr<Framebuffer>& targetFBO, bool clear, const glm::vec4& clearColor) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();
        
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(targetFBO);
        if (!vulkanFBO) return;

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = vulkanFBO->GetVulkanRenderPass();
        renderPassInfo.framebuffer = vulkanFBO->GetVulkanFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = { vulkanFBO->GetSpecification().Width, vulkanFBO->GetSpecification().Height };

        // ==========================================
        // 【核心修复】：为所有的附件提供 Clear 值！
        // ==========================================
        std::vector<VkClearValue> clearValues;
        if (clear) {
            VkClearValue colorClear{};
            colorClear.color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a} };
            clearValues.push_back(colorClear);

            // 无论有没有开启深度测试，只要 FBO 里有深度附件，就必须给它喂一个清屏值！
            VkClearValue depthClear{};
            depthClear.depthStencil = { 1.0f, 0 };
            clearValues.push_back(depthClear);

            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();
        }

        vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanRenderCommandBuffer::EndRenderPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdEndRenderPass(context->GetCurrentCommandBuffer());
    }

    void VulkanRenderCommandBuffer::BindPipeline(const std::shared_ptr<Pipeline>& pipeline) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        
        if (vulkanPipeline) {
            VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();
           // 1. 绑定图形管线
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkanPipeline->GetVulkanPipeline());

            // ==========================================
            // 2. 【核心新增】：绑定对应的集装箱 (Descriptor Set)
            // 这样 Shader 里的 binding=0, 1, 2 就能找到对应的数据了！
            // ==========================================
            VkDescriptorSet descSet = vulkanPipeline->GetVulkanDescriptorSet();
            vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                    vulkanPipeline->GetVulkanPipelineLayout(), 
                                    0, 1, &descSet, 0, nullptr);

           // 3. 动态状态 (Viewport & Scissor)
            uint32_t width = vulkanPipeline->GetSpecification().TargetFramebuffer->GetSpecification().Width;
            uint32_t height = vulkanPipeline->GetSpecification().TargetFramebuffer->GetSpecification().Height;

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)width;
            viewport.height = (float)height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {width, height};
            vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
        }
    }

    void VulkanRenderCommandBuffer::DrawArrays(uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        // 核心：执行不带索引缓冲的绘制 (后处理全屏三角形)
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::DrawArrays(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        // 目前对于全屏三角形，我们的 m_EmptyVAO 里没有任何真实的 Vulkan Buffer
        // 所以我们不需要绑定顶点缓冲，直接呼叫 Draw 即可！
        vkCmdDraw(context->GetCurrentCommandBuffer(), vertexCount, 1, 0, 0);
    }

    void VulkanRenderCommandBuffer::PushConstant(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, float data) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        
        // 注意：这里为了简化，我们暂时假设所有 PushConstant 都写在 Fragment 阶段
        // 实际引擎应该通过 Shader 反射来查找 Offset
        vkCmdPushConstants(context->GetCurrentCommandBuffer(), 
                          vulkanPipeline->GetVulkanPipelineLayout(), 
                          VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &data);
    }

    // 绑定 Framebuffer 附件的实现
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Framebuffer>& framebuffer, uint32_t attachmentIndex, bool isDepth) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        auto vulkanPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(framebuffer);

        if (!vulkanPipeline || !vulkanFBO) return;

        VkDescriptorImageInfo imageInfo{};
        
        // 如果你的 VulkanFramebuffer 还没有写 GetDepthImageView()，可以暂时用 ColorImageView 顶替防止报错
        // 我们在做 Vulkan 阴影的时候再来实现它。
        imageInfo.imageView = isDepth ? vulkanFBO->GetDepthImageView() : vulkanFBO->GetColorImageView(attachmentIndex);
        imageInfo.imageLayout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = vulkanFBO->GetColorSampler(); // 深度图和颜色图通常可以共用基础采样器

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = vulkanPipeline->GetVulkanDescriptorSet(); 
        descriptorWrite.dstBinding = slot;                                 
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    // 绑定独立 Texture2D 的实现 (占位，因为 VulkanTexture2D 我们还没写完)
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        AYAYA_CORE_WARN("BindTexture2D for Texture2D is not fully implemented yet!");
    }
}