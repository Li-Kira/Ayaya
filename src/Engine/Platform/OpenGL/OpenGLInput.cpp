#include "Core/Input.hpp"
#include "Core/Application.hpp"
#include <GLFW/glfw3.h>

namespace Ayaya {

    class OpenGLInput : public Input {
    protected:
        virtual bool IsKeyPressedImpl(KeyCode key) override {
            // 通过 Application 单例获取原生窗口句柄
            auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            int state = glfwGetKey(window, key);
            return state == GLFW_PRESS || state == GLFW_REPEAT;
        }

        virtual bool IsMouseButtonPressedImpl(MouseCode button) override {
            auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            int state = glfwGetMouseButton(window, button);
            return state == GLFW_PRESS;
        }

        virtual std::pair<float, float> GetMousePositionImpl() override {
            auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            return { (float)xpos, (float)ypos };
        }

        virtual float GetMouseXImpl() override { return GetMousePositionImpl().first; }
        virtual float GetMouseYImpl() override { return GetMousePositionImpl().second; }
    };

    // 初始化静态单例实例
    Input* Input::s_Instance = new OpenGLInput();

}