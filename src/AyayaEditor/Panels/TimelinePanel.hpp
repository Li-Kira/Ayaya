#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include <imgui.h>
#include "Engine/Scene/Entity.hpp"
#include "Engine/Scene/Components.hpp"
#include "Core/UUID.hpp"

namespace Ayaya {

    class Scene;

    struct EntitySnapshot {
        glm::vec3 Translation{0}, Rotation{0}, Scale{1,1,1}; bool HasTransform = false;
        glm::vec4 SpriteColor{1,1,1,1}; bool HasSprite = false;
        float CameraFOV = 0.0f, CameraOrthoSize = 5.0f; bool HasCamera = false;
        float PointLightIntensity = 1500.0f, PointLightRadius = 10.0f; bool HasPointLight = false;
        float DirLightIntensity = 100000.0f; bool HasDirLight = false;
        float UIOpacity = 1.0f; bool HasUIImage = false, HasUIText = false;
    };

    class TimelinePanel {
    public:
        void OnImGuiRender();
        void RenderContent();
        void SetContext(std::shared_ptr<Scene> scene);

        bool  IsPreviewing() const { return m_IsPreviewing; }
        float GetPreviewTime() const { return m_PreviewTime; }

    private:
        void DrawTrackCell(ImVec2 cellMin, ImVec2 cellSize,
                           AnimationTrack& track, Entity entity, int trackIdx);
        void HandleCanvasInput(float wheel, float wheelH);

        void StartPreview();
        void StopPreview();

        float TimeToScreenX(float time, float canvasX) const;
        float ScreenXToTime(float x, float canvasX) const;
        float SnapToNearestKeyframe(float time) const;

        std::shared_ptr<Scene> m_Scene;
        bool m_IsOpen = false;

        float m_Zoom = 500.0f, m_ScrollX = 50.0f;
        float m_PreviewTime = 0.0f;
        bool  m_IsPlaying = false, m_IsPreviewing = false;

        // Transient per-frame canvas rect (set during table layout)
        float m_CanvasX     = 0.0f;
        float m_CanvasWidth = 0.0f;
        float m_TableTopY   = 0.0f;

        // Tracks user-adjusted outliner column width (pixels)
        float m_OutlinerWidth = kTrackListWidth;

        std::unordered_map<UUID, EntitySnapshot> m_Snapshots;
        AnimationControllerComponent m_PreDragComponent;
        Entity m_DragEntity;
        std::unordered_set<UUID> m_ExpandedEntities;

        static constexpr float kTrackListWidth = 440.0f;
        static constexpr float kRulerHeight    = 32.0f;
        static constexpr float kTrackHeight    = 28.0f;
        static constexpr float kMinClipWidth   = 4.0f;
        static constexpr float kSnapThresholdPx = 8.0f;
    };

} // namespace Ayaya
