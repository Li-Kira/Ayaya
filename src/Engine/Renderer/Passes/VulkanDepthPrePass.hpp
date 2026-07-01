#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/GDRContext.hpp"
#include <vulkan/vulkan.h>
#include <memory>

namespace Ayaya {

    // Standalone depth pre-pass: GDR compute culling → depth-only indirect draw.
    // Writes SceneDepth only (no color attachments). Subsequent passes
    // (GBuffer, SSAO, Decals) read this depth for Early-Z / reconstruction.
    class VulkanDepthPrePass : public RenderPass {
    public:
        VulkanDepthPrePass() { m_PassName = "Depth Pre-Pass"; }
        ~VulkanDepthPrePass() override;

        void OnAttach() override;
        void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t vpW, uint32_t vpH);
        void SetGDRContext(std::shared_ptr<GDRContext> ctx) { m_GDRCtx = std::move(ctx); }

    private:
        std::shared_ptr<GDRContext>  m_GDRCtx;
        std::shared_ptr<Shader>      m_Shader;      // gbuffer_gdr.vert + depth_only.frag
        std::shared_ptr<Pipeline>    m_Pipeline;    // ColorWrite=false, LESS, DepthWrite=ON

        // Compute culling (independent)
        VkPipelineLayout        m_CullLayout = VK_NULL_HANDLE;
        VkPipeline              m_CullPipeline = VK_NULL_HANDLE;
        VkShaderModule          m_CullShader = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_CullDummyLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_CullSet3Layout = VK_NULL_HANDLE;
        VkDescriptorPool        m_CullSet3Pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_CullSet3Descriptors;
        std::unique_ptr<class VulkanStorageBuffer> m_DrawIndirectBuffer;
    };

} // namespace Ayaya
