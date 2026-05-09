#include "ayapch.h"
#include "VulkanTexture2D.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"

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
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Path(path) {
        int w, h, channels;
        
        // 1. 探测是否为 HDR
        bool isHDR = stbi_is_hdr(path.c_str());
        void* pixels = nullptr;
        
        if (isHDR) {
            // 使用浮点加载，强制 4 通道保证对齐
            pixels = stbi_loadf(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            m_Format = VK_FORMAT_R32G32B32A32_SFLOAT; // 或者 R16G16B16A16_SFLOAT
        } else {
            pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            // 只有颜色贴图用 SRGB；法线/金属度/粗糙度/AO 是数学数据，必须用线性
            std::string lowerPath = path;
            for (auto& c : lowerPath) c = (char)std::tolower(c);
            bool isLinearData = (lowerPath.find("normal")   != std::string::npos ||
                                 lowerPath.find("metallic") != std::string::npos ||
                                 lowerPath.find("roughness")!= std::string::npos ||
                                 lowerPath.find("ao.")     != std::string::npos ||
                                 lowerPath.find("height")  != std::string::npos ||
                                 lowerPath.find("displace")!= std::string::npos);
            m_Format = isLinearData ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
        }

        if (!pixels) {
            AYAYA_CORE_ERROR("Failed to load texture: {0}", path);
            return;
        }

        m_Width = w; m_Height = h;
        Invalidate(); // 内部使用更新后的 m_Format 创建 VkImage
        
        uint32_t bpp = isHDR ? 16 : 4; // HDR 是 4个float=16字节，LDR 是 4字节
        SetData(pixels, m_Width * m_Height * bpp);
        
        stbi_image_free(pixels);
    }

    VulkanTexture2D::~VulkanTexture2D() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        VkDevice device = context->GetDevice();
        vkDeviceWaitIdle(device);

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

    // 在 VulkanTexture2D.cpp 中补充采样器配置
    void VulkanTexture2D::CreateSampler() {
        auto context = std::dynamic_pointer_cast<VulkanContext>(Application::Get().GetWindow().GetContext());
        
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR; // 线性过滤
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // 重复平铺
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = false;   // 开启各项异性过滤
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;

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
        barrier.subresourceRange.levelCount = 1;
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

        // 【步骤 C】：将图片布局从 传输目标 转换为 着色器只读 (SHADER_READ_ONLY) —— 解决验证层报错的核心！
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        // 4. 提交命令并阻塞等待 GPU 执行完毕
        context->EndSingleTimeCommands(cmdBuffer);

        // 5. 过河拆桥：数据已经到了显卡里的 Image 中，干掉暂存缓冲区
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

        AYAYA_CORE_INFO("VulkanTexture2D: Pixel data securely uploaded & Layout transitioned for {0}", m_Path.empty() ? "Generated Texture" : m_Path);
    }

    void VulkanTexture2D::Bind(uint32_t slot) const {
        // Vulkan 不需要全局 Bind，由 VulkanRenderCommandBuffer::BindTexture2D 处理描述符更新
    }
}