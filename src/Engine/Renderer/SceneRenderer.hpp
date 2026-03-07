#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    class SceneRenderer {
    public:
        // 初始化管线（加载必备 Shader 和默认材质等）
        static void Init();

        // 开始一帧的渲染准备
        static void BeginScene(const glm::mat4& viewProjection);
        
        // 渲染整个 3D 场景的核心管线
        static void RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid);
        
        // 结束一帧的渲染
        static void EndScene();
    };

}