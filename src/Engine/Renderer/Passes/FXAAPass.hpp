#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp" // 【新增】：引入管线系统

namespace Ayaya {

    class FXAAPass : public RenderPass {
    public:
        FXAAPass();
        virtual ~FXAAPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_FXAAShader;
        std::shared_ptr<Framebuffer> m_FXAAFBO;
        std::shared_ptr<VertexArray> m_EmptyVAO;

        std::shared_ptr<Pipeline> m_Pipeline; // 【新增】：管线状态对象
    };

}