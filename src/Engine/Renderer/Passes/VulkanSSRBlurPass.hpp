#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

class VulkanSSRBlurPass : public RenderPass {
public:
    VulkanSSRBlurPass();
    ~VulkanSSRBlurPass() override = default;

    void OnAttach() override;
    void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    static void DeclareResources(class RGBuilder& builder,
                                 uint32_t width, uint32_t height);

private:
    // Blur pipelines (same shader, different BlurDir push constant)
    std::shared_ptr<Shader>      m_BlurShader;
    std::shared_ptr<Pipeline>    m_BlurXPipeline;
    std::shared_ptr<Pipeline>    m_BlurYPipeline;
    std::shared_ptr<Framebuffer> m_RefFBO;

    // Internal half-res FBO for intermediate blur result
    std::shared_ptr<Framebuffer> m_BlurXFBO;
    uint32_t m_LastW = 0, m_LastH = 0;
};

} // namespace Ayaya
