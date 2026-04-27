#include "ayapch.h"
#include "VulkanPipeline.hpp"
#include "Platform/Vulkan/VulkanShader.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanFramebuffer.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

namespace Ayaya {
    std::unordered_map<uint32_t, std::array<VkDescriptorBufferInfo, 3>> VulkanPipeline::s_GlobalUBOs;

    // 记录 UBO 缓冲区信息 (增加 frameIndex 参数)
    void VulkanPipeline::SetGlobalUniformBuffer(uint32_t binding, uint32_t frameIndex, VkBuffer buffer, uint32_t size) {
        s_GlobalUBOs[binding][frameIndex] = { buffer, 0, size };
    }

    VulkanPipeline::VulkanPipeline(const PipelineSpecification& spec)
        : m_Specification(spec) {
        
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        uint32_t framesInFlight = 3; // 与引擎 Double Buffering 保持一致
    
        auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(spec.Shader);
        AYAYA_CORE_ASSERT(vulkanShader, "Pipeline requires a valid VulkanShader!");

        // ==========================================
        // 1. 着色器阶段 (Shader Stages)
        // ==========================================
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        
        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vulkanShader->GetVertexShaderModule();
        vertShaderStageInfo.pName = "main";
        if (vertShaderStageInfo.module != VK_NULL_HANDLE) shaderStages.push_back(vertShaderStageInfo);

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = vulkanShader->GetFragmentShaderModule();
        fragShaderStageInfo.pName = "main";
        if (fragShaderStageInfo.module != VK_NULL_HANDLE) shaderStages.push_back(fragShaderStageInfo);

        // ==========================================
        // 2. 顶点输入 (Vertex Input) - 动态解析 BufferLayout
        // ==========================================
        VkVertexInputBindingDescription bindingDescription{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

        if (spec.Layout.GetElements().size() > 0) {
            bindingDescription.binding = 0;
            bindingDescription.stride = spec.Layout.GetStride();
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            uint32_t location = 0;
            for (const auto& element : spec.Layout) {
                VkVertexInputAttributeDescription attribute{};
                attribute.binding = 0;
                attribute.location = location;
                
                switch (element.Type) {
                    case ShaderDataType::Float:  attribute.format = VK_FORMAT_R32_SFLOAT; break;
                    case ShaderDataType::Float2: attribute.format = VK_FORMAT_R32G32_SFLOAT; break;
                    case ShaderDataType::Float3: attribute.format = VK_FORMAT_R32G32B32_SFLOAT; break;
                    case ShaderDataType::Float4: attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
                    case ShaderDataType::Int:    attribute.format = VK_FORMAT_R32_SINT; break;
                    case ShaderDataType::Int2:   attribute.format = VK_FORMAT_R32G32_SINT; break;
                    case ShaderDataType::Int3:   attribute.format = VK_FORMAT_R32G32B32_SINT; break;
                    case ShaderDataType::Int4:   attribute.format = VK_FORMAT_R32G32B32A32_SINT; break;
                    default: AYAYA_CORE_ASSERT(false, "Unknown ShaderDataType!"); break;
                }
                
                attribute.offset = element.Offset;
                attributeDescriptions.push_back(attribute);
                location++;
            }
        }

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (!attributeDescriptions.empty()) {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        }

        // ==========================================
        // 3. 图元装配 & 动态视口
        // ==========================================
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = attributeDescriptions.empty() ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        if (spec.PolygonModeLine) {
            dynamicStates.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
        }

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // ==========================================
        // 4. 光栅化 (Rasterizer) 
        // ==========================================
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = spec.LineWidth; 
        
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        if (spec.BackfaceCulling == CullMode::Back) rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        else if (spec.BackfaceCulling == CullMode::Front) rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
        
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        // ==========================================
        // 5. 深度与模板测试 (Depth & Stencil)
        // ==========================================
        auto AyayaToVulkanDepthCompare = [](DepthCompareOperator op) -> VkCompareOp {
            switch(op) {
               case DepthCompareOperator::Less: return VK_COMPARE_OP_LESS;
                case DepthCompareOperator::LEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
                case DepthCompareOperator::Equal: return VK_COMPARE_OP_EQUAL;
                case DepthCompareOperator::GEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case DepthCompareOperator::Greater: return VK_COMPARE_OP_GREATER;
                case DepthCompareOperator::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
                case DepthCompareOperator::Always: return VK_COMPARE_OP_ALWAYS;
                default: return VK_COMPARE_OP_LESS; // 加个 fallback
            }
            return VK_COMPARE_OP_LESS;
        };

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = spec.DepthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = spec.DepthWrite ? VK_TRUE : VK_FALSE;
        
        // ==========================================
        // 【核心修复】：不要再写死 VK_COMPARE_OP_LESS，使用传入的规范！
        // ==========================================
        depthStencil.depthCompareOp = AyayaToVulkanDepthCompare(spec.DepthOperator);
        
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // ==========================================
        // 6. 颜色混合 (Color Blend) 动态匹配 FBO 附件
        // ==========================================
        uint32_t colorAttachmentCount = 0;
        if (spec.TargetFramebuffer) {
            for (auto format : spec.TargetFramebuffer->GetSpecification().Attachments.Attachments) {
                if (format.TextureFormat != FramebufferTextureFormat::DEPTH24STENCIL8 && 
                    format.TextureFormat != FramebufferTextureFormat::Depth) {
                    colorAttachmentCount++;
                }
            }
        }

        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorAttachmentCount);
        for (uint32_t i = 0; i < colorAttachmentCount; i++) {
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (spec.Blend) {
                blendAttachments[i].blendEnable = VK_TRUE;
                blendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                blendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                blendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD;
                blendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                blendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD;
            } else {
                blendAttachments[i].blendEnable = VK_FALSE;
            }
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = (uint32_t)blendAttachments.size();
        colorBlending.pAttachments = blendAttachments.data();

        // ==========================================
        // 7. Descriptor Set Layouts (描述符集图纸)
        // ==========================================
        std::vector<VkDescriptorSetLayoutBinding> set0Bindings;
        for (uint32_t i = 0; i < 2; i++) {
            VkDescriptorSetLayoutBinding uboBind{};
            uboBind.binding = i;
            uboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboBind.descriptorCount = 1;
            uboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            set0Bindings.push_back(uboBind);
        }
        VkDescriptorSetLayoutCreateInfo set0Info{};
        set0Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set0Info.bindingCount = (uint32_t)set0Bindings.size();
        set0Info.pBindings = set0Bindings.data();
        vkCreateDescriptorSetLayout(device, &set0Info, nullptr, &m_DescriptorSetLayouts[0]);

        std::vector<VkDescriptorSetLayoutBinding> set1Bindings;
        for (uint32_t i = 0; i < 12; i++) {
            VkDescriptorSetLayoutBinding samplerBind{};
            samplerBind.binding = i;
            samplerBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplerBind.descriptorCount = 1;
            samplerBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            set1Bindings.push_back(samplerBind);
        }
        VkDescriptorSetLayoutCreateInfo set1Info{};
        set1Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set1Info.bindingCount = (uint32_t)set1Bindings.size();
        set1Info.pBindings = set1Bindings.data();
        vkCreateDescriptorSetLayout(device, &set1Info, nullptr, &m_DescriptorSetLayouts[1]);

        // ==========================================
        // 8. 管线布局 (Push Constants & Descriptor Sets)
        // ==========================================
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = 256; 

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 2; 
        pipelineLayoutInfo.pSetLayouts = m_DescriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout);

        // ==========================================
        // 9. 正式创建 Pipeline
        // ==========================================
        auto vulkanFBO = std::dynamic_pointer_cast<VulkanFramebuffer>(spec.TargetFramebuffer);
        
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = (uint32_t)shaderStages.size();
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipelineInfo.pMultisampleState = &multisampling; 
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = vulkanFBO ? vulkanFBO->GetVulkanRenderPass() : VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to create Vulkan Graphics Pipeline!");
        }

        // ==========================================
        // 10. 创建管线专属 Descriptor Pool
        // ==========================================
        VkDescriptorPoolSize poolSizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 20 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 40000 }
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = 3010; 

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_PipelineDescriptorPool) != VK_SUCCESS) {
            AYAYA_CORE_ERROR("Failed to create custom Descriptor Pool for Pipeline!");
        }

        // ==========================================
        // 11. 【核心升级】：为每一帧独立分配与写入 Set 0
        // ==========================================
        m_GlobalDescriptorSets.resize(framesInFlight);
        std::vector<VkDescriptorSetLayout> layouts0(framesInFlight, m_DescriptorSetLayouts[0]);
        
        VkDescriptorSetAllocateInfo allocInfo0{};
        allocInfo0.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo0.descriptorPool = m_PipelineDescriptorPool;
        allocInfo0.descriptorSetCount = framesInFlight;
        allocInfo0.pSetLayouts = layouts0.data();
        vkAllocateDescriptorSets(device, &allocInfo0, m_GlobalDescriptorSets.data());

        // 将每一帧对应的 UBO 缓冲区写入各自的描述符集中
        for (uint32_t i = 0; i < framesInFlight; i++) {
            std::vector<VkWriteDescriptorSet> descriptorWrites;
            for (auto& pair : s_GlobalUBOs) {
                uint32_t binding = pair.first;
                VkDescriptorBufferInfo& bufferInfo = pair.second[i]; // 取第 i 帧的显存信息

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_GlobalDescriptorSets[i];
                write.dstBinding = binding;
                write.dstArrayElement = 0;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = &bufferInfo;
                descriptorWrites.push_back(write);
            }
            if (!descriptorWrites.empty()) {
                vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
            }
        }

        // ==========================================
        // 12. 分配 100 个 Set 1 (纹理) 作为环形缓冲
        // ==========================================
        std::vector<VkDescriptorSetLayout> layouts3000(3000, m_DescriptorSetLayouts[1]); // 修改这里
        m_TextureDescriptorSets.resize(3000); // 修改这里
        
        VkDescriptorSetAllocateInfo allocInfo1{};
        allocInfo1.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo1.descriptorPool = m_PipelineDescriptorPool;
        allocInfo1.descriptorSetCount = 3000;
        allocInfo1.pSetLayouts = layouts3000.data();
        // 【核心修改】：加上结果检查！
        VkResult result = vkAllocateDescriptorSets(device, &allocInfo1, m_TextureDescriptorSets.data());
        AYAYA_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate 3000 texture descriptor sets! Descriptor Pool exhausted.");
    }

    VulkanPipeline::~VulkanPipeline() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (context) {
            VkDevice device = context->GetDevice();
            if (m_Pipeline) vkDestroyPipeline(device, m_Pipeline, nullptr);
            if (m_PipelineLayout) vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
            if (m_DescriptorSetLayouts[0]) vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayouts[0], nullptr);
            if (m_DescriptorSetLayouts[1]) vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayouts[1], nullptr);
            if (m_PipelineDescriptorPool) vkDestroyDescriptorPool(device, m_PipelineDescriptorPool, nullptr);
        }
    }

}