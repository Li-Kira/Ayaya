#include "ayapch.h"
#include "VulkanUniformBuffer.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanPipeline.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding)
        : m_Size(size), m_Binding(binding) {
        
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        // 【修改 1】：扩大为 3，覆盖三重缓冲！
        m_FramesInFlight = 3;
        
        m_Buffers.resize(m_FramesInFlight);
        m_Allocations.resize(m_FramesInFlight);
        m_AllocInfos.resize(m_FramesInFlight);

        for (uint32_t i = 0; i < m_FramesInFlight; i++) {
            VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.size = size;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            
            // ==========================================
            // 【核心修复】：强制要求内存具备“缓存一致性”！
            // 解决偶尔闪烁、数据未及时同步到显存的终极杀手锏。
            // ==========================================
            allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; 

            vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffers[i], &m_Allocations[i], &m_AllocInfos[i]);
            
            // 注册到管线...
            Ayaya::VulkanPipeline::SetGlobalUniformBuffer(binding, i, m_Buffers[i], size);
        }
    }

    VulkanUniformBuffer::~VulkanUniformBuffer() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!context) return;

        for (uint32_t i = 0; i < m_FramesInFlight; i++) {
            if (m_Buffers[i]) {
                vmaDestroyBuffer(context->GetAllocator(), m_Buffers[i], m_Allocations[i]);
            }
        }
    }

    void VulkanUniformBuffer::SetData(const void* data, uint32_t size, uint32_t offset) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        // 核心逻辑：获取当前正在录制的帧索引 (0 或 1)
        uint32_t frameIndex = context->GetCurrentFrameIndex() % m_FramesInFlight;
        // 只写到当前帧对应的内存地址，绝不干扰 GPU 正在读的另一帧！
        memcpy((uint8_t*)m_AllocInfos[frameIndex].pMappedData + offset, data, size);
    }
}