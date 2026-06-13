#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Engine/Scene/Entity.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"

namespace Ayaya {

    struct alignas(16) GBufferPushConstants {
        glm::mat4 Transform;
        alignas(16) glm::vec3 Albedo;
        float ReceiveShadows;
        float Metallic;
        float Roughness;
        float AO;
        float AlphaMultiplier;
        float AlphaCutoff;
        int   BlendMode;       // 0=Opaque, 1=Masked
        int UseAlbedoMap;
        int UseNormalMap;
        int UseORMMap;        // UE4-style ORM packed texture
        int UseMetallicMap;
        int UseRoughnessMap;
        int UseAOMap;
        int UseAlphaMap;
    };

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
    };
}
