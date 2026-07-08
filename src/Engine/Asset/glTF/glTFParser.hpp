#pragma once

#include "Core/UUID.hpp"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Ayaya {

class Scene;
class AssetManager;
struct DirectionalLightComponent;
struct PointLightComponent;
struct SpotLightComponent;

// ==========================================
// glTF Scene Parser — cgltf-backed, dedicated glTF 2.0 fast path.
// Bypasses Assimp for glTF to preserve PBR materials, node hierarchy,
// and KHR_lights_punctual extension data.
// ==========================================

struct glTFImportResult {
    bool Success = false;
    std::string ErrorMsg;
    int NodeCount   = 0;
    int MeshCount   = 0;
    int MaterialCount = 0;
    int TextureCount  = 0;
    int LightCount    = 0;
};

class glTFParser {
public:
    glTFParser()  = default;
    ~glTFParser() = default;

    // Parse a glTF/GLB file into an engine Scene.
    // Called from a background thread (CPU-only: no GPU calls).
    // Returns import statistics; entities/materials are created in the scene.
    glTFImportResult ImportScene(const std::string& filePath, Scene& scene);

private:
    // --- Texture cache (URI → AssetHandle) ---
    std::unordered_map<std::string, UUID> m_TextureCache;
};

} // namespace Ayaya
