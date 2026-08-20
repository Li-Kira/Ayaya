#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/UniformBuffer.hpp"
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/RenderQueue.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Ayaya {

    class RenderPass;
    class PipelineBuilder;
    struct SceneRendererData;

    // ==========================================
    // RenderViewConfig — 视图渲染配置, 解耦 EditorLayer 传参
    // ==========================================
    struct RenderViewConfig {
        bool IsEditorView  = false;   // 编辑器视图 (启用 Outline/Grid/Gizmo)
        bool EnableGrid    = false;   // 编辑器网格
        bool EnableOutline = false;   // 实体选中描边
        bool EnableSkybox  = true;    // 天空盒
        bool EnableBloom   = true;    // 泛光
        bool EnableSSAO    = false;   // 屏幕空间环境光遮蔽 (需 PostProcessVolume)
        bool EnableSprites = true;    // 2D 精灵
        std::vector<Entity> SelectedEntities{};  // 多选实体 (Outline)
        Entity HoveredEntity{};                  // 悬停实体
        glm::vec4 ClearColor{0.12f, 0.12f, 0.14f, 1.0f};
    };

    // ==========================================
    // CustomPostProcess — 用户自定义后处理扩展接口 (预留)
    // ==========================================
    class CustomPostProcess {
    public:
        virtual ~CustomPostProcess() = default;
        // 返回此 Pass 输出的纹理名 (支持链式 Ping-Pong)
        virtual std::string DeclareResources(class RGBuilder& builder,
            const std::string& currentInput, uint32_t w, uint32_t h) = 0;
        virtual void Execute(RenderContext& ctx, RenderCommandBuffer& cmd) = 0;
        bool InsertBeforeToneMapping = false; // true=HDR空间, false=LDR空间
    };

    class SceneRenderer {
    public:
        SceneRenderer();
        ~SceneRenderer();

        void Init();
        static void Shutdown();

        // Shared GDR data hub (GPUInstance[], GeometryRange[], GPUMaterial[] + set=2 descriptors)
        std::shared_ptr<struct GDRContext> m_GDRContext;  // per-renderer (dual-viewport safe)
        GDRContext* GetGDRContext() { return m_GDRContext.get(); }
        void ResetGDRCaches();  // Invalidate GDR caches after scene structure changes
        void OnWindowResize(uint32_t width, uint32_t height);
        void MarkViewportDirty() { m_ViewportDirty = true; }
        void* GetFinalColorAttachmentRendererID();
        void* GetPostProcessFBORendererID();

        void BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
                        const glm::vec3& cameraPosition);
        void RenderScene(const std::shared_ptr<Scene>& scene, const RenderViewConfig& config);
        void EndScene();

        void SetEnvironment(EnvironmentComponent& envComp);
        void SetEnvironmentSettings(float intensity, const glm::vec3& ambientColor);
        void SetMSAASamples(uint32_t samples);
        void SetClearColor(const glm::vec4& color);
        void SetExposure(float exposure) { m_Exposure = exposure; }

        // 后处理扩展链
        void AddCustomPostProcess(std::shared_ptr<CustomPostProcess> pass);
        void RemoveCustomPostProcess(const std::string& name);

        // ── SRP (Scriptable Render Pipeline) ──
        // Set a pipeline script asset. UUID=0 restores the hardcoded default pipeline.
        void SetSRPScript(UUID handle);
        UUID GetSRPScript() const { return m_SRPScriptHandle; }
        void MarkSRPDirty() { m_SRPDirty = true; m_ViewportDirty = true; }

        void* GetBlackboardTextureID(std::string_view key);

        struct Statistics {
            uint32_t DrawCalls = 0, ShaderBinds = 0, VertexCount = 0, TriangleCount = 0;
            float CPUTime = 0.0f, GPUTime = 0.0f;
            // GDR diagnostics
            uint32_t GDRInstanceCount = 0, GDRRangeCount = 0, GDRMaterialCount = 0;
        };
        void ResetStats();
        Statistics GetStats();

        void SetDebugStepLimit(int limit) { m_RenderContext.DebugStepLimit = limit; }
        const RenderContext& GetRenderContext() const { return m_RenderContext; }

    private:
        // 5阶段 RenderGraph 动态组装
        void BuildRenderGraph(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH);

        std::unique_ptr<SceneRendererData> m_Data;
        std::shared_ptr<UniformBuffer> m_CameraUniformBuffer;
        std::shared_ptr<UniformBuffer> m_DirLightUniformBuffer;
        std::unique_ptr<class VulkanStorageBuffer> m_PointLightSSBO;
        std::unique_ptr<class VulkanStorageBuffer> m_LightInstanceSSBO;
        std::unique_ptr<class VulkanStorageBuffer> m_SpotLightSSBO;
        std::unique_ptr<class VulkanStorageBuffer> m_SpotLightInstanceSSBO;

        RenderPipeline m_Pipeline;           // OpenGL
        RenderGraph    m_RenderGraph;        // Vulkan DAG
        bool m_ViewportDirty = true;
        std::string m_FinalExportTexture;    // 当前帧最终输出纹理名
        std::shared_ptr<Scene> m_LastScene;  // 检测场景切换 → 重置时序历史 (TAA/SSR)

        // Deferred Pass 实例池
        std::shared_ptr<RenderPass> m_ShadowPass;
        std::shared_ptr<RenderPass> m_GBufferPass;
        std::shared_ptr<RenderPass> m_DepthPass;
        std::shared_ptr<RenderPass> m_LightingPass;
        std::shared_ptr<RenderPass> m_ForwardBlendPass;
        std::shared_ptr<RenderPass> m_SSAOPass;
        std::shared_ptr<RenderPass> m_SSRPass;
        std::shared_ptr<RenderPass> m_SSRBlurPass;
        std::shared_ptr<RenderPass> m_SSRTemporalPass;
        std::shared_ptr<RenderPass> m_ApplyReflectionPass;
        std::shared_ptr<RenderPass> m_OutlinePass;
        std::shared_ptr<RenderPass> m_BloomPass;
        std::shared_ptr<RenderPass> m_TAAPass;
        std::shared_ptr<RenderPass> m_PostProcessPass;
        std::shared_ptr<RenderPass> m_FXAAPass;
        std::shared_ptr<RenderPass> m_UIPass;
        std::shared_ptr<class VulkanWBOITPass> m_WBOITPass;

        std::vector<std::shared_ptr<CustomPostProcess>> m_CustomPostProcesses;

        RenderContext m_RenderContext;
        RenderQueue  m_RenderQueue;
        float m_Exposure = 1.0f;
        uint32_t m_FrameIndex = 0;  // monotonic frame counter for TAA Halton jitter

        // ── SRP (Scriptable Render Pipeline) ──
        UUID m_SRPScriptHandle = 0;               // 0 = use hardcoded default
        std::unique_ptr<class PipelineBuilder> m_PipelineBuilder;  // PIMPL — avoids <sol/sol.hpp> in header
        bool m_SRPDirty = true;                    // re-execute Lua on next build

        void BuildRenderGraph_Default(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH);
        void BuildRenderGraph_SRP(const RenderViewConfig& config, uint32_t vpW, uint32_t vpH);
        void ApplyPerFrameCulling();
    };

}