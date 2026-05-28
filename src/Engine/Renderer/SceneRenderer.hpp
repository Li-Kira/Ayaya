#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/UniformBuffer.hpp"
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    class RenderPass;
    struct SceneRendererData;

    class SceneRenderer {
    public:

        SceneRenderer();
        ~SceneRenderer();

        void Init();
        static void Shutdown();
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

        void* GetBlackboardTextureID(std::string_view key);

        struct Statistics {
            uint32_t DrawCalls = 0;
            uint32_t ShaderBinds = 0;
            uint32_t VertexCount = 0;
            uint32_t TriangleCount = 0;
            float CPUTime = 0.0f;
            float GPUTime = 0.0f;
        };
        void ResetStats();
        Statistics GetStats();

        void SetDebugStepLimit(int limit) { m_RenderContext.DebugStepLimit = limit; }
        const RenderContext& GetRenderContext() const { return m_RenderContext; }
    private:
        std::unique_ptr<SceneRendererData> m_Data;

        std::shared_ptr<UniformBuffer> m_CameraUniformBuffer;
        std::shared_ptr<UniformBuffer> m_LightUniformBuffer;

        // OpenGL 线性管线 (保持兼容)
        RenderPipeline m_Pipeline;

        // Vulkan RenderGraph (声明式 DAG + 自动 Barrier)
        RenderGraph m_RenderGraph;
        bool m_ViewportDirty = true;   // 首帧/Resize 时触发图重建

        // Pass 实例 — 由 Graph 调度，管线只持有 Shader/Pipeline
        std::shared_ptr<RenderPass> m_ForwardPass;
        std::shared_ptr<RenderPass> m_PostProcessPass;
        std::shared_ptr<RenderPass> m_UIPass;

        // GPU 资源延迟释放 (3 帧安全期)
        struct DeferredRelease {
            std::vector<std::shared_ptr<TextureCube>> TextureCubes;
            int FramesRemaining = 3;
        };
        std::vector<DeferredRelease> m_DeferredReleases;

        RenderContext  m_RenderContext;

        float m_Exposure = 1.0f;
    };

}