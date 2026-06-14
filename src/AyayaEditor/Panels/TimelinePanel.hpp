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
        glm::vec3 Translation{0}, Rotation{0}, Scale{1,1,1};
        bool HasTransform = false;

        glm::vec4 SpriteColor{1,1,1,1};
        bool HasSprite = false;

        float CameraFOV = 0.0f;
        float CameraOrthoSize = 5.0f;
        bool HasCamera = false;

        float PointLightIntensity = 1500.0f;
        float PointLightRadius = 10.0f;
        bool HasPointLight = false;

        float DirLightIntensity = 100000.0f;
        bool HasDirLight = false;

        float UIOpacity = 1.0f;
        bool HasUIImage = false;
        bool HasUIText = false;
    };

    class TimelinePanel {
    public:
        void OnImGuiRender();
        void RenderContent();  // core content for drawer reuse
        void SetContext(std::shared_ptr<Scene> scene);

        bool  IsPreviewing() const { return m_IsPreviewing; }
        float GetPreviewTime() const { return m_PreviewTime; }

    private:
        void DrawTrackList();
        void DrawTimelineCanvas();
        void HandleCanvasInput(ImVec2 canvasPos, ImVec2 canvasSize);

        void StartPreview();
        void StopPreview();

        float TimeToScreenX(float time, float canvasX) const;
        float ScreenXToTime(float x, float canvasX) const;

        std::shared_ptr<Scene> m_Scene;
        bool m_IsOpen = true;

        // View
        float m_Zoom    = 100.0f;
        float m_ScrollX = 0.0f;

        // Playback
        float m_PreviewTime = 0.0f;
        bool  m_IsPlaying   = false;
        bool  m_IsPreviewing = false;

        // Snapshots
        std::unordered_map<UUID, EntitySnapshot> m_Snapshots;

        // Pre-drag undo capture
        AnimationControllerComponent m_PreDragComponent;
        Entity m_DragEntity;

        // Track expansion
        std::unordered_set<UUID> m_ExpandedEntities;

        // Layout
        static constexpr float kTrackListWidth = 220.0f;
        static constexpr float kRulerHeight    = 26.0f;
        static constexpr float kTrackHeight    = 28.0f;
        static constexpr float kMinClipWidth   = 4.0f;
    };

} // namespace Ayaya
