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
        
        // 注意：如果你在 main() 里已经调用了 Log::Init()，这里可以删掉，否则保留即可。
        Log::Init();
        AYAYA_CORE_WARN("Ayaya Engine is starting up...");

        m_Window = std::make_unique<Window>(1280, 720, "Ayaya Engine v0.1");
        m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
        AYAYA_CORE_INFO("GLFW Window initialized successfully.");

        Renderer::Init();
        AYAYA_CORE_INFO("Renderer initialized successfully.");

        // 创建并初始化 ImGuiLayer
        m_ImGuiLayer = new ImGuiLayer();
        PushOverlay(m_ImGuiLayer);
        AYAYA_CORE_INFO("ImGui initialized successfully.");
    }

    Application::~Application() {
        Renderer::Shutdown(); // 建议加上清理逻辑
    }

    void Application::PushLayer(Layer* layer) { 
        m_LayerStack.PushLayer(layer); 
        layer->OnAttach(); 
    }
    
    void Application::PushOverlay(Layer* overlay) { 
        m_LayerStack.PushOverlay(overlay); 
        overlay->OnAttach(); 
    }

    void Application::OnEvent(Event& e) {
        EventDispatcher dispatcher(e);
        
        dispatcher.Dispatch<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
        dispatcher.Dispatch<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
        dispatcher.Dispatch<KeyPressedEvent>(std::bind(&Application::OnKeyPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseButtonPressedEvent>(std::bind(&Application::OnMouseButtonPressed, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseMovedEvent>(std::bind(&Application::OnMouseMoved, this, std::placeholders::_1));
        dispatcher.Dispatch<MouseScrolledEvent>(std::bind(&Application::OnMouseScrolled, this, std::placeholders::_1));

        // 如果 ImGui 拦截了鼠标，e.Handled 会变为 true，后续 Layer 不再处理
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it) {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    void Application::Run() {
        while (m_Running) {
            float time = (float)glfwGetTime(); 
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // 1. 逻辑更新（渲染场景）
            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(timestep);

            // 2. ImGui 渲染阶段
            m_ImGuiLayer->Begin();
            for (Layer* layer : m_LayerStack)
                layer->OnImGuiRender(); // 每个层渲染自己的 UI
            m_ImGuiLayer->End();

            m_Window->OnUpdate();
        }
    }

    // =========================================================================
    // 事件处理函数实现
    // =========================================================================
    
    bool Application::OnWindowClose(WindowCloseEvent& e) {
        m_Running = false;
        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e) {
        if (e.GetWidth() == 0 || e.GetHeight() == 0) {
            return false; // 防止窗口最小化时触发 0x0 渲染崩溃
        }
        
        AYAYA_CORE_INFO("Window Resize Logic: {0}, {1}", e.GetWidth(), e.GetHeight());
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
        return false;
    }

    // 实现按键处理逻辑
    bool Application::OnKeyPressed(KeyPressedEvent& e) {
        // if (e.GetKeyCode() == Key::Escape) {
        //     m_Running = false;
        //     return true;
        // }

        if (e.GetKeyCode() == Key::Enter) {
            AYAYA_CORE_INFO("Enter key pressed! [Verified by Event System]");
            return true;
        }

        return false;
    }

    bool Application::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
        AYAYA_CORE_TRACE("Mouse Button Pressed: {0}", e.GetMouseButton());
        float mouseX = Input::GetMouseX();
        float mouseY = Input::GetMouseY();
        
        AYAYA_CORE_TRACE("Mouse Button Pressed: {0} at position ({1}, {2})", e.GetMouseButton(), mouseX, mouseY);
        return false; // 返回 false 以允许事件继续传递给 Layer
    }

    bool Application::OnMouseMoved(MouseMovedEvent& e) {
        // 注意：鼠标移动较频繁，生产环境建议按需开启
        // AYAYA_CORE_TRACE("Mouse Moved: {0}, {1}", e.GetX(), e.GetY());
        return false;
    }

    bool Application::OnMouseScrolled(MouseScrolledEvent& e) {
        AYAYA_CORE_TRACE("Mouse Scrolled: {0}, {1}", e.GetXOffset(), e.GetYOffset());
        return false;
    }
}