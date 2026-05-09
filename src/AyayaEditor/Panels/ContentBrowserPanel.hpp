#pragma once

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <string>

#include "Renderer/Texture.hpp"

namespace Ayaya {

    class ContentBrowserPanel {
    public:
        ContentBrowserPanel();

        void OnImGuiRender();

    private:
        std::shared_ptr<Texture2D> GetThumbnail(const std::filesystem::path& path);

        std::filesystem::path m_BaseDirectory;
        std::filesystem::path m_CurrentDirectory;

        std::shared_ptr<Texture2D> m_DirectoryIcon;
        std::shared_ptr<Texture2D> m_FileIcon;
        std::shared_ptr<Texture2D> m_PngIcon;

        // 图片缩略图缓存
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_ThumbnailCache;
        static constexpr size_t kMaxThumbnailCache = 64;

        float m_ThumbnailSize = 64.0f;
    };

}
