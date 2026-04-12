#include "ayapch.h"
#include "VulkanClearPass.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {
    void VulkanClearPass::Execute(RenderContext& context, RenderCommandBuffer& cmd) {
        uint32_t width = context.Get<uint32_t>("ViewportWidth");
        uint32_t height = context.Get<uint32_t>("ViewportHeight");

        if (width == 0 || height == 0) return;

        // 1. 获取或创建我们的离线 FBO
        std::shared_ptr<Framebuffer> targetFBO;
        if (context.Framebuffers.find("VulkanTarget") == context.Framebuffers.end()) {
            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Attachments = { FramebufferTextureFormat::RGBA8 };
            targetFBO = Framebuffer::Create(spec);
            context.Framebuffers["VulkanTarget"] = targetFBO;
        } else {
            targetFBO = context.Framebuffers["VulkanTarget"];
            if (targetFBO->GetSpecification().Width != width || targetFBO->GetSpecification().Height != height) {
                targetFBO->Resize(width, height);
            }
        }

        // 2. 从黑板拿到 EditorLayer 传来的背景色
       glm::vec4 clearColor = glm::vec4(0.8f, 0.0f, 0.2f, 1.0f);

        // 3. 执行清屏指令！
        cmd.BeginRenderPass(targetFBO, true, clearColor);
        cmd.EndRenderPass();

        // ==========================================
        // 【核心修复】：直接交出 Framebuffer 对象！
        // 绝不能调用 GetColorAttachmentRendererID，保持强类型安全！
        // ==========================================
        context.Set("Final_Output", targetFBO);
    }
}