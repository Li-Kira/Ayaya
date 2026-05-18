#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace Ayaya {
    namespace UI {

        // 绘制带颜色标签的 Vec3 控制器 (带高级状态拦截)
        static bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f, bool* outActivated = nullptr, bool* outDeactivated = nullptr) {
            bool valueChanged = false;
            bool activated = false;
            bool deactivated = false;

            ImGuiIO& io = ImGui::GetIO();
            auto boldFont = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0];

            ImGui::PushID(label.c_str());
            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, columnWidth);
            ImGui::Text("%s", label.c_str());
            ImGui::NextColumn();

            float colW = ImGui::GetContentRegionAvail().x;
            float lineHeight = ImGui::GetFrameHeight();
            ImVec2 buttonSize = { lineHeight + 0.5f, lineHeight };
            // Drag inputs get the column width minus the 3 X/Y/Z buttons
            float dragTotalW = colW - 3.0f * buttonSize.x;
            if (dragTotalW < 90.0f) dragTotalW = 90.0f; // minimum for 3 usable drags
            ImGui::PushMultiItemsWidths(3, dragTotalW);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

            // ==========================================
            // X 轴
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("X", buttonSize)) { values.x = resetValue; valueChanged = true; activated = true; deactivated = true; }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) valueChanged = true;
            if (ImGui::IsItemActivated()) activated = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // ==========================================
            // Y 轴
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("Y", buttonSize)) { values.y = resetValue; valueChanged = true; activated = true; deactivated = true; }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) valueChanged = true;
            if (ImGui::IsItemActivated()) activated = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // ==========================================
            // Z 轴
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("Z", buttonSize)) { values.z = resetValue; valueChanged = true; activated = true; deactivated = true; }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) valueChanged = true;
            if (ImGui::IsItemActivated()) activated = true;
            if (ImGui::IsItemDeactivatedAfterEdit()) deactivated = true;
            ImGui::PopItemWidth();

            ImGui::PopStyleVar();
            ImGui::Columns(1);
            ImGui::PopID();

            // 将状态抛出
            if (outActivated) *outActivated = activated;
            if (outDeactivated) *outDeactivated = deactivated;

            return valueChanged;
        }
    }
}