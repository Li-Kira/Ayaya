#include "EditorCamera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/KeyCodes.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {

    EditorCamera::EditorCamera() {
        // m_Position 的初始值现在不重要了，因为它会被 RecalculateView 覆盖
        m_Pitch = glm::radians(-22.093f);
        m_Yaw = glm::radians(-33.919f);

        // 新增：初始化轨道相机的核心参数
        m_FocalPoint = { 0.0f, 0.0f, 0.0f }; // 默认看向世界原点
        m_Distance = 10.0f;                  // 默认距离 10 米

        OnResize(1280.0f, 720.0f);
        RecalculateView();
    }

    void EditorCamera::OnResize(float width, float height) {
        m_AspectRatio = width / height;
        RecalculateProjection();
    }

    void EditorCamera::RecalculateProjection() {
        if (m_Perspective) {
            m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio,
                                            m_NearClip, m_FarClip);
        } else {
            float halfH = m_OrthoSize * 0.5f;
            float halfW = halfH * m_AspectRatio;
            m_Projection = glm::ortho(-halfW, halfW, -halfH, halfH,
                                      m_NearClip, m_FarClip);
        }
    }

    void EditorCamera::OnUpdate(Timestep ts, bool viewportFocused) {
        if (!viewportFocused) return;

        if (Input::IsMouseButtonPressed(1)) {
            glm::vec2 mousePos = { Input::GetMouseX(), Input::GetMouseY() };
            glm::vec2 delta = (mousePos - m_InitialMousePosition) * 0.003f; 
            m_InitialMousePosition = mousePos;

            // 1. 更新旋转角 (转头)
            m_Pitch -= delta.y;
            m_Yaw -= delta.x;

            if (m_Pitch > glm::radians(89.0f)) m_Pitch = glm::radians(89.0f);
            if (m_Pitch < glm::radians(-89.0f)) m_Pitch = glm::radians(-89.0f);

            glm::quat orientation = glm::quat(glm::vec3(m_Pitch, m_Yaw, 0.0f));
            glm::vec3 forward = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 right   = glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::vec3 up      = glm::rotate(orientation, glm::vec3(0.0f, 1.0f, 0.0f));

            float velocity = (Input::IsKeyPressed(Key::LeftShift) ? MoveSpeed * 2.0f : MoveSpeed) * (float)ts;

            if (Input::IsKeyPressed(Key::W)) m_Position += forward * velocity;
            if (Input::IsKeyPressed(Key::S)) m_Position -= forward * velocity;
            if (Input::IsKeyPressed(Key::A)) m_Position -= right * velocity;
            if (Input::IsKeyPressed(Key::D)) m_Position += right * velocity;
            if (Input::IsKeyPressed(Key::E)) m_Position += up * velocity;
            if (Input::IsKeyPressed(Key::Q)) m_Position -= up * velocity;

            m_FocalPoint = m_Position + forward * m_Distance;

            RecalculateView();
        } else {
            m_InitialMousePosition = { Input::GetMouseX(), Input::GetMouseY() };
        }
    }

    void EditorCamera::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& event) {
            // 用滚轮改变距离，并根据当前距离动态调整缩放速度（离得越近，缩得越慢，防止穿模）
            float distance = m_Distance;
            float zoomSpeed = std::max(distance * 0.2f, 0.1f);
            m_Distance -= event.GetYOffset() * zoomSpeed;
            
            // 限制一下最近和最远距离
            if (m_Distance < 0.1f) m_Distance = 0.1f;
            if (m_Distance > 10000.0f) m_Distance = 10000.0f;
            
            RecalculateView();
            return false;
        });
    }

    void EditorCamera::RecalculateView() {
        glm::quat orientation = glm::quat(glm::vec3(m_Pitch, m_Yaw, 0.0f));
        
        // ==========================================
        // 核心升级：真正的轨道相机数学模型！
        // ==========================================
        // 1. 提取相机当前的前方朝向
        glm::vec3 forward = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
        
        // 2. 相机的真实物理位置 = 焦点位置 - (前方方向 * 距离)
        m_Position = m_FocalPoint - forward * m_Distance;

        // 3. 计算最终的视图矩阵
        m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
        m_ViewMatrix = glm::inverse(m_ViewMatrix);
    }

}