#include "ayapch.h"
#include "OpenGLTexture2D.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

namespace Ayaya {

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
        : m_Path(path) {
        int width, height, channels;
        // 翻转 Y 轴，因为 OpenGL 的坐标系原点在左下角
        stbi_set_flip_vertically_on_load(1);

        // ==========================================
        // 核心新增：探测是否为 HDR 文件
        // ==========================================
        bool isHDR = stbi_is_hdr(path.c_str());

        if (isHDR) {
            // 使用 stbi_loadf 读取浮点数，保留极高的能量动态范围
            float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 0);
            if (data) {
                m_Width = width;
                m_Height = height;

                GLenum internalFormat = 0, dataFormat = 0;
                if (channels == 4) {
                    internalFormat = GL_RGBA16F;
                    dataFormat = GL_RGBA;
                } else if (channels == 3) {
                    internalFormat = GL_RGB16F;
                    dataFormat = GL_RGB;
                }

                // 安全防范：处理不支持的通道
                if (internalFormat == 0 || dataFormat == 0) {
                    AYAYA_CORE_ERROR("Unsupported number of channels: {0} in HDR texture: {1}", channels, path);
                    stbi_image_free(data);
                    return;
                }
                m_InternalFormat = internalFormat;
                m_DataFormat = dataFormat;

                glGenTextures(1, &m_RendererID);
                glBindTexture(GL_TEXTURE_2D, m_RendererID);

                // 注意：对于环境贴图，使用 GL_CLAMP_TO_EDGE 能有效防止边缘接缝处的采样溢出
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                // 重点：数据类型必须传 GL_FLOAT
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_FLOAT, data);

                stbi_image_free(data);
                AYAYA_CORE_INFO("Successfully loaded HDR Texture: {0} ({1}x{2})", path, m_Width, m_Height);
            } else {
                AYAYA_CORE_ERROR("Failed to load HDR texture at: {0}", path);
            }
        } 
        else {
            // ==========================================
            // 这里保留你原来那套极其完善的 8-bit LDR 读取逻辑
            // ==========================================
            stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
            if (data) {
                m_Width = width;
                m_Height = height;

                GLenum internalFormat = 0, dataFormat = 0;
                if (channels == 4) { internalFormat = GL_RGBA8; dataFormat = GL_RGBA; } 
                else if (channels == 3) { internalFormat = GL_RGB8; dataFormat = GL_RGB; } 
                else if (channels == 2) { internalFormat = GL_RG8; dataFormat = GL_RG; } 
                else if (channels == 1) { internalFormat = GL_R8; dataFormat = GL_RED; }

                // 安全防范：处理不支持的通道
                if (internalFormat == 0 || dataFormat == 0) {
                    AYAYA_CORE_ERROR("Unsupported number of channels: {0} in texture: {1}", channels, path);
                    stbi_image_free(data);
                    return;
                }
                m_InternalFormat = internalFormat;
                m_DataFormat = dataFormat;

                glGenTextures(1, &m_RendererID);
                glBindTexture(GL_TEXTURE_2D, m_RendererID);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

                // ==========================================
                // 核心陷阱防御：取消 4 字节对齐限制！
                // ==========================================
                // OpenGL 默认按 4 字节读取像素。对于单通道(1字节)图像，
                // 若宽度不是 4 的倍数，会导致内存读取错位，画面斜向扭曲！
                // 这里强制告诉 OpenGL 按 1 字节（紧凑像素）读取。
                if (channels == 1 || channels == 2) glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
                glGenerateMipmap(GL_TEXTURE_2D);

                if (channels == 1 || channels == 2) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

                stbi_image_free(data);
            } else {
                AYAYA_CORE_ERROR("Failed to load texture at: {0}", path);
            }
        }
    }

    // --- 根据宽高创建空白贴图的构造函数 ---
    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height) 
    {
        m_InternalFormat = GL_RGBA8;
        m_DataFormat = GL_RGBA;

        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, GL_UNSIGNED_BYTE, nullptr);
    }

    OpenGLTexture2D::OpenGLTexture2D(void* rendererID, uint32_t width, uint32_t height)
        : m_RendererID((uint32_t)(uintptr_t)rendererID), m_Width(width), m_Height(height)
    {
    }

    // 异步加载：从 CPU 端原始数据创建 GL 纹理（主线程执行）
    OpenGLTexture2D::OpenGLTexture2D(const RawTextureData& raw)
        : m_Path(raw.SourcePath), m_Width(raw.Width), m_Height(raw.Height) {
        if (!raw.Pixels) return;

        if (raw.IsHDR) {
            m_InternalFormat = raw.Channels == 4 ? GL_RGBA16F : GL_RGB16F;
            m_DataFormat = raw.Channels == 4 ? GL_RGBA : GL_RGB;
            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, GL_FLOAT, raw.Pixels);
        } else {
            if (raw.Channels == 4)      { m_InternalFormat = GL_RGBA8; m_DataFormat = GL_RGBA; }
            else if (raw.Channels == 3) { m_InternalFormat = GL_RGB8;  m_DataFormat = GL_RGB; }
            else if (raw.Channels == 2) { m_InternalFormat = GL_RG8;   m_DataFormat = GL_RG; }
            else if (raw.Channels == 1) { m_InternalFormat = GL_R8;    m_DataFormat = GL_RED; }
            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (raw.Channels == 1 || raw.Channels == 2) glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, m_InternalFormat, m_Width, m_Height, 0, m_DataFormat, GL_UNSIGNED_BYTE, raw.Pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            if (raw.Channels == 1 || raw.Channels == 2) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
    }

    void* OpenGLTexture2D::GetImGuiTextureID() const {
        // 使用 intptr_t 进行安全中转，防止 64 位编译器产生长度截断警告
        return (void*)(intptr_t)m_RendererID;
    }

    // --- 向显存填充像素数据 ---
    void OpenGLTexture2D::SetData(void* data, uint32_t size) {
        // 【新增防御】：如果是接管的外部 ID（格式未知），严禁 CPU 直接修改！
        if (m_DataFormat == 0 || m_InternalFormat == 0) {
            AYAYA_CORE_ERROR("Cannot call SetData on a texture with unknown format (External ID)!");
            return;
        }

        // 动态计算 bpp (Bytes Per Pixel)
        uint32_t bpp = 4;
        if (m_DataFormat == GL_RGBA) bpp = 4;
        else if (m_DataFormat == GL_RGB) bpp = 3;
        else if (m_DataFormat == GL_RG) bpp = 2;
        else if (m_DataFormat == GL_RED) bpp = 1;

        // assert(size == m_Width * m_Height * bpp); 
        
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        // 如果是外部动态写入单通道或双通道数据，同样需要解除对齐限制
        if (bpp == 1 || bpp == 2) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        }

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);

        if (bpp == 1 || bpp == 2) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
    }

    OpenGLTexture2D::~OpenGLTexture2D() {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    void OpenGLTexture2D::Unbind() const {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}