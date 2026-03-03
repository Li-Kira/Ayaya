#include "CameraController.hpp"
#include "Core/Input.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {
    CameraController::CameraController(float aspectRatio, bool isPerspective)
        : m_AspectRatio(aspectRatio) {
        if (isPerspective) m_Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        m_Camera.SetViewportSize(1280, 720); // 初始默认
    }

    void CameraController::OnUpdate(Timestep ts) {
        // 1. 位置控制
        if (Input::IsKeyPressed(Key::W)) m_Position.y += m_MoveSpeed * ts;
        if (Input::IsKeyPressed(Key::S)) m_Position.y -= m_MoveSpeed * ts;
        if (Input::IsKeyPressed(Key::A)) m_Position.x -= m_MoveSpeed * ts;
        if (Input::IsKeyPressed(Key::D)) m_Position.x += m_MoveSpeed * ts;
        
        // 2. 深度/缩放控制 (透视模式下移动 Z，正交模式下应调整 Size)
        if (Input::IsKeyPressed(Key::Z)) m_Position.z -= m_MoveSpeed * ts;
        if (Input::IsKeyPressed(Key::X)) m_Position.z += m_MoveSpeed * ts;

        // 3. 旋转控制 (示例 Q/E 绕 Z 轴转)
        if (Input::IsKeyPressed(Key::Q)) m_Rotation.z += m_RotationSpeed * ts;
        if (Input::IsKeyPressed(Key::E)) m_Rotation.z -= m_RotationSpeed * ts;
    }

    glm::mat4 CameraController::GetViewProjection() const {
        // 计算 View Matrix: T * R 的逆
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_Position) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.z), {0,0,1}) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.y), {0,1,0}) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation.x), {1,0,0});

        return m_Camera.GetProjection() * glm::inverse(transform);
    }

    void CameraController::OnResize(float width, float height) {
        m_AspectRatio = width / height;
        m_Camera.SetViewportSize((uint32_t)width, (uint32_t)height);
    }
}