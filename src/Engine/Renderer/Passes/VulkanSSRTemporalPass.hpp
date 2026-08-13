#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

class VulkanSSRTemporalPass : public RenderPass {
public:
    VulkanSSRTemporalPass();
    ~VulkanSSRTemporalPass() override = default;

    void OnAttach() override;
    void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    static void DeclareResources(class RGBuilder& builder,
                                  uint32_t width, uint32_t height);

private:
    std::shared_ptr<Shader>      m_Shader;
    std::shared_ptr<Pipeline>    m_Pipeline;
    std::shared_ptr<Framebuffer> m_RefFBO;

    // Internal history buffers — NOT RenderGraph managed (temporal accumulation
    // needs to read previous frame data, which triple-buffered RGTextures don't provide)
    std::shared_ptr<Framebuffer> m_HistoryFBO[3];  // RGBA16F, ½res, per frame-in-flight
    uint64_t m_FrameCount = 0;  // detects first frame (history invalid)
    uint32_t m_LastW = 0, m_LastH = 0;
};

} // namespace Ayaya
