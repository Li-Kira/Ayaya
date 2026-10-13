#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Ayaya {

    // 供编辑器 Log 面板消费的日志条目 (level 为 spdlog::level::level_enum 数值)
    struct LogEntry {
        int         level = 2;     // trace=0 ... error=4 ...
        std::string category;      // logger 名 ("AYAYA" / "APP")
        std::string time;          // "HH:MM:SS"
        std::string message;
        uint64_t    sequence = 0;  // 单调递增序号，供面板跨帧稳定选中
    };

    class Log {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

        // 编辑器日志面板读取内存环形缓冲中的最近日志 (maxCount=0 返回全部)
        static std::vector<LogEntry> GetRecentEntries(size_t maxCount = 0);
        static void ClearEntries();

    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };

}

// 引擎内部使用的日志宏
#define AYAYA_CORE_TRACE(...)    ::Ayaya::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define AYAYA_CORE_INFO(...)     ::Ayaya::Log::GetCoreLogger()->info(__VA_ARGS__)
#define AYAYA_CORE_WARN(...)     ::Ayaya::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define AYAYA_CORE_ERROR(...)    ::Ayaya::Log::GetCoreLogger()->error(__VA_ARGS__)

// 客户端（Sandbox）使用的日志宏
#define AYAYA_TRACE(...)         ::Ayaya::Log::GetClientLogger()->trace(__VA_ARGS__)
#define AYAYA_INFO(...)          ::Ayaya::Log::GetClientLogger()->info(__VA_ARGS__)
#define AYAYA_WARN(...)          ::Ayaya::Log::GetClientLogger()->warn(__VA_ARGS__)
#define AYAYA_ERROR(...)         ::Ayaya::Log::GetClientLogger()->error(__VA_ARGS__)

// -----------------------------------------------------------------------------
// 断言宏 (Assert Macros)
// -----------------------------------------------------------------------------

// 1. 定义跨平台的“触发断点”指令
#ifdef _WIN32
    #define AYAYA_DEBUGBREAK() __debugbreak()
#else
    #include <csignal>
    #define AYAYA_DEBUGBREAK() raise(SIGTRAP)
#endif

// 2. 定义引擎核心断言
// 如果 x 为 false，打印错误信息并强行中断程序！
#define AYAYA_CORE_ASSERT(x, ...) { if(!(x)) { AYAYA_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); AYAYA_DEBUGBREAK(); } }

// 3. 定义客户端断言
#define AYAYA_ASSERT(x, ...) { if(!(x)) { AYAYA_ERROR("Assertion Failed: {0}", __VA_ARGS__); AYAYA_DEBUGBREAK(); } }