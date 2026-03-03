#include "ayapch.h"
#include "ImGuiLayer.hpp"
#include "Engine/Core/Log.hpp" // 确保包含 Log

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "Engine/Core/Application.hpp"
#include <backends/IconsFontAwesome5.h>

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
        
        // ==========================================
        // 新增：加载默认字体，并合并 FontAwesome 图标
        // ==========================================
        io.Fonts->AddFontDefault(); // 先加载 ImGui 默认的像素字体
        ImFontConfig icons_config;
        icons_config.MergeMode = true;  // 开启合并模式，将图标拼接到默认字体后面
        icons_config.PixelSnapH = true; // 像素对齐，让图标更清晰

        // 定义我们需要加载的图标 Unicode 范围 (从 FontAwesome 头文件获取)
        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
        
        // 加载 TTF 字体文件 (请确保 assets/fonts/fa-solid-900.ttf 路径正确！)
        // 大小设置为 13.0f (与默认字体大小基准匹配，会被 io.FontGlobalScale 放大)
        io.Fonts->AddFontFromFileTTF("assets/fonts/fa-solid-900.ttf", 13.0f, &icons_config, icons_ranges);
        // ==========================================

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