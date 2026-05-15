#pragma once
#include <cstdint>

namespace Ayaya {

    enum class TextureFilterMode { Linear = 0, Nearest = 1 };
    enum class TextureWrapMode   { Repeat = 0, Clamp = 1 };

    struct TextureImportSettings {
        TextureFilterMode Filter = TextureFilterMode::Linear;
        TextureWrapMode   Wrap   = TextureWrapMode::Repeat;
        bool SRGB           = true;
        bool GenerateMipmaps = true;
        bool FlipY          = true;
    };

}
