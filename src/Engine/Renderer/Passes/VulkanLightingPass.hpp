#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    // Push constants follow GLSL std430 layout.
    // CRITICAL: GLSL mat4 always aligns to 16, but MSVC glm::mat4 aligns to 4.
    // Use alignas(16) on InverseViewProj to force 16-byte offset matching GLSL.
    struct alignas(16) DeferredLightingPushConstants {
        glm::mat4 LightSpaceMatrix;           // 64B @ 0
        alignas(16) glm::vec3 AmbientColor;   // 12B @ 64
        float Intensity;                      // 4B  @ 76
        int EnvMapEnabled;                    // 4B  @ 80
        int EnableSSAO;                       // 4B  @ 84
        float _pad1;                          // 4B  @ 88
        float _pad2;                          // 4B  @ 92
        alignas(16) glm::mat4 InverseViewProj;// 64B @ 96
    };
    static_assert(offsetof(DeferredLightingPushConstants, LightSpaceMatrix) == 0);
    static_assert(offsetof(DeferredLightingPushConstants, AmbientColor)    == 64);
    static_assert(offsetof(DeferredLightingPushConstants, Intensity)       == 76);
    static_assert(offsetof(DeferredLightingPushConstants, EnvMapEnabled)   == 80);
    static_assert(offsetof(DeferredLightingPushConstants, EnableSSAO)      == 84);
    static_assert(offsetof(DeferredLightingPushConstants, InverseViewProj) == 96);

    class VulkanLightingPass : public RenderPass {
    public:
        VulkanLightingPass() { m_PassName = "Lighting Pass"; }
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Framebuffer> m_RefFBO;
        std::shared_ptr<Shader> m_DeferredShader;
        PipelineSpecification m_DeferredPipeSpec;
        std::shared_ptr<Pipeline> m_DeferredPipeline;
    };
}
