#include "ayapch.h"
#include "OpenGLFramebuffer.hpp"
#include "Renderer/Renderer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {
        Invalidate();
    }

    OpenGLFramebuffer::~OpenGLFramebuffer() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_ColorAttachment);
            glDeleteTextures(1, &m_DepthAttachment);
        }
        if (m_ResolveFBO) {
            glDeleteFramebuffers(1, &m_ResolveFBO);
            glDeleteTextures(1, &m_ResolveColorAttachment);
        }
    }

    void OpenGLFramebuffer::Invalidate() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(1, &m_ColorAttachment);
            glDeleteTextures(1, &m_DepthAttachment);
            
            if (m_ResolveFBO) {
                glDeleteFramebuffers(1, &m_ResolveFBO);
                glDeleteTextures(1, &m_ResolveColorAttachment);
            }
        }

        bool multisampled = m_Specification.Samples > 1;

        // ==========================================
        // A. 创建主渲染缓冲 (支持 MSAA)
        // ==========================================
        glGenFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        // 1. 创建颜色附件
        glGenTextures(1, &m_ColorAttachment);
        if (multisampled) {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachment);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specification.Samples, GL_RGBA8, m_Specification.Width, m_Specification.Height, GL_FALSE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachment, 0);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);
        }

        // 2. 创建深度附件
        glGenTextures(1, &m_DepthAttachment);
        if (multisampled) {
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_DepthAttachment);
            glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specification.Samples, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, GL_FALSE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, m_DepthAttachment, 0);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            AYAYA_CORE_ERROR("Framebuffer is incomplete!");

        // ==========================================
        // B. 如果开启了 MSAA，额外创建一个普通的 Resolve 缓冲给 ImGui 用
        // ==========================================
        if (multisampled) {
            glGenFramebuffers(1, &m_ResolveFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, m_ResolveFBO);

            glGenTextures(1, &m_ResolveColorAttachment);
            glBindTexture(GL_TEXTURE_2D, m_ResolveColorAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Specification.Width, m_Specification.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ResolveColorAttachment, 0);
            
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                AYAYA_CORE_ERROR("Resolve Framebuffer is incomplete!");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, m_Specification.Width, m_Specification.Height);
    }

    void OpenGLFramebuffer::Unbind() {
        // ==========================================
        // 核心步骤：硬件级 MSAA 降采样 (Blit)
        // ==========================================
        if (m_Specification.Samples > 1) {
            // 指定读取源为 MSAA 缓冲
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
            // 指定写入目标为普通缓冲
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveFBO);
            
            // 执行硬件拷贝与混合降采样
            glBlitFramebuffer(0, 0, m_Specification.Width, m_Specification.Height, 
                              0, 0, m_Specification.Width, m_Specification.Height, 
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        
        // 恢复绑定到默认屏幕缓冲
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            AYAYA_CORE_WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
            return;
        }
        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

    // 工厂方法
    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
        // 理想情况下这里应该有 switch(Renderer::GetAPI())，这里保持你原有的精简写法
        return std::make_shared<OpenGLFramebuffer>(spec);
    }
}