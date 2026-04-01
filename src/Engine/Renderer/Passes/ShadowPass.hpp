#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

    class ShadowPass : public RenderPass {
    public:
        ShadowPass();
        virtual ~ShadowPass() override;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_ShadowShader;
        // 阴影 FBO 极其特殊，我们退回原生的 OpenGL 句柄精确控制
        uint32_t m_ShadowMapFBO = 0;
        uint32_t m_ShadowMapTexture = 0;
    };

}