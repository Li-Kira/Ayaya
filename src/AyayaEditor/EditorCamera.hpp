#pragma once
#include "Engine/Core/Timestep.hpp"
#include <glm/glm.hpp>

namespace Ayaya {

    class EditorCamera {
    public:
        EditorCamera();

        // 核心更新逻辑：处理键鼠输入
        void OnUpdate(Timestep ts, bool viewportFocused);
        // 处理视口缩放
        void OnResize(float width, float height);

        const glm::mat4& GetProjection() const { return m_Projection; }
        const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
        glm::mat4 GetViewProjection() const { return m_Projection * m_ViewMatrix; }
        const glm::vec3& GetPosition() const { return m_Position; }

    private:
        void RecalculateView();

    private:
        float m_FOV = 45.0f, m_AspectRatio = 1.778f, m_NearClip = 0.1f, m_FarClip = 1000.0f;
        glm::mat4 m_Projection{1.0f};
        glm::mat4 m_ViewMatrix{1.0f};

        glm::vec3 m_Position;
        float m_Pitch, m_Yaw; // 俯仰角和偏航角

        glm::vec2 m_InitialMousePosition{ 0.0f, 0.0f };
    };

}