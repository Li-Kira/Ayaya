#include "ContentBrowserPanel.hpp"
#include "Project/Project.hpp"  // 【新增】：引入项目配置
#include "Core/VFS.hpp"         // 【新增】：引入虚拟文件系统
#include "Core/Log.hpp"

#include <imgui.h>

namespace Ayaya {

    ContentBrowserPanel::ContentBrowserPanel() {
        m_DirectoryIcon = Texture2D::Create("assets/Editor/icons/folder_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_FileIcon      = Texture2D::Create("assets/Editor/icons/docs_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_PngIcon       = Texture2D::Create("assets/Editor/icons/image_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");

        // 【修改】：初始化时，默认指向当前项目的 Assets 目录
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

        // 1. 顶部的控制栏（返回按钮）
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
            
            // 【修改】：直接获取文件名，不再用旧的 relativePath 计算
            std::string filenameString = path.filename().string();

            // 完美保留你的隐藏文件过滤逻辑！
            if (filenameString.empty() || filenameString[0] == '.') continue;
            if (path.extension() == ".yaml") continue;

            // ==========================================
            // 1. 根据后缀名精准分配图标 (保留原有逻辑)
            // ==========================================
            std::shared_ptr<Texture2D> icon = m_FileIcon;
            if (directoryEntry.is_directory()) {
                icon = m_DirectoryIcon;
            } else if (path.extension() == ".png" || path.extension() == ".jpg") {
                icon = m_PngIcon; 
            }

            ImGui::PushID(filenameString.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
            
            // 检查 icon 指针是否有效且贴图已加载
            if (icon) {
                ImVec2 uv0 = icon->IsDataFlipped() ? ImVec2(0, 1) : ImVec2(0, 0);
                ImVec2 uv1 = icon->IsDataFlipped() ? ImVec2(1, 0) : ImVec2(1, 1);

                ImGui::ImageButton(filenameString.c_str(), (ImTextureID)icon->GetImGuiTextureID(), 
                                   { m_ThumbnailSize, m_ThumbnailSize }, uv0, uv1);
            } else {
                ImGui::Button(directoryEntry.is_directory() ? "[DIR]" : "[FILE]", 
                              { m_ThumbnailSize, m_ThumbnailSize });
            }
            
            // ==========================================
            // 让按钮变成可以抓起的“拖拽源”！
            // ==========================================
            if (ImGui::BeginDragDropSource()) {
                // 使用 VFS 解析当前物理路径为虚拟路径
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
            // ==========================================

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (directoryEntry.is_directory()) {
                    m_CurrentDirectory /= path.filename();
                }
            }

            // ==========================================
            // 多行居中 + 省略号截断显示
            // ==========================================
            float cursorPosX = ImGui::GetCursorPosX();
            ImFont* font = ImGui::GetFont();
            
            float fontScale = ImGui::GetIO().FontGlobalScale; 
            
            const char* text = filenameString.c_str();
            const char* text_end = text + filenameString.length();
            
            float wrapWidth = m_ThumbnailSize; 
            int lineCount = 0;
            const int maxLines = 2;

            while (text < text_end && lineCount < maxLines) {
                // 利用 ImGui 原生字体库计算安全的换行点，完美支持中英文单词切分！
                const char* line_end = font->CalcWordWrapPositionA(fontScale, text, text_end, wrapWidth);
                if (line_end == text) line_end++; // 防御性推进，防止死循环

                std::string line(text, line_end);
                
                // 如果到了最大行数的最后一行，且文本还没有读完，追加省略号
                if (lineCount == maxLines - 1 && line_end < text_end) {
                    line += "..."; 
                }

                float lineWidth = ImGui::CalcTextSize(line.c_str()).x;
                float offset = (wrapWidth - lineWidth) * 0.5f;
                
                // 针对切分出来的这单个行，进行完美的居中偏移
                if (offset > 0.0f) {
                    ImGui::SetCursorPosX(cursorPosX + offset);
                } else {
                    ImGui::SetCursorPosX(cursorPosX);
                }

                ImGui::TextUnformatted(line.c_str());

                // 推进文本指针，过滤掉换行产生的空格，准备渲染下一行
                text = line_end;
                while (text < text_end && (*text == ' ' || *text == '\n')) text++;
                
                lineCount++;
            }
            
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        // ==========================================
        // 滑杆样式 (完美保留！)
        // ==========================================
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