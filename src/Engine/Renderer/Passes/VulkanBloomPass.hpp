#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct VulkanBloomMip {
        glm::vec2 Size;
        glm::vec2 IntSize;
        std::shared_ptr<Framebuffer> FBO;
    };

    struct alignas(16) BloomDownsamplePushConstants {
        glm::vec2 TexelSize;
        int MipLevel;
        float Threshold;
        glm::vec3 Curve;
    };

    struct alignas(16) BloomUpsamplePushConstants {
        glm::vec2 FilterRadius;   // per-axis tent radius (source texels in UV units)
    };

    class VulkanBloomPass : public RenderPass {
    public:
        VulkanBloomPass();
        virtual ~VulkanBloomPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_DownsampleShader;
        std::shared_ptr<Shader> m_UpsampleShader;

        // Mip 0 = RenderGraph-managed "Bloom" FBO; mips 1-4 = pass-internal
        std::vector<VulkanBloomMip> m_InternalMips; // indices 0-3 = mips 1-4
        uint32_t m_LastVPWidth  = 0;
        uint32_t m_LastVPHeight = 0;

        std::shared_ptr<Pipeline> m_DownsamplePipeline;
        std::shared_ptr<Pipeline> m_UpsamplePipeline;
    };

}
