#include "ayapch.h"
#include "VulkanFramebuffer.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <backends/imgui_impl_vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

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
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!context) return;
        VkDevice device = context->GetDevice();
        VmaAllocator allocator = context->GetAllocator();

        // ==========================================
        // 【核心救命补丁】：施展“时停魔法”！
        // 在销毁任何资源前，强制等待 GPU 把手头正在画的上一帧处理完。
        // 不加这句，绝对会导致 GPU 崩溃并引发死锁！
        // ==========================================
        vkDeviceWaitIdle(device);
        
        // 1. 清理 ImGui 注册的描述符
        for (auto descSet : m_ImGuiDescriptorSets) {
            ImGui_ImplVulkan_RemoveTexture(descSet);
        }
        m_ImGuiDescriptorSets.clear();

        // 2. 清理 Framebuffer & RenderPass
        if (m_Framebuffer) vkDestroyFramebuffer(device, m_Framebuffer, nullptr);
        if (m_RenderPass) vkDestroyRenderPass(device, m_RenderPass, nullptr);
        if (m_ColorSampler) vkDestroySampler(device, m_ColorSampler, nullptr);

        // 3. 清理 Color Attachments (使用 VMA 销毁 Image 和 Memory)
        for (size_t i = 0; i < m_ColorImageViews.size(); i++) {
            vkDestroyImageView(device, m_ColorImageViews[i], nullptr);
            // VMA 会自动帮我们把 VkImage 和底层的显存块一起释放！
            vmaDestroyImage(allocator, m_ColorImages[i], (VmaAllocation)m_ColorMemories[i]); 
        }
        m_ColorImageViews.clear();
        m_ColorImages.clear();
        m_ColorMemories.clear();

        m_Framebuffer = VK_NULL_HANDLE;
        m_RenderPass = VK_NULL_HANDLE;
        m_ColorSampler = VK_NULL_HANDLE;
    }

    void VulkanFramebuffer::Invalidate() {
        if (m_Specification.Width == 0 || m_Specification.Height == 0) return;

        Release();

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        VmaAllocator allocator = context->GetAllocator();

        AYAYA_CORE_INFO("VulkanFramebuffer: Creating FBO {0}x{1}", m_Specification.Width, m_Specification.Height);

        // ==========================================
        // 1. 创建极其简化的 RenderPass (仅测试用)
        // ==========================================
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM; // 标准 RGBA
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // 【关键】：画完之后，这张图必须变成 SHADER_READ_ONLY_OPTIMAL 布局，ImGui 才能读取它！
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass);

        // ==========================================
        // 2. 利用 VMA 申请离线画布 (VkImage)
        // ==========================================
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_Specification.Width;
        imageInfo.extent.height = m_Specification.Height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // 允许作为颜色附件被渲染，也允许被着色器采样 (交给 ImGui)
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; // VMA 新特性：自动优选显存

        VkImage colorImage;
        VmaAllocation colorAllocation;
        vmaCreateImage(allocator, &imageInfo, &allocInfo, &colorImage, &colorAllocation, nullptr);
        
        m_ColorImages.push_back(colorImage);
        m_ColorMemories.push_back((VkDeviceMemory)colorAllocation);

        // ==========================================
        // 3. 创建 ImageView 和 Sampler
        // ==========================================
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView colorImageView;
        vkCreateImageView(device, &viewInfo, nullptr, &colorImageView);
        m_ColorImageViews.push_back(colorImageView);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(device, &samplerInfo, nullptr, &m_ColorSampler);

        // ==========================================
        // 4. 创建 Framebuffer
        // ==========================================
        VkFramebufferCreateInfo fboInfo{};
        fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fboInfo.renderPass = m_RenderPass;
        fboInfo.attachmentCount = 1;
        fboInfo.pAttachments = &colorImageView;
        fboInfo.width = m_Specification.Width;
        fboInfo.height = m_Specification.Height;
        fboInfo.layers = 1;
        vkCreateFramebuffer(device, &fboInfo, nullptr, &m_Framebuffer);

        // ==========================================
        // 5. 【核弹桥梁】：向 ImGui 注册并获取 DescriptorSet！
        // ==========================================
        VkDescriptorSet imguiSet = ImGui_ImplVulkan_AddTexture(
            m_ColorSampler, 
            colorImageView, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        m_ImGuiDescriptorSets.push_back(imguiSet);
    }

    void VulkanFramebuffer::Bind() {
        // Vulkan 中，绑定 FBO 是在 vkCmdBeginRenderPass 中指定的
        // 这是一种“记录时”状态，因此这里的全局 Bind 被架空。
    }

    void VulkanFramebuffer::Unbind() {
        // 同上，被架空。
    }

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || (m_Specification.Width == width && m_Specification.Height == height))
            return;

        m_Specification.Width = width;
        m_Specification.Height = height;
        
        Invalidate(); // 重新走一遍创建流程
    }

    void* VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const {
        AYAYA_CORE_ASSERT(index < m_ColorAttachmentSpecs.size(), "Index out of bounds!");
        
        // ==========================================
        // 【核心安全返回】：
        // 在真实的 VkDescriptorSet 还没分配之前，我们安全地返回 nullptr！
        // 你在 EditorLayer.cpp 刚写的 if(textureID) 会完美拦截它，并显示等待文字。
        // ==========================================
        if (index < m_ImGuiDescriptorSets.size()) {
            return (void*)m_ImGuiDescriptorSets[index];
        }
        return nullptr;
    }

    void* VulkanFramebuffer::GetDepthAttachmentRendererID() const {
        return (void*)m_DepthImageView; // 暂时直接返回 ImageView 句柄
    }

    void* VulkanFramebuffer::GetRendererID() const {
        return (void*)m_Framebuffer; // 暂时直接返回 FBO 句柄
    }

}