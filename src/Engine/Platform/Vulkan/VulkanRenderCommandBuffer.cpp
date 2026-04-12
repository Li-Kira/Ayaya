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
            // 1. 动态计算真实的 Color Attachment 数量
            uint32_t colorCount = 0;
            for (const auto& format : vulkanFBO->GetSpecification().Attachments.Attachments) {
                if (format.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8) {
                    colorCount++;
                }
            }

            // 2. 只为真正注册到 RenderPass 的 Color 附件推入清屏色
            for (uint32_t i = 0; i < colorCount; i++) {
                VkClearValue colorClear{};
                colorClear.color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a} };
                clearValues.push_back(colorClear);
            }

            // 【彻底删掉或注释掉深度清屏】：
            // 因为目前的 VulkanFramebuffer::Invalidate 并没有将深度附件加入 RenderPass
            // 强行给 DepthClear 会导致 clearValueCount > attachmentCount，直接闪退！
            /*
            VkClearValue depthClear{};
            depthClear.depthStencil = { 1.0f, 0 };
            clearValues.push_back(depthClear);
            */

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
        vkDeviceWaitIdle(context->GetDevice());

        vkUpdateDescriptorSets(context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    // 绑定独立 Texture2D 的实现 (占位，因为 VulkanTexture2D 我们还没写完)
    void VulkanRenderCommandBuffer::BindTexture2D(const std::shared_ptr<Pipeline>& pipeline, const std::string& name, uint32_t slot, const std::shared_ptr<Texture2D>& texture) {
        // AYAYA_CORE_WARN("BindTexture2D for Texture2D is not fully implemented yet!");
    }

    void VulkanRenderCommandBuffer::InsertExecutionBarrier() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkCommandBuffer cmdBuffer = context->GetCurrentCommandBuffer();

        // 创建一个全局内存屏障
        VkMemoryBarrier memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        
        // 生产者（上一个Pass）：我们必须等待颜色附件和深度附件的【写入操作】彻底完成
        memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        
        // 消费者（下一个Pass）：我们允许片段着色器进行【读取操作】
        memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // 向命令缓冲录制这条“交警指令”
        // 注意：它必须在 BeginRenderPass 之前，或者 EndRenderPass 之后调用！
        vkCmdPipelineBarrier(
            cmdBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, // 等待阶段
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // 阻塞阶段
            0,
            1, &memoryBarrier,
            0, nullptr,
            0, nullptr
        );
    }
}