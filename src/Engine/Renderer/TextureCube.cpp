#include "TextureCube.hpp"
#include <glad/glad.h>
#include <stb_image.h>
#include "Core/Log.hpp" // 假设你有类似 AYAYA_CORE_ERROR 的日志宏

namespace Ayaya {

    TextureCube::TextureCube(const std::vector<std::string>& faces) {
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_RendererID);

        int width, height, nrChannels;
        
        // ==========================================
        // 核心细节：加载 Cubemap 时通常不需要垂直翻转！
        // ==========================================
        stbi_set_flip_vertically_on_load(false); 

        for (unsigned int i = 0; i < faces.size(); i++) {
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                // 判断是 RGB 还是 RGBA
                GLenum format = GL_RGB;
                if (nrChannels == 4) format = GL_RGBA;
                else if (nrChannels == 3) format = GL_RGB;

                // OpenGL 的 Cubemap 面枚举刚好是连续的：
                // GL_TEXTURE_CUBE_MAP_POSITIVE_X + 0 (Right)
                // GL_TEXTURE_CUBE_MAP_POSITIVE_X + 1 (Left) ... 依此类推
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 
                             0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            } else {
                AYAYA_CORE_ERROR("Cubemap texture failed to load at path: {0}", faces[i]);
                stbi_image_free(data);
            }
        }

        // ==========================================
        // 设置纹理环绕与过滤方式
        // ==========================================
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // 使用 CLAMP_TO_EDGE 防止面与面之间的接缝处出现肉眼可见的黑线
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        // 恢复默认的翻转设置，以免影响后续普通 2D 贴图的加载
        stbi_set_flip_vertically_on_load(true); 
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