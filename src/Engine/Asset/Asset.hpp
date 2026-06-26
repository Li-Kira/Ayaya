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
        Prefab,      // entity tree template
        SubMesh,     // lightweight: ParentHandle + SubMeshIndex, no own buffers
        Curve,       // Hermite-interpolated animation curve (.curve)
        SRPipeline,  // Scriptable Render Pipeline (.srp Lua file)
        HLSLShader,  // HLSL shader source (.hlsl text file)
        AyaShader    // Unified shader definition (.ayashader YAML)
    };

    class Asset {
    public:
        UUID Handle; // 每个资产唯一的身份证

        virtual ~Asset() = default;
        virtual AssetType GetType() const = 0; 
    };

}