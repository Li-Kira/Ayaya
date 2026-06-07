#pragma once
#include "Core/UUID.hpp"

namespace Ayaya {

    // 定义引擎支持的完整资产类型
    enum class AssetType : uint16_t {
        None = 0,
        Scene,
        Texture2D,
        TextureCube,
        Model,
        Material,
        LuaScript,
        Prefab      // entity tree template
    };

    class Asset {
    public:
        UUID Handle; // 每个资产唯一的身份证

        virtual ~Asset() = default;
        virtual AssetType GetType() const = 0; 
    };

}