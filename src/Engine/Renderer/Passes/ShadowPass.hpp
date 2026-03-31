#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"

namespace Ayaya {

    class ShadowPass : public RenderPass {
    public:
        ShadowPass();
        virtual ~ShadowPass() override;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context) override;

    private:
        std::shared_ptr<Shader> m_ShadowShader;
        uint32_t m_ShadowMapFBO = 0;
        uint32_t m_ShadowMapTexture = 0;
    };

}