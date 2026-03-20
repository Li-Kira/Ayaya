#include "EditorLayer.hpp"
#include "Renderer/Mesh.hpp"
#include "Events/MouseEvent.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <IconsFontAwesome5.h> 

namespace Ayaya {

    EditorLayer::EditorLayer() : Layer("EditorLayer") {}

    void EditorLayer::OnAttach() {
        // ==========================================
        // 启动时读取资产注册表
        // ==========================================
        AssetManager::DeserializeRegistry("assets/AssetRegistry.yaml");
        // 新增：初始化并加载编辑器偏好设置
        m_PreferencesPanel.Init();

        // ==========================================
        // 减负：去掉了之前在这里手动配置和创建 m_Framebuffer 的代码
        // 现在全权交由 SceneRenderer 在内部自己打理
        // ==========================================
        SceneRenderer::Init();

        // ==========================================
        // 新增：创建 Game 窗口专属的 FBO 画布
        // ==========================================
        FramebufferSpecification spec;
        spec.Width = 1280; spec.Height = 720;
        spec.Format = FramebufferFormat::RGBA8; 
        m_GameFBO = Framebuffer::Create(spec);
        SetupScene();
    }

    void EditorLayer::OnUpdate(Timestep ts) {
        HandleShortcuts();

        // ==========================================
        // 2.1 处理 Scene (上帝视口) 的 Resize
        // ==========================================
        static glm::vec2 s_LastViewportSize = { 0.0f, 0.0f };
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f && 
           (s_LastViewportSize.x != m_ViewportSize.x || s_LastViewportSize.y != m_ViewportSize.y)) {
            
            SceneRenderer::OnWindowResize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            // 删除在这里强行修改 CameraComponent 比例的代码，上帝视口不该干涉玩家相机！
            s_LastViewportSize = m_ViewportSize;
        }

        // ==========================================
        // 2.2 处理 Game (游戏视口) 的 Resize
        // ==========================================
        static glm::vec2 s_LastGameViewportSize = { 0.0f, 0.0f };
        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f && 
           (s_LastGameViewportSize.x != m_GameViewportSize.x || s_LastGameViewportSize.y != m_GameViewportSize.y)) {
            
            m_GameFBO->Resize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
            // 只有 Game 窗口改变，才改变玩家相机的长宽比！
            auto view = m_ActiveScene->Reg().view<CameraComponent>();
            for (auto entityID : view) {
                auto& cameraComp = view.get<CameraComponent>(entityID);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
                }
            }
            s_LastGameViewportSize = m_GameViewportSize;
        }

        // 上帝相机只在 Edit 模式响应输入
        if (m_SceneState == SceneState::Edit) {
            m_EditorCamera.OnUpdate(ts, m_ViewportFocused);
        }
        // 如果处于 Play 模式且没有暂停，则推进物理运算！
        // (完美兼容你之前写的加速/减速播放 m_TimeStepScale)
        // ==========================================
        else if (m_SceneState == SceneState::Play && !m_IsPaused) {
            m_ActiveScene->OnUpdateRuntime(ts * m_TimeStepScale);
        }

        // ==========================================
        // Pass 1: 永远渲染 Game 窗口 (无论处于 Edit 还是 Play 模式)
        // ==========================================
        bool hasValidCamera = false;
        glm::mat4 cameraViewMatrix, cameraProjectionMatrix;
        glm::vec3 cameraPosition;
        
        // 用于接收当前玩家相机的环境配置
        bool renderSkybox = m_ShowSkybox; 
        glm::vec4 clearColor = { 0.12f, 0.12f, 0.14f, 1.0f }; 
        
        auto view = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
        for (auto entityID : view) {
            auto [transform, cameraComp] = view.get<TransformComponent, CameraComponent>(entityID);
            if (cameraComp.Primary) {
                Entity cameraEntity{ entityID, m_ActiveScene.get() };
                glm::mat4 worldTransform = cameraEntity.GetWorldTransform();
                
                glm::vec3 scale, translation, skew;
                glm::quat rotation;
                glm::vec4 perspective;
                glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);
                
                cameraPosition = translation;
                glm::mat4 unscaledTransform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation);
                cameraViewMatrix = glm::inverse(unscaledTransform); 
                
                // ==========================================
                // 核心修复 2：在获取投影矩阵前，强制矫正当前主相机的长宽比！
                // 这样新建的相机立刻就能拥有完美的 Game 窗口比例
                // ==========================================
                if (!cameraComp.FixedAspectRatio && m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f) {
                    cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
                }

                cameraProjectionMatrix = cameraComp.Camera.GetProjection();
                
                renderSkybox = (cameraComp.ClearFlag == CameraComponent::ClearFlags::Skybox);
                clearColor = cameraComp.BackgroundColor;

                hasValidCamera = true;
                break; 
            }
        }

        if (hasValidCamera) {
            SceneRenderer::BeginScene(cameraViewMatrix, cameraProjectionMatrix, cameraPosition);
            // 传入刚才提取出来的 skybox 和 clearColor
            SceneRenderer::RenderScene(m_ActiveScene, {}, false, renderSkybox, clearColor);
            SceneRenderer::EndScene();

            m_GameStats = SceneRenderer::GetStats();

            glBindFramebuffer(GL_READ_FRAMEBUFFER, SceneRenderer::GetPostProcessFBORendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_GameFBO->GetRendererID());
            glBlitFramebuffer(0, 0, (GLint)m_ViewportSize.x, (GLint)m_ViewportSize.y,
                              0, 0, (GLint)m_GameViewportSize.x, (GLint)m_GameViewportSize.y,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0); 
        } else {
            // 没有主相机时，Game 窗口呈现黑屏待机
            memset(&m_GameStats, 0, sizeof(SceneRenderer::Statistics));

            m_GameFBO->Bind();
            RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
            RenderCommand::Clear();
            m_GameFBO->Unbind();
        }

        // ==========================================
        // Pass 2: 永远渲染 Scene 窗口 (上帝视角)
        // ==========================================
        SceneRenderer::BeginScene(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection(), m_EditorCamera.GetPosition());
        SceneRenderer::RenderScene(m_ActiveScene, m_HoveredEntity, m_ShowGrid, m_ShowSkybox);
        SceneRenderer::EndScene();
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace();
        UIRenderMenuBar();
        UIRenderToolbar();

        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel.OnImGuiRender();
        m_PreferencesPanel.OnImGuiRender();

        UIRenderViewport();
        UIRenderGameViewport();

        ImGui::End(); // End DockSpace
    }

    void EditorLayer::OnEvent(Event& event) {}

    void EditorLayer::SetupScene() {
        m_ActiveScene = std::make_shared<Scene>();
        m_EditorScene = m_ActiveScene;

        // 创造摄像机
        Entity cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
        cameraComp.Camera.SetViewportSize(1280, 720);
        auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();
        cameraTransform.Translation = { -2.114f, 0.866f, 2.953f };
        cameraTransform.Rotation = glm::radians(glm::vec3(-12.907f, -35.081f, 0.0f));

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
        m_EditorScene = m_ActiveScene;

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
                m_EditorScene = m_ActiveScene;
                
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

            // 按下 F 键聚焦到选中物体 (Focus)
            static bool s_F_Pressed = false;
            if (Input::IsKeyPressed(Key::F)) {
                if (!s_F_Pressed) {
                    glm::mat4 transform = selectedEntity.GetWorldTransform();
                    glm::vec3 targetPos = glm::vec3(transform[3]); 
                    
                    // 只需要设置焦点和距离，相机内部会自动算出自己的新 Position！
                    m_EditorCamera.SetFocalPoint(targetPos);
                    m_EditorCamera.SetDistance(5.0f);
                    m_EditorCamera.UpdateCameraView(); 
                    
                    AYAYA_CORE_INFO("Camera focused on entity.");
                }
                s_F_Pressed = true;
            } else {
                s_F_Pressed = false;
            }
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

                // ==========================================
                // 核心魔法 1：在最上方切出约 6% 的高度作为全局工具栏！
                // ==========================================
                ImGuiID dock_id_top = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.035f, nullptr, &dock_main_id);

                ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
                ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.5f, nullptr, &dock_id_right);
                ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.30f, NULL, &dock_main_id);
                ImGuiID dock_id_game = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.5f, nullptr, &dock_main_id);

                // ==========================================
                // 核心魔法 2：把 ##Toolbar 强行停靠在这个顶部节点！
                // ==========================================
                ImGui::DockBuilderDockWindow("##Toolbar", dock_id_top);
                
                ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
                ImGui::DockBuilderDockWindow("Game", dock_main_id);
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

            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Preferences")) {
                    m_PreferencesPanel.SetOpen(true); // 打开偏好设置面板
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Grid", nullptr, &m_ShowGrid);
                ImGui::MenuItem("Show Skybox", nullptr, &m_ShowSkybox);
                ImGui::MenuItem("Show Statistics", nullptr, &m_ShowStatsPanel);

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

        if (m_ViewportHovered) {
            float scrollOffset = ImGui::GetIO().MouseWheel;
            if (scrollOffset != 0.0f) {
                // 生成一个虚拟的滚轮事件，强行塞给 EditorCamera
                MouseScrolledEvent e(0.0f, scrollOffset);
                m_EditorCamera.OnEvent(e);
            }
        }

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

    void EditorLayer::UIRenderGameViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Game");

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_GameViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

        // 记录画图前的光标起始位置，这是悬浮层定位的锚点！
        ImVec2 cursorStartPos = ImGui::GetCursorPos();

        // 渲染底层的游戏画面
        uint32_t textureID = m_GameFBO->GetColorAttachmentRendererID();
        ImGui::Image(reinterpret_cast<void*>((intptr_t)textureID), 
                     ImVec2{ m_GameViewportSize.x, m_GameViewportSize.y }, 
                     ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        // ==========================================
        // 核心魔法：Unity 风格的 Game 窗口内置 Stats 悬浮层 (完美动态适配版)
        // ==========================================
        if (m_ShowStatsPanel) {
            // 1. 使用 static 保存上一帧算出的完美尺寸，实现 0 延迟感的自适应外框
            static ImVec2 s_OverlaySize = ImVec2(375.0f, 340.0f); 
            
            // 2. 定位时使用这个动态尺寸
            ImGui::SetCursorPos(ImVec2(cursorStartPos.x + m_GameViewportSize.x - s_OverlaySize.x - 10.0f, cursorStartPos.y + 10.0f));

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
            
            // 3. 开启 Child，传入动态的外框尺寸
            ImGui::BeginChild("StatsOverlay", s_OverlaySize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            
            ImGui::SetCursorPosY(10.0f); 
            ImGui::Indent(10.0f);        

            auto& io = ImGui::GetIO();

            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Graphics");
            ImGui::Separator();
            ImGui::Text("%.1f FPS (%.1f ms)", io.Framerate, 1000.0f / io.Framerate);
            ImGui::Text("Screen: %dx%d", (int)m_GameViewportSize.x, (int)m_GameViewportSize.y);
            ImGui::Spacing();
            
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Rendering");
            ImGui::Separator();
            ImGui::Text("Batches: %d", m_GameStats.DrawCalls);
            ImGui::Text("SetPass calls: %d", m_GameStats.ShaderBinds);
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Geometry & Scene");
            ImGui::Separator();
            
            ImGui::Text("Tris: %d", m_GameStats.TriangleCount); 
            ImGui::SameLine(0.0f, 15.0f); 
            ImGui::Text("Verts: %d", m_GameStats.VertexCount);

            if (m_ActiveScene) {
                size_t entityCount = 0;
                auto view = m_ActiveScene->Reg().view<IDComponent>();
                for (auto e : view) entityCount++;
                ImGui::Text("Active Entities: %zu", entityCount);
            }

            ImGui::Unindent(10.0f); 

            // ==========================================
            // 终极动态适配魔法：在结束绘制前，向 ImGui 索要完美的宽高！
            // ==========================================
            
            // [宽度动态]: 根据当前系统字体的行高，等比缩放你觉得最舒服的 375.0f
            // (假设你在 Windows 上觉得刚好时，标准行高是 16.0f)
            float currentLineHeight = ImGui::GetTextLineHeight(); 
            s_OverlaySize.x = 375.0f * (currentLineHeight / 16.0f);
            
            // [高度动态]: 直接获取当前画笔(Cursor)的 Y 坐标，再加上 10 像素作为底部边距！
            s_OverlaySize.y = ImGui::GetCursorPosY() + 10.0f; 

            ImGui::EndChild();
            
            ImGui::PopStyleVar(); 
            ImGui::PopStyleColor();
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

    void EditorLayer::OnScenePlay() {
        m_SceneState = SceneState::Play;
        m_IsPaused = false;
        m_TimeStepScale = 1.0f;

        // 1. 克隆并覆盖当前运行场景
        m_ActiveScene = std::make_shared<Scene>();
        SceneSerializer serializer(m_EditorScene);
        EditorState dummyState;
        std::string tempPath = "assets/Editor/temp/temp_play_scene.ayaya";
        serializer.Serialize(tempPath, dummyState);
        SceneSerializer deserializer(m_ActiveScene);
        deserializer.Deserialize(tempPath, dummyState);

        // 2. 更新面板上下文
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        // ==========================================
        // 核心修复 3：强行重置克隆出来的玩家相机的视口比例和模式！
        // ==========================================
        auto view = m_ActiveScene->Reg().view<CameraComponent>();
        for (auto entityID : view) {
            auto& cameraComp = view.get<CameraComponent>(entityID);
            
            // 弥补序列化漏洞，拨回透视相机模式，防止画面缩小成一个点
            cameraComp.Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);

            if (!cameraComp.FixedAspectRatio) {
                // 解开这里的注释！让 Game 窗口的尺寸赋给新相机
                cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
            }
        }

        // ==========================================
        // 4. 开启物理效果计算
        // ==========================================
        m_ActiveScene->OnPhysics2DStart();
    }

    void EditorLayer::OnSceneStop() {
        m_SceneState = SceneState::Edit;
        // --- 新增：停止游戏也重置状态 ---
        m_IsPaused = false;
        m_TimeStepScale = 1.0f;

        // ==========================================
        // 停止并销毁物理世界，释放内存！
        // ==========================================
        if (m_ActiveScene) {
            m_ActiveScene->OnPhysics2DStop();
        }

        // 恢复编辑状态的场景
        m_ActiveScene = m_EditorScene;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);
    }

    void EditorLayer::UIRenderToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f)); 
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        
        auto& colors = ImGui::GetStyle().Colors;
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(colors[ImGuiCol_ButtonHovered].x, colors[ImGuiCol_ButtonHovered].y, colors[ImGuiCol_ButtonHovered].z, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(colors[ImGuiCol_ButtonActive].x, colors[ImGuiCol_ButtonActive].y, colors[ImGuiCol_ButtonActive].z, 0.5f));

        ImGuiWindowClass window_class;
        window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResize;
        ImGui::SetNextWindowClass(&window_class);

        ImGui::Begin("##Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        // ==========================================
        // 核心修改：固定大小并居中整个按钮组
        // ==========================================
        float size = ImGui::GetWindowHeight() - 4.0f;
        float speedBtnWidth = size + 28.0f; // 加速按钮带文字，所以宽一点
        float spacing = 8.0f;               // 按钮之间的间距
        
        // 计算 3 个按钮加起来的总宽度，用于居中
        float totalWidth = size + size + speedBtnWidth + (spacing * 2.0f);

        // 水平和垂直完美居中算法
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (totalWidth * 0.5f));
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - size) * 0.5f);
        
        // ==========================================
        // 1. 播放/停止按钮
        // ==========================================
        bool isPlayMode = (m_SceneState == SceneState::Play);
        std::string playIcon = isPlayMode ? ICON_FA_STOP : ICON_FA_PLAY;
        
        if (isPlayMode) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // 运行中变为醒目的绿色
        if (ImGui::Button(playIcon.c_str(), ImVec2(size, size))) {
            if (m_SceneState == SceneState::Edit) OnScenePlay();
            else OnSceneStop();
        }
        if (isPlayMode) ImGui::PopStyleColor();

        ImGui::SameLine(0, spacing);

        // ==========================================
        // 2. 暂停按钮
        // ==========================================
        // 如果处于暂停状态，给按钮加一个深色背景高亮，像按下去了一样
        if (m_IsPaused) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
        if (ImGui::Button(ICON_FA_PAUSE, ImVec2(size, size))) {
            // 只有在 Play 模式下才允许点击暂停
            if (m_SceneState == SceneState::Play) m_IsPaused = !m_IsPaused; 
        }
        if (m_IsPaused) ImGui::PopStyleColor();

        ImGui::SameLine(0, spacing);

        // ==========================================
        // 3. 加速按钮 (点击循环切换倍速: 1x -> 2x -> 4x -> 1x)
        // ==========================================
        std::string speedText = std::string(ICON_FA_FORWARD) + " " + std::to_string((int)m_TimeStepScale) + "x";
        if (ImGui::Button(speedText.c_str(), ImVec2(speedBtnWidth, size))) {
            m_TimeStepScale *= 2.0f;
            if (m_TimeStepScale > 4.0f) m_TimeStepScale = 1.0f; 
        }

        ImGui::PopStyleColor(4); 
        ImGui::PopStyleVar(2);
        ImGui::End();
    }
}