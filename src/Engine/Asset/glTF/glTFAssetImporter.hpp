#pragma once

#include "Core/UUID.hpp"
#include <string>
#include <vector>
#include <memory>

namespace Ayaya { class Mesh; }

namespace Ayaya {

class AssetManager;

// ==========================================
// glTF Asset Import — two-phase persistence pipeline.
// Phase 1 (background): I/O + cgltf parse + file copy + .meta write.
// Phase 2 (main thread): s_Registry insert + GPU upload + prefab load.
// ==========================================

struct glTFImportResult {
    bool Success = false;
    std::string ErrorMsg;

    UUID ModelHandle = 0;
    std::string ModelVirtualPath;
    std::string ModelPhysicalPath;

    struct MeshEntry { UUID Handle; std::string VirtualPath; std::string PhysicalPath; int SubMeshIndex = -1; std::string Name; };
    struct MatEntry  { UUID Handle; std::string VirtualPath; std::string PhysicalPath; };
    struct TexEntry  { std::string PhysicalPath; UUID Handle = 0; bool SRGB = true; };

    std::vector<MeshEntry> SubMeshes;
    std::vector<std::shared_ptr<Mesh>> SubMeshData; // GPU buffers created on bg thread
    std::vector<MatEntry>  Materials;
    std::vector<TexEntry>  CopiedTextures;

    UUID PrefabHandle = 0;
    std::string PrefabPath;

    int NodeCount     = 0;
    int MeshCount     = 0;
    int MaterialCount = 0;
    int TextureCount  = 0;
    int LightCount    = 0;
};

// Phase 1 — Background thread safe (CPU-only: file I/O + cgltf parse).
glTFImportResult ImportglTFSceneSync(const std::string& sourcePath);

// Phase 2 — Main thread only (registry insert, GPU uploads via AssetManager, prefab load).
void FinalizeglTFImport(const glTFImportResult& result);

} // namespace Ayaya
