#include "ContentBrowserPanel.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/AssetPreviewer.hpp"
#include "Renderer/RendererAPI.hpp"
#include "Utils/PlatformUtils.hpp"
#include "../EditorLayer.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <unordered_set>
#include <algorithm>

namespace Ayaya {

    static bool IsBuildArtifact(const std::filesystem::path& path) {
        static const std::unordered_set<std::string> s_ExcludeNames = {
            "CMakeFiles", "CMakeCache.txt", "Makefile", "cmake_install.cmake",
            "compile_commands.json", "CTestTestfile.cmake", "CPackConfig.cmake",
            "CPackSourceConfig.cmake", "CMakeTmp", "CMakeScripts",
            "Win32", "x64", "Debug", "Release", "packages",
            "AyayaEditor.xcodeproj", "Sandbox.xcodeproj",
            "imgui.ini", "AssetRegistry.yaml", "Temp",
        };
        static const std::unordered_set<std::string> s_ExcludeExtensions = {
            ".a", ".lib", ".dylib", ".so", ".exp", ".pdb", ".ilk",
            ".sln", ".vcxproj", ".vcxproj.filters", ".vcxproj.user",
            ".xcconfig", ".plist", ".dll", ".exe",
        };
        const std::string name = path.filename().string();
        if (s_ExcludeNames.count(name)) return true;
        if (path.has_extension()) {
            std::string ext = path.extension().string();
            if (s_ExcludeExtensions.count(ext)) return true;
        }
        if (std::filesystem::is_directory(path) && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".dir") == 0)
            return true;
        return false;
    }

    std::shared_ptr<Texture2D> ContentBrowserPanel::GetThumbnail(const std::filesystem::path& path) {
        UUID handle = AssetManager::FindHandleForPath(path);
        if (handle == 0) return nullptr;

        AssetMetadata meta = AssetManager::GetMetadata(handle);

        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".bmp") {
            return AssetManager::GetAsset<Texture2D>(handle);
        }

        if (meta.Type == AssetType::Model || meta.Type == AssetType::Prefab ||
            meta.Type == AssetType::Material || meta.Type == AssetType::SubMesh) {
            auto cached = AssetPreviewer::GetCachedThumbnail(handle);
            if (cached) return cached;
            int at = (meta.Type==AssetType::Model||meta.Type==AssetType::SubMesh)?0:(meta.Type==AssetType::Prefab)?1:2;
            AssetPreviewer::RequestThumbnail(handle, at);
            return m_FileIcon;
        }
        return m_FileIcon;
    }

    // =====================================================================
    // Rename helpers
    // =====================================================================
    void ContentBrowserPanel::BeginRename(const std::filesystem::path& path, bool isFolder) {
        m_RenamingPath = path;
        m_IsRenaming = true;
        m_IsRenamingFolder = isFolder;
        std::string stem = path.stem().string();
        std::memcpy(m_RenameBuffer, stem.c_str(), std::min(stem.size(), sizeof(m_RenameBuffer) - 1));
        m_RenameBuffer[std::min(stem.size(), sizeof(m_RenameBuffer) - 1)] = '\0';
    }

    void ContentBrowserPanel::RenderRenameInput(float cellWidth) {
        float scale = ImGui::GetIO().FontGlobalScale;
        float inputWidth = cellWidth - 4.0f * scale;  // stay inside cell
        if (inputWidth < 60.0f * scale) inputWidth = 60.0f * scale;

        // Center the input within the cell
        float cursorX = ImGui::GetCursorPosX();
        float offsetX = (cellWidth - inputWidth) * 0.5f;
        if (offsetX > 0.0f) ImGui::SetCursorPosX(cursorX + offsetX);

        ImGui::SetNextItemWidth(inputWidth);
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##RenameInput", m_RenameBuffer, sizeof(m_RenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            std::string newName(m_RenameBuffer);
            if (!newName.empty() && newName != m_RenamingPath.stem().string()) {
                if (m_IsRenamingFolder) {
                    std::filesystem::path newPath = m_RenamingPath.parent_path() / newName;
                    std::error_code ec;
                    std::filesystem::rename(m_RenamingPath, newPath, ec);
                    if (ec) AYAYA_CORE_ERROR("ContentBrowser: Folder rename failed: {0}", ec.message());
                } else {
                    UUID handle = AssetManager::FindHandleForPath(m_RenamingPath);
                    if (handle != 0) {
                        auto& watcher = EditorLayer::Get().GetAssetWatcher();
                        bool wasPaused = watcher.IsPaused();
                        watcher.SetPaused(true);
                        AssetManager::RenameAsset(handle, newName);
                        if (!wasPaused) watcher.SetPaused(false);
                    }
                }
            }
            m_IsRenaming = false;
            m_RenamingPath.clear();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsItemHovered())) {
            m_IsRenaming = false;
            m_RenamingPath.clear();
        }
    }

    // =====================================================================
    // Item context menu (called from BeginPopupContextItem)
    // =====================================================================
    void ContentBrowserPanel::RenderContextMenuForItem(
            const std::filesystem::path& path, bool isDir,
            UUID assetHandle, const std::string& filenameString) {

        size_t totalSelected = m_SelectedAssets.size() + m_SelectedFolders.size();

        // Batch operations when multiple items are selected
        if (totalSelected > 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Show in Explorer (%zu)", totalSelected);
            if (ImGui::MenuItem(buf)) {
                // Open Explorer at the first selected item's parent directory
                if (!m_SelectedFolders.empty())
                    FileDialogs::OpenInFileExplorer(m_SelectedFolders.begin()->string());
                else {
                    auto firstHandle = *m_SelectedAssets.begin();
                    auto meta = AssetManager::GetMetadata(firstHandle);
                    std::string phys = VFS::ResolveString(meta.VirtualPath);
                    FileDialogs::ShowInFileExplorer(phys);
                }
            }
            snprintf(buf, sizeof(buf), "Delete Selected (%zu)", totalSelected);
            if (ImGui::MenuItem(buf)) {
                std::vector<UUID> toDelete(m_SelectedAssets.begin(), m_SelectedAssets.end());
                std::vector<std::filesystem::path> toRemoveDirs(m_SelectedFolders.begin(), m_SelectedFolders.end());
                m_SelectedAssets.clear();
                m_SelectedFolders.clear();

                auto& w = EditorLayer::Get().GetAssetWatcher();
                bool wasPaused = w.IsPaused(); w.SetPaused(true);
                for (auto& h : toDelete) {
                    if (AssetManager::IsAssetHandleValid(h))
                        AssetManager::DeleteAsset(h);
                }
                for (auto& p : toRemoveDirs) {
                    std::error_code ec;
                    std::filesystem::remove_all(p, ec);
                }
                if (!wasPaused) w.SetPaused(false);
                return;
            }
            ImGui::Separator();
        }

        if (!isDir && assetHandle != 0) {
            // --- File context menu ---
            AssetMetadata meta = AssetManager::GetMetadata(assetHandle);
            std::string label = (meta.Type == AssetType::Scene) ? "Open Scene" : "Open";
            if (ImGui::MenuItem(label.c_str())) {
                if (meta.Type == AssetType::Scene) {
                    std::string phys = VFS::ResolveString(meta.VirtualPath);
                    EditorLayer::Get().OpenSceneFile(phys);
                } else {
                    EditorLayer::Get().GetSceneHierarchyPanel().GetPropertiesPanel().SetSelectedAsset(assetHandle);
                }
            }
            if (ImGui::MenuItem("Rename")) {
                BeginRename(path, false);
            }
            if (ImGui::MenuItem("Show in Explorer")) {
                FileDialogs::ShowInFileExplorer(path.string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Move To...")) {
                std::string dest = FileDialogs::OpenFolder();
                if (!dest.empty()) {
                    auto& watcher = EditorLayer::Get().GetAssetWatcher();
                    bool wasPaused = watcher.IsPaused();
                    watcher.SetPaused(true);
                    AssetManager::MoveAsset(assetHandle, dest);
                    if (!wasPaused) watcher.SetPaused(false);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                m_PendingDeleteHandle = assetHandle;
                m_PendingDeletePath = path;
                m_ShowDeleteConfirm = true;
            }
        }
        else if (isDir) {
            // --- Folder context menu ---
            if (ImGui::MenuItem("Open")) {
                m_CurrentDirectory = path;
            }
            if (ImGui::MenuItem("Rename")) {
                BeginRename(path, true);
            }
            if (ImGui::MenuItem("Show in Explorer")) {
                FileDialogs::OpenInFileExplorer(path.string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Folder")) {
                AssetManager::CreateFolder(path, "New Folder");
            }
            if (ImGui::MenuItem("New Scene")) {
                AssetManager::CreateSceneAsset(path, "New Scene");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete")) {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
                if (ec) AYAYA_CORE_ERROR("ContentBrowser: Folder delete failed: {0}", ec.message());
            }
        }
    }

    void ContentBrowserPanel::RenderDeleteConfirmModal() {
        if (!m_ShowDeleteConfirm) return;

        ImGui::OpenPopup("Delete Asset");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Delete Asset", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("Are you sure you want to delete '%s'?", m_PendingDeletePath.filename().string().c_str());

            // Check dependents
            auto& deps = AssetManager::GetDependents(m_PendingDeleteHandle);
            if (!deps.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                                   "Warning: %zu asset(s) depend on this file.", deps.size());
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                   "They may break after deletion.");
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            float btnWidth = 80.0f;
            if (ImGui::Button("Delete", ImVec2(btnWidth, 0))) {
                auto& watcher = EditorLayer::Get().GetAssetWatcher();
                bool wasPaused = watcher.IsPaused();
                watcher.SetPaused(true);
                AssetManager::DeleteAsset(m_PendingDeleteHandle);
                if (!wasPaused) watcher.SetPaused(false);
                m_ShowDeleteConfirm = false;
                m_PendingDeleteHandle = 0;
                m_PendingDeletePath.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnWidth, 0))) {
                m_ShowDeleteConfirm = false;
                m_PendingDeleteHandle = 0;
                m_PendingDeletePath.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // =====================================================================
    // Main render
    // =====================================================================
    ContentBrowserPanel::ContentBrowserPanel() {
        m_DirectoryIcon = Texture2D::Create("assets/Editor/icons/folder_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_FileIcon      = Texture2D::Create("assets/Editor/icons/docs_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_PngIcon       = Texture2D::Create("assets/Editor/icons/image_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");

        m_BaseDirectory = Project::GetProjectDirectory();
        m_CurrentDirectory = m_BaseDirectory;
    }

    void ContentBrowserPanel::OnImGuiRender() {
        float scale = ImGui::GetIO().FontGlobalScale;
        ImGui::Begin("Content Browser");

        std::filesystem::path activeAssetDir = Project::GetProjectDirectory();
        if (m_BaseDirectory != activeAssetDir) {
            m_BaseDirectory = activeAssetDir;
            m_CurrentDirectory = m_BaseDirectory;
        }

        // ==========================================
        // Top bar (breadcrumb navigation + search)
        // ==========================================
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        // Lambda: add drop target on breadcrumb that moves assets/folders to targetDir
        auto breadcrumbDropTarget = [&](const std::filesystem::path& targetDir) {
            if (ImGui::BeginDragDropTarget()) {
                // Batch: move all selected assets
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_BATCH")) {
                    auto& w = EditorLayer::Get().GetAssetWatcher();
                    bool wasPaused = w.IsPaused(); w.SetPaused(true);
                    uint32_t count = *(const uint32_t*)p->Data;
                    const UUID* handles = (const UUID*)((const char*)p->Data + sizeof(uint32_t));
                    for (uint32_t i = 0; i < count; i++)
                        AssetManager::MoveAsset(handles[i], targetDir);
                    if (!wasPaused) w.SetPaused(false);
                    m_SelectedAssets.clear();
                    m_SelectedFolders.clear();
                }
                else if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    UUID h = *(const UUID*)p->Data;
                    if (h != 0) {
                        auto& w = EditorLayer::Get().GetAssetWatcher();
                        bool wasPaused = w.IsPaused(); w.SetPaused(true);
                        AssetManager::MoveAsset(h, targetDir);
                        if (!wasPaused) w.SetPaused(false);
                    }
                }
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PATH")) {
                    std::string src((const char*)p->Data, p->DataSize - 1);
                    if (std::filesystem::is_directory(src) && src != targetDir.string()) {
                        auto dest = targetDir / std::filesystem::path(src).filename();
                        if (!std::filesystem::exists(dest))
                            std::filesystem::rename(src, dest);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        };

        float btnSize = 24.0f * scale;
        if (m_CurrentDirectory != m_BaseDirectory) {
            if (ImGui::Button("<", ImVec2(btnSize, btnSize)))
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
            ImGui::SameLine();
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("<", ImVec2(btnSize, btnSize));
            ImGui::EndDisabled();
            ImGui::SameLine();
        }

        std::string relPathStr = std::filesystem::relative(m_CurrentDirectory, m_BaseDirectory).string();
        if (relPathStr == "." || relPathStr == "") {
            ImGui::Button(m_BaseDirectory.filename().string().c_str());
            breadcrumbDropTarget(m_BaseDirectory);
        } else {
            if (ImGui::Button(m_BaseDirectory.filename().string().c_str()))
                m_CurrentDirectory = m_BaseDirectory;
            breadcrumbDropTarget(m_BaseDirectory);
            std::filesystem::path buildPath = m_BaseDirectory;
            auto relParts = std::filesystem::relative(m_CurrentDirectory, m_BaseDirectory);
            for (const auto& part : relParts) {
                if (part == "") continue;
                ImGui::SameLine(0, 4.0f * scale); ImGui::Text(">"); ImGui::SameLine(0, 4.0f * scale);
                buildPath /= part;
                if (ImGui::Button(part.string().c_str())) {
                    m_CurrentDirectory = buildPath;
                    break;
                }
                breadcrumbDropTarget(buildPath);
            }
        }
        ImGui::PopStyleColor();

        float searchWidth = 200.0f * scale;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - searchWidth);
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##Search", "Search...", m_SearchFilter, sizeof(m_SearchFilter));

        ImGui::Separator();

        // ==========================================
        // Main content area
        // ==========================================
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -32.0f * scale), false);

        float padding = 16.0f * scale;
        float cellSize = m_ThumbnailSize * scale + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        std::string searchStr = m_SearchFilter;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        // Reset hit-test state
        m_HoveredPath.clear();
        m_HoveredIsDir = false;
        m_HoveredHandle = 0;

        // Click empty space → deselect all
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && !ImGui::IsAnyItemHovered()) {
            m_SelectedAssets.clear();
            m_SelectedFolders.clear();
        }

        // Global rename shortcut: F2 (Windows), Enter (macOS) — acts on last selected item
        if (!m_IsRenaming && ImGui::IsWindowFocused()) {
#ifdef _WIN32
            bool renameKey = ImGui::IsKeyPressed(ImGuiKey_F2);
#else
            bool renameKey = ImGui::IsKeyPressed(ImGuiKey_Enter)
                && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt;
#endif
            if (renameKey) {
                size_t totalSel = m_SelectedAssets.size() + m_SelectedFolders.size();
                if (totalSel == 1) {
                    if (!m_SelectedFolders.empty()) {
                        BeginRename(*m_SelectedFolders.begin(), true);
                    } else if (!m_SelectedAssets.empty()) {
                        auto meta = AssetManager::GetMetadata(*m_SelectedAssets.begin());
                        std::string phys = VFS::ResolveString(meta.VirtualPath);
                        BeginRename(phys, false);
                    }
                }
            }
        }

        // Ctrl+A → select all visible items (handled in the table loop via visibleItems vector)
        bool selectAll = ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl
                         && ImGui::IsKeyPressed(ImGuiKey_A);

        // Empty-space right-click: use BeginPopupContextWindow on the child region
        if (ImGui::BeginPopupContextWindow("##EmptySpaceCtx", ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Open in Explorer")) {
                FileDialogs::OpenInFileExplorer(m_CurrentDirectory.string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Folder")) {
                AssetManager::CreateFolder(m_CurrentDirectory, "New Folder");
            }
            if (ImGui::MenuItem("New Scene")) {
                AssetManager::CreateSceneAsset(m_CurrentDirectory, "New Scene");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import Asset...")) {
                std::string filepath = FileDialogs::OpenFile(
                    "Supported Assets (*.png *.jpg *.jpeg *.bmp *.hdr *.obj *.fbx *.gltf *.glb *.mat *.lua *.cube *.prefab *.ayaya)");
                if (!filepath.empty()) {
                    std::string ext = std::filesystem::path(filepath).extension().string();
                    for (auto& c : ext) c = (char)std::tolower(c);
                    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb") {
                        EditorLayer::Get().GetImportModelPanel().RequestOpen(filepath);
                    } else {
                        auto destDir = m_CurrentDirectory;
                        std::filesystem::path src(filepath);
                        if (!std::filesystem::equivalent(src.parent_path(), destDir)) {
                            auto destPath = destDir / src.filename();
                            if (!std::filesystem::exists(destPath)) {
                                std::filesystem::copy_file(src, destPath);
                                AssetManager::ImportAsset(destPath);
                            }
                        } else {
                            AssetManager::ImportAsset(filepath);
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        // VisibleItem: pre-computed for range-select and Ctrl+A
        struct VisibleItem {
            std::filesystem::path path;
            std::string filename;
            bool isDir;
            UUID handle;
        };
        std::vector<VisibleItem> visibleItems;

        // Pre-scan: collect all visible items so range-select sees the full list
        {
            std::vector<std::filesystem::directory_entry> sortedEntries;
            for (auto& e : std::filesystem::directory_iterator(m_CurrentDirectory))
                sortedEntries.push_back(e);
            std::sort(sortedEntries.begin(), sortedEntries.end(), [](auto& a, auto& b) {
                bool aDir = a.is_directory(), bDir = b.is_directory();
                if (aDir != bDir) return aDir;
                std::string aExt = a.path().extension().string();
                std::string bExt = b.path().extension().string();
                if (aExt != bExt) return aExt < bExt;
                return a.path().filename() < b.path().filename();
            });

            for (auto& de : sortedEntries) {
                const auto& p = de.path();
                std::string fn = p.filename().string();
                if (fn.empty() || fn[0] == '.') continue;
                if (p.extension() == ".yaml" || p.extension() == ".meta") continue;
                if (IsBuildArtifact(p)) continue;
                if (!searchStr.empty()) {
                    std::string lower = fn;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (lower.find(searchStr) == std::string::npos) continue;
                }
                bool isD = de.is_directory();
                UUID h = 0;
                if (!isD) {
                    if (!std::filesystem::exists(p.string() + ".meta")) continue;
                    h = AssetManager::FindHandleForPath(p);
                }
                visibleItems.push_back({p, fn, isD, h});
            }
        }

        // Ctrl+A: select all
        if (selectAll) {
            m_SelectedAssets.clear();
            m_SelectedFolders.clear();
            for (auto& vi : visibleItems) {
                if (vi.isDir) m_SelectedFolders.insert(vi.path);
                else if (vi.handle != 0) m_SelectedAssets.insert(vi.handle);
            }
        }

        if (ImGui::BeginTable("ContentBrowserTable", columnCount, ImGuiTableFlags_SizingFixedFit)) {
            for (auto& vi : visibleItems) {
                const auto& path = vi.path;
                std::string filenameString = vi.filename;
                bool isDir = vi.isDir;
                UUID assetHandle = vi.handle;

                // Selection state for this item
                bool isSelected = isDir ? m_SelectedFolders.count(path) != 0
                                        : (assetHandle != 0 && m_SelectedAssets.count(assetHandle) != 0);

                ImGui::TableNextColumn();

                std::shared_ptr<Texture2D> icon = isDir ? m_DirectoryIcon : m_FileIcon;
                if (!isDir) {
                    auto thumb = GetThumbnail(path);
                    icon = thumb ? thumb : m_FileIcon;
                }

                ImGui::PushID(filenameString.c_str());

                float thumbSize = m_ThumbnailSize * scale;
                ImVec2 cursorPos = ImGui::GetCursorPos();
                float textHeight = ImGui::GetTextLineHeight() * 2.0f;
                float itemHeight = 4.0f * scale + thumbSize + 8.0f * scale + textHeight;
                ImVec2 itemSize(cellSize, itemHeight);

                ImGui::SetCursorPos(cursorPos);
                ImGui::InvisibleButton("##ItemHitbox", itemSize);

                bool hovered = ImGui::IsItemHovered();
                // macOS GLFW: modifier key polling unreliable. Combine all detection paths.
                bool modCtrl  = ImGui::IsKeyDown(ImGuiKey_LeftCtrl)
                             || ImGui::IsKeyDown(ImGuiKey_RightCtrl)
                             || ImGui::GetIO().KeyCtrl;
                bool modShift = ImGui::IsKeyDown(ImGuiKey_LeftShift)
                             || ImGui::IsKeyDown(ImGuiKey_RightShift)
                             || ImGui::GetIO().KeyShift;
                bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                                     && !modCtrl && !modShift;
                bool clicked = hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
                               && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left);

                // --- Multi-select click handling ---
                if (clicked) {
                    if (modCtrl) {
                        // Toggle
                        if (isDir) {
                            if (isSelected) m_SelectedFolders.erase(path);
                            else m_SelectedFolders.insert(path);
                        } else if (assetHandle != 0) {
                            if (isSelected) m_SelectedAssets.erase(assetHandle);
                            else m_SelectedAssets.insert(assetHandle);
                        }
                        m_LastClickedAsset = isDir ? UUID(0) : assetHandle;
                        m_LastClickedFolder = isDir ? path : std::filesystem::path();
                    } else if (modShift && (m_LastClickedAsset != 0 || !m_LastClickedFolder.empty())) {
                        // Range select — find both items in visibleItems by UUID/path
                        int idxLast = -1, idxCur = -1;
                        for (int i = 0; i < (int)visibleItems.size(); i++) {
                            auto& vi = visibleItems[i];
                            if (m_LastClickedAsset != 0 && !vi.isDir && vi.handle == m_LastClickedAsset)
                                idxLast = i;
                            if (!m_LastClickedFolder.empty() && vi.isDir && vi.path == m_LastClickedFolder)
                                idxLast = i;
                            if (!isDir && assetHandle != 0 && !vi.isDir && vi.handle == assetHandle)
                                idxCur = i;
                            if (isDir && vi.isDir && vi.path == path)
                                idxCur = i;
                        }
                        if (idxLast >= 0 && idxCur >= 0) {
                            m_SelectedAssets.clear();
                            m_SelectedFolders.clear();
                            int lo = std::min(idxLast, idxCur);
                            int hi = std::max(idxLast, idxCur);
                            for (int i = lo; i <= hi; i++) {
                                auto& vi = visibleItems[i];
                                if (vi.isDir) m_SelectedFolders.insert(vi.path);
                                else if (vi.handle != 0) m_SelectedAssets.insert(vi.handle);
                            }
                        }
                    } else {
                        // Single click — clear, select this one
                        m_SelectedAssets.clear();
                        m_SelectedFolders.clear();
                        if (isDir) m_SelectedFolders.insert(path);
                        else if (assetHandle != 0) m_SelectedAssets.insert(assetHandle);
                        m_LastClickedAsset = isDir ? UUID(0) : assetHandle;
                        m_LastClickedFolder = isDir ? path : std::filesystem::path();
                    }
                }

                // Record hover state
                if (hovered) {
                    m_HoveredPath = path;
                    m_HoveredIsDir = isDir;
                    m_HoveredHandle = assetHandle;

                    ImU32 highlightColor = isSelected
                        ? IM_COL32(60, 100, 180, 180)   // selected: blue
                        : IM_COL32(80, 80, 80, 150);     // hovered: gray
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        highlightColor, 6.0f * scale);
                    ImGui::SetTooltip("%s", filenameString.c_str());
                } else if (isSelected) {
                    // Show selected highlight even when not hovered
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                        IM_COL32(50, 80, 150, 120), 6.0f * scale);
                }

                // === Right-click context menu (canonical ImGui pattern) ===
                if (ImGui::BeginPopupContextItem("##ItemCtx")) {
                    RenderContextMenuForItem(path, isDir, assetHandle, filenameString);
                    ImGui::EndPopup();
                }


                // Drag source — UUID payload for assets (used by SceneHierarchy),
                // path payload for folders (used by folder-to-folder move).
                // Batch payload when dragging a multi-selected item.
                if (ImGui::BeginDragDropSource()) {
                    size_t totalSel = m_SelectedAssets.size() + m_SelectedFolders.size();
                    if (!isDir && assetHandle != 0) {
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", &assetHandle, sizeof(UUID));
                        // Batch: when dragging a selected item in a multi-selection,
                        // also send all selected UUIDs for bulk move.
                        if (isSelected && totalSel > 1) {
                            size_t count = m_SelectedAssets.size();
                            size_t bufSize = sizeof(uint32_t) + count * sizeof(UUID);
                            std::vector<char> buf(bufSize);
                            *(uint32_t*)buf.data() = (uint32_t)count;
                            size_t off = sizeof(uint32_t);
                            for (auto& h : m_SelectedAssets) {
                                memcpy(buf.data() + off, &h, sizeof(UUID));
                                off += sizeof(UUID);
                            }
                            ImGui::SetDragDropPayload("CONTENT_BROWSER_BATCH", buf.data(), bufSize);
                        }
                    }
                    if (isDir) {
                        std::string pathStr = path.string();
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_PATH", pathStr.c_str(), pathStr.size() + 1);
                    }
                    if (icon) {
                        ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                        ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                        ImGui::Image((ImTextureID)icon->GetImGuiTextureID(), {32.0f*scale, 32.0f*scale}, uv0, uv1);
                    }
                    if (totalSel > 1 && isSelected) {
                        ImGui::Text("%zu items", totalSel);
                    } else {
                        ImGui::Text("%s", filenameString.c_str());
                    }
                    ImGui::EndDragDropSource();
                }

                // Drag target — folders accept asset UUIDs, batches, and folder paths
                if (isDir && ImGui::BeginDragDropTarget()) {
                    // Batch: move all selected assets at once
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_BATCH")) {
                        auto& watcher = EditorLayer::Get().GetAssetWatcher();
                        bool wasPaused = watcher.IsPaused(); watcher.SetPaused(true);
                        uint32_t count = *(const uint32_t*)payload->Data;
                        const UUID* handles = (const UUID*)((const char*)payload->Data + sizeof(uint32_t));
                        for (uint32_t i = 0; i < count; i++)
                            AssetManager::MoveAsset(handles[i], path);
                        if (!wasPaused) watcher.SetPaused(false);
                        m_SelectedAssets.clear();
                        m_SelectedFolders.clear();
                    }
                    // Single asset UUID
                    else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        UUID draggedHandle = *(const UUID*)payload->Data;
                        if (draggedHandle != 0) {
                            auto& watcher = EditorLayer::Get().GetAssetWatcher();
                            bool wasPaused = watcher.IsPaused();
                            watcher.SetPaused(true);
                            AssetManager::MoveAsset(draggedHandle, path);
                            if (!wasPaused) watcher.SetPaused(false);
                        }
                    }
                    // Accept folder path
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_PATH")) {
                        std::string srcPath((const char*)payload->Data, payload->DataSize - 1);
                        if (!srcPath.empty() && std::filesystem::is_directory(srcPath) &&
                            srcPath != path.string() &&
                            srcPath != path.parent_path().string()) {
                            std::error_code ec;
                            auto dest = path / std::filesystem::path(srcPath).filename();
                            if (!std::filesystem::exists(dest)) {
                                std::filesystem::rename(srcPath, dest, ec);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // Double-click directory → navigate
                if (doubleClicked && isDir) {
                    m_CurrentDirectory /= path.filename();
                }

                // Double-click scene file → open
                if (doubleClicked && !isDir && assetHandle != 0) {
                    AssetMetadata meta = AssetManager::GetMetadata(assetHandle);
                    if (meta.Type == AssetType::Scene) {
                        std::string phys = VFS::ResolveString(meta.VirtualPath);
                        EditorLayer::Get().OpenSceneFile(phys);
                    }
                }

                // Single-click file → select in properties (skip during Ctrl/Shift multi-select)
                if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !isDir && assetHandle != 0) {
                    if (!ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)
                        && !modCtrl && !modShift) {
                        EditorLayer::Get().GetSceneHierarchyPanel().GetPropertiesPanel().SetSelectedAsset(assetHandle);
                    }
                }

                // Draw icon
                float imageOffsetX = (cellSize - thumbSize) * 0.5f;
                ImGui::SetCursorPos(ImVec2(cursorPos.x + imageOffsetX, cursorPos.y + 4.0f * scale));
                if (icon) {
                    bool flip = icon->IsDataFlipped();
                    ImVec2 uv0 = flip ? ImVec2(0, 1) : ImVec2(0, 0);
                    ImVec2 uv1 = flip ? ImVec2(1, 0) : ImVec2(1, 1);
                    ImGui::Image((ImTextureID)icon->GetImGuiTextureID(), {thumbSize, thumbSize}, uv0, uv1);
                }

                // Draw filename text
                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + 4.0f * scale + thumbSize + 8.0f * scale));

                // Inline rename?
                bool renamingThis = m_IsRenaming && (m_RenamingPath == path);
                if (renamingThis) {
                    RenderRenameInput(cellSize);
                } else {
                    ImFont* font = ImGui::GetFont();
                    float fontScale = ImGui::GetIO().FontGlobalScale;
                    const char* text = filenameString.c_str();
                    const char* text_end = text + filenameString.length();
                    int lineCount = 0;
                    const int maxLines = 2;

                    while (text < text_end && lineCount < maxLines) {
                        const char* line_end = font->CalcWordWrapPositionA(fontScale, text, text_end, cellSize);
                        if (line_end == text) line_end++;
                        std::string line(text, line_end);
                        if (lineCount == maxLines - 1 && line_end < text_end) line += "...";
                        float lineWidth = ImGui::CalcTextSize(line.c_str()).x;
                        float offsetX = (cellSize - lineWidth) * 0.5f;
                        ImGui::SetCursorPosX(cursorPos.x + std::max(0.0f, offsetX));
                        ImGui::TextUnformatted(line.c_str());
                        text = line_end;
                        while (text < text_end && (*text == ' ' || *text == '\n')) text++;
                        lineCount++;
                    }
                }

                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + itemHeight));
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        // ==========================================
        // Delete confirmation modal
        // ==========================================
        RenderDeleteConfirmModal();

        // ==========================================
        // Bottom bar: thumbnail size slider
        // ==========================================
        float sliderWidth = 120.0f * scale;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - sliderWidth - 10.0f * scale);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##IconSize", &m_ThumbnailSize, 32.0f, 256.0f, "");
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);

        ImGui::End();
    }
}
