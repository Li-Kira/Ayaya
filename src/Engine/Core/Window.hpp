#pragma once

// 【移除 glad/glad.h，不再让 Window 层依赖具体的渲染 API】
#define GLFW_INCLUDE_NONE
// ==========================================
// 【核心修复】：强行要求 GLFW 暴露 Vulkan 的表面创建 API！
// （放心，Vulkan 的头文件极其纯净，绝对不会污染你现有的 OpenGL 代码）
// ==========================================
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <functional>
#include <memory>

#include "Events/Event.hpp"
#include "Events/ApplicationEvent.hpp"
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Renderer/GraphicsContext.hpp" // 【新增】：引入图形上下文接口

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
        void SetSize(unsigned int width, unsigned int height);

        // 事件系统核心：设置回调函数
        void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
        
        void SetVSync(bool enabled);
        bool IsVSync() const { return m_Data.VSync; }

        inline GLFWwindow* GetNativeWindow() const { return m_Window; }

    private:
        GLFWwindow* m_Window;
        
        // 【新增】：当前窗口绑定的图形上下文
        std::shared_ptr<GraphicsContext> m_Context;

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