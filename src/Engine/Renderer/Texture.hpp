#pragma once

#include <string>
#include <memory>
#include <cstdint>

#include "Asset/Asset.hpp"
#include <stb_image.h>

namespace Ayaya {

    // ==========================================
    // CPU 端纹理原始数据（后台线程加载产物）
    // 析构时自动释放 stb 内存
    // ==========================================
    struct RawTextureData {
        void* Pixels = nullptr;
        int Width = 0, Height = 0, Channels = 0;
        bool IsHDR = false;
        std::string SourcePath;

        ~RawTextureData() {
            if (Pixels) { stbi_image_free(Pixels); Pixels = nullptr; }
        }

        RawTextureData() = default;

        RawTextureData(RawTextureData&& other) noexcept
            : Pixels(other.Pixels), Width(other.Width), Height(other.Height)
            , Channels(other.Channels), IsHDR(other.IsHDR)
            , SourcePath(std::move(other.SourcePath)) {
            other.Pixels = nullptr;
        }

        RawTextureData& operator=(RawTextureData&& other) noexcept {
            if (this != &other) {
                if (Pixels) stbi_image_free(Pixels);
                Pixels = other.Pixels; other.Pixels = nullptr;
                Width = other.Width; Height = other.Height;
                Channels = other.Channels; IsHDR = other.IsHDR;
                SourcePath = std::move(other.SourcePath);
            }
            return *this;
        }

        RawTextureData(const RawTextureData&) = delete;
        RawTextureData& operator=(const RawTextureData&) = delete;
    };

    class Texture : public Asset {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRendererID() const = 0;

        virtual void* GetImGuiTextureID() const {
            return (void*)(intptr_t)GetRendererID();
        }

        virtual void SetData(void* data, uint32_t size) = 0;

        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual void Unbind() const = 0;
        virtual bool IsDataFlipped() const { return false; }
    };

    class Texture2D : public Texture {
    public:
        static std::shared_ptr<Texture2D> Create(uint32_t width, uint32_t height);
        static std::shared_ptr<Texture2D> Create(const std::string& path);
        static std::shared_ptr<Texture2D> Create(void* rendererID, uint32_t width, uint32_t height);

        // 异步加载：后台线程读取像素（纯 CPU）
        static RawTextureData LoadRawDataFromDisk(const std::string& path);
        // 异步加载：主线程 GPU 上传
        static std::shared_ptr<Texture2D> CreateFromRawData(const RawTextureData& raw);

        virtual AssetType GetType() const override { return AssetType::Texture2D; }
    };

}
