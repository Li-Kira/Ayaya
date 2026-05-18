#pragma once
#include "Renderer/Pipeline.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <unordered_map>

namespace Ayaya {

    class VulkanPipeline : public Pipeline {
    public:
        VulkanPipeline(const PipelineSpecification& spec);
        virtual ~VulkanPipeline() override;

        virtual const PipelineSpecification& GetSpecification() const override { return m_Specification; }
        virtual PipelineSpecification& GetSpecification() override { return m_Specification; }
        virtual void Bind() override {} 

        VkPipeline GetVulkanPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetVulkanPipelineLayout() const { return m_PipelineLayout; }
        
        // ==========================================
        // 【核心修改】：现在需要传入 frameIndex 来获取对应帧的相机描述符集 (Set 0)
        // ==========================================
        VkDescriptorSet GetVulkanDescriptorSet(uint32_t setIndex, uint32_t frameIndex = 0) const {
            if (setIndex == 0) {
                // 增加安全取模，防止 frameIndex 超过 m_GlobalDescriptorSets 的大小
                size_t size = m_GlobalDescriptorSets.size();
                return size > 0 ? m_GlobalDescriptorSets[frameIndex % size] : VK_NULL_HANDLE;
            }
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetLayout GetDescriptorSetLayout(uint32_t setIndex) const {
            return setIndex < m_DescriptorSetLayouts.size() ? m_DescriptorSetLayouts[setIndex] : VK_NULL_HANDLE;
        }

        // 注册 UBO，需指定是哪一帧的 Buffer
        static void SetGlobalUniformBuffer(uint32_t binding, uint32_t frameIndex, VkBuffer buffer, uint32_t size);

        // ==========================================
        // 环形缓冲，每次索要都给一个全新的描述符集！(Set 1)
        // ==========================================
        VkDescriptorSet GetNextTextureDescriptorSet() {
            AYAYA_CORE_ASSERT(!m_TextureDescriptorSets.empty(), "Texture Descriptor Sets array is empty!");
            uint32_t index = m_CurrentTextureSetIndex;
            m_CurrentTextureSetIndex = (m_CurrentTextureSetIndex + 1) % m_TextureDescriptorSets.size();
            AYAYA_CORE_ASSERT(m_TextureDescriptorSets[index] != VK_NULL_HANDLE, "Descriptor Set is NULL!");
            return m_TextureDescriptorSets[index];
        }

    private:
        // 记录每一帧对应的 UBO 缓冲区信息 [Binding] -> [Frame0_Info, Frame1_Info]
        static std::unordered_map<uint32_t, std::array<VkDescriptorBufferInfo, 3>> s_GlobalUBOs;
        
        PipelineSpecification m_Specification;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        
        std::array<VkDescriptorSetLayout, 2> m_DescriptorSetLayouts = { VK_NULL_HANDLE, VK_NULL_HANDLE };

        // 【核心修改】：Set 0 变为数组，为每一帧保存一份专属的描述符
        std::vector<VkDescriptorSet> m_GlobalDescriptorSets; 

        // 专属池与环形缓冲 (Set 1)
        VkDescriptorPool m_PipelineDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_TextureDescriptorSets;
        uint32_t m_CurrentTextureSetIndex = 0;
    };

}