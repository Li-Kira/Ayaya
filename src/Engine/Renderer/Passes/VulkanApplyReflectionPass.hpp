#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

class VulkanApplyReflectionPass : public RenderPass {
public:
    VulkanApplyReflectionPass();
    ~VulkanApplyReflectionPass() override = default;

    void OnAttach() override;
    void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

private:
    std::shared_ptr<Shader>    m_Shader;
    std::shared_ptr<Pipeline>  m_Pipeline;
    std::shared_ptr<Framebuffer> m_RefFBO;
};

} // namespace Ayaya
