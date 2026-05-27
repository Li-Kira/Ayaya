#pragma once
#include "Renderer/Texture.hpp"
#include "Asset/AssetSettings.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Ayaya {

    class VulkanTexture2D : public Texture2D {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        VulkanTexture2D(void* rendererID, uint32_t width, uint32_t height);
        VulkanTexture2D(const RawTextureData& raw); // 异步加载：从 CPU 数据创建 GPU 纹理
        virtual ~VulkanTexture2D() override;

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }

        virtual uint32_t GetRendererID() const override { return m_BindlessIndex; }

        // ==========================================
        // 【核心修改 2】：覆盖实现，由 Vulkan 层去处理底层翻译
        // ==========================================
        virtual void* GetImGuiTextureID() const override;


        virtual void SetData(void* data, uint32_t size) override;

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind() const override {}

        // Vulkan 2D 贴图也使用 stbi 翻转，告知 ImGui 需要翻转 UV
        // FBO readback textures set this to false — data is already correctly oriented
        virtual bool IsDataFlipped() const override { return m_DataFlipped; }
        void SetDataFlipped(bool flipped) { m_DataFlipped = flipped; }

        virtual uint32_t GetBindlessIndex() const override { return m_BindlessIndex; }
        virtual void SetBindlessIndex(uint32_t index) override { m_BindlessIndex = index; }

        // 供渲染器内部获取真实句柄
        VkImage GetImage() const { return m_Image; }
        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler GetSampler() const { return m_Sampler; }

    private:
        void Invalidate();
        void CreateSampler();

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::string m_Path;
        bool m_IsWrapped = false;
        bool m_DataFlipped = true;

        VkImage m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkFormat m_Format = VK_FORMAT_R8G8B8A8_UNORM;
        TextureImportSettings m_ImportSettings;
        uint32_t m_BindlessIndex = 0;

        // 【新增】：缓存 ImGui 专用的描述符集 (使用 void* 避免污染头文件)
        mutable void* m_ImGuiDescriptorSet = nullptr;
    };

}