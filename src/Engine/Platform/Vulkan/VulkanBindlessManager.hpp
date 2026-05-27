#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Ayaya {

    class VulkanBindlessManager {
    public:
        void Init(VkDevice device, uint32_t capacity);
        void Shutdown(VkDevice device);

        uint32_t AllocateIndex();
        void FreeIndex(uint32_t index);

        void UpdateBinding(VkDevice device, uint32_t index, VkImageView view, VkSampler sampler);

        VkDescriptorSetLayout GetLayout() const { return m_Layout; }
        VkDescriptorSet GetSet() const { return m_Set; }

    private:
        uint32_t m_Capacity = 0;

        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;

        std::vector<uint32_t> m_FreeList;
        uint32_t m_NextIndex = 1;
    };

}
