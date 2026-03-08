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
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);

        if (data) {
            m_Width = width;
            m_Height = height;

            GLenum internalFormat = 0, dataFormat = 0;
            
            if (channels == 4) {
                internalFormat = GL_RGBA8;
                dataFormat = GL_RGBA;
            } else if (channels == 3) {
                internalFormat = GL_RGB8;
                dataFormat = GL_RGB;
            } else if (channels == 2) {
                // 顺手加上双通道支持 (有时用于包含 R和G 通道的特殊混合贴图/法线贴图)
                internalFormat = GL_RG8;
                dataFormat = GL_RG;
            } else if (channels == 1) {
                // ==========================================
                // 核心：支持灰度图 (Roughness, Metallic, AO)
                // ==========================================
                internalFormat = GL_R8;
                dataFormat = GL_RED;
            }

            // 安全防范：处理不支持的通道数
            if (internalFormat == 0 || dataFormat == 0) {
                AYAYA_CORE_ERROR("Unsupported number of channels: {0} in texture: {1}", channels, path);
                stbi_image_free(data);
                return;
            }

            m_InternalFormat = internalFormat;
            m_DataFormat = dataFormat;

            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);

            // PBR 渲染中，放大过滤推荐使用 GL_LINEAR 使过渡更平滑
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
            if (channels == 1 || channels == 2) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            }

            // 传输数据并生成 Mipmap
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            // 传输完成后，务必将状态恢复为默认的 4 字节对齐，以免污染后续其他贴图的加载
            if (channels == 1 || channels == 2) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }

            stbi_image_free(data);
        } else {
            AYAYA_CORE_ERROR("Failed to load texture at: {0}", path);
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

    // --- 向显存填充像素数据 ---
    void OpenGLTexture2D::SetData(void* data, uint32_t size) {
        
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
}