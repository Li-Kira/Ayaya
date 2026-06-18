#include "ayapch.h"
#include "VulkanBindlessManager.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    void VulkanBindlessManager::Init(VkDevice device, uint32_t capacity) {
        m_Capacity = capacity;

        VkDescriptorBindingFlags bindlessFlags =
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
        flagInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagInfo.bindingCount = 1;
        flagInfo.pBindingFlags = &bindlessFlags;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = m_Capacity;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &flagInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_Layout);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create bindless descriptor set layout!");

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = m_Capacity;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_Pool);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to create bindless descriptor pool!");

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_Layout;

        uint32_t variableCounts[] = { m_Capacity };
        VkDescriptorSetVariableDescriptorCountAllocateInfo varInfo{};
        varInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        varInfo.descriptorSetCount = 1;
        varInfo.pDescriptorCounts = variableCounts;
        allocInfo.pNext = &varInfo;

        result = vkAllocateDescriptorSets(device, &allocInfo, &m_Set);
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate bindless descriptor set!");

        AYAYA_CORE_INFO("VulkanBindlessManager initialized with capacity: {0}", m_Capacity);
    }

    void VulkanBindlessManager::Shutdown(VkDevice device) {
        if (m_Pool) vkDestroyDescriptorPool(device, m_Pool, nullptr);
        if (m_Layout) vkDestroyDescriptorSetLayout(device, m_Layout, nullptr);
        m_Pool = VK_NULL_HANDLE;
        m_Layout = VK_NULL_HANDLE;
        m_Set = VK_NULL_HANDLE;
        m_FreeList.clear();
        m_NextIndex = kFirstFreeIndex;
    }

    uint32_t VulkanBindlessManager::AllocateIndex() {
        // Reuse from free-list (entries are guaranteed >= kFirstFreeIndex)
        if (!m_FreeList.empty()) {
            uint32_t index = m_FreeList.back();
            m_FreeList.pop_back();
            return index;
        }
        if (m_NextIndex >= m_Capacity) {
            AYAYA_CORE_ERROR("VulkanBindlessManager: Out of texture slots!");
            return 0;
        }
        // Belt-and-suspenders: never return reserved indices 0-3
        uint32_t idx = m_NextIndex++;
        if (idx < kFirstFreeIndex) idx = kFirstFreeIndex;
        return idx;
    }

    void VulkanBindlessManager::FreeIndex(uint32_t index) {
        // Protect fixed default indices (0-3) from being recycled
        if (index >= kFirstFreeIndex && index < m_Capacity)
            m_FreeList.push_back(index);
    }

    void VulkanBindlessManager::UpdateBinding(VkDevice device, uint32_t index, VkImageView view, VkSampler sampler) {
        if (index == 0 || index >= m_Capacity) return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_Set;
        write.dstBinding = 0;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

}
