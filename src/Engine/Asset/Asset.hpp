#pragma once
#include "Core/UUID.hpp"

namespace Ayaya {

    // 定义引擎支持的完整资产类型
    enum class AssetType : uint16_t {
        None = 0,
        Scene,
        Texture2D,
        TextureCube,
        Model,       // 新增：3D模型
        Material,    // 新增：材质属性
        LuaScript    // 新增：Lua脚本文件
    };

    class Asset {
    public:
        UUID Handle; // 每个资产唯一的身份证

        virtual ~Asset() = default;
        virtual AssetType GetType() const = 0; 
    };

}