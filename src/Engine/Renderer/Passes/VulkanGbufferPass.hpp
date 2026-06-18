#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"

namespace Ayaya {

    // Bindless push constant layout — all vec4/uvec4 packed for GLSL std430 alignment
    struct alignas(16) GBufferPushConstants {
        glm::mat4 Transform;                       // offset 0   (64B)
        glm::vec4 Albedo_ReceiveShadows;           // offset 64  (16B): xyz=Albedo, w=ReceiveShadows
        glm::vec4 Metallic_Roughness_AO_Alpha;     // offset 80  (16B): x=Metallic, y=Roughness, z=AO, w=Alpha
        glm::vec4 AlphaCutoff_BlendMode_UseORMMap; // offset 96  (16B): x=AlphaCutoff, y=BlendMode, z=UseORMMap
        // Indices0: AlbedoMap, NormalMap, ORMMap, MetallicMap
        uint32_t AlbedoMapIndex = 1;
        uint32_t NormalMapIndex = 3;
        uint32_t ORMMapIndex = 2;
        uint32_t MetallicMapIndex = 1;             // offset 112 (16B)
        // Indices1: RoughnessMap, AOMap, AlphaMap, IsSelected
        uint32_t RoughnessMapIndex = 1;
        uint32_t AOMapIndex = 1;
        uint32_t AlphaMapIndex = 1;
        uint32_t IsSelected = 0;                   // offset 128 (16B)
    };
    static_assert(sizeof(GBufferPushConstants) == 144,
        "GBufferPushConstants must match GLSL layout exactly");

    class VulkanGBufferPass : public RenderPass {
    public:
        VulkanGBufferPass() { m_PassName = "G-Buffer Pass"; }
        ~VulkanGBufferPass() override;
        virtual void OnAttach() override;
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) override;
        static void DeclareResources(class RGBuilder& builder, uint32_t width, uint32_t height);

    private:
        std::shared_ptr<Shader> m_GBufferShader;
        std::shared_ptr<Framebuffer> m_RefFBO;  // format reference FBO
        PipelineSpecification m_PipeSpec;
        std::shared_ptr<Pipeline> m_Pipeline;

        // Instanced path — linear run-length batching (packets pre-sorted by SortKey)
        static constexpr uint32_t kMaxInstances = 4096;
        std::shared_ptr<Shader>        m_InstancedShader;
        std::shared_ptr<Pipeline>      m_InstancedPipeline;
        PipelineSpecification          m_InstancedSpec;
        std::unique_ptr<VulkanStorageBuffer> m_InstanceBuffer;
        std::vector<VkDescriptorSet>   m_InstanceDescriptorSets;
        VkDescriptorSetLayout          m_InstanceSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool               m_InstancePool = VK_NULL_HANDLE;

        // GPU-Driven Rendering (GDR) — deferred (needs bindless fragment shader)
        std::shared_ptr<Shader> m_GDRShader;  // compiled, pipeline created when frag shader ready

        // Bindless texture pipelines (UseBindlessTextures=true)
        std::shared_ptr<Shader>   m_BindlessShader;
        std::shared_ptr<Pipeline> m_BindlessPipeline;
        std::shared_ptr<Shader>   m_BindlessInstancedShader;
        std::shared_ptr<Pipeline> m_BindlessInstancedPipeline;
    };
}
