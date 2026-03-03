#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/CameraController.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>
#include <Events/ApplicationEvent.hpp>
#include <Events/KeyEvent.hpp>
#include <Events/MouseEvent.hpp>

// --- 新增：场景与 ECS 相关的头文件 ---
#include <Scene/Scene.hpp>
#include <Scene/Entity.hpp>
#include <Scene/Components.hpp>
#include <Scene/SceneHierarchyPanel.hpp> 

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        // 1. 初始化相机与 Framebuffer
        m_CameraController = std::make_unique<Ayaya::CameraController>(16.0f / 9.0f, true);

        Ayaya::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Ayaya::Framebuffer::Create(fbSpec);

        // 2. 加载渲染资源
        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        m_Texture = Ayaya::Texture2D::Create("assets/textures/bricks2.jpg");

        // 3. 构建立方体几何数据
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
        vbo->SetLayout({
            { Ayaya::ShaderDataType::Float3, "a_Position" },
            { Ayaya::ShaderDataType::Float3, "a_Color"    },
            { Ayaya::ShaderDataType::Float2, "a_TexCoord" }
        });
        m_VertexArray->AddVertexBuffer(vbo);

        uint32_t indices[] = {
            0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
            4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4
        };
        auto ibo = std::shared_ptr<Ayaya::IndexBuffer>(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);

        // ==========================================
        // 4. ECS 核心：初始化场景与实体
        // ==========================================
        m_ActiveScene = std::make_shared<Ayaya::Scene>();

        // 创建第一个方块
        Ayaya::Entity square1 = m_ActiveScene->CreateEntity("Left Square");
        square1.GetComponent<Ayaya::TransformComponent>().Translation = { -1.5f, 0.0f, 0.0f };

        // 创建第二个方块
        Ayaya::Entity square2 = m_ActiveScene->CreateEntity("Right Square");
        square2.GetComponent<Ayaya::TransformComponent>().Translation = { 1.5f, 0.0f, 0.0f };

        // 将场景传递给 UI 面板
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 1. 更新相机
        m_CameraController->OnUpdate(ts);

        // 2. 绑定 Framebuffer 绘制 3D 场景
        m_Framebuffer->Bind();
        Ayaya::RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        Ayaya::RenderCommand::Clear();

        Ayaya::Renderer::BeginScene(*m_CameraController);

        auto shader = m_ShaderLibrary.Get("default");
        m_Texture->Bind(0);
        shader->Bind();
        shader->SetInt("u_Texture", 0);
        shader->SetFloat3("u_ColorModifier", glm::vec3(1.0f)); // 暂未集成材质系统，固定白色

        // ==========================================
        // 核心渲染循环：向 ECS 查询并渲染数据！
        // ==========================================
        auto view = m_ActiveScene->Reg().view<Ayaya::TransformComponent>();
        for (auto entity : view) {
            auto& transform = view.get<Ayaya::TransformComponent>(entity);
            // 实体 Transform 数据转化为矩阵传递给渲染器
            Ayaya::Renderer::Submit(shader, m_VertexArray, transform.GetTransform());
        }

        Ayaya::Renderer::EndScene();
        m_Framebuffer->Unbind();

        // 3. 清理主屏幕
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Ayaya::RenderCommand::Clear();
    }

    virtual void OnImGuiRender() override {
        static bool dockspaceOpen = true;
        static bool opt_fullscreen = true;
        static bool opt_padding = false;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen) {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (!opt_padding) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Ayaya Editor DockSpace", &dockspaceOpen, window_flags);
        if (!opt_padding) ImGui::PopStyleVar();
        if (opt_fullscreen) ImGui::PopStyleVar(2);

        // 提交 DockSpace 节点，并构建新布局
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            
            static bool first_time = true;
            if (first_time) {
                first_time = false;
                if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
                    ImGui::DockBuilderRemoveNode(dockspace_id); 
                    ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
                    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

                    ImGuiID dock_main_id = dockspace_id;
                    // 右侧切出 25% 空间
                    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
                    // 右侧再上下平分给大纲和属性面板
                    ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.5f, nullptr, &dock_id_right);

                    ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
                    ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_id_right);
                    ImGui::DockBuilderDockWindow("Properties", dock_id_right_bottom);
                    
                    ImGui::DockBuilderFinish(dockspace_id);
                }
            }
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) { /* TODO: Close Window */ }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // ==========================================
        // 绘制编辑器面板
        // ==========================================
        m_SceneHierarchyPanel.OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();
        m_CameraController->SetActive(m_ViewportFocused);

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize.x != viewportPanelSize.x || m_ViewportSize.y != viewportPanelSize.y) {
            if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
                m_Framebuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
                m_CameraController->OnResize(viewportPanelSize.x, viewportPanelSize.y);
                m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            }
        }

        uint32_t textureID = m_Framebuffer->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((intptr_t)textureID), 
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                     ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::End(); // End DockSpace
    }

    virtual void OnEvent(Ayaya::Event& event) override {
        Ayaya::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Ayaya::WindowResizeEvent>([this](Ayaya::WindowResizeEvent& e) { return false; });
    }

private:
    Ayaya::ShaderLibrary m_ShaderLibrary; 
    std::shared_ptr<Ayaya::Texture2D> m_Texture;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;
    std::unique_ptr<Ayaya::CameraController> m_CameraController;

    std::shared_ptr<Ayaya::Framebuffer> m_Framebuffer; 
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         

    // ECS 相关成员
    std::shared_ptr<Ayaya::Scene> m_ActiveScene;
    Ayaya::SceneHierarchyPanel m_SceneHierarchyPanel;

    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
};