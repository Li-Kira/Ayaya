#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Ayaya {

    class Log {
    public:
        static void Init();

        inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

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