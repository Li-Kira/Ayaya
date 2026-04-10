#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderPipeline.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    struct SceneRendererData;

    class SceneRenderer {
    public:

        SceneRenderer();
        ~SceneRenderer();

        void Init();
        static void Shutdown(); // 【新增】：全局图形资源清理器
        void OnWindowResize(uint32_t width, uint32_t height);
        void SetMSAASamples(uint32_t samples);
        void* GetFinalColorAttachmentRendererID();
        void* GetPostProcessFBORendererID();

        void BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition);
        void RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool showSkybox, const glm::vec4& clearColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));
        void EndScene();

        void SetEnvironment(EnvironmentComponent& envComp);
        void SetEnvironmentSettings(float intensity, const glm::vec3& ambientColor);
        void SetClearColor(const glm::vec4& color);

        void SetExposure(float exposure) { m_Exposure = exposure; }

        // 获取黑板上的中间阶段贴图 ID (用于帧调试器)
        void* GetBlackboardTextureID(std::string_view key);

        struct Statistics {
            uint32_t DrawCalls = 0;
            uint32_t ShaderBinds = 0;   // SetPass Calls
            uint32_t VertexCount = 0;
            uint32_t TriangleCount = 0; // Tris
            float CPUTime = 0.0f; // C++ 准备指令的时间
            float GPUTime = 0.0f; // 显卡真实渲染的时间
        };
        void ResetStats();
        Statistics GetStats();

        void SetDebugStepLimit(int limit) { m_RenderContext.DebugStepLimit = limit; }
        const RenderContext& GetRenderContext() const { return m_RenderContext; }
    private:
        // 每个 Renderer 实例独有的数据指针！
        std::unique_ptr<SceneRendererData> m_Data;

        RenderPipeline m_Pipeline; 
        RenderContext  m_RenderContext; // 持有一份上下文

        float m_Exposure = 1.0f;
    };

}