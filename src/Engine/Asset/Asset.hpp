#pragma once
#include "Core/UUID.hpp"

namespace Ayaya {

    // 定义当前引擎支持的资产类型
    enum class AssetType : uint16_t {
        None = 0,
        Scene,
        Texture2D
    };

    class Asset {
    public:
        // 核心：每个存在于内存中的资源，都有一个独一无二的 UUID
        UUID Handle; 

        virtual ~Asset() = default;

        // 子类必须实现，用于在运行时判断这是什么资源
        virtual AssetType GetType() const = 0; 
    };

}