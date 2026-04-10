#pragma once
#include "Renderer/UniformBuffer.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> // 引入 VMA

namespace Ayaya {

    class VulkanUniformBuffer : public UniformBuffer {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~VulkanUniformBuffer() override;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

    private:
        uint32_t m_Binding = 0;
        uint32_t m_Size = 0;

        // VMA 管理的资源
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VmaAllocationInfo m_AllocInfo; // 存放映射好的内存地址
    };
}