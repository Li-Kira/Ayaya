#pragma once
#include "Platform/Vulkan/VulkanStorageBuffer.hpp"
#include "Platform/Vulkan/VulkanGeometryPool.hpp"
#include "Renderer/RenderQueue.hpp"
#include <cstdint>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Ayaya {

// Shared GPU-Driven Rendering data hub.
// Owned by SceneRenderer, referenced by Shadow and GBuffer passes.
// Builds GPUInstance[], GeometryRange[], GPUMaterial[] once per frame from the sorted RenderQueue.
struct GDRContext {
    static constexpr uint32_t kMaxInstances = 65536;
    static constexpr uint32_t kMaxMaterials = 512;
    static constexpr uint32_t kMaxMeshes    = 1024;

    // ── Shared, read-only scene-data SSBOs ──
    std::unique_ptr<VulkanStorageBuffer> InstanceSSBO;       // GPUInstance[]
    std::unique_ptr<VulkanStorageBuffer> GeometryRangeSSBO;  // GeometryRange[]
    std::unique_ptr<VulkanStorageBuffer> MaterialSSBO;       // GPUMaterial[]

    // ── Set=2 descriptor layout + pool + per-frame-in-flight descriptor sets ──
    // Binding 0 = InstanceSSBO   (VERTEX | FRAGMENT | COMPUTE)
    // Binding 1 = RangeSSBO      (VERTEX | COMPUTE)
    // Binding 2 = MaterialSSBO   (VERTEX | FRAGMENT | COMPUTE)
    // Binding 3 = GeometryPool   (VERTEX)
    VkDescriptorSetLayout Set2Layout = VK_NULL_HANDLE;
    VkDescriptorPool      Set2Pool   = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> Set2Descriptors;

    uint32_t InstanceCount = 0;
    float BuildTimeMs = 0.0f;  // CPU time of last BuildFromRenderQueue call

    ~GDRContext() { Shutdown(); }

    void Init(VkDevice device, uint32_t framesInFlight, VkBuffer geoPoolBuffer);
    void Shutdown();

    // Build SSBO data from sorted RenderQueue (called once per frame, before both passes).
    void BuildFromRenderQueue(const RenderQueue& queue,
                              GlobalGeometryPool& geoPool,
                              uint32_t frameIndex);

    // Build SSBO data directly from ECS — blind submit (no CPU culling).
    void BuildFromScene(class Scene* scene,
                        GlobalGeometryPool& geoPool,
                        uint32_t frameIndex);

    // Convenience: bind set=2 at the given pipeline bind point.
    void BindSet2(VkCommandBuffer cmd, VkPipelineLayout layout,
                  VkPipelineBindPoint bindPoint, uint32_t frameIndex) const;

private:
    VkDevice m_Device = VK_NULL_HANDLE;         // cached for Shutdown (no ctx dependency)
    uint32_t m_LastBuiltFrame = UINT32_MAX;   // guard against redundant same-frame rebuilds
};

} // namespace Ayaya
