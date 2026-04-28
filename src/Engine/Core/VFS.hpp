#pragma once

#include <string>
#include <unordered_map>
#include <filesystem>

namespace Ayaya {

    class VFS {
    public:
        static void Init();
        static void Shutdown();

        // 挂载物理目录到虚拟节点
        // 例如: Mount("engine", "C:/Ayaya/EngineResources")
        static void Mount(const std::string& virtualNode, const std::filesystem::path& physicalPath);
        static void Unmount(const std::string& virtualNode);

        // 解析虚拟路径为真实的物理路径
        // 例如输入: "engine://Shaders/pbr.frag"
        // 输出: "C:/Ayaya/EngineResources/Shaders/pbr.frag"
        static std::filesystem::path Resolve(const std::string& path);

        // 获取真实的物理路径字符串
        static std::string ResolveString(const std::string& path);

        // 判断是否为虚拟路径
        static bool IsVirtualPath(const std::string& path);

    private:
        inline static std::unordered_map<std::string, std::filesystem::path> s_MountPoints;
    };

}