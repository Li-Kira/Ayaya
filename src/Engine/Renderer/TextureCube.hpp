#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Ayaya {

    class TextureCube {
    public:
        // 传入 6 张贴图的路径。
        // 顺序必须严格按照 OpenGL 标准：
        // 0: Right, 1: Left, 2: Top, 3: Bottom, 4: Front, 5: Back
        TextureCube(const std::vector<std::string>& faces);
        ~TextureCube();

        void Bind(uint32_t slot = 0) const;
        void Unbind() const;

        uint32_t GetRendererID() const { return m_RendererID; }

    private:
        uint32_t m_RendererID;
        int m_Width = 0;
        int m_Height = 0;
    };

}