#pragma once
#include "Renderer/Texture.hpp"

namespace Ayaya {

    class VulkanTexture2D : public Texture2D {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        virtual ~VulkanTexture2D() override;

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetRendererID() const override { return 0; } // 暂时返回 0

        virtual void SetData(void* data, uint32_t size) override;

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind() const override;

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::string m_Path;
        
        // 未来我们会在这里加入：
        // VkImage m_Image;
        // VkDeviceMemory m_Memory;
        // VkImageView m_ImageView;
        // VkSampler m_Sampler;
    };

}