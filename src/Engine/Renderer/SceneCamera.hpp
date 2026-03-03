#pragma once
#include "Camera.hpp"

namespace Ayaya {
    class SceneCamera : public Camera {
    public:
        enum class ProjectionType { Perspective = 0, Orthographic = 1 };
    public:
        SceneCamera();
        virtual ~SceneCamera() = default;

        void SetViewportSize(uint32_t width, uint32_t height);
        
        // 设置参数后会自动重新计算矩阵
        void SetPerspective(float fov, float nearClip, float farClip);
        void SetOrthographic(float size, float nearClip, float farClip);

        void SetProjectionType(ProjectionType type) { m_ProjectionType = type; RecalculateProjection(); }
        ProjectionType GetProjectionType() const { return m_ProjectionType; }

    private:
        void RecalculateProjection();

    private:
        ProjectionType m_ProjectionType = ProjectionType::Orthographic;
        float m_PerspectiveFOV = glm::radians(45.0f);
        float m_PerspectiveNear = 0.01f, m_PerspectiveFar = 1000.0f;
        float m_OrthographicSize = 10.0f;
        float m_OrthographicNear = -1.0f, m_OrthographicFar = 1.0f;

        float m_AspectRatio = 1.0f;
    };
}