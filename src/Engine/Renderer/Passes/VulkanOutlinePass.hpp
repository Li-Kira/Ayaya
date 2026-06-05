#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/VertexArray.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {
    class VulkanOutlinePass : public RenderPass {
    public:
        VulkanOutlinePass() { m_PassName = "Outline"; }
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& ctx, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t w, uint32_t h);
    private:
        // GBuffer mask extraction (full-screen pass)
        std::shared_ptr<Shader> m_MaskShader;
        std::shared_ptr<VertexArray> m_EmptyVAO;
        PipelineSpecification m_MaskPipeSpec;
        std::shared_ptr<Pipeline> m_MaskPipeline;

        // Geometry rendering for full silhouette (meshes)
        std::shared_ptr<Shader> m_GeomShader;
        PipelineSpecification m_GeomPipeSpec;
        std::shared_ptr<Pipeline> m_GeomPipeline;

        // Sprite silhouette (vertex-index-driven quad)
        std::shared_ptr<Shader> m_SpriteShader;
        PipelineSpecification m_SpritePipeSpec;
        std::shared_ptr<Pipeline> m_SpritePipeline;

        std::shared_ptr<Framebuffer> m_RefFBO;
    };
}
