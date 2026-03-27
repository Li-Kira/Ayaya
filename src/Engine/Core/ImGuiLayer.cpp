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

    void SetDarkThemeColors() {
        auto& colors = ImGui::GetStyle().Colors;
        
        // =========================================================
        // 1. 基础深色背景 (深邃的黑灰色，层次分明)
        // =========================================================
        colors[ImGuiCol_WindowBg]           = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        colors[ImGuiCol_ChildBg]            = ImVec4{ 0.12f, 0.125f, 0.13f, 1.0f };
        colors[ImGuiCol_PopupBg]            = ImVec4{ 0.1f, 0.105f, 0.11f, 0.9f };
        colors[ImGuiCol_MenuBarBg]          = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

        // =========================================================
        // 2. 灵魂点缀色 (Engine Accent Blue) 
        // =========================================================
        ImVec4 accentColor        = ImVec4{ 0.17f, 0.45f, 0.85f, 1.0f }; // 现代蓝
        ImVec4 accentColorHovered = ImVec4{ 0.22f, 0.50f, 0.90f, 1.0f };
        ImVec4 accentColorActive  = ImVec4{ 0.12f, 0.40f, 0.80f, 1.0f };

        // 将点缀色应用到核心交互组件上
        colors[ImGuiCol_Tab]                = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f }; // 未激活的标签（深色）
        colors[ImGuiCol_TabHovered]         = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f }; // 鼠标悬浮时的颜色
        colors[ImGuiCol_TabActive]          = colors[ImGuiCol_WindowBg]; 
        colors[ImGuiCol_TabUnfocused]       = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_WindowBg];

        // =========================================================
        // 3. 选项卡 (Tabs) - 融入点缀色
        // =========================================================
        colors[ImGuiCol_Tab]                = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f }; // 未激活的标签
        colors[ImGuiCol_TabHovered]         = accentColor; // 悬浮
        colors[ImGuiCol_TabActive]          = colors[ImGuiCol_WindowBg];              // 激活：与背景融为一体
        colors[ImGuiCol_TabUnfocused]       = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f }; // 未聚焦
        colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_WindowBg];

        // =========================================================
        // 4. 输入框与背景 (Frame)
        // =========================================================
        colors[ImGuiCol_FrameBg]            = ImVec4{ 0.18f, 0.185f, 0.19f, 1.0f }; // 比底色亮一点，区分出输入框
        colors[ImGuiCol_FrameBgHovered]     = ImVec4{ 0.25f, 0.255f, 0.26f, 1.0f };
        colors[ImGuiCol_FrameBgActive]      = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

        // =========================================================
        // 5. 按钮与表头 (Header & Button)
        // =========================================================
        colors[ImGuiCol_Header]             = ImVec4{ 0.22f, 0.225f, 0.23f, 1.0f }; // 比如大纲里的选中项
        colors[ImGuiCol_HeaderHovered]      = ImVec4{ 0.28f, 0.285f, 0.29f, 1.0f };
        colors[ImGuiCol_HeaderActive]       = ImVec4{ 0.18f, 0.185f, 0.19f, 1.0f };
        
        colors[ImGuiCol_Button]             = ImVec4{ 0.22f, 0.225f, 0.23f, 1.0f };
        colors[ImGuiCol_ButtonHovered]      = ImVec4{ 0.28f, 0.285f, 0.29f, 1.0f };
        colors[ImGuiCol_ButtonActive]       = ImVec4{ 0.18f, 0.185f, 0.19f, 1.0f };

        // =========================================================
        // 6. 边缘修饰与辅助
        // =========================================================
        colors[ImGuiCol_TitleBg]            = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        colors[ImGuiCol_TitleBgActive]      = ImVec4{ 0.12f, 0.125f, 0.13f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed]   = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        
        colors[ImGuiCol_Separator]          = ImVec4{ 0.08f, 0.085f, 0.09f, 1.0f }; // 极暗的分割线，不抢眼
        colors[ImGuiCol_SeparatorHovered]   = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_SeparatorActive]    = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // 窗口拖拽调整大小的右下角三角符号
        colors[ImGuiCol_ResizeGrip]         = ImVec4{ 0.2f, 0.205f, 0.21f, 0.0f }; // 默认透明
        colors[ImGuiCol_ResizeGripHovered]  = accentColorHovered;
        colors[ImGuiCol_ResizeGripActive]   = accentColorActive;

        // 停靠时的预览高亮色
        colors[ImGuiCol_DockingPreview]     = ImVec4{ 0.17f, 0.45f, 0.85f, 0.4f };
    }

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

        float fontSize = 18.0f; // 基础字体大小
        // 建议加载你下载的高清 TTF 字体，不要用 AddFontDefault()
        io.Fonts->AddFontFromFileTTF("assets/Editor/fonts/Roboto-Regular.ttf", fontSize);

        // 加载并合并 FontAwesome 图标
        ImFontConfig icons_config;
        icons_config.MergeMode = true;  
        icons_config.PixelSnapH = true; 
        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
        io.Fonts->AddFontFromFileTTF("assets/Editor/fonts/fa-solid-900.ttf", 14.0f, &icons_config, icons_ranges);

        // 加载粗体作为辅助字体
        ImFontConfig boldConfig;
        io.Fonts->AddFontFromFileTTF("assets/Editor/fonts/Roboto-Bold.ttf", 18.0f, &boldConfig);

        // =========================================================
        // 2. 几何样式配置 (现代化圆角与间距)
        // =========================================================
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f; // 如果启用了多视口，顶级窗口通常不设圆角
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // --- 核心：高级 UI 的几何参数 ---
        style.FrameRounding = 4.0f;       // 按钮和输入框的圆角
        style.GrabRounding = 4.0f;        // 滚动条抓手的圆角
        style.WindowBorderSize = 0.0f;    // 移除丑陋的默认高光边框
        style.FrameBorderSize = 0.0f;     // 移除组件边框
        style.PopupBorderSize = 1.0f;     // 仅保留弹出菜单的边框
        style.WindowPadding = ImVec2(10.0f, 10.0f); // 增加面板内的呼吸感
        style.FramePadding = ImVec2(8.0f, 4.0f);    // 增加按钮内部的呼吸感
        style.ItemSpacing = ImVec2(8.0f, 6.0f);     // 组件之间的间距
        style.ScrollbarSize = 14.0f;      // 加宽滚动条更好点按

        // =========================================================
        // 3. 应用高级暗色主题
        // =========================================================
        ImGui::StyleColorsDark(); // 先以 ImGui 默认暗色垫底
        SetDarkThemeColors();     // 覆盖为我们的次世代引擎高级灰主题

        // 全局缩放适配高分屏 (Retina)
        // io.FontGlobalScale = 2.0f; 
        // style.ScaleAllSizes(2.0f);

        Application& app = Application::Get();
        GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());
        
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