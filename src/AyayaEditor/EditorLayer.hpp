#pragma once

#include <Ayaya.hpp>
#include "EditorCamera.hpp"
#include "Panels/SceneHierarchyPanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include "Panels/PreferencesPanel.hpp"
#include <Renderer/Renderer.hpp>
#include <Renderer/Texture.hpp>
// --- 新增：引入场景序列化器 ---
#include "Scene/SceneSerializer.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Utils/PlatformUtils.hpp"
#include "Asset/AssetManager.hpp"

#include <imgui.h>

namespace Ayaya {

    enum class SceneState {
        Edit = 0, Play = 1
    };

    class EditorLayer : public Layer {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override;
        virtual void OnUpdate(Timestep ts) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Event& event) override;

    private:
        void SetupScene();
        void NewScene();
        void OpenScene();
        void SaveScene();
        void SaveSceneAs();

        bool OnKeyPressed(KeyPressedEvent& e);
        
        void HandleShortcuts();
        
        void UIRenderDockspace();
        void UIRenderMenuBar();
        void UIRenderViewport();
        void UIRenderGameViewport();
        
        void HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);

        // ==========================================
        // 新增：游玩模式相关的函数
        // ==========================================
        void OnScenePlay();
        void OnSceneStop();
        void UIRenderToolbar(); // 顶部工具栏 (放播放按钮)

    private:
        EditorCamera m_EditorCamera;
        std::shared_ptr<Scene> m_ActiveScene;
        std::shared_ptr<Scene> m_EditorScene; 
        SceneState m_SceneState = SceneState::Edit;

        // ==========================================
        // 新增：控制游戏运行状态的变量
        // ==========================================
        bool m_IsPaused = false;
        float m_TimeStepScale = 1.0f; // 1x, 2x, 4x 等倍速

        std::string m_CurrentScenePath = std::string();
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        ImVec2 m_ViewportBounds[2]; 
        
        std::shared_ptr<Framebuffer> m_GameFBO;
        glm::vec2 m_GameViewportSize = { 0.0f, 0.0f };

        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;
        PreferencesPanel m_PreferencesPanel;

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_InitialMousePos = { 0.0f, 0.0f }; 

        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE 的值
        Entity m_HoveredEntity = {}; 

        bool m_ShowGrid = true; // 默认开启网格
        bool m_ShowSkybox = true; // 默认开启网格
        bool m_EnableMSAA = true; // 默认开启抗锯齿
        bool m_ShowPreferencesWindow = false; // 控制偏好设置窗口的开关
    };

}