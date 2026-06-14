#include "CurveEditorPanel.hpp"
#include "Engine/Animation/CurveSerializer.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Log.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome5.h>
#include <algorithm>
#include <cmath>

namespace Ayaya {

    // ==========================================
    // Public
    // ==========================================

    void CurveEditorPanel::OpenCurve(std::shared_ptr<CurveAsset> curve, const std::filesystem::path& filepath) {
        m_ActiveCurve   = curve;
        m_CurveFilePath = filepath;
        m_IsOpen        = true;
        m_SelectedKeyIndex = -1;
        m_DraggingTangent  = 0;

        // Auto-fit view to curve bounds
        if (curve && !curve->Keys.empty()) {
            float minT = curve->Keys[0].Time;
            float maxT = curve->Keys.back().Time;
            float minV = curve->Keys[0].Value;
            float maxV = minV;
            for (auto& k : curve->Keys) {
                if (k.Value < minV) minV = k.Value;
                if (k.Value > maxV) maxV = k.Value;
            }
            float padT = std::max(0.1f, (maxT - minT) * 0.15f);
            float padV = std::max(0.1f, (maxV - minV) * 0.15f);
            m_ViewMinTime  = minT - padT;
            m_ViewMaxTime  = maxT + padT;
            m_ViewMinValue = minV - padV;
            m_ViewMaxValue = maxV + padV;
        }
    }

    void CurveEditorPanel::CloseCurve() {
        m_ActiveCurve.reset();
        m_CurveFilePath.clear();
        m_SelectedKeyIndex = -1;
        m_DraggingTangent  = 0;
    }

    // ==========================================
    // Coordinate Mapping
    // ==========================================

    ImVec2 CurveEditorPanel::TimeValueToScreen(float time, float value,
                                                ImVec2 canvasPos, ImVec2 canvasSize) const {
        float x = canvasPos.x + ((time - m_ViewMinTime) / (m_ViewMaxTime - m_ViewMinTime)) * canvasSize.x;
        float y = canvasPos.y + (1.0f - ((value - m_ViewMinValue) / (m_ViewMaxValue - m_ViewMinValue))) * canvasSize.y;
        return ImVec2(x, y);
    }

    ImVec2 CurveEditorPanel::ScreenToTimeValue(ImVec2 screenPos,
                                                ImVec2 canvasPos, ImVec2 canvasSize) const {
        float time = m_ViewMinTime + ((screenPos.x - canvasPos.x) / canvasSize.x) * (m_ViewMaxTime - m_ViewMinTime);
        float value = m_ViewMinValue + (1.0f - ((screenPos.y - canvasPos.y) / canvasSize.y)) * (m_ViewMaxValue - m_ViewMinValue);
        return ImVec2(time, value);
    }

    // ==========================================
    // Drawing
    // ==========================================

    void CurveEditorPanel::DrawGrid(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize) {
        ImU32 gridCol = IM_COL32(55, 55, 65, 255);
        ImU32 axisCol = IM_COL32(80, 80, 95, 255);

        // Time grid: aim for ~8-12 vertical lines
        float timeRange = m_ViewMaxTime - m_ViewMinTime;
        float timeStep  = std::pow(10.0f, std::floor(std::log10(timeRange))) * 0.5f;
        if (timeRange / timeStep < 4)  timeStep *= 0.5f;
        if (timeRange / timeStep > 12) timeStep *= 2.0f;

        for (float t = std::floor(m_ViewMinTime / timeStep) * timeStep; t <= m_ViewMaxTime; t += timeStep) {
            ImVec2 p = TimeValueToScreen(t, 0, canvasPos, canvasSize);
            ImU32 col = (std::abs(t) < timeStep * 0.01f) ? axisCol : gridCol;
            dl->AddLine(ImVec2(p.x, canvasPos.y), ImVec2(p.x, canvasPos.y + canvasSize.y), col, 1.0f);
        }

        // Value grid: aim for ~6-10 horizontal lines
        float valRange = m_ViewMaxValue - m_ViewMinValue;
        float valStep  = std::pow(10.0f, std::floor(std::log10(valRange))) * 0.5f;
        if (valRange / valStep < 3)  valStep *= 0.5f;
        if (valRange / valStep > 10) valStep *= 2.0f;

        for (float v = std::floor(m_ViewMinValue / valStep) * valStep; v <= m_ViewMaxValue; v += valStep) {
            ImVec2 p = TimeValueToScreen(0, v, canvasPos, canvasSize);
            ImU32 col = (std::abs(v) < valStep * 0.01f) ? axisCol : gridCol;
            dl->AddLine(ImVec2(canvasPos.x, p.y), ImVec2(canvasPos.x + canvasSize.x, p.y), col, 1.0f);
        }
    }

    void CurveEditorPanel::DrawCurve(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize) {
        if (!m_ActiveCurve || m_ActiveCurve->Keys.size() < 1) return;

        float graphWidthPx = canvasSize.x;
        int samples = std::max(20, static_cast<int>(graphWidthPx / 3.0f));
        float timeStep = (m_ViewMaxTime - m_ViewMinTime) / samples;

        ImVec2 lastPoint = TimeValueToScreen(m_ViewMinTime,
            m_ActiveCurve->Evaluate(m_ViewMinTime), canvasPos, canvasSize);

        for (int i = 1; i <= samples; ++i) {
            float t = m_ViewMinTime + i * timeStep;
            float val = m_ActiveCurve->Evaluate(t);
            ImVec2 pt = TimeValueToScreen(t, val, canvasPos, canvasSize);
            dl->AddLine(lastPoint, pt, IM_COL32(34, 197, 94, 255), 2.0f);
            lastPoint = pt;
        }
    }

    void CurveEditorPanel::DrawKeyframes(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize) {
        if (!m_ActiveCurve) return;

        float handleTimeLen = (m_ViewMaxTime - m_ViewMinTime) * kHandleTimeFrac;

        for (int i = 0; i < (int)m_ActiveCurve->Keys.size(); i++) {
            auto& key = m_ActiveCurve->Keys[i];
            ImVec2 keyPos = TimeValueToScreen(key.Time, key.Value, canvasPos, canvasSize);

            // Tangents (only for selected keyframe)
            if (i == m_SelectedKeyIndex) {
                // Out tangent (right handle)
                if (i < (int)m_ActiveCurve->Keys.size() - 1) {
                    float outHT = key.Time + handleTimeLen;
                    float outHV = key.Value + key.OutTangent * handleTimeLen;
                    ImVec2 outPos = TimeValueToScreen(outHT, outHV, canvasPos, canvasSize);
                    dl->AddLine(keyPos, outPos, IM_COL32(148, 163, 184, 255), 1.5f);
                    dl->AddCircleFilled(outPos, 4.0f,
                        (m_DraggingTangent == 2) ? IM_COL32(125, 211, 252, 255) : IM_COL32(56, 189, 248, 255));
                }

                // In tangent (left handle)
                if (i > 0) {
                    float inHT = key.Time - handleTimeLen;
                    float inHV = key.Value - key.InTangent * handleTimeLen;
                    ImVec2 inPos = TimeValueToScreen(inHT, inHV, canvasPos, canvasSize);
                    dl->AddLine(keyPos, inPos, IM_COL32(148, 163, 184, 255), 1.5f);
                    dl->AddCircleFilled(inPos, 4.0f,
                        (m_DraggingTangent == 1) ? IM_COL32(125, 211, 252, 255) : IM_COL32(56, 189, 248, 255));
                }
            }

            // Keyframe dot
            ImU32 keyColor = (i == m_SelectedKeyIndex) ? IM_COL32(250, 204, 21, 255)   // gold
                         : (i == m_HoveredKeyIndex)     ? IM_COL32(200, 200, 200, 255) // hover
                         :                                IM_COL32(255, 255, 255, 255); // normal
            dl->AddCircleFilled(keyPos, 5.0f, keyColor);
            dl->AddCircle(keyPos, 5.0f, IM_COL32(0, 0, 0, 100), 0, 1.5f);

            // Time label
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", key.Time);
            dl->AddText(ImVec2(keyPos.x - 10.0f, keyPos.y + 8.0f),
                        IM_COL32(150, 150, 160, 255), buf);
        }
    }

    void CurveEditorPanel::DrawPlayhead(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize) {
        ImVec2 top = TimeValueToScreen(m_PreviewTime, m_ViewMaxValue, canvasPos, canvasSize);
        ImVec2 bot = TimeValueToScreen(m_PreviewTime, m_ViewMinValue, canvasPos, canvasSize);
        dl->AddLine(top, bot, IM_COL32(239, 68, 68, 200), 1.5f);

        // Triangle at top
        ImVec2 tri[3] = {
            ImVec2(top.x, top.y),
            ImVec2(top.x - 5.0f, top.y + 8.0f),
            ImVec2(top.x + 5.0f, top.y + 8.0f),
        };
        dl->AddTriangleFilled(tri[0], tri[1], tri[2], IM_COL32(239, 68, 68, 220));
    }

    void CurveEditorPanel::DrawSidebar(float height) {
        ImGui::BeginChild("CurveSidebar", ImVec2(kSidebarWidth, height), true);

        if (!m_ActiveCurve) {
            ImGui::TextDisabled("No curve open");
            ImGui::EndChild();
            return;
        }

        ImGui::Text("Keyframes (%zu)", m_ActiveCurve->Keys.size());
        ImGui::Separator();

        // Keyframe list
        for (int i = 0; i < (int)m_ActiveCurve->Keys.size(); i++) {
            auto& key = m_ActiveCurve->Keys[i];
            ImGui::PushID(i);

            bool isSel = (i == m_SelectedKeyIndex);
            if (isSel) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.40f, 0.75f, 0.70f));
            }

            char label[64];
            snprintf(label, sizeof(label), "%d: t=%.2f v=%.2f", i, key.Time, key.Value);
            if (ImGui::Selectable(label, isSel)) {
                m_SelectedKeyIndex = i;
            }

            if (isSel) ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Actions
        if (ImGui::Button(ICON_FA_PLUS " Add Key", ImVec2(-1, 0))) {
            float midT = (m_ViewMinTime + m_ViewMaxTime) * 0.5f;
            float midV = (m_ViewMinValue + m_ViewMaxValue) * 0.5f;
            m_SelectedKeyIndex = m_ActiveCurve->AddKey(midT, midV);
        }

        if (m_SelectedKeyIndex >= 0 && m_SelectedKeyIndex < (int)m_ActiveCurve->Keys.size()) {
            if (ImGui::Button("Remove Key", ImVec2(-1, 0))) {
                m_ActiveCurve->RemoveKey(m_SelectedKeyIndex);
                if (m_SelectedKeyIndex >= (int)m_ActiveCurve->Keys.size())
                    m_SelectedKeyIndex = (int)m_ActiveCurve->Keys.size() - 1;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Preview
        ImGui::Text("Preview");
        ImGui::SliderFloat("Time", &m_PreviewTime, m_ViewMinTime, m_ViewMaxTime, "%.3f");
        if (m_ActiveCurve->Keys.size() > 0) {
            float val = m_ActiveCurve->Evaluate(m_PreviewTime);
            ImGui::Text("Value: %.3f", val);
        }

        ImGui::Spacing();

        // Save
        if (ImGui::Button(ICON_FA_SAVE " Save", ImVec2(-1, 0))) {
            if (!m_CurveFilePath.empty() && m_ActiveCurve) {
                if (CurveSerializer::Serialize(*m_ActiveCurve, m_CurveFilePath))
                    AYAYA_CORE_INFO("Curve saved: {0}", m_CurveFilePath.string());
                else
                    AYAYA_CORE_ERROR("Failed to save curve: {0}", m_CurveFilePath.string());
            }
        }

        ImGui::EndChild();
    }

    // ==========================================
    // Interaction
    // ==========================================

    void CurveEditorPanel::HandleInput(ImVec2 canvasPos, ImVec2 canvasSize) {
        if (!m_ActiveCurve) return;

        ImVec2 mousePos = ImGui::GetMousePos();
        bool canvasHovered = ImGui::IsItemHovered();

        // --- Mouse wheel zoom ---
        if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
            float zoom = 1.0f - ImGui::GetIO().MouseWheel * 0.1f;
            float centerT = (m_ViewMinTime + m_ViewMaxTime) * 0.5f;
            float centerV = (m_ViewMinValue + m_ViewMaxValue) * 0.5f;
            float halfT = (m_ViewMaxTime - m_ViewMinTime) * 0.5f * zoom;
            float halfV = (m_ViewMaxValue - m_ViewMinValue) * 0.5f * zoom;
            m_ViewMinTime  = centerT - halfT;
            m_ViewMaxTime  = centerT + halfT;
            m_ViewMinValue = centerV - halfV;
            m_ViewMaxValue = centerV + halfV;
        }

        // --- Middle-button pan ---
        static bool s_Panning = false;
        static ImVec2 s_PanStart;
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            s_Panning = true;
            s_PanStart = mousePos;
        }
        if (s_Panning && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
            float dt = -(delta.x / canvasSize.x) * (m_ViewMaxTime - m_ViewMinTime);
            float dv =  (delta.y / canvasSize.y) * (m_ViewMaxValue - m_ViewMinValue);
            m_ViewMinTime  += dt;  m_ViewMaxTime  += dt;
            m_ViewMinValue += dv;  m_ViewMaxValue += dv;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) s_Panning = false;

        // --- Right-click context menu ---
        static float s_CtxTime = 0.0f, s_CtxValue = 0.0f;
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_IsDraggingKey && !m_DraggingTangent) {
            ImVec2 real = ScreenToTimeValue(mousePos, canvasPos, canvasSize);
            m_IsDraggingKey   = false;
            m_DraggingTangent = 0;
            s_CtxTime = real.x; s_CtxValue = real.y;
            ImGui::OpenPopup("##CurveCtx");
        }

        if (ImGui::BeginPopup("##CurveCtx")) {
            if (ImGui::MenuItem("Add Key Here")) {
                m_SelectedKeyIndex = m_ActiveCurve->AddKey(s_CtxTime, s_CtxValue);
            }
            if (ImGui::MenuItem("Reset View")) {
                m_ViewMinTime = -0.1f; m_ViewMaxTime = 2.0f;
                m_ViewMinValue = -0.5f; m_ViewMaxValue = 1.5f;
            }
            ImGui::EndPopup();
        }

        // --- Keyframe hit test (hover) ---
        m_HoveredKeyIndex = -1;
        if (canvasHovered && !m_IsDraggingKey && !m_DraggingTangent) {
            float bestDist = kKeyframeHitRadius;
            for (int i = 0; i < (int)m_ActiveCurve->Keys.size(); i++) {
                auto& key = m_ActiveCurve->Keys[i];
                ImVec2 kp = TimeValueToScreen(key.Time, key.Value, canvasPos, canvasSize);
                float dx = mousePos.x - kp.x, dy = mousePos.y - kp.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < bestDist) {
                    bestDist = dist;
                    m_HoveredKeyIndex = i;
                }
            }
        }

        // --- Keyframe click & drag ---
        if (m_HoveredKeyIndex >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_DraggingTangent) {
            m_SelectedKeyIndex = m_HoveredKeyIndex;
            m_IsDraggingKey = true;
        }

        if (m_IsDraggingKey && m_SelectedKeyIndex >= 0) {
            auto& key = m_ActiveCurve->Keys[m_SelectedKeyIndex];
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 real = ScreenToTimeValue(mousePos, canvasPos, canvasSize);

                // Clamp time between neighbors
                float minT = (m_SelectedKeyIndex > 0)
                    ? m_ActiveCurve->Keys[m_SelectedKeyIndex - 1].Time + 0.001f : -FLT_MAX;
                float maxT = (m_SelectedKeyIndex < (int)m_ActiveCurve->Keys.size() - 1)
                    ? m_ActiveCurve->Keys[m_SelectedKeyIndex + 1].Time - 0.001f : FLT_MAX;

                key.Time  = std::clamp(real.x, minT, maxT);
                key.Value = real.y;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_IsDraggingKey   = false;
            m_DraggingTangent = 0;
        }

        // --- Tangent drag ---
        if (m_SelectedKeyIndex >= 0 && !m_IsDraggingKey && canvasHovered) {
            auto& key = m_ActiveCurve->Keys[m_SelectedKeyIndex];
            float handleTimeLen = (m_ViewMaxTime - m_ViewMinTime) * kHandleTimeFrac;

            // Out tangent
            if (m_SelectedKeyIndex < (int)m_ActiveCurve->Keys.size() - 1) {
                float outHT = key.Time + handleTimeLen;
                float outHV = key.Value + key.OutTangent * handleTimeLen;
                ImVec2 outPos = TimeValueToScreen(outHT, outHV, canvasPos, canvasSize);
                float dx = mousePos.x - outPos.x, dy = mousePos.y - outPos.y;
                if (std::sqrt(dx*dx+dy*dy) < kTangentHitRadius && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_DraggingTangent = 2;
                }
            }

            // In tangent
            if (m_SelectedKeyIndex > 0) {
                float inHT = key.Time - handleTimeLen;
                float inHV = key.Value - key.InTangent * handleTimeLen;
                ImVec2 inPos = TimeValueToScreen(inHT, inHV, canvasPos, canvasSize);
                float dx = mousePos.x - inPos.x, dy = mousePos.y - inPos.y;
                if (std::sqrt(dx*dx+dy*dy) < kTangentHitRadius && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_DraggingTangent = 1;
                }
            }
        }

        // Tangent drag execution
        if (m_DraggingTangent && m_SelectedKeyIndex >= 0) {
            auto& key = m_ActiveCurve->Keys[m_SelectedKeyIndex];
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 real = ScreenToTimeValue(mousePos, canvasPos, canvasSize);

                if (m_DraggingTangent == 2) { // Out
                    float dt = real.x - key.Time;
                    float dv = real.y - key.Value;
                    if (dt < 0.001f) dt = 0.001f;
                    key.OutTangent = dv / dt;
                } else { // In
                    float dt = key.Time - real.x;
                    float dv = key.Value - real.y;
                    if (dt < 0.001f) dt = 0.001f;
                    key.InTangent = dv / dt;
                }
            }
        }

        // --- Click empty space to deselect ---
        if (canvasHovered && m_HoveredKeyIndex < 0 && !m_DraggingTangent &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_SelectedKeyIndex = -1;
        }
    }

    // ==========================================
    // Main Render
    // ==========================================

    void CurveEditorPanel::OnImGuiRender() {
        if (!IsOpen()) return;

        ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
        ImGui::Begin("Curve Editor", &m_IsOpen, ImGuiWindowFlags_MenuBar);

        // ---- Menu bar ----
        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem(ICON_FA_SAVE " Save")) {
                if (!m_CurveFilePath.empty() && m_ActiveCurve)
                    CurveSerializer::Serialize(*m_ActiveCurve, m_CurveFilePath);
            }
            ImGui::EndMenuBar();
        }

        // File name
        std::string title = m_CurveFilePath.filename().string();
        ImGui::Text("Editing: %s", title.c_str());
        ImGui::Separator();

        // Graph canvas — reserve space for bottom bar
        float bottomBarH = ImGui::GetFrameHeightWithSpacing() * 2.5f;
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.y -= bottomBarH;
        if (canvasSize.x < 100.0f) canvasSize.x = 100.0f;
        if (canvasSize.y < 100.0f) canvasSize.y = 100.0f;

        // ---- Two-pane layout ----
        DrawSidebar(canvasSize.y);
        ImGui::SameLine();

        // Background
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(20, 20, 28, 255), 4.0f);

        // Clip to canvas
        dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        DrawGrid(dl, canvasPos, canvasSize);
        DrawCurve(dl, canvasPos, canvasSize);
        DrawKeyframes(dl, canvasPos, canvasSize);
        DrawPlayhead(dl, canvasPos, canvasSize);

        dl->PopClipRect();

        // Invisible button for input capture
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##CurveCanvas", canvasSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
        HandleInput(canvasPos, canvasSize);

        // Playback controls
        ImGui::Separator();
        if (ImGui::Button(ICON_FA_PLAY)) { m_PreviewTime = m_ViewMinTime; }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP)) { m_IsPlayingPreview = false; }
        ImGui::SameLine();
        ImGui::ProgressBar((m_PreviewTime - m_ViewMinTime) / std::max(0.001f, m_ViewMaxTime - m_ViewMinTime),
                           ImVec2(-1, 0), "");

        // Cursor info
        if (ImGui::IsItemHovered() || true) {
            ImGui::Text("t = %.3f  |  value = %.3f", m_PreviewTime,
                        m_ActiveCurve ? m_ActiveCurve->Evaluate(m_PreviewTime) : 0.0f);
        }

        // Play preview timer
        if (m_IsPlayingPreview) {
            m_PreviewTime += ImGui::GetIO().DeltaTime;
            if (m_PreviewTime > m_ViewMaxTime)
                m_PreviewTime = m_ViewMinTime;
        }

        ImGui::End();

        // Clean up when user clicks X to close the window
        if (!m_IsOpen)
            CloseCurve();
    }

} // namespace Ayaya
