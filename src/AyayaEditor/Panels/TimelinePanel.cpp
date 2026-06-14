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

    // ==========================================
    // Coordinate helpers
    // ==========================================
    float TimelinePanel::TimeToScreenX(float time, float canvasX) const {
        return canvasX + time * m_Zoom + m_ScrollX;
    }

    float TimelinePanel::ScreenXToTime(float x, float canvasX) const {
        return (x - canvasX - m_ScrollX) / m_Zoom;
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
                snap.Translation = t.Translation;
                snap.Rotation = t.Rotation;
                snap.Scale = t.Scale;
                snap.HasTransform = true;
            }
            if (entity.HasComponent<SpriteRendererComponent>()) {
                snap.SpriteColor = entity.GetComponent<SpriteRendererComponent>().Color;
                snap.HasSprite = true;
            }
            if (entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>().Camera;
                snap.CameraFOV = cam.GetPerspectiveFOV();
                snap.CameraOrthoSize = cam.GetOrthographicSize();
                snap.HasCamera = true;
            }
            if (entity.HasComponent<PointLightComponent>()) {
                auto& pl = entity.GetComponent<PointLightComponent>();
                snap.PointLightIntensity = pl.LuminousPower;
                snap.PointLightRadius = pl.Radius;
                snap.HasPointLight = true;
            }
            if (entity.HasComponent<DirectionalLightComponent>()) {
                snap.DirLightIntensity = entity.GetComponent<DirectionalLightComponent>().Illuminance;
                snap.HasDirLight = true;
            }
            if (entity.HasComponent<UIImageComponent>()) {
                snap.UIOpacity = entity.GetComponent<UIImageComponent>().Color.a;
                snap.HasUIImage = true;
            }
            if (entity.HasComponent<UITextComponent>()) {
                snap.UIOpacity = entity.GetComponent<UITextComponent>().Color.a;
                snap.HasUIText = true;
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
                t.Translation = snap.Translation;
                t.Rotation = snap.Rotation;
                t.Scale = snap.Scale;
            }
            if (snap.HasSprite && entity.HasComponent<SpriteRendererComponent>())
                entity.GetComponent<SpriteRendererComponent>().Color = snap.SpriteColor;
            if (snap.HasCamera && entity.HasComponent<CameraComponent>()) {
                auto& cam = entity.GetComponent<CameraComponent>().Camera;
                cam.SetPerspectiveFOV(snap.CameraFOV);
                cam.SetOrthographicSize(snap.CameraOrthoSize);
            }
            if (snap.HasPointLight && entity.HasComponent<PointLightComponent>()) {
                auto& pl = entity.GetComponent<PointLightComponent>();
                pl.LuminousPower = snap.PointLightIntensity;
                pl.Radius = snap.PointLightRadius;
            }
            if (snap.HasDirLight && entity.HasComponent<DirectionalLightComponent>())
                entity.GetComponent<DirectionalLightComponent>().Illuminance = snap.DirLightIntensity;
            if (snap.HasUIImage && entity.HasComponent<UIImageComponent>())
                entity.GetComponent<UIImageComponent>().Color.a = snap.UIOpacity;
            if (snap.HasUIText && entity.HasComponent<UITextComponent>())
                entity.GetComponent<UITextComponent>().Color.a = snap.UIOpacity;
        }

        m_Snapshots.clear();
        m_PreviewTime = 0.0f;
        m_IsPlaying = false;
        m_IsPreviewing = false;
    }

    // ==========================================
    // Track List (left pane)
    // ==========================================
    void TimelinePanel::DrawTrackList() {
        ImGui::BeginChild("TrackList", ImVec2(kTrackListWidth, 0), true);
        if (!m_Scene) { ImGui::TextDisabled("No scene"); ImGui::EndChild(); return; }

        auto view = m_Scene->Reg().view<AnimationControllerComponent>();
        if (view.empty()) {
            ImGui::TextDisabled("No animated entities");
            ImGui::EndChild();
            return;
        }

        for (auto e : view) {
            Entity entity{e, m_Scene.get()};
            auto& controller = entity.GetComponent<AnimationControllerComponent>();
            UUID uuid = entity.GetComponent<IDComponent>().ID;
            std::string name = entity.GetComponent<TagComponent>().Tag;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            bool expanded = m_ExpandedEntities.count(uuid) != 0;
            if (expanded) flags |= ImGuiTreeNodeFlags_DefaultOpen;

            ImGui::PushID((int)e);
            bool nodeOpen = ImGui::TreeNodeEx(name.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                if (expanded) m_ExpandedEntities.erase(uuid);
                else          m_ExpandedEntities.insert(uuid);
            }

            if (nodeOpen) {
                for (int i = 0; i < (int)controller.Tracks.size(); i++) {
                    auto& track = controller.Tracks[i];
                    ImGui::PushID(i);
                    ImGui::BulletText("%s", GetTargetPropertyName(track.Property));
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // ==========================================
    // Timeline Canvas (right pane)
    // ==========================================
    void TimelinePanel::DrawTimelineCanvas() {
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;
        float canvasX = canvasPos.x + kTrackListWidth;
        float canvasW = canvasSize.x - kTrackListWidth;

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(22, 22, 30, 255));

        // ---- Ruler ----
        ImVec2 rulerMin(canvasX, canvasPos.y);
        ImVec2 rulerMax(canvasX + canvasW, canvasPos.y + kRulerHeight);
        dl->AddRectFilled(rulerMin, rulerMax, IM_COL32(30, 30, 40, 255));

        // Ruler ticks
        float timePerTick = 1.0f;
        if (m_Zoom < 30.0f) timePerTick = 5.0f;
        else if (m_Zoom < 60.0f) timePerTick = 2.0f;
        else if (m_Zoom > 300.0f) timePerTick = 0.2f;
        else if (m_Zoom > 150.0f) timePerTick = 0.5f;

        float firstTick = std::floor(ScreenXToTime(canvasX, canvasX) / timePerTick) * timePerTick;
        for (float t = firstTick; t < ScreenXToTime(canvasX + canvasW + 100, canvasX); t += timePerTick) {
            float sx = TimeToScreenX(t, canvasX);
            dl->AddLine(ImVec2(sx, rulerMin.y + kRulerHeight * 0.5f),
                        ImVec2(sx, rulerMax.y), IM_COL32(80, 80, 95, 255));
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1fs", t);
            dl->AddText(ImVec2(sx + 3.0f, rulerMin.y + 2.0f), IM_COL32(180, 180, 190, 255), buf);
        }

        // ---- Track rows ----
        float rowY = rulerMax.y;
        if (!m_Scene) return;

        auto view = m_Scene->Reg().view<AnimationControllerComponent>();
        for (auto e : view) {
            Entity entity{e, m_Scene.get()};
            auto& controller = entity.GetComponent<AnimationControllerComponent>();
            UUID uuid = entity.GetComponent<IDComponent>().ID;
            bool expanded = m_ExpandedEntities.count(uuid) != 0;

            // Entity header row
            ImVec2 hdrMin(canvasX, rowY);
            ImVec2 hdrMax(canvasX + canvasW, rowY + kTrackHeight);
            dl->AddRectFilled(hdrMin, hdrMax,
                expanded ? IM_COL32(40, 45, 55, 255) : IM_COL32(32, 35, 42, 255));
            dl->AddText(ImVec2(hdrMin.x + 6.0f, hdrMin.y + 5.0f),
                        IM_COL32(200, 200, 210, 255),
                        entity.GetComponent<TagComponent>().Tag.c_str());
            rowY += kTrackHeight;

            // Track rows (if expanded)
            if (expanded) {
                for (int i = 0; i < (int)controller.Tracks.size(); i++) {
                    auto& track = controller.Tracks[i];

                    float clipStartX = TimeToScreenX(track.TimeOffset, canvasX);
                    float duration = 1.0f;
                    if (track.CurveHandle != 0) {
                        auto curve = AssetManager::GetAsset<CurveAsset>(track.CurveHandle);
                        if (curve && !curve->Keys.empty())
                            duration = curve->Keys.back().Time - curve->Keys.front().Time;
                    }
                    float clipEndX = clipStartX + duration * m_Zoom;
                    if (clipEndX - clipStartX < kMinClipWidth)
                        clipEndX = clipStartX + kMinClipWidth;

                    // Clip rect
                    ImVec2 clipMin(clipStartX, rowY + 2.0f);
                    ImVec2 clipMax(clipEndX, rowY + kTrackHeight - 2.0f);
                    dl->AddRectFilled(clipMin, clipMax, IM_COL32(34, 150, 80, 200), 4.0f);
                    dl->AddRect(clipMin, clipMax, IM_COL32(50, 180, 100, 255), 4.0f);
                    dl->AddText(ImVec2(clipMin.x + 4.0f, clipMin.y + 2.0f),
                                IM_COL32(255, 255, 255, 220), GetTargetPropertyName(track.Property));

                    // InvisibleButton for clip drag
                    ImGui::PushID((int)e);
                    ImGui::PushID(i);
                    ImGui::SetCursorScreenPos(clipMin);
                    ImGui::InvisibleButton("##ClipDrag", ImVec2(clipMax.x - clipMin.x, clipMax.y - clipMin.y));

                    if (ImGui::IsItemActivated()) {
                        m_PreDragComponent = entity.GetComponent<AnimationControllerComponent>();
                        m_DragEntity = entity;
                    }
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                        float newOffset = track.TimeOffset + ImGui::GetIO().MouseDelta.x / m_Zoom;
                        // Don't let offset go below 0
                        if (newOffset < 0.0f) newOffset = 0.0f;
                        track.TimeOffset = newOffset;
                        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        float oldOffset = m_PreDragComponent.Tracks[i].TimeOffset;
                        float newOffset = track.TimeOffset;
                        if (std::abs(newOffset - oldOffset) > 0.001f && m_DragEntity) {
                            auto newComp = entity.GetComponent<AnimationControllerComponent>();
                            EditorLayer::Get().GetCommandHistory().AddCommand(
                                std::make_shared<ChangeComponentCommand<AnimationControllerComponent>>(
                                    m_DragEntity, m_PreDragComponent, newComp));
                        }
                        m_DragEntity = Entity{};
                    }

                    ImGui::PopID();
                    ImGui::PopID();
                    rowY += kTrackHeight;
                }
            }
        }

        // ---- Playhead ----
        float px = TimeToScreenX(m_PreviewTime, canvasX);
        dl->AddLine(ImVec2(px, rulerMin.y), ImVec2(px, rowY), IM_COL32(239, 68, 68, 220), 2.0f);
        // Triangle handle
        dl->AddTriangleFilled(ImVec2(px, rulerMin.y), ImVec2(px - 6.0f, rulerMin.y - 10.0f),
                              ImVec2(px + 6.0f, rulerMin.y - 10.0f), IM_COL32(239, 68, 68, 240));

        // InvisibleButton for playhead drag
        ImGui::SetCursorScreenPos(ImVec2(px - 8.0f, rulerMin.y - 10.0f));
        ImGui::InvisibleButton("##PlayheadDrag", ImVec2(16.0f, kRulerHeight + 10.0f));
        if (ImGui::IsItemActivated()) StartPreview();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            m_PreviewTime = ScreenXToTime(ImGui::GetMousePos().x, canvasX);
            if (m_PreviewTime < 0.0f) m_PreviewTime = 0.0f;
        }

        // Canvas-level InvisibleButton for scroll-to-scrub
        ImGui::SetCursorScreenPos(ImVec2(canvasX, rulerMin.y + kRulerHeight));
        ImVec2 canvasRemaining(canvasW, canvasSize.y - kRulerHeight);
        ImGui::InvisibleButton("##CanvasBg", canvasRemaining);
        // Scroll-to-click: jump playhead to clicked position
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemActive()) {
            float t = ScreenXToTime(ImGui::GetMousePos().x, canvasX);
            if (t >= 0.0f) { m_PreviewTime = t; StartPreview(); }
        }

        HandleCanvasInput(canvasPos, canvasSize);
    }

    // ==========================================
    // Canvas input (scroll, zoom)
    // ==========================================
    void TimelinePanel::HandleCanvasInput(ImVec2 canvasPos, ImVec2 canvasSize) {
        float canvasX = canvasPos.x + kTrackListWidth;
        float canvasW = canvasSize.x - kTrackListWidth;
        ImVec2 mousePos = ImGui::GetMousePos();
        bool hovered = ImGui::IsItemHovered() ||
            (mousePos.x >= canvasX && mousePos.x <= canvasX + canvasW &&
             mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + canvasSize.y);

        if (hovered) {
            // Scroll = zoom
            if (ImGui::GetIO().MouseWheel != 0.0f) {
                float oldTime = ScreenXToTime(mousePos.x, canvasX);
                m_Zoom *= 1.0f + ImGui::GetIO().MouseWheel * 0.1f;
                m_Zoom = std::clamp(m_Zoom, 20.0f, 1000.0f);
                float newScrollX = mousePos.x - canvasX - oldTime * m_Zoom;
                m_ScrollX = newScrollX;
            }

            // Ctrl+scroll = horizontal pan
            if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
                m_ScrollX += ImGui::GetIO().MouseWheel * 50.0f;
            }
        }
    }

    // ==========================================
    // Main Render
    // ==========================================
    void TimelinePanel::OnImGuiRender() {
        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Timeline", &m_IsOpen)) { ImGui::End(); return; }
        RenderContent();
        ImGui::End();

        if (!m_IsOpen && m_IsPreviewing) StopPreview();
    }

    void TimelinePanel::RenderContent() {
        if (!m_Scene) {
            ImGui::TextDisabled("No scene loaded");
            return;
        }

        // ---- Bottom controls ----
        if (ImGui::Button(ICON_FA_PLAY))  { StartPreview(); m_IsPlaying = true; }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP))  { StopPreview(); }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_UNDO " Reset")) { m_ScrollX = 0.0f; m_Zoom = 100.0f; }
        ImGui::SameLine();
        ImGui::Text("  t=%.3f  zoom=%.0fpx/s", m_PreviewTime, m_Zoom);

        ImGui::Separator();

        // ---- Splitter layout ----
        DrawTrackList();
        ImGui::SameLine(0, 0);
        DrawTimelineCanvas();

        // Play timer
        if (m_IsPlaying)
            m_PreviewTime += ImGui::GetIO().DeltaTime;
    }

} // namespace Ayaya
