#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

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

    enum class MaterialSearchRule {
        Local,        // Search the FBX's directory only for existing .mat files
        RecursiveUp,  // Walk up through project directories to find matching materials
        DoNotCreate   // Skip material generation entirely, assign built-in default
    };

    struct ModelImportSettings {
        // Transform
        float GlobalScale      = 1.0f;
        // Geometry
        NormalMode Normals     = NormalMode::Import;
        TangentMode Tangents   = TangentMode::Import;
        bool OptimizeMesh      = true;   // aiProcess_JoinIdenticalVertices | aiProcess_OptimizeMeshes
        bool WeldVertices      = false;
        bool MeshCompression   = false;
        bool SwapYZ            = false;  // Maya Y-up → engine Z-up via root transform rotation
        // Materials
        bool ImportMaterials   = true;
        MaterialSearchRule MatSearchRule = MaterialSearchRule::Local;
        // Animation
        bool ImportAnimations  = false;
        // Textures
        std::vector<std::filesystem::path> TextureFiles;  // paths to texture files to import
        // Output
        bool MergeMeshes = false;       // combine all sub-meshes into single mesh
        bool CombineIntoPrefab = true;
    };

}
