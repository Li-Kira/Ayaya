#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome5.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

#include "Engine/Scene/Entity.hpp"

namespace Ayaya {
    namespace UI {

        // ==========================================
        // Entity type → icon + color (shared by SceneHierarchyPanel & TimelinePanel)
        // Colors are strictly aligned with PropertiesPanel DrawComponentHeader colors.
        // ==========================================
        struct EntityIconInfo {
            const char* Icon;
            ImVec4      Color;
        };

        // Priority order: Camera > Mesh > Sprite > DirLight > PointLight >
        // Environment > PostProcess > Animation > LuaScript > Rigidbody2D > BoxCollider2D >
        // Canvas > UIButton > UIText > UIImage > default
        static EntityIconInfo GetEntityIconInfo(const Entity& entity) {
            // 1. Camera / Rendering
            if (entity.HasComponent<CameraComponent>())
                return { ICON_FA_VIDEO,         ImVec4(0.2f, 0.6f, 0.9f, 1.0f) };  // Blue — Camera header
            if (entity.HasComponent<MeshRendererComponent>())
                return { ICON_FA_CUBE,          ImVec4(0.2f, 0.8f, 0.4f, 1.0f) };  // Green — Mesh Renderer header
            if (entity.HasComponent<SpriteRendererComponent>())
                return { ICON_FA_IMAGE,         ImVec4(0.8f, 0.3f, 0.8f, 1.0f) };  // Purple — Sprite Renderer header

            // 2. Lights
            if (entity.HasComponent<DirectionalLightComponent>())
                return { ICON_FA_SUN,           ImVec4(0.9f, 0.8f, 0.2f, 1.0f) };  // Gold — Directional Light header
            if (entity.HasComponent<PointLightComponent>())
                return { ICON_FA_LIGHTBULB,     ImVec4(0.9f, 0.6f, 0.1f, 1.0f) };  // Orange — Point Light header

            // 3. Environment / PostProcess
            if (entity.HasComponent<EnvironmentComponent>())
                return { ICON_FA_CLOUD_SUN,     ImVec4(0.4f, 0.8f, 0.9f, 1.0f) };  // Teal — Environment header
            if (entity.HasComponent<PostProcessVolumeComponent>())
                return { ICON_FA_MAGIC,         ImVec4(0.7f, 0.4f, 0.9f, 1.0f) };  // Violet — Post Process Volume header

            // 4. Animation
            if (entity.HasComponent<AnimationControllerComponent>())
                return { ICON_FA_FILM,          ImVec4(0.3f, 0.9f, 0.8f, 1.0f) };  // Teal — Animation Controller header

            // 5. Scripting
            if (entity.HasComponent<LuaScriptComponent>())
                return { ICON_FA_FILE_CODE,     ImVec4(0.9f, 0.2f, 0.5f, 1.0f) };  // Pink — Lua Script header

            // 6. Physics
            if (entity.HasComponent<Rigidbody2DComponent>())
                return { ICON_FA_BULLSEYE,      ImVec4(0.9f, 0.3f, 0.3f, 1.0f) };  // Red — Rigidbody 2D header
            if (entity.HasComponent<BoxCollider2DComponent>())
                return { ICON_FA_VECTOR_SQUARE, ImVec4(0.5f, 0.9f, 0.3f, 1.0f) };  // Green-Yellow — Box Collider 2D header

            // 7. UI
            if (entity.HasComponent<CanvasComponent>())
                return { ICON_FA_DESKTOP,       ImVec4(0.2f, 0.75f, 0.75f, 1.0f) }; // Teal — Canvas header
            if (entity.HasComponent<UIButtonComponent>())
                return { ICON_FA_HAND_POINTER,  ImVec4(0.3f, 0.9f, 0.3f, 1.0f) };  // Green — UI Button header
            if (entity.HasComponent<UITextComponent>())
                return { ICON_FA_FONT,          ImVec4(0.9f, 0.6f, 0.2f, 1.0f) };  // Orange — UI Text header
            if (entity.HasComponent<UIImageComponent>())
                return { ICON_FA_IMAGE,         ImVec4(0.8f, 0.3f, 0.8f, 1.0f) };  // Purple — UI Image header

            // 8. Default
            return { ICON_FA_CUBE,              ImVec4(0.7f, 0.7f, 0.7f, 1.0f) };  // Gray
        }

        // ==========================================
        // Popup/menu helpers
        // ==========================================
        static void PushPopupStyles(float minWidth = 240.0f)
        {
            ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, 0), ImVec2(FLT_MAX, FLT_MAX));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg,        ImVec4(0.12f, 0.12f, 0.12f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.20f, 0.35f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Header,         ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0, 0, 0, 0));
        }
        static void PopPopupStyles()
        {
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(4);
        }

        static void DrawMenuHeader(const char* title)
        {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        static bool DrawMenuItem(const char* label, const char* icon = nullptr,
                                  const char* shortcut = nullptr, bool enabled = true,
                                  bool hasSubMenu = false, bool* outHovered = nullptr)
        {
            // Fixed column layout for pixel-perfect alignment
            const float iconColW = 40.0f;
            const float rightPad = 8.0f;
            const float chevronW = 12.0f;

            if (!enabled)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

            float fullW = ImGui::GetContentRegionAvail().x;
            if (fullW < 180.0f) fullW = 180.0f;

            // Selectable as invisible hit-target background
            ImGui::PushID(label);
            bool clicked = ImGui::Selectable("##Item", false,
                enabled ? 0 : ImGuiSelectableFlags_Disabled, ImVec2(fullW, 0));
            ImGui::PopID();

            if (outHovered) *outHovered = ImGui::IsItemHovered();

            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            float  centerY = itemMin.y + (itemMax.y - itemMin.y) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // 1. Icon — centred inside the fixed icon column
            if (icon) {
                ImVec2 sz = ImGui::CalcTextSize(icon);
                dl->AddText(ImVec2(itemMin.x + (iconColW - sz.x) * 0.5f, centerY - sz.y * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), icon);
            }

            // 2. Label — full iconColW when icon present, half when no icon
            float labelX = itemMin.x + (icon ? iconColW : iconColW * 0.5f);
            ImVec2 lsz = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(labelX, centerY - lsz.y * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), label);

            // 3. Shortcut — right-aligned, pushed left if chevron is present
            if (shortcut) {
                ImVec2 ssz = ImGui::CalcTextSize(shortcut);
                float  sx  = itemMax.x - rightPad
                           - (hasSubMenu ? chevronW + 6.0f : 0.0f) - ssz.x;
                dl->AddText(ImVec2(sx, centerY - ssz.y * 0.5f),
                            enabled ? IM_COL32(128,128,128,255) : IM_COL32(80,80,80,255),
                            shortcut);
            }

            // 4. Chevron — right-aligned submenu indicator
            if (hasSubMenu) {
                ImVec2 csz = ImGui::CalcTextSize(ICON_FA_CHEVRON_RIGHT);
                dl->AddText(ImVec2(itemMax.x - rightPad - csz.x, centerY - csz.y * 0.5f),
                            IM_COL32(150,150,150,255), ICON_FA_CHEVRON_RIGHT);
            }

            if (!enabled) ImGui::PopStyleColor();
            return clicked;
        }
        // ==========================================
        // Transparent-render-hijack menu items
        // Native MenuItem/BeginMenu handle ALL interaction logic
        // (hover, click, Safe Triangle, popup stack).
        // We hide their native text and overlay our layout.
        // ==========================================

        // Pure rendering using explicit rect + drawlist (survives BeginMenu window-context switch).
        static void RenderAdvancedMenuLayout(ImDrawList* dl, const ImVec2& rMin, const ImVec2& rMax,
                                              const char* label, const char* icon,
                                              const char* shortcut, bool hasSubMenu, bool enabled)
        {
            float cy = rMin.y + (rMax.y - rMin.y) * 0.5f;
            const float iconColW = 40.0f;
            const float rightPad = 8.0f;

            ImU32 textCol = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            ImU32 dimCol  = enabled ? IM_COL32(128, 128, 128, 255) : IM_COL32(80, 80, 80, 255);

            // Custom hover highlight — inset from item edges with rounding
            if (enabled && ImGui::IsItemHovered()) {
                float m = 6.0f;
                dl->AddRectFilled(ImVec2(rMin.x + m, rMin.y + 1.0f),
                                  ImVec2(rMax.x - m, rMax.y - 1.0f),
                                  ImGui::GetColorU32(ImGuiCol_HeaderHovered), 8.0f);
            }

            if (icon) {
                ImVec2 sz = ImGui::CalcTextSize(icon);
                dl->AddText(ImVec2(rMin.x + (iconColW - sz.x) * 0.5f, cy - sz.y * 0.5f), textCol, icon);
            }
            float labelX = rMin.x + (icon ? iconColW : iconColW * 0.5f);
            ImVec2 lsz = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(labelX, cy - lsz.y * 0.5f), textCol, label);

            if (hasSubMenu) {
                ImVec2 csz = ImGui::CalcTextSize(ICON_FA_CHEVRON_RIGHT);
                dl->AddText(ImVec2(rMax.x - rightPad - csz.x, cy - csz.y * 0.5f), dimCol, ICON_FA_CHEVRON_RIGHT);
            }
            if (shortcut) {
                ImVec2 ssz = ImGui::CalcTextSize(shortcut);
                float  off = rightPad + (hasSubMenu ? 18.0f : 0.0f) + ssz.x;
                dl->AddText(ImVec2(rMax.x - off, cy - ssz.y * 0.5f), dimCol, shortcut);
            }
        }

        // Regular menu item — native MenuItem for logic, our layout for visuals.
        static bool DrawNativeMenuItem(const char* label, const char* icon = nullptr,
                                        const char* shortcut = nullptr, bool enabled = true)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0, 0, 0, 0));
            bool clicked = ImGui::MenuItem(label, shortcut, false, enabled);
            ImGui::PopStyleColor(2);
            RenderAdvancedMenuLayout(ImGui::GetWindowDrawList(),
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                label, icon, shortcut, false, enabled);
            return clicked;
        }

        // Submenu trigger — native BeginMenu for logic, our layout for visuals.
        // Must snapshot drawlist + rect BEFORE BeginMenu because it switches
        // the current window into the child popup when the menu is open.
        static bool BeginNativeMenu(const char* label, const char* icon = nullptr,
                                     bool enabled = true, float childMinWidth = 200.0f)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            // Constrain child popup width so long labels don't clip
            ImGui::SetNextWindowSizeConstraints(ImVec2(childMinWidth, 0), ImVec2(FLT_MAX, FLT_MAX));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
            bool open = ImGui::BeginMenu(label, enabled);
            ImGui::PopStyleColor();
            RenderAdvancedMenuLayout(dl,
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                label, icon, nullptr, true, enabled);
            return open;
        }

        static void EndNativeMenu() { ImGui::EndMenu(); }

        static void MenuSeparator()
        {
            ImGui::Spacing();
            ImVec2 pMin = ImGui::GetCursorScreenPos();
            float margin = 8.0f;
            float width = ImGui::GetContentRegionAvail().x - margin * 2.0f;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(pMin.x + margin, pMin.y),
                ImVec2(pMin.x + margin + width, pMin.y),
                IM_COL32(20, 20, 20, 255));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
            ImGui::Spacing();
        }

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
                if (ImGui::MenuItem("Remove Component"))
                    if (outRemove) *outRemove = true;
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
        // Vec3 controller with colored axis buttons.
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