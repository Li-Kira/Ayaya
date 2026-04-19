#pragma once
#include "Renderer/Framebuffer.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
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
        virtual void* GetRendererID() const override { return m_Framebuffer; }
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        inline VkRenderPass GetVulkanRenderPass() const { return m_RenderPass; }
        inline VkFramebuffer GetVulkanFramebuffer() const { return m_Framebuffer; }

        // ==========================================\
        // 【核心对齐】：专供 VulkanRenderCommandBuffer 使用的获取器
        // ==========================================
        inline VkImageView GetColorAttachmentImageView(uint32_t index) const {
            AYAYA_CORE_ASSERT(index < m_ColorImageViews.size(), "Color Attachment Index out of bounds!");
            return m_ColorImageViews[index];
        }

        // ==========================================
        // 【新增】：暴漏原生 VkImage 供 IBLBuilder 物理拷贝使用
        // ==========================================
        inline VkImage GetColorAttachmentImage(uint32_t index) const {
            return m_ColorImages[index];
        }
        
        inline VkImageView GetDepthAttachmentImageView() const { return m_DepthImageView; }
        inline VkSampler GetSampler() const { return m_Sampler; }

    private:
        FramebufferSpecification m_Specification;
        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        // Vulkan 物理资源
        std::vector<VkImage> m_ColorImages;
        std::vector<VmaAllocation> m_ColorAllocations;
        std::vector<VkImageView> m_ColorImageViews;
        std::vector<VkDescriptorSet> m_ImGuiDescriptorSets; 

        VkImage m_DepthImage = VK_NULL_HANDLE;
        VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;

        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE; // 全局采样器
    };

}