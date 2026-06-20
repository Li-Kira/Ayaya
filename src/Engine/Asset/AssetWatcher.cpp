#include "ayapch.h"
#include "AssetWatcher.hpp"
#include "AssetManager.hpp"
#include "Core/Log.hpp"
#include "Core/VFS.hpp"
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <algorithm>

namespace Ayaya {

    // --- Asset weight for topological sort ---
    int AssetWatcher::GetAssetWeight(AssetType type) {
        switch (type) {
            case AssetType::Texture2D:
            case AssetType::TextureCube:
            case AssetType::LuaScript:  return 0;  // leaf resources
            case AssetType::Material:
            case AssetType::Model:      return 1;  // composite
            case AssetType::Prefab:     return 2;  // container
            case AssetType::Scene:      return 3;  // root
            default:                    return 0;
        }
    }

    void AssetWatcher::Initialize(const std::filesystem::path& assetRoot) {
        if (m_Initialized) return;
        m_AssetRoot = assetRoot;
        m_Watcher = std::make_unique<FileWatcher>();
        m_Watcher->Watch(assetRoot, true,
            [this](const FileEvent& event) { OnFileEvent(event); });
        m_Watcher->Start();
        m_Initialized = true;
        m_Enabled = true;
        AYAYA_CORE_INFO("[AssetWatcher] Monitoring: {}", assetRoot.string());
    }

    void AssetWatcher::Shutdown() {
        if (!m_Initialized) return;
        m_Watcher->Stop();
        m_Watcher.reset();
        m_Initialized = false;
    }

    void AssetWatcher::OnFileEvent(const FileEvent& event) {
        if (!m_Enabled || m_Paused) return;
        auto pathStr = event.Path.string();

        if (pathStr.find(".meta") != std::string::npos) return;
        if (pathStr.find(".tmp")  != std::string::npos) return;
        auto fn = event.Path.filename().string();
        if (!fn.empty() && fn[0] == '.') return;

        // Debounce
        auto now = std::chrono::steady_clock::now();
        auto it = m_DebounceMap.find(pathStr);
        if (it != m_DebounceMap.end()) {
            if (now - it->second.LastEvent < kDebounceMs) {
                it->second.LastEvent = now;
                it->second.Type = event.Type;
                return;
            }
        }
        m_DebounceMap[pathStr] = {now, event.Type};

        UUID handle = ResolveAssetUUID(event.Path);
        if (handle == 0 && event.Type == FileEventType::Added) {
            auto resolved = VFS::ResolveString(event.Path.string());
            if (!resolved.empty())
                handle = AssetManager::ImportAsset(resolved);
        }
        if (handle == 0) return;

        if (event.Type == FileEventType::Removed) {
            AYAYA_CORE_INFO("[AssetWatcher] Removed: {}", pathStr);
            return;
        }

        EnqueueReload(handle, event.Path);
    }

    void AssetWatcher::EnqueueReload(UUID handle, const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(m_ReloadMutex);
        for (auto& p : m_PendingReloads)
            if (p.Handle == handle) return;

        AssetType type = AssetType::None;
        auto& reg = AssetManager::GetRegistry();
        auto rit = reg.find(handle);
        if (rit != reg.end()) type = rit->second.Type;

        m_PendingReloads.push_back({handle, path, 0, type});
    }

    UUID AssetWatcher::ResolveAssetUUID(const std::filesystem::path& path) const {
        auto metaPath = path.string() + ".meta";
        if (std::filesystem::exists(metaPath)) {
            try {
                YAML::Node meta = YAML::LoadFile(metaPath);
                if (meta["UUID"]) return meta["UUID"].as<uint64_t>();
            } catch (...) {}
        }
        return AssetManager::FindHandleForPath(path);
    }

    void AssetWatcher::ResolveDependencies(UUID handle,
                                            std::vector<UUID>& outAffected) {
        // Avoid cycles
        if (std::find(outAffected.begin(), outAffected.end(), handle) != outAffected.end())
            return;
        outAffected.push_back(handle);
        for (auto dep : AssetManager::GetDependents(handle))
            ResolveDependencies(dep, outAffected);
    }

    std::vector<UUID> AssetWatcher::Update() {
        if (!m_Enabled || m_Paused) return {};
        return ProcessPendingReloads();
    }

    std::vector<UUID> AssetWatcher::ProcessPendingReloads() {
        std::lock_guard<std::mutex> lock(m_ReloadMutex);
        std::vector<UUID> allReloaded;

        auto it = m_PendingReloads.begin();
        while (it != m_PendingReloads.end()) {
            // Probe: wait for external writer to release
            if (!CanReadFileExclusive(it->Path)) {
                it->RetryCount++;
                if (it->RetryCount > PendingReload::kMaxRetries) {
                    AYAYA_CORE_WARN("[AssetWatcher] Timeout: {}", it->Path.string());
                    it = m_PendingReloads.erase(it);
                } else {
                    ++it;
                }
                continue;
            }

            // Collect all affected assets via dependency graph
            std::vector<UUID> affected;
            ResolveDependencies(it->Handle, affected);

            // Topological sort by asset weight — leaf resources first
            std::sort(affected.begin(), affected.end(),
                [](UUID a, UUID b) {
                    auto& reg = AssetManager::GetRegistry();
                    auto itA = reg.find(a);
                    auto itB = reg.find(b);
                    int wA = (itA != reg.end()) ? GetAssetWeight(itA->second.Type) : 0;
                    int wB = (itB != reg.end()) ? GetAssetWeight(itB->second.Type) : 0;
                    return wA < wB;  // lower weight first
                });

            // Reload in sorted order
            for (auto depHandle : affected) {
                AssetManager::ReloadAsset(depHandle);
            }
            allReloaded.insert(allReloaded.end(), affected.begin(), affected.end());

            AYAYA_CORE_INFO("[AssetWatcher] Reloaded {} asset(s): {}",
                affected.size(), it->Path.string());

            it = m_PendingReloads.erase(it);
        }

        return allReloaded;
    }

}
