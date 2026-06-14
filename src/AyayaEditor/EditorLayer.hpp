#pragma once

#include <Ayaya.hpp>
#include "EditorCamera.hpp"
#include "Panels/SceneHierarchyPanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/PreferencesPanel.hpp"
#include "Panels/ScreenshotPanel.hpp"
#include "Panels/HistoryPanel.hpp"
#include "Panels/FrameDebuggerPanel.hpp"
#include "Panels/ImportModelPanel.hpp"
#include "Asset/AssetWatcher.hpp"
#include "Renderer/SceneRenderer.hpp"
#include "Renderer/Framebuffer.hpp"
#include <Renderer/Renderer.hpp>
#include <Renderer/Texture.hpp>
#include "Renderer/MaterialSerializer.hpp"
#include "Scene/SceneSerializer.hpp"
#include "Utils/PlatformUtils.hpp"
#include "Asset/AssetManager.hpp"
#include "Engine/Core/CommandHistory.hpp"

#include <imgui.h>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
#endif

namespace Ayaya {

    enum class SceneState {
        Edit = 0, Play = 1
    };

    class EditorLayer : public Layer {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;
        static EditorLayer& Get();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(Timestep ts) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Event& event) override;

        CommandHistory& GetCommandHistory() { return m_CommandHistory; }
        SceneHierarchyPanel& GetSceneHierarchyPanel() { return m_SceneHierarchyPanel; }

        // Public file operations for ContentBrowser integration
        void OpenSceneFile(const std::filesystem::path& filepath);
        AssetWatcher& GetAssetWatcher() { return m_AssetWatcher; }
        ImportModelPanel& GetImportModelPanel() { return m_ImportModelPanel; }


    private:
        void SetupScene();
        void InitDefaultProject();
        void NewScene();
        void OpenScene();
        void SaveScene();
        void SaveSceneAs();
        void SaveProjectAs();
        void UIRenderSaveAsPopup();

        void NewProject();
        void OpenProject();
        bool OpenProject(const std::filesystem::path& path);
        void SaveProject();

        bool OnKeyPressed(KeyPressedEvent& e);
        
        void HandleShortcuts();
        
        void UIRenderDockspace(float reserveBottom = 0.0f);
        void UIRenderMenuBar();
        void UIRenderToolbar(); // 顶部工具栏 (放播放按钮)
        void UIRenderViewport();
        void UIRenderGameViewport();
        
        void HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void UIRenderDebugGizmos(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void UIRenderDebugUIGizmos(ImVec2 viewportScreenMin, ImVec2 vpSize, float dpiScale);

        // ==========================================
        // 新增：游玩模式相关的函数
        // ==========================================
        void OnScenePlay();
        void OnSceneStop();

    private:
        static EditorLayer* s_Instance;

        EditorCamera m_EditorCamera;
        std::shared_ptr<Scene> m_ActiveScene;
        std::shared_ptr<Scene> m_EditorScene; 
        SceneState m_SceneState = SceneState::Edit;

        bool m_IsPaused = false;
        float m_TimeStepScale = 1.0f; // 1x, 2x, 4x 等倍速

        std::string m_CurrentScenePath = std::string();
        
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        ImVec2 m_ViewportBounds[2]; 
        glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };

        std::shared_ptr<SceneRenderer> m_SceneRenderer;
        std::shared_ptr<SceneRenderer> m_GameRenderer;

        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        PreferencesPanel m_PreferencesPanel;
        ScreenshotPanel m_ScreenshotPanel;
        HistoryPanel m_HistoryPanel;
        FrameDebuggerPanel m_FrameDebuggerPanel;
        ImportModelPanel m_ImportModelPanel;
        AssetWatcher m_AssetWatcher;

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_InitialMousePos = { 0.0f, 0.0f }; 

        int m_GizmoType = -1;
        bool m_ShowCameraSpeedPopup = false;

        bool  m_ShowContentDrawer = false;
        float m_DrawerAnimationProgress = 0.0f;
        int   m_ViewportResW = 0;  // 0 = fit window
        int   m_ViewportResH = 0;

        Entity m_HoveredEntity = {}; 

        // Scene 窗口设置
        bool m_ShowGrid = true; // 默认开启网格
        bool m_ShowPreferencesWindow = false; // 控制偏好设置窗口的开关
        bool m_ShowCameraGizmos = true;
        bool m_ShowLightGizmos = true;
        bool m_ShowUIGizmos = true;
        bool m_ShowGizmosOverlay = false;
        bool m_ShowViewportOptions = false;
        bool m_ShowViewportStats = false;
        float m_CameraSpeed = 5.0f;

        // Gizmo icon textures
        std::shared_ptr<Texture2D> m_CameraIcon;
        std::shared_ptr<Texture2D> m_PointLightIcon;
        std::shared_ptr<Texture2D> m_DirLightIcon;

        // 统计
        SceneRenderer::Statistics m_GameStats;
        bool m_ShowStatsPanel = false;

        // 进度条加载系统
        std::string m_ProjectToLoad = "";
        void LoadProjectWithProgress(const std::string& projectFilePath);

        // 命令系统
        CommandHistory m_CommandHistory;

        // ==========================================
        // 新建项目弹窗 UI 状态
        // ==========================================
        bool m_ShowNewProjectPopup = false;
        char m_NewProjectName[256] = "MyProject";
        char m_NewProjectLocation[256] = "";

        bool m_ShowSaveAsPopup = false;
        char m_SaveAsProjectName[256] = "";
        char m_SaveAsProjectLocation[256] = ""; 
        
        // 渲染弹窗的函数
        void UIRenderNewProjectPopup();
    };


    // 静态工具类：获取当前引擎占用的物理内存 (MB)
    static float GetPhysicalMemoryUsageMB() {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
        }
        return 0.0f;
#elif defined(__APPLE__)
        struct mach_task_basic_info info;
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
            return (float)info.resident_size / (1024.0f * 1024.0f);
        }
        return 0.0f;
#else
        return 0.0f; // Linux 等其他平台暂不实现
#endif
    }
}