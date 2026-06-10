#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>

#include "Renderer/Texture.hpp"
#include "Core/UUID.hpp"

namespace Ayaya {

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();

        void OnImGuiRender();

    private:
        std::shared_ptr<Texture2D> GetThumbnail(const std::filesystem::path& path);

        // Context menu / interaction state
        void RenderContextMenuForItem(const std::filesystem::path& path, bool isDir, UUID handle, const std::string& name);
        void RenderDeleteConfirmModal();
        void RenderRenameInput(const std::filesystem::path& parentDir);
        void BeginRename(const std::filesystem::path& path, bool isFolder);

        std::filesystem::path m_BaseDirectory;
        std::filesystem::path m_CurrentDirectory;

        std::shared_ptr<Texture2D> m_DirectoryIcon;
        std::shared_ptr<Texture2D> m_FileIcon;
        std::shared_ptr<Texture2D> m_PngIcon;

        float m_ThumbnailSize = 96.0f;
        char m_SearchFilter[256] = "";

        // Hit-testing per frame
        std::filesystem::path m_HoveredPath;
        bool m_HoveredIsDir = false;
        UUID m_HoveredHandle = 0;

        // Inline rename
        std::filesystem::path m_RenamingPath;
        char m_RenameBuffer[256] = "";
        bool m_IsRenaming = false;
        bool m_IsRenamingFolder = false;

        // Multi-select
        std::unordered_set<UUID> m_SelectedAssets;
        std::unordered_set<std::filesystem::path> m_SelectedFolders;
        UUID m_LastClickedAsset = 0;
        std::filesystem::path m_LastClickedFolder;

        // Delete confirmation
        bool m_ShowDeleteConfirm = false;
        UUID m_PendingDeleteHandle = 0;
        std::filesystem::path m_PendingDeletePath;
    };

}
