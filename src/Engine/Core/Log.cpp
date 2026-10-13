#include "Log.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/base_sink.h>
#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <mutex>

namespace Ayaya {

    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

    namespace {

        std::atomic<uint64_t> s_SeqCounter{0};  // 日志条目单调序号

        // 去除 ANSI 转义序列 (如 "\x1b[38;2;255;150;200m"、"\x1b[0m")，避免日志面板显示乱码
        std::string StripAnsi(const char* data, size_t len) {
            std::string out;
            out.reserve(len);
            for (size_t i = 0; i < len; ++i) {
                if (data[i] == '\x1b' && i + 1 < len && data[i + 1] == '[') {
                    i += 2;  // 跳过 ESC [
                    while (i < len && !(data[i] >= '@' && data[i] <= '~')) ++i;  // 跳到终止字节
                    // for 循环的 ++i 会再越过终止字节
                } else {
                    out += data[i];
                }
            }
            return out;
        }

        // ==========================================
        // EditorLogSink — 捕获引擎 + 客户端日志到内存环形缓冲，
        // 供编辑器 Log 面板渲染。继承 base_sink<std::mutex>：
        // base_sink::log() 已在 mutex_ 锁内调用 sink_it_()，因此
        // sink_it_ 里不再加锁；公共 Snapshot()/Clear() 自行加锁。
        // ==========================================
        class EditorLogSink : public spdlog::sinks::base_sink<std::mutex> {
        protected:
            void sink_it_(const spdlog::details::log_msg& msg) override {
                // 时间 → "HH:MM:SS"
                auto tt = std::chrono::system_clock::to_time_t(msg.time);
                std::tm tm{};
#ifdef _WIN32
                localtime_s(&tm, &tt);
#else
                localtime_r(&tt, &tm);
#endif
                char timeBuf[16];
                std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tm);

                entries_.push_back({
                    static_cast<int>(msg.level),
                    std::string(msg.logger_name.data(), msg.logger_name.size()),
                    std::string(timeBuf),
                    StripAnsi(msg.payload.data(), msg.payload.size()),
                    s_SeqCounter.fetch_add(1, std::memory_order_relaxed)
                });
                if (entries_.size() > kMaxEntries) entries_.pop_front();
            }
            void flush_() override {}

        public:
            std::vector<LogEntry> Snapshot(size_t maxCount) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::vector<LogEntry> out;
                size_t start = (maxCount == 0 || entries_.size() <= maxCount)
                                   ? 0 : entries_.size() - maxCount;
                out.reserve(entries_.size() - start);
                for (size_t i = start; i < entries_.size(); ++i)
                    out.push_back(entries_[i]);
                return out;
            }

            void Clear() {
                std::lock_guard<std::mutex> lock(mutex_);
                entries_.clear();
            }

        private:
            std::deque<LogEntry> entries_;
            static constexpr size_t kMaxEntries = 2048;
        };

        std::shared_ptr<EditorLogSink> s_EditorSink;

    } // anonymous namespace

    void Log::Init() {
        // 设置输出格式: [时间] 日志名: 内容
        spdlog::set_pattern("%^[%T] %n: %v%$");

        s_CoreLogger = spdlog::stdout_color_mt("AYAYA");
        s_CoreLogger->set_level(spdlog::level::trace);

        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::trace);

        // 编辑器内存日志捕获 (供 Log 面板)
        s_EditorSink = std::make_shared<EditorLogSink>();
        s_CoreLogger->sinks().push_back(s_EditorSink);
        s_ClientLogger->sinks().push_back(s_EditorSink);
    }

    std::vector<LogEntry> Log::GetRecentEntries(size_t maxCount) {
        return s_EditorSink ? s_EditorSink->Snapshot(maxCount) : std::vector<LogEntry>{};
    }

    void Log::ClearEntries() {
        if (s_EditorSink) s_EditorSink->Clear();
    }

}