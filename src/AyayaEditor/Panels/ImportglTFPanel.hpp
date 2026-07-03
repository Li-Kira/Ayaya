#pragma once

#include "ImportModelPanel.hpp"   // for ImportState enum
#include <filesystem>
#include <string>
#include <vector>

namespace Ayaya {

// Reuses ImportState enum from ImportModelPanel.hpp

struct glTFFileEntry {
    std::string URI;
    std::string ResolvedPath;
    bool Found = false;
    bool Selected = true;
};

class ImportglTFPanel {
public:
    void RequestOpen(const std::filesystem::path& filePath);
    void Draw();
    bool IsOpen() const { return m_State != ImportState::Hidden; }

private:
    void ExecuteImport();
    void ScanReferencedFiles();          // parse glTF URIs, check existence

    ImportState m_State = ImportState::Hidden;
    bool m_PendingOpen = false;
    std::filesystem::path m_TargetFilePath;

    // Import settings
    bool m_CombineIntoPrefab = true;
    bool m_ImportLights = true;
    bool m_ImportCameras = false;
    bool m_GenerateMipmaps = false;

    // Auto-scanned file list (textures + .bin)
    std::vector<glTFFileEntry> m_ReferencedFiles;

    // Scene statistics (from cgltf parse)
    int m_NodeCount = 0;
    int m_MeshCount = 0;
    int m_MaterialCount = 0;
    int m_TextureCount = 0;
    int m_LightCount = 0;
    bool m_StatsReady = false;
};

} // namespace Ayaya
