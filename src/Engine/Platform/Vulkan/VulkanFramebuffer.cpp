#include "ayapch.h"
#include "VulkanFramebuffer.hpp"
#include "VulkanContext.hpp"
#include "Core/Application.hpp"
#include <backends/imgui_impl_vulkan.h>

namespace Ayaya {

    // 辅助格式转换
    static VkFormat AyayaFormatToVulkan(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::RGBA8:       return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RGBA16F:     return VK_FORMAT_R16G16B16A16_SFLOAT;
            case FramebufferTextureFormat::RGBA32F:     return VK_FORMAT_R32G32B32A32_SFLOAT;
            case FramebufferTextureFormat::RED_INTEGER: return VK_FORMAT_R32_SINT;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {
        
        for (auto format : m_Specification.Attachments.Attachments) {
            if (format.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8)
                m_DepthAttachmentSpec = format;
            else
                m_ColorAttachmentSpecs.emplace_back(format);
        }

        Invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer() {
        Release();
    }

    void VulkanFramebuffer::Release() {
        if (!m_Framebuffer) return;

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        vkDeviceWaitIdle(device);

        vkDestroyFramebuffer(device, m_Framebuffer, nullptr);
        vkDestroyRenderPass(device, m_RenderPass, nullptr);
        vkDestroySampler(device, m_Sampler, nullptr);

        for (size_t i = 0; i < m_ColorImages.size(); i++) {
            vkDestroyImageView(device, m_ColorImageViews[i], nullptr);
            vkDestroyImage(device, m_ColorImages[i], nullptr);
            vkFreeMemory(device, m_ColorMemories[i], nullptr);
            
            // 释放 ImGui 描述符
            if (m_ImGuiDescriptorSets[i]) {
                ImGui_ImplVulkan_RemoveTexture(m_ImGuiDescriptorSets[i]);
            }
        }

        if (m_DepthImage) {
            vkDestroyImageView(device, m_DepthImageView, nullptr);
            vkDestroyImage(device, m_DepthImage, nullptr);
            vkFreeMemory(device, m_DepthMemory, nullptr);
        }

        m_ColorImages.clear();
        m_ColorMemories.clear();
        m_ColorImageViews.clear();
        m_ImGuiDescriptorSets.clear();
    }

    void VulkanFramebuffer::Invalidate() {
        if (m_Specification.Width == 0 || m_Specification.Height == 0) return;

        Release();

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        // 1. 创建 RenderPass
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorAttachmentRefs;
        VkAttachmentReference depthAttachmentRef = {};
        bool hasDepth = m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None;

        for (size_t i = 0; i < m_ColorAttachmentSpecs.size(); i++) {
            VkFormat format = AyayaFormatToVulkan(m_ColorAttachmentSpecs[i].TextureFormat);
            m_ColorAttachmentFormats.push_back(format);

            VkAttachmentDescription colorAttachment = {};
            colorAttachment.format = format;
            colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 暂不考虑 MSAA
            colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // 【魔法 1】：渲染结束后，自动将图像布局转换为“着色器只读”，方便 ImGui 读取！
            colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
            
            attachments.push_back(colorAttachment);

            VkAttachmentReference colorRef = {};
            colorRef.attachment = (uint32_t)i;
            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachmentRefs.push_back(colorRef);
        }

        if (hasDepth) {
            m_DepthFormat = context->FindDepthFormat();
            VkAttachmentDescription depthAttachment = {};
            depthAttachment.format = m_DepthFormat;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            attachments.push_back(depthAttachment);
            depthAttachmentRef.attachment = (uint32_t)m_ColorAttachmentSpecs.size();
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = (uint32_t)colorAttachmentRefs.size();
        subpass.pColorAttachments = colorAttachmentRefs.data();
        if (hasDepth) subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = (uint32_t)attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass);

        // 2. 创建图像 (Images) & 分配显存
        m_ColorImages.resize(m_ColorAttachmentSpecs.size());
        m_ColorMemories.resize(m_ColorAttachmentSpecs.size());
        m_ColorImageViews.resize(m_ColorAttachmentSpecs.size());
        m_ImGuiDescriptorSets.resize(m_ColorAttachmentSpecs.size());

        std::vector<VkImageView> fbImageViews;

        for (size_t i = 0; i < m_ColorAttachmentSpecs.size(); i++) {
            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = m_Specification.Width;
            imageInfo.extent.height = m_Specification.Height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = m_ColorAttachmentFormats[i];
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            // 【关键】：必须有 SAMPLED 标志，ImGui 才能拿它当贴图画！
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; 
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            vkCreateImage(device, &imageInfo, nullptr, &m_ColorImages[i]);

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, m_ColorImages[i], &memRequirements);

            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = context->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            vkAllocateMemory(device, &allocInfo, nullptr, &m_ColorMemories[i]);
            vkBindImageMemory(device, m_ColorImages[i], m_ColorMemories[i], 0);

            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_ColorImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_ColorAttachmentFormats[i];
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            vkCreateImageView(device, &viewInfo, nullptr, &m_ColorImageViews[i]);
            fbImageViews.push_back(m_ColorImageViews[i]);
        }

        if (hasDepth) {
            // 深度图的分配逻辑类似...
            VkImageCreateInfo imageInfo = {};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = m_Specification.Width;
            imageInfo.extent.height = m_Specification.Height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = m_DepthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            vkCreateImage(device, &imageInfo, nullptr, &m_DepthImage);

            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(device, m_DepthImage, &memRequirements);
            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = context->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &allocInfo, nullptr, &m_DepthMemory);
            vkBindImageMemory(device, m_DepthImage, m_DepthMemory, 0);

            VkImageViewCreateInfo viewInfo = {};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_DepthImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_DepthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &viewInfo, nullptr, &m_DepthImageView);
            fbImageViews.push_back(m_DepthImageView);
        }

        // 3. 创建 Framebuffer
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = (uint32_t)fbImageViews.size();
        framebufferInfo.pAttachments = fbImageViews.data();
        framebufferInfo.width = m_Specification.Width;
        framebufferInfo.height = m_Specification.Height;
        framebufferInfo.layers = 1;

        vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_Framebuffer);

        // ==========================================
        // 【魔法 2】：创建一个采样器，并将图片注册给 ImGui！
        // ==========================================
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler);

        for (size_t i = 0; i < m_ColorImageViews.size(); i++) {
            m_ImGuiDescriptorSets[i] = (VkDescriptorSet)ImGui_ImplVulkan_AddTexture(m_Sampler, m_ColorImageViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void VulkanFramebuffer::Bind() {
        // 在 Vulkan 中，Bind 通常由 CommandBuffer::BeginRenderPass 接管
        // 所以这里可以留空，后续我们在 Renderer 架构中重构它
    }

    void VulkanFramebuffer::Unbind() {
    }

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) return;
        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

}