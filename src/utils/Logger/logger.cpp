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
        spdlog::init_thread_pool(32768, 2);

        // Console sink setup with custom pattern
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(static_cast<spdlog::level::level_enum>(server_config.debug_level));  // Set console level to warn
        console_sink->set_pattern(fmt::format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v", color_map.at(Color::Magenta),
                                              color_map.at(Color::Reset), color_map.at(Color::Red), color_map.at(Color::Reset)));

        // File sink setup
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{}", file_path.data()), 5 * 1024 * 1024, 3);

        file_sink->set_level(static_cast<spdlog::level::level_enum>(server_config.debug_level));  // Set file level to trace
        file_sink->set_pattern(fmt::format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v", color_map.at(Color::Magenta),
                                           color_map.at(Color::Reset), color_map.at(Color::Red), color_map.at(Color::Reset)));
        std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
        if (server_config.log_to_console)
            sinks.push_back(console_sink);

        if (server_config.log_to_file)
            sinks.push_back(file_sink);

        // Create the async logger with the thread pool and sinks
        logger_ = std::make_shared<spdlog::async_logger>("Project Valhalla", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                         spdlog::async_overflow_policy::overrun_oldest);

        // Set logger level to debug
        logger_->set_level(static_cast<spdlog::level::level_enum>(server_config.debug_level));
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
