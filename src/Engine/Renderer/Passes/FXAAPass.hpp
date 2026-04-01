#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

    class FXAAPass : public RenderPass {
    public:
        FXAAPass();
        // 【修改】：使用智能指针后，不需要手动写析构函数去 delete VAO 了
        virtual ~FXAAPass() override = default; 

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        
        // 【修改】：加上 cmd 参数
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_FXAAShader;
        std::shared_ptr<Framebuffer> m_FXAAFBO;
        
        // 【修改】：抛弃 uint32_t 原生句柄，使用引擎 HAL 抽象
        std::shared_ptr<VertexArray> m_EmptyVAO; 
    };

}