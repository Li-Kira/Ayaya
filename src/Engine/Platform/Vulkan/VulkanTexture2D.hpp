#pragma once
#include "Renderer/Texture.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    class VulkanTexture2D : public Texture2D {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        virtual ~VulkanTexture2D() override;

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        // 【关键】：在 Vulkan 下，RendererID 返回的是 VkImageView 的句柄，供 ImGui 使用
        virtual uint32_t GetRendererID() const override { return (uint32_t)(uintptr_t)m_ImageView; }

        virtual void SetData(void* data, uint32_t size) override;

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind() const override {}

        // 供渲染器内部获取真实句柄
        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler GetSampler() const { return m_Sampler; }

    private:
        void Invalidate();
        void CreateSampler();

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::string m_Path;
        
        VkImage m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkFormat m_Format = VK_FORMAT_R8G8B8A8_UNORM;
    };

}