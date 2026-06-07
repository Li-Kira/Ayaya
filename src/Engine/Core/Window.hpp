#pragma once

#include <GLFW/glfw3.h>

#include <string>
#include <vector>
#include <mutex>
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

        // 【新增】：暴露图形上下文，方便上层系统（如 ImGui 和 Renderer）获取底层句柄
        inline std::shared_ptr<GraphicsContext> GetContext() const { return m_Context; }

        // OS file drag-drop: drain and return all paths dropped since last call.
        // Thread-safe — called from EditorLayer::OnUpdate on the main thread.
        static std::vector<std::string> GetDroppedPaths();

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

        // OS file drag-drop queue
        static std::vector<std::string> s_DroppedPaths;
        static std::mutex s_DropMutex;
    };

}