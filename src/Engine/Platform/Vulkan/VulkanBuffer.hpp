#pragma once
#include "Renderer/Buffer.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    class VulkanVertexBuffer : public VertexBuffer {
    public:
        VulkanVertexBuffer(float* vertices, uint32_t size);
        virtual ~VulkanVertexBuffer() override;

        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual const BufferLayout& GetLayout() const override { return m_Layout; }
        virtual void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }

        // 【新增】：暴露底层句柄
        VkBuffer GetVulkanBuffer() const { return m_Buffer; }

    private:
        BufferLayout m_Layout;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VmaAllocationInfo m_AllocInfo{};
    };

    class VulkanIndexBuffer : public IndexBuffer {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~VulkanIndexBuffer() override;

        virtual void Bind() const override {}
        virtual void Unbind() const override {}

        virtual uint32_t GetCount() const override { return m_Count; }

        // 【新增】：暴露底层句柄
        VkBuffer GetVulkanBuffer() const { return m_Buffer; }

    private:
        uint32_t m_Count;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VmaAllocationInfo m_AllocInfo{};
    };
}