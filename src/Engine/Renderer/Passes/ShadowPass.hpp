#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp" // 【新增】

namespace Ayaya {

    class ShadowPass : public RenderPass {
    public:
        ShadowPass();
        virtual ~ShadowPass() override = default; // 析构全交给智能指针

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_ShadowShader;
        std::shared_ptr<Framebuffer> m_ShadowMapFBO;
        std::shared_ptr<Pipeline> m_Pipeline; // 【新增】：阴影管线
    };
}