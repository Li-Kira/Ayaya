#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/CameraController.hpp>
#include <Events/ApplicationEvent.hpp>
#include <Events/KeyEvent.hpp>
#include <Events/MouseEvent.hpp>
#include <glm/gtc/matrix_transform.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        // 1. 初始化相机控制器 (16:9, 开启透视)
        m_CameraController = std::make_unique<Ayaya::CameraController>(16.0f / 9.0f, true);

        // 2. 创建立方体 VAO
        m_VertexArray.reset(Ayaya::VertexArray::Create());

        float vertices[] = {
            -0.5f, -0.5f,  0.5f,  0.8f, 0.2f, 0.3f, // 0
             0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.3f, // 1
             0.5f,  0.5f,  0.5f,  0.2f, 0.3f, 0.8f, // 2
            -0.5f,  0.5f,  0.5f,  0.7f, 0.7f, 0.2f, // 3
            -0.5f, -0.5f, -0.5f,  0.8f, 0.2f, 0.3f, // 4
             0.5f, -0.5f, -0.5f,  0.2f, 0.8f, 0.3f, // 5
             0.5f,  0.5f, -0.5f,  0.2f, 0.3f, 0.8f, // 6
            -0.5f,  0.5f, -0.5f,  0.7f, 0.7f, 0.2f  // 7
        };

        auto vbo = std::shared_ptr<Ayaya::VertexBuffer>(Ayaya::VertexBuffer::Create(vertices, sizeof(vertices)));
        vbo->SetLayout({
            { Ayaya::ShaderDataType::Float3, "a_Position" },
            { Ayaya::ShaderDataType::Float3, "a_Color" }
        });
        m_VertexArray->AddVertexBuffer(vbo);

        uint32_t indices[] = {
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
            4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4
        };
        auto ibo = std::shared_ptr<Ayaya::IndexBuffer>(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);

        m_Shader = std::make_unique<Ayaya::Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 轮询输入处理相机位移 (WASD)
        m_CameraController->OnUpdate(ts);

        // 渲染流程
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.12f, 1.0f });
        Ayaya::RenderCommand::Clear();

        Ayaya::Renderer::BeginScene(*m_CameraController);

        // 提交多个立方体测试 Transform
        for (int x = 0; x < 3; x++) {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), { x * 1.5f, 0.0f, 0.0f });
            Ayaya::Renderer::Submit(m_Shader, m_VertexArray, transform);
        }

        Ayaya::Renderer::EndScene();
    }

    virtual void OnEvent(Ayaya::Event& event) override {
        Ayaya::EventDispatcher dispatcher(event);

        dispatcher.Dispatch<Ayaya::WindowResizeEvent>([this](Ayaya::WindowResizeEvent& e) {
            // 必须立即更新相机控制器的纵横比
            m_CameraController->OnResize((float)e.GetWidth(), (float)e.GetHeight());
            return false; 
        });

        // // 测试 1：响应滚轮事件进行“缩放”
        // dispatcher.Dispatch<Ayaya::MouseScrolledEvent>([this](Ayaya::MouseScrolledEvent& e) {
        //     // 这里可以根据偏移量 e.GetYOffset() 调整相机位置或 FOV
        //     // AYAYA_TRACE("Mouse scrolled: {0}", e.GetYOffset());
        //     return false; 
        // });

        // // 测试 2：响应按键事件切换投影模式
        // dispatcher.Dispatch<Ayaya::KeyPressedEvent>([this](Ayaya::KeyPressedEvent& e) {
        //     if (e.GetKeyCode() == Ayaya::Key::P) {
        //         auto& camera = m_CameraController->GetCamera();
        //         auto type = camera.GetProjectionType() == Ayaya::SceneCamera::ProjectionType::Perspective 
        //                     ? Ayaya::SceneCamera::ProjectionType::Orthographic 
        //                     : Ayaya::SceneCamera::ProjectionType::Perspective;
        //         camera.SetProjectionType(type);
        //         AYAYA_INFO("Switched Projection Type!");
        //     }
        //     return false;
        // });
    }

private:
    std::shared_ptr<Ayaya::Shader> m_Shader;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
    std::unique_ptr<Ayaya::CameraController> m_CameraController;
};