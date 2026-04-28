#pragma once

#include <string>
#include <filesystem>
#include <memory>

namespace Ayaya {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::string StartScene = "Scenes/Default.ayaya";
        std::string AssetDirectory = "assets"; // 默认资产夹名为 Assets
    };

    class Project {
    public:
        static const std::shared_ptr<Project>& GetActive() { return s_ActiveProject; }
        
        // 核心接口：从硬盘加载项目文件
        static std::shared_ptr<Project> New();
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

        ProjectConfig& GetConfig() { return m_Config; }

        // ==========================================
        // 核心逻辑：动态获取路径
        // ==========================================
        static std::filesystem::path GetProjectDirectory() {
            // 【防御】：如果没有激活的项目，默认返回当前程序的运行目录
            if (!s_ActiveProject) return std::filesystem::current_path();
            return s_ActiveProject->m_ProjectDirectory;
        }

        static std::filesystem::path GetAssetDirectory() {
            // 【防御】：如果没有激活的项目，默认退回到老架构的 "assets" 文件夹
            if (!s_ActiveProject) return std::filesystem::current_path() / "assets";
            return GetProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory;
        }

        // 获取相对于项目资产目录的相对路径
        static std::filesystem::path GetRelativeAssetPath(const std::filesystem::path& path) {
            std::error_code ec;
            std::filesystem::path rel = std::filesystem::relative(path, GetAssetDirectory(), ec);
            if (ec || rel.empty()) return path.lexically_normal();
            return rel;
        }

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory; // 运行时的物理锚点
        inline static std::shared_ptr<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };
}