#include "ayapch.h"
#include "Window.hpp"
#include "Core/Log.hpp"
#include "Renderer/Renderer.hpp" // 【新增】：获取当前激活的 API

namespace Ayaya {

    Window::Window(int width, int height, const std::string& title) {
        m_Data.Title = title;

        if (!glfwInit()) {
            AYAYA_CORE_ERROR("Could not initialize GLFW!");
            return;
        }

        // ==========================================
        // 【核心修改】：根据 API 动态配置 GLFW 窗口属性
        // ==========================================
        if (RendererAPI::GetAPI() == RendererAPI::API::Vulkan) {
            // 如果是 Vulkan，必须告诉 GLFW 不要创建 OpenGL 上下文！
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); 
        } 
        else if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            // 如果是 OpenGL，走 Mac 兼容的 4.1 Core Profile 配置
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        }

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        
        if (!m_Window) {
            AYAYA_CORE_ERROR("Failed to create GLFW window!");
            glfwTerminate();
            return;
        }

        glfwSetWindowUserPointer(m_Window, &m_Data);

        // ==========================================
        // 【核心修改】：把 glad 和 context 的事情全权委托给 Context 工厂！
        // ==========================================
        m_Context = GraphicsContext::Create(m_Window);
        m_Context->Init();

        // --- 核心修复：获取初始物理像素大小以适配 Retina 屏幕 ---
        int fbWidth, fbHeight;
        glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
        m_Data.Width = fbWidth;
        m_Data.Height = fbHeight;

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

    void Window::SetSize(unsigned int width, unsigned int height) {
        glfwSetWindowSize(m_Window, width, height);
    }

    void Window::OnUpdate() {
        // ==========================================
        // 【终极修复】：调换顺序，拆除 Vulkan 的帧中炸弹！
        // 1. 必须先安全地结束当前帧，并提交给显卡 (SwapBuffers)
        // 2. 然后再去处理系统事件 (PollEvents)，防止中途触发画布重建
        // ==========================================
        
        // 先提交画面！此时 vkCmdEndRenderPass 能够安全访问到有效的 RenderPass
        if (m_Context) {
            m_Context->SwapBuffers();
        }
        
        // 再处理系统拖拽、缩放等事件。
        // 如果触发了 RecreateSwapChain，底层的 vkDeviceWaitIdle 会确保
        // 刚才提交的画面已经渲染完毕，然后再安全地销毁旧画布。
        glfwPollEvents();
    }
}