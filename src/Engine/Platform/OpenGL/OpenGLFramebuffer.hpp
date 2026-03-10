#pragma once

#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    class OpenGLFramebuffer : public Framebuffer {
    public:
        OpenGLFramebuffer(const FramebufferSpecification& spec);
        virtual ~OpenGLFramebuffer();

        void Invalidate();

        virtual void Bind() override;
        virtual void Unbind() override;

        virtual void Resize(uint32_t width, uint32_t height) override;

        // ==========================================
        // 核心魔法：向 ImGui 提供正确的贴图 ID
        // ==========================================
        virtual uint32_t GetColorAttachmentRendererID() const override { 
            return m_Specification.Samples > 1 ? m_ResolveColorAttachment : m_ColorAttachment; 
        }
        
        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_ColorAttachment = 0, m_DepthAttachment = 0;

        // ==========================================
        // 新增：用于 MSAA 降采样 (Resolve) 的第二套缓冲
        // ==========================================
        uint32_t m_ResolveFBO = 0;
        uint32_t m_ResolveColorAttachment = 0;

        FramebufferSpecification m_Specification;
    };

}