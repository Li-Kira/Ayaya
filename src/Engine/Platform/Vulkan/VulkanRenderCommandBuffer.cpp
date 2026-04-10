#include "ayapch.h"
#include "VulkanRenderCommandBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

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

        if (clear) {
            VkClearValue cv{};
            cv.color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a} };
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &cv;
        }

        vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanRenderCommandBuffer::EndRenderPass() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkCmdEndRenderPass(context->GetCurrentCommandBuffer());
    }
}