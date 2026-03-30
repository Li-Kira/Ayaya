#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    class FXAAPass : public RenderPass {
    public:
        FXAAPass();
        virtual ~FXAAPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context) override;

    private:
        std::shared_ptr<Shader> m_FXAAShader;
        std::shared_ptr<Framebuffer> m_FXAAFBO;
        uint32_t m_EmptyVAO = 0;
    };

}