#include "ayapch.h"
#include "VulkanStorageBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    VulkanStorageBuffer::VulkanStorageBuffer(uint32_t size) : m_Size(size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        m_Buffers.resize(m_FramesInFlight);
        m_Allocations.resize(m_FramesInFlight);
        m_AllocInfos.resize(m_FramesInFlight);

        for (uint32_t i = 0; i < m_FramesInFlight; i++) {
            VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.size  = size;
            bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage   = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags   = VMA_ALLOCATION_CREATE_MAPPED_BIT
                              | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                &m_Buffers[i], &m_Allocations[i], &m_AllocInfos[i]);
        }
    }

    VulkanStorageBuffer::~VulkanStorageBuffer() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        if (!context) return;
        VmaAllocator allocator = context->GetAllocator();
        for (uint32_t i = 0; i < m_FramesInFlight; i++) {
            if (m_Buffers[i])
                vmaDestroyBuffer(allocator, m_Buffers[i], m_Allocations[i]);
        }
    }

    void VulkanStorageBuffer::SetData(const void* data, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(
            Application::Get().GetWindow().GetContext());
        uint32_t frameIndex = context->GetCurrentFrameIndex() % m_FramesInFlight;

        // Persistent mapped — just memcpy, zero API overhead
        memcpy(m_AllocInfos[frameIndex].pMappedData, data, size);
    }

}
