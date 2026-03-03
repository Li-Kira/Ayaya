#include "Application.hpp"
#include <GLFW/glfw3.h> // 需要用到 glfwGetTime
#include "Input.hpp"
#include "Log.hpp"

namespace Ayaya {

    Application* Application::s_Instance = nullptr;

    Application::Application() {
        // 1. 初始化单例指针
        s_Instance = this; 

        // 2. 初始化日志
        Log::Init();
        AYAYA_CORE_WARN("Ayaya Engine is starting up...");

        // 3. 创建窗口
        m_Window = std::make_unique<Window>(1280, 720, "Ayaya Engine v0.1");
        AYAYA_CORE_INFO("Window created: 1280x720");
    }

    Application::~Application() {
    }

    void Application::PushLayer(Layer* layer) { m_LayerStack.PushLayer(layer); }
    void Application::PushOverlay(Layer* overlay) { m_LayerStack.PushOverlay(overlay); }

    void Application::Run() {
        while (m_Running) {
            if (m_Window->ShouldClose()) {
                m_Running = false;
            }

            // 1. 计算当前帧的时间
            float time = (float)glfwGetTime(); 
            Timestep timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            // --- 输入检测测试 ---
            if (Input::IsKeyPressed(Key::Escape)) {
                AYAYA_CORE_WARN("Escape key pressed! Closing application...");
                m_Running = false;
            }

            if (Input::IsMouseButtonPressed(Mouse::ButtonLeft)) {
                auto [x, y] = Input::GetMousePosition();
                AYAYA_CORE_TRACE("Mouse Left Clicked at: {0}, {1}", x, y);
            }

            // 2. 对每个层应用时间
            for (Layer* layer : m_LayerStack)
                layer->OnUpdate(timestep);

            m_Window->OnUpdate();
        }
    }

}