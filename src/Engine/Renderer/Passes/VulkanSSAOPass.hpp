#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class VulkanSSAOPass : public RenderPass {
    public:
        VulkanSSAOPass();
        virtual ~VulkanSSAOPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder,
                                     uint32_t width, uint32_t height);

        static std::shared_ptr<Texture2D> GetNoiseTexture();
        static void ReleaseNoiseTexture();

    private:
        // Pipelines
        std::shared_ptr<Shader> m_GenShader;
        std::shared_ptr<Shader> m_BlurShader;
        std::shared_ptr<Framebuffer> m_RefFBO;
        std::shared_ptr<Pipeline> m_GenPipeline;
        std::shared_ptr<Pipeline> m_BlurXPipeline;
        std::shared_ptr<Pipeline> m_BlurYPipeline;

        // Internal half-res FBOs (only SSAO_Final is RenderGraph-managed)
        std::shared_ptr<Framebuffer> m_RawFBO;
        std::shared_ptr<Framebuffer> m_BlurXFBO;
        uint32_t m_LastW = 0, m_LastH = 0;

        // SSAO noise texture (generated once, shared across instances)
        static std::shared_ptr<Texture2D> s_NoiseTexture;
    };

}
