#pragma once
#include "Renderer/TextureCube.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    class VulkanTextureCube : public TextureCube {
    public:
        // 核心：支持从 6 张图创建或从现有句柄包装
        VulkanTextureCube(const std::vector<std::string>& faces);
        VulkanTextureCube(uint32_t rendererID, uint32_t width, uint32_t height);
        virtual ~VulkanTextureCube() override;

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        
        // 【关键】：在 Vulkan 下，RendererID 返回的是 VkImageView 的句柄
        virtual uint32_t GetRendererID() const override { return (uint32_t)(uintptr_t)m_ImageView; }

        virtual void Bind(uint32_t slot = 0) const override {}
        virtual void Unbind() const override {}

        virtual AssetType GetType() const override { return AssetType::TextureCube; }
        virtual void SetData(void* data, uint32_t size) override;

        // 供渲染器获取原生信息
        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler GetSampler() const { return m_Sampler; }

    private:
        void Invalidate(); 
        void CreateFromFiles(const std::vector<std::string>& faces);
        void CreateSampler();

    private:
        uint32_t m_Width = 0, m_Height = 0;
        
        VkImage m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        
        bool m_IsWrapped = false;
    };

}