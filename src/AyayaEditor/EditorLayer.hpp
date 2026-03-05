#pragma once

#include <Ayaya.hpp>
#include "Panels/SceneHierarchyPanel.hpp"
#include "Panels/ContentBrowserPanel.hpp"
#include <Renderer/Renderer.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>
// --- 新增：引入场景序列化器 ---
#include "Scene/SceneSerializer.hpp"
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
        void SetupGeometry();
        void SetupScene();

        // =====================================
        // 场景操作逻辑抽象
        // =====================================
        void NewScene();
        void OpenScene();
        void SaveScene();
        void SaveSceneAs();

        bool OnKeyPressed(KeyPressedEvent& e);
        
        void HandleShortcuts();
        bool GetPrimaryCamera(glm::mat4& outView, glm::mat4& outProjection, Timestep ts = 0.0f);
        void ProcessCameraInput(Timestep ts, TransformComponent& transform);
        void RenderScene(const glm::mat4& cameraViewProj);
        
        void UIRenderDockspace();
        void UIRenderMenuBar();
        void UIRenderViewport();
        
        void HandleMousePicking(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);
        void HandleGizmo(const glm::mat4& cameraViewMatrix, const glm::mat4& cameraProjectionMatrix);

    private:
        ShaderLibrary m_ShaderLibrary;
        std::shared_ptr<Texture2D> m_WhiteTexture; // 全局默认白底贴图
        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<Framebuffer> m_Framebuffer; 

        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         
        ImVec2 m_ViewportBounds[2]; 

        std::shared_ptr<Scene> m_ActiveScene;
        SceneHierarchyPanel m_SceneHierarchyPanel;
        ContentBrowserPanel m_ContentBrowserPanel;

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_InitialMousePos = { 0.0f, 0.0f };

        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE 的值
        Entity m_HoveredEntity = {}; 

        // ==========================================
        // 新增：记录当前打开的场景路径
        // ==========================================
        std::string m_CurrentScenePath = std::string();

        bool m_ShowGrid = true; // 默认开启网格
    };

}