#pragma once
#include "Material.hpp"
#include <memory>
#include <string>

namespace Ayaya {

    class MaterialSerializer {
    public:
        // 将内存中的材质保存为 .mat 文件
        static void Serialize(const std::shared_ptr<Material>& material, const std::string& filepath);
        // 从 .mat 文件读取属性，自动重组材质面板！
        static bool Deserialize(const std::shared_ptr<Material>& material, const std::string& filepath);
    };

}