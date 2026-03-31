#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include <vector>

namespace Ayaya {

    class BloomPass : public RenderPass {
    public:
        BloomPass();
        virtual ~BloomPass() override;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context) override;

    private:
        // 【新增】：定义降采样 Mip 金字塔结构
        struct BloomMip {
            glm::vec2 Size;
            glm::vec2 IntSize;
            std::shared_ptr<Framebuffer> FBO;
        };

        std::vector<BloomMip> m_MipChain;
        
        std::shared_ptr<Shader> m_DownsampleShader;
        std::shared_ptr<Shader> m_UpsampleShader; // 【新增】：升采样专用 Shader
        
        uint32_t m_EmptyVAO = 0;
    };
}