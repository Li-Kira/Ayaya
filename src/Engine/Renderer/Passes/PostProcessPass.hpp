#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    class PostProcessPass : public RenderPass {
    public:
        PostProcessPass();
        virtual ~PostProcessPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context) override;

    private:
        std::shared_ptr<Shader> m_PostProcessShader;
        std::shared_ptr<Framebuffer> m_PostProcessFBO;
        uint32_t m_EmptyVAO = 0;
    };

}