#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"

namespace Ayaya {

    class VulkanForwardBlendPass : public RenderPass {
    public:
        VulkanForwardBlendPass() { m_PassName = "ForwardBlend"; }
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        void RenderSkybox(RenderContext& ctx, RenderCommandBuffer& cmd);
        void RenderGrid(RenderContext& ctx, RenderCommandBuffer& cmd);
        void RenderSprites(RenderContext& ctx, RenderCommandBuffer& cmd);

        std::shared_ptr<Framebuffer> m_RefFBO;
        std::shared_ptr<Shader> m_SkyboxShader;
        PipelineSpecification m_SkyboxPipeSpec;
        std::shared_ptr<Pipeline> m_SkyboxPipeline;
        std::shared_ptr<Shader> m_GridShader;
        std::shared_ptr<Mesh> m_GridMesh;
        PipelineSpecification m_GridPipeSpec;
        std::shared_ptr<Pipeline> m_GridPipeline;

        std::shared_ptr<Shader> m_SpriteShader;
        PipelineSpecification m_SpritePipeSpec;
        std::shared_ptr<Pipeline> m_SpritePipeline;
    };
}
