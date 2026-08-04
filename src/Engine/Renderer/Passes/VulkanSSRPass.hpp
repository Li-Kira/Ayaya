#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class VulkanSSRPass : public RenderPass {
    public:
        VulkanSSRPass();
        virtual ~VulkanSSRPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder,
                                     uint32_t width, uint32_t height);

    private:
        // March pipeline (fullscreen triangle ray march)
        std::shared_ptr<Shader>      m_MarchShader;
        std::shared_ptr<Pipeline>    m_MarchPipeline;
        std::shared_ptr<Framebuffer> m_RefFBO;         // 1280×720 ref for pipeline creation

        // Internal half-res FBO (SSR_Result is RenderGraph-managed)
        std::shared_ptr<Framebuffer> m_ReflectionFBO;
        uint32_t m_LastW = 0, m_LastH = 0;
    };

} // namespace Ayaya
