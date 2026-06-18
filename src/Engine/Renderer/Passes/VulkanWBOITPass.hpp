#pragma once
#include "Renderer/RenderPipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Ayaya {

    // WBOIT Gather pass push constants (must match wboit_gather_bindless.frag).
    // In the instanced path, Transform is ignored (SSBO provides it).
    struct alignas(16) WBOITGatherPushConstants {
        glm::mat4 Transform;                       // offset 0   (64B)
        glm::vec4 Albedo;                          // offset 64  (16B)
        float  Metallic;                           // offset 80  (4B)
        float  Roughness;                          // offset 84  (4B)
        float  AO;                                 // offset 88  (4B)
        uint32_t UseORMMap;                        // offset 92  (4B)
        uint32_t AlbedoMapIndex;                   // offset 96  (4B)
        uint32_t NormalMapIndex;                   // offset 100 (4B)
        uint32_t ORMMapIndex;                      // offset 104 (4B)
        uint32_t MetallicMapIndex;                 // offset 108 (4B)
        uint32_t RoughnessMapIndex;                // offset 112 (4B)
        uint32_t AOMapIndex;                       // offset 116 (4B)
        float  Alpha;                              // offset 120 (4B)
    };
    static_assert(sizeof(WBOITGatherPushConstants) <= 256,
        "WBOITGatherPushConstants must fit within push constant limit");

    // Maximum translucent instances per frame (SSBO capacity).
    // Increase if you need >2048 overlapping transparent objects.
    static constexpr uint32_t kMaxInstances = 2048;

    // WBOIT (Weighted Blended Order-Independent Transparency)
    // Two-pass approach:
    //   1. Gather — all transparent objects rendered unordered into accumulation buffers
    //      (GPU-instanced: same Mesh+Material → single DrawIndexedInstanced)
    //   2. Resolve — full-screen composite of accumulation onto SceneColor_HDR

    class VulkanWBOITPass {
    public:
        VulkanWBOITPass() = default;
        ~VulkanWBOITPass();

        void OnAttach();
        void OnResize(uint32_t width, uint32_t height);

        // RenderGraph resource declarations
        static void DeclareGatherResources(class RGBuilder& builder,
                                           uint32_t width, uint32_t height);
        static void DeclareResolveResources(class RGBuilder& builder,
                                            uint32_t width, uint32_t height);

        // Execute callbacks
        void ExecuteGather(RenderContext& context, RenderCommandBuffer& cmd);
        void ExecuteResolve(RenderContext& context, RenderCommandBuffer& cmd);

    private:
        // Gather pass — non-instanced (fallback)
        std::shared_ptr<Shader>       m_GatherShader;
        std::shared_ptr<Pipeline>     m_GatherPipeline;
        std::shared_ptr<Framebuffer>  m_GatherRefFBO;
        PipelineSpecification         m_GatherSpec;

        // Gather pass — instanced (same Mesh+Material batches)
        std::shared_ptr<Shader>       m_InstancedShader;
        std::shared_ptr<Pipeline>     m_InstancedPipeline;
        PipelineSpecification         m_InstancedSpec;

        // SSBO for instance transforms (triple-buffered, persistent-mapped)
        std::unique_ptr<VulkanStorageBuffer> m_InstanceBuffer;
        // Descriptor set writing: we bind the SSBO into set=1 binding=6 each frame
        std::vector<VkDescriptorSet>  m_InstanceDescriptorSets;  // one per frame-in-flight
        VkDescriptorSetLayout         m_InstanceSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool              m_InstancePool = VK_NULL_HANDLE;

        // Resolve pass (full-screen quad onto Lighting HDR)
        std::shared_ptr<Shader>       m_ResolveShader;
        std::shared_ptr<Pipeline>     m_ResolvePipeline;
        std::shared_ptr<Framebuffer>  m_ResolveRefFBO;
        PipelineSpecification         m_ResolveSpec;

        // IBL descriptor set (set=3) — bound once per frame, not per-material
        VkDescriptorSetLayout         m_IBLSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool              m_IBLPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet>  m_IBLDescriptorSets;  // one per frame-in-flight

        // Bindless gather pipeline (UseBindlessTextures=true)
        std::shared_ptr<Shader>       m_BindlessInstancedShader;
        std::shared_ptr<Pipeline>     m_BindlessInstancedPipeline;
    };

}
