#pragma once

#include <Ayaya.hpp>
#include "EditorCamera.hpp"
#include "Panels/SceneHierarchyPanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include <Renderer/Renderer.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>
// --- 新增：引入场景序列化器 ---
#include "Scene/SceneSerializer.hpp"
#include "Renderer/MaterialSerializer.hpp"
#include "Utils/PlatformUtils.hpp"
#include "Asset/AssetManager.hpp"

#include <imgui.h>

namespace Ayaya {

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
        
        void HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);

    private:
        EditorCamera m_EditorCamera;
        std::shared_ptr<Framebuffer> m_Framebuffer; 
        std::shared_ptr<Scene> m_ActiveScene;
        std::string m_CurrentScenePath = std::string();

        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         
        ImVec2 m_ViewportBounds[2]; 

        
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_InitialMousePos = { 0.0f, 0.0f };

        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE 的值
        Entity m_HoveredEntity = {}; 

        bool m_ShowGrid = true; // 默认开启网格
        bool m_ShowSkybox = true; // 默认开启网格
        bool m_EnableMSAA = true; // 默认开启抗锯齿
    };

}