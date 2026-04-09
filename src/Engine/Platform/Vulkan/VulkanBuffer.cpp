#include "ayapch.h"
#include "VulkanBuffer.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    // ==========================================
    // Vulkan 顶点缓冲区实现 (Stub)
    // ==========================================
    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size) {
        AYAYA_CORE_WARN("VulkanVertexBuffer created (Stub): Static size {0} bytes", size);
        // 未来逻辑：
        // 1. 创建 CPU 可见的 Staging Buffer
        // 2. 将 vertices 数据 memcpy 进去
        // 3. 创建 GPU 本地的 Device Local Buffer
        // 4. 通过单次 Command Buffer 执行 vkCmdCopyBuffer
    }

    VulkanVertexBuffer::~VulkanVertexBuffer() {
        // 未来逻辑：销毁 VkBuffer 并释放 VkDeviceMemory
    }

    // ==========================================
    // Vulkan 索引缓冲区实现 (Stub)
    // ==========================================
    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count) 
        : m_Count(count) 
    {
        AYAYA_CORE_WARN("VulkanIndexBuffer created (Stub): count {0}", count);
        // 未来逻辑同上，通过 Staging Buffer 拷贝数据到 GPU
    }

    VulkanIndexBuffer::~VulkanIndexBuffer() {
        // 未来逻辑：销毁 VkBuffer 并释放 VkDeviceMemory
    }

}