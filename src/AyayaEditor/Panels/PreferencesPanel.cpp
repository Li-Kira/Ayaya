#include "ayapch.h"
#include "PreferencesPanel.hpp"
#include "Engine/Core/Application.hpp"

#include <imgui.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>
#include <algorithm>

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
            // 每次打开面板时，同步一次当前的真实数据
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

        // 保存渲染后处理参数
        out << YAML::Key << "ToneMappingType" << YAML::Value << ToneMappingType;
        out << YAML::Key << "Exposure" << YAML::Value << Exposure;
        
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

                // 读取渲染后处理参数
                if (prefs["ToneMappingType"]) ToneMappingType = prefs["ToneMappingType"].as<int>();
                if (prefs["Exposure"]) Exposure = prefs["Exposure"].as<float>();
            }
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load preferences: {0}", e.what());
        }
    }

    void PreferencesPanel::OnImGuiRender() {
        if (!m_IsOpen) return;

        ImGui::Begin("Preferences", &m_IsOpen);

        if (ImGui::BeginTabBar("PreferencesTabs")) {

            // ------------------------------------------
            // 选项卡 1：窗口与界面设置
            // ------------------------------------------
            if (ImGui::BeginTabItem("Window & UI")) {
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

                ImGui::Separator();

                if (ImGui::TreeNodeEx("Window", ImGuiTreeNodeFlags_DefaultOpen)) {
                    int size[2] = { m_WindowWidth, m_WindowHeight };
                    if (ImGui::InputInt2("Resolution", size)) {
                        m_WindowWidth = std::max(800, size[0]);
                        m_WindowHeight = std::max(600, size[1]);
                    }

                    if (ImGui::Button("Apply Window Size")) {
                        Application::Get().GetWindow().SetSize(m_WindowWidth, m_WindowHeight);
                        AYAYA_CORE_INFO("Changed window size to {0}x{1}", m_WindowWidth, m_WindowHeight);
                    }
                    ImGui::TreePop();
                }

                ImGui::EndTabItem();
            }

            // ------------------------------------------
            // 选项卡 2：渲染与后处理 (Post-Processing)
            // ------------------------------------------
            if (ImGui::BeginTabItem("Rendering")) {
                ImGui::Spacing();
                
                ImGui::TextDisabled("Tone Mapping & Color Grading");
                ImGui::Separator();
                ImGui::Spacing();

                // 曝光度调节
                ImGui::DragFloat("Exposure Compensation", &Exposure, 0.05f, 0.1f, 10.0f, "%.2fx");
                
                // 色调映射算法下拉框
                const char* tmTypes[] = { "Reinhard (Classic)", "ACES (Filmic)" };
                ImGui::Combo("Tone Mapping Algorithm", &ToneMappingType, tmTypes, 2);

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextWrapped("ACES provides a cinematic color curve that handles ultra-bright highlights better than standard Reinhard.");
                ImGui::PopStyleColor();

                ImGui::TextDisabled("Bloom (Optical Flare)");
                ImGui::Separator(); 
                ImGui::Checkbox("Enable Bloom", &EnableBloom);
                if (EnableBloom) {
                    ImGui::DragFloat("Threshold", &BloomThreshold, 0.05f, 0.0f, 10.0f, "%.2f");
                    ImGui::DragFloat("Intensity", &BloomIntensity, 0.05f, 0.0f, 5.0f, "%.2f");
                }
                ImGui::Spacing();
                
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // ==========================================
        // 【核心修复】：动态计算按钮高度，完美自适应任何缩放倍率！
        // ==========================================
        float buttonHeight = 30.0f * m_UIScale;
        if (ImGui::Button("Save Preferences", ImVec2(-1.0f, buttonHeight))) {
            m_WindowWidth = (int)ImGui::GetMainViewport()->Size.x;
            m_WindowHeight = (int)ImGui::GetMainViewport()->Size.y;
            m_UIScale = ImGui::GetIO().FontGlobalScale;
            
            SavePreferences();
        }

        ImGui::End();
    }

}