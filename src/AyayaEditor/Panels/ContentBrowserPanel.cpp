#include "ContentBrowserPanel.hpp"
#include <imgui.h>

namespace Ayaya {

    extern const std::filesystem::path g_AssetPath = "assets";

    ContentBrowserPanel::ContentBrowserPanel()
        : m_BaseDirectory(g_AssetPath), m_CurrentDirectory(g_AssetPath) {
        // 在构造函数中加载两张图标贴图
        m_DirectoryIcon = Texture2D::Create("assets/icons/folder_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_FileIcon      = Texture2D::Create("assets/icons/docs_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
        m_PngIcon       = Texture2D::Create("assets/icons/image_128dp_FFFFFF_FILL0_wght400_GRAD0_opsz48.png");
    }

    void ContentBrowserPanel::OnImGuiRender() {
        ImGui::Begin("Content Browser");

        // 1. 顶部的控制栏（返回按钮）
        if (m_CurrentDirectory != std::filesystem::path(g_AssetPath)) {
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
            auto relativePath = std::filesystem::relative(path, g_AssetPath);
            std::string filenameString = relativePath.filename().string();

            if (filenameString.empty() || filenameString[0] == '.') continue;

            // ==========================================
            // 1. 根据后缀名精准分配图标
            // ==========================================
            std::shared_ptr<Texture2D> icon = m_FileIcon;
            if (directoryEntry.is_directory()) {
                icon = m_DirectoryIcon;
            } else if (path.extension() == ".png" || path.extension() == ".jpg") {
                icon = m_PngIcon; // 如果是图片文件，换上专属图标！
            }

            ImGui::PushID(filenameString.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0)); 
            
            ImGui::ImageButton(filenameString.c_str(), (ImTextureID)(intptr_t)icon->GetRendererID(), { m_ThumbnailSize, m_ThumbnailSize }, { 0, 1 }, { 1, 0 });
            
            // ==========================================
            // 补上这段核心魔法：让按钮变成可以抓起的“拖拽源”！
            // ==========================================
            if (ImGui::BeginDragDropSource()) {
                // 将文件的相对路径转为字符串
                std::string itemPathStr = relativePath.string();
                
                // 打包包裹：贴上 "CONTENT_BROWSER_ITEM" 的标签，并塞入路径字符串
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPathStr.c_str(), itemPathStr.size() + 1);
                
                // 拖拽时跟在鼠标旁边的小半透明提示：显示图标和文件名
                ImGui::Image((ImTextureID)(intptr_t)icon->GetRendererID(), { 32.0f, 32.0f }, { 0, 1 }, { 1, 0 });
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
            // 核心修复：文字单行居中与高度锁定
            // ==========================================
            float textWidth = ImGui::CalcTextSize(filenameString.c_str()).x;
            float cursorPosX = ImGui::GetCursorPosX();
            
            // 如果文字宽度小于图标宽度，给它加一个 X 轴偏移，让它在图标下方完美居中
            if (textWidth <= m_ThumbnailSize) {
                float offset = (m_ThumbnailSize - textWidth) * 0.5f;
                ImGui::SetCursorPosX(cursorPosX + offset);
            }
            
            // 绝对不使用 TextWrapped！使用普通 Text 保证高度永远是一行。
            // (如果文件名超过图标宽度，ImGui 会自动把超出的部分裁掉，保持 UI 整洁)
            ImGui::Text("%s", filenameString.c_str());
            
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        // ==========================================
        // 滑杆样式
        // ==========================================
        float sliderWidth = 120.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - sliderWidth - 10.0f);
        
        // 因为滑杆变扁了，我们稍微往下多偏移一点点，让它在底栏视觉居中
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

        // 核心魔法：修改 FramePadding 的 Y 值 (这里设为 1.0f 或者 0.0f)，强行把滑杆的高度压扁！
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0.0f));

        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::SliderFloat("##IconSize", &m_ThumbnailSize, 32.0f, 128.0f, ""); // 空字符串隐藏数字

        // 记得弹出样式，防止污染其他 UI 组件
        ImGui::PopStyleVar(); 
        ImGui::PopStyleColor(5);

        ImGui::End();
    }
}