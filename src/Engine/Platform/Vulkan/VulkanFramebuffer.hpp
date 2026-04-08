#pragma once
#include "Renderer/Framebuffer.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace Ayaya {

    class VulkanFramebuffer : public Framebuffer {
    public:
        VulkanFramebuffer(const FramebufferSpecification& spec);
        virtual ~VulkanFramebuffer();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        // 核心：将 DescriptorSet 作为 void* 返回给 ImGui！
        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override { 
            return (void*)m_ImGuiDescriptorSets[index]; 
        }
        virtual void* GetDepthAttachmentRendererID() const override { return nullptr; } 
        virtual void* GetRendererID() const override { return (void*)m_ImGuiDescriptorSets[0]; }
        
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkFramebuffer GetVulkanFramebuffer() const { return m_Framebuffer; }

    private:
        void Invalidate();
        void Release();

    private:
        FramebufferSpecification m_Specification;
        
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;

        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        // 颜色附件资源
        std::vector<VkImage> m_ColorImages;
        std::vector<VkDeviceMemory> m_ColorMemories;
        std::vector<VkImageView> m_ColorImageViews;
        std::vector<VkFormat> m_ColorAttachmentFormats;

        // 深度附件资源
        VkImage m_DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
        VkImageView m_DepthImageView = VK_NULL_HANDLE;
        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

        // 供 ImGui 读取的采样器和描述符集
        VkSampler m_Sampler = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_ImGuiDescriptorSets;
    };

}