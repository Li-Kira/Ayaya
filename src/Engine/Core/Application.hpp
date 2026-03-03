#pragma once

#include "Window.hpp"
#include "LayerStack.hpp"
#include <memory>

namespace Ayaya {

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();

        // 单例访问器
        inline static Application& Get() { return *s_Instance; }
        inline Window& GetWindow() { return *m_Window; }

        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
    private:
        std::unique_ptr<Window> m_Window;
        bool m_Running = true;
        static Application* s_Instance;

        LayerStack m_LayerStack;
        float m_LastFrameTime = 0.0f;
    };

}