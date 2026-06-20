#include "ayapch.h"
#include "VulkanGeometryPool.hpp"
#include "Renderer/Mesh.hpp"
#include "Core/Log.hpp"
#include <cstring>

namespace Ayaya {

    void GlobalGeometryPool::Init(VkDevice device, VmaAllocator allocator, VkDeviceSize size) {
        m_Device = device;
        m_Allocator = allocator;
        m_Size = size;
        m_Cursor = 0;

        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                         | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                         | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;  // must be coherent: no explicit flush needed

        VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
            &m_Buffer, &m_Allocation, nullptr);
        if (result != VK_SUCCESS) {
            AYAYA_CORE_ERROR("GlobalGeometryPool: Failed to allocate {0} MB", size / (1024*1024));
            return;
        }
        AYAYA_CORE_INFO("GlobalGeometryPool: Allocated {0} MB", size / (1024*1024));
    }

    void GlobalGeometryPool::Shutdown() {
        if (m_Buffer) {
            vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
            m_Buffer = VK_NULL_HANDLE;
        }
    }

    GeometryRange GlobalGeometryPool::Upload(
        const void* vertexData, VkDeviceSize vertexSize,
        const void* indexData,  VkDeviceSize indexSize,
        uint32_t vertexCount, uint32_t indexCount)
    {
        GeometryRange range{};

        // Bounds check: ensure pool has space for vertex + index data (with 16-byte alignment padding)
        VkDeviceSize required = vertexSize + indexSize + 32; // 32 bytes slop for alignment
        if (m_Cursor + required > m_Size) {
            AYAYA_CORE_ERROR("GlobalGeometryPool: out of space! Cursor={0} + Required={1} > Size={2} (256MB limit). "
                "Consider increasing pool size or reducing mesh count.", m_Cursor, required, m_Size);
            return range;
        }

        // Map and write vertex data
        if (vertexData && vertexSize > 0) {
            range.vertexOffset = (uint32_t)m_Cursor;
            range.vertexCount  = vertexCount;

            void* mapped = nullptr;
            vmaMapMemory(m_Allocator, m_Allocation, &mapped);
            memcpy((char*)mapped + m_Cursor, vertexData, (size_t)vertexSize);
            vmaUnmapMemory(m_Allocator, m_Allocation);

            m_Cursor += vertexSize;
            m_Cursor = (m_Cursor + 15) & ~15ull; // 16-byte align
        }

        // Map and write index data
        if (indexData && indexSize > 0) {
            range.indexOffset = (uint32_t)m_Cursor;
            range.indexCount  = indexCount;

            void* mapped = nullptr;
            vmaMapMemory(m_Allocator, m_Allocation, &mapped);
            memcpy((char*)mapped + m_Cursor, indexData, (size_t)indexSize);
            vmaUnmapMemory(m_Allocator, m_Allocation);

            m_Cursor += indexSize;
            m_Cursor = (m_Cursor + 15) & ~15ull;
        }

        return range;
    }

    GeometryRange GlobalGeometryPool::GetOrUploadMesh(Mesh* mesh) {
        if (!mesh) return {};
        auto it = m_MeshRanges.find(mesh);
        if (it != m_MeshRanges.end()) return it->second;

        const auto& verts = mesh->GetRawVertices();
        const auto& inds  = mesh->GetRawIndices();
        if (verts.empty() || inds.empty()) return {};

        uint32_t vCount = mesh->GetVertexCount();
        uint32_t iCount = mesh->GetIndexCount();
        auto range = Upload(verts.data(), vCount * sizeof(Vertex),
                            inds.data(),  iCount * sizeof(uint32_t),
                            vCount, iCount);
        // Convert byte offsets: vertex → uint elements for SSBO, index stays as byte offset
        range.vertexOffset = range.vertexOffset / 4;
        m_MeshRanges[mesh] = range;
        return range;
    }

}
