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
        float FilterRadius;
    };

    class VulkanBloomPass : public RenderPass {
    public:
        VulkanBloomPass();
        virtual ~VulkanBloomPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::shared_ptr<Shader> m_DownsampleShader;
        std::shared_ptr<Shader> m_UpsampleShader;
        std::vector<VulkanBloomMip> m_MipChain;

        std::shared_ptr<Pipeline> m_DownsamplePipeline;
        std::shared_ptr<Pipeline> m_UpsamplePipeline;
    };

}
