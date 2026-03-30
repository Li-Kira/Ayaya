#pragma once

#include "Engine/Scene/Scene.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Renderer/Framebuffer.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    struct SceneRendererData;

    class SceneRenderer {
    public:

        SceneRenderer();
        ~SceneRenderer();

        void Init();
        void OnWindowResize(uint32_t width, uint32_t height);
        void SetMSAASamples(uint32_t samples);
        uint32_t GetFinalColorAttachmentRendererID();
        uint32_t GetPostProcessFBORendererID();

        void BeginScene(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition);
        void RenderScene(const std::shared_ptr<Scene>& scene, Entity hoveredEntity, bool showGrid, bool showSkybox, const glm::vec4& clearColor = glm::vec4(0.12f, 0.12f, 0.14f, 1.0f));
        void EndScene();

        void SetEnvironment(EnvironmentComponent& envComp);
        void SetEnvironmentSettings(float intensity, const glm::vec3& ambientColor);
        void SetClearColor(const glm::vec4& color);

        void SetBloomSettings(bool enable, float threshold, float intensity) {
            m_EnableBloom = enable; m_BloomThreshold = threshold; m_BloomIntensity = intensity;
        }
        void SetExposure(float exposure) { m_Exposure = exposure; }
        void SetToneMappingType(int type) { m_ToneMappingType = type; }

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
    private:
        // 每个 Renderer 实例独有的数据指针！
        std::unique_ptr<SceneRendererData> m_Data;

        float m_Exposure = 1.0f;
        int m_ToneMappingType = 1; // 默认使用更高级的 1: ACES

        bool m_EnableBloom = true;
        float m_BloomThreshold = 1.0f; // 亮度超过 1.0 (纯白) 就泛光
        float m_BloomIntensity = 1.0f;
    };

}