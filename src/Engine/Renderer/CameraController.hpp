#pragma once
#include "SceneCamera.hpp"
#include "Core/Timestep.hpp"

namespace Ayaya {
    class CameraController {
    public:
        CameraController(float aspectRatio, bool isPerspective = false);

        void OnUpdate(Timestep ts);
        void OnResize(float width, float height);

        SceneCamera& GetCamera() { return m_Camera; }
        
        // 核心：View-Projection 矩阵
        glm::mat4 GetViewProjection() const;
        
    private:
        SceneCamera m_Camera;
        float m_AspectRatio;

        // 相机的世界变换属性
        glm::vec3 m_Position = { 0.0f, 0.0f, 5.0f };
        glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f }; // X, Y, Z 欧拉角

        float m_MoveSpeed = 5.0f;
        float m_RotationSpeed = 90.0f;
    };
}