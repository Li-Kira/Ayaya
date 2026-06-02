#include "ayapch.h"
#include "ImGuiBackend.hpp"
#include "Engine/Core/Application.hpp"
#include "Renderer/Renderer.hpp"

#include "Engine/Platform/Vulkan/VulkanContext.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_vulkan.h> 
#include <GLFW/glfw3.h>

namespace Ayaya {

    void ImGuiBackend::BeginFrame() {
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            ImGui_ImplVulkan_NewFrame();
        } else {
            ImGui_ImplOpenGL3_NewFrame();
        }
        
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiBackend::EndFrameAndSwapBuffers() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Render();
        
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            glClear(GL_COLOR_BUFFER_BIT); 
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        } 
        else if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            VkCommandBuffer cmdBuffer = vulkanContext->GetCurrentCommandBuffer();

            // 全局内存屏障：确保离屏 FBO 的颜色写入对 ImGui 片元着色器可见
            VkMemoryBarrier globalBarrier{};
            globalBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            globalBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            globalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                1, &globalBarrier,
                0, nullptr,
                0, nullptr);

            // Dynamic Rendering: swapchain image 在 Acquire 后处于 UNDEFINED（首帧）
            // 或 PRESENT_SRC_KHR（后续帧），需显式过渡到 COLOR_ATTACHMENT。
            // VK_IMAGE_LAYOUT_UNDEFINED 作为 oldLayout 始终合法。
            {
                VkImageMemoryBarrier swapchainBarrier{};
                swapchainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                swapchainBarrier.srcAccessMask = 0;
                swapchainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                swapchainBarrier.image = vulkanContext->GetCurrentImage();
                swapchainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                swapchainBarrier.subresourceRange.levelCount = 1;
                swapchainBarrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(cmdBuffer,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &swapchainBarrier);
            }

            // Dynamic Rendering: 直接在 swapchain image 上绘制
            VkExtent2D extent = vulkanContext->GetSwapChainExtent();
            VkRenderingAttachmentInfo colorAttachment{};
            colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachment.imageView = vulkanContext->GetCurrentImageView();
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = {{0.08f, 0.085f, 0.09f, 1.0f}};

            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea = {{0, 0}, {extent.width, extent.height}};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &colorAttachment;
            renderingInfo.pDepthAttachment = nullptr;

            vkCmdBeginRendering(cmdBuffer, &renderingInfo);

            ImDrawData* draw_data = ImGui::GetDrawData();
            if (draw_data) {
                int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
                int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);

                int win_fb_width, win_fb_height;
                GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
                glfwGetFramebufferSize(window, &win_fb_width, &win_fb_height);

                if (fb_width == win_fb_width && fb_height == win_fb_height && fb_width > 0 && fb_height > 0) {
                    ImGui_ImplVulkan_RenderDrawData(draw_data, vulkanContext->GetCurrentCommandBuffer());
                } else {
                    AYAYA_CORE_WARN("Vulkan SwapChain size mismatch! Skipping ImGui render for 1 frame.");
                }
            }

            vkCmdEndRendering(cmdBuffer);

            // Dynamic Rendering: 手动将 swapchain image 从 COLOR_ATTACHMENT 转换为 PRESENT_SRC_KHR
            VkImageMemoryBarrier presentBarrier{};
            presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            presentBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            presentBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            presentBarrier.dstAccessMask = 0;
            presentBarrier.image = vulkanContext->GetCurrentImage();
            presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            presentBarrier.subresourceRange.levelCount = 1;
            presentBarrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
        }
        
        // 处理多视口 (Viewports)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        
        // ==========================================
        // 【防重复崩溃修复】：不再在这里调用 SwapBuffers！
        // 这一步已经交由外部的 Window::OnUpdate() 全权接管，
        // 对于 Vulkan 来说，Window 的调用最终会走到 VulkanContext::SwapBuffers。
        // ==========================================
        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            // 如果还想兼容 OpenGL，保留这句（推荐未来也将它统一到 Window 里去）
            // GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            // glfwSwapBuffers(window);
        }
    }
}