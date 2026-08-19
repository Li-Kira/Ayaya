#include "ayapch.h"
#include "PreferencesPanel.hpp"
#include "Engine/Core/Application.hpp"
#include "../EditorLayer.hpp"

#include <imgui.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include <IconsFontAwesome5.h> 

namespace Ayaya {

    void PreferencesPanel::Init() {
        LoadPreferences();
        InitialGraphicsAPI = GraphicsAPI;

        // Compute actual content scale from window state, NOT from
        // glfwGetWindowContentScale. The latter returns the monitor's
        // theoretical DPI (e.g. 1.5 for 150%) even when the app runs in a
        // DPI-virtualized mode where framebuffer pixels == window pixels,
        // causing the stored physical size to be divided too aggressively.
        GLFWwindow* win = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (win) {
            int winW, winH, fbW, fbH;
            glfwGetWindowSize(win, &winW, &winH);
            glfwGetFramebufferSize(win, &fbW, &fbH);
            if (winW > 0 && fbW > 0) {
                m_ContentScale = (float)fbW / (float)winW;
            }
            if (m_ContentScale < 1.0f) m_ContentScale = 1.0f;
        }

        // Clamp stored physical size to current monitor so cross-machine YAML
        // (e.g. 2560x1600 from a Mac) fits the actual screen on this machine.
        // CRITICAL: glfwGetVideoMode returns screen-coordinate dimensions, which on
        // macOS Retina are LOGICAL points (e.g. 1920×1080 for a "looks like 1920×1080"
        // 4K display). Multiply by the content scale to get true physical pixel limits.
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            int maxPhysW = (int)(mode->width * m_ContentScale);
            int maxPhysH = (int)(mode->height * m_ContentScale);
            if (m_WindowWidth > maxPhysW || m_WindowHeight > maxPhysH) {
                AYAYA_CORE_WARN("Preferences stored size {0}x{1} exceeds monitor {2}x{3}, clamping.",
                    m_WindowWidth, m_WindowHeight, maxPhysW, maxPhysH);
                m_WindowWidth  = std::min(m_WindowWidth,  maxPhysW);
                m_WindowHeight = std::min(m_WindowHeight, maxPhysH);
            }
        }

        int logicalW = (int)(m_WindowWidth / m_ContentScale);
        int logicalH = (int)(m_WindowHeight / m_ContentScale);
        AYAYA_CORE_INFO("Preferences: contentScale={0}, physical={1}x{2} -> logical={3}x{4}",
            m_ContentScale, m_WindowWidth, m_WindowHeight, logicalW, logicalH);

        Application::Get().GetWindow().SetSize(logicalW, logicalH);
        ImGui::GetIO().FontGlobalScale = m_UIScale;
        
        // 【新增】：初始化时同步从 yaml 读到的历史容量！
        EditorLayer::Get().GetCommandHistory().SetCapacity((size_t)MaxUndoSteps);
        
        AYAYA_CORE_INFO("Preferences loaded and applied.");
    }

    void PreferencesPanel::SetOpen(bool isOpen) {
        m_IsOpen = isOpen;
        if (isOpen) {
            // Recompute content scale from current window in case monitor changed.
            GLFWwindow* win = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            if (win) {
                int winW, winH, fbW, fbH;
                glfwGetWindowSize(win, &winW, &winH);
                glfwGetFramebufferSize(win, &fbW, &fbH);
                if (winW > 0 && fbW > 0) {
                    m_ContentScale = (float)fbW / (float)winW;
                }
                if (m_ContentScale < 1.0f) m_ContentScale = 1.0f;
            }
            // ImGui viewport is already in physical framebuffer pixels via GLFW backend;
            // do NOT multiply by m_ContentScale again (would double-count on Retina).
            m_WindowWidth  = (int)(ImGui::GetMainViewport()->Size.x);
            m_WindowHeight = (int)(ImGui::GetMainViewport()->Size.y);
            m_UIScale = ImGui::GetIO().FontGlobalScale;
        }
    }

    void PreferencesPanel::SavePreferences() {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "EditorPreferences" << YAML::Value << YAML::BeginMap;
        
        out << YAML::Key << "WindowWidth" << YAML::Value << m_WindowWidth;
        out << YAML::Key << "WindowHeight" << YAML::Value << m_WindowHeight;
        out << YAML::Key << "UIScale" << YAML::Value << m_UIScale;

        out << YAML::Key << "MaxUndoSteps" << YAML::Value << MaxUndoSteps;
        out << YAML::Key << "EnableVSync" << YAML::Value << EnableVSync;

        // 【更新】：保存全局底层配置，删除了旧的后处理配置
        out << YAML::Key << "GraphicsAPI" << YAML::Value << GraphicsAPI;

        out << YAML::EndMap;
        out << YAML::EndMap;

        std::ofstream fout(m_PrefsFilePath);
        fout << out.c_str();
        
        AYAYA_CORE_INFO("Preferences successfully saved to {0}", m_PrefsFilePath);
    }

    void PreferencesPanel::LoadPreferences() {
        if (!std::filesystem::exists(m_PrefsFilePath)) return;

        try {
            YAML::Node data = YAML::LoadFile(m_PrefsFilePath);
            auto prefs = data["EditorPreferences"];
            if (prefs) {
                if (prefs["WindowWidth"]) m_WindowWidth = prefs["WindowWidth"].as<int>();
                if (prefs["WindowHeight"]) m_WindowHeight = prefs["WindowHeight"].as<int>();
                if (prefs["UIScale"]) m_UIScale = prefs["UIScale"].as<float>();

                if (prefs["MaxUndoSteps"]) MaxUndoSteps = prefs["MaxUndoSteps"].as<int>();
                if (prefs["EnableVSync"]) EnableVSync = prefs["EnableVSync"].as<bool>();

                // 【更新】：读取全局底层配置
                if (prefs["GraphicsAPI"]) {
                    try {
                        GraphicsAPI = prefs["GraphicsAPI"].as<int>();
                    } catch (...) {
                        GraphicsAPI = static_cast<int>(prefs["GraphicsAPI"].as<float>());
                    }
                }
            }
            // Apply loaded values
            Application::Get().GetWindow().SetVSync(EnableVSync);
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load preferences: {0}", e.what());
        }
    }

    void PreferencesPanel::OnImGuiRender() {
        if (!m_IsOpen) return;

        // ==========================================
        // 【核心修复】：抽取当前真实生效的缩放系数，用于所有的排版计算！
        // ==========================================
        float currentScale = ImGui::GetIO().FontGlobalScale;

        ImGui::SetNextWindowSize(ImVec2(750.0f * currentScale, 550.0f * currentScale), ImGuiCond_FirstUseEver);
        ImGui::Begin("Preferences", &m_IsOpen);

        float buttonHeight = 30.0f * currentScale;
        float bottomReserved = buttonHeight + 25.0f * currentScale; 
        float leftPaneWidth = 180.0f * currentScale;

        static int s_ActiveTab = 0;

        ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, -bottomReserved), true);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));

        if (ImGui::Selectable("  " ICON_FA_DESKTOP "   Window & UI", s_ActiveTab == 0, 0, ImVec2(0, 32.0f * currentScale))) s_ActiveTab = 0;
        if (ImGui::Selectable("  " ICON_FA_PALETTE "   Rendering", s_ActiveTab == 1, 0, ImVec2(0, 32.0f * currentScale))) s_ActiveTab = 1;
        if (ImGui::Selectable("  " ICON_FA_ROCKET "   Performance", s_ActiveTab == 2, 0, ImVec2(0, 32.0f * currentScale))) s_ActiveTab = 2;

        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightPane", ImVec2(0, -bottomReserved), true);
        ImGui::Indent(8.0f * currentScale);
        ImGui::Spacing();

        if (s_ActiveTab == 0) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Appearance & Resolution");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::TreeNodeEx("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                // 滑杆修改的是 m_UIScale，不影响这帧的排版 currentScale
                ImGui::SliderFloat("UI Scale", &m_UIScale, 0.5f, 3.0f, "%.1fx");
                
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ImGui::GetIO().FontGlobalScale = m_UIScale;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##Scale")) {
                    m_UIScale = 1.0f;
                    ImGui::GetIO().FontGlobalScale = m_UIScale;
                }
                ImGui::TreePop();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::TreeNodeEx("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
                // 【新增】：垂直同步开关
                if (ImGui::Checkbox("Enable VSync (Limit to Monitor Refresh Rate)", &EnableVSync)) {
                    // 通知底层窗口系统更改 VSync 状态
                    Application::Get().GetWindow().SetVSync(EnableVSync);
                }

                ImGui::Spacing();
                
                // ==========================================
                // 【新增】：常用的分辨率预设下拉框
                // ==========================================
                const char* resOptions[] = { 
                    "1280 x 720 (HD 720p)", 
                    "1600 x 900 (HD+)",
                    "1920 x 1080 (FHD 1080p)", 
                    "2560 x 1440 (QHD 2K)", 
                    "3840 x 2160 (UHD 4K)" 
                };
                
                static int currentResIndex = -1; // -1 代表用户自定义分辨率
                
                if (ImGui::Combo("Presets", &currentResIndex, resOptions, IM_ARRAYSIZE(resOptions))) {
                    switch (currentResIndex) {
                        case 0: m_WindowWidth = 1280; m_WindowHeight = 720; break;
                        case 1: m_WindowWidth = 1600; m_WindowHeight = 900; break;
                        case 2: m_WindowWidth = 1920; m_WindowHeight = 1080; break;
                        case 3: m_WindowWidth = 2560; m_WindowHeight = 1440; break;
                        case 4: m_WindowWidth = 3840; m_WindowHeight = 2160; break;
                    }
                }

                int size[2] = { m_WindowWidth, m_WindowHeight };
                if (ImGui::InputInt2("Resolution", size)) {
                    m_WindowWidth = std::max(800, size[0]);
                    m_WindowHeight = std::max(600, size[1]);
                    currentResIndex = -1; // 手动修改时重置 Preset 下拉框状态
                }

                ImGui::Spacing();
                if (ImGui::Button("Apply Window Size", ImVec2(150.0f * currentScale, 0))) {
                    Application::Get().GetWindow().SetSize(
                        (int)(m_WindowWidth / m_ContentScale),
                        (int)(m_WindowHeight / m_ContentScale));
                    AYAYA_CORE_INFO("Changed window size to {0}x{1} (physical)", m_WindowWidth, m_WindowHeight);
                }
                ImGui::TreePop();
            }
        }
        else if (s_ActiveTab == 1) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Graphics API & Backend");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            // ==========================================
            // 图形 API 切换预留位
            // ==========================================
            const char* apiItems[] = { "OpenGL", "Vulkan", "DirectX 12" };
            ImGui::Combo("Graphics API", &GraphicsAPI, apiItems, IM_ARRAYSIZE(apiItems));
                
            // ==========================================
            // 【修改】：只有当选择的 API 与当前正在运行的 API 不一致时，才弹出重启警告！
            // ==========================================
            if (GraphicsAPI != InitialGraphicsAPI) { 
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.36f, 0.36f, 1.0f)); // 醒目的珊瑚红
                ImGui::TextWrapped(ICON_FA_EXCLAMATION_TRIANGLE " API changed. Requires restarting the engine to take effect.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // ==========================================
            // 预留给未来的全局硬件/质量上限配置
            // ==========================================
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Global Rendering Quality");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::TextDisabled("Note: Post-Processing effects (Bloom, Tone Mapping, FXAA, TAA) have been moved to the ECS.");
            ImGui::TextDisabled("Please use a 'Post Process Volume' component in your scene.");
            
            ImGui::Spacing();
            ImGui::TextDisabled("[Future Updates]");
            ImGui::TextDisabled(" - Global MSAA Target");
            ImGui::TextDisabled(" - Maximum Shadow Map Resolution (VRAM Limit)");
            ImGui::TextDisabled(" - Texture Streaming Budget");
        }
        else if (s_ActiveTab == 2) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Memory & History");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::TreeNodeEx("Command History", ImGuiTreeNodeFlags_DefaultOpen)) {
                
                if (ImGui::InputInt("Max Undo Steps", &MaxUndoSteps)) {
                    MaxUndoSteps = std::max(10, MaxUndoSteps); // 强制设置下限，防止用户乱调导致撤回失效
                    // 实时同步给引擎
                    EditorLayer::Get().GetCommandHistory().SetCapacity((size_t)MaxUndoSteps);
                }
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("Higher values consume more RAM but allow longer undo history.");
                ImGui::PopStyleColor();
                
                ImGui::TreePop();
            }

            // (预留：未来可以在这里继续添加：纹理显存限制、阴影分辨率预设、物理引擎 Tick Rate 等性能相关选项)
        }

        ImGui::Unindent(8.0f * currentScale);
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        float btnWidth = 200.0f * currentScale;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - btnWidth);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.45f, 0.85f, 1.0f)); 
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.50f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.80f, 1.0f));
        
        if (ImGui::Button("Save Preferences", ImVec2(btnWidth, buttonHeight))) {
            // ImGui viewport is already in physical framebuffer pixels; no scale multiply needed.
            m_WindowWidth  = (int)(ImGui::GetMainViewport()->Size.x);
            m_WindowHeight = (int)(ImGui::GetMainViewport()->Size.y);
            m_UIScale = ImGui::GetIO().FontGlobalScale;
            SavePreferences();
        }
        
        ImGui::PopStyleColor(3);
        ImGui::End();
    }
}