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
            m_WindowWidth = Application::Get().GetWindow().GetWidth();
            m_WindowHeight = Application::Get().GetWindow().GetHeight();
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
            }
        } catch (YAML::ParserException e) {
            AYAYA_CORE_ERROR("Failed to load preferences: {0}", e.what());
        }
    }

    void PreferencesPanel::OnImGuiRender() {
        if (!m_IsOpen) return;

        ImGui::Begin("Preferences", &m_IsOpen);

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

        // ==========================================
        // 新增：底部保存按钮区域
        // ==========================================
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // 让按钮填满宽度，稍微高一点，看起来像个确定按钮
        if (ImGui::Button("Save Preferences", ImVec2(-1.0f, 30.0f))) {
            // 点击时不仅保存文件，同时自动把当前的真实窗口大小抓取下来保存
            m_WindowWidth = Application::Get().GetWindow().GetWidth();
            m_WindowHeight = Application::Get().GetWindow().GetHeight();
            m_UIScale = ImGui::GetIO().FontGlobalScale;
            
            SavePreferences();
        }

        ImGui::End();
    }

}