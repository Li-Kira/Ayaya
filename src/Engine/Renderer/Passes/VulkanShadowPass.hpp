#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/RenderGraph.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Platform/Vulkan/VulkanGeometryPool.hpp"
#include "Renderer/GDRContext.hpp"

namespace Ayaya {

    class VulkanContext;  // forward declaration for method signatures

    struct VulkanShadowPushConstants {
        glm::mat4 LightSpaceMatrix;
        glm::mat4 Transform;
        // NormalOffsetScale hardcoded in shader (1.5) to stay within 128-byte push constant guarantee
    };

    class VulkanShadowPass : public RenderPass {
    public:
        VulkanShadowPass();
        virtual ~VulkanShadowPass() override;

        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;

        static void DeclareResources(RGBuilder& builder);

        void SetGDRContext(std::shared_ptr<GDRContext> ctx) { m_GDRCtx = ctx; }

    private:
        // ── Legacy CPU-driven path ──
        std::shared_ptr<Shader>   m_ShadowShader;
        std::shared_ptr<Pipeline> m_Pipeline;
        PipelineSpecification     m_PipeSpec;

        // ── GDR mode ──
        std::shared_ptr<GDRContext> m_GDRCtx;
        bool m_UseGDR = true;

        // Shared set=2 layout from GDRCtx
        VkDescriptorSetLayout m_GDR_Set2Layout = VK_NULL_HANDLE;

        bool m_HasDrawIndirectCount = false;  // set from VulkanCapabilities at init

        // ── Atomic path (desktop GPUs): set=3 with 4 bindings (binned indirect + counts) ──
        VkDescriptorSetLayout m_ShadowSet3Layout = VK_NULL_HANDLE;
        VkDescriptorPool      m_ShadowSet3Pool   = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_ShadowSet3Descriptors;

        // Compute culling pipeline (atomic variant)
        VkPipelineLayout      m_CullLayout = VK_NULL_HANDLE;
        VkPipeline            m_CullPipeline = VK_NULL_HANDLE;
        VkShaderModule        m_CullShaderModule = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_CullDummyLayout = VK_NULL_HANDLE; // empty layout for set 0/1 placeholders

        // Indirect command + count buffers (binned opaque + masked, atomic path only)
        std::unique_ptr<VulkanStorageBuffer> m_OpaqueIndirectBuffer;
        std::unique_ptr<VulkanStorageBuffer> m_OpaqueCountBuffer;
        std::unique_ptr<VulkanStorageBuffer> m_MaskedIndirectBuffer;
        std::unique_ptr<VulkanStorageBuffer> m_MaskedCountBuffer;

        // ── Fixed-slot path (MoltenVK/M1 fallback): set=3 with 2 bindings (indirect only) ──
        VkDescriptorSetLayout m_ShadowSet3LayoutFixed = VK_NULL_HANDLE;
        VkDescriptorPool      m_ShadowSet3PoolFixed   = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_ShadowSet3DescriptorsFixed;

        VkPipelineLayout      m_CullLayoutFixed = VK_NULL_HANDLE;
        VkPipeline            m_CullPipelineFixed = VK_NULL_HANDLE;
        VkShaderModule        m_CullShaderModuleFixed = VK_NULL_HANDLE;

        // GDR graphics pipelines
        std::shared_ptr<Shader>       m_GDR_OpaqueShader;
        std::shared_ptr<Pipeline>     m_GDR_OpaquePipeline;
        std::shared_ptr<Framebuffer>  m_ShadowRefFBO;

        // Masked pipeline (Phase 3)
        std::shared_ptr<Shader>       m_GDR_MaskedShader;
        std::shared_ptr<Pipeline>     m_GDR_MaskedPipeline;

        static constexpr uint32_t kGDRMaxInstances = 65536;

        void InitGDR(VkDevice device, uint32_t framesInFlight);
        void ExecuteGDR(RenderContext& context, RenderCommandBuffer& cmd);
        void ExecuteGDR_Atomic(RenderContext& context, RenderCommandBuffer& cmd,
                               VulkanContext* vkCtx, uint32_t frameIdx, const glm::mat4& lightSpaceMatrix);
        void ExecuteGDR_Fixed(RenderContext& context, RenderCommandBuffer& cmd,
                              VulkanContext* vkCtx, uint32_t frameIdx, const glm::mat4& lightSpaceMatrix);
        void ExecuteCPU(RenderContext& context, RenderCommandBuffer& cmd);
    };

}
