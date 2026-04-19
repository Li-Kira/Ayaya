#include "ayapch.h"
#include "VulkanTextureCube.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

#include <stb_image.h>

namespace Ayaya {

    // ==========================================
    // 构造函数 1：从现有句柄包装 (如 IBL 计算产物)
    // ==========================================
    VulkanTextureCube::VulkanTextureCube(void* rendererID, uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height), m_IsWrapped(true) {
        m_ImageView = (VkImageView)rendererID;
        CreateSampler();
    }

    // ==========================================
    // 构造函数 2：从本地硬盘的 6 张图读取天空盒
    // ==========================================
    VulkanTextureCube::VulkanTextureCube(const std::vector<std::string>& faces) {
        m_IsWrapped = false;
        CreateFromFiles(faces);
        AYAYA_CORE_INFO("VulkanTextureCube fully loaded and transferred to GPU from 6 faces.");
    }

    VulkanTextureCube::~VulkanTextureCube() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        
        vkDeviceWaitIdle(device);

        // ==========================================
        // 【核心修复 2A】：Sampler 是我们自己创建的，不管是不是包装模式，都必须销毁！
        // ==========================================
        if (m_Sampler) vkDestroySampler(device, m_Sampler, nullptr);

        // 包装模式下，Image 和 View 属于 IBLBuilder，我们无权销毁，直接返回
        if (m_IsWrapped) return; 

        // 非包装模式（从文件读取），我们自己负责销毁
        if (m_ImageView) vkDestroyImageView(device, m_ImageView, nullptr);
        if (m_Image) vmaDestroyImage(context->GetAllocator(), m_Image, m_Allocation);
    }

    void VulkanTextureCube::SetData(void* data, uint32_t size) {
        AYAYA_CORE_WARN("SetData for TextureCube is not implemented yet!");
    }

    // ==========================================
    // 【核心实现】：分配显存、挖坑、建立视图
    // ==========================================
    void VulkanTextureCube::Invalidate() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        // 1. 创建 VkImage (开启 CUBE_COMPATIBLE)
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { m_Width, m_Height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;  // 天空盒的 6 个面
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        vmaCreateImage(context->GetAllocator(), &imageInfo, nullptr, &m_Image, &m_Allocation, nullptr);

        // 2. 创建 VkImageView
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // 必须是 CUBE 视图
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6; // 必须囊括 6 个面
        
        vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView);
    }

    // ==========================================
    // 【核心实现】：配置天空盒专属采样器
    // ==========================================
    void VulkanTextureCube::CreateSampler() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        
        // 【关键防御】：天空盒必须使用 CLAMP_TO_EDGE，否则在矩形交界处会看到一条清晰的黑线！
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler);
    }

    // ==========================================
    // 【终极重头戏】：读取 6 张图，打包上传到显存
    // ==========================================
    void VulkanTextureCube::CreateFromFiles(const std::vector<std::string>& faces) {
        // 1. 检查第一张图是否为 HDR 以决定全局格式
        bool isHDR = stbi_is_hdr(faces[0].c_str());
        VkFormat format = isHDR ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t bytesPerPixel = isHDR ? 16 : 4;

        std::vector<void*> pixels(6);
        int width, height, channels;

        for (int i = 0; i < 6; i++) {
            if (isHDR) 
                pixels[i] = stbi_loadf(faces[i].c_str(), &width, &height, &channels, STBI_rgb_alpha);
            else 
                pixels[i] = stbi_load(faces[i].c_str(), &width, &height, &channels, STBI_rgb_alpha);
        }

        m_Width = width;
        m_Height = height;
        uint32_t faceSize = m_Width * m_Height * 4; // 每个面的字节数 (RGBA = 4)
        uint32_t imageSize = faceSize * 6;          // 总字节数

        // 2. 创建 VMA 暂存缓冲区 (Staging Buffer)
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkBuffer stagingBuffer;
        VmaAllocation stagingAllocation;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        vmaCreateBuffer(context->GetAllocator(), &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr);

        // 3. 将 6 张图的数据按顺序拷贝进暂存区
        void* data;
        vmaMapMemory(context->GetAllocator(), stagingAllocation, &data);
        for (int i = 0; i < 6; i++) {
            memcpy((uint8_t*)data + (i * faceSize), pixels[i], faceSize);
            stbi_image_free(pixels[i]); // 内存复制完就可以把 stb 读的内存释放了
        }
        vmaUnmapMemory(context->GetAllocator(), stagingAllocation);

        // 4. 正式在 GPU 分配 Image 坑位
        Invalidate();
        CreateSampler();

        // 5. 开启一次性指令录制 (假设你的 VulkanContext 有这两个常用的封装函数)
        VkCommandBuffer cmdBuffer = context->BeginSingleTimeCommands();

        // 步骤 A: 转换 Image 布局为 "准备接收拷贝数据" (TRANSFER_DST)
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6; // 关键：转换所有 6 层

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 步骤 B: 拷贝数据 (Vulkan 非常聪明，指定 layerCount = 6，它会自动按 faceSize 切分数据块填入 6 个面)
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 6; // 关键：拷贝 6 层
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { m_Width, m_Height, 1 };

        vkCmdCopyBufferToImage(cmdBuffer, stagingBuffer, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // 步骤 C: 转换 Image 布局为 "着色器只读" (SHADER_READ_ONLY)
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 6. 提交指令并等待完成
        context->EndSingleTimeCommands(cmdBuffer);

        // 7. 过河拆桥，销毁暂存区
        vmaDestroyBuffer(context->GetAllocator(), stagingBuffer, stagingAllocation);
    }
}