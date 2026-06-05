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
        bool UnpremultiplyAlpha = false; // undo pre-multiplied alpha on load (Photoshop exports)
    };

    enum class NormalMode   { Import = 0, Calculate = 1, None = 2 };
    enum class TangentMode  { Import = 0, Calculate = 1, None = 2 };

    struct ModelImportSettings {
        float GlobalScale      = 1.0f;
        NormalMode Normals     = NormalMode::Import;
        TangentMode Tangents   = TangentMode::Import;
        bool ImportMaterials   = true;
        bool WeldVertices      = false;
        bool MeshCompression   = false;
        bool SwapYZ            = false;
    };

}
