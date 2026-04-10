#include "ayapch.h"
#include "VulkanPipeline.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    VulkanPipeline::VulkanPipeline(const PipelineSpecification& spec)
        : m_Specification(spec) {
        
        AYAYA_CORE_WARN("VulkanPipeline created (Stub)!");
        
        // ==========================================
        // Vulkan 管线烘焙蓝图 (待实现)：
        // ==========================================
        // 1. 读取 spec.Shader 的 SPIR-V 模块，配置 VkPipelineShaderStageCreateInfo
        // 2. 配置顶点输入 VkPipelineVertexInputStateCreateInfo
        // 3. 配置图元装配 VkPipelineInputAssemblyStateCreateInfo (使用 spec.Topology)
        // 4. 配置光栅化 VkPipelineRasterizationStateCreateInfo (使用 spec.PolygonMode 和 BackfaceCulling)
        // 5. 配置深度模板 VkPipelineDepthStencilStateCreateInfo (使用 spec.DepthTest 等)
        // 6. 配置颜色混合 VkPipelineColorBlendStateCreateInfo (使用 spec.BlendMode)
        // 7. 调用 vkCreatePipelineLayout 和 vkCreateGraphicsPipelines 烤制出最终的 m_Pipeline！
    }

    VulkanPipeline::~VulkanPipeline() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (context) {
            VkDevice device = context->GetDevice();
            if (m_Pipeline) vkDestroyPipeline(device, m_Pipeline, nullptr);
            if (m_PipelineLayout) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        }
    }

}