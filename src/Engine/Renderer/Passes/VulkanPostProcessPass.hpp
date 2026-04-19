#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    // 必须和 vulkan_postprocess.frag 保持严格的内存对齐
    struct alignas(16) PostProcessPushConstants {
        float Exposure;
        int ToneMappingType;
        glm::vec2 TexelSize;
        int EnableBloom;
        float BloomIntensity;
    };

    class VulkanPostProcessPass : public RenderPass {
    public:
        VulkanPostProcessPass();
        virtual ~VulkanPostProcessPass() override = default;

        virtual void OnAttach() override;
        virtual void OnResize(uint32_t width, uint32_t height) override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

    private:
        std::vector<std::shared_ptr<Framebuffer>> m_PostProcessFBOs;
        std::shared_ptr<Shader> m_PostProcessShader;
        std::shared_ptr<Pipeline> m_Pipeline;
        std::shared_ptr<VertexArray> m_EmptyVAO; 
    };

}