#include "ayapch.h"
#include "VFS.hpp"
#include "Core/Log.hpp"
#include <algorithm>

namespace Ayaya {

    static std::string Normalize(std::string path) {
        std::replace(path.begin(), path.end(), '\\', '/');
        if (!path.empty() && path.back() == '/') path.pop_back();
        return path;
    }

    void VFS::Init() {
        AYAYA_CORE_INFO("VFS Initialized");
    }

    void VFS::Shutdown() {
        s_MountPoints.clear();
    }

    void VFS::Mount(const std::string& scheme, const std::filesystem::path& physicalPath) {
        s_MountPoints[scheme] = std::filesystem::absolute(physicalPath).lexically_normal();
        AYAYA_CORE_INFO("VFS Mounted: {0}:// -> {1}", scheme, s_MountPoints[scheme].string());
    }

    void VFS::Unmount(const std::string& scheme) {
        s_MountPoints.erase(scheme);
    }

    std::string VFS::GetScheme(const std::string& path) {
        if (path.empty()) return "";
        size_t pos = path.find("://");
        if (pos != std::string::npos) {
            return path.substr(0, pos);
        }
        return ""; // 没有协议头
    }

    bool VFS::IsVirtualPath(const std::string& path) {
        return path.find("://") != std::string::npos;
    }

    std::filesystem::path VFS::Resolve(const std::string& path) {
        return std::filesystem::path(ResolveString(path));
    }

    std::string VFS::ResolveString(const std::string& path) {
        if (path.empty()) return "";
        std::string nPath = Normalize(path);

        // 1. 已经是绝对物理路径，直接放行
        if (std::filesystem::path(nPath).is_absolute()) {
            return nPath;
        }

        // 2. 带有明确的协议头 (engine:// 或 project://)
        size_t schemePos = nPath.find("://");
        if (schemePos != std::string::npos) {
            std::string scheme = nPath.substr(0, schemePos);
            std::string rest = nPath.substr(schemePos + 3);
            if (s_MountPoints.count(scheme)) {
                return Normalize((s_MountPoints[scheme] / rest).string());
            }
            return nPath; 
        }

        // 3. 【核心简化规则】：没有任何协议头的相对路径，全部视为 Engine 资源！
        // 这样 Shader::Create("shaders/skybox.vert") 就会自动去引擎目录下找
        if (s_MountPoints.count("engine")) {
            return Normalize((s_MountPoints["engine"] / nPath).string());
        }

        return nPath;
    }

    std::string VFS::GetVirtualPath(const std::filesystem::path& physicalPath) {
        std::string target = Normalize(std::filesystem::absolute(physicalPath).lexically_normal().string());

        // 优先检查是否属于项目目录 (因为项目可能建在引擎文件夹旁边，先判断具体的)
        if (s_MountPoints.count("project")) {
            std::string projectDir = Normalize(s_MountPoints["project"].string());
            if (target.find(projectDir) == 0) {
                std::string rel = target.substr(projectDir.length());
                if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
                return "project://" + rel; 
            }
        }

        // 再检查是否属于引擎目录
        if (s_MountPoints.count("engine")) {
            std::string engineDir = Normalize(s_MountPoints["engine"].string());
            if (target.find(engineDir) == 0) {
                std::string rel = target.substr(engineDir.length());
                if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
                return "engine://" + rel; 
            }
        }

        // 如果都不在，说明用户从桌面等外部位置拖入，原样返回绝对路径
        return target; 
    }
}