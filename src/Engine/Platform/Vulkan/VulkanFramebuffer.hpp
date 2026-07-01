#pragma once
#include "Renderer/Framebuffer.hpp"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace Ayaya {

    // Vulkan 1.3 Dynamic Rendering — 不再使用 VkRenderPass / VkFramebuffer
    class VulkanFramebuffer : public Framebuffer {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec, bool asyncInit = false);
        virtual ~VulkanFramebuffer() override;

        void Invalidate();
        void Invalidate(VkCommandBuffer cmd);  // async: records barriers into active command buffer
        void Release();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override;
        virtual void* GetDepthAttachmentRendererID() const override;
        virtual void* GetRendererID() const override { return nullptr; }
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        // 格式查询 — 供 Pipeline (VkPipelineRenderingCreateInfo) 和 BeginRendering 使用
        uint32_t GetColorAttachmentCount() const { return (uint32_t)m_ColorAttachmentSpecs.size(); }
        VkFormat GetColorAttachmentFormat(uint32_t index) const;
        VkFormat GetDepthAttachmentFormat() const;
        bool HasDepthAttachment() const { return m_DepthImage != VK_NULL_HANDLE; }
        VkSampleCountFlagBits GetSampleCount() const {
            return static_cast<VkSampleCountFlagBits>(m_Specification.Samples);
        }

        inline VkImageView GetColorAttachmentImageView(uint32_t index) const {
            AYAYA_CORE_ASSERT(index < m_ColorImageViews.size(), "Color Attachment Index out of bounds!");
            return m_ColorImageViews[index];
        }

        inline VkImage GetColorAttachmentImage(uint32_t index) const {
            if (index >= m_ColorImages.size()) return VK_NULL_HANDLE;
            return m_ColorImages[index];
        }
        inline VkImage GetDepthAttachmentImage() const { return m_DepthImage; }
        inline VkImageView GetDepthAttachmentImageView() const { return m_DepthImageView; }
        inline VkSampler GetSampler() const { return m_Sampler; }
        inline VkSampler GetShadowSampler() const { return m_ShadowSampler; }

    private:
        FramebufferSpecification m_Specification;
        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        std::vector<VkImage> m_ColorImages;
        std::vector<VmaAllocation> m_ColorAllocations;
        std::vector<VkImageView> m_ColorImageViews;
        std::vector<VkDescriptorSet> m_ImGuiDescriptorSets;
        VkDescriptorSet m_DepthImGuiSet = VK_NULL_HANDLE;

        VkImage m_DepthImage = VK_NULL_HANDLE;
        VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;

        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;     // hardware PCF for shadow maps
    };
}
