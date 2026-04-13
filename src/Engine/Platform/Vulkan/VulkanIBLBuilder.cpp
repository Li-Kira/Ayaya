#include "ayapch.h"
#include "VulkanIBLBuilder.hpp"
#include "Core/Log.hpp"
#include "Platform/Vulkan/VulkanContext.hpp"
#include "Core/Application.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Ayaya {

    // ==========================================
    // 💡 Vulkan IBL 烘焙核心指南：
    // 在 Vulkan 中，你不能像 OpenGL 那样随时 glFramebufferTexture2D。
    // 正确的做法是：
    // 1. 创建目标 VkImage (设置 VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT，层数为 6)。
    // 2. 为每一层 (Face) 和每一个 Mip 级别单独创建一个 VkImageView (VK_IMAGE_VIEW_TYPE_2D)。
    // 3. 为上述每一个 ImageView 创建一个 VkFramebuffer。
    // 4. 使用一个专用的单通道 VkRenderPass 进行绘制。
    // 5. 循环 6 次 (或 MipLevel * 6 次)，修改 PushConstant 的 ViewMatrix，录制命令。
    // ==========================================

    uint32_t VulkanIBLBuilder::ConvertEquirectangularToCubemap(const std::shared_ptr<Texture2D>& hdrTexture, 
                                                               const std::shared_ptr<Mesh>& cubeMesh, 
                                                               const std::shared_ptr<Shader>& convertShader) {
        AYAYA_CORE_WARN("VulkanIBLBuilder: ConvertEquirectangularToCubemap is currently a stub.");
        
        // 核心流程预告：
        // 1. 创建分辨率为 1024x1024，包含 6 层的 VkImage
        // 2. 创建 6 个单层的 VkImageView
        // 3. 构造 6 个方向的 LookAt 矩阵
        // 4. 开启 CommandBuffer，遍历 6 个面，绑定对应 FBO，推送矩阵，DrawIndexed(CubeMesh)
        // 5. 转换 ImageLayout 到 SHADER_READ_ONLY_OPTIMAL

        return 0; // 返回未来创建的 VulkanTextureCube 的内部 ID
    }

    uint32_t VulkanIBLBuilder::CreateIrradianceMap(uint32_t envCubemap, 
                                                   const std::shared_ptr<Mesh>& cubeMesh, 
                                                   const std::shared_ptr<Shader>& irradianceShader) {
        AYAYA_CORE_WARN("VulkanIBLBuilder: CreateIrradianceMap is currently a stub.");
        return 0; 
    }

    uint32_t VulkanIBLBuilder::CreatePrefilterMap(uint32_t envCubemap, 
                                                  const std::shared_ptr<Mesh>& cubeMesh, 
                                                  const std::shared_ptr<Shader>& prefilterShader) {
        AYAYA_CORE_WARN("VulkanIBLBuilder: CreatePrefilterMap is currently a stub.");
        return 0; 
    }

    uint32_t VulkanIBLBuilder::CreateBRDFLUT(const std::shared_ptr<Shader>& brdfShader, uint32_t emptyVAO) {
        AYAYA_CORE_WARN("VulkanIBLBuilder: CreateBRDFLUT is currently a stub.");
        
        // 核心流程预告 (这个最简单，因为它是一张 2D 贴图)：
        // 1. 创建 512x512 的普通 VkImage (Texture2D)
        // 2. 创建单目标 VkFramebuffer
        // 3. BeginRenderPass, BindPipeline(BRDFShader)
        // 4. DrawArrays(3) 或 DrawTriangleStrip(4)
        // 5. EndRenderPass

        return 0; 
    }

}