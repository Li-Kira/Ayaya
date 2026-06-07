#pragma once
#include <functional>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace Ayaya {

    enum class FileEventType { Added, Modified, Removed };

    struct FileEvent {
        std::filesystem::path Path;
        FileEventType         Type;
        std::chrono::steady_clock::time_point Timestamp;
    };

    // ==========================================
    // Lightweight file watcher — tracks mtime of
    // known files only (no directory scan).
    // For production, replace with ReadDirectoryChangesW / FSEvents.
    // ==========================================
    class FileWatcher {
    public:
        using Callback = std::function<void(const FileEvent&)>;

        FileWatcher() = default;
        ~FileWatcher() { Stop(); }

        // Watch a directory: new files trigger Added, tracked files check mtime.
        // Callbacks fire from the background thread — synchronize externally.
        void Watch(const std::filesystem::path& dir, bool recursive, Callback cb);

        void Start();
        void Stop();

    private:
        void Poll();

        struct WatchEntry {
            std::filesystem::path Dir;
            bool                  Recursive;
            Callback              Cb;
        };

        std::vector<WatchEntry> m_Watches;
        // Track only files we've seen (not a full directory scan)
        std::unordered_map<std::string, std::filesystem::file_time_type> m_Tracked;
        std::thread m_Thread;
        std::atomic<bool> m_Running = false;
        std::mutex m_Mutex;

        void scanDir(const std::filesystem::path& dir, bool recursive,
                     const Callback& cb);
    };

    // Platform-specific: test if a file can be opened with exclusive-read
    // (no writer is still active).  Used by AssetWatcher probe.
    bool CanReadFileExclusive(const std::filesystem::path& path);

}
