#pragma once

#include "Window.hpp"
#include "LayerStack.hpp"
#include "Events/Event.hpp"            
#include "Events/ApplicationEvent.hpp" 
#include "Events/KeyEvent.hpp"
#include "Events/MouseEvent.hpp"
#include "Core/Timestep.hpp"
#include "Core/ImGuiLayer.hpp"

#include <memory>

namespace Ayaya {

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();
        void OnEvent(Event& e);
        void Close() { m_Running = false; }

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);

        inline static Application& Get() { return *s_Instance; }
        inline Window& GetWindow() { return *m_Window; }

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
        bool OnMouseMoved(MouseMovedEvent& e);
        bool OnMouseScrolled(MouseScrolledEvent& e);

    private:
        std::unique_ptr<Window> m_Window;
        ImGuiLayer* m_ImGuiLayer; // 特殊持有的 ImGui 指针
        bool m_Running = true;
        LayerStack m_LayerStack;
        float m_LastFrameTime = 0.0f;

        static Application* s_Instance;
    };

    // 供客户端（如 Sandbox）调用的入口点
    Application* CreateApplication();

}