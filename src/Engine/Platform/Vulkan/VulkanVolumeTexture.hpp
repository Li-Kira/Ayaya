#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

namespace Ayaya {

    // Minimal 3D storage image wrapper (VK_IMAGE_TYPE_3D). Used for volumetric
    // resources written by compute shaders and sampled later (froxel fog, future
    // voxel GI). Follows the Hi-Z storage-image pattern in VulkanGBufferPass.
    class VulkanVolumeTexture {
    public:
        VulkanVolumeTexture() = default;
        VulkanVolumeTexture(VkDevice device, VmaAllocator allocator,
                            uint32_t width, uint32_t height, uint32_t depth, VkFormat format);
        ~VulkanVolumeTexture();

        // Non-copyable (owns GPU resources).
        VulkanVolumeTexture(const VulkanVolumeTexture&) = delete;
        VulkanVolumeTexture& operator=(const VulkanVolumeTexture&) = delete;

        void Init(VkDevice device, VmaAllocator allocator,
                  uint32_t width, uint32_t height, uint32_t depth, VkFormat format);
        void Release();

        VkImage     GetImage() const { return m_Image; }
        VkImageView GetImageView() const { return m_ImageView; }
        VkSampler   GetSampler() const { return m_Sampler; }
        uint32_t    GetWidth()  const { return m_Width; }
        uint32_t    GetHeight() const { return m_Height; }
        uint32_t    GetDepth()  const { return m_Depth; }
        VkFormat    GetFormat() const { return m_Format; }
        bool        IsValid() const { return m_Image != VK_NULL_HANDLE; }

        // Layout transitions (raw image barriers on the 3D image). Tracks the
        // current layout so repeated frames (UNDEFINED→GENERAL on first use,
        // SHADER_READ_ONLY→GENERAL afterwards) transition correctly.
        void TransitionToGeneral(VkCommandBuffer cmd);    // current → GENERAL (compute write)
        void TransitionToReadOnly(VkCommandBuffer cmd);   // current → SHADER_READ_ONLY (sample)

    private:
        VkDevice      m_Device = VK_NULL_HANDLE;
        VmaAllocator  m_Allocator = VK_NULL_HANDLE;
        VkImage       m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView   m_ImageView = VK_NULL_HANDLE;
        VkSampler     m_Sampler = VK_NULL_HANDLE;
        VkFormat      m_Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        uint32_t      m_Width = 0, m_Height = 0, m_Depth = 0;
        VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

} // namespace Ayaya
