#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct alignas(16) VulkanShadowPushConstants {
        glm::mat4 LightSpaceMatrix;
        glm::mat4 Transform;
    };

    class VulkanShadowPass : public RenderPass {
    public:
        VulkanShadowPass();
        virtual ~VulkanShadowPass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_ShadowShader;
        std::shared_ptr<Framebuffer> m_ShadowMapFBO;
        std::shared_ptr<Pipeline> m_Pipeline;
    };

}
