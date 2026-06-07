#include "ayapch.h"
#include "FileWatcher.hpp"
#include "Core/Log.hpp"

#ifdef AYAYA_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace Ayaya {

    void FileWatcher::Watch(const std::filesystem::path& dir, bool recursive,
                            Callback cb) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Watches.push_back({dir, recursive, std::move(cb)});
    }

    void FileWatcher::Start() {
        if (m_Running) return;
        m_Running = true;
        m_Thread = std::thread(&FileWatcher::Poll, this);
    }

    void FileWatcher::Stop() {
        m_Running = false;
        if (m_Thread.joinable()) m_Thread.join();
    }

    void FileWatcher::scanDir(const std::filesystem::path& dir, bool recursive,
                               const Callback& cb) {
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.is_directory()) {
                if (recursive) scanDir(entry.path(), recursive, cb);
                continue;
            }
            if (!entry.is_regular_file()) continue;

            auto pathStr = entry.path().string();
            auto current = entry.last_write_time(ec);
            if (ec) continue;

            auto it = m_Tracked.find(pathStr);
            if (it == m_Tracked.end()) {
                // New file
                m_Tracked[pathStr] = current;
                FileEvent ev{entry.path(), FileEventType::Added,
                    std::chrono::steady_clock::now()};
                cb(ev);
            } else if (it->second < current) {
                it->second = current;
                FileEvent ev{entry.path(), FileEventType::Modified,
                    std::chrono::steady_clock::now()};
                cb(ev);
            }
        }
    }

    void FileWatcher::Poll() {
        using namespace std::chrono_literals;
        const auto kInterval = std::chrono::milliseconds(500);

        // First poll: discover all files
        bool firstPoll = true;

        while (m_Running) {
            if (firstPoll) {
                // Initial scan with a short delay to let the app start
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                firstPoll = false;
            } else {
                std::this_thread::sleep_for(kInterval);
            }

            std::lock_guard<std::mutex> lock(m_Mutex);

            for (auto& watch : m_Watches) {
                if (!std::filesystem::exists(watch.Dir)) continue;
                scanDir(watch.Dir, watch.Recursive, watch.Cb);
            }

            // Purge tracked entries for removed files
            auto it = m_Tracked.begin();
            while (it != m_Tracked.end()) {
                if (!std::filesystem::exists(it->first)) {
                    FileEvent ev{it->first, FileEventType::Removed,
                        std::chrono::steady_clock::now()};
                    for (auto& watch : m_Watches)
                        watch.Cb(ev);
                    it = m_Tracked.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // ---- Platform-specific exclusive-read probe ----
    bool CanReadFileExclusive(const std::filesystem::path& path) {
#ifdef AYAYA_PLATFORM_WINDOWS
        // Open with FILE_SHARE_READ only — denies writes while we open it.
        // If an external process is still writing, CreateFileW fails.
        HANDLE h = CreateFileW(path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,              // allow other readers, block writers
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        CloseHandle(h);
        return true;
#else
        // macOS / Linux: try to open — if the file is being written,
        // the OS may return a partial or locked result.
        // A robust implementation would use fcntl(F_WRLCK) on macOS.
        std::ifstream f(path, std::ios::binary);
        return f.good();
#endif
    }

}
