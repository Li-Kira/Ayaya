#include "Window.hpp"
#include <iostream>

namespace Ayaya {

    Window::Window(int width, int height, const std::string& title)
        : m_Width(width), m_Height(height), m_Title(title) 
    {
        if (!glfwInit()) {
            std::cerr << "Could not initialize GLFW!" << std::endl;
            return;
        }

        // macOS OpenGL 4.1 Core Profile 要求
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        
        if (!m_Window) {
            std::cerr << "Could not create GLFW window!" << std::endl;
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(m_Window);

        // 初始化 GLAD
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cerr << "Failed to initialize GLAD!" << std::endl;
        }

        // 开启垂直同步
        glfwSwapInterval(1);
    }

    Window::~Window() {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    bool Window::ShouldClose() const {
        return glfwWindowShouldClose(m_Window);
    }

    void Window::OnUpdate() {
        glfwPollEvents();
        glfwSwapBuffers(m_Window);
    }

}