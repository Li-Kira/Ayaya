#include "ayapch.h"
#include "VulkanFramebuffer.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <backends/imgui_impl_vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    static VkFormat AyayaFormatToVulkanFormat(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::R8:          return VK_FORMAT_R8_UNORM;
        case FramebufferTextureFormat::R32F:        return VK_FORMAT_R32_SFLOAT;
            case FramebufferTextureFormat::RGBA8:       return VK_FORMAT_R8G8B8A8_UNORM;
            case FramebufferTextureFormat::RG16F:       return VK_FORMAT_R16G16_SFLOAT;
            case FramebufferTextureFormat::RGBA16F:     return VK_FORMAT_R16G16B16A16_SFLOAT;
            case FramebufferTextureFormat::RGBA32F:     return VK_FORMAT_R32G32B32A32_SFLOAT;
            case FramebufferTextureFormat::RED_INTEGER: return VK_FORMAT_R32_SINT;
            default: return VK_FORMAT_UNDEFINED;
        }
    }

    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec, bool asyncInit)
        : m_Specification(spec) {
        for (auto format : m_Specification.Attachments.Attachments) {
            if (format.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8 || format.TextureFormat == FramebufferTextureFormat::Depth)
                m_DepthAttachmentSpec = format;
            else
                m_ColorAttachmentSpecs.emplace_back(format);
        }
        // asyncInit: caller will call Invalidate(cmd) to record barriers into main CB.
        // Otherwise, use synchronous one-time command path (Init-time, no active CB).
        if (!asyncInit)
            Invalidate();
    }

    VulkanFramebuffer::~VulkanFramebuffer() { Release(); }

    VkFormat VulkanFramebuffer::GetColorAttachmentFormat(uint32_t index) const {
        if (index >= m_ColorAttachmentSpecs.size()) return VK_FORMAT_UNDEFINED;
        return AyayaFormatToVulkanFormat(m_ColorAttachmentSpecs[index].TextureFormat);
    }

    VkFormat VulkanFramebuffer::GetDepthAttachmentFormat() const {
        if (m_DepthAttachmentSpec.TextureFormat == FramebufferTextureFormat::None) return VK_FORMAT_UNDEFINED;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        return context ? context->FindDepthFormat() : VK_FORMAT_UNDEFINED;
    }

    void VulkanFramebuffer::Release() {
        auto vkCtx = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        if (!vkCtx) return;

        // Vulkan + ImGui resources — ALL deferred by 3 frames.
        // ImGui_ImplVulkan_RemoveTexture calls vkFreeDescriptorSets internally,
        // which must NOT happen while in-flight command buffers reference the set.
        // Lambdas MUST capture handles by VALUE only — no [&], no [this].
        VkDevice device = vkCtx->GetDevice();
        VmaAllocator allocator = vkCtx->GetAllocator();

        // Defer ImGui descriptor removal along with the VkImageView they reference
        for (size_t i = 0; i < m_ColorImageViews.size(); i++) {
            VkImageView view = m_ColorImageViews[i];
            VkDescriptorSet imGuiDesc = i < m_ImGuiDescriptorSets.size() ? m_ImGuiDescriptorSets[i] : VK_NULL_HANDLE;
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[device, view, imGuiDesc]() {
                if (imGuiDesc && ImGui::GetCurrentContext()) ImGui_ImplVulkan_RemoveTexture(imGuiDesc);
                vkDestroyImageView(device, view, nullptr);
            }}});
        }
        m_ImGuiDescriptorSets.clear();
        for (size_t i = 0; i < m_ColorImages.size(); i++) {
            VkImage img = m_ColorImages[i];
            VmaAllocation alloc = m_ColorAllocations[i];
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[allocator, img, alloc]() {
                vmaDestroyImage(allocator, img, alloc);
            }}});
        }
        if (m_DepthImageView) {
            VkImageView dv = m_DepthImageView;
            VkDescriptorSet depthImGui = m_DepthImGuiSet;
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[device, dv, depthImGui]() {
                if (depthImGui && ImGui::GetCurrentContext()) ImGui_ImplVulkan_RemoveTexture(depthImGui);
                vkDestroyImageView(device, dv, nullptr);
            }}});
        }
        m_DepthImGuiSet = VK_NULL_HANDLE;
        if (m_DepthImage) {
            VkImage dimg = m_DepthImage;
            VmaAllocation dalloc = m_DepthAllocation;
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[allocator, dimg, dalloc]() {
                vmaDestroyImage(allocator, dimg, dalloc);
            }}});
        }
        if (m_Sampler) {
            VkSampler s = m_Sampler;
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[device, s]() {
                vkDestroySampler(device, s, nullptr);
            }}});
        }
        if (m_ShadowSampler) {
            VkSampler ss = m_ShadowSampler;
            vkCtx->QueueDeferredResource(VulkanContext::DeferredResource{{[device, ss]() {
                vkDestroySampler(device, ss, nullptr);
            }}});
        }

        // Clear vectors to prevent double-free
        m_ColorImages.clear();
        m_ColorImageViews.clear();
        m_ColorAllocations.clear();
        m_DepthImage = VK_NULL_HANDLE;
        m_DepthImageView = VK_NULL_HANDLE;
        m_DepthAllocation = VK_NULL_HANDLE;
        m_Sampler = VK_NULL_HANDLE;
        m_ShadowSampler = VK_NULL_HANDLE;
    }

    void VulkanFramebuffer::Invalidate() {
        if (!m_ColorImages.empty()) Release();

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        VmaAllocator allocator = context->GetAllocator();
        VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);

        // ==========================================
        // 1. 创建颜色附件 (Color Attachments)
        // ==========================================
        for (auto& spec : m_ColorAttachmentSpecs) {
            VkFormat vkFormat = AyayaFormatToVulkanFormat(spec.TextureFormat);

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = vkFormat;
            imageInfo.extent = { m_Specification.Width, m_Specification.Height, 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = samples;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
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
            imageInfo.samples = samples;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            VkResult depthResult = vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr);
            if (depthResult != VK_SUCCESS) {
                AYAYA_CORE_ERROR("Failed to create depth image with SAMPLED_BIT! Try without it...");
                imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                depthResult = vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr);
                if (depthResult != VK_SUCCESS) {
                    AYAYA_CORE_ERROR("Failed to create depth image entirely! Depth will be unavailable.");
                }
            }

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
        }

        // ==========================================
        // 3. 初始布局转换 UNDEFINED → SHADER_READ_ONLY（供 ImGui 采样）
        //    首次渲染前由 EnsureWritable 转为 COLOR_ATTACHMENT
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
            if (m_DepthImage != VK_NULL_HANDLE) {
                VkImageMemoryBarrier depthBarrier{};
                depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                depthBarrier.image = m_DepthImage;
                // D32_SFLOAT_S8_UINT 格式必须同时包含 DEPTH + STENCIL
                depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                depthBarrier.subresourceRange.baseMipLevel = 0;
                depthBarrier.subresourceRange.levelCount = 1;
                depthBarrier.subresourceRange.baseArrayLayer = 0;
                depthBarrier.subresourceRange.layerCount = 1;
                depthBarrier.srcAccessMask = 0;
                depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
            }
            context->EndSingleTimeCommands(cmd);
        }

        // ==========================================
        // 4. 采样器 + ImGui 注册
        // ==========================================
        if (!m_Sampler) {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxAnisotropy = 1.0f;
            vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler);
        }

        // Shadow map hardware PCF sampler — comparison-filtered, white border for out-of-frustum.
        // On MoltenVK portability subset without mutableComparisonSamplers, fall back to
        // a non-comparison sampler (manual PCF in shader).
        if (m_Specification.IsShadowMap && !m_ShadowSampler) {
            bool hasHWPCF = context->GetCapabilities().HasHardwarePCF;

            VkSamplerCreateInfo shadowInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            shadowInfo.magFilter = VK_FILTER_LINEAR;
            shadowInfo.minFilter = VK_FILTER_LINEAR;
            shadowInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            shadowInfo.compareEnable = hasHWPCF ? VK_TRUE : VK_FALSE;
            shadowInfo.compareOp = VK_COMPARE_OP_GREATER;
            shadowInfo.maxAnisotropy = 1.0f;
            vkCreateSampler(device, &shadowInfo, nullptr, &m_ShadowSampler);
        }

        for (auto& view : m_ColorImageViews) {
            m_ImGuiDescriptorSets.push_back(
                ImGui_ImplVulkan_AddTexture(m_Sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        }

        // Depth attachment ImGui descriptor — for FrameDebugger preview
        if (m_DepthImageView != VK_NULL_HANDLE) {
            // After the first RenderGraph execution, the depth is transitioned to
            // DEPTH_STENCIL_READ_ONLY_OPTIMAL by InsertTileResolveBarrier, so sampling works.
            m_DepthImGuiSet = ImGui_ImplVulkan_AddTexture(
                m_Sampler, m_DepthImageView,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }
    }

    void VulkanFramebuffer::Invalidate(VkCommandBuffer cmd) {
        // Async path: records layout transition barriers directly into the active
        // command buffer (used during RenderScene → Compile to avoid vkQueueWaitIdle).
        if (!m_ColorImages.empty()) Release();

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        VmaAllocator allocator = context->GetAllocator();
        VkSampleCountFlagBits samples = static_cast<VkSampleCountFlagBits>(m_Specification.Samples);

        // ── Create color attachments ──
        for (auto& spec : m_ColorAttachmentSpecs) {
            VkFormat vkFormat = AyayaFormatToVulkanFormat(spec.TextureFormat);
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = vkFormat;
            imageInfo.extent = { m_Specification.Width, m_Specification.Height, 1 };
            imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.samples = samples;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo allocInfo{}; allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            VkImage image; VmaAllocation allocation;
            vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
            m_ColorImages.push_back(image);
            m_ColorAllocations.push_back(allocation);

            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = vkFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1; viewInfo.subresourceRange.layerCount = 1;
            VkImageView imageView;
            vkCreateImageView(device, &viewInfo, nullptr, &imageView);
            m_ColorImageViews.push_back(imageView);
        }

        // ── Create depth attachment ──
        if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None) {
            VkFormat depthFormat = context->FindDepthFormat();
            VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = depthFormat;
            imageInfo.extent = { m_Specification.Width, m_Specification.Height, 1 };
            imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.samples = samples;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            VmaAllocationCreateInfo allocInfo{}; allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_DepthImage, &m_DepthAllocation, nullptr);

            VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = m_DepthImage; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.levelCount = 1; viewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &viewInfo, nullptr, &m_DepthImageView);
        }

        // ── Record layout transitions into the active command buffer (no GPU wait) ──
        for (auto& image : m_ColorImages) {
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1; barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        if (m_DepthImage != VK_NULL_HANDLE) {
            VkImageMemoryBarrier depthBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthBarrier.image = m_DepthImage;
            depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            depthBarrier.subresourceRange.levelCount = 1; depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = 0;
            depthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &depthBarrier);
        }

        // ── Samplers + ImGui registration (same as synchronous path) ──
        if (!m_Sampler) {
            VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.maxAnisotropy = 1.0f;
            vkCreateSampler(device, &samplerInfo, nullptr, &m_Sampler);
        }
        if (m_Specification.IsShadowMap && !m_ShadowSampler) {
            bool hasHWPCF = context->GetCapabilities().HasHardwarePCF;
            VkSamplerCreateInfo shadowInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            shadowInfo.magFilter = VK_FILTER_LINEAR;
            shadowInfo.minFilter = VK_FILTER_LINEAR;
            shadowInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            shadowInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            shadowInfo.compareEnable = hasHWPCF ? VK_TRUE : VK_FALSE;
            shadowInfo.compareOp = VK_COMPARE_OP_GREATER;
            shadowInfo.maxAnisotropy = 1.0f;
            vkCreateSampler(device, &shadowInfo, nullptr, &m_ShadowSampler);
        }
        for (auto& view : m_ColorImageViews)
            m_ImGuiDescriptorSets.push_back(
                ImGui_ImplVulkan_AddTexture(m_Sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        if (m_DepthImageView != VK_NULL_HANDLE)
            m_DepthImGuiSet = ImGui_ImplVulkan_AddTexture(
                m_Sampler, m_DepthImageView,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    void VulkanFramebuffer::Bind() {}
    void VulkanFramebuffer::Unbind() {}

    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || (m_Specification.Width == width && m_Specification.Height == height)) return;
        m_Specification.Width = width;
        m_Specification.Height = height;
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        vkDeviceWaitIdle(context->GetDevice());
        Invalidate();
    }

    void* VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const {
        if (index < m_ImGuiDescriptorSets.size()) return (void*)m_ImGuiDescriptorSets[index];
        return nullptr;
    }

    void* VulkanFramebuffer::GetDepthAttachmentRendererID() const {
        return (void*)m_DepthImGuiSet;
    }

} // namespace Ayaya
