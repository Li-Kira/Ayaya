#pragma once

#include <filesystem>
#include <memory>

#include "Renderer/Texture.hpp"

namespace Ayaya {

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();

        void OnImGuiRender();

    private:
        // 根目录 (比如 "assets" 文件夹)
        std::filesystem::path m_BaseDirectory;
        // 当前浏览的所在目录
        std::filesystem::path m_CurrentDirectory;

        // ==========================================
        // 存放图标贴图的智能指针
        // ==========================================
        std::shared_ptr<Texture2D> m_DirectoryIcon;
        std::shared_ptr<Texture2D> m_FileIcon;
        std::shared_ptr<Texture2D> m_PngIcon;

        // ==========================================
        // 新增：记录缩略图大小（默认给个 96）
        // ==========================================
        float m_ThumbnailSize = 64.0f;
    };

}