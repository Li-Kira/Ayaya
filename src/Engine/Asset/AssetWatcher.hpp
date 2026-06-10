#pragma once
#include "Engine/Core/FileWatcher.hpp"
#include "Engine/Asset/Asset.hpp"
#include "Engine/Core/UUID.hpp"
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <chrono>
#include <filesystem>

namespace Ayaya {

    // ==========================================
    // AssetWatcher — hot-reload engine.
    // ==========================================
    class AssetWatcher {
    public:
        AssetWatcher() = default;
        ~AssetWatcher() { Shutdown(); }

        void Initialize(const std::filesystem::path& assetRoot);
        void Shutdown();
        void Update();  // main thread, each frame

        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool enabled) { m_Enabled = enabled; }

        // Pause: suppress all event processing during intentional file mutations
        // (rename/move/delete via AssetManager). Caller MUST re-enable.
        bool IsPaused() const { return m_Paused; }
        void SetPaused(bool paused) { m_Paused = paused; }

    private:
        void OnFileEvent(const FileEvent& event);
        void EnqueueReload(UUID handle, const std::filesystem::path& path);

        // Topological sort by asset weight, then process reloads
        void ProcessPendingReloads();

        // Resolve dependencies — chain affected assets
        void ResolveDependencies(UUID handle, std::vector<UUID>& outAffected);

        // Asset weight for topological sort (0=texture/shader, 1=material, 2=prefab, 3=scene)
        static int GetAssetWeight(AssetType type);

        UUID ResolveAssetUUID(const std::filesystem::path& path) const;

        std::unique_ptr<FileWatcher> m_Watcher;
        std::filesystem::path m_AssetRoot;
        bool m_Enabled = false;
        bool m_Paused = false;
        bool m_Initialized = false;

        // Debounce
        static constexpr auto kDebounceMs = std::chrono::milliseconds(300);
        struct DebounceEntry {
            std::chrono::steady_clock::time_point LastEvent;
            FileEventType Type;
        };
        std::unordered_map<std::string, DebounceEntry> m_DebounceMap;

        // Pending reloads
        struct PendingReload {
            UUID Handle;
            std::filesystem::path Path;
            int RetryCount = 0;
            AssetType Type = AssetType::None;
            static constexpr int kMaxRetries = 10;
        };
        std::vector<PendingReload> m_PendingReloads;
        std::mutex m_ReloadMutex;
    };

}
