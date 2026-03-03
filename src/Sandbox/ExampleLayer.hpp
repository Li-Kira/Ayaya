#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>
#include <Events/ApplicationEvent.hpp>
#include <Events/KeyEvent.hpp>
#include <Events/MouseEvent.hpp>

// --- ECS 头文件 ---
#include <Scene/Scene.hpp>
#include <Scene/Entity.hpp>
#include <Scene/Components.hpp>
#include <Scene/SceneHierarchyPanel.hpp> 

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

class ExampleLayer : public Ayaya::Layer {
public:
    ExampleLayer() : Layer("ExampleLayer") {}

    virtual void OnAttach() override {
        // 1. 初始化 Framebuffer
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

        Ayaya::Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<Ayaya::CameraComponent>();
        cameraComp.Camera.SetProjectionType(Ayaya::SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        cameraEntity.GetComponent<Ayaya::TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };

        // ==========================================
        // 建立父子层级关系测试
        // ==========================================
        // 1. 创建一个空节点 (没有 SpriteRendererComponent，所以在画面中不可见)
        Ayaya::Entity parentNode = m_ActiveScene->CreateEntity("Parent Empty Node");
        
        // 2. 创建两个子方块
        Ayaya::Entity square1 = m_ActiveScene->CreateEntity("Left Square");
        square1.GetComponent<Ayaya::TransformComponent>().Translation = { -1.5f, 0.0f, 0.0f };
        square1.AddComponent<Ayaya::SpriteRendererComponent>(glm::vec4{0.2f, 0.8f, 0.3f, 1.0f});

        Ayaya::Entity square2 = m_ActiveScene->CreateEntity("Right Square");
        square2.GetComponent<Ayaya::TransformComponent>().Translation = { 1.5f, 0.0f, 0.0f };
        square2.AddComponent<Ayaya::SpriteRendererComponent>(glm::vec4{0.8f, 0.2f, 0.3f, 1.0f});

        // 3. 将方块设置为 parentNode 的子节点！
        parentNode.AddChild(square1);
        // parentNode.AddChild(square2);

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // 准备渲染环境
        m_Framebuffer->Bind();
        Ayaya::RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        Ayaya::RenderCommand::Clear();

        glm::mat4 cameraViewProj = glm::mat4(1.0f);
        bool hasPrimaryCamera = false;

        // ==========================================
        // ECS 阶段 1：查询主相机，更新逻辑并获取矩阵
        // ==========================================
        auto cameraView = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::CameraComponent>();
        for (auto entity : cameraView) {
            auto [transform, camera] = cameraView.get<Ayaya::TransformComponent, Ayaya::CameraComponent>(entity);
            if (camera.Primary) {
                hasPrimaryCamera = true;
                
                // 将脏乱的漫游逻辑封装进独立函数
                ProcessCameraInput(ts, transform);

                // glm::mat4 view = glm::inverse(transform.GetTransform());
                glm::mat4 worldTransform = GetWorldTransform(Ayaya::Entity{ entity, m_ActiveScene.get() });
                glm::mat4 view = glm::inverse(worldTransform);
                cameraViewProj = camera.Camera.GetProjection() * view;
                break; // 找到主相机即可
            }
        }

        // ==========================================
        // ECS 阶段 2：提交渲染
        // ==========================================
        if (hasPrimaryCamera) {
            Ayaya::Renderer::BeginScene(cameraViewProj);
            auto shader = m_ShaderLibrary.Get("default");
            m_Texture->Bind(0);
            shader->Bind();
            shader->SetInt("u_Texture", 0);

            // 查询所有拥有 Transform 和 SpriteRenderer 的实体
            auto renderGroup = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::SpriteRendererComponent>();
            for (auto entityID : renderGroup) {
                Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
                auto& sprite = entity.GetComponent<Ayaya::SpriteRendererComponent>();
                
                shader->SetFloat3("u_ColorModifier", glm::vec3(sprite.Color)); 
                
                // 核心修改：这里使用递归算出来的世界矩阵进行渲染！
                Ayaya::Renderer::Submit(shader, m_VertexArray, GetWorldTransform(entity));
            }
            Ayaya::Renderer::EndScene();
        }

        m_Framebuffer->Unbind();

        // 清理系统主窗口
        Ayaya::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        Ayaya::RenderCommand::Clear();
    }

    virtual void OnImGuiRender() override {
        // --- 1. DockSpace 全屏停靠容器 ---
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
                    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
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
                if (ImGui::MenuItem("Exit")) { Ayaya::Application::Get().GetWindow().ShouldClose(); }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // --- 2. 绘制大纲与属性面板 ---
        m_SceneHierarchyPanel.OnImGuiRender();

        // --- 3. 绘制 Viewport ---
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        if (m_ViewportSize.x != viewportPanelSize.x || m_ViewportSize.y != viewportPanelSize.y) {
            if (viewportPanelSize.x > 0.0f && viewportPanelSize.y > 0.0f) {
                m_Framebuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
                m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

                // 当视口大小改变时，更新 ECS 中所有的相机投影矩阵
                auto view = m_ActiveScene->Reg().view<Ayaya::CameraComponent>();
                for (auto entity : view) {
                    auto& cameraComp = view.get<Ayaya::CameraComponent>(entity);
                    if (!cameraComp.FixedAspectRatio) {
                        cameraComp.Camera.SetViewportSize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
                    }
                }
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

    virtual void OnEvent(Ayaya::Event& event) override {}

private:

    // 计算实体的绝对世界矩阵 (父矩阵 * 子局部矩阵)
    glm::mat4 GetWorldTransform(Ayaya::Entity entity) {
        auto& transform = entity.GetComponent<Ayaya::TransformComponent>();
        auto& rel = entity.GetComponent<Ayaya::RelationshipComponent>();
        
        glm::mat4 localTransform = transform.GetTransform();

        // 如果有父节点，递归获取父节点的世界矩阵，并与之相乘
        if (rel.Parent != entt::null) {
            Ayaya::Entity parentEntity{ rel.Parent, m_ActiveScene.get() };
            return GetWorldTransform(parentEntity) * localTransform;
        }
        return localTransform;
    }

    // ==========================================
    // 独立的相机逻辑封装
    // ==========================================
    void ProcessCameraInput(Ayaya::Timestep ts, Ayaya::TransformComponent& transform) {
        if (!m_ViewportFocused) return;

        if (Ayaya::Input::IsMouseButtonPressed(1)) {
            // 计算鼠标增量
            glm::vec2 currentMousePos = { Ayaya::Input::GetMouseX(), Ayaya::Input::GetMouseY() };
            glm::vec2 delta = (currentMousePos - m_InitialMousePos) * 0.2f;
            m_InitialMousePos = currentMousePos;

            // 处理旋转 (Pitch & Yaw)
            glm::vec3 rotationDegrees = glm::degrees(transform.Rotation);
            rotationDegrees.x -= delta.y;
            rotationDegrees.y -= delta.x;
            if (rotationDegrees.x > 89.0f) rotationDegrees.x = 89.0f;
            if (rotationDegrees.x < -89.0f) rotationDegrees.x = -89.0f;
            transform.Rotation = glm::radians(rotationDegrees);

            // 处理位移 (WASD QE)
            glm::quat orientation = glm::quat(transform.Rotation);
            glm::vec3 forward = glm::rotate(orientation, glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 right   = glm::rotate(orientation, glm::vec3(1.0f, 0.0f, 0.0f));
            float velocity = 5.0f * (float)ts;

            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::W)) transform.Translation += forward * velocity;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::S)) transform.Translation -= forward * velocity;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::A)) transform.Translation -= right * velocity;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::D)) transform.Translation += right * velocity;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::E)) transform.Translation.y += velocity;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::Q)) transform.Translation.y -= velocity;
        } else {
            // 未按住右键时也要更新初始坐标，防止视角瞬间跳跃
            m_InitialMousePos = { Ayaya::Input::GetMouseX(), Ayaya::Input::GetMouseY() };
        }
    }

private:
    Ayaya::ShaderLibrary m_ShaderLibrary; 
    std::shared_ptr<Ayaya::Texture2D> m_Texture;
    std::shared_ptr<Ayaya::VertexArray> m_VertexArray;

    std::shared_ptr<Ayaya::Framebuffer> m_Framebuffer; 
    glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         

    std::shared_ptr<Ayaya::Scene> m_ActiveScene;
    Ayaya::SceneHierarchyPanel m_SceneHierarchyPanel;

    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
    glm::vec2 m_InitialMousePos = { 0.0f, 0.0f };
};