#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>

// --- 没有 ImGui, 没有 Framebuffer, 没有 Gizmo! ---
#include <Scene/Scene.hpp>
#include <Scene/Entity.hpp>
#include <Scene/Components.hpp>

class SandboxLayer : public Ayaya::Layer {
public:
    SandboxLayer() : Layer("SandboxLayer") {}

    virtual void OnAttach() override {
        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        m_Texture = Ayaya::Texture2D::Create("assets/textures/bricks2.jpg");

        // 构建基础网格 (与 Editor 相同)
        m_VertexArray.reset(Ayaya::VertexArray::Create());
        float vertices[] = {
            -0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 0.0f,
             0.5f, -0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f,
             0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 1.0f,
            -0.5f,  0.5f,  0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 1.0f,
            -0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  1.0f, 0.0f,
             0.5f, -0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 0.0f,
             0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  0.0f, 1.0f,
            -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 1.0f,  1.1f, 1.0f
        };
        auto vbo = std::shared_ptr<Ayaya::VertexBuffer>(Ayaya::VertexBuffer::Create(vertices, sizeof(vertices)));
        vbo->SetLayout({ { Ayaya::ShaderDataType::Float3, "a_Position" }, { Ayaya::ShaderDataType::Float3, "a_Color" }, { Ayaya::ShaderDataType::Float2, "a_TexCoord" } });
        m_VertexArray->AddVertexBuffer(vbo);
        uint32_t indices[] = { 0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4 };
        auto ibo = std::shared_ptr<Ayaya::IndexBuffer>(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);

        // 初始化纯净场景
        m_ActiveScene = std::make_shared<Ayaya::Scene>();
        
        Ayaya::Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<Ayaya::CameraComponent>();
        cameraComp.Camera.SetProjectionType(Ayaya::SceneCamera::ProjectionType::Perspective);
        
        // 动态获取当前窗口大小来设置相机的长宽比
        auto windowWidth = Ayaya::Application::Get().GetWindow().GetWidth();
        auto windowHeight = Ayaya::Application::Get().GetWindow().GetHeight();
        cameraComp.Camera.SetViewportSize(windowWidth, windowHeight);
        
        cameraEntity.GetComponent<Ayaya::TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };

        Ayaya::Entity square = m_ActiveScene->CreateEntity("Player Square");
        square.AddComponent<Ayaya::SpriteRendererComponent>(glm::vec4{0.2f, 0.8f, 0.3f, 1.0f});
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 直接清空系统的主屏幕
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Ayaya::RenderCommand::Clear();

        // 查找主相机并渲染
        glm::mat4 cameraViewProj = glm::mat4(1.0f);
        bool hasPrimaryCamera = false;
        
        auto cameraView = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::CameraComponent>();
        for (auto entityID : cameraView) {
            Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
            auto& camera = entity.GetComponent<Ayaya::CameraComponent>();
            if (camera.Primary) {
                hasPrimaryCamera = true;
                cameraViewProj = camera.Camera.GetProjection() * glm::inverse(entity.GetWorldTransform());
                break; 
            }
        }

        if (hasPrimaryCamera) {
            Ayaya::Renderer::BeginScene(cameraViewProj);
            auto shader = m_ShaderLibrary.Get("default");
            m_Texture->Bind(0);
            shader->Bind();
            shader->SetInt("u_Texture", 0);

            auto renderGroup = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::SpriteRendererComponent>();
            for (auto entityID : renderGroup) {
                Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
                auto& sprite = entity.GetComponent<Ayaya::SpriteRendererComponent>();
                shader->SetFloat3("u_ColorModifier", glm::vec3(sprite.Color)); 
                Ayaya::Renderer::Submit(shader, m_VertexArray, entity.GetWorldTransform());
            }
            Ayaya::Renderer::EndScene();
        }
    }

    // 纯游戏环境，不渲染任何 ImGui 界面！
    virtual void OnImGuiRender() override {}

    // 监听窗口大小改变，实时更新相机的 Aspect Ratio 防止画面拉伸
    virtual void OnEvent(Ayaya::Event& event) override {
        Ayaya::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Ayaya::WindowResizeEvent>([this](Ayaya::WindowResizeEvent& e) {
            if (e.GetWidth() == 0 || e.GetHeight() == 0) return false;
            
            auto view = m_ActiveScene->Reg().view<Ayaya::CameraComponent>();
            for (auto entity : view) {
                auto& cameraComp = view.get<Ayaya::CameraComponent>(entity);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize(e.GetWidth(), e.GetHeight());
                }
            }
            return false;
        });
    }

private:
    Ayaya::ShaderLibrary m_ShaderLibrary; 
    std::shared_ptr<Ayaya::Texture2D> m_Texture;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
    std::shared_ptr<Ayaya::Scene> m_ActiveScene;
};