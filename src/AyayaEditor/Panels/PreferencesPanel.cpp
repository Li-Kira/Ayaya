#include "ayapch.h"
#include "PreferencesPanel.hpp"
#include "Engine/Core/Application.hpp"

#include <imgui.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

// 【新增】：引入图标库让左侧标签更美观
#include <IconsFontAwesome5.h> 

namespace Ayaya {

    void PreferencesPanel::Init() {
        LoadPreferences();

        // 在初始化时（引擎刚启动），立刻应用保存好的窗口大小和 UI 缩放！
        Application::Get().GetWindow().SetSize(m_WindowWidth, m_WindowHeight);
        ImGui::GetIO().FontGlobalScale = m_UIScale;
        
        AYAYA_CORE_INFO("Preferences loaded and applied.");
    }

    void PreferencesPanel::SetOpen(bool isOpen) {
        m_IsOpen = isOpen;
        if (isOpen) {
            m_WindowWidth = (int)ImGui::GetMainViewport()->Size.x;
            m_WindowHeight = (int)ImGui::GetMainViewport()->Size.y;
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

        out << YAML::Key << "ToneMappingType" << YAML::Value << ToneMappingType;
        out << YAML::Key << "Exposure" << YAML::Value << Exposure;
        
        out << YAML::Key << "EnableBloom" << YAML::Value << EnableBloom;
        out << YAML::Key << "BloomThreshold" << YAML::Value << BloomThreshold;
        out << YAML::Key << "BloomIntensity" << YAML::Value << BloomIntensity;
        
        out << YAML::Key << "EnableFXAA" << YAML::Value << EnableFXAA;
        
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

                if (prefs["ToneMappingType"]) ToneMappingType = prefs["ToneMappingType"].as<int>();
                if (prefs["Exposure"]) Exposure = prefs["Exposure"].as<float>();
                
                if (prefs["EnableBloom"]) EnableBloom = prefs["EnableBloom"].as<bool>();
                if (prefs["BloomThreshold"]) BloomThreshold = prefs["BloomThreshold"].as<float>();
                if (prefs["BloomIntensity"]) BloomIntensity = prefs["BloomIntensity"].as<float>();
                
                if (prefs["EnableFXAA"]) EnableFXAA = prefs["EnableFXAA"].as<bool>();
            }
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load preferences: {0}", e.what());
        }
    }

    void PreferencesPanel::OnImGuiRender() {
        if (!m_IsOpen) return;

        // 设置面板推荐的初始大小
        ImGui::SetNextWindowSize(ImVec2(750.0f * m_UIScale, 550.0f * m_UIScale), ImGuiCond_FirstUseEver);
        ImGui::Begin("Preferences", &m_IsOpen);

        // ==========================================
        // 动态尺寸计算 (完美兼容任何分辨率和缩放)
        // ==========================================
        float buttonHeight = 30.0f * m_UIScale;
        // 底部保留空间：按钮高度 + 分割线 + 上下边距
        float bottomReserved = buttonHeight + 25.0f * m_UIScale; 
        // 左侧导航栏宽度
        float leftPaneWidth = 180.0f * m_UIScale;

        // 记录当前选中的标签页 (0 = Window & UI, 1 = Rendering)
        static int s_ActiveTab = 0;

        // ==========================================
        // 1. 左侧导航面板 (Left Pane)
        // ==========================================
        // true 代表带有一圈轻微的内边框，更像一个独立的列表框
        ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, -bottomReserved), true);
        
        // 美化标签项：文本垂直居中，增加上下间距
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));

        if (ImGui::Selectable("  " ICON_FA_DESKTOP "   Window & UI", s_ActiveTab == 0, 0, ImVec2(0, 32.0f * m_UIScale))) s_ActiveTab = 0;
        if (ImGui::Selectable("  " ICON_FA_PALETTE "   Rendering", s_ActiveTab == 1, 0, ImVec2(0, 32.0f * m_UIScale))) s_ActiveTab = 1;
        
        ImGui::PopStyleVar(2);
        ImGui::EndChild();

        // 极度关键：让右侧面板和左侧面板在同一行！
        ImGui::SameLine();

        // ==========================================
        // 2. 右侧内容面板 (Right Pane)
        // ==========================================
        ImGui::BeginChild("RightPane", ImVec2(0, -bottomReserved), true);
        
        // 为了右侧内容不显得拥挤，给它加一点内边距推移
        ImGui::Indent(8.0f * m_UIScale);
        ImGui::Spacing();

        if (s_ActiveTab == 0) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Appearance & Resolution");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::TreeNodeEx("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::SliderFloat("UI Scale", &m_UIScale, 0.5f, 3.0f, "%.1fx")) {
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
                int size[2] = { m_WindowWidth, m_WindowHeight };
                if (ImGui::InputInt2("Resolution", size)) {
                    m_WindowWidth = std::max(800, size[0]);
                    m_WindowHeight = std::max(600, size[1]);
                }

                ImGui::Spacing();
                if (ImGui::Button("Apply Window Size", ImVec2(150.0f * m_UIScale, 0))) {
                    Application::Get().GetWindow().SetSize(m_WindowWidth, m_WindowHeight);
                    AYAYA_CORE_INFO("Changed window size to {0}x{1}", m_WindowWidth, m_WindowHeight);
                }
                ImGui::TreePop();
            }
        }
        else if (s_ActiveTab == 1) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Tone Mapping & Color Grading");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::DragFloat("Exposure Compensation", &Exposure, 0.05f, 0.1f, 10.0f, "%.2fx");
            
            const char* tmTypes[] = { "Reinhard (Classic)", "ACES (Filmic)" };
            ImGui::Combo("Tone Mapping Algorithm", &ToneMappingType, tmTypes, 2);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("ACES provides a cinematic color curve that handles ultra-bright highlights better than standard Reinhard.");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Spacing();
            
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Bloom (Optical Flare)");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::Checkbox("Enable Bloom", &EnableBloom);
            if (EnableBloom) {
                ImGui::Indent(10.0f * m_UIScale);
                ImGui::DragFloat("Threshold", &BloomThreshold, 0.05f, 0.0f, 10.0f, "%.2f");
                ImGui::DragFloat("Intensity", &BloomIntensity, 0.05f, 0.0f, 5.0f, "%.2f");
                ImGui::Unindent(10.0f * m_UIScale);
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::Text("Anti-Aliasing");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();
            
            ImGui::Checkbox("Enable FXAA (Fast Approximate)", &EnableFXAA);
            ImGui::Spacing();
        }

        ImGui::Unindent(8.0f * m_UIScale);
        ImGui::EndChild();

        // ==========================================
        // 3. 底部操作栏 (Bottom Bar)
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 右对齐保存按钮的魔法
        float btnWidth = 200.0f * m_UIScale;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - btnWidth);
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.45f, 0.85f, 1.0f)); // 引擎主色调的保存按钮
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.50f, 0.90f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.80f, 1.0f));
        
        if (ImGui::Button("Save Preferences", ImVec2(btnWidth, buttonHeight))) {
            m_WindowWidth = (int)ImGui::GetMainViewport()->Size.x;
            m_WindowHeight = (int)ImGui::GetMainViewport()->Size.y;
            m_UIScale = ImGui::GetIO().FontGlobalScale;
            
            SavePreferences();
        }
        
        ImGui::PopStyleColor(3);

        ImGui::End();
    }

}