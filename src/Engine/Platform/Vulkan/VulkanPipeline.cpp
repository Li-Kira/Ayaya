#include "ayapch.h"
#include "VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanPipeline::VulkanPipeline(const PipelineSpecification& spec)
        : m_Specification(spec) {
        
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
    
        auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(spec.Shader);
        AYAYA_CORE_ASSERT(vulkanShader, "Pipeline requires a valid VulkanShader!");

        // ==========================================
        // 1. 着色器阶段 (Shader Stages)
        // ==========================================
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vulkanShader->GetVertexShaderModule();
        vertShaderStageInfo.pName = "main"; // GLSL 的入口函数名

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = vulkanShader->GetFragmentShaderModule();
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        // ==========================================
        // 2. 动态状态 (Dynamic States)
        // Vulkan 默认连视口大小都不能改！我们必须在这里声明它们是“动态”的，
        // 这样在 CommandBuffer 里才能调用 vkCmdSetViewport。
        // ==========================================
        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // ==========================================
        // 3. 顶点输入 (Vertex Input)
        // 目前我们的全屏后处理是通过 gl_VertexIndex 自动生成的，不需要实际的 VertexBuffer，
        // 所以这里先留空。未来我们会根据 Mesh 的 BufferLayout 来填充这里！
        // ==========================================
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;

        // ==========================================
        // 4. 图元装配 (Input Assembly)
        // 翻译我们的 spec.Topology
        // ==========================================
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.primitiveRestartEnable = VK_FALSE;
        switch (spec.Topology) {
            case PrimitiveTopology::Triangles:     inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
            case PrimitiveTopology::Lines:         inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
            case PrimitiveTopology::TriangleStrip: inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
        }

        // ==========================================
        // 5. 视口与裁剪 (Viewport & Scissor)
        // 虽然设为了动态，但管线创建时仍需要提供这个结构体
        // ==========================================
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // ==========================================
        // 6. 光栅化 (Rasterizer)
        // 翻译 PolygonMode 和 CullMode
        // ==========================================
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.lineWidth = spec.LineWidth;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // OpenGL 标准是逆时针

        switch (spec.PolygonMode) {
            case PolygonMode::Fill:  rasterizer.polygonMode = VK_POLYGON_MODE_FILL; break;
            case PolygonMode::Line:  rasterizer.polygonMode = VK_POLYGON_MODE_LINE; break;
            case PolygonMode::Point: rasterizer.polygonMode = VK_POLYGON_MODE_POINT; break;
        }

        switch (spec.BackfaceCulling) {
            case CullMode::None:         rasterizer.cullMode = VK_CULL_MODE_NONE; break;
            case CullMode::Front:        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
            case CullMode::Back:         rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; break;
            case CullMode::FrontAndBack: rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK; break;
        }

        // ==========================================
        // 7. 多重采样 (Multisampling)
        // 我们目前的 FBO 都是 1 sample 的
        // ==========================================
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // ==========================================
        // 8. 深度与模板测试 (Depth & Stencil)
        // ==========================================
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = spec.DepthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = spec.DepthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;
        
        switch (spec.DepthOperator) {
            case DepthCompareOperator::Less:    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; break;
            case DepthCompareOperator::LEqual:  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
            case DepthCompareOperator::Equal:   depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
            case DepthCompareOperator::GEqual:  depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
            case DepthCompareOperator::Greater: depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER; break;
            case DepthCompareOperator::NotEqual:depthStencil.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL; break;
            case DepthCompareOperator::Always:  depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
            default: depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; break;
        }

        // ==========================================
        // 9. 颜色混合 (Color Blending)
        // ==========================================
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = spec.Blend ? VK_TRUE : VK_FALSE;

        if (spec.BlendMode == BlendMode::Alpha) {
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (spec.BlendMode == BlendMode::Additive) {
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // ==========================================
        // 10. 管线布局 (Pipeline Layout)
        // 这里需要配置 Push Constants 和 Descriptor Set Layouts (也就是 Binding 的槽位)
        // 【核心】：根据我们在 shader 写的 push_constant，推算出它占用的大小！
        // ==========================================
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // 我们是在 frag 里定义的
        pushConstantRange.offset = 0;
        // u_Exposure(4) + u_TexelSize(8) + u_ToneMappingType(4) + u_EnableBloom(4) + u_BloomIntensity(4) = 24 bytes
        pushConstantRange.size = 128; 

        // 临时构造一个极其简陋的 Descriptor Set Layout，用来绑定我们的三张贴图
        // (真实的引擎会通过 SPIRV-Cross 反射自动生成这些 Layout，但为了让你先看到画面，我们手写)
        std::vector<VkDescriptorSetLayoutBinding> bindings(3);
        for (uint32_t i = 0; i < 3; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings[i].pImmutableSamplers = nullptr;
        }
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings.data();
        
        // 注意：这里我们为了快速验证，把 Layout 存在了类的成员变量里（需要在头文件补充）
        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_DescriptorSetLayout);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to create Vulkan Pipeline Layout!");
        }

        // ==========================================
        // 11. 终于：烘焙图形管线！
        // ==========================================
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(spec.TargetFramebuffer);
        
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = vulkanFBO->GetVulkanRenderPass(); // 这是为什么 Pipeline 必须绑死在特定 FBO 上的原因！
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to create Vulkan Graphics Pipeline!");
        } else {
            AYAYA_CORE_INFO("Vulkan Graphics Pipeline baked successfully!");
        }

        // ==========================================
        // 【核心新增】：从池子中申请出一个集装箱 (Descriptor Set)
        // ==========================================
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = context->GetDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_DescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to allocate Descriptor Set!");
        } else {
            AYAYA_CORE_INFO("Vulkan Descriptor Set allocated successfully!");
        }
    }

    VulkanPipeline::~VulkanPipeline() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (context) {
            VkDevice device = context->GetDevice();
            if (m_Pipeline) vkDestroyPipeline(device, m_Pipeline, nullptr);
            if (m_PipelineLayout) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
            if (m_DescriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        }
    }

}