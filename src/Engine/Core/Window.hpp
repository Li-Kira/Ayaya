#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

namespace Ayaya {

    class Window {
    public:
        Window(int width, int height, const std::string& title);
        ~Window();

        // 禁用拷贝，防止意外销毁 GLFW 句柄
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        bool ShouldClose() const;
        void OnUpdate();

        unsigned int GetWidth() const { return m_Width; }
        unsigned int GetHeight() const { return m_Height; }
        GLFWwindow* GetNativeWindow() const { return m_Window; }

    private:
        GLFWwindow* m_Window;
        unsigned int m_Width, m_Height;
        std::string m_Title;
    };

}