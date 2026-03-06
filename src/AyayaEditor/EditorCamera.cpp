#include "EditorCamera.hpp"
#include "Engine/Core/Input.hpp"
#include "Engine/Core/KeyCodes.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {

    EditorCamera::EditorCamera() {
        m_Position = { -4.255f, 2.300f, 5.245f };
        m_Pitch = glm::radians(-22.093f);
        m_Yaw = glm::radians(-33.919f);

        OnResize(1280.0f, 720.0f);
        RecalculateView();
    }

    void EditorCamera::OnResize(float width, float height) {
        m_AspectRatio = width / height;
        m_Projection = glm::perspective(glm::radians(m_FOV), m_AspectRatio, m_NearClip, m_FarClip);
    }

    void EditorCamera::OnUpdate(Timestep ts, bool viewportFocused) {
        // 只有鼠标停留在视口内，才允许漫游
        if (!viewportFocused) return;

        // 按住鼠标右键进入飞行模式！
        if (Input::IsMouseButtonPressed(1)) {
            glm::vec2 mousePos = { Input::GetMouseX(), Input::GetMouseY() };
            glm::vec2 delta = (mousePos - m_InitialMousePosition) * 0.003f; // 鼠标灵敏度
            m_InitialMousePosition = mousePos;

            m_Pitch -= delta.y;
            m_Yaw -= delta.x;

            // 限制抬头/低头角度，防止后空翻导致万向节死锁
            if (m_Pitch > glm::radians(89.0f)) m_Pitch = glm::radians(89.0f);
            if (m_Pitch < glm::radians(-89.0f)) m_Pitch = glm::radians(-89.0f);

            glm::quat orientation = glm::quat(glm::vec3(m_Pitch, m_Yaw, 0.0f));
            glm::vec3 forward = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 right   = glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::vec3 up      = glm::rotate(orientation, glm::vec3(0.0f, 1.0f, 0.0f));

            // 专业优化：按住 Shift 键提供 4 倍移速！
            float velocity = (Input::IsKeyPressed(Key::LeftShift) ? 10.0f : 5.0f) * (float)ts;

            if (Input::IsKeyPressed(Key::W)) m_Position += forward * velocity;
            if (Input::IsKeyPressed(Key::S)) m_Position -= forward * velocity;
            if (Input::IsKeyPressed(Key::A)) m_Position -= right * velocity;
            if (Input::IsKeyPressed(Key::D)) m_Position += right * velocity;
            if (Input::IsKeyPressed(Key::E)) m_Position += up * velocity;
            if (Input::IsKeyPressed(Key::Q)) m_Position -= up * velocity;

            RecalculateView();
        } else {
            // 松开右键时，时刻记录鼠标位置，防止下次按下时镜头瞬移
            m_InitialMousePosition = { Input::GetMouseX(), Input::GetMouseY() };
        }
    }

    void EditorCamera::RecalculateView() {
        glm::quat orientation = glm::quat(glm::vec3(m_Pitch, m_Yaw, 0.0f));
        // 视图矩阵是相机位置矩阵的逆矩阵
        m_ViewMatrix = glm::translate(glm::mat4(1.0f), m_Position) * glm::toMat4(orientation);
        m_ViewMatrix = glm::inverse(m_ViewMatrix);
    }

}