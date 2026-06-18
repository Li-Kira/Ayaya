#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Platform/Vulkan/VulkanGeometryPool.hpp"

namespace Ayaya {

    class VulkanGBufferPass : public RenderPass {
    public:
        VulkanGBufferPass() { m_PassName = "G-Buffer Pass"; }
        ~VulkanGBufferPass() override;
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Framebuffer> m_RefFBO;  // format reference FBO for GDR pipeline creation

        // GPU-Driven Rendering (GDR) — SSBO-based material & instance data
        static constexpr uint32_t kGDRMaxInstances = 4096;
        static constexpr uint32_t kGDRMaxMaterials = 512;

        std::shared_ptr<Shader>   m_GDRShader;      // gbuffer_gdr.vert + gbuffer_gdr_bindless.frag
        std::shared_ptr<Pipeline> m_GDRPipeline;

        static constexpr uint32_t kGDRMaxMeshes = 1024;

        std::unique_ptr<VulkanStorageBuffer> m_GDR_InstanceSSBO;       // GPUInstance[]
        std::unique_ptr<VulkanStorageBuffer> m_GDR_GeometryRangeSSBO;  // GeometryRange[]
        std::unique_ptr<VulkanStorageBuffer> m_GDR_MaterialSSBO;       // GPUMaterial[]

        VkDescriptorSetLayout m_GDR_SetLayout = VK_NULL_HANDLE;   // set=2: Instances + Ranges + Materials + Geometry
        VkDescriptorPool      m_GDR_Pool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_GDR_DescriptorSets;        // one per frame-in-flight

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

    };
}
