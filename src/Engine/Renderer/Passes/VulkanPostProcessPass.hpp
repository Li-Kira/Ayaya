#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    struct alignas(16) PostProcessPushConstants {
        float Exposure;
        int ToneMappingType;
        glm::vec2 TexelSize;
        int EnableBloom;
        float BloomIntensity;
        // Tint stored as 3 scalars: a glm::vec3/vec4 in a push constant would be
        // 4-byte aligned on the C++ side but 16-byte aligned in GLSL std430, shifting
        // the read by 8 bytes and tinting bloom (e.g. white -> yellow). Scalars avoid
        // the alignment mismatch entirely.
        float BloomTintR;
        float BloomTintG;
        float BloomTintB;
    };

    class VulkanPostProcessPass : public RenderPass {
    public:
        VulkanPostProcessPass();
        virtual ~VulkanPostProcessPass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        // 声明该 Pass 对 RenderGraph 的资源依赖
        static void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_PostProcessShader;
        std::shared_ptr<Pipeline> m_Pipeline;
        PipelineSpecification m_PipeSpec;
        std::shared_ptr<VertexArray> m_EmptyVAO;
    };

}
