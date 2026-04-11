#pragma once
#include "Renderer/Framebuffer.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace Ayaya {

    class VulkanFramebuffer : public Framebuffer {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec);
        virtual ~VulkanFramebuffer() override;

        void Invalidate();
        void Release();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override;
        virtual void* GetDepthAttachmentRendererID() const override;
        virtual void* GetRendererID() const override;
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        // 【新增】
        inline VkRenderPass GetVulkanRenderPass() const { return m_RenderPass; }
        inline VkFramebuffer GetVulkanFramebuffer() const { return m_Framebuffer; }

        // 【新增】：专供 Vulkan 描述符集装货使用的底层句柄
        inline VkImageView GetColorImageView(uint32_t index) const {
            AYAYA_CORE_ASSERT(index < m_ColorImageViews.size(), "Index out of bounds!");
            return m_ColorImageViews[index];
        }
        inline VkImageView GetDepthImageView() const { return m_DepthImageView; }

        inline VkSampler GetColorSampler() const { return m_ColorSampler; }

    private:
        FramebufferSpecification m_Specification;
        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        // ==========================================
        // Vulkan 专属的物理显存资源
        // ==========================================
        std::vector<VkImage> m_ColorImages;
        std::vector<VkDeviceMemory> m_ColorMemories;
        std::vector<VkImageView> m_ColorImageViews;
        
        // 【核心大杀器】：专门喂给 ImGui 的 DescriptorSet (它就是原本的 textureID)
        std::vector<VkDescriptorSet> m_ImGuiDescriptorSets; 

        VkImage m_DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;

        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE; // Vulkan 专属：FBO 必须配有自己的 RenderPass
        VkSampler m_ColorSampler = VK_NULL_HANDLE;  // 供 ImGui 采样用的采样器
    };

}