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

    // ── Aggregate debug statistics (populated during Build) ──
    uint32_t RangeCount = 0;      // unique meshes this frame
    uint32_t MaterialCount = 0;   // unique materials this frame
    uint32_t TotalTriangles = 0;  // sum of indexCount/3 across ranges
    uint32_t TotalVertices = 0;   // sum of vertexCount across ranges

    ~GDRContext() { Shutdown(); }

    void Init(VkDevice device, uint32_t framesInFlight, VkBuffer geoPoolBuffer);
    void Shutdown();

    // Build SSBO data from sorted RenderQueue (called once per frame, before both passes).
    void BuildFromRenderQueue(const RenderQueue& queue,
                              GlobalGeometryPool& geoPool,
                              uint64_t frameNumber);

    // Build SSBO data directly from ECS — all blend modes uploaded, GPU masks handle filtering
    void BuildFromScene(class Scene* scene,
                        GlobalGeometryPool& geoPool,
                        uint64_t frameNumber);

    // Convenience: bind set=2 at the given pipeline bind point.
    void BindSet2(VkCommandBuffer cmd, VkPipelineLayout layout,
                  VkPipelineBindPoint bindPoint, uint32_t frameIndex) const;

    // Query the SSBO material index assigned by BuildFromScene.
    // Returns 0 if the material hasn't been built this frame.
    uint32_t GetMaterialSSBOIndex(const Material* mat) const;

    // Clear all per-scene caches and force a full rebuild on the next frame.
    // Does NOT touch GeometryPool caches — those are global and persist across scenes.
    void ResetCachesAndForceRebuild();

private:
    VkDevice m_Device = VK_NULL_HANDLE;         // cached for Shutdown (no ctx dependency)
    uint64_t m_LastBuiltFrameNumber = UINT64_MAX;  // monotonic frame guard (never wraps)
    std::unordered_map<const Material*, uint32_t> m_MaterialToIndex; // Material* → SSBO index

    // Previous-frame per-entity world transforms, keyed by full 32-bit entity ID.
    // Populated by BuildFromScene; read for motion vectors (prevTransform).
    std::unordered_map<uint32_t, glm::mat4> m_PrevWorldTransforms;
};

} // namespace Ayaya
