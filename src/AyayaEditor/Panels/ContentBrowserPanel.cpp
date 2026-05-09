#include "ContentBrowserPanel.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"

#include <imgui.h>
#include <unordered_set>

namespace Ayaya {

    static bool IsBuildArtifact(const std::filesystem::path& path) {
        static const std::unordered_set<std::string> s_ExcludeNames = {
            "CMakeFiles", "CMakeCache.txt", "Makefile", "cmake_install.cmake",
            "compile_commands.json", "CTestTestfile.cmake", "CPackConfig.cmake",
            "CPackSourceConfig.cmake", "CMakeTmp", "CMakeScripts",
            "Win32", "x64", "Debug", "Release", "packages",
            "AyayaEditor.xcodeproj", "Sandbox.xcodeproj",
            "imgui.ini", "AssetRegistry.yaml",
        };
        static const std::unordered_set<std::string> s_ExcludeExtensions = {
            ".a", ".lib", ".dylib", ".so", ".exp", ".pdb", ".ilk",
            ".sln", ".vcxproj", ".vcxproj.filters", ".vcxproj.user",
            ".xcconfig", ".plist",
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
        std::string key = path.string();
        auto it = m_ThumbnailCache.find(key);
        if (it != m_ThumbnailCache.end()) return it->second;

        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".hdr" && ext != ".bmp")
            return nullptr;

        if (m_ThumbnailCache.size() >= kMaxThumbnailCache)
            m_ThumbnailCache.erase(m_ThumbnailCache.begin());

        auto thumbnail = Texture2D::Create(path.string());
        if (!thumbnail) return nullptr;

        m_ThumbnailCache[key] = thumbnail;
        return thumbnail;
    }

    ContentBrowserPanel::ContentBrowserPanel() {
        m_DirectoryIcon = Texture2D::Create("assets/Editor/icons/folder_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_FileIcon      = Texture2D::Create("assets/Editor/icons/docs_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_PngIcon       = Texture2D::Create("assets/Editor/icons/image_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");

        m_BaseDirectory = Project::GetProjectDirectory();
        m_CurrentDirectory = m_BaseDirectory;
    }

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Content Browser");

        std::filesystem::path activeAssetDir = Project::GetProjectDirectory();
        if (m_BaseDirectory != activeAssetDir) {
            m_BaseDirectory = activeAssetDir;
            m_CurrentDirectory = m_BaseDirectory;
        }

        if (m_CurrentDirectory != m_BaseDirectory) {
            if (ImGui::Button("<- Back")) {
                m_CurrentDirectory = m_CurrentDirectory.parent_path();
            }
            ImGui::Separator();
        }

        float bottomBarHeight = 32.0f;
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -bottomBarHeight), false);

        float padding = 16.0f;
        float cellSize = m_ThumbnailSize + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        ImGui::Columns(columnCount, 0, false);

        for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
            const auto& path = directoryEntry.path();
            std::string filenameString = path.filename().string();

            if (filenameString.empty() || filenameString[0] == '.') continue;
            if (path.extension() == ".yaml") continue;
            if (IsBuildArtifact(path)) continue;

            // 图标：目录/图片/普通文件
            std::shared_ptr<Texture2D> icon = m_FileIcon;
            if (directoryEntry.is_directory()) {
                icon = m_DirectoryIcon;
            } else if (path.extension() == ".png" || path.extension() == ".jpg") {
                auto thumb = GetThumbnail(path);
                icon = thumb ? thumb : m_PngIcon;
            }

            ImGui::PushID(filenameString.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

            if (icon) {
                ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                ImGui::ImageButton(filenameString.c_str(), (ImTextureID)icon->GetImGuiTextureID(),
                                   { m_ThumbnailSize, m_ThumbnailSize }, uv0, uv1);
            } else {
                ImGui::Button(directoryEntry.is_directory() ? "[DIR]" : "[FILE]",
                              { m_ThumbnailSize, m_ThumbnailSize });
            }

            // 悬停显示完整文件名
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", filenameString.c_str());

            if (ImGui::BeginDragDropSource()) {
                std::string vfsPath = VFS::GetVirtualPath(path);
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", vfsPath.c_str(), vfsPath.size() + 1);
                if (icon) {
                    ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                    ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                    ImGui::Image((ImTextureID)icon->GetImGuiTextureID(), { 32.0f, 32.0f }, uv0, uv1);
                }
                ImGui::Text("%s", filenameString.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (directoryEntry.is_directory()) {
                    m_CurrentDirectory /= path.filename();
                }
            }

            // 文件名居中显示
            float cursorPosX = ImGui::GetCursorPosX();
            ImFont* font = ImGui::GetFont();
            float fontScale = ImGui::GetIO().FontGlobalScale;
            const char* text = filenameString.c_str();
            const char* text_end = text + filenameString.length();
            float wrapWidth = m_ThumbnailSize;
            int lineCount = 0;
            const int maxLines = 2;

            while (text < text_end && lineCount < maxLines) {
                const char* line_end = font->CalcWordWrapPositionA(fontScale, text, text_end, wrapWidth);
                if (line_end == text) line_end++;

                std::string line(text, line_end);
                if (lineCount == maxLines - 1 && line_end < text_end) {
                    line += "...";
                }

                float lineWidth = ImGui::CalcTextSize(line.c_str()).x;
                float offset = (wrapWidth - lineWidth) * 0.5f;
                if (offset > 0.0f) {
                    ImGui::SetCursorPosX(cursorPosX + offset);
                } else {
                    ImGui::SetCursorPosX(cursorPosX);
                }

                ImGui::TextUnformatted(line.c_str());

                text = line_end;
                while (text < text_end && (*text == ' ' || *text == '\n')) text++;
                lineCount++;
            }

            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        // 缩略图大小滑杆
        float sliderWidth = 120.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - sliderWidth - 10.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
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
