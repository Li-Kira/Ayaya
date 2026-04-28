#pragma once

#include <string>
#include <filesystem>
#include <memory>

namespace Ayaya {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::string StartScene = "Scenes/Default.ayaya";
        std::string AssetDirectory = "Assets"; // 默认资产夹名为 Assets
    };

    class Project {
    public:
        static const std::shared_ptr<Project>& GetActive() { return s_ActiveProject; }
        
        // 核心接口：从硬盘加载项目文件
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

        ProjectConfig& GetConfig() { return m_Config; }

        // ==========================================
        // 核心逻辑：动态获取路径
        // ==========================================
        static std::filesystem::path GetProjectDirectory() {
            return s_ActiveProject->m_ProjectDirectory;
        }

        static std::filesystem::path GetAssetDirectory() {
            // 项目根目录 + 配置中的资产目录名 = 绝对物理资产路径
            return GetProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory;
        }

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory; // 运行时的物理锚点
        inline static std::shared_ptr<Project> s_ActiveProject;

        friend class ProjectSerializer;
    };
}