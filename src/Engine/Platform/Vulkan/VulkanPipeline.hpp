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
        virtual void Bind() override {} 

        VkPipeline GetVulkanPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetVulkanPipelineLayout() const { return m_PipelineLayout; }
        
        VkDescriptorSet GetVulkanDescriptorSet(uint32_t setIndex = 0) const { 
            return m_DescriptorSets[setIndex]; 
        }

        static void SetGlobalUniformBuffer(uint32_t binding, VkBuffer buffer, uint32_t size);

        // ==========================================
        // 【核心架构】：环形缓冲，每次索要都给一个全新的描述符集！
        // ==========================================
        VkDescriptorSet GetNextTextureDescriptorSet() {
            uint32_t index = m_CurrentTextureSetIndex;
            m_CurrentTextureSetIndex = (m_CurrentTextureSetIndex + 1) % m_TextureDescriptorSets.size();
            return m_TextureDescriptorSets[index];
        }

    private:
        static std::unordered_map<uint32_t, VkDescriptorBufferInfo> s_GlobalUBOs;
        PipelineSpecification m_Specification;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        
        std::array<VkDescriptorSetLayout, 2> m_DescriptorSetLayouts = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        std::array<VkDescriptorSet, 2> m_DescriptorSets = { VK_NULL_HANDLE, VK_NULL_HANDLE };

        // 专属池与环形缓冲
        VkDescriptorPool m_PipelineDescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_TextureDescriptorSets;
        uint32_t m_CurrentTextureSetIndex = 0;
    };

}