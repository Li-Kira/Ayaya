#include "Input.hpp"
#include "Application.hpp"
#include <GLFW/glfw3.h>

namespace Ayaya {

    bool Input::IsKeyPressed(KeyCode key) {
        // 获取当前 GLFW 窗口句柄
        auto* window = Application::Get().GetWindow().GetNativeWindow();
        int state = glfwGetKey(window, key);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(KeyCode button) {
        auto* window = Application::Get().GetWindow().GetNativeWindow();
        int state = glfwGetMouseButton(window, button);
        return state == GLFW_PRESS;
    }

    std::pair<float, float> Input::GetMousePosition() {
        auto* window = Application::Get().GetWindow().GetNativeWindow();
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { (float)xpos, (float)ypos };
    }

    float Input::GetMouseX() { return GetMousePosition().first; }
    float Input::GetMouseY() { return GetMousePosition().second; }

}