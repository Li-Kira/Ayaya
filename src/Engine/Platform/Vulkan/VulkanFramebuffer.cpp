#include "ayapch.h"
#include "VulkanFramebuffer.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <backends/imgui_impl_vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    // 辅助转换函数
    static VkFormat AyayaFormatToVulkanFormat(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::RGBA8:       return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RG16F:       return VK_FORMAT_R16G16_SFLOAT; 
            case FramebufferTextureFormat::RGBA16F:     return VK_FORMAT_R16G16B16A16_SFLOAT;
            case FramebufferTextureFormat::RGBA32F:     return VK_FORMAT_R32G32B32A32_SFLOAT;
            case FramebufferTextureFormat::RED_INTEGER: return VK_FORMAT_R32_SINT;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {

        for (auto format : m_Specification.Attachments.Attachments) {
            if (format.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8 || format.TextureFormat == FramebufferTextureFormat::Depth)
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

        vkDeviceWaitIdle(device);

        for (auto desc : m_ImGuiDescriptorSets) {
            if (desc) ImGui_ImplVulkan_RemoveTexture(desc);
        }
        m_ImGuiDescriptorSets.clear();

        for (size_t i = 0; i < m_ColorImages.size(); i++) {
            vkDestroyImageView(device, m_ColorImageViews[i], nullptr);
            vmaDestroyImage(allocator, m_ColorImages[i], m_ColorAllocations[i]);
        }
        m_ColorImages.clear();
        m_ColorImageViews.clear();
        m_ColorAllocations.clear();

        if (m_DepthImage) {
            vkDestroyImageView(device, m_DepthImageView, nullptr);
            vmaDestroyImage(allocator, m_DepthImage, m_DepthAllocation);
            m_DepthImage = VK_NULL_HANDLE;
        }

        if (m_Framebuffer) { vkDestroyFramebuffer(device, m_Framebuffer, nullptr); m_Framebuffer = VK_NULL_HANDLE; }
        if (m_RenderPass) { vkDestroyRenderPass(device, m_RenderPass, nullptr); m_RenderPass = VK_NULL_HANDLE; }
        if (m_Sampler) { vkDestroySampler(device, m_Sampler, nullptr); m_Sampler = VK_NULL_HANDLE; }
    }

    void VulkanFramebuffer::Invalidate() {
        if (m_Framebuffer) Release();

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        VmaAllocator allocator = context->GetAllocator();

        std::vector<VkAttachmentDescription> attachmentDescs;
        std::vector<VkAttachmentReference> colorRefs;
        VkAttachmentReference depthRef{};
        bool hasDepth = false;

        // ==========================================
        // 1. 创建颜色附件 (Color Attachments)
        // ==========================================
        for (uint32_t i = 0; i < m_ColorAttachmentSpecs.size(); i++) {
            VkFormat vkFormat = AyayaFormatToVulkanFormat(m_ColorAttachmentSpecs[i].TextureFormat);

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = vkFormat;
            imageInfo.extent = { m_Specification.Width, m_Specification.Height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            // 该图像可作为颜色附件、被纹理采样、作为传输源/目标 (用于 MSAA resolve)
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            VkImage image;
            VmaAllocation allocation;
            vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
            
            m_ColorImages.push_back(image);
            m_ColorAllocations.push_back(allocation);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = vkFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            VkImageView imageView;
            vkCreateImageView(device, &viewInfo, nullptr, &imageView);
            m_ColorImageViews.push_back(imageView);

            // initialLayout must match the previous frame's finalLayout (SHADER_READ_ONLY_OPTIMAL)
            // so MoltenVK on Apple Silicon can properly flush TBDR tile caches before clearing.
            // Using UNDEFINED here skips the cache flush and causes magenta flickering on M1/M2.
            VkAttachmentDescription desc{};
            desc.format = vkFormat;
            desc.samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
            desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            desc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            attachmentDescs.push_back(desc);

            VkAttachmentReference ref{};
            ref.attachment = i;
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs.push_back(ref);
        }

        // ==========================================
        // 2. 创建深度附件 (Depth Attachment)
        // ==========================================
        if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None) {
            VkFormat depthFormat = context->FindDepthFormat();

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = depthFormat;
            imageInfo.extent = { m_Specification.Width, m_Specification.Height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_DepthImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            vkCreateImageView(device, &viewInfo, nullptr, &m_DepthImageView);

            VkAttachmentDescription depthDesc{};
            depthDesc.format = depthFormat;
            depthDesc.samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
            depthDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // <--- 支持将深度图传给下一个管线做 Shadow/SSAO
            attachmentDescs.push_back(depthDesc);

            depthRef.attachment = (uint32_t)colorRefs.size();
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            hasDepth = true;
        }

        // ==========================================
        // 3. 构建 RenderPass 与 Subpass 依赖
        // ==========================================
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = (uint32_t)colorRefs.size();
        subpass.pColorAttachments = colorRefs.data();
        if (hasDepth) subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> dependencies;

        // 依赖 1：进入渲染前，等待外部 (ImGui) 彻底读完，再开始写
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        // 【核心修复】：绝对不能用 BY_REGION_BIT，强制全局内存屏障！
        dependencies[0].dependencyFlags = 0; 

        // 依赖 2：渲染结束后，必须把颜色写入彻底完成并刷新到显存，才能让外部 (ImGui) 采样
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; 
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // 【核心修复】：绝对不能用 BY_REGION_BIT，强制全局内存屏障！
        dependencies[1].dependencyFlags = 0;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = (uint32_t)attachmentDescs.size();
        renderPassInfo.pAttachments = attachmentDescs.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_RenderPass);

        // ==========================================
        // 4. 组装 Framebuffer 对象
        // ==========================================
        std::vector<VkImageView> attachments = m_ColorImageViews;
        if (hasDepth) attachments.push_back(m_DepthImageView);

        VkFramebufferCreateInfo fboInfo{};
        fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fboInfo.renderPass = m_RenderPass;
        fboInfo.attachmentCount = (uint32_t)attachments.size();
        fboInfo.pAttachments = attachments.data();
        fboInfo.width = m_Specification.Width;
        fboInfo.height = m_Specification.Height;
        fboInfo.layers = 1;
        vkCreateFramebuffer(device, &fboInfo, nullptr, &m_Framebuffer);

        // ==========================================
        // 4.5 将新创建的颜色图像从 UNDEFINED 过渡到 SHADER_READ_ONLY_OPTIMAL
        // 否则 RenderPass 以 SHADER_READ_ONLY_OPTIMAL 作为 initialLayout 时会报 VUID 09600
        // ==========================================
        {
            VkCommandBuffer cmd = context->BeginSingleTimeCommands();
            for (auto& image : m_ColorImages) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = image;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            context->EndSingleTimeCommands(cmd);
        }

        // ==========================================
        // 5. 创建全局读取采样器与 ImGui 注册
        // ==========================================
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler);

        for (auto& view : m_ColorImageViews) {
            // 向 ImGui 注册 DescriptorSet，这样编辑器 UI 就能直接显示渲染画面了！
            m_ImGuiDescriptorSets.push_back(ImGui_ImplVulkan_AddTexture(m_Sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }
    }

    void VulkanFramebuffer::Bind() {}
    void VulkanFramebuffer::Unbind() {}

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || (m_Specification.Width == width && m_Specification.Height == height)) return;
        
        m_Specification.Width = width;
        m_Specification.Height = height;
        
        // ==========================================
        // 【核心防御】：等待 GPU 彻底停歇！
        // 绝对不能在 GPU 还在画上一帧的时候把 FBO 显存炸掉！
        // ==========================================
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkDeviceWaitIdle(context->GetDevice());
        
        Invalidate(); 
    }

    void* VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const {
        if (index < m_ImGuiDescriptorSets.size()) return (void*)m_ImGuiDescriptorSets[index];
        return nullptr;
    }

    void* VulkanFramebuffer::GetDepthAttachmentRendererID() const {
        return nullptr; // 如果你想让深度图也能在 UI 面板显示，这里同样需要为其注册 ImGui_ImplVulkan_AddTexture
    }

}