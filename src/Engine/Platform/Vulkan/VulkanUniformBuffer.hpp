#pragma once
#include "Renderer/UniformBuffer.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace Ayaya {

    class VulkanUniformBuffer : public UniformBuffer {
    public:
        // 这里的 size 是单个帧的 Buffer 大小
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~VulkanUniformBuffer() override;

        // 核心修改：SetData 现在会自动根据”当前帧索引”写入对应的内存块
        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

        // 将数据同步写入所有帧的缓冲区 (用于单次命令提交、不确定帧索引的场景，如 AssetPreviewer)
        void SetDataAllFrames(const void* data, uint32_t size, uint32_t offset = 0);

        // 供 Pipeline 绑定时获取正确的 Buffer 句柄
        VkBuffer GetBuffer(uint32_t frameIndex) const { return m_Buffers[frameIndex]; }

    private:
        uint32_t m_Binding = 0;
        uint32_t m_Size = 0;
        uint32_t m_FramesInFlight = 0;

        // 为每一帧分配独立的资源
        std::vector<VkBuffer> m_Buffers;
        std::vector<VmaAllocation> m_Allocations;
        std::vector<VmaAllocationInfo> m_AllocInfos;
    };
}