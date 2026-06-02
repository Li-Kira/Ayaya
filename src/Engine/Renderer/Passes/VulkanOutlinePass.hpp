#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"

namespace Ayaya {

    class VulkanOutlinePass : public RenderPass {
    public:
        VulkanOutlinePass();
        virtual ~VulkanOutlinePass() override = default;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_OutlineShader;
        std::shared_ptr<Pipeline> m_OutlinePipeline;
        PipelineSpecification m_OutlinePipeSpec;
    };

}
