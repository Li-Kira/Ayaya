#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome5.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace Ayaya {
    namespace UI {

        // ==========================================
        // Professional component header with gear icon + remove popup
        // ==========================================
        // `id` must be a stable pointer that uniquely identifies this component
        // type across frames (e.g. (void*)typeid(T).hash_code()).  Never pass a
        // temporary or dynamic string — ImGui relies on pointer identity for
        // collapse-state persistence.
        static bool DrawComponentHeader(const char* label, const char* icon,
                                         const ImVec4& color, void* id,
                                         bool defaultOpen = true,
                                         bool* outRemove = nullptr)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_Framed |
                                       ImGuiTreeNodeFlags_SpanAvailWidth |
                                       ImGuiTreeNodeFlags_AllowOverlap |
                                       ImGuiTreeNodeFlags_FramePadding;
            if (!defaultOpen) flags &= ~ImGuiTreeNodeFlags_DefaultOpen;

            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // bold
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            bool opened = ImGui::TreeNodeEx(id, flags, "%s %s", icon, label);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            // Gear button + popup — wrapped in PushID/PopID so each component
            // instance gets an isolated popup (avoids ID collision when multiple
            // components are visible simultaneously).
            ImGui::PushID(id);
            float btnSize = ImGui::GetFontSize()
                          + ImGui::GetStyle().FramePadding.y * 2.0f;
            // GetContentRegionMax accounts for scrollbar presence dynamically
            float gearX = ImGui::GetContentRegionMax().x - btnSize;
            ImGui::SameLine(gearX);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(ICON_FA_COG "##CompSettings", ImVec2(btnSize, btnSize)))
                ImGui::OpenPopup("ComponentPopup");
            ImGui::PopStyleColor();

            if (ImGui::BeginPopup("ComponentPopup")) {
                if (ImGui::MenuItem("Remove Component") && outRemove)
                    *outRemove = true;
                ImGui::EndPopup();
            }
            ImGui::PopID();

            return opened;
        }

        // ==========================================
        // Two-column property table (label | value)
        // ==========================================
        // labelRatio controls the label column share: 4 = ~20%, 2 = ~33%, 1.5 = ~40%
        static bool BeginPropertyTable(const char* id, float labelWidth = 100.0f,
                                         float labelRatio = 2.0f)
        {
            static ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_Resizable |
                                           ImGuiTableFlags_NoBordersInBody;
            if (!ImGui::BeginTable(id, 2, flags)) return false;
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, labelWidth);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch,
                                    labelWidth * labelRatio);
            return true;
        }

        // ==========================================
        // Dimmed-left label row for BeginPropertyTable.
        // Long labels are truncated with "..." to prevent column stretching.
        // ==========================================
        static void DrawPropertyLabel(const char* label)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            // Truncate if text exceeds column width
            float colW  = ImGui::GetColumnWidth();
            float avail = colW - ImGui::GetStyle().CellPadding.x * 2.0f;
            std::string text(label);
            if (avail > 0.0f && ImGui::CalcTextSize(label).x > avail) {
                while (!text.empty()
                       && ImGui::CalcTextSize((text + "...").c_str()).x > avail)
                    text.pop_back();
                if (!text.empty()) text += "...";
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
        }

        // ==========================================
        // UE5-style Vec3 controller with colored axis buttons.
        // Must be called inside an active BeginPropertyTable/EndTable pair.
        // `outCommitted` is set when the user finishes a drag or clicks a
        // reset button — use this to push a single undo command.
        // ==========================================
        static bool DrawVec3Control(const char* label, glm::vec3& values,
                                     float resetValue = 0.0f,
                                     bool* outCommitted = nullptr)
        {
            bool modified = false;
            bool committed = false;

            DrawPropertyLabel(label);
            ImGui::PushID(label);

            float colW = ImGui::GetContentRegionAvail().x;
            float lineH = ImGui::GetFrameHeight();
            // Each axis bar: 4px bar + 6px gap = 10px
            float barW = 10.0f;
            float dragTotalW = colW - 3.0f * barW;
            if (dragTotalW < 90.0f) dragTotalW = 90.0f;
            ImGui::PushMultiItemsWidths(3, dragTotalW);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            auto drawAxis = [&](const char* id, float& v, ImVec4 c)
            {
                // Thin colored vertical bar (no text)
                ImVec2 barMin(ImGui::GetCursorScreenPos().x,
                              ImGui::GetCursorScreenPos().y + 2);
                ImVec2 barMax(barMin.x + 4, barMin.y + lineH - 4);
                ImU32 barCol = ImGui::GetColorU32(c);
                ImGui::GetWindowDrawList()->AddRectFilled(barMin, barMax, barCol);
                ImGui::SetCursorScreenPos(ImVec2(barMin.x + 6, ImGui::GetCursorScreenPos().y));
                // Click on bar to reset (PushID(id) isolates X/Y/Z from each other)
                ImGui::PushID(id);
                ImGui::InvisibleButton("##reset", ImVec2(4, lineH));
                if (ImGui::IsItemClicked()) {
                    v = resetValue; modified = true; committed = true;
                }
                ImGui::PopID();
                ImGui::SameLine();
                if (ImGui::DragFloat(id, &v, 0.1f, 0.0f, 0.0f, "%.2f"))
                    modified = true;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    committed = true;
                ImGui::PopItemWidth();
            };

            drawAxis("##X", values.x, {1.0f,0.15f,0.15f,1});
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            drawAxis("##Y", values.y, {0.15f,1.0f,0.15f,1});
            ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            drawAxis("##Z", values.z, {0.2f,0.4f,1.0f,1});

            ImGui::PopStyleVar();
            ImGui::PopID();

            if (outCommitted) *outCommitted = committed;
            return modified;
        }
    }
}