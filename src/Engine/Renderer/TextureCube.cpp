#include "TextureCube.hpp"
#include <glad/glad.h>
#include <stb_image.h>
#include "Core/Log.hpp" // 假设你有类似 AYAYA_CORE_ERROR 的日志宏

namespace Ayaya {

    TextureCube::TextureCube(const std::vector<std::string>& faces) {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

        int width, height, nrChannels;
        
        // Cubemap 通常不需要垂直翻转
        stbi_set_flip_vertically_on_load(false); 

        for (unsigned int i = 0; i < faces.size(); i++) {
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                // ==========================================
                // 【核心修复 1】：Mac 对 GL_RGB 的内部支持极差，
                // 我们强行向显卡申请 GL_RGBA8 的完美内存！
                // ==========================================
                GLenum internalFormat = GL_RGBA8; 
                GLenum dataFormat = GL_RGB;
                
                if (nrChannels == 4) {
                    dataFormat = GL_RGBA;
                } else if (nrChannels == 3) {
                    dataFormat = GL_RGB;
                }

                // ==========================================
                // 【核心修复 2】：解除 4 字节对齐限制，防止非 2 次幂图片变蓝/倾斜！
                // ==========================================
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                // 将内存中的 RGB 数据，安全地塞进显卡的 RGBA8 空间里 (Alpha 自动补 1.0)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                             0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
                
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // 存完恢复默认状态

                stbi_image_free(data);
            } else {
                AYAYA_CORE_ERROR("Cubemap texture failed to load at path: {0}", faces[i]);
                
                // ==========================================
                // 【核心修复 3】：防御性编程
                // 如果用户填错了某张图的路径，不要让显存空着！
                // 塞一张 1x1 的纯黑像素进去，防止引发显存泄漏变蓝！
                // ==========================================
                unsigned char black[4] = {0, 0, 0, 255};
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                             0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
            }
        }

        // ==========================================
        // 设置纹理环绕与过滤方式
        // ==========================================
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        stbi_set_flip_vertically_on_load(true); 
    }

    TextureCube::TextureCube(uint32_t rendererID, int width, int height)
        : m_RendererID(rendererID), m_Width(width), m_Height(height)
    {
        // 我们什么都不需要做，因为 IBLBuilder 已经生成了 ID 并填充了 HDR 数据！
        // 这里的 TextureCube 对象只是优雅地将它包装成了引擎的 C++ 资产。
    }

    TextureCube::~TextureCube() {
        glDeleteTextures(1, &m_RendererID);
    }

    void TextureCube::Bind(uint32_t slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);
    }

    void TextureCube::Unbind() const {
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

}