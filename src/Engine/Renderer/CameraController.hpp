#pragma once
#include "SceneCamera.hpp"
#include "Core/Timestep.hpp"
#include <glm/glm.hpp>

namespace Ayaya {
    class CameraController {
    public:
        CameraController(float aspectRatio, bool isPerspective = false);

        void OnUpdate(Timestep ts);
        void OnResize(float width, float height);

        SceneCamera& GetCamera() { return m_Camera; }
        
        // 核心：View-Projection 矩阵
        glm::mat4 GetViewProjection() const;
        
        // 控制相机状态的开关
        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const { return m_Active; }
        
    private:
        SceneCamera m_Camera;
        float m_AspectRatio;

        // 相机的世界变换属性
        glm::vec3 m_Position = { 0.0f, 0.0f, 5.0f };
        glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f }; // X(Pitch/俯仰), Y(Yaw/偏航), Z(Roll/翻滚)

        float m_MoveSpeed = 5.0f;
        float m_MouseSensitivity = 0.2f; // 鼠标旋转灵敏度

        bool m_Active = true; 
        glm::vec2 m_InitialMousePosition = { 0.0f, 0.0f }; // 记录上一帧的鼠标位置
    };
}