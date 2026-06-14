#pragma once

#include <memory>
#include <filesystem>
#include <imgui.h>
#include "Engine/Animation/CurveAsset.hpp"

namespace Ayaya {

    class CurveEditorPanel {
    public:
        CurveEditorPanel() = default;

        void OnImGuiRender();

        // Open a .curve asset for editing
        void OpenCurve(std::shared_ptr<CurveAsset> curve, const std::filesystem::path& filepath);
        void CloseCurve();
        bool IsOpen() const { return m_ActiveCurve != nullptr && m_IsOpen; }

    private:
        // Coordinate mapping
        ImVec2 TimeValueToScreen(float time, float value, ImVec2 canvasPos, ImVec2 canvasSize) const;
        ImVec2 ScreenToTimeValue(ImVec2 screenPos, ImVec2 canvasPos, ImVec2 canvasSize) const;

        // Drawing
        void DrawGrid(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize);
        void DrawCurve(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize);
        void DrawKeyframes(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize);
        void DrawPlayhead(ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize);
        void DrawSidebar(float height);

        // Interaction
        void HandleInput(ImVec2 canvasPos, ImVec2 canvasSize);

        // State
        std::shared_ptr<CurveAsset> m_ActiveCurve;
        std::filesystem::path      m_CurveFilePath;

        // View
        float m_ViewMinTime  = -0.1f;
        float m_ViewMaxTime  = 2.0f;
        float m_ViewMinValue = -0.5f;
        float m_ViewMaxValue = 1.5f;

        // Selection & drag
        int  m_SelectedKeyIndex  = -1;
        int  m_HoveredKeyIndex   = -1;
        bool m_IsDraggingKey      = false;
        int  m_DraggingTangent    = 0; // 0=none, 1=in, 2=out

        // Preview
        float m_PreviewTime     = 0.0f;
        bool  m_IsPlayingPreview = false;

        bool m_IsOpen = true;

        // Layout
        static constexpr float kSidebarWidth = 220.0f;
        static constexpr float kHandleTimeFrac = 0.12f; // tangent handle length as fraction of view time range
        static constexpr float kKeyframeHitRadius = 8.0f;
        static constexpr float kTangentHitRadius  = 6.0f;
    };

} // namespace Ayaya
