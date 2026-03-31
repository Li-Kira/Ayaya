#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    class BloomPass : public RenderPass {
    public:
        BloomPass();
        virtual ~BloomPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context) override;

    private:
        std::shared_ptr<Shader> m_BloomExtractShader;
        std::shared_ptr<Shader> m_BloomBlurShader;
        std::shared_ptr<Framebuffer> m_BloomFBO[2];
        uint32_t m_EmptyVAO = 0;
    };

}