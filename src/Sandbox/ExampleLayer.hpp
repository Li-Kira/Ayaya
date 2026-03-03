#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/CameraController.hpp>
#include <glm/gtc/matrix_transform.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        // 1. 开启深度测试（立方体渲染必备）
        glEnable(GL_DEPTH_TEST);

        // 2. 初始化相机控制器 (16:9 比例, 默认透视模式)
        m_CameraController = std::make_unique<Ayaya::CameraController>(16.0f / 9.0f, true);

        // 3. 创建顶点数组 VAO
        m_VertexArray.reset(Ayaya::VertexArray::Create());

        // 4. 立方体数据：位置(3f) + 颜色(3f)
        float vertices[] = {
            // 前面 (Z=0.5)
            -0.5f, -0.5f,  0.5f,  0.8f, 0.2f, 0.3f, // 0
             0.5f, -0.5f,  0.5f,  0.2f, 0.8f, 0.3f, // 1
             0.5f,  0.5f,  0.5f,  0.2f, 0.3f, 0.8f, // 2
            -0.5f,  0.5f,  0.5f,  0.7f, 0.7f, 0.2f, // 3
            // 后面 (Z=-0.5)
            -0.5f, -0.5f, -0.5f,  0.8f, 0.2f, 0.3f, // 4
             0.5f, -0.5f, -0.5f,  0.2f, 0.8f, 0.3f, // 5
             0.5f,  0.5f, -0.5f,  0.2f, 0.3f, 0.8f, // 6
            -0.5f,  0.5f, -0.5f,  0.7f, 0.7f, 0.2f  // 7
        };

        std::shared_ptr<Ayaya::VertexBuffer> vbo;
        vbo.reset(Ayaya::VertexBuffer::Create(vertices, sizeof(vertices)));
        
        // 自动计算 Offset 和 Stride
        vbo->SetLayout({
            { Ayaya::ShaderDataType::Float3, "a_Position" },
            { Ayaya::ShaderDataType::Float3, "a_Color" }
        });
        m_VertexArray->AddVertexBuffer(vbo);

        // 5. 索引数据 (12个三角形)
        uint32_t indices[] = {
            0, 1, 2, 2, 3, 0, // front
            1, 5, 6, 6, 2, 1, // right
            7, 6, 5, 5, 4, 7, // back
            4, 0, 3, 3, 7, 4, // left
            3, 2, 6, 6, 7, 3, // top
            4, 5, 1, 1, 0, 4  // bottom
        };
        std::shared_ptr<Ayaya::IndexBuffer> ibo;
        ibo.reset(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);

        // 6. 加载着色器
        m_Shader = std::make_unique<Ayaya::Shader>("assets/shaders/default.vert", "assets/shaders/default.frag");
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // --- 1. 逻辑更新 ---
        m_CameraController->OnUpdate(ts);

        // 演示：按下 P 键切换投影模式
        if (Ayaya::Input::IsKeyPressed(Ayaya::Key::P)) {
            static bool perspective = true;
            perspective = !perspective;
            m_CameraController->GetCamera().SetProjectionType(
                perspective ? Ayaya::SceneCamera::ProjectionType::Perspective : Ayaya::SceneCamera::ProjectionType::Orthographic
            );
        }

        // --- 2. 渲染 ---
        // 设置背景色并清屏
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.12f, 1.0f });
        Ayaya::RenderCommand::Clear();

        // 开始场景：上传相机矩阵
        Ayaya::Renderer::BeginScene(*m_CameraController);

        // 提交第一个立方体（原点）
        Ayaya::Renderer::Submit(m_Shader, m_VertexArray);

        // 提交第二个立方体（平移并旋转）
        static float rotation = 0.0f;
        rotation += ts * 50.0f;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), { 2.0f, 0.0f, 0.0f }) 
                            * glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 1.0f, 1.0f, 1.0f });
        
        Ayaya::Renderer::Submit(m_Shader, m_VertexArray, transform);

        // 结束场景
        Ayaya::Renderer::EndScene();
    }

private:
    std::shared_ptr<Ayaya::Shader> m_Shader;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
    std::unique_ptr<Ayaya::CameraController> m_CameraController;
};