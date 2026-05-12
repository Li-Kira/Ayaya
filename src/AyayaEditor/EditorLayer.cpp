#include "EditorLayer.hpp"
#include "Renderer/Mesh.hpp"
#include "Events/MouseEvent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Engine/Core/EditorCommands.hpp"
#include "Engine/Core/ImGuiBackend.hpp"
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
        Project::New();

        // ==========================================
        // 启动时读取资产注册表
        // ==========================================
        AssetManager::DeserializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));
        // 初始化并加载编辑器偏好设置
        m_PreferencesPanel.Init();
        // ==========================================
        // 初始化 Lua 虚拟机
        // ==========================================
        ScriptEngine::Init();

        // 【核心防御】：只在 OpenGL 模式下创建和初始化基于 GL 的 SceneRenderer
        m_SceneRenderer = std::make_shared<SceneRenderer>();
        m_SceneRenderer->Init();
        m_GameRenderer = std::make_shared<SceneRenderer>();
        m_GameRenderer->Init();

        m_FrameDebuggerPanel.SetContext(m_GameRenderer);
        

        SetupScene();

        // ==========================================
        // 验证异步加载：找一个注册了但尚未加载到内存的纹理
        // 预期日志：[Async] Spawning bg thread → BG: stbi_load → MAIN: GPU upload done
        // ==========================================
        AYAYA_CORE_INFO("=== Async Load Verification Start ===");
        const auto& loaded = AssetManager::GetLoadedAssets();
        for (const auto& [handle, metadata] : AssetManager::GetRegistry()) {
            if (metadata.Type == AssetType::Texture2D && loaded.find(handle) == loaded.end()) {
                AYAYA_CORE_INFO("Verification: RequestAsyncLoad for UNLOADED handle {0} ({1})", (uint64_t)handle, metadata.VirtualPath);
                AssetManager::RequestAsyncLoad(handle);
                break;
            }
        }
        AYAYA_CORE_INFO("=== Async Load Verification: done ===");

        // 清理临时文件
        std::string tempPath = VFS::ResolveString("project://temp/temp_play_scene.ayaya");
        if (std::filesystem::exists(tempPath)) {
            std::filesystem::remove(tempPath);
        }
    }

    void EditorLayer::OnDetach() {
        // 在 Layer 被剥离（程序退出）时，立刻释放渲染器实例，
        // 确保 VMA 的离线画布 (Framebuffer) 赶在 Vulkan 销毁前被安全释放！
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

        // ==========================================
        // 0.1 执行挂起的场景加载任务 (带实时进度条)
        // ==========================================
        if (!m_SceneToLoad.empty()) {
            LoadSceneWithProgress(m_SceneToLoad);
            m_SceneToLoad = ""; // 清空标记
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
            
            uint32_t physicalWidth = (uint32_t)(m_ViewportSize.x * dpiScale);
            uint32_t physicalHeight = (uint32_t)(m_ViewportSize.y * dpiScale);
            // 【修改为调用 m_SceneRenderer】
            m_SceneRenderer->OnWindowResize(physicalWidth, physicalHeight);
            m_EditorCamera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
            s_LastViewportSize = m_ViewportSize;
        }

        // ------------------------------------------
        // 2.2 处理 Game (游戏视口) 的 Resize
        // ------------------------------------------
        static glm::vec2 s_LastGameViewportSize = { 0.0f, 0.0f };
        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f && 
           (s_LastGameViewportSize.x != m_GameViewportSize.x || s_LastGameViewportSize.y != m_GameViewportSize.y)) {
            
            uint32_t physicalGameWidth = (uint32_t)(m_GameViewportSize.x * dpiScale);
            uint32_t physicalGameHeight = (uint32_t)(m_GameViewportSize.y * dpiScale);
            
            // 【修改为调用 m_GameRenderer，并删除 m_GameFBO->Resize】
            m_GameRenderer->OnWindowResize(physicalGameWidth, physicalGameHeight);
            
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

        // ==========================================
        // 3. 处理runtime，包括物理引擎和Lua脚本系统
        // ==========================================
        // 上帝相机只在 Edit 模式响应输入
        if (m_SceneState == SceneState::Edit) {
            m_EditorCamera.OnUpdate(ts, m_ViewportFocused);
        }
        // 如果处于 Play 模式且没有暂停，则推进物理运算，兼容 m_TimeStepScale
        else if (m_SceneState == SceneState::Play && !m_IsPaused) {
            m_ActiveScene->OnUpdateRuntime(ts * m_TimeStepScale);
        }


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
                    cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
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
            m_GameRenderer->RenderScene(m_ActiveScene, {}, false, renderSkybox, clearColor);
            m_GameRenderer->EndScene();

            m_GameStats = m_GameRenderer->GetStats();
        } else {
            memset(&m_GameStats, 0, sizeof(SceneRenderer::Statistics));
            // 由于没有主相机，直接让管线渲染黑屏
            m_GameRenderer->BeginScene(glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f));
            m_GameRenderer->RenderScene(m_ActiveScene, {}, false, false, {0.0f, 0.0f, 0.0f, 1.0f});
            m_GameRenderer->EndScene();
        }

        // ------------------------------------------
        // 5.2: 渲染 Scene 窗口
        // ------------------------------------------
        m_SceneRenderer->SetClearColor(clearColor);
        m_SceneRenderer->BeginScene(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection(), m_EditorCamera.GetPosition());
        m_SceneRenderer->RenderScene(m_ActiveScene, m_HoveredEntity, m_ShowGrid, renderSkybox, clearColor);
        m_SceneRenderer->EndScene();

        // ------------------------------------------
        // 5.3: 独立的高清截图执行器 (离线渲染 Pass)
        // ------------------------------------------
        if (m_ScreenshotPanel.ConsumePending()) {
            uint32_t shotWidth = m_ScreenshotPanel.GetWidth();
            uint32_t shotHeight = m_ScreenshotPanel.GetHeight();
            std::string shotPath = m_ScreenshotPanel.GetPath();

            auto cameraView = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
            for (auto entityID : cameraView) {
                auto [transform, cameraComp] = cameraView.get<TransformComponent, CameraComponent>(entityID);
                if (cameraComp.Primary) {
                    // 1. 提取当前相机的世界坐标矩阵
                    Entity cameraEntity{ entityID, m_ActiveScene.get() };
                    glm::mat4 worldTransform = cameraEntity.GetWorldTransform();
                    glm::vec3 scale, translation, skew;
                    glm::quat rotation;
                    glm::vec4 perspective;
                    glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);
                    
                    glm::vec3 camPos = translation;
                    glm::mat4 unscaledTransform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation);
                    glm::mat4 camViewMat = glm::inverse(unscaledTransform); 

                    // 2. 备份当前的管线状态
                    uint32_t oldFboWidth = (uint32_t)(m_GameViewportSize.x * dpiScale);
                    uint32_t oldFboHeight = (uint32_t)(m_GameViewportSize.y * dpiScale);

                    // 3. 临时篡改相机比例和渲染器尺寸
                    cameraComp.Camera.SetViewportSize(shotWidth, shotHeight);
                    glm::mat4 camProjMat = cameraComp.Camera.GetProjection();
                    m_GameRenderer->OnWindowResize(shotWidth, shotHeight);
                    m_GameRenderer->SetClearColor(cameraComp.BackgroundColor);

                    // 4. 独立执行一帧专属渲染！
                    bool drawSkybox = (cameraComp.ClearFlag == CameraComponent::ClearFlags::Skybox);
                    m_GameRenderer->BeginScene(camViewMat, camProjMat, camPos);
                    m_GameRenderer->RenderScene(m_ActiveScene, {}, false, drawSkybox, cameraComp.BackgroundColor);
                    m_GameRenderer->EndScene();

                    // 5. 从显存偷出像素数据
                    uint32_t fboID = (uint32_t)(intptr_t)m_GameRenderer->GetPostProcessFBORendererID();
                    glBindFramebuffer(GL_FRAMEBUFFER, fboID);
                    std::vector<unsigned char> pixels(shotWidth * shotHeight * 4);
                    glReadPixels(0, 0, shotWidth, shotHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);

                    // 6. 编码并写入硬盘
                    stbi_flip_vertically_on_write(true);
                    stbi_write_png(shotPath.c_str(), shotWidth, shotHeight, 4, pixels.data(), shotWidth * 4);
                    AYAYA_CORE_INFO("High-Res Screenshot saved to: {0} ({1}x{2})", shotPath, shotWidth, shotHeight);

                    // 7. 打扫战场：恢复相机和渲染器的原本状态
                    cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
                    m_GameRenderer->OnWindowResize(oldFboWidth, oldFboHeight);

                    break; // 截完图直接退出循环
                }
            }
        }
    }

    void EditorLayer::OnImGuiRender() {
        UIRenderDockspace();
        UIRenderMenuBar();
        UIRenderToolbar();

        m_SceneHierarchyPanel.OnImGuiRender();
        m_ContentBrowserPanel.OnImGuiRender();
        m_PreferencesPanel.OnImGuiRender();
        m_ScreenshotPanel.OnImGuiRender();
        m_HistoryPanel.OnImGuiRender();
        // m_FrameDebuggerPanel.OnImGuiRender();

        if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
            // OpenGL 模式下，一切照常渲染
            // m_ContentBrowserPanel.OnImGuiRender();
            m_FrameDebuggerPanel.OnImGuiRender();
        } else {
            // Vulkan 模式下，保留窗口外壳防止布局错乱，但内部用文字占位
            // ImGui::Begin("Content Browser");
            // ImGui::TextDisabled("Vulkan Mode: Content Browser is paused pending Descriptor Sets.");
            // ImGui::End();

            // ImGui::Begin("Frame Debugger");
            // ImGui::TextDisabled("Vulkan Mode: Frame Debugger is paused pending Descriptor Sets.");
            // ImGui::End();
        }
        
        UIRenderViewport();
        UIRenderGameViewport();

        UIRenderNewProjectPopup(); // 【新增】：渲染弹窗层

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

        // 【核心修复】：新建场景时，彻底清空撤销历史！
        m_CommandHistory.Clear();

        AYAYA_CORE_INFO("Created a new empty scene with default camera.");
    }

    void EditorLayer::OpenScene() {
        std::string filepath = FileDialogs::OpenFile("ayaya"); 
        if (!filepath.empty()) { 
            // 【核心】：不在这里加载！只登记路径，推迟到帧开始时处理，防止 ImGui 崩溃！
            m_SceneToLoad = filepath; 
        }
    }

    // =====================================================================
    // 项目管理系统 (Project Management)
    // =====================================================================

    void EditorLayer::NewProject() {
        // 1. 弹出原生保存对话框让用户选择项目路径和名称 (.ayaproj)
        std::string filepath = FileDialogs::SaveFile("Ayaya Project (*.ayaproj)|*.ayaproj");
        if (filepath.empty()) return;

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
        
        // 保存项目文件（这也会记录项目的物理根目录位置）
        Project::SaveActive(projectFilePath);

        // 4. 【关键步骤】：动态将新项目的物理路径挂载到 project:// 虚拟协议
        VFS::Mount("project", assetDir);

        // 5. 初始化该项目的 AssetRegistry.yaml
        AssetManager::Clear(); // 清空旧项目内存
        AssetManager::SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));

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

                    // 动态挂载 VFS 节点
                    VFS::Mount("project", assetDir);

                    // 初始化资产数据库
                    AssetManager::Clear();
                    AssetManager::SerializeRegistry(VFS::ResolveString("project://AssetRegistry.yaml"));

                    // 生成并自动保存起始场景
                    NewScene();
                    m_CurrentScenePath = VFS::ResolveString("project://Scenes/Default.ayaya");

                    // 将引擎内置默认材质克隆到项目 Materials 目录，使项目自包含
                    UUID builtInMat = AssetManager::GetBuiltInMaterial();
                    std::string engineMatPath = AssetManager::GetAssetPhysicalPath(builtInMat);
                    if (!engineMatPath.empty()) {
                        std::string projectMatPath = VFS::ResolveString("project://Materials/DefaultPBR.mat");
                        try {
                            std::filesystem::copy_file(engineMatPath, projectMatPath,
                                std::filesystem::copy_options::overwrite_existing);
                            // 将同一个 UUID 重定向到项目本地路径，场景引用无需修改
                            AssetManager::UpdateAssetPath(builtInMat, "project://Materials/DefaultPBR.mat");
                        } catch (const std::exception& e) {
                            AYAYA_CORE_WARN("Failed to clone default material: {0}", e.what());
                        }
                    }

                    // 保存场景 + 所有材质 .mat 文件 + 资产注册表
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
        // 1. 加载 .ayaproj 配置文件
        if (Project::Load(path)) {
            std::filesystem::path assetDir = Project::GetAssetDirectory();
            
            // 2. 【核心爆炸】：将 VFS 的 project 协议重定向到用户选择的文件夹！
            VFS::Mount("project", assetDir);

            // 3. 读取该项目专属的资产账本
            std::string registryPath = VFS::ResolveString("project://AssetRegistry.yaml");
            if (!AssetManager::DeserializeRegistry(registryPath)) {
                // 如果是空项目没有账本，自动建一个
                AssetManager::SerializeRegistry(registryPath);
            }

            // 4. 加载项目配置的默认场景
            std::string startScenePath = VFS::ResolveString("project://" + Project::GetActive()->GetConfig().StartScene);
            if (std::filesystem::exists(startScenePath)) {
                LoadSceneWithProgress(startScenePath);
            } else {
                NewScene();
            }

            AYAYA_CORE_INFO("📂 Successfully opened project: {0}", Project::GetActive()->GetConfig().Name);
            return true;
        }
        
        AYAYA_CORE_ERROR("Failed to load project at {0}", path.string());
        return false;
    }

    void EditorLayer::SaveProject() {
    if (Project::GetActive()) {
        // 1. 保存项目配置文件 (.ayaproj)
        auto projectPath = Project::GetProjectDirectory() / (Project::GetActive()->GetConfig().Name + ".ayaproj");
        Project::SaveActive(projectPath);

        // 2. 保存当前场景 (.ayaya)
        if (!m_CurrentScenePath.empty()) {
            SaveScene();
        }

        // ==========================================
        // 【核心修复】：保存内存中所有的材质资产
        // ==========================================
        AYAYA_CORE_INFO("Saving all materials...");
        for (auto& [handle, assetPtr] : AssetManager::GetLoadedAssets()) {
            auto metadata = AssetManager::GetMetadata(handle);
            
            // 如果这个资产是材质类型
            if (metadata.Type == AssetType::Material) {
                std::string physicalPath = AssetManager::GetAssetPhysicalPath(handle);
                // 排除引擎内置材质，只保存项目路径下的材质
                if (!physicalPath.empty() && physicalPath.find("assets/Editor/") == std::string::npos) {
                    auto material = std::static_pointer_cast<Material>(assetPtr);
                    MaterialSerializer::Serialize(material, physicalPath);
                }
            }
        }

        // 3. 最后保存资产账本 (AssetRegistry.yaml)
        // 确保所有在保存过程中新产生的 UUID 映射都被写入
        std::string registryPath = VFS::ResolveString("project://AssetRegistry.yaml");
        AssetManager::SerializeRegistry(registryPath);

        AYAYA_CORE_INFO("💾 Project Saved: Config, Scene, Materials and Registry are all synced to disk.");
    }
}
    void EditorLayer::LoadSceneWithProgress(const std::string& filepath) {
        std::shared_ptr<Scene> newScene = std::make_shared<Scene>();
        SceneSerializer serializer(newScene);
        EditorState state;

        // ==========================================
        // 进度回调：OpenGL 在帧内渲染进度条；Vulkan 通过日志输出进度
        // ==========================================
        auto progressCallback = [&](float progress, const std::string& message) {
            if (RendererAPI::GetAPI() == RendererAPI::API::OpenGL) {
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
                ImGui::TextColored(ImVec4(0.17f, 0.45f, 0.85f, 1.0f), "Loading Scene...");
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
            } else {
                AYAYA_CORE_INFO("⏳ Loading scene... {0:.0f}% - {1}", progress * 100.0f, message);
            }
        };

        // 通知第一帧：正在解析 YAML 文件...
        progressCallback(0.0f, "Parsing YAML data structure...");

        // ==========================================
        // 开始真正的反序列化，并将回调传进去
        // ==========================================
        if (serializer.Deserialize(filepath, state, progressCallback)) {
            // 原 OpenScene()代码
            m_ActiveScene = newScene;
            m_EditorScene = m_ActiveScene;
            
            m_ShowGrid = state.ShowGrid;
            m_EditorCamera.SetPosition(state.CameraPosition);
            m_EditorCamera.SetDistance(state.CameraDistance);
            m_EditorCamera.SetPitch(state.CameraPitch);
            m_EditorCamera.SetYaw(state.CameraYaw);
            m_EditorCamera.SetFocalPoint(state.CameraFocalPoint);
            m_EditorCamera.UpdateCameraView();
            
            auto view = m_ActiveScene->Reg().view<CameraComponent>();
            for (auto entityID : view) {
                auto& cameraComp = view.get<CameraComponent>(entityID);
                if (!cameraComp.FixedAspectRatio) {
                    cameraComp.Camera.SetViewportSize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
                }
            }

            m_SceneHierarchyPanel.SetContext(m_ActiveScene);
            m_CurrentScenePath = filepath;
            m_HoveredEntity = {};
            m_SceneHierarchyPanel.SetSelectedEntity({});

            // 【核心修复】：加载新场景成功时，彻底清空上一个场景残留的撤销历史！
            m_CommandHistory.Clear();

            AYAYA_CORE_INFO("Scene loaded successfully from {0}!", filepath);
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

            // 按下 F 键聚焦到选中物体 (Focus)
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                glm::mat4 transform = selectedEntity.GetWorldTransform();
                glm::vec3 targetPos = glm::vec3(transform[3]); 
                m_EditorCamera.SetFocalPoint(targetPos);
                m_EditorCamera.SetDistance(5.0f);
                m_EditorCamera.UpdateCameraView(); 
                AYAYA_CORE_INFO("Camera focused on entity.");
            }
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
                AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene As...");
                SaveSceneAs();
            } else {
                AYAYA_CORE_INFO("👉 Shortcut Triggered: Save Scene");
                // SaveScene();
                SaveProject();
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
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
    }

    void EditorLayer::UIRenderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {

                if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                    m_ShowNewProjectPopup = true; 
                }
                if (ImGui::MenuItem("Save Project", "Ctrl+S")) SaveProject();
                if (ImGui::MenuItem("Open Project", "Ctrl+O")) OpenProject();

                // if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
                // if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                // if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) SaveSceneAs();
                // if (ImGui::MenuItem("Load Scene", "Ctrl+O")) OpenScene();

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
                ImGui::MenuItem("Show Camera Gizmos", nullptr, &m_ShowCameraGizmos);
                ImGui::MenuItem("Show Light Gizmos", nullptr, &m_ShowLightGizmos);

                ImGui::Separator();

                ImGui::MenuItem("Show Statistics", nullptr, &m_ShowStatsPanel);
                ImGui::MenuItem("Show History", nullptr, &m_HistoryPanel.IsOpen);
                ImGui::MenuItem("Frame Debugger", nullptr, &m_FrameDebuggerPanel.IsOpen);

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("High-Res Screenshot")) {
                    m_ScreenshotPanel.Open(); // 唤出截图面板
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
        m_ViewportSize = { std::floor(viewportPanelSize.x), std::floor(viewportPanelSize.y) };

        // ==========================================
        // 核心修改：向渲染管线索要处理完毕的后期画面 (增加安全检查)
        // ==========================================

        if (m_SceneRenderer) {
            void* textureID = m_SceneRenderer->GetFinalColorAttachmentRendererID();
            if (textureID) {
                ImGui::Image(textureID, ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
                
                HandleMousePicking(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
                HandleGizmo(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
                UIRenderDebugGizmos(m_EditorCamera.GetViewMatrix(), m_EditorCamera.GetProjection());
            } else {
                // 占位提示，等待我们的 Vulkan 离线画布就绪
                ImGui::Text("Viewport is initializing...");
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorLayer::UIRenderGameViewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        ImGui::Begin("Game");

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        m_GameViewportSize = { std::floor(viewportPanelSize.x), std::floor(viewportPanelSize.y) };
        // 记录画图前的光标起始位置，这是悬浮层定位的锚点！
        ImVec2 cursorStartPos = ImGui::GetCursorPos();

        // 渲染底层的游戏画面
        if (m_GameRenderer) {
            void* textureID = m_GameRenderer->GetFinalColorAttachmentRendererID();
            if (textureID) {
                ImGui::Image(textureID, ImVec2{ m_GameViewportSize.x, m_GameViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
            } else {
                // 占位提示，等待我们的 Vulkan 离线画布就绪
                ImGui::Text("Game is initializing...");
            }
        }

        // ==========================================
        // Game 窗口内置 Stats 悬浮层 (完美动态适配版)
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
            auto boldFont = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : io.Fonts->Fonts[0];

            // 获取我们刚刚写好的内存数据和渲染统计数据
            float memoryMB = GetPhysicalMemoryUsageMB();
            float uiScale = io.FontGlobalScale;
            float alignOffset = 100.0f * uiScale;

            // --- 显卡信息大类 ---
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
            ImGui::Text("Screen Size: %dx%d", (int)m_GameViewportSize.x, (int)m_GameViewportSize.y);
            ImGui::Spacing();
            
            // --- 渲染调用大类 ---
            ImGui::PushFont(boldFont);
            ImGui::TextColored(ImVec4(0.8f, 0.6f, 0.2f, 1.0f), "Rendering");
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::Text("Draw Calls: %d", m_GameStats.DrawCalls);
            ImGui::Text("Shader Binds: %d", m_GameStats.ShaderBinds);
            ImGui::Spacing();

            ImGui::PushFont(boldFont);
            ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "Geometry");
            ImGui::PopFont();
            ImGui::Separator();
            
            ImGui::Text("Triangle Count: %d", m_GameStats.TriangleCount); 
            ImGui::SameLine(0.0f, 15.0f * uiScale); 
            ImGui::Text("Vertex Count: %d", m_GameStats.VertexCount);

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
        // 【修改】：不再因为没有选中物体就 return，我们需要获取它用来做"高亮区分"
        Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        glm::mat4 viewProj = cameraProjectionMatrix * cameraViewMatrix;

        auto ProjectToScreen = [&](const glm::vec3& worldPos, ImVec2& outScreenPos) -> bool {
            glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
            if (clipPos.w < 0.01f) return false; 
            glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
            float viewportWidth = m_ViewportBounds[1].x - m_ViewportBounds[0].x;
            float viewportHeight = m_ViewportBounds[1].y - m_ViewportBounds[0].y;
            outScreenPos.x = m_ViewportBounds[0].x + (ndcPos.x + 1.0f) * 0.5f * viewportWidth;
            outScreenPos.y = m_ViewportBounds[0].y + (1.0f - ndcPos.y) * 0.5f * viewportHeight;
            return true;
        };

        // ==========================================
        // 1. 遍历并绘制所有 Camera 的视锥体
        // ==========================================
        if (m_ShowCameraGizmos) {
            auto view = m_ActiveScene->Reg().view<TransformComponent, CameraComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                // 【绝妙细节】：被选中的相机白线加粗，未选中的相机变成半透明灰线
                bool isSelected = (entity == selectedEntity);
                ImU32 color = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 200);
                float thickness = isSelected ? 2.0f : 1.0f;

                auto& cameraComp = entity.GetComponent<CameraComponent>();
                glm::mat4 transform = entity.GetWorldTransform();
                glm::mat4 proj = cameraComp.Camera.GetProjection();
                
                glm::mat4 invViewProj = transform * glm::inverse(proj); 

                glm::vec3 frustumCornersNDC[8] = {
                    {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}, 
                    {-1.0f, -1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}  
                };

                ImVec2 screenPoints[8];
                bool pointsValid[8];
                for (int i = 0; i < 8; i++) {
                    glm::vec4 worldPos = invViewProj * glm::vec4(frustumCornersNDC[i], 1.0f);
                    worldPos /= worldPos.w; 
                    pointsValid[i] = ProjectToScreen(glm::vec3(worldPos), screenPoints[i]);
                }

                auto DrawLineIfValid = [&](int p1, int p2) {
                    if (pointsValid[p1] && pointsValid[p2]) drawList->AddLine(screenPoints[p1], screenPoints[p2], color, thickness);
                };

                DrawLineIfValid(0, 1); DrawLineIfValid(1, 2); DrawLineIfValid(2, 3); DrawLineIfValid(3, 0);
                DrawLineIfValid(4, 5); DrawLineIfValid(5, 6); DrawLineIfValid(6, 7); DrawLineIfValid(7, 4);
                DrawLineIfValid(0, 4); DrawLineIfValid(1, 5); DrawLineIfValid(2, 6); DrawLineIfValid(3, 7);
            }
        }

        // ==========================================
        // 2. 遍历并绘制所有 Point Light 的光照球体
        // ==========================================
        if (m_ShowLightGizmos) {
            auto view = m_ActiveScene->Reg().view<TransformComponent, PointLightComponent>();
            for (auto entityID : view) {
                Entity entity{ entityID, m_ActiveScene.get() };
                if (!entity.IsActiveInHierarchy()) continue;

                bool isSelected = (entity == selectedEntity);
                
                auto& lightComp = entity.GetComponent<PointLightComponent>();
                glm::mat4 transform = entity.GetWorldTransform();
                glm::vec3 worldPos = glm::vec3(transform[3]);

                ImVec2 screenPos;
                if (ProjectToScreen(worldPos, screenPos)) {
                    ImU32 lightColor = IM_COL32((int)(lightComp.Color.r * 255), (int)(lightComp.Color.g * 255), (int)(lightComp.Color.b * 255), 255);
                    drawList->AddCircleFilled(screenPos, 6.0f, lightColor);
                    
                    // 中心圆点的选中反馈：选中白边加粗，未选中给个黑边
                    ImU32 outlineColor = isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
                    float outlineThick = isSelected ? 2.0f : 1.0f;
                    drawList->AddCircle(screenPos, 8.0f, outlineColor, 0, outlineThick);
                }

                // 【绝妙细节】：未选中的光环透明度极低，防止场景里灯太多导致画面被淹没！
                int alpha = isSelected ? 150 : 25; 
                float lineThick = isSelected ? 1.5f : 1.0f;
                ImU32 ringColor = IM_COL32((int)(lightComp.Color.r * 255), (int)(lightComp.Color.g * 255), (int)(lightComp.Color.b * 255), alpha);

                float radius = lightComp.Radius;

                const int segments = 48; 
                for (int plane = 0; plane < 3; plane++) {
                    ImVec2 prevScreenPos;
                    bool prevValid = false;
                    for (int i = 0; i <= segments; i++) {
                        float angle = (float)i / (float)segments * 2.0f * 3.14159265f;
                        glm::vec3 offset;
                        
                        if (plane == 0) offset = glm::vec3(glm::cos(angle), glm::sin(angle), 0.0f) * radius;     
                        else if (plane == 1) offset = glm::vec3(glm::cos(angle), 0.0f, glm::sin(angle)) * radius; 
                        else offset = glm::vec3(0.0f, glm::cos(angle), glm::sin(angle)) * radius;                 

                        ImVec2 currScreenPos;
                        bool currValid = ProjectToScreen(worldPos + offset, currScreenPos);

                        if (i > 0 && prevValid && currValid) {
                            drawList->AddLine(prevScreenPos, currScreenPos, ringColor, lineThick);
                        }
                        prevScreenPos = currScreenPos;
                        prevValid = currValid;
                    }
                }
            }
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
        m_SceneHierarchyPanel.SetContext(m_ActiveScene);

        // ==========================================
        // 核心修复 3：强行重置克隆出来的玩家相机的视口比例和模式！
        // ==========================================
        auto view = m_ActiveScene->Reg().view<CameraComponent>();
        for (auto entityID : view) {
            auto& cameraComp = view.get<CameraComponent>(entityID);
            
            if (!cameraComp.FixedAspectRatio) {
                // 解开这里的注释！让 Game 窗口的尺寸赋给新相机
                cameraComp.Camera.SetViewportSize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
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