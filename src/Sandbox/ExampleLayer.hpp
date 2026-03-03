#pragma once

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>
#include <Events/ApplicationEvent.hpp>
#include <Events/KeyEvent.hpp>
#include <Events/MouseEvent.hpp>

#include <glad/glad.h>

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
        Ayaya::FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Ayaya::Framebuffer::Create(fbSpec);

        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        m_ShaderLibrary.Load("assets/shaders/outline.vert", "assets/shaders/outline.frag");

        m_Texture = Ayaya::Texture2D::Create("assets/textures/bricks2.jpg");

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
        uint32_t indices[] = { 0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4 };
        auto ibo = std::shared_ptr<Ayaya::IndexBuffer>(Ayaya::IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);

        m_ActiveScene = std::make_shared<Ayaya::Scene>();

        Ayaya::Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<Ayaya::CameraComponent>();
        cameraComp.Camera.SetProjectionType(Ayaya::SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        cameraEntity.GetComponent<Ayaya::TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };

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
        Ayaya::Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (m_ViewportFocused && selectedEntity && !Ayaya::Input::IsMouseButtonPressed(1)) {
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::Q)) m_GizmoType = -1;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::W)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::E)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (Ayaya::Input::IsKeyPressed(Ayaya::Key::R)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }

        m_Framebuffer->Bind();
        Ayaya::RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        Ayaya::RenderCommand::Clear();
        glClear(GL_STENCIL_BUFFER_BIT); 

        glm::mat4 cameraViewProj = glm::mat4(1.0f);
        bool hasPrimaryCamera = false;

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

        if (hasPrimaryCamera) {
            Ayaya::Renderer::BeginScene(cameraViewProj);
            
            // ==========================================
            // Pass 1: 正常渲染，获取完美 2D 遮罩
            // ==========================================
            auto defaultShader = m_ShaderLibrary.Get("default");
            m_Texture->Bind(0);
            defaultShader->Bind();
            defaultShader->SetInt("u_Texture", 0);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_STENCIL_TEST);
            
            // 【核心修复】：将第二个参数改为 GL_REPLACE！
            // 无论物体是否被挡住（深度测试失败），都强行将它的影子刻印在模板缓冲里。
            glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

            auto renderGroup = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::SpriteRendererComponent>();
            for (auto entityID : renderGroup) {
                Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
                auto& sprite = entity.GetComponent<Ayaya::SpriteRendererComponent>();
                
                if (m_HoveredEntity && m_HoveredEntity == entity) {
                    glStencilFunc(GL_ALWAYS, 1, 0xFF);
                    glStencilMask(0xFF); 
                } else {
                    glStencilFunc(GL_ALWAYS, 0, 0xFF);
                    glStencilMask(0x00); 
                }

                defaultShader->SetFloat3("u_ColorModifier", glm::vec3(sprite.Color)); 
                Ayaya::Renderer::Submit(defaultShader, m_VertexArray, entity.GetWorldTransform());
            }

            // ==========================================
            // Pass 2: 轮廓描边 (X-Ray 效果)
            // ==========================================
            if (m_HoveredEntity && m_HoveredEntity.HasComponent<Ayaya::SpriteRendererComponent>()) {
                glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
                glStencilMask(0x00);      
                glDisable(GL_DEPTH_TEST); 

                auto outlineShader = m_ShaderLibrary.Get("outline");
                outlineShader->Bind();
                outlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f)); 
                
                glm::mat4 transform = m_HoveredEntity.GetWorldTransform();
                transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.05f)); 
                
                Ayaya::Renderer::Submit(outlineShader, m_VertexArray, transform);

                glStencilMask(0xFF);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glEnable(GL_DEPTH_TEST);
                glDisable(GL_STENCIL_TEST);
            }

            Ayaya::Renderer::EndScene();
        }

        m_Framebuffer->Unbind();

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

        m_SceneHierarchyPanel.OnImGuiRender();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Viewport");

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

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

        Ayaya::Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

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

        m_HoveredEntity = {}; 
        
        if (hasCamera && m_ViewportHovered && !ImGuizmo::IsOver()) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float mx = mousePos.x - m_ViewportBounds[0].x;
            float my = mousePos.y - m_ViewportBounds[0].y;
            float viewportWidth = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
            float viewportHeight = m_ViewportBounds[1].y - m_ViewportBounds[0].y;

            if (mx >= 0 && mx <= viewportWidth && my >= 0 && my <= viewportHeight) {
                my = viewportHeight - my; 
                float nx = (mx / viewportWidth) * 2.0f - 1.0f;
                float ny = (my / viewportHeight) * 2.0f - 1.0f;

                glm::vec4 clipCoords = glm::vec4(nx, ny, -1.0f, 1.0f);
                glm::vec4 eyeCoords = glm::inverse(cameraProjection) * clipCoords;
                eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
                glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(cameraViewMatrix) * eyeCoords));
                glm::vec3 rayOrigin = glm::vec3(glm::inverse(cameraViewMatrix)[3]);

                float closestT = std::numeric_limits<float>::max();
                auto renderGroup = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::SpriteRendererComponent>();

                for (auto entityID : renderGroup) {
                    Ayaya::Entity entity{ entityID, m_ActiveScene.get() };
                    glm::mat4 inverseTransform = glm::inverse(entity.GetWorldTransform());

                    glm::vec3 localRayOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
                    glm::vec3 localRayDir = glm::normalize(glm::vec3(inverseTransform * glm::vec4(rayWorldDir, 0.0f)));

                    glm::vec3 invDir = 1.0f / localRayDir;
                    glm::vec3 t0 = (-0.5f - localRayOrigin) * invDir; 
                    glm::vec3 t1 = (0.5f - localRayOrigin) * invDir;

                    glm::vec3 tmin = glm::min(t0, t1);
                    glm::vec3 tmax = glm::max(t0, t1);

                    float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
                    float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

                    if (tNear <= tFar && tFar >= 0.0f) {
                        if (tNear < closestT) {
                            closestT = tNear;
                            m_HoveredEntity = entity;
                        }
                    }
                }

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
                }
            }
        }

        if (selectedEntity && m_GizmoType != -1 && hasCamera) {
            ImGuizmo::BeginFrame(); 
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, 
                              m_ViewportBounds[1].x - m_ViewportBounds[0].x, 
                              m_ViewportBounds[1].y - m_ViewportBounds[0].y);

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

        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::End(); 
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
    ImVec2 m_ViewportBounds[2]; 

    std::shared_ptr<Ayaya::Scene> m_ActiveScene;
    Ayaya::SceneHierarchyPanel m_SceneHierarchyPanel;

    bool m_ViewportFocused = false;
    bool m_ViewportHovered = false;
    glm::vec2 m_InitialMousePos = { 0.0f, 0.0f };

    int m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
    Ayaya::Entity m_HoveredEntity = {}; 
};