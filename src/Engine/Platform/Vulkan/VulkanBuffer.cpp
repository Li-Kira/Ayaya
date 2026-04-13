#include "ayapch.h"
#include "VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    // ==========================================
    // 顶点缓冲区 (VBO)
    // ==========================================
    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; 
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // 自动映射显存到 CPU (为简单起见，暂不使用 Staging Buffer)
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &m_AllocInfo);

        // 直接拷贝数据进显存！
        memcpy(m_AllocInfo.pMappedData, vertices, size);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (m_Buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context->GetAllocator(), m_Buffer, m_Allocation);
        }
    }

    // ==========================================
    // 索引缓冲区 (IBO)
    // ==========================================
    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count) 
        : m_Count(count) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = count * sizeof(uint32_t); // uint32_t 索引！
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &m_AllocInfo);

        memcpy(m_AllocInfo.pMappedData, indices, count * sizeof(uint32_t));
    }

    VulkanIndexBuffer::~VulkanIndexBuffer() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (m_Buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context->GetAllocator(), m_Buffer, m_Allocation);
        }
    }

}