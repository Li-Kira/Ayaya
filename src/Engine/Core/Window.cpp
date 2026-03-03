#include "Window.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    Window::Window(int width, int height, const std::string& title) {
        m_Data.Title = title;

        if (!glfwInit()) {
            AYAYA_CORE_ERROR("Could not initialize GLFW!");
            return;
        }

        // macOS OpenGL 4.1 Core Profile 配置
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        
        if (!m_Window) {
            AYAYA_CORE_ERROR("Failed to create GLFW window!");
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(m_Window);
        glfwSetWindowUserPointer(m_Window, &m_Data);

        // --- 核心修复：获取初始物理像素大小以适配 Retina 屏幕 ---
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
        m_Data.Width = fbWidth;
        m_Data.Height = fbHeight;

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            AYAYA_CORE_ERROR("Failed to initialize GLAD!");
        }

        // --- 注册物理像素级的缩放回调 ---
        glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            WindowResizeEvent event(width, height);
            data.EventCallback(event);
        });

        // 2. 窗口关闭回调
        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        // 3. 键盘按键回调 (对接 KeyEvent)
        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            switch (action) {
                case GLFW_PRESS:   { KeyPressedEvent event(key, 0); data.EventCallback(event); break; }
                case GLFW_RELEASE: { KeyReleasedEvent event(key); data.EventCallback(event); break; }
                case GLFW_REPEAT:  { KeyPressedEvent event(key, 1); data.EventCallback(event); break; }
            }
        });

        // 4. 鼠标按键回调 (对接 MouseEvent)
        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            switch (action) {
                case GLFW_PRESS:   { MouseButtonPressedEvent event(button); data.EventCallback(event); break; }
                case GLFW_RELEASE: { MouseButtonReleasedEvent event(button); data.EventCallback(event); break; }
            }
        });

        // 5. 鼠标滚动回调
        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event);
        });

        // 6. 鼠标移动回调
        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos) {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseMovedEvent event((float)xPos, (float)yPos);
            data.EventCallback(event);
        });

        SetVSync(true);
    }

    Window::~Window() {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }

    void Window::SetVSync(bool enabled) {
        if (enabled) glfwSwapInterval(1);
        else glfwSwapInterval(0);
        m_Data.VSync = enabled;
    }

    void Window::OnUpdate() {
        glfwPollEvents();
        glfwSwapBuffers(m_Window);
    }
}