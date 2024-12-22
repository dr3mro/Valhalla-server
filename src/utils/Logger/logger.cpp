#include "utils/Logger/logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "configurator/configurator.hpp"
#include "store/store.hpp"
// Custom Logger that inherits from crow::ILogHandler

Logger::Logger()
{
    try
    {
        auto configurator  = Store::getObject<Configurator>();
        auto server_config = configurator->get<Configurator::ServerConfig>();
        auto file_path     = fmt::format("{}{}", server_config.log_dir, server_config.log_file);

        // Initialize thread pool for async logging
        spdlog::init_thread_pool(THREADPOOL, THREADS);

        // Console sink setup with custom pattern
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern(fmt::format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v", get_color(Color::Magenta), get_color(Color::Reset),
            get_color(Color::Red), get_color(Color::Reset)));

        // File sink setup
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{}", file_path), MAX_LOG_SIZE, MAX_LOG_FILES);

        file_sink->set_pattern(fmt::format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v", get_color(Color::Magenta), get_color(Color::Reset),
            get_color(Color::Red), get_color(Color::Reset)));
        std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
        if (server_config.log_to_console)
        {
            sinks.push_back(console_sink);
        }

        if (server_config.log_to_file)
        {
            sinks.push_back(file_sink);
        }

        // Create the async logger with the thread pool and sinks
        logger_ = std::make_shared<spdlog::async_logger>(
            "Project Valhalla", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);

        // Register logger
        spdlog::register_logger(logger_);
        // Optional: set as the default logger
        spdlog::set_default_logger(logger_);
        trantor::Logger::enableSpdLog(logger_);
    }
    catch (const spdlog::spdlog_ex& e)
    {
        fmt::print("SPDLog initialization failed: {}", e.what());
    }
}
// Override the log function from crow::ILogHandler
void Logger::log(const std::string& message, trantor::Logger::LogLevel level)
{
    switch (level)
    {
        case trantor::Logger::LogLevel::kDebug:
            logger_->debug(message);
            break;
        case trantor::Logger::LogLevel::kInfo:
            logger_->info(message);
            break;
        case trantor::Logger::LogLevel::kWarn:
            logger_->warn(message);
            break;
        case trantor::Logger::LogLevel::kError:
            logger_->error(message);
            break;
        case trantor::Logger::LogLevel::kFatal:
            logger_->critical(message);
            break;
        default:
            break;
    }
}

std::string Logger::get_color(Color color)
{
    static const std::unordered_map<Logger::Color, std::string> color_map = {
        {Logger::Color::Red, "\033[1;91m"},          // Bright Red for errors
        {Logger::Color::Green, "\033[1;92m"},        // Bright Green for success
        {Logger::Color::Yellow, "\033[1;93m"},       // Bright Yellow for warnings
        {Logger::Color::Blue, "\033[1;34m"},         // Bold Blue for informational messages
        {Logger::Color::Magenta, "\033[38;5;140m"},  // Soft Purple for debug messages
        {Logger::Color::White, "\033[1;97m"},        // Bright White for general or unspecified messages
        {Logger::Color::Cyan, "\033[1;96m"},         // Bold Cyan for optional info or context
        {Logger::Color::Reset, "\033[0m"}            // Reset
    };
    return color_map.at(color);
}
