#pragma once

#include <string>
#include <memory>
#include <cstdint> // 确保 uint32_t 能被正确识别

// --- 新增：引入刚才写的 Asset 基类 ---
#include "Asset/Asset.hpp"

namespace Ayaya {

    // --- 修改 1：让 Texture 继承自 Asset ---
    class Texture : public Asset {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRendererID() const = 0;

        // --- 新增：允许通过代码直接向显存写入像素数据 ---
        virtual void SetData(void* data, uint32_t size) = 0;

        virtual void Bind(uint32_t slot = 0) const = 0;
    };

    class Texture2D : public Texture {
    public:
        // --- 新增：通过代码指定宽高生成贴图 ---
        static std::shared_ptr<Texture2D> Create(uint32_t width, uint32_t height);

        static std::shared_ptr<Texture2D> Create(const std::string& path);

        // --- 修改 2：实现基类的类型识别 ---
        virtual AssetType GetType() const override { return AssetType::Texture2D; }
    };

    // ========================================================
    // 注意：原来的 TextureLibrary 已经被彻底移除了！
    // 因为全局的 AssetManager 已经完全接管了它的工作。
    // ========================================================
}