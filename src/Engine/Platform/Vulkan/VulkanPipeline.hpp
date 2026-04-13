#pragma once
#include "Renderer/Pipeline.hpp"
#include <vulkan/vulkan.h>
#include <vector>
#include <array>

namespace Ayaya {

    class VulkanPipeline : public Pipeline {
    public:
        VulkanPipeline(const PipelineSpecification& spec);
        virtual ~VulkanPipeline() override;

        virtual const PipelineSpecification& GetSpecification() const override { return m_Specification; }
        
        virtual void Bind() override {} // 真实绑定在 CommandBuffer 阶段完成

        VkPipeline GetVulkanPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetVulkanPipelineLayout() const { return m_PipelineLayout; }
        
        // 【核心修改】：支持获取特定 Set 的描述符集
        // set = 0: UBO 全局变量 (Camera, Light)
        // set = 1: Sampler 贴图绑定
        VkDescriptorSet GetVulkanDescriptorSet(uint32_t setIndex = 0) const { 
            return m_DescriptorSets[setIndex]; 
        }

        static void SetGlobalUniformBuffer(uint32_t binding, VkBuffer buffer, uint32_t size);

    private:
        static std::unordered_map<uint32_t, VkDescriptorBufferInfo> s_GlobalUBOs;
        
        PipelineSpecification m_Specification;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        
        // 存储 2 个 Set 的 Layout 和实例
        std::array<VkDescriptorSetLayout, 2> m_DescriptorSetLayouts = { VK_NULL_HANDLE, VK_NULL_HANDLE };
        std::array<VkDescriptorSet, 2> m_DescriptorSets = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    };

}