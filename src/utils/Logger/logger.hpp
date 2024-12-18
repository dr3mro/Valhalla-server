#pragma once

#include <drogon/drogon.h>
#include <spdlog/async.h>
#include <spdlog/spdlog.h>

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
    static constexpr uint64_t             MAX_LOG_SIZE  = 1024 * 1024 * 5;
    static constexpr uint64_t             THREADPOOL    = 8;
    static constexpr uint64_t             THREADS       = 2;
    static constexpr uint64_t             MAX_LOG_FILES = 3;
    enum Color
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
    const std::unordered_map<Logger::Color, std::string> color_map = {
        {Logger::Color::Red, "\033[1;91m"},          // Bright Red for errors
        {Logger::Color::Green, "\033[1;92m"},        // Bright Green for success
        {Logger::Color::Yellow, "\033[1;93m"},       // Bright Yellow for warnings
        {Logger::Color::Blue, "\033[1;34m"},         // Bold Blue for informational messages
        {Logger::Color::Magenta, "\033[38;5;140m"},  // Soft Purple for debug messages
        {Logger::Color::White, "\033[1;97m"},        // Bright White for general or unspecified messages
        {Logger::Color::Cyan, "\033[1;96m"},         // Bold Cyan for optional info or context
        {Logger::Color::Reset, "\033[0m"}            // Reset
    };
};