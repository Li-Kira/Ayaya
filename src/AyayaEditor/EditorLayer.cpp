#include "EditorLayer.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Ayaya {

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        m_ShaderLibrary.Load("assets/shaders/default.vert", "assets/shaders/default.frag");
        m_ShaderLibrary.Load("assets/shaders/outline.vert", "assets/shaders/outline.frag");
        m_Texture = Texture2D::Create("assets/textures/bricks2.jpg");

        SetupGeometry();
        SetupScene();
    }

    void EditorLayer::OnUpdate(Timestep ts) {
        HandleShortcuts();

        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.14f, 1.0f });
        RenderCommand::Clear();
        glClear(GL_STENCIL_BUFFER_BIT); 

        glm::mat4 cameraViewMatrix, cameraProjectionMatrix;
        if (GetPrimaryCamera(cameraViewMatrix, cameraProjectionMatrix, ts)) {
            glm::mat4 cameraViewProj = cameraProjectionMatrix * cameraViewMatrix;
            RenderScene(cameraViewProj);
        }

        m_Framebuffer->Unbind();

        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace();
        UIRenderMenuBar();

        m_SceneHierarchyPanel.OnImGuiRender();

        UIRenderViewport();

        ImGui::End(); // End DockSpace
    }

    void EditorLayer::OnEvent(Event& event) {}

    void EditorLayer::SetupGeometry() {
        m_VertexArray.reset(VertexArray::Create());
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
        auto vbo = std::shared_ptr<VertexBuffer>(VertexBuffer::Create(vertices, sizeof(vertices)));
        vbo->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Color"    },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        m_VertexArray->AddVertexBuffer(vbo);
        uint32_t indices[] = { 0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7, 4, 0, 3, 3, 7, 4, 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4 };
        auto ibo = std::shared_ptr<IndexBuffer>(IndexBuffer::Create(indices, 36));
        m_VertexArray->SetIndexBuffer(ibo);
    }

    void EditorLayer::SetupScene() {
        m_ActiveScene = std::make_shared<Scene>();

        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        cameraEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 5.0f };

        Entity parentNode = m_ActiveScene->CreateEntity("Parent Empty Node");
        Entity square1 = m_ActiveScene->CreateEntity("Left Square");
        square1.GetComponent<TransformComponent>().Translation = { -1.5f, 0.0f, 0.0f };
        square1.AddComponent<SpriteRendererComponent>(glm::vec4{0.2f, 0.8f, 0.3f, 1.0f});

        Entity square2 = m_ActiveScene->CreateEntity("Right Square");
        square2.GetComponent<TransformComponent>().Translation = { 1.5f, 0.0f, 0.0f };
        square2.AddComponent<SpriteRendererComponent>(glm::vec4{0.8f, 0.2f, 0.3f, 1.0f});

        square1.SetParent(parentNode); 
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::HandleShortcuts() {
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (m_ViewportFocused && selectedEntity && !Input::IsMouseButtonPressed(1)) {
            if (Input::IsKeyPressed(Key::Q)) m_GizmoType = -1;
            if (Input::IsKeyPressed(Key::W)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (Input::IsKeyPressed(Key::E)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (Input::IsKeyPressed(Key::R)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }
    }

    bool EditorLayer::GetPrimaryCamera(glm::mat4& outView, glm::mat4& outProjection, Timestep ts) {
        auto cameraView = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
        for (auto entityID : cameraView) {
            Entity entity{ entityID, m_ActiveScene.get() };
            auto& camera = entity.GetComponent<CameraComponent>();
            auto& transform = entity.GetComponent<TransformComponent>();
            
            if (camera.Primary) {
                if (ts > 0.0f) ProcessCameraInput(ts, transform); 
                outProjection = camera.Camera.GetProjection();
                outView = glm::inverse(entity.GetWorldTransform());
                return true; 
            }
        }
        return false;
    }

    void EditorLayer::ProcessCameraInput(Timestep ts, TransformComponent& transform) {
        if (!m_ViewportFocused) return;

        if (Input::IsMouseButtonPressed(1)) {
            glm::vec2 currentMousePos = { Input::GetMouseX(), Input::GetMouseY() };
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

            if (Input::IsKeyPressed(Key::W)) transform.Translation += forward * velocity;
            if (Input::IsKeyPressed(Key::S)) transform.Translation -= forward * velocity;
            if (Input::IsKeyPressed(Key::A)) transform.Translation -= right * velocity;
            if (Input::IsKeyPressed(Key::D)) transform.Translation += right * velocity;
            if (Input::IsKeyPressed(Key::E)) transform.Translation.y += velocity;
            if (Input::IsKeyPressed(Key::Q)) transform.Translation.y -= velocity;
        } else {
            m_InitialMousePos = { Input::GetMouseX(), Input::GetMouseY() };
        }
    }

    void EditorLayer::RenderScene(const glm::mat4& cameraViewProj) {
        Renderer::BeginScene(cameraViewProj);
        
        auto defaultShader = m_ShaderLibrary.Get("default");
        m_Texture->Bind(0);
        defaultShader->Bind();
        defaultShader->SetInt("u_Texture", 0);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_REPLACE, GL_REPLACE);

        auto renderGroup = m_ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
        for (auto entityID : renderGroup) {
            Entity entity{ entityID, m_ActiveScene.get() };
            auto& sprite = entity.GetComponent<SpriteRendererComponent>();
            
            if (m_HoveredEntity && m_HoveredEntity == entity) {
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilMask(0xFF); 
            } else {
                glStencilFunc(GL_ALWAYS, 0, 0xFF);
                glStencilMask(0x00); 
            }

            defaultShader->SetFloat3("u_ColorModifier", glm::vec3(sprite.Color)); 
            Renderer::Submit(defaultShader, m_VertexArray, entity.GetWorldTransform());
        }

        if (m_HoveredEntity && m_HoveredEntity.HasComponent<SpriteRendererComponent>()) {
            glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            glStencilMask(0x00);      
            glDisable(GL_DEPTH_TEST); 

            auto outlineShader = m_ShaderLibrary.Get("outline");
            outlineShader->Bind();
            outlineShader->SetFloat3("u_Color", glm::vec3(1.0f, 0.65f, 0.0f)); 
            
            glm::mat4 transform = m_HoveredEntity.GetWorldTransform();
            transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(1.05f)); 
            
            Renderer::Submit(outlineShader, m_VertexArray, transform);

            glStencilMask(0xFF);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_STENCIL_TEST);
        }

        Renderer::EndScene();
    }

    void EditorLayer::UIRenderDockspace() {
        static bool dockspaceOpen = true;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Ayaya Editor DockSpace", &dockspaceOpen, window_flags);
        ImGui::PopStyleVar(3);

        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            static bool first_time = true;
            if (first_time) {
                first_time = false;
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
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
    }

    void EditorLayer::UIRenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) Application::Get().Close(); 
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }

    void EditorLayer::UIRenderViewport() {
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

                auto view = m_ActiveScene->Reg().view<CameraComponent>();
                for (auto entity : view) {
                    auto& cameraComp = view.get<CameraComponent>(entity);
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

        glm::mat4 cameraViewMatrix, cameraProjectionMatrix;
        if (GetPrimaryCamera(cameraViewMatrix, cameraProjectionMatrix)) {
            HandleMousePicking(cameraViewMatrix, cameraProjectionMatrix);
            HandleGizmo(cameraViewMatrix, cameraProjectionMatrix);
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix) {
        m_HoveredEntity = {}; 
        
        if (!m_ViewportHovered || ImGuizmo::IsOver()) return;

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
            glm::vec4 eyeCoords = glm::inverse(cameraProjectionMatrix) * clipCoords;
            eyeCoords = glm::vec4(eyeCoords.x, eyeCoords.y, -1.0f, 0.0f);
            glm::vec3 rayWorldDir = glm::normalize(glm::vec3(glm::inverse(cameraViewMatrix) * eyeCoords));
            glm::vec3 rayOrigin = glm::vec3(glm::inverse(cameraViewMatrix)[3]);

            float closestT = std::numeric_limits<float>::max();
            auto renderGroup = m_ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();

            for (auto entityID : renderGroup) {
                Entity entity{ entityID, m_ActiveScene.get() };
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

    void EditorLayer::HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix) {
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (!selectedEntity || m_GizmoType == -1) return;

        ImGuizmo::BeginFrame(); 
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();

        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, 
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x, 
                          m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        glm::mat4 transform = selectedEntity.GetWorldTransform();

        bool snap = Input::IsKeyPressed(Key::LeftControl);
        float snapValue = (m_GizmoType == ImGuizmo::OPERATION::ROTATE) ? 45.0f : 0.5f; 
        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(cameraViewMatrix), glm::value_ptr(cameraProjectionMatrix),
            (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
            nullptr, snap ? snapValues : nullptr);

        if (ImGuizmo::IsUsing()) {
            auto& rel = selectedEntity.GetComponent<RelationshipComponent>();
            glm::mat4 localTransform = transform;
            
            if (rel.Parent != entt::null) {
                Entity parent{ rel.Parent, m_ActiveScene.get() };
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