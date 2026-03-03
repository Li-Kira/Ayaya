#pragma once

#include <string>
#include <memory>

namespace Ayaya {

    class Texture {
    public:
        virtual ~Texture() = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRendererID() const = 0;

        virtual void Bind(uint32_t slot = 0) const = 0;
    };

    class Texture2D : public Texture {
    public:
        static std::shared_ptr<Texture2D> Create(const std::string& path);
    };

    // 在 Texture.hpp 中增加
    class TextureLibrary {
    public:
        std::shared_ptr<Texture2D> Load(const std::string& path);
        std::shared_ptr<Texture2D> Get(const std::string& name);
    private:
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_Textures;
    };

}