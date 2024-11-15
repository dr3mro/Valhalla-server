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
        console_sink->set_level(spdlog::level::debug);  // Set console level to warn
        console_sink->set_pattern(fmt::format("{}[Project Valhalla]{} [%^%l%$] %v", color_map.at(Color::Red), color_map.at(Color::Reset)));

        // File sink setup
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fmt::format("{}", file_path.data()), 5 * 1024 * 1024, 3);
        file_sink->set_level(spdlog::level::debug);  // Set file level to trace

        std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks;
        if (server_config.log_to_console)
            sinks.push_back(console_sink);

        if (server_config.log_to_file)
            sinks.push_back(file_sink);

        // Create the async logger with the thread pool and sinks
        logger = std::make_shared<spdlog::async_logger>("Project Valhalla", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                        spdlog::async_overflow_policy::overrun_oldest);

        // Set logger level to debug
        logger->set_level(spdlog::level::debug);

        // Register logger
        spdlog::register_logger(logger);

        // Optional: set as the default logger
        spdlog::set_default_logger(logger);
        crow::logger::setHandler(this);
    }
    catch (const spdlog::spdlog_ex& e)
    {
        fmt::print("SPDLog initialization failed: {}", e.what());
    }
}
// Override the log function from crow::ILogHandler
void Logger::log(std::string message, crow::LogLevel level)
{
    switch (level)
    {
        case crow::LogLevel::DEBUG:
            logger->debug(message);
            break;
        case crow::LogLevel::INFO:
            logger->info(message);
            break;
        case crow::LogLevel::WARNING:
            logger->warn(message);
            break;
        case crow::LogLevel::ERROR:
            logger->error(message);
            break;
        case crow::LogLevel::CRITICAL:
            logger->critical(message);
            break;
        default:
            break;
    }
}
