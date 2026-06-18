#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

namespace Ayaya {

    // GPU-visible storage buffer (SSBO), triple-buffered, persistent-mapped.
    // SetData is a plain memcpy — zero Vulkan API overhead per frame.
    class VulkanStorageBuffer {
    public:
        VulkanStorageBuffer(uint32_t size, VkBufferUsageFlags extraFlags = 0);
        ~VulkanStorageBuffer();

        // Copy data into current-frame buffer (persistent mapped pointer, no vkMap).
        void SetData(const void* data, uint32_t size);

        VkBuffer GetBuffer(uint32_t frameIndex) const { return m_Buffers[frameIndex]; }
        uint32_t GetSize() const { return m_Size; }

    private:
        uint32_t m_Size = 0;
        uint32_t m_FramesInFlight = 3;

        std::vector<VkBuffer>       m_Buffers;
        std::vector<VmaAllocation>  m_Allocations;
        std::vector<VmaAllocationInfo> m_AllocInfos;
    };

}
