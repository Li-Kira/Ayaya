#include "ayapch.h"
#include "ExportPackager.hpp"
#include "Project/Project.hpp"
#include "Project/LaunchConfig.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"
#include "Asset/AssetManager.hpp"
#include "Scene/Scene.hpp"

#include <fstream>
#include <set>
#include <unordered_set>
#include <cstdlib>
#include <cstdio>

namespace Ayaya {

    namespace {

        // ---- 字符串替换 (模板填充) ----
        static std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
            if (from.empty()) return str;
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::string::npos) {
                str.replace(pos, from.length(), to);
                pos += to.length();
            }
            return str;
        }

        // ---- 排除项：着色器源码 / 编译器工具链 / 构建产物 / 临时文件 ----
        // 构建工具链 (.hlsl/.py) 永远不导出；GLSL 源码 (.glsl/.vert/.frag/.comp) 只有
        // OpenGL 后端运行时编译需要，按 excludeShaderSource 决定是否排除。
        static bool IsExcludedProjectEntry(const std::filesystem::path& p, bool excludeShaderSource) {
            std::string name = p.filename().string();
            if (name == ".Trash" || name == "Temp" || name == ".DS_Store") return true;
            if (name == "AssetRegistry.yaml") return true;  // 遗留集中注册表

            std::string ext = p.extension().string();
            if (ext == ".hlsl" || ext == ".py") return true;  // 构建工具链，永远排除
            if (excludeShaderSource) {
                if (ext == ".glsl" || ext == ".vert" || ext == ".frag" || ext == ".comp") return true;
            }
            return false;
        }

        // 递归遍历 srcDir，为每个未被排除的文件生成 (Source, DestRel)
        static void WalkDir(const std::filesystem::path& srcDir,
                            const std::filesystem::path& destPrefix,
                            std::vector<ExportPackager::FileEntry>& out,
                            std::unordered_set<std::string>* seen = nullptr,
                            bool excludeShaderSource = true) {
            if (!std::filesystem::exists(srcDir)) return;
            for (auto& entry : std::filesystem::recursive_directory_iterator(
                     srcDir, std::filesystem::directory_options::skip_permission_denied)) {
                if (!entry.is_regular_file()) continue;
                const auto& src = entry.path();
                if (IsExcludedProjectEntry(src, excludeShaderSource)) continue;

                std::filesystem::path rel = std::filesystem::relative(src, srcDir);
                std::filesystem::path destRel = destPrefix / rel;

                std::string key = src.lexically_normal().string();
                if (seen && !seen->insert(key).second) continue;  // 去重

                out.push_back({ src, destRel });
            }
        }

        // 在 projectDir 下查找 .ayaproj 文件
        static std::filesystem::path FindProjectFile(const std::filesystem::path& projectDir) {
            if (!std::filesystem::exists(projectDir)) return {};
            for (auto& entry : std::filesystem::directory_iterator(projectDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".ayaproj")
                    return entry.path();
            }
            return {};
        }

        // CMake target 名不允许空格等字符
        static std::string SanitizeTargetName(const std::string& name) {
            std::string out = name;
            for (auto& c : out) {
                if (!(std::isalnum((unsigned char)c) || c == '_')) c = '_';
            }
            if (out.empty()) out = "Game";
            return out;
        }

        // 运行命令并逐行回调输出 (跨平台 popen；返回退出状态，非 0 表示失败)
        static int RunCommandStreaming(const std::string& cmd,
                                       const std::function<void(const std::string&)>& onLine) {
#ifdef _WIN32
            FILE* pipe = _popen((cmd + " 2>&1").c_str(), "r");
#else
            FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
            if (!pipe) return -1;

            char buf[512];
            while (fgets(buf, sizeof(buf), pipe)) {
                std::string line(buf);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
                if (onLine) onLine(line);
            }
#ifdef _WIN32
            int status = _pclose(pipe);
#else
            int status = pclose(pipe);
#endif
            return status;
        }

    } // anonymous namespace

    // ==========================================
    // 收集项目资产树
    // ==========================================
    void ExportPackager::CollectProjectAssets(std::vector<FileEntry>& out) {
        auto project = Project::GetActive();
        if (!project) return;

        auto& config = project->GetConfig();
        std::filesystem::path projectDir = Project::GetProjectDirectory();
        std::filesystem::path assetDir  = Project::GetAssetDirectory();

        // 1. .ayaproj 项目文件
        std::filesystem::path projFile = FindProjectFile(projectDir);
        if (!projFile.empty()) {
            out.push_back({ projFile, std::filesystem::path("project") / projFile.filename() });
        }

        // 2. 整个资产树 (含 .meta 侧车) → project/<AssetDirectory>/...
        std::filesystem::path destPrefix = std::filesystem::path("project") / config.AssetDirectory;
        WalkDir(assetDir, destPrefix, out);
    }

    // ==========================================
    // 收集引擎资产最小子集
    // ==========================================
    void ExportPackager::CollectEngineAssets(const std::string& api, std::vector<FileEntry>& out) {
        // 注意：不能写 VFS::Resolve("engine://") —— VFS::Normalize 会把末尾 '/' 剥掉，
        // "engine://" 变成 "engine:/" 导致解析到错误路径。改用一个已知 engine 子路径解析。
        std::filesystem::path editorRoot = VFS::Resolve("engine://Editor");
        std::unordered_set<std::string> seen;

        // 必需：内建资产注册表 + 内建材质 + 天空盒 (UUID …04/…05/…06)
        const char* kRequiredFiles[] = {
            "EngineAssets.yaml",
            "materials/DefaultPBR.mat",
        };
        for (const char* rel : kRequiredFiles) {
            std::filesystem::path src = editorRoot / rel;
            if (std::filesystem::exists(src))
                out.push_back({ src, std::filesystem::path("engine/Editor") / rel });
        }
        // 天空盒目录 (HDR + cube)
        WalkDir(editorRoot / "textures/skybox", "engine/Editor/textures/skybox", out, &seen);

        // 着色器：Vulkan 发 .spv 缓存，OpenGL 保留 GLSL 源码 (驱动运行时编译)
        if (api == "OpenGL") {
            // OpenGL 后端运行时编译 GLSL，需保留 .vert/.frag 源码 (excludeShaderSource=false)
            WalkDir(editorRoot / "shaders/src/opengl", "engine/Editor/shaders/src/opengl", out, &seen, /*excludeShaderSource=*/false);
        } else {
            WalkDir(editorRoot / "shaders/cache/vulkan", "engine/Editor/shaders/cache/vulkan", out, &seen);
        }

        // 字体 (ImGui 呈现路径需要)
        WalkDir(editorRoot / "fonts", "engine/Editor/fonts", out, &seen);
    }

    // ==========================================
    // 场景闭包里额外引用的 engine:// 资产 (兜底)
    // ==========================================
    void ExportPackager::CollectReferencedEngineAssets(const std::shared_ptr<Scene>& scene, std::vector<FileEntry>& out) {
        if (!scene) return;
        std::unordered_set<std::string> seen;
        for (const auto& e : out) seen.insert(e.Source.lexically_normal().string());

        auto handles = scene->GetActiveAssetHandles();
        for (UUID handle : handles) {
            AssetMetadata meta = AssetManager::GetMetadata(handle);
            if (meta.VirtualPath.rfind("engine://", 0) != 0) continue;  // 只关心引擎资产
            std::filesystem::path physical = VFS::Resolve(meta.VirtualPath);
            if (!std::filesystem::exists(physical)) continue;

            // 相对 engine 根 → engine/...
            std::filesystem::path engineRoot = VFS::Resolve("engine://Editor").parent_path();
            std::filesystem::path rel = std::filesystem::relative(physical, engineRoot);
            if (rel.empty() || rel.string().rfind("..", 0) == 0) continue;

            if (seen.insert(physical.lexically_normal().string()).second)
                out.push_back({ physical, std::filesystem::path("engine") / rel });
        }
    }

    // ==========================================
    // 写 game.json (lc 已在 Collect 阶段快照好)
    // ==========================================
    bool ExportPackager::WriteLaunchConfig(const LaunchConfig& lc, const std::filesystem::path& destDir) {
        return LaunchConfig::Save(destDir / "game.json", lc);
    }

    // ==========================================
    // 生成构建脚本 (CMakeLists.txt + Info.plist + build_game.*)
    // ==========================================
    bool ExportPackager::GenerateBuildScripts(const ExportSettings& s, const std::filesystem::path& destDir) {
        std::string target = SanitizeTargetName(s.GameName);

        // ---- CMakeLists.txt ----
        std::string cmake = R"cmake(cmake_minimum_required(VERSION 3.20)
project(@TARGET@ VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED AYAYA_SOURCE_DIR)
    message(FATAL_ERROR "Pass -DAYAYA_SOURCE_DIR=<path-to-Ayaya-repo> to configure.")
endif()

# 复用引擎静态库定义；EXCLUDE_FROM_ALL 使 AyayaEditor/Sandbox 只定义不构建
add_subdirectory("${AYAYA_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ayaya-engine" EXCLUDE_FROM_ALL)

# 游戏运行时：复用 Sandbox 源码 (入口 + 通用运行时层)
file(GLOB PLAYER_SOURCES
    "${AYAYA_SOURCE_DIR}/src/Sandbox/*.cpp"
    "${AYAYA_SOURCE_DIR}/src/Sandbox/*.hpp")
add_executable(@TARGET@ ${PLAYER_SOURCES})
target_link_libraries(@TARGET@ PRIVATE Ayaya)
# 引擎主 CMake 里这些是 add_definitions (目录作用域)，不会传播到这个 target，必须在此补上
target_compile_definitions(@TARGET@ PRIVATE
    AYAYA_BUILD_RUNTIME
    GLFW_INCLUDE_NONE
    GLFW_INCLUDE_VULKAN)
if(APPLE)
    target_compile_definitions(@TARGET@ PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
endif()
if(WIN32)
    target_compile_definitions(@TARGET@ PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(EXPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(APPLE)
    set_target_properties(@TARGET@ PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "@NAME@"
        MACOSX_BUNDLE_INFO_PLIST "${EXPORT_DIR}/Info.plist"
        MACOSX_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET @TARGET@ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXPORT_DIR}/engine" "$<TARGET_BUNDLE_CONTENT_DIR:@TARGET@>/Resources/engine"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXPORT_DIR}/project" "$<TARGET_BUNDLE_CONTENT_DIR:@TARGET@>/Resources/project"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXPORT_DIR}/game.json" "$<TARGET_BUNDLE_CONTENT_DIR:@TARGET@>/Resources/game.json"
        COMMENT "Packaging game content into bundle")
elseif(WIN32)
    set_target_properties(@TARGET@ PROPERTIES WIN32_EXECUTABLE TRUE)
    file(GLOB VENDOR_DLLS "${AYAYA_SOURCE_DIR}/vendor/bin/*.dll")
    add_custom_command(TARGET @TARGET@ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXPORT_DIR}/engine" "$<TARGET_FILE_DIR:@TARGET@>/engine"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${EXPORT_DIR}/project" "$<TARGET_FILE_DIR:@TARGET@>/project"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EXPORT_DIR}/game.json" "$<TARGET_FILE_DIR:@TARGET@>/game.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VENDOR_DLLS} "$<TARGET_FILE_DIR:@TARGET@>"
        COMMENT "Packaging game content next to executable")
endif()
)cmake";
        cmake = ReplaceAll(cmake, "@TARGET@", target);
        cmake = ReplaceAll(cmake, "@NAME@", s.GameName);
        std::ofstream(destDir / "CMakeLists.txt") << cmake;

        // ---- Info.plist (macOS) ----
        std::string plist = R"plist(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>@TARGET@</string>
    <key>CFBundleIdentifier</key><string>com.ayaya.@TARGET@</string>
    <key>CFBundleName</key><string>@NAME@</string>
    <key>CFBundleDisplayName</key><string>@NAME@</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
)plist";
        plist = ReplaceAll(plist, "@TARGET@", target);
        plist = ReplaceAll(plist, "@NAME@", s.GameName);
        std::ofstream(destDir / "Info.plist") << plist;

        // ---- build_game.command (macOS) ----
        std::string command = R"bash(#!/bin/bash
set -e
cd "$(dirname "$0")"
if [ -z "$AYAYA_SOURCE_DIR" ] && [ -z "$1" ]; then
    echo "Usage: ./build_game.command /path/to/Ayaya-repo"
    exit 1
fi
AYAYA_SOURCE_DIR="${AYAYA_SOURCE_DIR:-$1}"
cmake -S . -B build -DAYAYA_SOURCE_DIR="$AYAYA_SOURCE_DIR"
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"
echo "Done. Bundle: build/@TARGET@.app"
)bash";
        command = ReplaceAll(command, "@TARGET@", target);
        std::ofstream(destDir / "build_game.command") << command;
        std::error_code ec;
        std::filesystem::permissions(destDir / "build_game.command",
            std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add, ec);

        // ---- build_game.bat (Windows) ----
        std::string bat = R"bat(@echo off
cd /d "%~dp0"
if "%~1"=="" (
    echo Usage: build_game.bat C:\path\to\Ayaya-repo
    exit /b 1
)
set AYAYA_SOURCE_DIR=%~1
cmake -S . -B build -DAYAYA_SOURCE_DIR="%AYAYA_SOURCE_DIR%" -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
echo Done. Output: build\Release\@TARGET@.exe
)bat";
        bat = ReplaceAll(bat, "@TARGET@", target);
        std::ofstream(destDir / "build_game.bat") << bat;

        return true;
    }

    // ==========================================
    // 同平台直接构建产物
    // ==========================================
    bool ExportPackager::BuildArtifact(const ExportSettings& s, const std::filesystem::path& destDir, ProgressFn progress) {
#ifdef AYAYA_ENGINE_SOURCE_DIR
        std::string engineDir = AYAYA_ENGINE_SOURCE_DIR;
#else
        AYAYA_CORE_WARN("[Export] Engine source dir unknown — skipping direct build. Use build_game.* instead.");
        return false;
#endif

        std::filesystem::path buildDir = destDir / "build";

        // cmake 绝对路径 (烘焙，避免 Finder 启动的 .app 找不到 PATH 里的 cmake)
#ifdef AYAYA_CMAKE_PATH
        std::string cmakeExe = std::string("\"") + AYAYA_CMAKE_PATH + "\"";
#else
        std::string cmakeExe = "cmake";
#endif

        // 复用编辑器构建时的 SDK，避免环境自动探测到不兼容 SDK
#ifdef AYAYA_OSX_SYSROOT
        std::string sdkFlag = " -DCMAKE_OSX_SYSROOT=\"" + std::string(AYAYA_OSX_SYSROOT) + "\"";
#else
        std::string sdkFlag;
#endif

        // 1. 配置 (较快，无细粒度进度)
        std::string cfgCmd = cmakeExe + " -S \"" + destDir.string() + "\" -B \"" + buildDir.string()
            + "\" -DAYAYA_SOURCE_DIR=\"" + engineDir + "\"" + sdkFlag;
        AYAYA_CORE_INFO("[Export] {0}", cfgCmd);
        int rc = std::system(cfgCmd.c_str());
        if (rc != 0) {
            AYAYA_CORE_ERROR("[Export] cmake configure failed (exit {0})", rc);
            return false;
        }

        // 2. 构建 (慢，流式读取 cmake 输出的 "[ NN%]" 作为真实进度)
        std::string buildCmd = cmakeExe + " --build \"" + buildDir.string() + "\" --config Release --parallel";
        AYAYA_CORE_INFO("[Export] {0}", buildCmd);

        int status = RunCommandStreaming(buildCmd, [&](const std::string& line) {
            if (line.size() < 3 || line[0] != '[') return;
            size_t close = line.find(']');
            if (close == std::string::npos || close <= 1) return;

            int pct = -1;
            try { pct = std::stoi(line.substr(1, close - 1)); } catch (...) { pct = -1; }
            if (pct >= 0 && pct <= 100 && progress)
                progress((float)pct / 100.0f, "Building... " + std::to_string(pct) + "%");
        });
        if (status != 0) {
            AYAYA_CORE_ERROR("[Export] cmake build failed (status {0})", status);
            return false;
        }
        return true;
    }

    // ==========================================
    // 拷贝文件
    // ==========================================
    bool ExportPackager::CopyFiles(const std::vector<FileEntry>& entries,
                                   const std::filesystem::path& destDir,
                                   ProgressFn progress) {
        float total = (float)std::max<size_t>(1, entries.size());
        for (size_t i = 0; i < entries.size(); i++) {
            const auto& e = entries[i];
            std::filesystem::path dest = destDir / e.DestRel;
            std::error_code ec;
            std::filesystem::create_directories(dest.parent_path(), ec);
            std::filesystem::copy_file(e.Source, dest, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                AYAYA_CORE_WARN("[Export] Failed to copy {0}: {1}", e.Source.string(), ec.message());
            }
            if (progress) progress((float)(i + 1) / total,
                "Copying assets... " + std::to_string(i + 1) + "/" + std::to_string(entries.size()));
        }
        return true;
    }

    // ==========================================
    // 收集阶段 (主线程)：读 Project/VFS/AssetManager/Scene，快照成纯值计划
    // ==========================================
    bool ExportPackager::Collect(const ExportSettings& settings,
                                 const std::shared_ptr<Scene>& activeScene,
                                 ExportPlan& plan) {
        auto project = Project::GetActive();
        if (!project) {
            AYAYA_CORE_ERROR("[Export] No active project.");
            return false;
        }

        // 收集资产文件清单
        CollectProjectAssets(plan.entries);
        CollectEngineAssets(settings.GraphicsAPI, plan.entries);
        CollectReferencedEngineAssets(activeScene, plan.entries);

        // 快照启动配置 (后台线程不再读 Project)
        auto& config = project->GetConfig();
        LaunchConfig lc;
        lc.GameName         = settings.GameName;
        lc.ProjectFile      = "project/" + std::filesystem::path(FindProjectFile(Project::GetProjectDirectory())).filename().string();
        lc.StartScene       = config.StartScene;
        lc.AssetDirectory   = config.AssetDirectory;
        lc.DefaultSRPScript = config.DefaultSRPScript;
        lc.WindowWidth      = settings.WindowWidth;
        lc.WindowHeight     = settings.WindowHeight;
        lc.WindowTitle      = settings.GameName;
        lc.VSync            = settings.VSync;
        lc.GraphicsAPI      = settings.GraphicsAPI;
        plan.launchConfig = lc;

        return true;
    }

    // ==========================================
    // 执行阶段 (后台线程)：只碰 value 类型 + 文件系统 + 子进程
    // ==========================================
    bool ExportPackager::Execute(const ExportSettings& settings,
                                 const ExportPlan& plan,
                                 ProgressFn progress) {
        if (progress) progress(0.0f, "Preparing destination...");
        std::error_code ec;
        std::filesystem::create_directories(settings.DestDir, ec);
        if (ec) {
            AYAYA_CORE_ERROR("[Export] Cannot create destination: {0}", settings.DestDir.string());
            return false;
        }

        // 1. 拷贝 (5-35%)
        CopyFiles(plan.entries, settings.DestDir, [&](float p, const std::string& msg) {
            if (progress) progress(0.05f + p * 0.30f, msg);
        });

        // 2. 启动配置 + 构建脚本 (35-40%)
        if (progress) progress(0.35f, "Writing launch config...");
        WriteLaunchConfig(plan.launchConfig, settings.DestDir);

        if (progress) progress(0.38f, "Generating build scripts...");
        GenerateBuildScripts(settings, settings.DestDir);

        // 3. 同平台直接构建可运行产物 (Unreal Package Project 行为)；跨平台仅保留脚本
        // 构建是慢的部分，占 40-100% 的进度
#ifdef __APPLE__
        const bool hostIsMac = true;
#else
        const bool hostIsMac = false;
#endif
        const bool samePlatform = (settings.Platform == "macOS") == hostIsMac;
        bool success = true;
        if (samePlatform) {
            if (progress) progress(0.40f, "Building executable...");
            success = BuildArtifact(settings, settings.DestDir, [&](float p, const std::string& msg) {
                if (progress) progress(0.40f + p * 0.60f, msg);
            });
        }

        if (progress) progress(1.0f, success ? "Export complete." : "Export finished with build errors.");
        AYAYA_CORE_INFO("[Export] Exported {0} ({1}/{2}) to {3}",
            settings.GameName, settings.Platform, settings.GraphicsAPI, settings.DestDir.string());
        return success;
    }

} // namespace Ayaya
