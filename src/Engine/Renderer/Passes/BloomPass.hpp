#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include <vector>

namespace Ayaya {

    class BloomPass : public RenderPass {
    public:
        BloomPass();
        virtual ~BloomPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        struct BloomMip {
            glm::vec2 Size;
            glm::vec2 IntSize;
            std::shared_ptr<Framebuffer> FBO;
        };

        std::vector<BloomMip> m_MipChain;
        std::shared_ptr<Shader> m_DownsampleShader;
        std::shared_ptr<Shader> m_UpsampleShader; 
        
        std::shared_ptr<VertexArray> m_EmptyVAO;
    };
}