#include "ayapch.h"
#include "VFS.hpp"
#include "Core/Log.hpp"

namespace Ayaya {

    void VFS::Init() {
        AYAYA_CORE_INFO("VFS Initialized");
    }

    void VFS::Shutdown() {
        s_MountPoints.clear();
    }

    void VFS::Mount(const std::string& virtualNode, const std::filesystem::path& physicalPath) {
        s_MountPoints[virtualNode] = physicalPath;
        AYAYA_CORE_TRACE("VFS: Mounted '{0}://' to '{1}'", virtualNode, physicalPath.string());
    }

    void VFS::Unmount(const std::string& virtualNode) {
        s_MountPoints.erase(virtualNode);
    }

    bool VFS::IsVirtualPath(const std::string& path) {
        return path.find("://") != std::string::npos;
    }

    std::filesystem::path VFS::Resolve(const std::string& path) {
        // 【向下兼容】：如果不是虚拟路径，直接当做普通路径返回！
        if (!IsVirtualPath(path)) {
            return std::filesystem::path(path);
        }

        size_t pos = path.find("://");
        std::string mountPoint = path.substr(0, pos);
        std::string remainder = path.substr(pos + 3);

        if (s_MountPoints.find(mountPoint) != s_MountPoints.end()) {
            std::filesystem::path physicalPath = s_MountPoints[mountPoint] / remainder;
            return physicalPath;
        }

        AYAYA_CORE_ERROR("VFS: Failed to resolve mount point '{0}' in path '{1}'", mountPoint, path);
        return std::filesystem::path(path); // 找不到挂载点，兜底返回原路径
    }

    std::string VFS::ResolveString(const std::string& path) {
        return Resolve(path).string();
    }
}