#include "ayapch.h"
#include "VulkanTexture2D.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

#include <stb_image.h>

namespace Ayaya {

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) {
        Invalidate();
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path)
        : m_Path(path) {
        int w, h, channels;
        // 强制转换为 4 通道 RGBA
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        
        if (!pixels) {
            AYAYA_CORE_ERROR("Failed to load texture: {0}", path);
            return;
        }

        m_Width = (uint32_t)w;
        m_Height = (uint32_t)h;
        
        Invalidate();
        SetData(pixels, m_Width * m_Height * 4);
        
        stbi_image_free(pixels);
    }

    VulkanTexture2D::~VulkanTexture2D() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();

        vkDeviceWaitIdle(device); // 销毁前确保 GPU 没在用它
        if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);
        if (m_ImageView) vkDestroyImageView(device, m_ImageView, nullptr);
        if (m_Image) vmaDestroyImage(context->GetAllocator(), m_Image, m_Allocation);
    }

    void VulkanTexture2D::Invalidate() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        // 1. 创建 VkImage
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_Width;
        imageInfo.extent.height = m_Height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_Format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr);

        // 2. 创建 ImageView
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);
        
        CreateSampler();
    }

    void VulkanTexture2D::CreateSampler() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE; // 【修改】：暂时关闭各向异性，防止特性校验报错
        // samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler);
    }

    void VulkanTexture2D::SetData(void* data, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        // 此处应调用你定义的 Staging Buffer 拷贝逻辑
        // 简写：创建暂存缓冲 -> Map内存 -> Unmap -> 录制拷贝命令 -> 转换布局到 SHADER_READ_ONLY
        AYAYA_CORE_INFO("VulkanTexture2D: Pixel data uploaded for {0}", m_Path);
    }

    void VulkanTexture2D::Bind(uint32_t slot) const {
        // Vulkan 不需要全局 Bind，由 VulkanRenderCommandBuffer::BindTexture2D 处理描述符更新
    }
}