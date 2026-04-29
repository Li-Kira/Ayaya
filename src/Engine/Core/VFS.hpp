#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>

namespace Ayaya {

    class VFS {
    public:
        static void Init();
        static void Shutdown();

        // 挂载目录 (例如 Mount("engine", "C:/Ayaya/assets/Editor"))
        static void Mount(const std::string& scheme, const std::filesystem::path& physicalPath);
        static void Unmount(const std::string& scheme);

        // 【核心解析】：将虚拟路径转换为物理绝对路径
        // 规则 1: "project://xxx" -> 项目挂载点
        // 规则 2: "engine://xxx"  -> 引擎挂载点
        // 规则 3: "C:/xxx"        -> 原样返回
        // 规则 4: "shaders/xxx"   -> 默认无协议头的，全部视为 engine 资源！
        static std::filesystem::path Resolve(const std::string& path);
        static std::string ResolveString(const std::string& path);

        // 【核心打包】：将物理路径压缩为虚拟路径 (仅供 UI 拖拽时使用)
        // 返回 "project://..." 或 "engine://..."，不在挂载点则返回原绝对路径
        static std::string GetVirtualPath(const std::filesystem::path& physicalPath);

        // 提取协议头，例如 "engine://xxx" 返回 "engine"。如果没有 "://" 则返回空
        static std::string GetScheme(const std::string& path);
        
        // 判断是否是明确的虚拟路径 (即包含 "://")
        static bool IsVirtualPath(const std::string& path);

    private:
        // scheme -> physical path
        inline static std::unordered_map<std::string, std::filesystem::path> s_MountPoints;
    };

}