#include "ayapch.h"
#include "VulkanVolumeTexture.hpp"

namespace Ayaya {

    VulkanVolumeTexture::VulkanVolumeTexture(VkDevice device, VmaAllocator allocator,
                                             uint32_t width, uint32_t height, uint32_t depth,
                                             VkFormat format) {
        Init(device, allocator, width, height, depth, format);
    }

    VulkanVolumeTexture::~VulkanVolumeTexture() {
        Release();
    }

    void VulkanVolumeTexture::Init(VkDevice device, VmaAllocator allocator,
                                   uint32_t width, uint32_t height, uint32_t depth,
                                   VkFormat format) {
        if (m_Image != VK_NULL_HANDLE) Release();
        m_Device = device;
        m_Allocator = allocator;
        m_Width = width; m_Height = height; m_Depth = depth;
        m_Format = format;
        m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImageCreateInfo imgCI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imgCI.imageType = VK_IMAGE_TYPE_3D;
        imgCI.format = format;
        imgCI.extent = { width, height, depth };
        imgCI.mipLevels = 1;
        imgCI.arrayLayers = 1;
        imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgCI.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(allocator, &imgCI, &allocCI, &m_Image, &m_Allocation, nullptr);

        VkImageViewCreateInfo viewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewCI.image = m_Image;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewCI.format = format;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel = 0;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &viewCI, nullptr, &m_ImageView);

        VkSamplerCreateInfo sampCI{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampCI.magFilter = VK_FILTER_LINEAR;
        sampCI.minFilter = VK_FILTER_LINEAR;
        sampCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.minLod = 0.0f;
        sampCI.maxLod = 0.0f;
        vkCreateSampler(device, &sampCI, nullptr, &m_Sampler);
    }

    void VulkanVolumeTexture::Release() {
        if (m_Sampler && m_Device)   vkDestroySampler(m_Device, m_Sampler, nullptr);
        if (m_ImageView && m_Device) vkDestroyImageView(m_Device, m_ImageView, nullptr);
        if (m_Image && m_Allocator)  vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
        m_Sampler = VK_NULL_HANDLE;
        m_ImageView = VK_NULL_HANDLE;
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void VulkanVolumeTexture::TransitionToGeneral(VkCommandBuffer cmd) {
        if (!m_Image) return;
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.oldLayout = m_CurrentLayout;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_Image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = (m_CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        VkPipelineStageFlags srcStage = (m_CurrentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        m_CurrentLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    void VulkanVolumeTexture::TransitionToReadOnly(VkCommandBuffer cmd) {
        if (!m_Image) return;
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.oldLayout = m_CurrentLayout;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_Image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        m_CurrentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

} // namespace Ayaya
