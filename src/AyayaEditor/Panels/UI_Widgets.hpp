#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace Ayaya {
    namespace UI {

        // 绘制带颜色标签的 Vec3 控制器 (Unity / UE5 风格)
        // 返回值：如果数值发生了改变（被拖拽或点击了重置），返回 true
        static bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f) {
            bool valueChanged = false;
            
            ImGuiIO& io = ImGui::GetIO();
            // 假设索引 1 是你上一轮加载的粗体字体 (Roboto-Bold.ttf)。
            // 如果没加粗体，用备用逻辑防止崩溃
            auto boldFont = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0]; 

            ImGui::PushID(label.c_str());

            ImGui::Columns(2);
            ImGui::SetColumnWidth(0, columnWidth);
            ImGui::Text("%s", label.c_str());
            ImGui::NextColumn();

            ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

            float lineHeight = ImGui::GetFrameHeight();
            ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

            // ==========================================
            // X 轴 (红色)
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("X", buttonSize)) {
                values.x = resetValue;
                valueChanged = true;
            }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
                valueChanged = true;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // ==========================================
            // Y 轴 (绿色)
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("Y", buttonSize)) {
                values.y = resetValue;
                valueChanged = true;
            }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
                valueChanged = true;
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();

            // ==========================================
            // Z 轴 (蓝色)
            // ==========================================
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
            ImGui::PushFont(boldFont);
            if (ImGui::Button("Z", buttonSize)) {
                values.z = resetValue;
                valueChanged = true;
            }
            ImGui::PopFont();
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
                valueChanged = true;
            }
            ImGui::PopItemWidth();

            ImGui::PopStyleVar();
            ImGui::Columns(1);
            ImGui::PopID();

            return valueChanged;
        }
    }
}