#pragma once

#include "Window.hpp"
#include "LayerStack.hpp"
#include "Events/Event.hpp"            
#include "Events/ApplicationEvent.hpp" 
#include "Events/KeyEvent.hpp"
#include <memory>

namespace Ayaya {

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();
        void OnEvent(Event& e);

        inline static Application& Get() { return *s_Instance; }
        inline Window& GetWindow() { return *m_Window; }

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);

    private:
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);
        bool OnMouseMoved(MouseMovedEvent& e);
        bool OnMouseScrolled(MouseScrolledEvent& e);

    private:
        std::unique_ptr<Window> m_Window;
        bool m_Running = true;
        static Application* s_Instance;

        LayerStack m_LayerStack;
        float m_LastFrameTime = 0.0f;
    };

}