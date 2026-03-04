#pragma once

#include <Ayaya.hpp>
#include "Panels/SceneHierarchyPanel.hpp"
#include <Renderer/Renderer.hpp>
#include <Renderer/Shader.hpp>
#include <Renderer/Texture.hpp>
#include <Renderer/Framebuffer.hpp>

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
        std::shared_ptr<Texture2D> m_Texture;
        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<Framebuffer> m_Framebuffer; 

        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };         
        ImVec2 m_ViewportBounds[2]; 

        std::shared_ptr<Scene> m_ActiveScene;
        SceneHierarchyPanel m_SceneHierarchyPanel;

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        glm::vec2 m_InitialMousePos = { 0.0f, 0.0f };

        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE 的值
        Entity m_HoveredEntity = {}; 
    };

}