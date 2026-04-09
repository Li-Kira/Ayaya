#include "ayapch.h"
#include "VulkanTexture2D.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) {
        // 占位提示：这里以后会创建 VkImage
        AYAYA_CORE_WARN("VulkanTexture2D created (Stub): {0}x{1}", width, height);
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path)
        : m_Path(path) {
        // 占位提示：这里以后会用 stb_image 读取图片并上传到 VkImage
        AYAYA_CORE_WARN("VulkanTexture2D created (Stub) from path: {0}", path);
    }

    VulkanTexture2D::~VulkanTexture2D() {
        // 占位提示：清理 VkImage 资源
    }

    void VulkanTexture2D::SetData(void* data, uint32_t size) {
        // 占位提示：以后需要用 Staging Buffer 将像素数据拷贝到显存
        AYAYA_CORE_WARN("VulkanTexture2D::SetData called but not implemented yet!");
    }

    void VulkanTexture2D::Bind(uint32_t slot) const {
        // Vulkan 不需要像 OpenGL 那样绑定状态机，通常是通过 DescriptorSet 绑定
    }

    void VulkanTexture2D::Unbind() const {}

}