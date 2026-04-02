#include "ayapch.h"
#include "OpenGLFramebuffer.hpp"
#include <glad/glad.h>

namespace Ayaya {

    static bool IsDepthFormat(FramebufferTextureFormat format) {
        return format == FramebufferTextureFormat::DEPTH24STENCIL8;
    }

    // 辅助函数：根据我们的枚举获取 OpenGL 内部格式
    static GLenum AyayaTextureFormatToGL(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::RGBA8:       return GL_RGBA8;
            case FramebufferTextureFormat::RGBA16F:     return GL_RGBA16F;
            case FramebufferTextureFormat::RGBA32F:     return GL_RGBA32F;
            case FramebufferTextureFormat::RED_INTEGER: return GL_R32I;
            default: return 0;
        }
    }

    static GLenum AyayaTextureFormatToGLDataFormat(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::RGBA8:       return GL_RGBA;
            case FramebufferTextureFormat::RGBA16F:     return GL_RGBA;
            case FramebufferTextureFormat::RGBA32F:     return GL_RGBA;
            case FramebufferTextureFormat::RED_INTEGER: return GL_RED_INTEGER;
            default: return 0;
        }
    }
    
    static GLenum AyayaTextureFormatToGLDataType(FramebufferTextureFormat format) {
        switch (format) {
            case FramebufferTextureFormat::RGBA8:       return GL_UNSIGNED_BYTE;
            case FramebufferTextureFormat::RGBA16F:     return GL_FLOAT;
            case FramebufferTextureFormat::RGBA32F:     return GL_FLOAT;
            case FramebufferTextureFormat::RED_INTEGER: return GL_INT;
            default: return 0;
        }
    }

    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec) {
        
        for (auto format : m_Specification.Attachments.Attachments) {
            if (!IsDepthFormat(format.TextureFormat))
                m_ColorAttachmentSpecs.emplace_back(format);
            else
                m_DepthAttachmentSpec = format;
        }

        Invalidate();
    }

    OpenGLFramebuffer::~OpenGLFramebuffer() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
            glDeleteTextures(1, &m_DepthAttachment);
        }
        if (m_ResolveFBO) {
            glDeleteFramebuffers(1, &m_ResolveFBO);
            glDeleteTextures(m_ResolveColorAttachments.size(), m_ResolveColorAttachments.data());
        }
    }

    void OpenGLFramebuffer::Invalidate() {
        if (m_RendererID) {
            glDeleteFramebuffers(1, &m_RendererID);
            glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
            glDeleteTextures(1, &m_DepthAttachment);
            if (m_ResolveFBO) {
                glDeleteFramebuffers(1, &m_ResolveFBO);
                glDeleteTextures(m_ResolveColorAttachments.size(), m_ResolveColorAttachments.data());
            }
            m_ColorAttachments.clear();
            m_ResolveColorAttachments.clear();
        }

        bool multisampled = m_Specification.Samples > 1;

        glGenFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);

        // ==========================================
        // 1. 动态生成多个颜色附件 (MRT)
        // ==========================================
        if (m_ColorAttachmentSpecs.size()) {
            m_ColorAttachments.resize(m_ColorAttachmentSpecs.size());
            glGenTextures(m_ColorAttachments.size(), m_ColorAttachments.data());

            for (size_t i = 0; i < m_ColorAttachments.size(); i++) {
                GLenum internalFormat = AyayaTextureFormatToGL(m_ColorAttachmentSpecs[i].TextureFormat);
                GLenum dataFormat = AyayaTextureFormatToGLDataFormat(m_ColorAttachmentSpecs[i].TextureFormat);
                GLenum dataType = AyayaTextureFormatToGLDataType(m_ColorAttachmentSpecs[i].TextureFormat);

                if (multisampled) {
                    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachments[i]);
                    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specification.Samples, internalFormat, m_Specification.Width, m_Specification.Height, GL_FALSE);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D_MULTISAMPLE, m_ColorAttachments[i], 0);
                } else {
                    glBindTexture(GL_TEXTURE_2D, m_ColorAttachments[i]);
                    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Specification.Width, m_Specification.Height, 0, dataFormat, dataType, nullptr);
                    
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, m_ColorAttachments[i], 0);
                }
            }
        }

        // ==========================================
        // 2. 动态生成深度附件
        // ==========================================
        if (m_DepthAttachmentSpec.TextureFormat != FramebufferTextureFormat::None) {
            glGenTextures(1, &m_DepthAttachment);
            if (multisampled) {
                glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_DepthAttachment);
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, m_Specification.Samples, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, GL_FALSE);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D_MULTISAMPLE, m_DepthAttachment, 0);
            } else {
                glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
                // 降级使用 OpenGL 4.1 支持的 glTexImage2D
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
                
                // 【核心修复 1】：必须设置滤波参数！否则 OpenGL 会认为纹理 Incomplete，导致采样返回纯黑(0.0)！
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                
                // 【核心修复 2】：为阴影贴图配置边缘纯白包裹模式 (Clamp to Border)，防止视野外出现黑色伪影
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
            }
        }

        // ==========================================
        // 3. 告诉 OpenGL 我们要同时绘制到哪些缓冲！
        // ==========================================
        if (m_ColorAttachments.size() > 0) {
            // 【核心修复 3】：解除硬编码 4 张贴图的限制！动态生成 buffers 数组，适配 5 张 G-Buffer！
            std::vector<GLenum> buffers(m_ColorAttachments.size());
            for (size_t i = 0; i < m_ColorAttachments.size(); i++) {
                buffers[i] = GL_COLOR_ATTACHMENT0 + i;
            }
            glDrawBuffers((GLsizei)m_ColorAttachments.size(), buffers.data());
        } else if (m_ColorAttachments.empty()) {
            // 只有深度测试的 pass (例如 Shadow Map Pass)
            glDrawBuffer(GL_NONE); 
            glReadBuffer(GL_NONE); // 确保读取也被禁用，保证 FBO 完整性
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            AYAYA_CORE_ERROR("Framebuffer is incomplete!");

        // ==========================================
        // 4. 重建 MSAA 降采样缓冲
        // ==========================================
        if (multisampled && m_ColorAttachments.size() > 0) {
            glGenFramebuffers(1, &m_ResolveFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, m_ResolveFBO);

            m_ResolveColorAttachments.resize(m_ColorAttachments.size());
            glGenTextures(m_ResolveColorAttachments.size(), m_ResolveColorAttachments.data());

            for (size_t i = 0; i < m_ResolveColorAttachments.size(); i++) {
                GLenum internalFormat = AyayaTextureFormatToGL(m_ColorAttachmentSpecs[i].TextureFormat);
                GLenum dataFormat = AyayaTextureFormatToGLDataFormat(m_ColorAttachmentSpecs[i].TextureFormat);
                GLenum dataType = AyayaTextureFormatToGLDataType(m_ColorAttachmentSpecs[i].TextureFormat);

                glBindTexture(GL_TEXTURE_2D, m_ResolveColorAttachments[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Specification.Width, m_Specification.Height, 0, dataFormat, dataType, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, m_ResolveColorAttachments[i], 0);
            }

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
        // MSAA 降采样 (循环拷贝所有开启了的通道)
        if (m_Specification.Samples > 1) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolveFBO);
            
            for (size_t i = 0; i < m_ColorAttachments.size(); i++) {
                glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
                glDrawBuffer(GL_COLOR_ATTACHMENT0 + i);
                glBlitFramebuffer(0, 0, m_Specification.Width, m_Specification.Height, 
                                  0, 0, m_Specification.Width, m_Specification.Height, 
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) return;
        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

    std::shared_ptr<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec) {
        // 由于我们目前只有 OpenGL 后端，直接返回 OpenGLFramebuffer
        return std::make_shared<OpenGLFramebuffer>(spec);
    }
    
}