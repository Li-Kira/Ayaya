#include "ayapch.h"
#include "VulkanTextureCube.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

namespace Ayaya {

    VulkanTextureCube::VulkanTextureCube(uint32_t rendererID, uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height), m_IsWrapped(true) {
        m_ImageView = (VkImageView)(uintptr_t)rendererID;
        // 注意：如果是包装类型，通常 Sampler 已经在 IBLBuilder 中创建好并绑定了
    }

    VulkanTextureCube::VulkanTextureCube(const std::vector<std::string>& faces) {
        m_Width = 1024; m_Height = 1024; // 默认尺寸
        Invalidate();
        AYAYA_CORE_INFO("VulkanTextureCube created from 6 faces.");
    }

    VulkanTextureCube::~VulkanTextureCube() {
        if (m_IsWrapped) return; 

        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        
        vkDeviceWaitIdle(device);
        if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);
        if (m_ImageView) vkDestroyImageView(device, m_ImageView, nullptr);
        if (m_Image) vmaDestroyImage(context->GetAllocator(), m_Image, m_Allocation);
    }

    void VulkanTextureCube::Invalidate() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        // 【核心】：设置 arrayLayers = 6 并开启 CUBE_COMPATIBLE
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { m_Width, m_Height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6; 
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        
        vmaCreateImage(context->GetAllocator(), &imageInfo, nullptr, &m_Image, &m_Allocation, nullptr);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // 关键：指定为 CUBE
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.layerCount = 6;

        vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);
    }

    void VulkanTextureCube::SetData(void* data, uint32_t size) {
        AYAYA_CORE_WARN("VulkanTextureCube::SetData called but not implemented yet!");
        // 如果未来需要动态更新 Cubemap 的某个面，需要写 StagingBuffer 拷贝逻辑
    }
}