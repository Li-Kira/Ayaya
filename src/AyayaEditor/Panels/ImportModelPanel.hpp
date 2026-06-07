#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "Asset/AssetSettings.hpp"

namespace Ayaya {

    enum class ImportState { Hidden, Configuring };

    struct TextureImportEntry {
        std::filesystem::path SourcePath;
        std::string Label;       // display name in the UI (e.g., "Albedo", "Normal")
        bool Selected = true;
    };

    class ImportModelPanel {
    public:
        void RequestOpen(const std::filesystem::path& filePath);
        void Draw();  // called every frame from EditorLayer::OnImGuiRender
        bool IsOpen() const { return m_State != ImportState::Hidden; }

    private:
        void ExecuteImport();
        void ScanTextures();
        void AddTextureManual();

        ImportState m_State = ImportState::Hidden;
        bool m_PendingOpen = false;
        std::filesystem::path m_TargetFilePath;
        ModelImportSettings m_Settings;
        std::vector<TextureImportEntry> m_TextureEntries;
    };

}
