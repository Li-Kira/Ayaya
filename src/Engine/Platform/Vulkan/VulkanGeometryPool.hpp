#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Ayaya {

    // Per-sub-mesh range within the global geometry buffer
    struct GeometryRange {
        uint32_t vertexOffset = 0;   // uint element offset into SSBO (byteOffset / 4)
        uint32_t indexOffset  = 0;   // byte offset for vkCmdBindIndexBuffer / firstIndex
        uint32_t vertexCount  = 0;
        uint32_t indexCount   = 0;
    };

    // GPU-side instance data (per-frame SSBO)
    struct alignas(16) GPUInstance {
        glm::mat4 transform;         // 64 bytes
        glm::vec4 boundingSphere;    // 16 bytes — xyz=center, w=radius
        uint32_t geometryRangeIdx;   // index into geometryRanges[] SSBO
        uint32_t materialIdx;        // index into materials[] SSBO
        uint32_t entityId;           // for selection/hover
        uint32_t _pad;
    };

    // GPU-side material data (uploaded once, updated on material change)
    struct alignas(16) GPUMaterial {
        glm::vec4 albedo;                    // 16
        float metallic, roughness, ao, alpha; // 16
        int useAlbedoMap, useNormalMap, useORMMap;       // 12
        int useMetallicMap, useRoughnessMap, useAOMap;   // 12
        int albedoBindless, normalBindless, ormBindless;         // 12
        int metallicBindless, roughnessBindless, aoBindless;     // 12
        float alphaCutoff;                   // 4
        int   blendMode;                     // 4
        int   _pad[4];                       // 16
    };

    // Single global VkBuffer for ALL vertex + index data.
    // Bound via vkCmdBindVertexBuffers / vkCmdBindIndexBuffer with per-mesh offsets.
    // No manual SSBO vertex fetch needed.
    class GlobalGeometryPool {
    public:
        static constexpr VkDeviceSize kDefaultSize = 256ULL * 1024 * 1024;

        void Init(VkDevice device, VmaAllocator allocator, VkDeviceSize size = kDefaultSize);
        void Shutdown();

        // Append vertex + index data; return the range for binding.
        // vertexOffset is uint element offset (byteOffset / 4),
        // indexOffset is byte offset for vkCmdBindIndexBuffer / firstIndex.
        GeometryRange Upload(const void* vertexData, VkDeviceSize vertexSize,
                             const void* indexData,  VkDeviceSize indexSize,
                             uint32_t vertexCount, uint32_t indexCount);

        // Mesh lookup for GDR — registers the mesh on first call, returns cached range on subsequent.
        GeometryRange GetOrUploadMesh(class Mesh* mesh);

        VkBuffer GetBuffer() const { return m_Buffer; }

    private:
        VkBuffer      m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VmaAllocator  m_Allocator = VK_NULL_HANDLE;
        VkDevice      m_Device = VK_NULL_HANDLE;
        VkDeviceSize  m_Size = 0;
        VkDeviceSize  m_Cursor = 0;  // byte offset, grows linearly

        // O(1) mesh → range lookup; entries added on first upload, queried every frame
        std::unordered_map<class Mesh*, GeometryRange> m_MeshRanges;
    };

}
