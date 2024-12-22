#pragma once

#include <drogon/drogon.h>
#include <spdlog/async.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <memory>
#include <string>

// Custom Logger that inherits from crow::ILogHandler
class Logger
{
   public:
    Logger();
    void log(const std::string& message, trantor::Logger::LogLevel level);

   private:
    std::shared_ptr<spdlog::async_logger> logger_;
    static constexpr uint64_t             MAX_LOG_SIZE  = static_cast<uint64_t>(1024 * 1024 * 5);
    static constexpr uint64_t             THREADPOOL    = 8;
    static constexpr uint64_t             THREADS       = 2;
    static constexpr uint64_t             MAX_LOG_FILES = 3;

    enum Color : std::uint8_t
    {
        Red,
        Green,
        Yellow,
        Blue,
        Magenta,
        White,
        Reset,
        Cyan
    };
    static std::string get_color(Color color);
};
