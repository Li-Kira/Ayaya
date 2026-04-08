#include "ayapch.h"
#include "ImGuiBackend.hpp"
#include "Engine/Core/Application.hpp"
#include "Engine/Renderer/RendererAPI.hpp"

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
            auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            // 获取下一张图像，并开启 Render Pass
            vulkanContext->BeginFrame(); 
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
            // ==========================================
            // 将 ImGui 的绘制指令录入当前的 Command Buffer
            // ==========================================
            auto vulkanContext = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vulkanContext->GetCurrentCommandBuffer());
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