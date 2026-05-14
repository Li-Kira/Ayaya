#include "ContentBrowserPanel.hpp"
#include "Project/Project.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"
#include "Asset/AssetManager.hpp"

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
        std::string key = path.string();
        auto it = m_ThumbnailCache.find(key);
        if (it != m_ThumbnailCache.end()) return it->second;

        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".hdr" && ext != ".bmp")
            return nullptr;

        // 优先从资产缓存获取（场景渲染时可能已加载过）
        UUID handle = AssetManager::FindHandleForPath(path);
        std::shared_ptr<Texture2D> thumbnail = nullptr;
        if (handle != 0) {
            thumbnail = AssetManager::GetAsset<Texture2D>(handle);
        }
        // 缓存未命中才同步创建
        if (!thumbnail) {
            thumbnail = Texture2D::Create(path.string());
        }
        if (!thumbnail) return nullptr;

        if (m_ThumbnailCache.size() >= kMaxThumbnailCache)
            m_ThumbnailCache.erase(m_ThumbnailCache.begin());
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
        float scale = ImGui::GetIO().FontGlobalScale;
        ImGui::Begin("Content Browser");

        std::filesystem::path activeAssetDir = Project::GetProjectDirectory();
        if (m_BaseDirectory != activeAssetDir) {
            m_BaseDirectory = activeAssetDir;
            m_CurrentDirectory = m_BaseDirectory;
        }

        // ==========================================
        // 顶部控制栏 (面包屑导航 + 搜索框)
        // ==========================================
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

        float btnSize = 24.0f * scale;
        if (m_CurrentDirectory != m_BaseDirectory) {
            if (ImGui::Button("<", ImVec2(btnSize, btnSize))) m_CurrentDirectory = m_CurrentDirectory.parent_path();
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
        } else {
            if (ImGui::Button(m_BaseDirectory.filename().string().c_str()))
                m_CurrentDirectory = m_BaseDirectory;
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
            }
        }
        ImGui::PopStyleColor();

        // 搜索框
        float searchWidth = 200.0f * scale;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - searchWidth);
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##Search", "Search...", m_SearchFilter, sizeof(m_SearchFilter));

        ImGui::Separator();

        // ==========================================
        // 主内容区
        // ==========================================
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -32.0f * scale), false);

        float padding = 16.0f * scale;
        float cellSize = m_ThumbnailSize * scale + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1) columnCount = 1;

        std::string searchStr = m_SearchFilter;
        std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

        if (ImGui::BeginTable("ContentBrowserTable", columnCount, ImGuiTableFlags_SizingFixedFit)) {
            for (auto& directoryEntry : std::filesystem::directory_iterator(m_CurrentDirectory)) {
                const auto& path = directoryEntry.path();
                std::string filenameString = path.filename().string();

                if (filenameString.empty() || filenameString[0] == '.') continue;
                if (path.extension() == ".yaml") continue;
                if (path.extension() == ".meta") continue;
                if (IsBuildArtifact(path)) continue;

                if (!searchStr.empty()) {
                    std::string lower = filenameString;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (lower.find(searchStr) == std::string::npos) continue;
                }

                // ==========================================
                // .meta-aware: 只显示已导入的资产（在 s_Registry 中能找到的项）
                // ==========================================
                bool isImage = false;
                UUID assetHandle = 0;
                if (!directoryEntry.is_directory()) {
                    // 检查该文件是否已在注册表中（通过查找 .meta 文件）
                    if (!std::filesystem::exists(path.string() + ".meta")) {
                        continue; // 未导入，屏蔽不显示
                    }

                    std::string ext = path.extension().string();
                    for (auto& c : ext) c = (char)std::tolower(c);
                    isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" || ext == ".bmp");

                    assetHandle = AssetManager::FindHandleForPath(path);
                }

                ImGui::TableNextColumn();

                std::shared_ptr<Texture2D> icon = m_FileIcon;
                if (directoryEntry.is_directory()) {
                    icon = m_DirectoryIcon;
                } else if (isImage) {
                    auto thumb = GetThumbnail(path);
                    icon = thumb ? thumb : m_PngIcon;
                }

                ImGui::PushID(filenameString.c_str());

                // ==========================================
                // 【核心修复】：基于隐形按钮的全单元格交互
                // ==========================================
                float thumbSize = m_ThumbnailSize * scale;
                ImVec2 cursorPos = ImGui::GetCursorPos();

                // 预留两行文字高度 + 图标高度 + 间距，计算出卡片整体高度
                float textHeight = ImGui::GetTextLineHeight() * 2.0f;
                float itemHeight = 4.0f * scale + thumbSize + 8.0f * scale + textHeight;
                ImVec2 itemSize(cellSize, itemHeight);

                // 1. 绘制隐形交互区（覆盖整个单元格）
                ImGui::SetCursorPos(cursorPos);
                ImGui::InvisibleButton("##ItemHitbox", itemSize);
                
                bool hovered = ImGui::IsItemHovered();
                bool doubleClicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                
                if (hovered) {
                    // 添加圆角高亮背景底板
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 
                        IM_COL32(80, 80, 80, 150), 6.0f * scale);
                    ImGui::SetTooltip("%s", filenameString.c_str());
                }

                // 2. 拖拽源：传输资产 UUID（非路径字符串）
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", &assetHandle, sizeof(UUID));
                    if (icon) {
                        ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                        ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                        ImGui::Image((ImTextureID)icon->GetImGuiTextureID(), { 32.0f * scale, 32.0f * scale }, uv0, uv1);
                    }
                    ImGui::Text("%s", filenameString.c_str());
                    ImGui::EndDragDropSource();
                }

                // 双击进入目录
                if (doubleClicked && directoryEntry.is_directory()) {
                    m_CurrentDirectory /= path.filename();
                }

                // 3. 手动绘制纯净图标 (居中，并放置在掩膜层上方)
                float imageOffsetX = (cellSize - thumbSize) * 0.5f;
                ImGui::SetCursorPos(ImVec2(cursorPos.x + imageOffsetX, cursorPos.y + 4.0f * scale));
                if (icon) {
                    ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                    ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);
                    // 彻底舍弃自带边框的 ImageButton，直接渲染原生的 Image
                    ImGui::Image((ImTextureID)icon->GetImGuiTextureID(), { thumbSize, thumbSize }, uv0, uv1);
                }

                // 4. 手动绘制文本 (居中 + 折断处理)
                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + 4.0f * scale + thumbSize + 8.0f * scale));
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
                    
                    // 防止文本框偏移越界
                    ImGui::SetCursorPosX(cursorPos.x + std::max(0.0f, offsetX));
                    ImGui::TextUnformatted(line.c_str());
                    
                    text = line_end;
                    while (text < text_end && (*text == ' ' || *text == '\n')) text++;
                    lineCount++;
                }

                // ==========================================
                // 5. 补齐光标 Y 轴，防止 Table 高度塌陷
                // 【核心修复】：提交一个 Dummy 空白占位符！
                // 告诉 ImGui 引擎：“我确实把画笔移到了这里，且我在这里放了一个 0x0 大小的隐形盒子”，
                // 从而合法扩展父级边界，彻底解决拖动滑杆导致的 Assert 闪退！
                // ==========================================
                ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + itemHeight));
                ImGui::Dummy(ImVec2(0.0f, 0.0f));

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        // ==========================================
        // 底栏: 缩略图大小滑杆
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