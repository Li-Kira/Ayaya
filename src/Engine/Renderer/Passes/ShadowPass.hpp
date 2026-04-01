#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

    class ShadowPass : public RenderPass {
    public:
        ShadowPass();
        virtual ~ShadowPass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_ShadowShader;
        std::shared_ptr<Framebuffer> m_ShadowMapFBO;
    };

}