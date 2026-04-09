#pragma once
#include "Renderer/UniformBuffer.hpp"
#include <vulkan/vulkan.h>

namespace Ayaya {

    class VulkanUniformBuffer : public UniformBuffer {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~VulkanUniformBuffer() override;

        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

    private:
        uint32_t m_Binding = 0;
        uint32_t m_Size = 0;

        // 未来将由 VMA 管理以下资源
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_Memory = VK_NULL_HANDLE;
        void* m_MappedMemory = nullptr;
    };
}