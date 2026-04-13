#include "ayapch.h"
#include "VulkanUniformBuffer.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"
#include <cstring> // for memcpy

namespace Ayaya {

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding)
        : m_Size(size), m_Binding(binding) {
        
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        // 1. 声明这是一个 Uniform Buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // 2. VMA 高级配置：自动选择内存位置，并声明我们需要从 CPU 频繁写入
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        // 【魔法参数】：CREATE_MAPPED_BIT 让 VMA 直接帮我们把显存映射到 CPU 内存空间！
        // HOST_ACCESS_SEQUENTIAL_WRITE_BIT 告诉底层驱动我们会经常覆盖写入，驱动会为其优化带宽。
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &m_AllocInfo);
        Ayaya::VulkanPipeline::SetGlobalUniformBuffer(binding, m_Buffer, size);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan Uniform Buffer!");
    }

    VulkanUniformBuffer::~VulkanUniformBuffer() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (context && m_Buffer != VK_NULL_HANDLE) {
            // VMA 的销毁非常省心，一行代码带走 Buffer 和 显存块
            vmaDestroyBuffer(context->GetAllocator(), m_Buffer, m_Allocation);
        }
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
        // 由于我们使用了 MAPPED_BIT，m_AllocInfo.pMappedData 就是直通显存的 CPU 指针！
        // 直接使用 memcpy 暴力写入，没有任何额外 API 开销，极致性能！
        if (m_AllocInfo.pMappedData) {
            memcpy((uint8_t*)m_AllocInfo.pMappedData + offset, data, size);
        }
    }
}