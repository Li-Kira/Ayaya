#pragma once

#include "Renderer/Framebuffer.hpp"
#include <vector>

namespace Ayaya {

    class OpenGLFramebuffer : public Framebuffer {
    public:
        OpenGLFramebuffer(const FramebufferSpecification& spec);
        virtual ~OpenGLFramebuffer();

        void Invalidate();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        // 获取指定索引的附件
        // ==========================================
        // 修改：返回 void*，内部将 GLuint 强转为指针
        // ==========================================
        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override { 
            uint32_t id = m_Specification.Samples > 1 ? m_ResolveColorAttachments[index] : m_ColorAttachments[index]; 
            return (void*)(intptr_t)id; 
        }

        virtual void* GetDepthAttachmentRendererID() const override { 
            return (void*)(intptr_t)m_DepthAttachment; 
        }

        virtual void* GetRendererID() const override { 
            return (void*)(intptr_t)m_RendererID; 
        }
        
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

    private:
        uint32_t m_RendererID = 0;
        
        // ==========================================
        // 核心：支持多渲染目标 (MRT) 数组
        // ==========================================
        std::vector<uint32_t> m_ColorAttachments;
        uint32_t m_DepthAttachment = 0;

        // MSAA 降采样缓冲数组
        uint32_t m_ResolveFBO = 0;
        std::vector<uint32_t> m_ResolveColorAttachments;

        std::vector<FramebufferTextureSpecification> m_ColorAttachmentSpecs;
        FramebufferTextureSpecification m_DepthAttachmentSpec = FramebufferTextureFormat::None;

        FramebufferSpecification m_Specification;
    };

}