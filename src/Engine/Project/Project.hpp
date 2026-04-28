#pragma once
#include <string>
#include <filesystem>
#include <memory>

namespace Ayaya {

    struct ProjectConfig {
        std::string Name = "Untitled";
        std::string StartScene;      // 项目默认启动场景的路径
        std::string AssetDirectory;  // 资产目录的相对路径 (通常是 "Assets")
        std::string ScriptModulePath;// 用户 C#/Lua 脚本的编译输出路径
    };

    class Project {
    public:
        // 获取当前激活的项目
        static const std::shared_ptr<Project>& GetActive() { return s_ActiveProject; }
        static std::shared_ptr<Project> New();
        static std::shared_ptr<Project> Load(const std::filesystem::path& path);
        static bool SaveActive(const std::filesystem::path& path);

        ProjectConfig& GetConfig() { return m_Config; }

        // 获取该项目资产的绝对路径
        static std::filesystem::path GetAssetDirectory();
        // 获取相对于项目资产目录的路径 (用于序列化保存)
        static std::filesystem::path GetRelativeAssetPath(const std::filesystem::path& path);

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory; // 项目 .ayaproj 所在的文件夹绝对路径
        inline static std::shared_ptr<Project> s_ActiveProject;
    };
}