#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <string>
#include <functional>

#include "Events/Event.hpp"
#include "Events/ApplicationEvent.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"

namespace Ayaya {

    class Window {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        Window(int width, int height, const std::string& title);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void OnUpdate();

        // 基础状态查询
        bool ShouldClose() const { return glfwWindowShouldClose(m_Window); }
        unsigned int GetWidth() const { return m_Data.Width; }
        unsigned int GetHeight() const { return m_Data.Height; }

        // 事件系统核心：设置回调函数
        void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
        
        void SetVSync(bool enabled);
        bool IsVSync() const { return m_Data.VSync; }

        inline GLFWwindow* GetNativeWindow() const { return m_Window; }

    private:
        GLFWwindow* m_Window;

        // 内部结构体，用于在 GLFW 回调中通过 UserPointer 获取数据
        struct WindowData {
            std::string Title;
            unsigned int Width, Height;
            bool VSync;
            EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };

}