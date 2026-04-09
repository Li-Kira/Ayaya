#include "ayapch.h"
#include "VulkanUniformBuffer.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding)
        : m_Size(size), m_Binding(binding) {
        // 占位逻辑：此处未来应包含 vkCreateBuffer 和内存绑定
        AYAYA_CORE_WARN("VulkanUniformBuffer created (Stub): size {0}, binding {1}", size, binding);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer() {
        // 此处应包含 vkDestroyBuffer
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
        // 未来逻辑：memcpy 到 m_MappedMemory 或使用 Staging Buffer
        AYAYA_CORE_TRACE("VulkanUniformBuffer::SetData: Updating {0} bytes at offset {1}", size, offset);
    }
}