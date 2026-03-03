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
            }

            m_InternalFormat = internalFormat;
            m_DataFormat = dataFormat;

            glGenTextures(1, &m_RendererID);
            glBindTexture(GL_TEXTURE_2D, m_RendererID);

            // 配置过滤与包裹参数
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_Width, m_Height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            stbi_image_free(data);
        } else {
            AYAYA_CORE_ERROR("Failed to load texture at: {0}", path);
        }
    }

    OpenGLTexture2D::~OpenGLTexture2D() {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    // 工厂方法
    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path) {
        return std::make_shared<OpenGLTexture2D>(path);
    }

}