#pragma once

#include "Renderer/Buffer.hpp"
#include <vulkan/vulkan.h>

namespace Ayaya {

    // ==========================================
    // Vulkan 顶点缓冲区
    // ==========================================
    class VulkanVertexBuffer : public VertexBuffer {
    public:
        VulkanVertexBuffer(float* vertices, uint32_t size);
        virtual ~VulkanVertexBuffer() override;

        // Vulkan 在录制 CommandBuffer 时才绑定，这里架空
        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual const BufferLayout& GetLayout() const override { return m_Layout; }
        virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

    private:
        BufferLayout m_Layout;
        
        // 占位：未来将由 VMA 或手写分配器填充
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    };

    // ==========================================
    // Vulkan 索引缓冲区
    // ==========================================
    class VulkanIndexBuffer : public IndexBuffer {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~VulkanIndexBuffer() override;

        // Vulkan 在录制 CommandBuffer 时才绑定，这里架空
        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual uint32_t GetCount() const override { return m_Count; }

    private:
        uint32_t m_Count = 0;

        // 占位：未来将由 VMA 或手写分配器填充
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_Memory = VK_NULL_HANDLE;
    };

}