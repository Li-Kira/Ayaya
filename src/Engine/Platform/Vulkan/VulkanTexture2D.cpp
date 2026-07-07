#include "ayapch.h"
#include "VulkanTexture2D.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include "Asset/AssetManager.hpp"

#include <stb_image.h>
// ==========================================
// 【核心修改 3】：把底层 API 脏活累活藏在具体的实现文件里
// ==========================================
#include <backends/imgui_impl_vulkan.h>

namespace Ayaya {

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) {
        Invalidate();
    }

    // 从已有 VkImageView 包装 (用于 IBL BRDF LUT 等预先烘焙的纹理)
    VulkanTexture2D::VulkanTexture2D(void* rendererID, uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height), m_IsWrapped(true) {
        m_ImageView = (VkImageView)rendererID;
        CreateSampler();

        if (m_ImageView != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE) {
            auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
            m_BindlessIndex = context->GetBindlessManager().AllocateIndex();
            if (m_BindlessIndex != 0) {
                context->GetBindlessManager().UpdateBinding(context->GetDevice(), m_BindlessIndex, m_ImageView, m_Sampler);
            }
        }
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Path(path) {
        int w, h, channels;

        // 1. 先读取导入设置（FlipY 等），再决定是否翻转
        UUID handle = AssetManager::FindHandleForPath(path);
        if (handle != 0) m_ImportSettings = AssetManager::GetMetadata(handle).TextureSettings;

        bool isHDR = stbi_is_hdr(path.c_str());
        void* pixels = nullptr;

        stbi_set_flip_vertically_on_load(m_ImportSettings.FlipY ? 1 : 0);
        m_DataFlipped = m_ImportSettings.FlipY;

        if (isHDR) {
            pixels = stbi_loadf(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            m_Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        } else {
            pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            m_Format = m_ImportSettings.SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        }

        if (!pixels) {
            AYAYA_CORE_ERROR("Failed to load texture: {0}", path);
            return;
        }

        // Un-premultiply alpha if the texture was saved with pre-multiplied alpha
        // (e.g. Photoshop "Export As"). Produces straight alpha for the shader.
        if (m_ImportSettings.UnpremultiplyAlpha && !isHDR) {
            uint8_t* p = (uint8_t*)pixels;
            for (int i = 0; i < w * h; i++) {
                uint8_t a = p[3];
                if (a > 0 && a < 255) {
                    p[0] = (uint8_t)std::min((uint32_t)p[0] * 255 / a, 255u);
                    p[1] = (uint8_t)std::min((uint32_t)p[1] * 255 / a, 255u);
                    p[2] = (uint8_t)std::min((uint32_t)p[2] * 255 / a, 255u);
                }
                p += 4;
            }
        }

        m_Width = w; m_Height = h;
        Invalidate(); // 内部使用更新后的 m_Format 创建 VkImage
        
        uint32_t bpp = isHDR ? 16 : 4; // HDR 是 4个float=16字节，LDR 是 4字节
        SetData(pixels, m_Width * m_Height * bpp);
        
        stbi_image_free(pixels);
    }

    // 异步加载：从 CPU 端原始数据创建 GPU 纹理（主线程执行）
    VulkanTexture2D::VulkanTexture2D(const RawTextureData& raw)
        : m_Width(raw.Width), m_Height(raw.Height), m_Path(raw.SourcePath) {
        m_ImportSettings = raw.ImportSettings;
        m_DataFlipped = raw.ImportSettings.FlipY;

        if (raw.IsHDR) {
            m_Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        } else {
            m_Format = m_ImportSettings.SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        }
        // Un-premultiply alpha for async-loaded textures
        if (m_ImportSettings.UnpremultiplyAlpha && !raw.IsHDR) {
            uint8_t* p = (uint8_t*)raw.Pixels;
            for (int i = 0; i < (int)(m_Width * m_Height); i++) {
                uint8_t a = p[3];
                if (a > 0 && a < 255) {
                    p[0] = (uint8_t)std::min((uint32_t)p[0] * 255 / a, 255u);
                    p[1] = (uint8_t)std::min((uint32_t)p[1] * 255 / a, 255u);
                    p[2] = (uint8_t)std::min((uint32_t)p[2] * 255 / a, 255u);
                }
                p += 4;
            }
        }

        Invalidate();
        uint32_t bpp = raw.IsHDR ? 16 : 4;
        SetData(raw.Pixels, m_Width * m_Height * bpp);
    }

    VulkanTexture2D::~VulkanTexture2D() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        vkDeviceWaitIdle(device);

        if (m_BindlessIndex != 0) {
            // Deferred release: the bindless index is freed after 3 frames
            // to ensure the GPU has finished referencing it in-flight.
            context->QueueDeferredBindlessRelease(m_BindlessIndex);
            m_BindlessIndex = 0;
        }

        if (m_ImGuiDescriptorSet) {
            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr) {
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)m_ImGuiDescriptorSet);
            }
            m_ImGuiDescriptorSet = nullptr;
        }

        // 包装模式：Image/ImageView 属于外部 (如 IBLBuilder)，只销毁自己的 Sampler
        if (m_IsWrapped) {
            if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);
            return;
        }

        vkDeviceWaitIdle(device);
        if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);
        if (m_ImageView) vkDestroyImageView(device, m_ImageView, nullptr);
        if (m_Image) vmaDestroyImage(context->GetAllocator(), m_Image, m_Allocation);
    }

    // Check if format is compressed (ASTC/BCn/ETC2) — Vulkan forbids vkCmdBlitImage on these
    static bool IsCompressedFormat(VkFormat fmt) {
        switch (fmt) {
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:  case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC2_UNORM_BLOCK:      case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC3_UNORM_BLOCK:      case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC4_UNORM_BLOCK:      case VK_FORMAT_BC4_SNORM_BLOCK:
            case VK_FORMAT_BC5_UNORM_BLOCK:      case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC6H_UFLOAT_BLOCK:    case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:      case VK_FORMAT_BC7_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
            case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
            case VK_FORMAT_EAC_R11_UNORM_BLOCK:  case VK_FORMAT_EAC_R11_SNORM_BLOCK:
            case VK_FORMAT_EAC_R11G11_UNORM_BLOCK: case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
            case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:  case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
            case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:  case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
            case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:  case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
            case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:  case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
            case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:  case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
            case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:  case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
            case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:  case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
            case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:  case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
            case VK_FORMAT_ASTC_10x5_UNORM_BLOCK: case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
            case VK_FORMAT_ASTC_10x6_UNORM_BLOCK: case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
            case VK_FORMAT_ASTC_10x8_UNORM_BLOCK: case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
            case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
            case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
            case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
                return true;
            default: return false;
        }
    }

    void VulkanTexture2D::Invalidate() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        // Calculate mip levels with safety checks
        bool isCompressed = IsCompressedFormat(m_Format);
        if (!isCompressed && m_ImportSettings.GenerateMipmaps) {
            // Verify hardware supports linear blit for this format
            VkFormatProperties fmtProps;
            vkGetPhysicalDeviceFormatProperties(context->GetPhysicalDevice(), m_Format, &fmtProps);
            bool supportsLinearBlit = (fmtProps.optimalTilingFeatures &
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
            if (supportsLinearBlit)
                m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_Width, m_Height)))) + 1;
            else
                m_MipLevels = 1;
        } else {
            m_MipLevels = 1;
        }

        // 1. 创建 VkImage
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = m_Width;
        imageInfo.extent.height = m_Height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = m_MipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_Format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr);

        // 2. 创建 ImageView (all mip levels)
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);

        CreateSampler();
    }

    // 在 VulkanTexture2D.cpp 中补充采样器配置
    void VulkanTexture2D::CreateSampler() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        bool linear = (m_ImportSettings.Filter == TextureFilterMode::Linear);
        samplerInfo.magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        samplerInfo.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        VkSamplerAddressMode addr = (m_ImportSettings.Wrap == TextureWrapMode::Clamp)
            ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeU = addr;
        samplerInfo.addressModeV = addr;
        samplerInfo.addressModeW = addr;

        samplerInfo.anisotropyEnable = false;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_MipLevels);
        samplerInfo.mipLodBias = 0.0f;

        vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler);
    }

    // ==========================================
    // 实现懒加载包装：只有当 UI 真正需要画这张图时，才向 Vulkan 申请描述符集
    // ==========================================
    void* VulkanTexture2D::GetImGuiTextureID() const {
        if (m_ImGuiDescriptorSet == nullptr && m_Sampler != VK_NULL_HANDLE && m_ImageView != VK_NULL_HANDLE) {
            // 将 Vulkan 视口和采样器打包，注册给 ImGui 后端
            m_ImGuiDescriptorSet = (void*)ImGui_ImplVulkan_AddTexture(
                m_Sampler, 
                m_ImageView, 
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }
        return m_ImGuiDescriptorSet;
    }

    void VulkanTexture2D::SetData(void* data, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VmaAllocator allocator = context->GetAllocator();

        // ==========================================
        // 1. 创建暂存缓冲区 (Staging Buffer)
        // 用于在 CPU 和 GPU 之间中转像素数据
        // ==========================================
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // 声明它是一个数据源
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;
        VmaAllocationInfo stagingAllocInfo;
        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo);

        // ==========================================
        // 2. 将 CPU 图片数据拷贝到暂存缓冲区
        // ==========================================
        memcpy(stagingAllocInfo.pMappedData, data, size);

        // ==========================================
        // 3. 录制单次指令将数据推送给真正的 Image
        // ==========================================
        VkCommandBuffer cmdBuffer = context->BeginSingleTimeCommands();

        // 【步骤 A】：将图片布局从 UNDEFINED 转换为 传输目标 (TRANSFER_DST)
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_MipLevels;  // all mips → DST for subsequent blit
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 【步骤 B】：将暂存缓冲区里的像素拷贝到图片中
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {m_Width, m_Height, 1};

        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // 【步骤 C-MIP】：逐级 Blit 生成 Mipmap 链，流水线式 Layout 转换
        // Mip 0 is now TRANSFER_DST_OPTIMAL with the source data.
        if (m_MipLevels > 1) {
            int32_t mipW = static_cast<int32_t>(m_Width);
            int32_t mipH = static_cast<int32_t>(m_Height);

            for (uint32_t i = 1; i < m_MipLevels; i++) {
                int32_t nextW = mipW > 1 ? mipW / 2 : 1;
                int32_t nextH = mipH > 1 ? mipH / 2 : 1;

                // Step A: Mip[i-1]  DST → SRC  (prepare as blit source for current level)
                VkImageMemoryBarrier mipBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                mipBarrier.image = m_Image;
                mipBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1 };
                vkCmdPipelineBarrier(cmdBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                // Step B: Blit Mip[i-1] → Mip[i]
                VkImageBlit blit{};
                blit.srcOffsets[1] = { mipW, mipH, 1 };
                blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 };
                blit.dstOffsets[1] = { nextW, nextH, 1 };
                blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 };
                vkCmdBlitImage(cmdBuffer,
                    m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit, VK_FILTER_LINEAR);

                // Step C: Mip[i-1]  SRC → SHADER_READ_ONLY  (this level done)
                mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmdBuffer,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &mipBarrier);

                mipW = nextW; mipH = nextH;
            }

            // Step D: Final mip level  DST → SHADER_READ_ONLY
            barrier.subresourceRange.baseMipLevel = m_MipLevels - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        } else {
            // 【步骤 C】：将图片布局从 传输目标 转换为 着色器只读 (单层 mip 路径)
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmdBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // 4. 提交命令并阻塞等待 GPU 执行完毕
        context->EndSingleTimeCommands(cmdBuffer);

        // 5. 过河拆桥：数据已经到了显卡里的 Image 中，干掉暂存缓冲区
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

        // 6. 注册到全局 Bindless 纹理数组
        if (m_BindlessIndex == 0 && m_ImageView != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE) {
            m_BindlessIndex = context->GetBindlessManager().AllocateIndex();
            if (m_BindlessIndex != 0) {
                context->GetBindlessManager().UpdateBinding(context->GetDevice(), m_BindlessIndex, m_ImageView, m_Sampler);
            }
        }

        AYAYA_CORE_INFO("VulkanTexture2D: Pixel data securely uploaded & Layout transitioned for {0}", m_Path.empty() ? "Generated Texture" : m_Path);
    }

    void VulkanTexture2D::SetDataBatched(VkCommandBuffer cmd, VkBuffer stagingBuffer,
                                         VkDeviceSize stagingOffset, void* data, uint32_t size) {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());

        // Transition image to TRANSFER_DST (all mip levels)
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = m_MipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy from staging sub-region to image
        VkBufferImageCopy region{};
        region.bufferOffset = stagingOffset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { m_Width, m_Height, 1 };

        vkCmdCopyBufferToImage(cmd, stagingBuffer, m_Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to SHADER_READ_ONLY (skip mip generation — caller pre-disables GenerateMipmaps)
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Register bindless index
        if (m_BindlessIndex == 0 && m_ImageView != VK_NULL_HANDLE && m_Sampler != VK_NULL_HANDLE) {
            m_BindlessIndex = context->GetBindlessManager().AllocateIndex();
            if (m_BindlessIndex != 0) {
                context->GetBindlessManager().UpdateBinding(context->GetDevice(), m_BindlessIndex, m_ImageView, m_Sampler);
            }
        }
    }

    void VulkanTexture2D::Bind(uint32_t slot) const {
        // Vulkan 不需要全局 Bind，由 VulkanRenderCommandBuffer::BindTexture2D 处理描述符更新
    }
}