#include "TimelinePanel.hpp"
#include "Engine/Scene/Scene.hpp"
#include "Engine/Animation/AnimationSystem.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "../EditorLayer.hpp"

#include <imgui_internal.h>
#include <IconsFontAwesome5.h>
#include <algorithm>
#include <cmath>

namespace Ayaya {

    void TimelinePanel::SetContext(std::shared_ptr<Scene> scene) {
        StopPreview();
        m_Scene = scene;
        m_ExpandedEntities.clear();
    }

    float TimelinePanel::TimeToScreenX(float time, float canvasX) const {
        return canvasX + time * m_Zoom + m_ScrollX;
    }

    float TimelinePanel::ScreenXToTime(float x, float canvasX) const {
        return (x - canvasX - m_ScrollX) / m_Zoom;
    }

    float TimelinePanel::SnapToNearestKeyframe(float time) const {
        if (!m_Scene) return time;
        float snapDist = kSnapThresholdPx / m_Zoom; // threshold in seconds
        float bestTime = time;
        float bestDist = snapDist;

        auto view = m_Scene->Reg().view<AnimationControllerComponent>();
        for (auto e : view) {
            Entity entity{e, m_Scene.get()};
            auto& ctrl = entity.GetComponent<AnimationControllerComponent>();
            for (auto& track : ctrl.Tracks) {
                auto curve = track.CurveHandle
                    ? AssetManager::GetAsset<CurveAsset>(track.CurveHandle) : nullptr;
                if (!curve || curve->Keys.empty()) continue;
                for (const auto& k : curve->Keys) {
                    float keyTime = track.TimeOffset + k.Time;
                    float dist = std::abs(keyTime - time);
                    if (dist < bestDist) { bestDist = dist; bestTime = keyTime; }
                }
            }
        }
        return bestTime;
    }

    // ==========================================
    // Snapshot / Restore
    // ==========================================
    void TimelinePanel::StartPreview() {
        if (!m_Scene || m_IsPreviewing) return;
        m_Snapshots.clear();
        auto view = m_Scene->Reg().view<AnimationControllerComponent>();
        for (auto e : view) {
            Entity entity{e, m_Scene.get()};
            UUID uuid = entity.GetComponent<IDComponent>().ID;
            EntitySnapshot snap;
            if (entity.HasComponent<TransformComponent>()) {
                auto& t = entity.GetComponent<TransformComponent>();
                snap.Translation = t.Translation; snap.Rotation = t.Rotation; snap.Scale = t.Scale;
                snap.HasTransform = true;
            }
            if (entity.HasComponent<SpriteRendererComponent>()) {
                snap.SpriteColor = entity.GetComponent<SpriteRendererComponent>().Color; snap.HasSprite = true;
            }
            if (entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>().Camera;
                snap.CameraFOV = cam.GetPerspectiveFOV(); snap.CameraOrthoSize = cam.GetOrthographicSize();
                snap.HasCamera = true;
            }
            if (entity.HasComponent<PointLightComponent>()) {
                auto& pl = entity.GetComponent<PointLightComponent>();
                snap.PointLightIntensity = pl.LuminousPower; snap.PointLightRadius = pl.Radius;
                snap.HasPointLight = true;
            }
            if (entity.HasComponent<DirectionalLightComponent>()) {
                snap.DirLightIntensity = entity.GetComponent<DirectionalLightComponent>().Illuminance;
                snap.HasDirLight = true;
            }
            if (entity.HasComponent<UIImageComponent>()) {
                snap.UIOpacity = entity.GetComponent<UIImageComponent>().Color.a; snap.HasUIImage = true;
            }
            if (entity.HasComponent<UITextComponent>()) {
                snap.UIOpacity = entity.GetComponent<UITextComponent>().Color.a; snap.HasUIText = true;
            }
            m_Snapshots[uuid] = snap;
        }
        m_IsPreviewing = true;
    }

    void TimelinePanel::StopPreview() {
        if (!m_Scene) return;
        for (auto& [uuid, snap] : m_Snapshots) {
            Entity entity = m_Scene->GetEntityByUUID(uuid);
            if (!entity) continue;
            if (snap.HasTransform && entity.HasComponent<TransformComponent>()) {
                auto& t = entity.GetComponent<TransformComponent>();
                t.Translation = snap.Translation; t.Rotation = snap.Rotation; t.Scale = snap.Scale;
            }
            if (snap.HasSprite && entity.HasComponent<SpriteRendererComponent>())
                entity.GetComponent<SpriteRendererComponent>().Color = snap.SpriteColor;
            if (snap.HasCamera && entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>().Camera;
                cam.SetPerspectiveFOV(snap.CameraFOV); cam.SetOrthographicSize(snap.CameraOrthoSize);
            }
            if (snap.HasPointLight && entity.HasComponent<PointLightComponent>()) {
                auto& pl = entity.GetComponent<PointLightComponent>();
                pl.LuminousPower = snap.PointLightIntensity; pl.Radius = snap.PointLightRadius;
            }
            if (snap.HasDirLight && entity.HasComponent<DirectionalLightComponent>())
                entity.GetComponent<DirectionalLightComponent>().Illuminance = snap.DirLightIntensity;
            if (snap.HasUIImage && entity.HasComponent<UIImageComponent>())
                entity.GetComponent<UIImageComponent>().Color.a = snap.UIOpacity;
            if (snap.HasUIText && entity.HasComponent<UITextComponent>())
                entity.GetComponent<UITextComponent>().Color.a = snap.UIOpacity;
        }
        m_Snapshots.clear();
        m_PreviewTime = 0.0f; m_IsPlaying = false; m_IsPreviewing = false;
    }

    // ==========================================
    // Draw helpers
    // ==========================================
    static void DrawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border) {
        ImVec2 p[4] = {{c.x, c.y-r}, {c.x+r, c.y}, {c.x, c.y+r}, {c.x-r, c.y}};
        dl->AddConvexPolyFilled(p, 4, fill);
        dl->AddPolyline(p, 4, border, ImDrawFlags_Closed, 1.0f);
    }

    // ==========================================
    // Track Cell (clip + keyframes inside a table cell)
    // ==========================================
    void TimelinePanel::DrawTrackCell(ImVec2 cellMin, ImVec2 cellSize,
                                       AnimationTrack& track,
                                       Entity entity, int trackIdx) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float canvasX = m_CanvasX;

        auto curve = track.CurveHandle
            ? AssetManager::GetAsset<CurveAsset>(track.CurveHandle) : nullptr;

        float clipS = TimeToScreenX(track.TimeOffset, canvasX);
        float dur = 1.0f;
        if (curve && !curve->Keys.empty())
            dur = curve->Keys.back().Time - curve->Keys.front().Time;
        float clipE = clipS + dur * m_Zoom;
        if (clipE - clipS < kMinClipWidth) clipE = clipS + kMinClipWidth;

        // ---- Gradient clip (clipped to cell bounds) ----
        ImVec2 cMin(clipS, cellMin.y + 3.0f);
        ImVec2 cMax(clipE, cellMin.y + cellSize.y - 3.0f);

        dl->PushClipRect(ImVec2(cellMin.x, cellMin.y),
                         ImVec2(cellMin.x + cellSize.x, cellMin.y + cellSize.y), true);

        // 1. Dark green semi-transparent base
        dl->AddRectFilled(cMin, cMax, IM_COL32(30, 80, 50, 150), 4.0f);
        // 2. Top 30% subtle highlight (Clip bevel)
        dl->AddRectFilled(cMin, ImVec2(cMax.x, cMin.y + (cMax.y - cMin.y) * 0.3f),
                          IM_COL32(255, 255, 255, 15), 4.0f, ImDrawFlags_RoundCornersTop);
        // 3. Outer border
        dl->AddRect(cMin, cMax, IM_COL32(50, 150, 80, 200), 4.0f);
        // 4. Clip label (truncated with ellipsis if too wide)
        {
            const char* fullLabel = GetTargetPropertyName(track.Property);
            float availW = (cMax.x - cMin.x) - 12.0f; // 6px padding each side
            if (availW > 10.0f) {
                std::string label(fullLabel);
                float textW = ImGui::CalcTextSize(label.c_str()).x;
                if (textW > availW) {
                    while (!label.empty() && ImGui::CalcTextSize((label + "...").c_str()).x > availW)
                        label.pop_back();
                    label += "...";
                }
                dl->AddText(ImVec2(cMin.x + 6.0f, cMin.y + 2.0f),
                            IM_COL32(210, 235, 215, 230), label.c_str());
            }
        }

        // ---- Keyframe diamonds ----
        if (curve && !curve->Keys.empty()) {
            for (const auto& k : curve->Keys) {
                float kx = TimeToScreenX(track.TimeOffset + k.Time, canvasX);
                if (kx >= cMin.x && kx <= cMax.x)
                    DrawDiamond(dl, ImVec2(kx, cellMin.y + cellSize.y * 0.5f), 4.5f,
                                IM_COL32(240, 240, 240, 255),
                                IM_COL32(30, 30, 30, 200));
            }
        }

        dl->PopClipRect();

        // ---- Clip drag interaction ----
        ImGui::SetCursorScreenPos(cMin);
        ImGui::InvisibleButton("##ClipDrag", ImVec2(cMax.x - cMin.x, cMax.y - cMin.y));
        if (ImGui::IsItemActivated()) {
            m_PreDragComponent = entity.GetComponent<AnimationControllerComponent>();
            m_DragEntity = entity;
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float no = track.TimeOffset + ImGui::GetIO().MouseDelta.x / m_Zoom;
            if (no < 0.0f) no = 0.0f;
            track.TimeOffset = no;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float old = m_PreDragComponent.Tracks[trackIdx].TimeOffset;
            if (std::abs(track.TimeOffset - old) > 0.001f && m_DragEntity) {
                EditorLayer::Get().GetCommandHistory().AddCommand(
                    std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                        m_DragEntity, m_PreDragComponent,
                        entity.GetComponent<AnimationControllerComponent>()));
            }
            m_DragEntity = Entity{};
        }
    }

    // ==========================================
    // Canvas input: zoom + horizontal scroll + canvas click
    //   wheel / wheelH are pre-saved before BeginTable (table's ScrollY
    //   consumes io.MouseWheel during EndTable, so we must capture early).
    // ==========================================
    void TimelinePanel::HandleCanvasInput(float wheel, float wheelH) {
        if (m_CanvasX <= 0.0f) return;
        // Only process input when the Timeline window is actually hovered
        // (prevents cross-window leakage, e.g. ContentBrowser clicks moving playhead)
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) return;
        ImVec2 mp = ImGui::GetMousePos();
        float canvasRight = m_CanvasX + m_CanvasWidth;
        bool hov = (mp.x >= m_CanvasX && mp.x <= canvasRight &&
                    mp.y >= m_TableTopY);
        if (!hov) return;

        // Ctrl+Wheel: zoom at cursor
        if (ImGui::GetIO().KeyCtrl && wheel != 0.0f) {
            float oldT = ScreenXToTime(mp.x, m_CanvasX);
            m_Zoom *= 1.0f + wheel * 0.1f;
            m_Zoom = std::clamp(m_Zoom, 20.0f, 2000.0f);
            m_ScrollX = mp.x - m_CanvasX - oldT * m_Zoom;
        }
        // Horizontal mouse wheel: pan
        if (wheelH != 0.0f) {
            m_ScrollX += wheelH * 50.0f;
        }

        // Middle-mouse drag: free pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            m_ScrollX += ImGui::GetIO().MouseDelta.x;
        }

        // ---- Canvas background click → scrub (with keyframe snap) ----
        // Only in the track area (below ruler), and only when no item is active
        if (mp.y >= m_TableTopY + kRulerHeight &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemActive()) {
            float t = ScreenXToTime(mp.x, m_CanvasX);
            if (t >= 0.0f) { m_PreviewTime = SnapToNearestKeyframe(t); StartPreview(); }
        }
    }

    // ==========================================
    // OnImGuiRender (standalone)
    // ==========================================
    void TimelinePanel::OnImGuiRender() {
        if (!m_IsOpen) return;
        ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Timeline", &m_IsOpen)) { ImGui::End(); return; }
        RenderContent();
        ImGui::End();
        if (!m_IsOpen && m_IsPreviewing) StopPreview();
    }

    // ==========================================
    // RenderContent — Sequencer Table
    //   Ruler drawn frozen OUTSIDE scroll area; table body (no ScrollY)
    //   scrolls inside BeginChild.  This eliminates the table's internal
    //   channel-split z-order issue — all overlay drawing after EndChild
    //   is naturally on top of both ruler and body.
    // ==========================================
    void TimelinePanel::RenderContent() {
        if (!m_Scene) { ImGui::TextDisabled("No scene loaded"); return; }

        // ---- Bottom transport controls ----
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Space)) {
            if (m_IsPlaying) { m_IsPlaying = false; }
            else { StartPreview(); m_IsPlaying = true; }
        }
        if (ImGui::Button(m_IsPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY)) {
            if (m_IsPlaying) { m_IsPlaying = false; }
            else { StartPreview(); m_IsPlaying = true; }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP)) StopPreview();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO " Reset")) { m_ScrollX = 50.0f; m_Zoom = 500.0f; }
        ImGui::SameLine();
        ImGui::Text("  t=%.3f  zoom=%.0fpx/s", m_PreviewTime, m_Zoom);
        ImGui::Separator();

        // ---- Layout constants ----
        ImGuiStyle& style = ImGui::GetStyle();
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        float scrollbarW = style.ScrollbarSize;
        ImVec2  areaPos   = ImGui::GetCursorScreenPos();
        float   areaW     = ImGui::GetContentRegionAvail().x;
        if (areaW < m_OutlinerWidth + 100.0f) areaW = m_OutlinerWidth + 100.0f;
        float outlinerW   = m_OutlinerWidth;
        float canvasX     = areaPos.x + outlinerW;
        float canvasW     = areaW - outlinerW - scrollbarW;
        if (canvasW < 50.0f) canvasW = 50.0f;

        m_CanvasX     = canvasX;
        m_CanvasWidth = canvasW;
        m_TableTopY   = areaPos.y;

        // ==========================================
        // Frozen ruler header (outside scroll area)
        // ==========================================
        float rulerBottom = areaPos.y + kRulerHeight;

        // Outliner header bg
        dl->AddRectFilled(ImVec2(areaPos.x, areaPos.y),
                          ImVec2(canvasX, rulerBottom),
                          IM_COL32(36, 36, 42, 255));
        {
            float textH = ImGui::GetFontSize();
            float textY = areaPos.y + (kRulerHeight - textH) * 0.5f;
            dl->AddText(ImVec2(areaPos.x + 8.0f, textY),
                        IM_COL32(150, 150, 165, 255), "Track Name");
        }

        // Ruler bg (cover full width including scrollbar gutter)
        dl->AddRectFilled(ImVec2(canvasX, areaPos.y),
                          ImVec2(areaPos.x + areaW, areaPos.y + kRulerHeight),
                          IM_COL32(36, 36, 42, 255));
        dl->AddLine(ImVec2(canvasX, areaPos.y + kRulerHeight),
                    ImVec2(areaPos.x + areaW, areaPos.y + kRulerHeight),
                    IM_COL32(18, 18, 22, 255), 1.0f);

        // Ruler ticks (clipped to canvas area)
        dl->PushClipRect(ImVec2(canvasX, areaPos.y),
                         ImVec2(areaPos.x + areaW, areaPos.y + kRulerHeight), true);
        float timePerTick = 1.0f;
        if (m_Zoom < 30.0f)      timePerTick = 5.0f;
        else if (m_Zoom < 60.0f) timePerTick = 2.0f;
        else if (m_Zoom > 300.0f) timePerTick = 0.2f;
        else if (m_Zoom > 150.0f) timePerTick = 0.5f;
        float firstTick = std::floor(ScreenXToTime(canvasX, canvasX) / timePerTick) * timePerTick;
        float tickBottom = rulerBottom;
        float majorTop   = areaPos.y + kRulerHeight * 0.40f;
        float minorTop   = areaPos.y + kRulerHeight * 0.80f;
        for (float t = firstTick; t < ScreenXToTime(canvasX + canvasW + 100, canvasX); t += timePerTick) {
            float ms = timePerTick / 5.0f;
            for (int m = 0; m < 5; m++) {
                float mx = TimeToScreenX(t + m * ms, canvasX);
                if (mx > canvasX + canvasW) break;
                if (m == 0) {
                    dl->AddLine(ImVec2(mx, majorTop), ImVec2(mx, tickBottom),
                                IM_COL32(180, 180, 180, 255));
                    char buf[16]; snprintf(buf, sizeof(buf), "%.1f", t + m * ms);
                    dl->AddText(ImVec2(mx + 3.0f, areaPos.y + 2.0f),
                                IM_COL32(190, 190, 205, 255), buf);
                } else {
                    dl->AddLine(ImVec2(mx, minorTop), ImVec2(mx, tickBottom),
                                IM_COL32(100, 100, 100, 150));
                }
            }
        }
        dl->PopClipRect();

        // Vertical divider between outliner and canvas (ruler region)
        dl->AddLine(ImVec2(canvasX, areaPos.y), ImVec2(canvasX, areaPos.y + kRulerHeight),
                    IM_COL32(50, 50, 60, 255), 2.0f);

        // ---- Manual column resize handle (ruler region only) ----
        // Limited to ruler height so the playhead-drag InvisibleButton
        // (which spans ruler + pill area) is not obstructed at the left edge.
        {
            const float kResizeHandleHW = 4.0f;
            ImGui::SetCursorScreenPos(ImVec2(canvasX - kResizeHandleHW, areaPos.y));
            ImGui::InvisibleButton("##TimelineColResize",
                                   ImVec2(kResizeHandleHW * 2.0f, kRulerHeight));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive())
            {
                float newW = ImGui::GetIO().MousePos.x - areaPos.x;
                if (newW < 100.0f) newW = 100.0f;
                if (newW > areaW - 150.0f) newW = areaW - 150.0f;
                m_OutlinerWidth = newW;
            }
        }

        // ==========================================
        // Scrollable table body (BeginChild, not ScrollY)
        // ==========================================
        ImGui::SetCursorScreenPos(ImVec2(areaPos.x, areaPos.y + kRulerHeight));
        // Reserve space for bottom bar (separator + compact slider row)
        // Separator: ~ItemSpacing.y*2 + 1px   Text/Slider: ~1 line
        float bottomBarH = ImGui::GetTextLineHeightWithSpacing()
                         + ImGui::GetStyle().ItemSpacing.y + 6.0f;
        float childH     = ImGui::GetContentRegionAvail().y - bottomBarH;
        ImVec2 tableAreaSize = ImVec2(areaW, childH);
        if (tableAreaSize.y < kTrackHeight * 2.0f) tableAreaSize.y = kTrackHeight * 2.0f;

        static ImGuiTableFlags tableFlags =
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings;

        ImGui::PushStyleColor(ImGuiCol_TableRowBg,    ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

        // Save wheel before child consumes it
        float preTableWheel  = ImGui::GetIO().MouseWheel;
        float preTableWheelH = ImGui::GetIO().MouseWheelH;

        if (ImGui::BeginChild("##TimelineScrollArea", tableAreaSize, false)) {

            if (ImGui::BeginTable("TimelineSequencer", 2, tableFlags)) {
                ImGui::TableSetupColumn("Outliner", ImGuiTableColumnFlags_WidthFixed, kTrackListWidth);
                ImGui::TableSetupColumn("Canvas",   ImGuiTableColumnFlags_WidthStretch);

                // Sync table column width to tracked value.
                // Skip first frame (MinColumnWidth not yet set by TableUpdateLayout).
                if (ImGui::GetCurrentTable()->MinColumnWidth > 0.0f)
                    ImGui::TableSetColumnWidth(0, m_OutlinerWidth);

                auto view = m_Scene->Reg().view<AnimationControllerComponent>();
                for (auto e : view) {
                    Entity entity{e, m_Scene.get()};
                    auto& ctrl = entity.GetComponent<AnimationControllerComponent>();
                    UUID   uuid = entity.GetComponent<IDComponent>().ID;
                    std::string name = entity.GetComponent<TagComponent>().Tag;
                    bool expanded = m_ExpandedEntities.count(uuid) != 0;

                    ImGui::TableNextRow(0, kTrackHeight);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();

                    auto iconInfo = UI::GetEntityIconInfo(entity);

                    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (expanded) fl |= ImGuiTreeNodeFlags_DefaultOpen;

                    ImGui::PushID((int)e);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.84f, 0.92f, 1.0f));
                    bool nodeOpen = ImGui::TreeNodeEx(name.c_str(), fl, "%s  %s", iconInfo.Icon, name.c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        if (expanded) m_ExpandedEntities.erase(uuid);
                        else          m_ExpandedEntities.insert(uuid);
                    }

                    ImGui::TableSetColumnIndex(1);

                    if (nodeOpen) {
                        for (int i = 0; i < (int)ctrl.Tracks.size(); i++) {
                            auto& track = ctrl.Tracks[i];
                            bool hasKeys = track.CurveHandle != 0;
                            auto curve = hasKeys
                                ? AssetManager::GetAsset<CurveAsset>(track.CurveHandle) : nullptr;
                            hasKeys = curve && !curve->Keys.empty();

                            ImGui::TableNextRow(0, kTrackHeight);
                            ImGui::TableSetColumnIndex(0);
                            ImGui::AlignTextToFramePadding();
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                hasKeys ? ImVec4(0.70f, 0.76f, 0.85f, 1.0f)
                                        : ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
                            ImGui::Text("%s  %s",
                                hasKeys ? ICON_FA_CIRCLE : ICON_FA_LINK,
                                GetTargetPropertyName(track.Property));
                            ImGui::PopStyleColor();

                            ImGui::TableSetColumnIndex(1);
                            ImVec2 cellMin = ImGui::GetCursorScreenPos();
                            ImVec2 cellSize = ImGui::GetContentRegionAvail();
                            cellSize.y = kTrackHeight;
                            ImGui::PushID(i);
                            DrawTrackCell(cellMin, cellSize, track, entity, i);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            // ---- Grid + playhead (child's draw list: on top of clips) ----
            if (m_CanvasX > 0.0f) {
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                float cTop = ImGui::GetWindowPos().y;
                float cBot = cTop + ImGui::GetWindowHeight();

                cdl->PushClipRect(ImVec2(m_CanvasX, cTop),
                                  ImVec2(m_CanvasX + m_CanvasWidth, cBot), true);
                float gTick = 1.0f;
                if (m_Zoom < 30.0f)      gTick = 5.0f;
                else if (m_Zoom < 60.0f) gTick = 2.0f;
                else if (m_Zoom > 300.0f) gTick = 0.2f;
                else if (m_Zoom > 150.0f) gTick = 0.5f;
                float gFirst = std::floor(ScreenXToTime(m_CanvasX, m_CanvasX) / gTick) * gTick;
                for (float t = gFirst; t < ScreenXToTime(m_CanvasX + m_CanvasWidth + 100, m_CanvasX); t += gTick) {
                    float ms = gTick / 5.0f;
                    for (int m = 0; m < 5; m++) {
                        float mx = TimeToScreenX(t + m * ms, m_CanvasX);
                        if (mx > m_CanvasX + m_CanvasWidth) break;
                        cdl->AddLine(ImVec2(mx, cTop), ImVec2(mx, cBot),
                                     m == 0 ? IM_COL32(255,255,255,15) : IM_COL32(255,255,255,5), 1.0f);
                    }
                }
                float px = TimeToScreenX(m_PreviewTime, m_CanvasX);
                cdl->AddLine(ImVec2(px - 2.0f, cTop), ImVec2(px - 2.0f, cBot), IM_COL32(255,50,50,30), 2.0f);
                cdl->AddLine(ImVec2(px + 2.0f, cTop), ImVec2(px + 2.0f, cBot), IM_COL32(255,50,50,30), 2.0f);
                cdl->AddLine(ImVec2(px, cTop), ImVec2(px, cBot), IM_COL32(255,55,55,255), 1.0f);
                cdl->PopClipRect();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        // ---- Bottom status bar ----
        {
            ImGui::Separator();
            int objCount = (int)m_Scene->Reg()
                               .view<AnimationControllerComponent>().size();
            ImGui::Text("Objects: %d", objCount);
            ImGui::SameLine();
            float avail   = ImGui::GetContentRegionAvail().x;
            float sliderW = 160.0f;
            float sliderX = ImGui::GetCursorPosX() + avail - sliderW;
            if (sliderX > ImGui::GetCursorPosX())
                ImGui::SetCursorPosX(sliderX);
            ImGui::SetNextItemWidth(sliderW);

            // Match ContentBrowser slider style
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab,       ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            float zoomBefore = m_Zoom;
            ImGui::SliderFloat("##ZoomSlider", &m_Zoom, 20.0f, 2000.0f, "");
            // Keep playhead at same screen position when zoom changes
            if (m_Zoom != zoomBefore)
                m_ScrollX += m_PreviewTime * (zoomBefore - m_Zoom);
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();
        }

        // ---- Pill + ruler-region line (parent draw list; track-region
        //      grid + line drawn inside child above so they sit on top of clips) ----
        if (m_CanvasX > 0.0f) {
            float px   = TimeToScreenX(m_PreviewTime, m_CanvasX);
            float topY = m_TableTopY;

            char tb[16]; snprintf(tb, sizeof(tb), "%.2f", m_PreviewTime);
            float tw = ImGui::CalcTextSize(tb).x + 20.0f;
            const float pillH = 32.0f;

            dl->PushClipRect(ImVec2(m_CanvasX, m_TableTopY - pillH - 8.0f),
                             ImVec2(m_CanvasX + m_CanvasWidth, m_TableTopY + kRulerHeight + 2.0f), true);

            dl->AddLine(ImVec2(px - 2.0f, topY), ImVec2(px - 2.0f, topY + kRulerHeight + 2.0f),
                        IM_COL32(255, 50, 50, 30), 2.0f);
            dl->AddLine(ImVec2(px + 2.0f, topY), ImVec2(px + 2.0f, topY + kRulerHeight + 2.0f),
                        IM_COL32(255, 50, 50, 30), 2.0f);
            dl->AddLine(ImVec2(px, topY), ImVec2(px, topY + kRulerHeight + 2.0f),
                        IM_COL32(255, 55, 55, 255), 1.0f);

            float pillX = px - tw * 0.5f;
            float pillTop = topY - pillH;
            float pillBottom = topY - 2.0f;
            ImVec2 hMin(pillX, pillTop);
            ImVec2 hMax(pillX + tw, pillBottom);
            dl->AddRectFilled(hMin, hMax, IM_COL32(200, 40, 40, 240), 3.0f);
            dl->AddRect(hMin, hMax, IM_COL32(255, 70, 70, 255), 3.0f);
            float textH = ImGui::CalcTextSize(tb).y;
            float textY = pillTop + (pillH - 2.0f - textH) * 0.5f;
            dl->AddText(ImVec2(hMin.x + 10.0f, textY),
                        IM_COL32(255, 255, 255, 255), tb);

            dl->PopClipRect();
        }

        // ---- Playhead drag handle ----
        if (m_CanvasX > 0.0f) {
            float px = TimeToScreenX(m_PreviewTime, m_CanvasX);
            float topY = m_TableTopY;
            char tb[16]; snprintf(tb, sizeof(tb), "%.2f", m_PreviewTime);
            float tw = ImGui::CalcTextSize(tb).x + 20.0f;
            const float pillH = 32.0f;
            float btnX = px - tw * 0.5f;
            float btnW = tw;
            float canvasRight = m_CanvasX + m_CanvasWidth;
            if (btnX < m_CanvasX) { btnW -= (m_CanvasX - btnX); btnX = m_CanvasX; }
            if (btnX + btnW > canvasRight) btnW = canvasRight - btnX;
            if (btnW > 4.0f) {
                ImGui::SetCursorScreenPos(ImVec2(btnX, topY - pillH));
                ImGui::InvisibleButton("##PlayheadDrag", ImVec2(btnW, pillH + kRulerHeight));
                if (ImGui::IsItemActivated()) StartPreview();
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    float raw = ScreenXToTime(ImGui::GetMousePos().x, m_CanvasX);
                    if (raw < 0.0f) raw = 0.0f;
                    m_PreviewTime = SnapToNearestKeyframe(raw);
                }
            }
        }

        HandleCanvasInput(preTableWheel, preTableWheelH);

        // ---- Playback tick ----
        if (m_IsPlaying) m_PreviewTime += ImGui::GetIO().DeltaTime;
    }

} // namespace Ayaya
