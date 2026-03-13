#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Renderer/Framebuffer.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    class SceneRenderer {
    public:
        // 初始化管线（加载必备 Shader 和默认材质等）
        static void Init();

        // --- 新增：视口尺寸改变时通知管线重建 FBO ---
        static void OnWindowResize(uint32_t width, uint32_t height);
        // --- 新增：给 Editor 提供开关 MSAA 的能力 ---
        static void SetMSAASamples(uint32_t samples);
        // --- 新增：获取最终处理完毕的屏幕贴图 ---
        static uint32_t GetFinalColorAttachmentRendererID();

        // 开始一帧的渲染准备
        static void BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition);
        
        // 渲染整个 3D 场景的核心管线
        static void RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool  showSkybox);
        
        // 结束一帧的渲染
        static void EndScene();
    };

}