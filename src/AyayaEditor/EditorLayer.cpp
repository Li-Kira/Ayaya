#include "EditorLayer.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/SceneRenderer.hpp"

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
        // ==========================================
        // 启动时读取资产注册表
        // ==========================================
        AssetManager::DeserializeRegistry("assets/AssetRegistry.yaml");

        // ==========================================
        // 减负：去掉了之前在这里手动配置和创建 m_Framebuffer 的代码
        // 现在全权交由 SceneRenderer 在内部自己打理
        // ==========================================
        SceneRenderer::Init();
        SetupScene();
    }

    void EditorLayer::OnUpdate(Timestep ts) {
        
        // ==========================================
        // 1.处理输入
        // ==========================================
        HandleShortcuts();

        // ==========================================
        // 2.处理相机和视口 Resize
        // ==========================================
        static glm::vec2 s_LastViewportSize = { 0.0f, 0.0f };
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && 
           (s_LastViewportSize.x != m_ViewportSize.x || s_LastViewportSize.y != m_ViewportSize.y)) {
            
            // 【核心修改】：通知管线视口大小变了，让它自己去重置它内部的 HDR 和 LDR 缓冲
            SceneRenderer::OnWindowResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            
            auto view = m_ActiveScene->Reg().view<CameraComponent>();
            for (auto entityID : view) {
                auto& cameraComp = view.get<CameraComponent>(entityID);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                }
            }
            s_LastViewportSize = m_ViewportSize;
        }

        m_EditorCamera.OnUpdate(ts, m_ViewportFocused);

        // ==========================================
        // 3. 渲染管线调用
        // 减负：外面不再需要手动 Bind 和 Unbind 画布，也不需要在这 Clear 背景了！
        // SceneRenderer::BeginScene 内部会搞定一切
        // ==========================================
        SceneRenderer::BeginScene(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection(), m_EditorCamera.GetPosition());
        SceneRenderer::RenderScene(m_ActiveScene, m_HoveredEntity, m_ShowGrid, m_ShowSkybox);
        SceneRenderer::EndScene();

        // 仅在最外层的默认窗口（不是视口内）刷一层深色底，防止 ImGui 窗口外露白
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
        RenderCommand::Clear();
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace();
        UIRenderMenuBar();

        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel.OnImGuiRender();

        UIRenderViewport();

        ImGui::End(); // End DockSpace
    }

    void EditorLayer::OnEvent(Event& event) {}

    void EditorLayer::SetupScene() {
        m_ActiveScene = std::make_shared<Scene>();

        // 创造摄像机
        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Translation = { 0.0f, 0.0f, 5.0f };

        // 创造太阳光
        Entity dirLight = m_ActiveScene->CreateEntity("Directional Light");
        auto& lightTransform = dirLight.GetComponent<TransformComponent>();
        lightTransform.Rotation = glm::radians(glm::vec3(-45.0f, 45.0f, 0.0f));
        dirLight.AddComponent<DirectionalLightComponent>();
        dirLight.GetComponent<DirectionalLightComponent>().AmbientStrength = 1500.0f;

        
        Entity cubeEntity = m_ActiveScene->CreateEntity("Cube");
        cubeEntity.GetComponent<TransformComponent>().Scale = { 1.0f, 1.0f, 1.0f };
        cubeEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
        auto& mrc = cubeEntity.AddComponent<MeshRendererComponent>(); 

        auto DefaultMat = std::make_shared<Material>();
        bool success = MaterialSerializer::Deserialize(DefaultMat, "assets/Editor/materials/DefaultPBR.mat");

        if (success) {
            // 给物体分配一个克隆体！
            mrc.MaterialAsset = DefaultMat->Clone();
        } else {
            AYAYA_CORE_WARN("Failed to load DefaultPBR.mat!");
            // 如果连母材质都没找到，只能给一个空材质，管线会自动走 Fallback(品红色)
            mrc.MaterialAsset = std::make_shared<Material>(); 
        }

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    // =====================================================================
    // 智能保存逻辑
    // =====================================================================
    void EditorLayer::SaveScene() {
        // 如果当前路径不为空，直接静默保存覆写
        if (!m_CurrentScenePath.empty()) {
            SceneSerializer serializer(m_ActiveScene);
            
            // ==========================================
            // 核心修复：同样在这里收集并传入 EditorState
            // ==========================================
            EditorState state;
            state.ShowGrid = m_ShowGrid;
            state.ShowSkybox = m_ShowSkybox;
            state.EnableMSAA = m_EnableMSAA;
            state.CameraPosition = m_EditorCamera.GetPosition();
            state.CameraDistance = m_EditorCamera.GetDistance();
            state.CameraPitch = m_EditorCamera.GetPitch();
            state.CameraYaw = m_EditorCamera.GetYaw();
            state.CameraFocalPoint = m_EditorCamera.GetFocalPoint();

            serializer.Serialize(m_CurrentScenePath, state); // 传入两个参数！
            
            AYAYA_CORE_INFO("Scene strictly saved to {0}", m_CurrentScenePath);
        } 
        // 否则（这是一个新建的未保存场景），转为“另存为”逻辑
        else {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveSceneAs() {
        std::string defaultName = "Untitled.ayaya";
        
        // 如果当前场景已经有路径，提取它的文件名作为默认名字
        if (!m_CurrentScenePath.empty()) {
            size_t pos = m_CurrentScenePath.find_last_of("/\\");
            defaultName = pos != std::string::npos ? m_CurrentScenePath.substr(pos + 1) : m_CurrentScenePath;
        }

        // 呼出带默认名字的原生保存弹窗
        std::string filepath = FileDialogs::SaveFile("ayaya", defaultName);
        
        if (!filepath.empty()) { 
            SceneSerializer serializer(m_ActiveScene);
            // 收集当前状态
            EditorState state;
            state.ShowGrid = m_ShowGrid;
            state.ShowSkybox = m_ShowSkybox;
            state.EnableMSAA = m_EnableMSAA;
            state.CameraPosition = m_EditorCamera.GetPosition();
            state.CameraDistance = m_EditorCamera.GetDistance();
            state.CameraPitch = m_EditorCamera.GetPitch();
            state.CameraYaw = m_EditorCamera.GetYaw();
            state.CameraFocalPoint = m_EditorCamera.GetFocalPoint();

            // 传入状态保存
            serializer.Serialize(filepath, state);

            m_CurrentScenePath = filepath; // 更新当前工作路径
            AYAYA_CORE_INFO("Scene saved as to {0}", filepath);
        }
    }

    void EditorLayer::NewScene() {
        m_ActiveScene = std::make_shared<Scene>();

        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
            cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
        }
        
        auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Translation = { 0.0f, 0.0f, 5.0f };

        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
        m_CurrentScenePath = std::string(); 
        m_HoveredEntity = {};
        m_SceneHierarchyPanel.SetSelectedEntity({});
        AYAYA_CORE_INFO("Created a new empty scene with default camera.");
    }

    void EditorLayer::OpenScene() {
        std::string filepath = FileDialogs::OpenFile("ayaya"); 
        if (!filepath.empty()) { 
            std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
            SceneSerializer serializer(newScene);
            EditorState state;
            if (serializer.Deserialize(filepath, state)) {
                m_ActiveScene = newScene;
                
                m_ShowGrid = state.ShowGrid;
                m_ShowSkybox = state.ShowSkybox;
                m_EditorCamera.SetPosition(state.CameraPosition);
                m_EditorCamera.SetDistance(state.CameraDistance);
                m_EditorCamera.SetPitch(state.CameraPitch);
                m_EditorCamera.SetYaw(state.CameraYaw);
                m_EditorCamera.SetFocalPoint(state.CameraFocalPoint);
                m_EditorCamera.UpdateCameraView();
                
                // 【核心修改】：通知管线修改 MSAA 采样率
                if (m_EnableMSAA != state.EnableMSAA) {
                    m_EnableMSAA = state.EnableMSAA;
                    SceneRenderer::SetMSAASamples(m_EnableMSAA ? 4 : 1);
                }
                
                auto view = m_ActiveScene->Reg().view<CameraComponent>();
                for (auto entityID : view) {
                    auto& cameraComp = view.get<CameraComponent>(entityID);
                    cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
                    if (!cameraComp.FixedAspectRatio) {
                        cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                    }
                }

                m_SceneHierarchyPanel.SetContext(m_ActiveScene);
                m_CurrentScenePath = filepath;
                m_HoveredEntity = {};
                m_SceneHierarchyPanel.SetSelectedEntity({});
                AYAYA_CORE_INFO("Scene loaded successfully from {0}!", filepath);
            }
        }
    }

    void EditorLayer::HandleShortcuts() {
        // =====================================
        // 1. 视口焦点相关的快捷键 (Gizmo 等)
        // =====================================
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        if (selectedEntity && !Input::IsMouseButtonPressed(1)) {
            if (Input::IsKeyPressed(Key::Q)) m_GizmoType = -1;
            if (Input::IsKeyPressed(Key::W)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (Input::IsKeyPressed(Key::E)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (Input::IsKeyPressed(Key::R)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }

        // =====================================
        // 2. 全局场景快捷键 (绕过 ImGui 事件拦截)
        // =====================================
        bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl) ||
                       Input::IsKeyPressed(Key::LeftSuper)   || Input::IsKeyPressed(Key::RightSuper);
        bool shift   = Input::IsKeyPressed(Key::LeftShift)   || Input::IsKeyPressed(Key::RightShift);

        // 使用静态变量记录上一帧的按键状态，实现“按下瞬间”单次触发 (Edge Detection)
        static bool s_N_Pressed = false;
        static bool s_O_Pressed = false;
        static bool s_S_Pressed = false;

        // --- New Scene (Cmd + N) ---
        if (Input::IsKeyPressed(Key::N)) {
            if (!s_N_Pressed && control) {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: New Scene");
                NewScene();
            }
            s_N_Pressed = true; // 锁定，只要不松手就不会再次触发
        } else {
            s_N_Pressed = false; // 松手后解锁
        }

        // --- Open Scene (Cmd + O) ---
        if (Input::IsKeyPressed(Key::O)) {
            if (!s_O_Pressed && control) {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: Open Scene");
                OpenScene();
            }
            s_O_Pressed = true;
        } else {
            s_O_Pressed = false;
        }

        // --- Save / Save As (Cmd + S / Cmd + Shift + S) ---
        if (Input::IsKeyPressed(Key::S)) {
            if (!s_S_Pressed && control) {
                if (shift) {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene As...");
                    SaveSceneAs();
                } else {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene");
                    SaveScene();
                }
            }
            s_S_Pressed = true;
        } else {
            s_S_Pressed = false;
        }
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
                ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, NULL, &dock_main_id);

                ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
                ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_id_right);
                ImGui::DockBuilderDockWindow("Properties", dock_id_right_bottom);
                ImGui::DockBuilderDockWindow("Content Browser", dock_id_bottom);
                
                ImGui::DockBuilderFinish(dockspace_id);
            }
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
    }

    void EditorLayer::UIRenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) SaveSceneAs();
                if (ImGui::MenuItem("Load Scene", "Ctrl+O")) OpenScene();

                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) Application::Get().Close(); 
                
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
                ImGui::MenuItem("Show Skybox", nullptr, &m_ShowSkybox);

                if (ImGui::MenuItem("Enable MSAA (4x)", nullptr, &m_EnableMSAA)) {
                    // 【核心修改】：优雅的一行调用！再也不用在 UI 层操作底层缓冲了
                    SceneRenderer::SetMSAASamples(m_EnableMSAA ? 4 : 1);
                    AYAYA_CORE_INFO("MSAA state changed: {0}", m_EnableMSAA ? "Enabled (4x)" : "Disabled");
                }

                ImGui::EndMenu();
            }

            // 右侧状态文本...
            std::string sceneName = "Untitled";
            if (!m_CurrentScenePath.empty()) {
                size_t pos = m_CurrentScenePath.find_last_of("/\\");
                sceneName = pos != std::string::npos ? m_CurrentScenePath.substr(pos + 1) : m_CurrentScenePath;
            }
            std::string displayTitle = "Scene: " + sceneName;
            float textWidth = ImGui::CalcTextSize(displayTitle.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - textWidth - 20.0f);
            ImGui::TextDisabled("%s", displayTitle.c_str());

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
        m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        // ==========================================
        // 核心修改：向渲染管线索要处理完毕的后期画面
        // ==========================================
        uint32_t textureID = SceneRenderer::GetFinalColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((intptr_t)textureID), 
                     ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                     ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        HandleMousePicking(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
        HandleGizmo(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());

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
            // ==========================================
            // 核心修复：把 SpriteRendererComponent 换成 MeshRendererComponent！
            // ==========================================
            auto renderGroup = m_ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();

            for (auto entityID : renderGroup) {
                Entity entity{ entityID, m_ActiveScene.get() };
                // ==========================================
                // 核心修复：射线检测直接无视被隐藏的物体，直接穿透过去！
                // ==========================================
                if (!entity.IsActiveInHierarchy()) continue;

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