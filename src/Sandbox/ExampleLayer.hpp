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
#include <ImGuizmo.h>
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
        Ayaya::Entity parentNode = m_ActiveScene->CreateEntity("Parent Empty Node");
        
        Ayaya::Entity square1 = m_ActiveScene->CreateEntity("Left Square");
        square1.GetComponent<Ayaya::TransformComponent>().Translation = { -1.5f, 0.0f, 0.0f };
        square1.AddComponent<Ayaya::SpriteRendererComponent>(glm::vec4{0.2f, 0.8f, 0.3f, 1.0f});

        Ayaya::Entity square2 = m_ActiveScene->CreateEntity("Right Square");
        square2.GetComponent<Ayaya::TransformComponent>().Translation = { 1.5f, 0.0f, 0.0f };
        square2.AddComponent<Ayaya::SpriteRendererComponent>(glm::vec4{0.8f, 0.2f, 0.3f, 1.0f});

        square1.SetParent(parentNode); 
        
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        // ==========================================
        // 优化：仅当视口被聚焦、有物体被选中，且未按住右键漫游时，才响应快捷键
        // 这防止了在属性面板改名字时意外触发 QWER
        // ==========================================
        Ayaya::Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && !Ayaya::Input::IsMouseButtonPressed(1)) {
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::Q)) m_GizmoType = -1;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::W)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::E)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::R)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }

        // 准备渲染环境
        m_Framebuffer->Bind();
        Ayaya::RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        Ayaya::RenderCommand::Clear();

        glm::mat4 cameraViewProj = glm::mat4(1.0f);
        bool hasPrimaryCamera = false;

        // ECS 阶段 1：查询主相机
        auto cameraView = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::CameraComponent>();
        for (auto entityID : cameraView) {
            Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
            auto& camera = entity.GetComponent<Ayaya::CameraComponent>();
            auto& transform = entity.GetComponent<Ayaya::TransformComponent>();

            if (camera.Primary) {
                hasPrimaryCamera = true;
                ProcessCameraInput(ts, transform);
                glm::mat4 worldTransform = entity.GetWorldTransform();
                glm::mat4 view = glm::inverse(worldTransform);
                cameraViewProj = camera.Camera.GetProjection() * view;
                break; 
            }
        }

        // ECS 阶段 2：提交渲染
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
                if (ImGui::MenuItem("Exit")) { Ayaya::Application::Get().Close(); }
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

        // ==========================================
        // ImGuizmo 渲染与交互逻辑
        // ==========================================
        Ayaya::Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

        if (selectedEntity && m_GizmoType != -1) {
            // 修复 1：必须调用 BeginFrame，否则 ImGuizmo 无法读取鼠标点击事件！
            ImGuizmo::BeginFrame(); 
            
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            // 修复 2：剔除 Title Bar 和 Tab 栏带来的坐标偏移！
            ImVec2 windowPos = ImGui::GetWindowPos();
            ImVec2 minBound = ImGui::GetWindowContentRegionMin();
            ImGuizmo::SetRect(windowPos.x + minBound.x, windowPos.y + minBound.y, m_ViewportSize.x, m_ViewportSize.y);

            auto cameraView = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::CameraComponent>();
            glm::mat4 cameraProjection;
            glm::mat4 cameraViewMatrix;
            bool hasCamera = false;

            for (auto entityID : cameraView) {
                Ayaya::Entity cameraEntity{ entityID, m_ActiveScene.get() };
                auto& cameraComp = cameraEntity.GetComponent<Ayaya::CameraComponent>();
                if (cameraComp.Primary) {
                    cameraProjection = cameraComp.Camera.GetProjection();
                    cameraViewMatrix = glm::inverse(cameraEntity.GetWorldTransform());
                    hasCamera = true;
                    break;
                }
            }

            if (hasCamera) {
                auto& tc = selectedEntity.GetComponent<Ayaya::TransformComponent>();
                glm::mat4 transform = selectedEntity.GetWorldTransform();

                bool snap = Ayaya::Input::IsKeyPressed(Ayaya::Key::LeftControl);
                float snapValue = 0.5f; 
                if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) snapValue = 45.0f; 
                float snapValues[3] = { snapValue, snapValue, snapValue };

                ImGuizmo::Manipulate(glm::value_ptr(cameraViewMatrix), glm::value_ptr(cameraProjection),
                    (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
                    nullptr, snap ? snapValues : nullptr);

                if (ImGuizmo::IsUsing()) {
                    auto& rel = selectedEntity.GetComponent<Ayaya::RelationshipComponent>();
                    glm::mat4 localTransform = transform;
                    
                    if (rel.Parent != entt::null) {
                        Ayaya::Entity parent{ rel.Parent, m_ActiveScene.get() };
                        glm::mat4 parentWorld = parent.GetWorldTransform();
                        localTransform = glm::inverse(parentWorld) * transform;
                    }

                    glm::vec3 scale, translation, skew;
                    glm::quat rotation;
                    glm::vec4 perspective;
                    glm::decompose(localTransform, scale, rotation, translation, skew, perspective);
                    
                    tc.Translation = translation;
                    tc.Rotation = glm::eulerAngles(rotation);
                    tc.Scale = scale;
                }
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::End(); // End DockSpace
    }

    virtual void OnEvent(Ayaya::Event& event) override {}

private:
    void ProcessCameraInput(Ayaya::Timestep ts, Ayaya::TransformComponent& transform) {
        if (!m_ViewportFocused) return;

        if (Ayaya::Input::IsMouseButtonPressed(1)) {
            glm::vec2 currentMousePos = { Ayaya::Input::GetMouseX(), Ayaya::Input::GetMouseY() };
            glm::vec2 delta = (currentMousePos - m_InitialMousePos) * 0.2f;
            m_InitialMousePos = currentMousePos;

            glm::vec3 rotationDegrees = glm::degrees(transform.Rotation);
            rotationDegrees.x -= delta.y;
            rotationDegrees.y -= delta.x;
            if (rotationDegrees.x > 89.0f) rotationDegrees.x = 89.0f;
            if (rotationDegrees.x < -89.0f) rotationDegrees.x = -89.0f;
            transform.Rotation = glm::radians(rotationDegrees);

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

    int m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
};