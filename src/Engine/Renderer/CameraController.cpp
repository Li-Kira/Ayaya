#include "CameraController.hpp"
#include "Core/Input.hpp"
#include "Core/KeyCodes.hpp"

// 引入四元数运算
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {
    CameraController::CameraController(float aspectRatio, bool isPerspective)
        : m_AspectRatio(aspectRatio) {
        if (isPerspective) m_Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        m_Camera.SetViewportSize(1280, 720); // 初始默认
    }

    void CameraController::OnUpdate(Timestep ts) {
        // 如果 Viewport 没有焦点，则不响应任何输入
        if (!m_Active) return;

        // ==========================================
        // 只有在按住鼠标右键时，才允许旋转和移动相机
        // ==========================================
        if (Input::IsMouseButtonPressed(1)) {
            // --- 1. 处理鼠标拖拽旋转 ---
            glm::vec2 currentMousePos = { Input::GetMouseX(), Input::GetMouseY() };
            glm::vec2 delta = (currentMousePos - m_InitialMousePosition) * m_MouseSensitivity;
            m_InitialMousePosition = currentMousePos;

            m_Rotation.x -= delta.y; 
            m_Rotation.y -= delta.x;

            // 限制 Pitch 角度，防止翻转死锁
            if (m_Rotation.x > 89.0f)  m_Rotation.x = 89.0f;
            if (m_Rotation.x < -89.0f) m_Rotation.x = -89.0f;

            // --- 2. 处理键盘平移 (现在包含在右键按下的条件内) ---
            glm::quat orientation = glm::quat(glm::radians(m_Rotation));
            glm::vec3 forward = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 right   = glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f));
            // glm::vec3 up   = glm::rotate(orientation, glm::vec3(0.0f, 1.0f, 0.0f));

            float velocity = m_MoveSpeed * (float)ts;

            if (Input::IsKeyPressed(Key::W)) m_Position += forward * velocity;
            if (Input::IsKeyPressed(Key::S)) m_Position -= forward * velocity;
            if (Input::IsKeyPressed(Key::A)) m_Position -= right * velocity;
            if (Input::IsKeyPressed(Key::D)) m_Position += right * velocity;
            
            // Q/E 控制绝对的上升/下降
            if (Input::IsKeyPressed(Key::E)) m_Position += glm::vec3(0.0f, 1.0f, 0.0f) * velocity;
            if (Input::IsKeyPressed(Key::Q)) m_Position -= glm::vec3(0.0f, 1.0f, 0.0f) * velocity;

        } else {
            // 如果没按住右键，也要实时更新初始位置，防止下次右击时画面跳跃
            m_InitialMousePosition = { Input::GetMouseX(), Input::GetMouseY() };
        }
    }

    glm::mat4 CameraController::GetViewProjection() const {
        glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(m_Rotation)));
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) * rotation;

        return m_Camera.GetProjection() * glm::inverse(transform);
    }

    void CameraController::OnResize(float width, float height) {
        m_AspectRatio = width / height;
        m_Camera.SetViewportSize((uint32_t)width, (uint32_t)height);
    }
}