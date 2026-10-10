#pragma once

#include <string>
#include <filesystem>
#include <cstdint>

namespace Ayaya {

    // ==========================================
    // LaunchConfig — 游戏启动配置 (game.json / YAML)
    // 由导出打包器生成，运行时 (Sandbox) 读取。
    // 一份二进制 + 启动配置即可服务所有导出游戏。
    // ==========================================
    struct LaunchConfig {
        std::string GameName;

        std::string ProjectFile = "project/Game.ayaproj";  // 相对 game.json 所在目录
        std::string StartScene  = "Scenes/Default.ayaya";  // 相对项目 AssetDirectory
        std::string AssetDirectory = "Assets";
        uint64_t    DefaultSRPScript = 0;                  // 0 = 硬编码默认管线

        uint32_t    WindowWidth  = 1280;
        uint32_t    WindowHeight = 720;
        std::string WindowTitle;
        bool        VSync = false;

        std::string GraphicsAPI = "Vulkan";                // "Vulkan" | "OpenGL"

        static bool Load(const std::filesystem::path& filepath, LaunchConfig& out);
        static bool Save(const std::filesystem::path& filepath, const LaunchConfig& config);
    };

} // namespace Ayaya
