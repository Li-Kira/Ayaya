#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Renderer/Texture.hpp" // 继承自 Texture 基类

namespace Ayaya {

    class TextureCube : public Texture {
    public:
        virtual ~TextureCube() = default;

        // 【工厂方法】：从 6 张贴图路径创建
        static std::shared_ptr<TextureCube> Create(const std::vector<std::string>& faces);

        // 【工厂方法】：从已有的底层显存 ID 包装 (比如从 IBLBuilder 传过来)
        static std::shared_ptr<TextureCube> Create(void* rendererID, int width = 1024, int height = 1024);
    };

}