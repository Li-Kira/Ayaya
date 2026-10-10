#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <memory>

#include "LaunchConfig.hpp"

namespace Ayaya {

    class Scene;

    // ==========================================
    // ExportSettings — 导出面板收集的用户设置
    // ==========================================
    struct ExportSettings {
        std::string GameName;
        std::string Platform;        // "macOS" | "Windows"
        std::string GraphicsAPI;     // "Vulkan" | "OpenGL"
        uint32_t    WindowWidth  = 1280;
        uint32_t    WindowHeight = 720;
        bool        VSync = false;
        std::filesystem::path DestDir;
    };

    // ==========================================
    // ExportPackager — 内容打包器 (v1: 松散文件复制)
    // 两阶段：Collect (主线程，读共享状态) → Execute (后台线程，只碰值类型 + 文件系统 + 子进程)。
    // ==========================================
    class ExportPackager {
    public:
        struct FileEntry {
            std::filesystem::path Source;    // 源物理路径
            std::filesystem::path DestRel;   // 相对导出目录的路径 (engine/... 或 project/...)
        };

        // 收集阶段产出的计划：资产文件列表 + 快照的启动配置 (纯值类型，可安全跨线程拷贝)
        struct ExportPlan {
            std::vector<FileEntry> entries;
            LaunchConfig launchConfig;
        };

        using ProgressFn = std::function<void(float, const std::string&)>;

        // 主线程：收集项目/引擎资产 + 快照 game.json 配置 (读 Project/VFS/AssetManager/Scene)
        static bool Collect(const ExportSettings& settings,
                            const std::shared_ptr<Scene>& activeScene,
                            ExportPlan& plan);

        // 后台线程：拷贝文件 + 写 game.json + 生成构建脚本 + 直接构建
        static bool Execute(const ExportSettings& settings,
                            const ExportPlan& plan,
                            ProgressFn progress = nullptr);

    private:
        // 收集项目资产树 (整个 AssetDirectory, 排除着色器源码/编译器/构建产物)
        static void CollectProjectAssets(std::vector<FileEntry>& out);
        // 收集引擎资产最小子集 (EngineAssets.yaml + 内置材质/天空盒 + 着色器缓存 + 字体)
        static void CollectEngineAssets(const std::string& api, std::vector<FileEntry>& out);
        // 场景闭包里额外引用的 engine:// 资产 (兜底)
        static void CollectReferencedEngineAssets(const std::shared_ptr<Scene>& scene, std::vector<FileEntry>& out);

        static bool WriteLaunchConfig(const LaunchConfig& lc, const std::filesystem::path& destDir);
        static bool GenerateBuildScripts(const ExportSettings& s, const std::filesystem::path& destDir);
        // 同平台直接构建产物 (调用 cmake + build 产出 .app / .exe)
        // progress: 报告构建内部进度 0.0~1.0 (流式解析 cmake 的 [NN%])
        static bool BuildArtifact(const ExportSettings& s, const std::filesystem::path& destDir,
                                  ProgressFn progress = nullptr);
        static bool CopyFiles(const std::vector<FileEntry>& entries,
                              const std::filesystem::path& destDir,
                              ProgressFn progress);
    };

} // namespace Ayaya
