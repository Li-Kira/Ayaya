#include "Application.hpp"

namespace Ayaya {

    Application::Application() {
        // 创建 1280x720 的窗口
        m_Window = std::make_unique<Window>(1280, 720, "Ayaya Engine v0.1");
    }

    Application::~Application() {
    }

    void Application::Run() {
        while (m_Running) {
            if (m_Window->ShouldClose()) {
                m_Running = false;
            }

            // 渲染指令 (暂时写在这里，之后会移入 Renderer 类)
            glClearColor(0.1f, 0.1f, 0.12f, 1.0f); // 深色背景
            glClear(GL_COLOR_BUFFER_BIT);

            m_Window->OnUpdate();
        }
    }

}