#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct alignas(16) DeferredLightingPushConstants {
        glm::mat4 LightSpaceMatrix;
        alignas(16) glm::vec3 AmbientColor;
        float Intensity;
        int EnvMapEnabled;
    };

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
