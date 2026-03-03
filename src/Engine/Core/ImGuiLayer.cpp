#include "ayapch.h"
#include "ImGuiLayer.hpp"
#include "Engine/Core/Log.hpp" // 确保包含 Log

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "Engine/Core/Application.hpp"

namespace Ayaya {

    ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}
    ImGuiLayer::~ImGuiLayer() {}

    void ImGuiLayer::OnAttach() {
        // --- 核心防御逻辑：防止被外部意外多次调用 ---
        if (ImGui::GetCurrentContext() != nullptr) {
            AYAYA_CORE_WARN("ImGuiLayer::OnAttach() was called more than once! Skipping re-initialization.");
            return;
        }

        // 1. 设置 ImGui 上下文
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // 允许键盘控制
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // 开启停靠功能
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 开启多视口（可拖出主窗口）

        ImGui::StyleColorsDark();

        // 2. 绑定后端
        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());
        
        // 保持 false，防止与你 Window.cpp 里的手动事件回调冲突
        ImGui_ImplGlfw_InitForOpenGL(window, true); 
        ImGui_ImplOpenGL3_Init("#version 410");
    }

    void ImGuiLayer::OnDetach() {
        // 为了安全起见，增加判空
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    }

    void ImGuiLayer::Begin() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiLayer::End() {
        ImGuiIO& io = ImGui::GetIO();
        Application& app = Application::Get();
        io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
    }

    void ImGuiLayer::OnEvent(Event& e) {
        ImGuiIO& io = ImGui::GetIO();
        e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
        e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }
}