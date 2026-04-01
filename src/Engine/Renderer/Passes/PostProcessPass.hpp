#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

    class PostProcessPass : public RenderPass {
    public:
        PostProcessPass();
        virtual ~PostProcessPass() override = default; // 析构函数交给智能指针自动管理

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        
        // 核心：签名加入 cmd
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_PostProcessShader;
        std::shared_ptr<Framebuffer> m_PostProcessFBO;
        std::shared_ptr<VertexArray> m_EmptyVAO; // 替换为高级抽象
    };

}