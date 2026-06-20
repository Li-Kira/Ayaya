#include "EditorLayer.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/AssetPreviewer.hpp"
#include "Events/MouseEvent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Engine/Animation/AnimationSystem.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "Engine/Core/ImGuiBackend.hpp"
#include "Engine/Core/Application.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"

#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <stb_image_write.h>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <IconsFontAwesome5.h> 

namespace Ayaya {

    EditorLayer* EditorLayer::s_Instance = nullptr;

    EditorLayer::EditorLayer() : Layer("EditorLayer") {
        s_Instance = this;
        std::string docsDir = std::filesystem::path(
            getenv("HOME") ? getenv("HOME") : getenv("USERPROFILE") ? getenv("USERPROFILE") : "."
        ).append("Documents/AyayaProjects").string();
        strncpy(m_NewProjectLocation, docsDir.c_str(), sizeof(m_NewProjectLocation) - 1);
    }

    EditorLayer& EditorLayer::Get() {
        return *s_Instance;
    }

    void EditorLayer::OnAttach() {
        // engine:// mount 必须最先执行，后续所有资源加载都依赖它
        Project::New();

        m_PreferencesPanel.Init();
        ScriptEngine::Init();

        // Gizmo icons
        m_CameraIcon    = Texture2D::Create("assets/Editor/icons/camera_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_PointLightIcon = Texture2D::Create("assets/Editor/icons/lightbulb_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_DirLightIcon  = Texture2D::Create("assets/Editor/icons/sunny_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");

        m_SceneRenderer = std::make_shared<SceneRenderer>();
        m_SceneRenderer->Init();
        m_GameRenderer = std::make_shared<SceneRenderer>();
        m_GameRenderer->Init();

        AssetPreviewer::Init();
        m_FrameDebuggerPanel.SetContext(m_GameRenderer);

        // Pre-frame material pre-bake: ensures textures are GPU-resident before
        // BeginFrame, preventing SetData→EndSingleTimeCommands during CB recording.
        Application::SetPreFrameCallback([this]() {
            if (!m_ActiveScene) return;
            auto view = m_ActiveScene->Reg().view<MeshRendererComponent>();
            for (auto e : view) {
                auto& mrc = view.get<MeshRendererComponent>(e);
                if (mrc.MaterialHandle) {
                    auto mat = AssetManager::GetAsset<Material>(mrc.MaterialHandle);
                    if (mat) { mat->BakeProperties(); }
                }
            }
        });

        InitDefaultProject();
        m_TimelinePanel.SetContext(m_ActiveScene);

        // 清理临时文件
        std::string tempPath = VFS::ResolveString("project://temp/temp_play_scene.ayaya");
        if (std::filesystem::exists(tempPath)) {
            std::filesystem::remove(tempPath);
        }
    }

    void EditorLayer::OnDetach() {
        // AssetPreviewer 必须在 VulkanContext 销毁前释放其 VMA 资源。
        // EditorLayer 属于 m_LayerStack，在 Application 成员中先于 m_Window 析构，
        // 因此这里的 Shutdown 调用时序是正确的。
        AssetPreviewer::Shutdown();
        m_SceneRenderer.reset();
        m_GameRenderer.reset();
    }

    void EditorLayer::OnUpdate(Timestep ts) {
        // ==========================================
        // 0. 执行挂起的项目加载任务 (最优先)
        // ==========================================
        if (!m_ProjectToLoad.empty()) {
            OpenProject(m_ProjectToLoad); // 这里会安全地触发进度条
            m_ProjectToLoad = "";         // 清空标记
        }

        // Asset hot-reload: process pending file changes + invalidate caches
        auto reloadedUUIDs = m_AssetWatcher.Update();
        if (!reloadedUUIDs.empty() && m_ActiveScene) {
            for (auto uuid : reloadedUUIDs)
                m_ActiveScene->InvalidateAssetCache(uuid);
        }

        // GPU-resident thumbnail generation: one per frame, zero CPU blocking
        AssetPreviewer::ProcessOneThumbnail();

        // OS file drag-drop: open import modal for model files dropped onto the window
        {
            auto droppedPaths = Ayaya::Window::GetDroppedPaths();
            for (auto& path : droppedPaths) {
                std::string ext = std::filesystem::path(path).extension().string();
                for (auto& c : ext) c = (char)std::tolower(c);
                if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                    m_ImportModelPanel.RequestOpen(path);
                    break; // one modal at a time
                }
            }
        }

        // ==========================================
        // 1. 处理输入
        // ==========================================
        HandleShortcuts();

        // ==========================================
        // 2. 处理UI窗口 的 Resize
        // ==========================================
        // 获取窗口的缩放系数，用来适配 Mac 的 Retina 缩放
        float dpiScale = ImGui::GetIO().DisplayFramebufferScale.x;
        // AYAYA_CORE_ERROR("dpiScale: {0}", dpiScale);

        // ------------------------------------------
        // 2.1 处理 Scene (上帝视口) 的 Resize
        // ------------------------------------------
        static glm::vec2 s_LastViewportSize = { 0.0f, 0.0f };
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
           (s_LastViewportSize.x != m_ViewportSize.x || s_LastViewportSize.y != m_ViewportSize.y)) {

            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            // When custom viewport resolution is active, skip renderer resize here —
            // step 5.2 below applies the custom resolution. This avoids a redundant
            // OnWindowResize (and vkDeviceWaitIdle) that would be immediately overridden.
            if (!(m_ViewportResW > 0 && m_ViewportResH > 0)) {
                uint32_t physicalWidth = (uint32_t)(m_ViewportSize.x * dpiScale);
                uint32_t physicalHeight = (uint32_t)(m_ViewportSize.y * dpiScale);
                m_SceneRenderer->OnWindowResize(physicalWidth, physicalHeight);
            }
            s_LastViewportSize = m_ViewportSize;
        }

        // ------------------------------------------
        // 2.2 处理 Game (游戏视口) 的 Resize
        // ------------------------------------------
        static glm::vec2 s_LastGameViewportSize = { 0.0f, 0.0f };
        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f && 
           (s_LastGameViewportSize.x != m_GameViewportSize.x || s_LastGameViewportSize.y != m_GameViewportSize.y)) {
            
            uint32_t physicalGameWidth  = (uint32_t)(m_GameViewportSize.x * dpiScale);
            uint32_t physicalGameHeight = (uint32_t)(m_GameViewportSize.y * dpiScale);

            // Skip renderer resize when custom resolution is active
            if (!(m_GameViewportResW > 0 && m_GameViewportResH > 0))
                m_GameRenderer->OnWindowResize(physicalGameWidth, physicalGameHeight);

            // Update non-fixed-aspect-ratio cameras for the Game viewport
            auto view = m_ActiveScene->Reg().view<CameraComponent>();
            for (auto entityID : view) {
                auto& cameraComp = view.get<CameraComponent>(entityID);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize(
                        (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                            ? (uint32_t)m_GameViewportResW : (uint32_t)m_GameViewportSize.x,
                        (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                            ? (uint32_t)m_GameViewportResH : (uint32_t)m_GameViewportSize.y);
                }
            }
            s_LastGameViewportSize = m_GameViewportSize;
        }

        // Apply custom game viewport resolution each frame
        if (m_GameViewportResW > 0 && m_GameViewportResH > 0) {
            m_GameRenderer->OnWindowResize(
                (uint32_t)(m_GameViewportResW * dpiScale),
                (uint32_t)(m_GameViewportResH * dpiScale));
        }

        // ==========================================
        // 3. 处理runtime，包括物理引擎和Lua脚本系统
        // ==========================================
        // 上帝相机只在 Edit 模式响应输入
        if (m_SceneState == SceneState::Edit) {
            m_EditorCamera.MoveSpeed = m_CameraSpeed;
            m_EditorCamera.OnUpdate(ts, m_ViewportFocused);
            // Editor-mode Lua scripts (lazy-init env + call OnEditorUpdate)
            ScriptEngine::OnEditorUpdate(m_ActiveScene.get(), ts);
            m_TweenManager.Update(ts);
            // Timeline preview — drives AnimationSystem with preview time (not scene clock)
            if (m_TimelinePanel.IsPreviewing()) {
                m_ActiveScene->SetAnimationTime(m_TimelinePanel.GetPreviewTime());
                AnimationSystem::Update(*m_ActiveScene, m_TimelinePanel.GetPreviewTime());
            }
        }
        // 如果处于 Play 模式且没有暂停，则推进物理运算，兼容 m_TimeStepScale
        else if (m_SceneState == SceneState::Play && !m_IsPaused) {
            m_ActiveScene->OnUpdateRuntime(ts * m_TimeStepScale);
        }

        // Content drawer toggle: Ctrl+Space
        { static bool wasDown = false;
          bool down = Input::IsKeyPressed(Key::LeftControl) && Input::IsKeyPressed(Key::Space);
          if (down && !wasDown) m_ShowContentDrawer = !m_ShowContentDrawer;
          wasDown = down; }
        if (m_ShowContentDrawer)
            m_DrawerAnimationProgress = glm::mix(m_DrawerAnimationProgress, 1.0f, 15.0f * ts.GetSeconds());
        else
            m_DrawerAnimationProgress = 0.0f;
        if (m_DrawerAnimationProgress > 0.999f) m_DrawerAnimationProgress = 1.0f;

        if (m_ShowTimelineDrawer)
            m_TimelineDrawerProgress = glm::mix(m_TimelineDrawerProgress, 1.0f, 15.0f * ts.GetSeconds());
        else
            m_TimelineDrawerProgress = 0.0f;
        if (m_TimelineDrawerProgress > 0.999f) m_TimelineDrawerProgress = 1.0f;

        // ==========================================
        // 4. 环境光与 IBL 动态更新系统
        // ==========================================
        auto envView = m_ActiveScene->Reg().view<EnvironmentComponent>();
        bool hasEnvironment = false; // 记录场景里是否还有环境组件
        
        for (auto entityID : envView) {
            hasEnvironment = true;
            auto& envComp = envView.get<EnvironmentComponent>(entityID);
            
            if (envComp.IsDirty) {
                m_SceneRenderer->SetEnvironment(envComp);
                envComp.IsDirty = true;
                m_GameRenderer->SetEnvironment(envComp);
                envComp.IsDirty = false; 
            }
            
            m_SceneRenderer->SetEnvironmentSettings(envComp.Intensity, envComp.AmbientColor);
            m_GameRenderer->SetEnvironmentSettings(envComp.Intensity, envComp.AmbientColor);
            break; 
        }

        // 【核心修复】：如果用户把天空盒实体直接删了，强制清空渲染器的光照数据！
        if (!hasEnvironment) {
            EnvironmentComponent emptyEnv;
            emptyEnv.Type = EnvironmentType::None;
            emptyEnv.AmbientColor = { 0.0f, 0.0f, 0.0f }; 
            m_SceneRenderer->SetEnvironment(emptyEnv);
            m_GameRenderer->SetEnvironment(emptyEnv);
            m_SceneRenderer->SetEnvironmentSettings(0.0f, emptyEnv.AmbientColor);
            m_GameRenderer->SetEnvironmentSettings(0.0f, emptyEnv.AmbientColor);
        }

        // ==========================================
        // 5. 渲染管线
        // ==========================================
        // ------------------------------------------
        // 5.1: Game 窗口
        // ------------------------------------------
        bool hasValidCamera = false;
        glm::mat4 cameraViewMatrix, cameraProjectionMatrix;
        glm::vec3 cameraPosition;
        
        // 用于接收当前玩家相机的环境配置
        bool renderSkybox = false; 
        glm::vec4 clearColor = { 0.06f, 0.06f, 0.065f, 1.0f };
        
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
                    cameraComp.Camera.SetViewportSize(
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResW : (uint32_t)m_GameViewportSize.x,
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResH : (uint32_t)m_GameViewportSize.y);
                }

                cameraProjectionMatrix = cameraComp.Camera.GetProjection();

                renderSkybox = (cameraComp.ClearFlag == CameraComponent::ClearFlags::Skybox);
                clearColor = cameraComp.BackgroundColor;
                m_GameRenderer->SetClearColor(clearColor);

                hasValidCamera = true;
                break; 
            }
        }

        if (hasValidCamera) {
            m_GameRenderer->BeginScene(cameraViewMatrix, cameraProjectionMatrix, cameraPosition);
            RenderViewConfig gameCfg;
            gameCfg.EnableSkybox = renderSkybox;
            gameCfg.ClearColor = clearColor;
            m_GameRenderer->RenderScene(m_ActiveScene, gameCfg);
            m_GameRenderer->EndScene();

            m_GameStats = m_GameRenderer->GetStats();
        } else {
            memset(&m_GameStats, 0, sizeof(SceneRenderer::Statistics));
            // 由于没有主相机，直接让管线渲染黑屏
            m_GameRenderer->BeginScene(glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f));
            RenderViewConfig gameCfg2;
            gameCfg2.EnableSkybox = false;
            gameCfg2.ClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            m_GameRenderer->RenderScene(m_ActiveScene, gameCfg2);
            m_GameRenderer->EndScene();
        }

        // ------------------------------------------
        // 5.2: 渲染 Scene 窗口
        // ------------------------------------------
        // Apply custom viewport resolution if set
        if (m_ViewportResW > 0 && m_ViewportResH > 0)
            m_SceneRenderer->OnWindowResize(m_ViewportResW, m_ViewportResH);
        m_SceneRenderer->SetClearColor(clearColor);
        m_SceneRenderer->BeginScene(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection(), m_EditorCamera.GetPosition());
        RenderViewConfig editorCfg;
        editorCfg.IsEditorView = true;
        editorCfg.EnableGrid = m_ShowGrid;
        editorCfg.EnableSkybox = renderSkybox;
        editorCfg.HoveredEntity = m_HoveredEntity;
        editorCfg.SelectedEntities = m_SceneHierarchyPanel.GetSelectedEntities();
        editorCfg.ClearColor = clearColor;
        m_SceneRenderer->RenderScene(m_ActiveScene, editorCfg);
        m_SceneRenderer->EndScene();

        // ------------------------------------------
        // 5.3: 独立的高清截图执行器 (离线渲染 Pass)
        // ------------------------------------------
        if (m_ScreenshotPanel.ConsumePending() && RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            uint32_t shotW = m_ScreenshotPanel.GetWidth();
            uint32_t shotH = m_ScreenshotPanel.GetHeight();
            std::string shotPath = m_ScreenshotPanel.GetPath();

            auto cameraView = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
            for (auto entityID : cameraView) {
                auto [transform, cameraComp] = cameraView.get<TransformComponent, CameraComponent>(entityID);
                if (!cameraComp.Primary) continue;

                // Camera matrices
                Entity camEntity{ entityID, m_ActiveScene.get() };
                glm::mat4 worldXform = camEntity.GetWorldTransform();
                glm::vec3 scale, trans, skew; glm::quat rot; glm::vec4 persp;
                glm::decompose(worldXform, scale, rot, trans, skew, persp);
                glm::vec3 camPos = trans;
                glm::mat4 camView = glm::inverse(glm::translate(glm::mat4(1.0f), trans) * glm::toMat4(rot));

                // Save state
                uint32_t oldW = (uint32_t)(m_GameViewportSize.x * dpiScale);
                uint32_t oldH = (uint32_t)(m_GameViewportSize.y * dpiScale);

                // Resize renderer to screenshot resolution
                cameraComp.Camera.SetViewportSize(shotW, shotH);
                glm::mat4 camProj = cameraComp.Camera.GetProjection();
                m_GameRenderer->OnWindowResize(shotW, shotH);
                m_GameRenderer->SetClearColor(cameraComp.BackgroundColor);

                // Render offscreen
                bool skybox = (cameraComp.ClearFlag == CameraComponent::ClearFlags::Skybox);
                m_GameRenderer->BeginScene(camView, camProj, camPos);
                RenderViewConfig cfg;
                cfg.EnableSkybox = skybox;
                cfg.ClearColor = cameraComp.BackgroundColor;
                m_GameRenderer->RenderScene(m_ActiveScene, cfg);
                m_GameRenderer->EndScene();

                // Read pixels from the FBO
                std::vector<unsigned char> pixels(shotW * shotH * 4);
                auto& fbs = m_GameRenderer->GetRenderContext().Framebuffers;
                std::shared_ptr<Framebuffer> fbo;
                for (auto& key : {"FXAA", "FinalOutput"}) {
                    auto it = fbs.find(key);
                    if (it != fbs.end()) { fbo = it->second; break; }
                }
                if (fbo) {
                    GLuint fboID = (GLuint)(intptr_t)fbo->GetRendererID();
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, fboID);
                    glReadPixels(0, 0, shotW, shotH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                }

                // Write PNG
                stbi_flip_vertically_on_write(true);
                stbi_write_png(shotPath.c_str(), shotW, shotH, 4, pixels.data(), shotW * 4);
                AYAYA_CORE_INFO("Screenshot saved: {0} ({1}x{2})", shotPath, shotW, shotH);

                // Restore
                cameraComp.Camera.SetViewportSize(
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResW : (uint32_t)m_GameViewportSize.x,
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResH : (uint32_t)m_GameViewportSize.y);
                m_GameRenderer->OnWindowResize(oldW, oldH);
                break;
            }
        }
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace(40.0f);
        UIRenderMenuBar();
        UIRenderToolbar();

        m_SceneHierarchyPanel.OnImGuiRender();
        // ContentBrowser in bottom drawer
        // m_ContentBrowserPanel.OnImGuiRender();
        m_PreferencesPanel.OnImGuiRender();
        m_ScreenshotPanel.OnImGuiRender();
        m_HistoryPanel.OnImGuiRender();
        m_FrameDebuggerPanel.OnImGuiRender();
        m_TimelinePanel.OnImGuiRender();
        
        UIRenderViewport();
        UIRenderGameViewport();

        UIRenderNewProjectPopup();
        UIRenderSaveAsPopup();
        m_ImportModelPanel.Draw();
        m_CurveEditorPanel.OnImGuiRender();

        // ---- Drawer + Bottom bar ----
        float barH = 40.0f;
        float contentBottom = ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y;

        // Drawer (above bottom bar, animated)
        if (m_DrawerAnimationProgress > 0.001f) {
            ImVec2 dockWinPos = ImGui::GetWindowPos();
            float dockWinW = ImGui::GetWindowWidth();
            float targetH = ImGui::GetWindowHeight() * 0.40f;
            float curH = targetH * m_DrawerAnimationProgress;
            float curY = dockWinPos.y + contentBottom - barH - curH + 1.0f;
            float curW = dockWinW * 0.75f;
            bool animating = (m_DrawerAnimationProgress < 0.995f);
            if (animating) {
                ImGui::SetNextWindowPos(ImVec2(dockWinPos.x, curY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);
            }
            if (ImGui::Begin("Content Browser##Drawer", &m_ShowContentDrawer, ImGuiWindowFlags_NoDocking))
                m_ContentBrowserPanel.RenderContent();
            ImGui::End();
        }

        // ---- Timeline Drawer ----
        if (m_TimelineDrawerProgress > 0.001f) {
            ImVec2 winPos = ImGui::GetWindowPos();
            float winW = ImGui::GetWindowWidth();
            float targetH = ImGui::GetWindowHeight() * 0.40f;
            float curH = targetH * m_TimelineDrawerProgress;
            float curY = winPos.y + contentBottom - barH - curH + 1.0f;
            float curW = winW * 0.75f;
            bool animating = (m_TimelineDrawerProgress < 0.995f);
            if (animating) {
                ImGui::SetNextWindowPos(ImVec2(winPos.x, curY), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(curW, curH), ImGuiCond_Always);
            }
            if (ImGui::Begin("Timeline##Drawer", &m_ShowTimelineDrawer,
                    ImGuiWindowFlags_NoDocking))
                m_TimelinePanel.RenderContent();
            ImGui::End();
        }

        // Bottom bar (at contentBottom)
        {
            ImGui::SetCursorPosY(contentBottom - barH);
            ImGui::BeginChild("##BottomBar", ImVec2(0, barH), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImVec2 bp = ImGui::GetWindowPos(); ImVec2 bs = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRectFilled(bp, ImVec2(bp.x + bs.x, bp.y + bs.y),
                IM_COL32(20, 20, 25, 255), 0.0f);
            float btnH = barH - 6.0f;
            ImGui::SetCursorPosY(3.0f);
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.68f, 1.0f));
            if (ImGui::Button(ICON_FA_FOLDER_OPEN " Content Browser", ImVec2(0, btnH)))
                m_ShowContentDrawer = !m_ShowContentDrawer;
            ImGui::SameLine(0, 4.0f);
            if (ImGui::Button(ICON_FA_LIST_ALT " Log", ImVec2(0, btnH))) {}
            ImGui::SameLine(0, 4.0f);
            if (ImGui::Button(ICON_FA_FILM " Timeline", ImVec2(0, btnH)))
                m_ShowTimelineDrawer = !m_ShowTimelineDrawer;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::PopFont();
            ImGui::EndChild();
        }

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

        // 创造天空盒/环境光
        Entity skyEntity = m_ActiveScene->CreateEntity("Skybox");
        auto& envComp = skyEntity.AddComponent<EnvironmentComponent>();
        envComp.Type = EnvironmentType::HDR_Equirectangular;
        envComp.EquirectangularHandle = AssetManager::ImportAsset("assets/Editor/textures/skybox/hdr/newport_loft.hdr");
        envComp.Type = EnvironmentType::HDR_Equirectangular;
        envComp.Intensity = 30000.0f; 
        envComp.AmbientColor = glm::vec3(0.0f, 0.0f, 0.0f);
        envComp.IsDirty = true;

        // 创造物体
        Entity cubeEntity = m_ActiveScene->CreateEntity("Cube");
        cubeEntity.GetComponent<TransformComponent>().Scale = { 1.0f, 1.0f, 1.0f };
        cubeEntity.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
        auto& mrc = cubeEntity.AddComponent<MeshRendererComponent>();
        mrc.ModelHandle = AssetManager::GetBuiltInCube();
        mrc.MaterialHandle = AssetManager::GetBuiltInMaterial();

        m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
    }

    // =====================================================================
    // initProject：引擎启动时的默认项目。不存在则创建，存在则加载
    // =====================================================================
    void EditorLayer::InitDefaultProject() {
        std::filesystem::path initProjDir = std::filesystem::current_path() / "assets/AyayaProject/initProject";
        std::filesystem::path initProjFile = initProjDir / "initProject.ayaproj";

        if (std::filesystem::exists(initProjFile)) {
            // 已存在 → 作为项目打开
            OpenProject(initProjFile);
        } else {
            // 首次启动 → 创建项目 + 默认场景
            std::filesystem::create_directories(initProjDir / "Assets" / "Scenes");
            std::filesystem::create_directories(initProjDir / "Assets" / "Materials");
            std::filesystem::create_directories(initProjDir / "Temp");

            auto project = Project::New();
            project->GetConfig().Name = "initProject";
            project->GetConfig().AssetDirectory = "Assets";
            project->GetConfig().StartScene = "Scenes/Default.ayaya";
            Project::SaveActive(initProjFile);

            // 重新加载以同步 m_ProjectDirectory（Project::New 设为 current_path，需要修正为 initProjDir）
            Project::Load(initProjFile);
            VFS::Mount("project", initProjDir / "Assets");
            AssetManager::Clear(); // 清除旧 project:// mount 加载的残留条目
            AssetManager::RefreshRegistry();

            SetupScene();
            m_CurrentScenePath = VFS::ResolveString("project://Scenes/Default.ayaya");

            // COW: built-in DefaultPBR stays as engine reference.
            // Users create project copies via PropertiesPanel "Create Material File".

            SaveScene();
            SaveProject();
        }
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
            state.CameraPosition = m_EditorCamera.GetPosition();
            state.CameraDistance = m_EditorCamera.GetDistance();
            state.CameraPitch = m_EditorCamera.GetPitch();
            state.CameraYaw = m_EditorCamera.GetYaw();
            state.CameraFocalPoint = m_EditorCamera.GetFocalPoint();

            serializer.Serialize(m_CurrentScenePath, state); // 传入两个参数！
            
            AYAYA_CORE_INFO("Scene strictly saved to {0}", m_CurrentScenePath);
        } 
        // 否则（这是一个新建的未保存场景），转为"另存为"逻辑
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

    // =====================================================================
    // 另存为项目：选择文件夹 → 创建项目结构 → 保存当前场景+材质+注册表
    // =====================================================================
    void EditorLayer::SaveProjectAs() {
        // Populate defaults from current project
        auto active = Project::GetActive();
        if (active) {
            strncpy(m_SaveAsProjectName, active->GetConfig().Name.c_str(), sizeof(m_SaveAsProjectName) - 1);
            m_SaveAsProjectName[sizeof(m_SaveAsProjectName) - 1] = '\0';
        } else {
            strncpy(m_SaveAsProjectName, "AyayaProject", sizeof(m_SaveAsProjectName) - 1);
            m_SaveAsProjectName[sizeof(m_SaveAsProjectName) - 1] = '\0';
        }

        // Default location: parent of current project directory
        auto projDir = Project::GetProjectDirectory();
        if (!projDir.empty()) {
            std::string parent = projDir.parent_path().string();
            strncpy(m_SaveAsProjectLocation, parent.c_str(), sizeof(m_SaveAsProjectLocation) - 1);
        }

        m_ShowSaveAsPopup = true;
    }

    void EditorLayer::UIRenderSaveAsPopup() {
        if (m_ShowSaveAsPopup) {
            ImGui::OpenPopup("Save Project As");
            m_ShowSaveAsPopup = false;
        }

        ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Save Project As", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.9f, 1.0f), ICON_FA_SAVE " Save Project As");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Project Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##SaveAsName", m_SaveAsProjectName, sizeof(m_SaveAsProjectName));
            ImGui::Spacing();

            ImGui::Text("Location");
            ImGui::PushItemWidth(-110.0f);
            ImGui::InputText("##SaveAsLocation", m_SaveAsProjectLocation, sizeof(m_SaveAsProjectLocation), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(100, 0))) {
                std::string selectedDir = FileDialogs::OpenFolder();
                if (!selectedDir.empty()) {
                    strncpy(m_SaveAsProjectLocation, selectedDir.c_str(), sizeof(m_SaveAsProjectLocation) - 1);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Preview
            std::filesystem::path finalDir = std::filesystem::path(m_SaveAsProjectLocation) / m_SaveAsProjectName;
            ImGui::TextDisabled("Project will be saved to:");
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 480.0f);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", finalDir.generic_string().c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing(); ImGui::Spacing();

            if (ImGui::Button("Save", ImVec2(120, 35))) {
                if (strlen(m_SaveAsProjectName) > 0 && strlen(m_SaveAsProjectLocation) > 0) {
                    std::string baseName = m_SaveAsProjectName;
                    std::filesystem::path finalDirTry = std::filesystem::path(m_SaveAsProjectLocation) / baseName;
                    int suffix = 2;
                    while (std::filesystem::exists(finalDirTry)) {
                        baseName = std::string(m_SaveAsProjectName) + " (" + std::to_string(suffix) + ")";
                        finalDirTry = std::filesystem::path(m_SaveAsProjectLocation) / baseName;
                        suffix++;
                    }
                    if (baseName != std::string(m_SaveAsProjectName)) {
                        strncpy(m_SaveAsProjectName, baseName.c_str(), sizeof(m_SaveAsProjectName) - 1);
                    }
                    std::filesystem::path destDir = finalDirTry;

                    // Copy entire project directory
                    try {
                        auto srcDir = Project::GetProjectDirectory();
                        if (!srcDir.empty() && std::filesystem::exists(srcDir)) {
                            std::filesystem::copy(srcDir, destDir,
                                std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::overwrite_existing);
                        } else {
                            std::filesystem::create_directories(destDir / "Assets" / "Scenes");
                            std::filesystem::create_directories(destDir / "Assets" / "Models");
                            std::filesystem::create_directories(destDir / "Assets" / "Textures");
                            std::filesystem::create_directories(destDir / "Assets" / "Materials");
                        }
                    } catch (const std::exception& e) {
                        AYAYA_CORE_ERROR("SaveProjectAs: copy failed: {0}", e.what());
                    }

                    // Save new project file
                    std::filesystem::path projFile = destDir / (baseName + ".ayaproj");

                    // Remove old .ayaproj copied from source
                    auto oldActive = Project::GetActive();
                    if (oldActive) {
                        std::string oldName = oldActive->GetConfig().Name;
                        std::filesystem::path oldProj = destDir / (oldName + ".ayaproj");
                        if (oldProj != projFile && std::filesystem::exists(oldProj))
                            std::filesystem::remove(oldProj);
                    }
                    auto project = Project::New();
                    project->GetConfig().Name = baseName;
                    project->GetConfig().AssetDirectory = "Assets";
                    project->GetConfig().StartScene = "Scenes/Default.ayaya";
                    Project::SaveActive(projFile);

                    // Reload the new project so VFS/assets point to the new location
                    m_ProjectToLoad = projFile.string();

                    AYAYA_CORE_INFO("Project saved as: {0}", destDir.string());
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 35))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    void EditorLayer::NewScene() {
        if (m_SceneState == SceneState::Play) OnSceneStop();
        m_FrameDebuggerPanel.Reset();

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

        m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
        m_CurrentScenePath = std::string(); 
        m_HoveredEntity = {};
        m_SceneHierarchyPanel.SetSelectedEntity({});

        // 【核心修复】：新建场景时，彻底清空撤销历史！
        m_CommandHistory.Clear();

        AYAYA_CORE_INFO("Created a new empty scene with default camera.");
    }

    void EditorLayer::OpenScene() {
        std::string filepath = FileDialogs::OpenFile("ayaya");
        if (filepath.empty()) return;

        // Stop game if running, reset debugger
        if (m_SceneState == SceneState::Play) OnSceneStop();
        m_FrameDebuggerPanel.Reset();

        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
        SceneSerializer serializer(newScene);
        EditorState state;
        if (serializer.Deserialize(filepath, state)) {
            m_ActiveScene = newScene;
            m_EditorScene = m_ActiveScene;
            m_CurrentScenePath = filepath;
            m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
            m_HoveredEntity = {};
            m_SceneHierarchyPanel.SetSelectedEntity({});
            m_CommandHistory.Clear();

            auto activeHandles = m_ActiveScene->GetActiveAssetHandles();
            AssetManager::UnloadUnusedAssets(activeHandles);

            AYAYA_CORE_INFO("Scene loaded: {0}", filepath);
        }
    }

    void EditorLayer::OpenSceneFile(const std::filesystem::path& filepath) {
        if (filepath.empty() || !std::filesystem::exists(filepath)) return;

        if (m_SceneState == SceneState::Play) OnSceneStop();
        m_FrameDebuggerPanel.Reset();

        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
        SceneSerializer serializer(newScene);
        EditorState editorState;
        if (serializer.Deserialize(filepath.string(), editorState)) {
            m_ActiveScene = newScene;
            m_EditorScene = m_ActiveScene;
            m_CurrentScenePath = filepath.string();
            m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
            m_HoveredEntity = {};
            m_SceneHierarchyPanel.SetSelectedEntity({});
            m_CommandHistory.Clear();

            auto activeHandles = m_ActiveScene->GetActiveAssetHandles();
            AssetManager::UnloadUnusedAssets(activeHandles);

            AYAYA_CORE_INFO("Scene opened from ContentBrowser: {0}", filepath.string());
        }
    }

    // =====================================================================
    // 项目管理系统 (Project Management)
    // =====================================================================

    void EditorLayer::NewProject() {
        // 1. 弹出原生保存对话框让用户选择项目路径和名称 (.ayaproj)
        std::string filepath = FileDialogs::SaveFile("Ayaya Project (*.ayaproj)|*.ayaproj");
        if (filepath.empty()) return;

        // Stop game if running, reset debugger before creating new project
        if (m_SceneState == SceneState::Play) OnSceneStop();
        m_FrameDebuggerPanel.Reset();

        std::filesystem::path projectFilePath = filepath;
        std::filesystem::path projectDir = projectFilePath.parent_path();
        std::filesystem::path assetDir = projectDir / "Assets"; // 默认资产根目录

        // 2. 在磁盘上创建标准的物理文件夹结构
        try {
            std::filesystem::create_directories(assetDir);
            std::filesystem::create_directories(assetDir / "Scenes");
            std::filesystem::create_directories(assetDir / "Models");
            std::filesystem::create_directories(assetDir / "Textures");
            std::filesystem::create_directories(assetDir / "Scripts");
            std::filesystem::create_directories(assetDir / "Materials");
        } catch (const std::exception& e) {
            AYAYA_CORE_ERROR("Failed to create project directories: {0}", e.what());
            return;
        }

        // 3. 初始化 Project 配置对象
        auto project = Project::New();
        project->GetConfig().Name = projectFilePath.stem().string();
        project->GetConfig().AssetDirectory = "Assets";
        project->GetConfig().StartScene = "Scenes/Default.ayaya";
        
        // 保存项目文件
        Project::SaveActive(projectFilePath);

        // 重新加载以同步 m_ProjectDirectory
        Project::Load(projectFilePath);

        // 4. 【关键步骤】：动态将新项目的物理路径挂载到 project:// 虚拟协议
        VFS::Mount("project", assetDir);

        // 5. 初始化该项目的资产注册表
        AssetManager::Clear(); // 清空旧项目内存
        AssetManager::RefreshRegistry();

        // 6. 自动创建一个初始场景并保存到项目内
        NewScene();
        std::string defaultScenePath = VFS::ResolveString("project://Scenes/Default.ayaya");
        SceneSerializer serializer(m_ActiveScene);
        EditorState dummyState;
        serializer.Serialize(defaultScenePath, dummyState);
        
        m_CurrentScenePath = defaultScenePath;

        AYAYA_CORE_INFO("Successfully created and initialized project: {0}", project->GetConfig().Name);
    }

    // =====================================================================
    // 项目创建向导
    // =====================================================================
    void EditorLayer::UIRenderNewProjectPopup() {
        if (m_ShowNewProjectPopup) {
            ImGui::OpenPopup("Create New Project");
            m_ShowNewProjectPopup = false; 
        }

        // ==========================================
        // 【核心修复 1】：设定一个合理的窗口固定宽度 (例如 500 像素)
        // 使用 ImGuiCond_Appearing 确保每次打开时重置大小
        // ==========================================
        ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
        
        // 居中显示
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        
        if (ImGui::BeginPopupModal("Create New Project", nullptr, ImGuiWindowFlags_NoResize)) {
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); 
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.9f, 1.0f), ICON_FA_FOLDER_PLUS " Configure New Project");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Spacing();

            // 1. 项目名称
            ImGui::Text("Project Name");
            ImGui::SetNextItemWidth(-1.0f); // 填满整行
            ImGui::InputText("##ProjectName", m_NewProjectName, sizeof(m_NewProjectName));
            
            ImGui::Spacing();

            // 2. 项目位置 (使用 Browse 按钮)
            ImGui::Text("Project Location");
            
            // 【核心修复 2】：使用 PushItemWidth(-120.0f) 而不是 GetContentRegionAvail().x
            // 这意味着：总宽度减去 120 像素，这在 AutoResize 窗口中是稳定的
            ImGui::PushItemWidth(-110.0f); 
            ImGui::InputText("##ProjectLocation", m_NewProjectLocation, sizeof(m_NewProjectLocation), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            
            // 点击调用我们刚刚完善的原生文件夹弹窗
            if (ImGui::Button("Browse...", ImVec2(100, 0))) {
                std::string selectedDir = FileDialogs::OpenFolder();
                if (!selectedDir.empty()) {
                    strncpy(m_NewProjectLocation, selectedDir.c_str(), sizeof(m_NewProjectLocation) - 1);
                    m_NewProjectLocation[sizeof(m_NewProjectLocation) - 1] = '\0'; // 确保安全截断
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // 3. 实时预览完整路径
            std::filesystem::path finalDir = std::filesystem::path(m_NewProjectLocation) / m_NewProjectName;
            ImGui::TextDisabled("New project folder will be created at:");
            
            // 【核心修复 3】：限制 TextWrapped 的宽度，防止长路径把窗口顶飞
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 480.0f);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", finalDir.generic_string().c_str());
            ImGui::PopTextWrapPos();
            
            ImGui::Spacing();
            ImGui::Spacing();

            // 4. 操作按钮
            if (ImGui::Button("Create Project", ImVec2(150, 35))) {
                if (strlen(m_NewProjectName) > 0 && strlen(m_NewProjectLocation) > 0) {

                    // 检测重名：自动添加序号后缀
                    std::string baseName = m_NewProjectName;
                    std::filesystem::path finalDirTry = std::filesystem::path(m_NewProjectLocation) / baseName;
                    int suffix = 2;
                    while (std::filesystem::exists(finalDirTry)) {
                        baseName = std::string(m_NewProjectName) + " (" + std::to_string(suffix) + ")";
                        finalDirTry = std::filesystem::path(m_NewProjectLocation) / baseName;
                        suffix++;
                    }
                    if (baseName != std::string(m_NewProjectName)) {
                        strncpy(m_NewProjectName, baseName.c_str(), sizeof(m_NewProjectName) - 1);
                        AYAYA_CORE_INFO("Project name adjusted to avoid conflict: {0}", baseName);
                    }
                    std::filesystem::path finalDir = finalDirTry;

                    std::filesystem::path assetDir = finalDir / "Assets";
                    std::filesystem::path projectFile = finalDir / (baseName + ".ayaproj");

                    // 创建标准的 Unity 式物理目录
                    std::filesystem::create_directories(assetDir / "Scenes");
                    std::filesystem::create_directories(assetDir / "Models");
                    std::filesystem::create_directories(assetDir / "Textures");
                    std::filesystem::create_directories(assetDir / "Materials");
                    std::filesystem::create_directories(finalDir / "Temp");

                    // 实例化项目单例
                    auto project = Project::New();
                    project->GetConfig().Name = baseName;
                    project->GetConfig().AssetDirectory = "Assets";
                    project->GetConfig().StartScene = "Scenes/Default.ayaya";
                    Project::SaveActive(projectFile);

                    // 重新加载以同步 m_ProjectDirectory
                    Project::Load(projectFile);

                    // 动态挂载 VFS 节点
                    VFS::Mount("project", assetDir);

                    // 初始化资产数据库
                    AssetManager::Clear();
                    AssetManager::RefreshRegistry();

                    // 生成并自动保存起始场景
                    NewScene();
                    m_CurrentScenePath = VFS::ResolveString("project://Scenes/Default.ayaya");

                    // COW: built-in DefaultPBR stays as engine reference.
                    // Users create project copies via PropertiesPanel "Create Material File".

                    // 保存场景 + 资产注册表
                    SaveScene();
                    SaveProject();

                    AYAYA_CORE_INFO("🚀 Project Ready: {0}", finalDir.string());
                    ImGui::CloseCurrentPopup();
                }
            }
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 35))) { ImGui::CloseCurrentPopup(); }
            
            ImGui::EndPopup();
        }
    }

    void EditorLayer::OpenProject() {
        std::string filepath = FileDialogs::OpenFile("Ayaya Project (*.ayaproj)|*.ayaproj");
        if (!filepath.empty()) {
            m_ProjectToLoad = filepath;
        }
    }

    bool EditorLayer::OpenProject(const std::filesystem::path& path) {
        if (path.extension() != ".ayaproj" && path.extension() != ".ayaproject") {
            AYAYA_CORE_ERROR("Not a valid Ayaya project file: {0}", path.string());
            return false;
        }
        if (std::filesystem::exists(path)) {
            LoadProjectWithProgress(path.string());
            return true;
        }
        AYAYA_CORE_ERROR("Project file not found: {0}", path.string());
        return false;
    }

    void EditorLayer::SaveProject() {
    if (Project::GetActive()) {
        // 1. 保存项目配置文件 (.ayaproj)
        auto projectPath = Project::GetProjectDirectory() / (Project::GetActive()->GetConfig().Name + ".ayaproj");
        Project::SaveActive(projectPath);

        // 2. COW: built-in materials stay as shared references — no auto-clone.
        //    Users explicitly create project copies via the "Create Material File"
        //    button in the Properties panel.

        // 3. 保存当前场景 (.ayaya)
        if (!m_CurrentScenePath.empty()) {
            SaveScene();
        }

        // 4. 保存内存中所有的材质资产
        AYAYA_CORE_INFO("Saving all materials...");
        for (auto& [handle, assetPtr] : AssetManager::GetLoadedAssets()) {
            auto metadata = AssetManager::GetMetadata(handle);
            if (metadata.Type == AssetType::Material) {
                std::string physicalPath = AssetManager::GetAssetPhysicalPath(handle);
                if (!physicalPath.empty() && physicalPath.find("assets/Editor/") == std::string::npos) {
                    auto material = std::static_pointer_cast<Material>(assetPtr);
                    MaterialSerializer::Serialize(material, physicalPath);
                }
            }
        }

        AYAYA_CORE_INFO("💾 Project Saved: Config, Scene and Materials synced to disk.");
    }
}
    void EditorLayer::LoadProjectWithProgress(const std::string& projectFilePath) {
        // Stop game if running, reset debugger before loading new project
        if (m_SceneState == SceneState::Play) OnSceneStop();
        m_FrameDebuggerPanel.Reset();

        // 进度条渲染（OpenGL 可显示图形进度条，Vulkan 输出到控制台）
        auto renderProgressFrame = [&](float progress, const std::string& message) {
            if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
                auto context = Application::Get().GetWindow().GetContext();
                ImGuiBackend::BeginFrame();
                ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.085f, 0.09f, 1.0f));
                ImGui::Begin("LoadingScreen", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImVec2 windowSize = ImGui::GetWindowSize();
                float barWidth = 600.0f;
                ImGui::SetCursorPos(ImVec2((windowSize.x - barWidth) * 0.5f, windowSize.y * 0.5f - 50.0f));
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts.Size > 1 ? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetIO().Fonts->Fonts[0]);
                ImGui::TextColored(ImVec4(0.17f, 0.45f, 0.85f, 1.0f), "Loading Project...");
                ImGui::PopFont();
                ImGui::SetCursorPosX((windowSize.x - barWidth) * 0.5f);
                ImGui::TextDisabled("%s", message.c_str());
                ImGui::SetCursorPosX((windowSize.x - barWidth) * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.17f, 0.45f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.04f, 0.045f, 1.0f));
                ImGui::ProgressBar(progress, ImVec2(barWidth, 24.0f));
                ImGui::PopStyleColor(2);
                ImGui::End();
                ImGui::PopStyleColor();
                ImGuiBackend::EndFrameAndSwapBuffers();
                GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
                glfwSwapBuffers(window);
            } else {
                AYAYA_CORE_INFO("⏳ Loading project... {0:.0f}% - {1}", progress * 100.0f, message);
            }
        };

        // 阶段 1: 注册表加载 (0% – 20%)
        renderProgressFrame(0.0f, "Loading project config...");

        if (!Project::Load(projectFilePath)) {
            AYAYA_CORE_ERROR("Failed to load project: {0}", projectFilePath);
            return;
        }

        std::filesystem::path assetDir = Project::GetAssetDirectory();
        VFS::Mount("project", assetDir);

        // .meta-aware loading: migrate from old AssetRegistry.yaml if needed
        std::string registryPath = VFS::ResolveString("project://AssetRegistry.yaml");
        bool hasMetaFiles = false;
        if (std::filesystem::exists(assetDir)) {
            for (auto& entry : std::filesystem::recursive_directory_iterator(assetDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".meta") {
                    hasMetaFiles = true;
                    break;
                }
            }
        }

        renderProgressFrame(0.05f, "Loading asset registry...");

        if (hasMetaFiles) {
            AssetManager::RefreshRegistry();
        } else if (std::filesystem::exists(registryPath)) {
            AssetManager::DeserializeRegistry(registryPath);
            AssetManager::MigrateFromRegistry(registryPath);
            AssetManager::RefreshRegistry();
        } else {
            AssetManager::RefreshRegistry();
        }

        renderProgressFrame(0.15f, "Registry loaded. Loading scene...");

        // 阶段 2: 场景反序列化 (15% – 80%)
        std::string startScenePath = VFS::ResolveString("project://" + Project::GetActive()->GetConfig().StartScene);
        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
        SceneSerializer serializer(newScene);
        EditorState state;

        auto progressCallback = [&](float progress, const std::string& message) {
            float mapped = 0.15f + progress * 0.65f;
            renderProgressFrame(mapped, message);
        };

        if (std::filesystem::exists(startScenePath)) {
            if (serializer.Deserialize(startScenePath, state, progressCallback)) {
                m_ActiveScene = newScene;
                m_EditorScene = m_ActiveScene;
                m_ShowGrid = state.ShowGrid;
                m_EditorCamera.SetPosition(state.CameraPosition);
                m_EditorCamera.SetDistance(state.CameraDistance);
                m_EditorCamera.SetPitch(state.CameraPitch);
                m_EditorCamera.SetYaw(state.CameraYaw);
                m_EditorCamera.SetFocalPoint(state.CameraFocalPoint);
                m_EditorCamera.UpdateCameraView();
                m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
                m_CurrentScenePath = startScenePath;
                m_HoveredEntity = {};
                m_SceneHierarchyPanel.SetSelectedEntity({});
                m_CommandHistory.Clear();

                // 阶段 3: GC 与收尾 (80% – 100%)
                renderProgressFrame(0.85f, "Cleaning up unused assets...");
                auto activeHandles = m_ActiveScene->GetActiveAssetHandles();
                AssetManager::UnloadUnusedAssets(activeHandles);

                renderProgressFrame(1.0f, "Done!");
                AYAYA_CORE_INFO("Project loaded: {0}", Project::GetActive()->GetConfig().Name);

                // Start asset watcher on the new project
                auto assetDir = Project::GetAssetDirectory();
                if (!assetDir.empty() && std::filesystem::exists(assetDir)) {
                    m_AssetWatcher.Shutdown();
                    m_AssetWatcher.Initialize(assetDir);
                }
            }
        } else {
            SetupScene();
            AYAYA_CORE_INFO("New project initialized");
        }
    }

    void EditorLayer::HandleShortcuts() {
        ImGuiIO& io = ImGui::GetIO();

        // 1. 如果当前正在输入文本（比如在 Tag 里打字），把快捷键让给 ImGui 自带的文本撤回
        if (io.WantTextInput) {
            return;
        }

        // =====================================
        // 1. 视口焦点相关的快捷键 (Gizmo 等)
        // =====================================
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
        bool canUseGizmoShortcuts = m_ViewportHovered || m_ViewportFocused;

        // 【核心修复】：全部改用 ImGui 的按键枚举，无视操作系统焦点丢失！
        if (canUseGizmoShortcuts && selectedEntity && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_GizmoType = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoType = ImGuizmo::OPERATION::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoType = ImGuizmo::OPERATION::SCALE;
        }

        // =====================================
        // 2. 全局场景快捷键 (撤销、保存等)
        // =====================================
        bool control = io.KeyCtrl;   // 获取全局 Ctrl 状态
        bool shift   = io.KeyShift;  // 获取全局 Shift 状态

        // --- New Scene (Ctrl + N) ---
        if (ImGui::IsKeyPressed(ImGuiKey_N, false) && control) {
            AYAYA_CORE_INFO("👉 Shortcut Triggered: New Scene");
            // NewScene();
            NewProject();
        }

        // --- Open Scene (Ctrl + O) ---
        if (ImGui::IsKeyPressed(ImGuiKey_O, false) && control) {
            AYAYA_CORE_INFO("👉 Shortcut Triggered: Open Scene");
            // OpenScene();
            OpenProject();
        }

        // --- Save / Save As (Ctrl + S / Ctrl + Shift + S) ---
        if (ImGui::IsKeyPressed(ImGuiKey_S, false) && control) {
            if (shift) {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Project As...");
                SaveProjectAs();
            } else {
                bool hasProjectFile = Project::GetActive() &&
                    std::filesystem::exists(Project::GetProjectDirectory() /
                        (Project::GetActive()->GetConfig().Name + ".ayaproj"));
                if (hasProjectFile) {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Project");
                    SaveProject();
                } else {
                    AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Project As... (no .ayaproj)");
                    SaveProjectAs();
                }
            }
        }

        // --- 撤销 (Ctrl + Z) ---
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && control) {
            if (shift) {
                m_CommandHistory.Redo();
                AYAYA_CORE_INFO("👉 Redo");
            } else {
                m_CommandHistory.Undo();
                AYAYA_CORE_INFO("👉 Undo");
            }
        }

        // --- 重做 (Ctrl + Y) ---
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && control) {
            m_CommandHistory.Redo();
            AYAYA_CORE_INFO("👉 Redo");
        }
    }

    void EditorLayer::UIRenderDockspace(float reserveBottom) {
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
                ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.65f, nullptr, &dock_id_right);
                ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.4f, NULL, &dock_main_id);
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
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, -reserveBottom), dockspace_flags);
        }
    }

    void EditorLayer::UIRenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            // ---- File ----
            UI::PushPopupStyles(220.0f);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            if (ImGui::BeginMenu("File")) {
                if (UI::DrawNativeMenuItem("New Project",       nullptr, "Ctrl+N"))       { m_ShowNewProjectPopup = true; }
                if (UI::DrawNativeMenuItem("Save Project",      nullptr, "Ctrl+S")) {
                    bool has = Project::GetActive() && std::filesystem::exists(
                        Project::GetProjectDirectory() / (Project::GetActive()->GetConfig().Name + ".ayaproj"));
                    has ? SaveProject() : SaveProjectAs();
                }
                if (UI::DrawNativeMenuItem("Save Project As...",nullptr, "Ctrl+Shift+S")) { SaveProjectAs(); }
                if (UI::DrawNativeMenuItem("Open Project",      nullptr, "Ctrl+O"))       { OpenProject(); }
                UI::MenuSeparator();
                if (UI::DrawNativeMenuItem("Import Model...",   nullptr, "Ctrl+I")) {
                    std::string filepath = FileDialogs::OpenFile(
                        "3D Models (*.fbx *.obj *.gltf *.glb)|*.fbx;*.obj;*.gltf;*.glb");
                    if (!filepath.empty()) m_ImportModelPanel.RequestOpen(filepath);
                }
                UI::MenuSeparator();
                if (UI::DrawNativeMenuItem("Exit")) { Application::Get().Close(); }
                ImGui::EndMenu();
            }
            UI::PopPopupStyles();
            ImGui::PopStyleColor();

            // ---- Edit ----
            UI::PushPopupStyles(200.0f);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            if (ImGui::BeginMenu("Edit")) {
                if (UI::DrawNativeMenuItem("Preferences")) { m_PreferencesPanel.SetOpen(true); }
                ImGui::EndMenu();
            }
            UI::PopPopupStyles();
            ImGui::PopStyleColor();

            // ---- View (checkbox items kept native) ----
            UI::PushPopupStyles(200.0f);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show History",   nullptr, &m_HistoryPanel.IsOpen);
                ImGui::MenuItem("Frame Debugger", nullptr, &m_FrameDebuggerPanel.IsOpen);
                ImGui::MenuItem("Curve Editor",   nullptr, &m_CurveEditorPanel.GetOpenFlag());
                ImGui::EndMenu();
            }
            UI::PopPopupStyles();
            ImGui::PopStyleColor();

            // ---- Tools ----
            UI::PushPopupStyles(200.0f);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            if (ImGui::BeginMenu("Tools")) {
                if (UI::DrawNativeMenuItem("High-Res Screenshot")) { m_ScreenshotPanel.Open(); }
                ImGui::EndMenu();
            }
            UI::PopPopupStyles();
            ImGui::PopStyleColor();

            // Right-side status text
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

        // Close Game overlays when Viewport is active
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            m_ShowGameOptions = false;

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
        m_ViewportSize = { std::floor(viewportPanelSize.x), std::floor(viewportPanelSize.y) };

        // ==========================================
        // 核心修改：向渲染管线索要处理完毕的后期画面 (增加安全检查)
        // ==========================================

        if (m_SceneRenderer) {
            void* textureID = m_SceneRenderer->GetFinalColorAttachmentRendererID();
            if (textureID) {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImVec2 imageSize = avail;
                ImVec2 imageOffset(0, 0);

                if (m_ViewportResW > 0 && m_ViewportResH > 0) {
                    float aspect = (float)m_ViewportResW / (float)m_ViewportResH;
                    if (avail.x / avail.y > aspect) {
                        imageSize.x = avail.y * aspect;
                        imageSize.y = avail.y;
                        imageOffset.x = (avail.x - imageSize.x) * 0.5f;
                    } else {
                        imageSize.x = avail.x;
                        imageSize.y = avail.x / aspect;
                        imageOffset.y = (avail.y - imageSize.y) * 0.5f;
                    }
                    ImGui::SetCursorPos(ImGui::GetCursorPos() + imageOffset);
                }
                ImVec2 vpSize = imageSize;
                ImGui::Image(textureID, vpSize, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

                // Update viewport bounds for gizmo/picking
                ImVec2 imgMin = ImGui::GetItemRectMin();
                ImVec2 imgMax = ImGui::GetItemRectMax();
                m_ViewportBounds[0] = { imgMin.x, imgMin.y };
                m_ViewportBounds[1] = { imgMax.x, imgMax.y };
                m_ViewportSize = { imageSize.x, imageSize.y };

                // UI overlay — must match image display size
                ImVec2 uiOverlayPos = ImGui::GetItemRectMin();
                void* uiTexID = m_SceneRenderer->GetBlackboardTextureID("UI");
                if (uiTexID) {
                    ImGui::SetCursorScreenPos(uiOverlayPos);
                    ImGui::Image(uiTexID, vpSize, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
                }
                if (m_ShowUIGizmos) UIRenderDebugUIGizmos(uiOverlayPos, vpSize,
                    ImGui::GetIO().DisplayFramebufferScale.x);

                HandleMousePicking(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
                HandleGizmo(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());

                // Clip debug gizmos to viewport image area so camera/light icons
                // don't spill into letterbox bars when a custom resolution is active.
                ImDrawList* vpDrawList = ImGui::GetWindowDrawList();
                vpDrawList->PushClipRect(imgMin, imgMax, true);
                UIRenderDebugGizmos(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
                vpDrawList->PopClipRect();

                // Anchor: viewport image top-left corner (saved before overlays)
                ImVec2 vpMin = ImGui::GetItemRectMin();
                float btnW = 38.0f, btnH = 32.0f, pad = 3.0f;

                // ---- Top-left buttons: [Options]  [Show] ----
                {
                    ImVec4 inactiveTxt = ImVec4(0.65f, 0.65f, 0.68f, 1.0f);
                    ImVec4 activeTxt   = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 borderCol = IM_COL32(10, 10, 15, 180);
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    // Close all other popups when a new one opens
                    auto closeOthers = [&](bool* keep) {
                        if (keep != &m_ShowViewportOptions)  m_ShowViewportOptions = false;
                        if (keep != &m_ShowViewportGizmos)    m_ShowViewportGizmos = false;
                        if (keep != &m_ShowViewportCameraSpeed) m_ShowViewportCameraSpeed = false;
                        m_ShowGameOptions = false; // close Game's Options too
                    };

                    auto oneBtn = [&](float x, const char* icon, const char* tip, bool* toggle) {
                        bool active = toggle ? *toggle : false;
                        ImVec2 bMin(x, vpMin.y + 6);
                        ImVec2 bMax(x + btnW, vpMin.y + 6 + btnH);
                        dl->AddRectFilled(bMin, bMax,
                            active ? IM_COL32(48, 88, 145, 180) : IM_COL32(20, 20, 25, 180), 10.0f);
                        dl->AddRect(bMin, bMax, borderCol, 10.0f, 0, 1.0f);
                        ImGui::SetCursorScreenPos(bMin);
                        ImGui::PushID(tip);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, active ? activeTxt : inactiveTxt);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        bool pressed = ImGui::Button(icon, ImVec2(btnW, btnH));
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        if (pressed && toggle) {
                            closeOthers(toggle);
                            *toggle = !*toggle;
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("%s", tip);
                    };

                    // Left panel buttons: [Options] [Show] [Proj] [CamSpeed]
                    float ox  = vpMin.x + 8;
                    float sx  = ox  + btnW + 4;
                    float pjx = sx  + btnW + 4;

                    oneBtn(ox, ICON_FA_BARS, "Options", &m_ShowViewportOptions);
                    oneBtn(sx, ICON_FA_EYE,  "Show",    &m_ShowViewportGizmos);

                    // Projection mode toggle (icon + text)
                    {
                        const char* projLabel = m_EditorCamera.IsPerspective()
                            ? ICON_FA_CUBE " Persp" : ICON_FA_BORDER_ALL " Ortho";
                        float projW = ImGui::CalcTextSize(projLabel).x
                                      + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
                        ImVec2 pMin(pjx, vpMin.y + 6);
                        ImVec2 pMax(pjx + projW, vpMin.y + 6 + btnH);
                        dl->AddRectFilled(pMin, pMax, IM_COL32(20, 20, 25, 180), 10.0f);
                        dl->AddRect(pMin, pMax, IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                        ImGui::SetCursorScreenPos(pMin);
                        ImGui::PushID("ProjToggle");
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f,0.65f,0.68f,1.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        if (ImGui::Button(projLabel, ImVec2(projW, btnH))) {
                            m_EditorCamera.SetPerspective(!m_EditorCamera.IsPerspective());
                            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("Projection: %s",
                                m_EditorCamera.IsPerspective() ? "Perspective" : "Orthographic");
                    }

                    // Camera speed button (shows speed text)
                    {
                        float spdW = btnW + 48.0f;  // enough for "⚡ 50.0"
                        float spdX = pjx + ImGui::CalcTextSize(
                            m_EditorCamera.IsPerspective()
                                ? ICON_FA_CUBE " Persp" : ICON_FA_BORDER_ALL " Ortho").x
                            + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f + 4;
                        ImVec2 cMin(spdX, vpMin.y + 6);
                        ImVec2 cMax(spdX + spdW, vpMin.y + 6 + btnH);
                        dl->AddRectFilled(cMin, cMax, IM_COL32(20, 20, 25, 180), 10.0f);
                        dl->AddRect(cMin, cMax, IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                        ImGui::SetCursorScreenPos(cMin);
                        ImGui::PushID("CamSpeed");
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f,0.65f,0.68f,1.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
                        char spdLabel[24];
                        snprintf(spdLabel, sizeof(spdLabel), "%s %.1f", ICON_FA_BOLT, m_CameraSpeed);
                        if (ImGui::Button(spdLabel, ImVec2(spdW, btnH))) {
                            closeOthers(&m_ShowViewportCameraSpeed);
                            m_ShowViewportCameraSpeed = !m_ShowViewportCameraSpeed;
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("Camera Speed");
                    }

                    // Camera speed slider window
                    if (m_ShowViewportCameraSpeed) {
                        float popW = 200.0f;
                        ImVec2 popPos(vpMin.x + 8 + (btnW + 4) * 2, vpMin.y + 6 + btnH + 4);
                        ImGui::SetNextWindowPos(popPos);
                        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.95f));
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
                        ImGui::SetNextWindowSize(ImVec2(popW, 0));
                        if (ImGui::Begin("##CamSpeedWin", &m_ShowViewportCameraSpeed,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::SetNextItemWidth(popW - 20.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                            ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);
                            ImGui::SliderFloat("##SpeedSlider", &m_CameraSpeed, 0.1f, 50.0f, "%.1f");
                            ImGui::PopStyleVar(2);
                        }
                        ImGui::End();
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor();
                    }

                    ImGui::PopFont();
                }

                // ---- Viewport options panel (toggled by Options button) ----
                if (m_ShowViewportOptions) {
                    float panelW = 352.0f, rowH = 32.0f;
                    float titleTop = 10.0f, titleH = 20.0f, sepGap = 10.0f, rowsTop = 8.0f, bottomPad = 10.0f;
                    int rowCount = 4;
                    float panelH = titleTop + titleH + sepGap + rowsTop + rowH * rowCount + bottomPad;
                    ImVec2 panelPos(vpMin.x + 8, vpMin.y + 6 + btnH + 4);
                    ImDrawList* odl = ImGui::GetWindowDrawList();
                    odl->AddRectFilled(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(20, 20, 25, 230), 8.0f);
                    odl->AddRect(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(10, 10, 15, 180), 8.0f, 0, 1.0f);

                    ImGui::SetNextWindowPos(panelPos);
                    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                    ImGui::Begin("##ViewportOptions", nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings);

                    ImGui::SetCursorPos(ImVec2(16.0f, titleTop));
                    ImGui::TextDisabled("Viewport Options");
                    ImGui::SetCursorPos(ImVec2(16.0f, titleTop + titleH + sepGap));
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f,0.35f,0.40f,0.60f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();

                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    struct ResEntry { const char* label; int w, h; };
                    ResEntry resolutions[] = {
                        {"1920 x 1080  (1080p)", 1920, 1080},
                        {"2560 x 1440  (2K)",    2560, 1440},
                        {"3840 x 2160  (4K)",    3840, 2160},
                        {"Fit Window",            0,    0},
                    };

                    auto resRow = [&](const char* label, bool active) {
                        float rowY = ImGui::GetCursorPosY();
                        ImVec2 rMin(panelPos.x, panelPos.y + rowY);
                        ImVec2 rMax(panelPos.x + panelW, panelPos.y + rowY + rowH);
                        bool hovered = ImGui::IsMouseHoveringRect(rMin, rMax);
                        if (hovered) {
                            odl->AddRectFilled(rMin, rMax, IM_COL32(0,112,255,100), 2.0f);
                        }
                        ImGui::SetCursorPos(ImVec2(0, rowY));
                        ImGui::InvisibleButton("##resHit", ImVec2(panelW, rowH));
                        // Active indicator
                        if (active) {
                            ImGui::SetCursorPos(ImVec2(16.0f, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                            ImGui::Text("%s", ICON_FA_CHECK);
                        }
                        ImGui::SetCursorPos(ImVec2(40.0f, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                        ImGui::Text("%s", label);
                        return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    };

                    for (auto& r : resolutions) {
                        ImGui::PushID(r.label);
                        bool active = (m_ViewportResW == r.w && m_ViewportResH == r.h);
                        if (resRow(r.label, active)) {
                            m_ViewportResW = r.w;
                            m_ViewportResH = r.h;
                            // Mark viewport dirty so RenderGraph rebuilds at new resolution
                            m_SceneRenderer->MarkViewportDirty();
                        }
                        ImGui::PopID();
                    }

                    ImGui::PopFont();
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }

                // ---- Viewport display filter panel (toggled by Show button) ----
                if (m_ShowViewportGizmos) {
                    float panelW = 352.0f;
                    float rowH = 32.0f;
                    float colCheckW = 32.0f, colIconW = 48.0f;
                    float colCheck = 16.0f, colIcon = colCheck + colCheckW;
                    float colText = colIcon + colIconW;
                    ImVec2 panelPos(vpMin.x + 8 + (btnW + 4), vpMin.y + 6 + btnH + 4);
                    int rowCount = 4;
                    float titleTop = 10.0f, titleH = 20.0f, sepGap = 10.0f, rowsTop = 8.0f, bottomPad = 10.0f;
                    float panelH = titleTop + titleH + sepGap + rowsTop + rowH * rowCount + bottomPad;
                    ImDrawList* fdl = ImGui::GetWindowDrawList();
                    fdl->AddRectFilled(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(20, 20, 25, 230), 8.0f);
                    fdl->AddRect(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(10, 10, 15, 180), 8.0f, 0, 1.0f);

                    ImGui::SetNextWindowPos(panelPos);
                    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                    ImGui::Begin("##ViewportFilters", nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings);

                    // Title
                    ImGui::SetCursorPos(ImVec2(colCheck, titleTop));
                    ImGui::TextDisabled("Viewport Display");
                    ImGui::SetCursorPos(ImVec2(colCheck, titleTop + titleH + sepGap));
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f,0.35f,0.40f,0.60f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();

                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    auto filterRow = [&](const char* icon, const char* label, bool* val) {
                        ImGui::PushID(label);
                        float rowY = ImGui::GetCursorPosY();
                        ImVec2 rowMin(panelPos.x, panelPos.y + rowY);
                        ImVec2 rowMax(panelPos.x + panelW, panelPos.y + rowY + rowH);

                        bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
                        if (hovered) {
                            fdl->AddRectFilled(rowMin, rowMax,
                                IM_COL32(0, 112, 255, 100), 2.0f);
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                                *val = !*val;
                        }

                        ImGui::SetCursorPos(ImVec2(0, rowY));
                        ImGui::InvisibleButton("##hit", ImVec2(panelW, rowH));

                        if (*val) {
                            ImGui::SetCursorPos(ImVec2(colCheck, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                            ImGui::Text("%s", ICON_FA_CHECK);
                        }
                        ImVec2 isz = ImGui::CalcTextSize(icon);
                        ImGui::SetCursorPos(ImVec2(colIcon + (colIconW - isz.x) * 0.5f,
                            rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                        ImGui::Text("%s", icon);
                        ImGui::SetCursorPos(ImVec2(colText, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                        ImGui::Text("%s", label);

                        ImGui::PopID();
                    };

                    filterRow(ICON_FA_BORDER_ALL,   "Grid",   &m_ShowGrid);
                    filterRow(ICON_FA_VIDEO,         "Camera", &m_ShowCameraGizmos);
                    filterRow(ICON_FA_LIGHTBULB,     "Light",  &m_ShowLightGizmos);
                    filterRow(ICON_FA_OBJECT_GROUP,  "UI",     &m_ShowUIGizmos);

                    ImGui::PopFont();
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }

                // ---- Gizmo mode toolbar (top-right) ----
                {
                    float btnW = 38.0f, btnH = 32.0f;
                    int btnCount = 4;
                    ImVec2 gizmoStart(vpMin.x + vpSize.x - btnW - btnW * btnCount - 8 - 4, vpMin.y + 6);
                    ImDrawList* dl2 = ImGui::GetWindowDrawList();
                    // Continuous background behind all 4 mode buttons
                    ImVec2 gEnd(gizmoStart.x + btnW * btnCount, gizmoStart.y + btnH);
                    dl2->AddRectFilled(gizmoStart, gEnd, IM_COL32(20, 20, 25, 180), 10.0f);
                    dl2->AddRect(gizmoStart, gEnd, IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                    ImVec4 activeTxt   = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
                    ImVec4 inactiveTxt = ImVec4(0.65f, 0.65f, 0.68f, 1.0f);
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                    ImGui::SetCursorScreenPos(gizmoStart);

                    auto modeBtn = [&](const char* icon, int mode, const char* tip) {
                        bool active = (m_GizmoType == mode);
                        if (active) {
                            ImVec2 bMin = ImGui::GetCursorScreenPos();
                            dl2->AddRectFilled(bMin, ImVec2(bMin.x + btnW, bMin.y + btnH),
                                IM_COL32(48, 88, 145, 180), 10.0f);
                        }
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, active ? activeTxt : inactiveTxt);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        bool pressed = ImGui::Button(icon, ImVec2(btnW, btnH));
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        if (pressed) m_GizmoType = (m_GizmoType == mode) ? -1 : mode;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("%s", tip);
                        ImGui::SameLine(0, 0);
                    };

                    modeBtn(ICON_FA_MOUSE_POINTER, -1,                      "Select (Q)");
                    modeBtn(ICON_FA_ARROWS_ALT,    ImGuizmo::OPERATION::TRANSLATE, "Translate (W)");
                    modeBtn(ICON_FA_SYNC_ALT,      ImGuizmo::OPERATION::ROTATE,    "Rotate (E)");
                    modeBtn(ICON_FA_EXPAND_ALT,    ImGuizmo::OPERATION::SCALE,     "Scale (R)");

                    ImGui::PopFont();
                }

                // ---- Stats button (far right edge) ----
                {
                    float btnW = 38.0f, btnH = 32.0f;
                    ImVec2 sp(vpMin.x + vpSize.x - btnW - 8, vpMin.y + 6);
                    ImDrawList* dl3 = ImGui::GetWindowDrawList();
                    dl3->AddRectFilled(sp, ImVec2(sp.x + btnW, sp.y + btnH),
                        m_ShowViewportStats ? IM_COL32(48, 88, 145, 180) : IM_COL32(20, 20, 25, 180), 10.0f);
                    dl3->AddRect(sp, ImVec2(sp.x + btnW, sp.y + btnH), IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                    ImGui::SetCursorScreenPos(sp);
                    ImVec4 atxt  = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
                    ImVec4 itxt  = ImVec4(0.65f, 0.65f, 0.68f, 1.0f);
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                    ImGui::PushID("PerfStats");
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_Text, m_ShowViewportStats ? atxt : itxt);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                    if (ImGui::Button(ICON_FA_CHART_LINE, ImVec2(btnW, btnH)))
                        m_ShowViewportStats = !m_ShowViewportStats;
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                    ImGui::PopFont();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Performance Stats");
                }

                // ---- Stats overlay (matching Game viewport panel) ----
                if (m_ShowViewportStats) {
                    static ImVec2 s_EditorStatsSize = ImVec2(375.0f, 340.0f);
                    ImGui::SetCursorScreenPos(ImVec2(vpMin.x + vpSize.x - s_EditorStatsSize.x - 8,
                        vpMin.y + 6 + btnH + pad * 2 + 4));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
                    ImGui::BeginChild("EditorStatsOverlay", s_EditorStatsSize, false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                    ImGui::SetCursorPosY(10.0f);
                    ImGui::Indent(10.0f);

                    auto& io = ImGui::GetIO();
                    auto boldFont = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0];
                    float memoryMB = GetPhysicalMemoryUsageMB();
                    float uiScale = io.FontGlobalScale;
                    float alignOffset = 100.0f * uiScale;

                    const auto& stats = m_SceneRenderer->GetStats();

                    // Graphics
                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Graphics");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("%.1f FPS (%.1f ms)", io.Framerate, 1000.0f / io.Framerate);
                    ImGui::Text("CPU Time:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "%8.2f ms", stats.CPUTime);
                    ImGui::Text("GPU Time:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.2f, 1.0f), "%8.2f ms", stats.GPUTime);
                    ImGui::Text("RAM Usage:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.9f, 1.0f), "%8.1f MB", memoryMB);
                    if (m_ViewportResW > 0 && m_ViewportResH > 0)
                        ImGui::Text("Screen Size: %dx%d (custom)", m_ViewportResW, m_ViewportResH);
                    else
                        ImGui::Text("Screen Size: %dx%d", (int)m_ViewportSize.x, (int)m_ViewportSize.y);
                    ImGui::Spacing();

                    // Rendering
                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Rendering");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("Draw Calls: %d", stats.DrawCalls);
                    ImGui::Text("Shader Binds: %d", stats.ShaderBinds);
                    ImGui::Spacing();

                    // Geometry
                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Geometry");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("Triangle Count: %d", stats.TriangleCount);
                    ImGui::SameLine(0.0f, 15.0f * uiScale);
                    ImGui::Text("Vertex Count: %d", stats.VertexCount);

                    if (m_ActiveScene) {
                        size_t entityCount = 0;
                        auto view = m_ActiveScene->Reg().view<IDComponent>();
                        for (auto e : view) entityCount++;
                        ImGui::Text("Active Entities: %zu", entityCount);
                    }

                    ImGui::Unindent(10.0f);

                    float currentLineHeight = ImGui::GetTextLineHeight();
                    s_EditorStatsSize.x = 375.0f * (currentLineHeight / 16.0f);
                    s_EditorStatsSize.y = ImGui::GetCursorPosY() + 10.0f;

                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }

            } else {
                ImGui::Text("Viewport is initializing...");
            }
        }

        // ---- Global F-key focus (Unity-style: works regardless of window focus,
        //      as long as an entity is selected in the Scene Hierarchy) ----
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            Entity sel = m_SceneHierarchyPanel.GetSelectedEntity();
            if (sel) {
                glm::vec3 targetPos = glm::vec3(sel.GetWorldTransform()[3]);
                glm::vec3 dir = targetPos - m_EditorCamera.GetPosition();
                float dirLen = glm::length(dir);
                if (dirLen < 0.01f)
                    dir = glm::vec3(2.0f, 1.5f, 3.0f);
                glm::vec3 forward = glm::normalize(dir);

                float pitch = glm::asin(glm::clamp(forward.y, -1.0f, 1.0f));
                float yaw   = std::atan2(-forward.x, -forward.z);

                m_EditorCamera.SetFocalPoint(targetPos);
                m_EditorCamera.SetPitch(pitch);
                m_EditorCamera.SetYaw(yaw);
                m_EditorCamera.SetDistance(5.0f);
                m_EditorCamera.UpdateCameraView();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UIRenderGameViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Game");

        // Close Viewport overlays when Game tab is focused
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            m_ShowViewportOptions     = false;
            m_ShowViewportGizmos       = false;
            m_ShowViewportCameraSpeed  = false;
        }

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_GameViewportSize = { std::floor(viewportPanelSize.x), std::floor(viewportPanelSize.y) };
        ImVec2 cursorStartPos = ImGui::GetCursorPos();

        // ==========================================
        // Game Viewport Image (with letterboxing for custom resolution)
        // ==========================================
        if (m_GameRenderer) {
            void* textureID = m_GameRenderer->GetFinalColorAttachmentRendererID();
            if (textureID) {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImVec2 vpSize = avail;
                ImVec2 imageOffset(0, 0);

                // Letterbox when a custom resolution is active
                if (m_GameViewportResW > 0 && m_GameViewportResH > 0) {
                    float aspect = (float)m_GameViewportResW / (float)m_GameViewportResH;
                    if (avail.x / avail.y > aspect) {
                        vpSize.x = avail.y * aspect;
                        vpSize.y = avail.y;
                        imageOffset.x = (avail.x - vpSize.x) * 0.5f;
                    } else {
                        vpSize.x = avail.x;
                        vpSize.y = avail.x / aspect;
                        imageOffset.y = (avail.y - vpSize.y) * 0.5f;
                    }
                    ImGui::SetCursorPos(cursorStartPos + imageOffset);
                }

                ImGui::Image(textureID, vpSize, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

                // UI overlay (must match letterboxed position)
                ImVec2 gameVpScreenMin = ImGui::GetItemRectMin();
                void* uiTexID = m_GameRenderer->GetBlackboardTextureID("UI");
                if (uiTexID) {
                    ImGui::SetCursorScreenPos(gameVpScreenMin);
                    ImGui::Image(uiTexID, vpSize, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
                }
                if (m_ShowUIGizmos) UIRenderDebugUIGizmos(gameVpScreenMin, vpSize,
                    ImGui::GetIO().DisplayFramebufferScale.x);

                // Anchor: viewport image top-left corner (matching Editor viewport toolbar pattern)
                ImVec2 vpMin = ImGui::GetItemRectMin();
                float btnW = 38.0f, btnH = 32.0f;
                { // Game Viewport Toolbar Overlay block
                    // ---- Top-left: [Options] button ----
                    ImVec4 inactiveTxt = ImVec4(0.65f, 0.65f, 0.68f, 1.0f);
                    ImVec4 activeTxt   = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 borderCol = IM_COL32(10, 10, 15, 180);
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    auto oneBtn = [&](float x, const char* icon, const char* tip, bool* toggle) {
                        bool active = toggle ? *toggle : false;
                        ImVec2 bMin(x, vpMin.y + 6);
                        ImVec2 bMax(x + btnW, vpMin.y + 6 + btnH);
                        dl->AddRectFilled(bMin, bMax,
                            active ? IM_COL32(48, 88, 145, 180) : IM_COL32(20, 20, 25, 180), 10.0f);
                        dl->AddRect(bMin, bMax, borderCol, 10.0f, 0, 1.0f);
                        ImGui::SetCursorScreenPos(bMin);
                        ImGui::PushID(tip);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, active ? activeTxt : inactiveTxt);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        bool pressed = ImGui::Button(icon, ImVec2(btnW, btnH));
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        if (pressed && toggle) *toggle = !*toggle;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("%s", tip);
                    };

                    float ox  = vpMin.x + 8;
                    float pjx = ox  + btnW + 4;
                    oneBtn(ox, ICON_FA_BARS, "Options", &m_ShowGameOptions);
                    if (m_ShowGameOptions) {
                        m_ShowViewportOptions = false;
                        m_ShowViewportGizmos   = false;
                        m_ShowViewportCameraSpeed = false;
                    }

                    // Projection mode toggle (icon + text)
                    {
                        bool isPersp = true;
                        if (m_ActiveScene) {
                            auto camView = m_ActiveScene->Reg().view<CameraComponent>();
                            for (auto e : camView) {
                                auto& cc = camView.get<CameraComponent>(e);
                                isPersp = (cc.Camera.GetProjectionType()
                                           == SceneCamera::ProjectionType::Perspective);
                                break;
                            }
                        }

                        const char* projLabel = isPersp
                            ? ICON_FA_CUBE " Persp" : ICON_FA_BORDER_ALL " Ortho";
                        float projW = ImGui::CalcTextSize(projLabel).x
                                      + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
                        ImVec2 pMin(pjx, vpMin.y + 6);
                        ImVec2 pMax(pjx + projW, vpMin.y + 6 + btnH);
                        dl->AddRectFilled(pMin, pMax, IM_COL32(20, 20, 25, 180), 10.0f);
                        dl->AddRect(pMin, pMax, IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                        ImGui::SetCursorScreenPos(pMin);
                        ImGui::PushID("GameProjToggle");
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f,0.65f,0.68f,1.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        if (ImGui::Button(projLabel, ImVec2(projW, btnH)) && m_ActiveScene) {
                            auto camView = m_ActiveScene->Reg().view<CameraComponent>();
                            for (auto e : camView) {
                                auto& cc = camView.get<CameraComponent>(e);
                                cc.Camera.SetProjectionType(isPersp
                                    ? SceneCamera::ProjectionType::Orthographic
                                    : SceneCamera::ProjectionType::Perspective);
                                break;
                            }
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("Projection: %s",
                                isPersp ? "Perspective" : "Orthographic");
                    }

                    ImGui::PopFont();
                }

                // ---- Game Viewport Resolution Panel ----
                if (m_ShowGameOptions) {
                    float panelW = 352.0f, rowH = 32.0f;
                    float titleTop = 10.0f, titleH = 20.0f, sepGap = 10.0f;
                    float rowsTop = 8.0f, bottomPad = 10.0f;
                    int rowCount = 4;
                    float panelH = titleTop + titleH + sepGap + rowsTop + rowH * rowCount + bottomPad;
                    ImVec2 panelPos(vpMin.x + 8, vpMin.y + 6 + btnH + 4);
                    ImDrawList* odl = ImGui::GetWindowDrawList();
                    odl->AddRectFilled(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(20, 20, 25, 230), 8.0f);
                    odl->AddRect(panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH),
                        IM_COL32(10, 10, 15, 180), 8.0f, 0, 1.0f);

                    ImGui::SetNextWindowPos(panelPos);
                    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
                    ImGui::Begin("##GameViewportOptions", nullptr,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoSavedSettings);

                    ImGui::SetCursorPos(ImVec2(16.0f, titleTop));
                    ImGui::TextDisabled("Game Viewport Options");
                    ImGui::SetCursorPos(ImVec2(16.0f, titleTop + titleH + sepGap));
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.30f,0.35f,0.40f,0.60f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();

                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);

                    struct ResEntry { const char* label; int w, h; };
                    ResEntry resolutions[] = {
                        {"1920 x 1080  (1080p)", 1920, 1080},
                        {"2560 x 1440  (2K)",    2560, 1440},
                        {"3840 x 2160  (4K)",    3840, 2160},
                        {"Fit Window",            0,    0},
                    };

                    auto resRow = [&](const char* label, bool active) {
                        float rowY = ImGui::GetCursorPosY();
                        ImVec2 rMin(panelPos.x, panelPos.y + rowY);
                        ImVec2 rMax(panelPos.x + panelW, panelPos.y + rowY + rowH);
                        bool hovered = ImGui::IsMouseHoveringRect(rMin, rMax);
                        if (hovered) {
                            odl->AddRectFilled(rMin, rMax, IM_COL32(0,112,255,100), 2.0f);
                        }
                        ImGui::SetCursorPos(ImVec2(0, rowY));
                        ImGui::InvisibleButton("##resHit", ImVec2(panelW, rowH));
                        if (active) {
                            ImGui::SetCursorPos(ImVec2(16.0f, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                            ImGui::Text("%s", ICON_FA_CHECK);
                        }
                        ImGui::SetCursorPos(ImVec2(40.0f, rowY + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
                        ImGui::Text("%s", label);
                        return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                    };

                    for (auto& r : resolutions) {
                        ImGui::PushID(r.label);
                        bool active = (m_GameViewportResW == r.w && m_GameViewportResH == r.h);
                        if (resRow(r.label, active)) {
                            m_GameViewportResW = r.w;
                            m_GameViewportResH = r.h;
                            m_GameRenderer->MarkViewportDirty();
                        }
                        ImGui::PopID();
                    }

                    ImGui::PopFont();
                    ImGui::End();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }

                { // ---- Top-right: [Stats] button ----
                    ImVec2 sp(vpMin.x + vpSize.x - btnW - 8, vpMin.y + 6);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(sp, ImVec2(sp.x + btnW, sp.y + btnH),
                        m_ShowGameStats ? IM_COL32(48, 88, 145, 180) : IM_COL32(20, 20, 25, 180), 10.0f);
                    dl->AddRect(sp, ImVec2(sp.x + btnW, sp.y + btnH),
                        IM_COL32(10, 10, 15, 180), 10.0f, 0, 1.0f);
                    ImGui::SetCursorScreenPos(sp);
                    ImVec4 atxt = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);
                    ImVec4 itxt = ImVec4(0.65f, 0.65f, 0.68f, 1.0f);
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
                    ImGui::PushID("GamePerfStats");
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_Text, m_ShowGameStats ? atxt : itxt);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                    if (ImGui::Button(ICON_FA_CHART_LINE, ImVec2(btnW, btnH)))
                        m_ShowGameStats = !m_ShowGameStats;
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                    ImGui::PopFont();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Performance Stats");
                }

                // ---- Game Stats Overlay ----
                if (m_ShowGameStats) {
                    static ImVec2 s_OverlaySize = ImVec2(375.0f, 340.0f);

                    ImVec2 vpMinRel = ImVec2(vpMin.x - ImGui::GetWindowPos().x, vpMin.y - ImGui::GetWindowPos().y);
                    float statsY = vpMinRel.y + 6.0f + 32.0f + 3.0f * 2.0f + 4.0f;
                    ImGui::SetCursorPos(ImVec2(vpMinRel.x + vpSize.x - s_OverlaySize.x - 10.0f, statsY));

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);

                    ImGui::BeginChild("StatsOverlay", s_OverlaySize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                    ImGui::SetCursorPosY(10.0f);
                    ImGui::Indent(10.0f);

                    auto& io = ImGui::GetIO();
                    auto boldFont = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0];

                    float memoryMB = GetPhysicalMemoryUsageMB();
                    float uiScale = io.FontGlobalScale;
                    float alignOffset = 100.0f * uiScale;

                    // --- Graphics ---
                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Graphics");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("%.1f FPS (%.1f ms)", io.Framerate, 1000.0f / io.Framerate);
                    ImGui::Text("CPU Time:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.2f, 1.0f), "%8.2f ms", m_GameStats.CPUTime);
                    ImGui::Text("GPU Time:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.2f, 1.0f), "%8.2f ms", m_GameStats.GPUTime);
                    ImGui::Text("RAM Usage:"); ImGui::SameLine(alignOffset);
                    ImGui::TextColored(ImVec4(0.2f, 0.7f, 0.9f, 1.0f), "%8.1f MB", memoryMB);
                    ImGui::Text("Screen Size: %dx%d",
                        (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                            ? m_GameViewportResW : (int)m_GameViewportSize.x,
                        (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                            ? m_GameViewportResH : (int)m_GameViewportSize.y);
                    ImGui::Spacing();

                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Rendering");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("Draw Calls:"); ImGui::SameLine(alignOffset);
                    ImGui::Text("%d", m_GameStats.DrawCalls);
                    ImGui::Text("Shader Binds:"); ImGui::SameLine(alignOffset);
                    ImGui::Text("%d", m_GameStats.ShaderBinds);
                    ImGui::Spacing();

                    ImGui::PushFont(boldFont);
                    ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Geometry");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Text("Triangles:"); ImGui::SameLine(alignOffset);
                    ImGui::Text("%d", m_GameStats.TriangleCount);
                    ImGui::Text("Vertices:"); ImGui::SameLine(alignOffset);
                    ImGui::Text("%d", m_GameStats.VertexCount);

                    if (m_ActiveScene) {
                        size_t entityCount = 0;
                        auto view = m_ActiveScene->Reg().view<IDComponent>();
                        for (auto e : view) entityCount++;
                        ImGui::Text("Active Entities:"); ImGui::SameLine(alignOffset);
                        ImGui::Text("%zu", entityCount);
                    }

                    ImGui::Unindent(10.0f);

                    // Dynamic sizing for stats overlay
                    float currentLineHeight = ImGui::GetTextLineHeight();
                    s_OverlaySize.x = 375.0f * (currentLineHeight / 16.0f);
                    s_OverlaySize.y = ImGui::GetCursorPosY() + 10.0f;

                    ImGui::EndChild();

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                }
            } else {
                ImGui::Text("Game is initializing...");
            }
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

            // ==========================================
            // 【终极进化】：兼容正交(Ortho)和透视(Perspective)的万能射线生成法
            // ==========================================
            glm::mat4 inverseProj = glm::inverse(cameraProjectionMatrix);
            glm::mat4 inverseView = glm::inverse(cameraViewMatrix);
            
            glm::vec4 rayStartNDC = glm::vec4(nx, ny, -1.0f, 1.0f);
            glm::vec4 rayEndNDC = glm::vec4(nx, ny, 1.0f, 1.0f);

            glm::vec4 rayStartWorld = inverseView * inverseProj * rayStartNDC;
            rayStartWorld /= rayStartWorld.w;

            glm::vec4 rayEndWorld = inverseView * inverseProj * rayEndNDC;
            rayEndWorld /= rayEndWorld.w;

            glm::vec3 rayWorldDir = glm::normalize(glm::vec3(rayEndWorld - rayStartWorld));
            glm::vec3 rayOrigin = glm::vec3(rayStartWorld);

            float closestT = std::numeric_limits<float>::max();

            // ==========================================
            // 1. 射线检测：3D 网格模型 (Mesh Renderer)
            // ==========================================
            auto renderGroup = m_ActiveScene->Reg().view<TransformComponent, MeshRendererComponent>();
            for (auto entityID : renderGroup) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                auto& meshComp = entity.GetComponent<MeshRendererComponent>();
                auto model = AssetManager::GetAsset<Model>(meshComp.ModelHandle);
                if (!model) continue; 

                glm::mat4 transform = entity.GetWorldTransform();
                
                // 防御性编程：防止物体的某个轴缩放为 0 导致逆矩阵崩溃产生 NaN
                if (std::abs(glm::determinant(transform)) < 0.00001f) continue;
                
                glm::mat4 inverseTransform = glm::inverse(transform);
                glm::vec3 localRayOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
                glm::vec3 localRayDir = glm::normalize(glm::vec3(inverseTransform * glm::vec4(rayWorldDir, 0.0f)));

                for (auto& mesh : model->GetMeshes()) {
                    const auto& aabb = mesh->GetAABB(); 

                    // 健壮的 AABB 相交测试（防除零引发的 INF 陷阱）
                    float tmin = 0.0f;
                    float tmax = std::numeric_limits<float>::max();
                    
                    glm::vec3 minAABB = aabb.Min;
                    glm::vec3 maxAABB = aabb.Max;
                    
                    bool hit = true;
                    for (int i = 0; i < 3; ++i) {
                        if (std::abs(localRayDir[i]) < 0.00001f) {
                            // 射线平行于该平面，检查起点是否在范围内
                            if (localRayOrigin[i] < minAABB[i] || localRayOrigin[i] > maxAABB[i]) {
                                hit = false; break;
                            }
                        } else {
                            float t1 = (minAABB[i] - localRayOrigin[i]) / localRayDir[i];
                            float t2 = (maxAABB[i] - localRayOrigin[i]) / localRayDir[i];
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = glm::max(tmin, t1);
                            tmax = glm::min(tmax, t2);
                            if (tmin > tmax) { hit = false; break; }
                        }
                    }

                    if (hit && tmax >= 0.0f) {
                        glm::vec3 localIntersect = localRayOrigin + localRayDir * tmin;
                        glm::vec3 worldIntersect = glm::vec3(transform * glm::vec4(localIntersect, 1.0f));
                        float worldDistance = glm::length(worldIntersect - rayOrigin);

                        if (worldDistance < closestT) {
                            closestT = worldDistance;
                            m_HoveredEntity = entity;
                        }
                    }
                }
            }

            // ==========================================
            // 2. 射线检测：2D 精灵 (Sprite Renderer)
            // ==========================================
            auto spriteGroup = m_ActiveScene->Reg().view<TransformComponent, SpriteRendererComponent>();
            for (auto entityID : spriteGroup) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                glm::mat4 transform = entity.GetWorldTransform();
                
                // 【绝妙修复】：2D 游戏的美术很容易把 Z 缩放拖成 0！
                // 遇到不可逆矩阵，我们强行解构，给 Z 轴一个极小的厚度并重组矩阵，保证运算不崩溃！
                if (std::abs(glm::determinant(transform)) < 0.00001f) {
                    glm::vec3 scale, translation, skew;
                    glm::quat rotation;
                    glm::vec4 perspective;
                    glm::decompose(transform, scale, rotation, translation, skew, perspective);
                    if (std::abs(scale.x) < 0.0001f) scale.x = 0.0001f;
                    if (std::abs(scale.y) < 0.0001f) scale.y = 0.0001f;
                    if (std::abs(scale.z) < 0.0001f) scale.z = 0.0001f; // 强行拉高 Z！
                    transform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
                }

                glm::mat4 inverseTransform = glm::inverse(transform);
                glm::vec3 localRayOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
                glm::vec3 localRayDir = glm::normalize(glm::vec3(inverseTransform * glm::vec4(rayWorldDir, 0.0f)));

                // 【核心算法】：真正的 Ray-Quad (射线-平面) 相交算法！
                // Sprite 永远渲染在自己局部坐标系的 Z=0 平面上
                if (std::abs(localRayDir.z) > 0.00001f) {
                    // 计算射线命中 Z=0 平面的距离 t
                    float t = -localRayOrigin.z / localRayDir.z;
                    if (t >= 0.0f) {
                        glm::vec3 localIntersect = localRayOrigin + localRayDir * t;
                        // 判断穿透点是否落在 -0.5 到 0.5 的图片框范围内
                        if (localIntersect.x >= -0.5f && localIntersect.x <= 0.5f &&
                            localIntersect.y >= -0.5f && localIntersect.y <= 0.5f) {
                            
                            glm::vec3 worldIntersect = glm::vec3(transform * glm::vec4(localIntersect, 1.0f));
                            float worldDistance = glm::length(worldIntersect - rayOrigin);

                            if (worldDistance < closestT) {
                                closestT = worldDistance;
                                m_HoveredEntity = entity;
                            }
                        }
                    }
                }
            }

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemHovered()) {  // don't deselect when clicking toolbar buttons
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

        // ==========================================
        // 【核心魔法】：Gizmo 状态拦截器
        // ==========================================
        static bool s_IsDragging = false;
        static TransformComponent s_OldTransform;

        if (ImGuizmo::IsUsing()) {
            // 拦截 1：鼠标刚按下 Gizmo 的第一帧，备份旧状态！
            if (!s_IsDragging) {
                s_OldTransform = tc;
                s_IsDragging = true;
            }

            // 实时应用矩阵变换
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
        else {
            // 拦截 2：松开鼠标的第一帧，打包并推送撤回命令！
            if (s_IsDragging) {
                s_IsDragging = false;
                
                // 动态生成高级命令名字
                std::string actionName = "Modify Transform";
                std::string entityName = selectedEntity.GetComponent<TagComponent>().Tag;
                if (m_GizmoType == ImGuizmo::OPERATION::TRANSLATE) actionName = "Translate '" + entityName + "'";
                else if (m_GizmoType == ImGuizmo::OPERATION::ROTATE) actionName = "Rotate '" + entityName + "'";
                else if (m_GizmoType == ImGuizmo::OPERATION::SCALE) actionName = "Scale '" + entityName + "'";

                auto macroCmd = std::make_shared<MacroCommand>(actionName);
                macroCmd->AddCommand(std::make_shared<ChangeComponentCommand<TransformComponent>>(
                    selectedEntity, s_OldTransform, tc
                ));
                
                m_CommandHistory.AddCommand(macroCmd);
            }
        }
    }

    void EditorLayer::UIRenderDebugGizmos(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        glm::mat4 viewProj = cameraProjectionMatrix * cameraViewMatrix;
        ImVec2 mousePos = ImGui::GetMousePos();
        bool canClick = m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        Entity bestClick;
        float bestClickDist = FLT_MAX;

        auto ProjectToScreen = [&](const glm::vec3& worldPos, ImVec2& outScreenPos, float* outDepth = nullptr) -> bool {
            glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
            if (clipPos.w < 0.01f) return false;
            glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
            if (outDepth) *outDepth = ndcPos.z;
            float vpW = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
            float vpH = m_ViewportBounds[1].y - m_ViewportBounds[0].y;
            outScreenPos.x = m_ViewportBounds[0].x + (ndcPos.x + 1.0f) * 0.5f * vpW;
            outScreenPos.y = m_ViewportBounds[0].y + (1.0f - ndcPos.y) * 0.5f * vpH;
            return true;
        };

        // Helper: draw an icon and check if the mouse clicked inside it.
        auto DrawGizmoIcon = [&](Entity entity, const ImVec2& pos, float size,
                                  std::shared_ptr<Texture2D>& tex, ImU32 tint) {
            if (!tex) return;
            float s = size * 0.5f;
            drawList->AddImage((ImTextureID)tex->GetImGuiTextureID(),
                ImVec2(pos.x - s, pos.y - s), ImVec2(pos.x + s, pos.y + s),
                ImVec2(0, 1), ImVec2(1, 0), tint);
            if (canClick && mousePos.x >= pos.x - s && mousePos.x <= pos.x + s &&
                mousePos.y >= pos.y - s && mousePos.y <= pos.y + s) {
                float depth;
                ImVec2 dummy;
                ProjectToScreen(glm::vec3(entity.GetWorldTransform()[3]), dummy, &depth);
                if (depth < bestClickDist) { bestClickDist = depth; bestClick = entity; }
            }
        };

        // ==========================================
        // 1. Cameras: frustum + icon
        // ==========================================
        if (m_ShowCameraGizmos) {
            auto view = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                auto& cameraComp = entity.GetComponent<CameraComponent>();
                glm::mat4 transform = entity.GetWorldTransform();
                glm::mat4 proj = cameraComp.Camera.GetProjection();
                glm::mat4 invViewProj = transform * glm::inverse(proj);

                glm::vec3 fc[8] = {
                    {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
                    {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}};
                ImVec2 sp[8]; bool v[8];
                for (int i = 0; i < 8; i++) {
                    glm::vec4 wp = invViewProj * glm::vec4(fc[i], 1.0f); wp /= wp.w;
                    v[i] = ProjectToScreen(glm::vec3(wp), sp[i]);
                }
                ImU32 col = IM_COL32(200, 200, 200, 200);
                auto DL = [&](int a, int b) { if (v[a] && v[b]) drawList->AddLine(sp[a], sp[b], col); };
                DL(0,1); DL(1,2); DL(2,3); DL(3,0); DL(4,5); DL(5,6); DL(6,7); DL(7,4);
                DL(0,4); DL(1,5); DL(2,6); DL(3,7);

                glm::vec3 camPos = glm::vec3(transform[3]);
                ImVec2 iconPos;
                if (ProjectToScreen(camPos, iconPos))
                    DrawGizmoIcon(entity, iconPos, 44.0f, m_CameraIcon,
                                  IM_COL32(255, 255, 255, 255));
            }
        }

        // ==========================================
        // 2a. Directional Light: sun icon
        // ==========================================
        if (m_ShowLightGizmos) {
            auto view = m_ActiveScene->Reg().view<TransformComponent, DirectionalLightComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;
                glm::vec3 pos = entity.GetWorldTransform()[3];
                ImVec2 sp;
                if (ProjectToScreen(pos, sp))
                    DrawGizmoIcon(entity, sp, 48.0f, m_DirLightIcon,
                                  IM_COL32(255, 255, 255, 255));
            }
        }

        // ==========================================
        // 2b. Point Light: lightbulb icon + rings
        // ==========================================
        if (m_ShowLightGizmos) {
            auto view = m_ActiveScene->Reg().view<TransformComponent, PointLightComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                auto& lightComp = entity.GetComponent<PointLightComponent>();
                glm::vec3 worldPos = entity.GetWorldTransform()[3];

                ImVec2 screenPos;
                if (ProjectToScreen(worldPos, screenPos)) {
                    ImU32 tint = IM_COL32((int)(lightComp.Color.r*255), (int)(lightComp.Color.g*255), (int)(lightComp.Color.b*255), 255);
                    DrawGizmoIcon(entity, screenPos, 40.0f, m_PointLightIcon, tint);
                }

                // Wireframe rings
                ImU32 ringColor = IM_COL32((int)(lightComp.Color.r*255), (int)(lightComp.Color.g*255), (int)(lightComp.Color.b*255), 25);
                float radius = lightComp.Radius;
                const int segments = 48;
                for (int plane = 0; plane < 3; plane++) {
                    ImVec2 prev; bool prevOk = false;
                    for (int i = 0; i <= segments; i++) {
                        float a = (float)i / (float)segments * 2.0f * 3.14159265f;
                        glm::vec3 off;
                        if (plane == 0)      off = glm::vec3(cosf(a), sinf(a), 0) * radius;
                        else if (plane == 1) off = glm::vec3(cosf(a), 0, sinf(a)) * radius;
                        else                 off = glm::vec3(0, cosf(a), sinf(a)) * radius;
                        ImVec2 cur; bool curOk = ProjectToScreen(worldPos + off, cur);
                        if (i > 0 && prevOk && curOk) drawList->AddLine(prev, cur, ringColor);
                        prev = cur; prevOk = curOk;
                    }
                }
            }
        }

        // Apply click selection (closest entity to camera wins)
        if (bestClick)
            m_SceneHierarchyPanel.SetSelectedEntity(bestClick);
    }

    void EditorLayer::UIRenderDebugUIGizmos(ImVec2 viewportScreenMin, ImVec2 vpSize, float dpiScale) {
        if (!m_ShowUIGizmos || !m_ActiveScene) return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float invScale = 1.0f / dpiScale;
        auto view = m_ActiveScene->Reg().view<RectTransformComponent>();
        for (auto entity : view) {
            auto& rt = view.get<RectTransformComponent>(entity);

            // ScreenMin/Max 是物理像素, 需除以 dpiScale 转为 ImGui 逻辑坐标
            ImVec2 minPos = ImVec2(viewportScreenMin.x + rt.ScreenMin.x * invScale,
                                   viewportScreenMin.y + rt.ScreenMin.y * invScale);
            ImVec2 maxPos = ImVec2(viewportScreenMin.x + rt.ScreenMax.x * invScale,
                                   viewportScreenMin.y + rt.ScreenMax.y * invScale);

            // 红色包围盒
            drawList->AddRect(minPos, maxPos, IM_COL32(255, 0, 0, 255), 0.0f, 0, 2.0f);

            // 蓝色 Pivot 轴心点 (从 HierarchyTransform 第 4 列提取世界坐标)
            glm::vec3 pivotWorld = glm::vec3(rt.HierarchyTransform[3]);
            ImVec2 pivotPos = ImVec2(viewportScreenMin.x + pivotWorld.x * invScale,
                                     viewportScreenMin.y + pivotWorld.y * invScale);
            drawList->AddCircleFilled(pivotPos, 4.0f, IM_COL32(0, 200, 255, 255));

            // 黄色十字标出元素原点
            drawList->AddLine(ImVec2(pivotPos.x - 6, pivotPos.y),
                              ImVec2(pivotPos.x + 6, pivotPos.y),
                              IM_COL32(255, 255, 0, 200), 1.5f);
            drawList->AddLine(ImVec2(pivotPos.x, pivotPos.y - 6),
                              ImVec2(pivotPos.x, pivotPos.y + 6),
                              IM_COL32(255, 255, 0, 200), 1.5f);
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
        std::string tempPath = VFS::ResolveString("project://temp/temp_play_scene.ayaya");
        std::filesystem::path dirPath = std::filesystem::path(tempPath).parent_path();
        if (!std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }
        serializer.Serialize(tempPath, dummyState);
        SceneSerializer deserializer(m_ActiveScene);
        deserializer.Deserialize(tempPath, dummyState);

        // 2. 更新面板上下文
        m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);

        // ==========================================
        // 核心修复 3：强行重置克隆出来的玩家相机的视口比例和模式！
        // ==========================================
        auto view = m_ActiveScene->Reg().view<CameraComponent>();
        for (auto entityID : view) {
            auto& cameraComp = view.get<CameraComponent>(entityID);
            
            if (!cameraComp.FixedAspectRatio) {
                // 解开这里的注释！让 Game 窗口的尺寸赋给新相机
                cameraComp.Camera.SetViewportSize(
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResW : (uint32_t)m_GameViewportSize.x,
                    (m_GameViewportResW > 0 && m_GameViewportResH > 0)
                        ? (uint32_t)m_GameViewportResH : (uint32_t)m_GameViewportSize.y);
            }
        }

        // ==========================================
        // 4. 开启统一的运行时总开关！
        // ==========================================
        m_ActiveScene->OnRuntimeStart();
    }

    void EditorLayer::OnSceneStop() {
        m_SceneState = SceneState::Edit;
        // --- 新增：停止游戏也重置状态 ---
        m_IsPaused = false;
        m_TimeStepScale = 1.0f;

        // ==========================================
        // 停止统一的运行时，清理内存！
        // ==========================================
        if (m_ActiveScene) {
            m_ActiveScene->OnRuntimeStop();
        }

        std::string tempPath = VFS::ResolveString("project://temp/temp_play_scene.ayaya");
        if (std::filesystem::exists(tempPath)) {
            std::filesystem::remove(tempPath);
        }

        // 恢复编辑状态的场景
        m_ActiveScene = m_EditorScene;
        m_SceneHierarchyPanel.SetContext(m_ActiveScene); m_TimelinePanel.SetContext(m_ActiveScene);
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