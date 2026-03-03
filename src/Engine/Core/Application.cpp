#include "Application.hpp"
#include <GLFW/glfw3.h>
#include "Input.hpp"
#include "Log.hpp"
#include "KeyCodes.hpp"
#include "Renderer/Renderer.hpp" 

namespace Ayaya {

    Application* Application::s_Instance = nullptr;

    Application::Application() {
        s_Instance = this; 
        Log::Init();
        AYAYA_CORE_WARN("Ayaya Engine is starting up...");

        m_Window = std::make_unique<Window>(1280, 720, "Ayaya Engine v0.1");
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));

        Renderer::Init();
        AYAYA_CORE_INFO("Window created and Renderer initialized.");
    }

    Application::~Application() {}

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        
        dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
        dispatcher.Dispatch<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
        dispatcher.Dispatch<KeyPressedEvent>(std::bind(&Application::OnKeyPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&Application::OnMouseButtonPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseMovedEvent>(std::bind(&Application::OnMouseMoved, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseScrolledEvent>(std::bind(&Application::OnMouseScrolled, this, std::placeholders::_1));

        for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); ) {
            (*--it)->OnEvent(e);
            if (e.Handled) break;
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent& e) {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        AYAYA_CORE_INFO("Window Resize Logic: {0}, {1}", e.GetWidth(), e.GetHeight());
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
        return false;
    }

    // 实现按键处理逻辑
    bool Application::OnKeyPressed(KeyPressedEvent& e) {
        if (e.GetKeyCode() == Key::Escape) {
            m_Running = false;
            return true;
        }

        if (e.GetKeyCode() == Key::Enter) {
            AYAYA_CORE_INFO("Enter key pressed! [Verified by Event System]");
            return true;
        }

        return false;
    }

    bool Application::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
        AYAYA_CORE_TRACE("Mouse Button Pressed: {0}", e.GetMouseButton());
        return false; // 返回 false 以允许事件继续传递给 Layer
    }

    bool Application::OnMouseMoved(MouseMovedEvent& e) {
        // 注意：鼠标移动较频繁，生产环境建议按需开启
        AYAYA_CORE_TRACE("Mouse Moved: {0}, {1}", e.GetX(), e.GetY());
        return false;
    }

    bool Application::OnMouseScrolled(MouseScrolledEvent& e) {
        AYAYA_CORE_TRACE("Mouse Scrolled: {0}, {1}", e.GetXOffset(), e.GetYOffset());
        return false;
    }

    void Application::PushLayer(Layer* layer) { m_LayerStack.PushLayer(layer); layer->OnAttach(); }
    void Application::PushOverlay(Layer* overlay) { m_LayerStack.PushOverlay(overlay); overlay->OnAttach(); }

    void Application::Run() {
        while (m_Running) {
            float time = (float)glfwGetTime(); 
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(timestep);

            m_Window->OnUpdate();
        }
    }
}