#include "ayapch.h"
#include "OpenGLTextureCube.hpp"
#include <glad/glad.h>
#include <stb_image.h>
#include "Core/Log.hpp"

namespace Ayaya {

    OpenGLTextureCube::OpenGLTextureCube(const std::vector<std::string>& faces) {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

        int width, height, nrChannels;
        stbi_set_flip_vertically_on_load(false); 

        for (unsigned int i = 0; i < faces.size(); i++) {
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                // Mac 兼容处理：强制申请 GL_RGBA8 内存
                GLenum internalFormat = GL_RGBA8; 
                GLenum dataFormat = GL_RGB;
                
                if (nrChannels == 4) {
                    dataFormat = GL_RGBA;
                } else if (nrChannels == 3) {
                    dataFormat = GL_RGB;
                }

                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                             0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            } else {
                AYAYA_CORE_ERROR("Cubemap texture failed to load at path: {0}", faces[i]);
                unsigned char black[4] = {0, 0, 0, 255};
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                             0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
            }
        }

        m_Width = width;
        m_Height = height;

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        stbi_set_flip_vertically_on_load(true);
    }

    OpenGLTextureCube::OpenGLTextureCube(void* rendererID, int width, int height)
        : m_RendererID((uint32_t)(uintptr_t)rendererID), m_Width(width), m_Height(height) 
    {
    }

    OpenGLTextureCube::~OpenGLTextureCube() {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTextureCube::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
    }

    void OpenGLTextureCube::Unbind() const {
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

}