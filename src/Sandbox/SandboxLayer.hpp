#pragma once

// ==========================================
// SandboxLayer — 通用游戏运行时层 (导出游戏使用)
// 读取 LaunchConfig (game.json)，挂载打包资产，加载启动场景，
// 用生产路径 SceneRenderer 渲染，并通过 ImGui 全屏呈现。
// 取代旧的 immediate-mode 渲染 demo。
// ==========================================

#include <Ayaya.hpp>
#include <Renderer/Renderer.hpp>
#include <Renderer/SceneRenderer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Entity.hpp>
#include <Scene/Components.hpp>
#include <Scene/SceneSerializer.hpp>
#include <Project/Project.hpp>
#include <Project/LaunchConfig.hpp>
#include <Core/VFS.hpp>
#include <Asset/AssetManager.hpp>
#include <Scripting/ScriptEngine.hpp>
#include <GLFW/glfw3.h>

#include <memory>
#include <filesystem>

class SandboxLayer : public Ayaya::Layer {
public:
    SandboxLayer(const Ayaya::LaunchConfig& config, const std::filesystem::path& gameRoot)
        : Layer("SandboxLayer"), m_Config(config), m_GameRoot(gameRoot) {}

    virtual void OnAttach() override {
        // ==========================================
        // 1. 加载项目 (Project::Load 会挂 project→项目根、engine→CWD/assets)
        // ==========================================
        std::filesystem::path projectFile = m_GameRoot / m_Config.ProjectFile;
        auto project = Ayaya::Project::Load(projectFile);
        if (!project) {
            AYAYA_CORE_ERROR("[Sandbox] Failed to load project: {0}", projectFile.string());
            return;
        }

        // ==========================================
        // 2. 重挂挂载点到 bundle 相对目录 (不依赖 current_path())
        // ==========================================
        Ayaya::VFS::Mount("engine", m_GameRoot / "engine");
        Ayaya::VFS::Mount("project", Ayaya::Project::GetAssetDirectory());

        // ==========================================
        // 3. 脚本引擎 + 资产注册表
        // ==========================================
        Ayaya::ScriptEngine::Init();
        Ayaya::AssetManager::RefreshRegistry();

        // ==========================================
        // 4. 生产渲染器
        // ==========================================
        m_SceneRenderer = std::make_shared<Ayaya::SceneRenderer>();
        m_SceneRenderer->Init();

        // ==========================================
        // 5. 加载启动场景
        // ==========================================
        m_ActiveScene = std::make_shared<Ayaya::Scene>();
        std::string startScenePath = Ayaya::VFS::ResolveString("project://" + m_Config.StartScene);
        if (std::filesystem::exists(startScenePath)) {
            Ayaya::SceneSerializer serializer(m_ActiveScene);
            Ayaya::EditorState state;
            if (!serializer.Deserialize(startScenePath, state)) {
                AYAYA_CORE_ERROR("[Sandbox] Failed to deserialize start scene: {0}", startScenePath);
            }
        } else {
            AYAYA_CORE_ERROR("[Sandbox] Start scene not found: {0}", startScenePath);
        }

        // GC 收尾：卸载未被场景引用的资产
        auto activeHandles = m_ActiveScene->GetActiveAssetHandles();
        Ayaya::AssetManager::UnloadUnusedAssets(activeHandles);

        // ==========================================
        // 6. SRP 管线
        // ==========================================
        if (m_Config.DefaultSRPScript != 0) {
            m_SceneRenderer->SetSRPScript(Ayaya::UUID(m_Config.DefaultSRPScript));
        }

        // ==========================================
        // 7. 运行时启动 (唤醒物理 + Lua 脚本)
        // ==========================================
        m_ActiveScene->OnRuntimeStart();

        // ==========================================
        // 8. 窗口尺寸 / 标题 / VSync
        // ==========================================
        auto& window = Ayaya::Application::Get().GetWindow();
        if (m_Config.WindowWidth > 0 && m_Config.WindowHeight > 0) {
            window.SetSize(m_Config.WindowWidth, m_Config.WindowHeight);
        }
        window.SetVSync(m_Config.VSync);
        if (!m_Config.WindowTitle.empty()) {
            glfwSetWindowTitle(window.GetNativeWindow(), m_Config.WindowTitle.c_str());
        }

        AYAYA_CORE_INFO("[Sandbox] Runtime started: {0}", m_Config.GameName);
    }

    virtual void OnUpdate(Ayaya::Timestep ts) override {
        if (!m_ActiveScene || !m_SceneRenderer) return;

        // ==========================================
        // 1. 运行时逻辑 (UI 布局 / Lua / 动画 / 物理)
        // ==========================================
        m_ActiveScene->OnUpdateRuntime(ts);

        // ==========================================
        // 2. 环境光与 IBL 动态更新
        // ==========================================
        auto envView = m_ActiveScene->Reg().view<Ayaya::EnvironmentComponent>();
        bool hasEnvironment = false;
        for (auto entityID : envView) {
            hasEnvironment = true;
            auto& envComp = envView.get<Ayaya::EnvironmentComponent>(entityID);
            if (envComp.IsDirty) {
                m_SceneRenderer->SetEnvironment(envComp);
                envComp.IsDirty = false;
            }
            m_SceneRenderer->SetEnvironmentSettings(envComp.Intensity, envComp.AmbientColor);
            break;
        }
        if (!hasEnvironment) {
            Ayaya::EnvironmentComponent emptyEnv;
            emptyEnv.Type = Ayaya::EnvironmentType::None;
            emptyEnv.AmbientColor = { 0.0f, 0.0f, 0.0f };
            m_SceneRenderer->SetEnvironment(emptyEnv);
            m_SceneRenderer->SetEnvironmentSettings(0.0f, emptyEnv.AmbientColor);
        }

        // ==========================================
        // 3. 主相机
        // ==========================================
        bool hasValidCamera = false;
        glm::mat4 cameraViewMatrix(1.0f), cameraProjectionMatrix(1.0f);
        glm::vec3 cameraPosition(0.0f);
        bool renderSkybox = false;
        glm::vec4 clearColor{ 0.06f, 0.06f, 0.065f, 1.0f };

        auto cameraView = m_ActiveScene->Reg().view<Ayaya::TransformComponent, Ayaya::CameraComponent>();
        for (auto entityID : cameraView) {
            auto [transform, cameraComp] = cameraView.get<Ayaya::TransformComponent, Ayaya::CameraComponent>(entityID);
            if (cameraComp.Primary) {
                Ayaya::Entity cameraEntity{ entityID, m_ActiveScene.get() };
                glm::mat4 worldTransform = cameraEntity.GetWorldTransform();

                glm::vec3 scale, translation, skew;
                glm::quat rotation;
                glm::vec4 perspective;
                glm::decompose(worldTransform, scale, rotation, translation, skew, perspective);

                cameraPosition = translation;
                glm::mat4 unscaledTransform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation);
                cameraViewMatrix = glm::inverse(unscaledTransform);

                if (!cameraComp.FixedAspectRatio) {
                    auto& win = Ayaya::Application::Get().GetWindow();
                    cameraComp.Camera.SetViewportSize(win.GetWidth(), win.GetHeight());
                }
                cameraProjectionMatrix = cameraComp.Camera.GetProjection();

                renderSkybox = (cameraComp.ClearFlag == Ayaya::CameraComponent::ClearFlags::Skybox);
                clearColor = cameraComp.BackgroundColor;
                hasValidCamera = true;
                break;
            }
        }

        // ==========================================
        // 4. 渲染
        // ==========================================
        m_SceneRenderer->SetClearColor(clearColor);
        if (hasValidCamera) {
            m_SceneRenderer->BeginScene(cameraViewMatrix, cameraProjectionMatrix, cameraPosition);
            Ayaya::RenderViewConfig cfg;
            cfg.EnableSkybox = renderSkybox;
            cfg.ClearColor = clearColor;
            m_SceneRenderer->RenderScene(m_ActiveScene, cfg);
            m_SceneRenderer->EndScene();
        } else {
            m_SceneRenderer->BeginScene(glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f));
            Ayaya::RenderViewConfig cfg;
            cfg.EnableSkybox = false;
            cfg.ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_SceneRenderer->RenderScene(m_ActiveScene, cfg);
            m_SceneRenderer->EndScene();
        }
    }

    // 全屏呈现最终颜色附件 (对齐 EditorLayer 的 Game 视口 UV 翻转)
    virtual void OnImGuiRender() override {
        if (!m_SceneRenderer) return;
        void* textureID = m_SceneRenderer->GetFinalColorAttachmentRendererID();
        if (!textureID) return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("Game", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings);

        ImVec2 vp = ImGui::GetContentRegionAvail();
        bool vk = (Ayaya::RendererAPI::GetAPI() == Ayaya::RendererAPI::API::Vulkan);
        ImGui::Image(textureID, vp,
            vk ? ImVec2{ 0, 0 } : ImVec2{ 0, 1 },
            vk ? ImVec2{ 1, 1 } : ImVec2{ 1, 0 });

        ImGui::End();
    }

    // 窗口大小改变 → 更新渲染器 + 非固定宽高比相机
    virtual void OnEvent(Ayaya::Event& event) override {
        Ayaya::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Ayaya::WindowResizeEvent>([this](Ayaya::WindowResizeEvent& e) {
            if (e.GetWidth() == 0 || e.GetHeight() == 0) return false;

            if (m_SceneRenderer) m_SceneRenderer->OnWindowResize(e.GetWidth(), e.GetHeight());

            if (m_ActiveScene) {
                auto view = m_ActiveScene->Reg().view<Ayaya::CameraComponent>();
                for (auto entityID : view) {
                    auto& cameraComp = view.get<Ayaya::CameraComponent>(entityID);
                    if (!cameraComp.FixedAspectRatio) {
                        cameraComp.Camera.SetViewportSize(e.GetWidth(), e.GetHeight());
                    }
                }
            }
            return false;
        });
    }

private:
    Ayaya::LaunchConfig              m_Config;
    std::filesystem::path            m_GameRoot;
    std::shared_ptr<Ayaya::Scene>    m_ActiveScene;
    std::shared_ptr<Ayaya::SceneRenderer> m_SceneRenderer;
};
