#pragma once
#include "Renderer/Pipeline.hpp"
#include <vulkan/vulkan.h>

namespace Ayaya {

    class VulkanPipeline : public Pipeline {
    public:
        VulkanPipeline(const PipelineSpecification& spec);
        virtual ~VulkanPipeline() override;

        virtual const PipelineSpecification& GetSpecification() const override { return m_Specification; }
        
        // 这里的 Bind 会被架空，真实的绑定在 CommandBuffer 里做 (vkCmdBindPipeline)
        virtual void Bind() override {}

        // 供底层获取真实句柄
        VkPipeline GetVulkanPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetVulkanPipelineLayout() const { return m_PipelineLayout; }
        // 【新增】：暴露出属于这个管线的集装箱
        VkDescriptorSet GetVulkanDescriptorSet() const { return m_DescriptorSet; }

    private:
        PipelineSpecification m_Specification;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        
        // 【新增】：用于管理着色器中绑定的贴图槽位布局
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        // 【新增】：具体的集装箱实例
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    };

}