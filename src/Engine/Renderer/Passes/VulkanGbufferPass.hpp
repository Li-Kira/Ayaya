#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Platform/Vulkan/VulkanGeometryPool.hpp"
#include "Renderer/GDRContext.hpp"
#include <array>

namespace Ayaya {

    class VulkanGBufferPass : public RenderPass {
    public:
        VulkanGBufferPass() { m_PassName = "G-Buffer Pass"; }
        ~VulkanGBufferPass() override;
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

        void SetGDRContext(std::shared_ptr<GDRContext> ctx) { m_GDRCtx = ctx; }

    private:
        std::shared_ptr<Framebuffer> m_RefFBO;  // format reference FBO for GDR pipeline creation
        std::shared_ptr<GDRContext>  m_GDRCtx;   // shared GDR data hub

        // GPU-Driven Rendering (GDR) — SSBO-based material & instance data
        static constexpr uint32_t kGDRMaxInstances = 65536;
        static constexpr uint32_t kGDRMaxMaterials = 512;
        static constexpr uint32_t kGDRMaxMeshes = 1024;

        std::shared_ptr<Shader>   m_GDRShader;      // gbuffer_gdr.vert + gbuffer_gdr_bindless.frag
        std::shared_ptr<Pipeline> m_GDRPipeline;

        // Compute culling (Step 3) — GPU frustum cull → indirect draw commands
        VkPipelineLayout        m_Cull_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline              m_Cull_Pipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout   m_Cull_Set3Layout = VK_NULL_HANDLE; // DrawCommands
        VkDescriptorPool        m_Cull_Set3Pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_Cull_Set3Descriptors;      // one per frame-in-flight
        VkDescriptorSetLayout   m_Cull_DummyLayout = VK_NULL_HANDLE; // empty layout for unused set 0 placeholder
        VkShaderModule          m_Cull_ShaderModule = VK_NULL_HANDLE; // compute SPIR-V module

        // Indirect draw buffer — compute writes per-instance draw commands, graphics consumes
        std::unique_ptr<VulkanStorageBuffer> m_DrawIndirectBuffer;

        // ── Hi-Z Occlusion Culling (Temporal: previous-frame depth pyramid) ──
        // Ring-buffered per frame-in-flight to avoid GPU Write-After-Read hazards.
        struct HiZFrameResources {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation alloc = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;     // full mip chain
            VkSampler sampler = VK_NULL_HANDLE;     // NEAREST, CLAMP_TO_EDGE
            std::vector<VkImageView> mipSrcViews;   // per-mip SRC views for downsample
            std::vector<VkImageView> mipDstViews;   // per-mip DST views for downsample
            std::vector<VkDescriptorSet> mipDescSets; // per-mip descriptor sets (src+dst)
        };
        std::array<HiZFrameResources, 3> m_HiZFrames;
        std::array<glm::mat4, 3> m_HiZPrevView;       // camera matrices when this Hi-Z was built
        std::array<glm::mat4, 3> m_HiZPrevProj;
        uint32_t m_HiZViewportW = 0, m_HiZViewportH = 0;
        uint32_t m_HiZMipLevels = 0;
        uint32_t m_HiZFrameCount = 0;  // skips occlusion cull on first frame

        // Hi-Z build compute (copy depth → R32 level 0 + downsample chain)
        VkPipelineLayout m_HiZBuildLayout = VK_NULL_HANDLE;
        VkPipeline m_HiZBuildPipeline = VK_NULL_HANDLE;
        VkShaderModule m_HiZBuildShader = VK_NULL_HANDLE;
        VkShaderModule m_HiZDownsampleShader = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_HiZBuildSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_HiZBuildPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_HiZBuildSet0s;    // per mip: {sampler2D src, image2D dst}

        // Hi-Z occlusion cull compute
        VkPipelineLayout m_HiZCullLayout = VK_NULL_HANDLE;
        VkPipeline m_HiZCullPipeline = VK_NULL_HANDLE;
        VkShaderModule m_HiZCullShader = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_HiZCullSet4Layout = VK_NULL_HANDLE;
        VkDescriptorPool m_HiZCullSet4Pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_HiZCullSet4s;    // per frame-in-flight (reader side)

        void InitHiZResources(VkDevice device, VmaAllocator allocator, uint32_t framesInFlight,
                              uint32_t viewportW, uint32_t viewportH);
        void BuildHiZ(VkCommandBuffer vkCmd, uint32_t writeIdx,
                      std::shared_ptr<class VulkanFramebuffer> gbufferFBO);
        void CleanupHiZ();

    };
}
