#include "ayapch.h"
#include "ScreenshotPanel.hpp"
#include "Utils/PlatformUtils.hpp" // 确保包含 FileDialogs 的头文件

#include <imgui.h>

namespace Ayaya {

    void ScreenshotPanel::OnImGuiRender() {
        if (m_ShowPopup) {
            ImGui::OpenPopup("Export High-Res Screenshot");
            m_ShowPopup = false;
        }

        if (ImGui::BeginPopupModal("Export High-Res Screenshot", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Select Output Resolution");
            ImGui::Spacing();
            
            // 预设分辨率快捷按钮
            if (ImGui::Button("720p")) { m_Width = 1280; m_Height = 720; } ImGui::SameLine();
            if (ImGui::Button("1080p")) { m_Width = 1920; m_Height = 1080; } ImGui::SameLine();
            if (ImGui::Button("2K")) { m_Width = 2560; m_Height = 1440; } ImGui::SameLine();
            if (ImGui::Button("4K")) { m_Width = 3840; m_Height = 2160; }
            
            ImGui::Spacing();
            int w = (int)m_Width;
            int h = (int)m_Height;
            ImGui::InputInt("Width", &w);
            ImGui::InputInt("Height", &h);
            m_Width = w > 0 ? w : 1;
            m_Height = h > 0 ? h : 1;
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("Save as PNG", ImVec2(120, 0))) {
                std::string filepath = FileDialogs::SaveFile("png", "screenshot.png");
                if (!filepath.empty()) {
                    m_Path = filepath;
                    m_Pending = true; // 打上截图标记，渲染器下一帧会自动抓取！
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
            
            ImGui::EndPopup();
        }
    }

}