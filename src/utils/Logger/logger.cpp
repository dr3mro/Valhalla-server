/**
 * @file logger.cpp
 * @brief Implementation for logger.
 * @details Implements Logger. The constructor reads the server configuration,
 *          initialises spdlog's async thread pool, builds a coloured console
 * sink and a rotating file sink with the same pattern, enables only the sinks
 *          the config asks for, then registers the logger and wires it into
 *          Trantor. log() maps a Trantor severity to the matching spdlog call,
 *          and get_color returns the ANSI escape code for each colour name.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where logging is actually set up and driven. Every log call in the
 * process ultimately reaches these sinks, so the chosen format, file-rotation
 * limits and non-blocking behaviour are decided here. Doing the setup once in
 * the constructor keeps configuration in one place and makes logging fast
 * (messages are queued and written by background threads). Without it,
 * Drogon/Trantor would log with default settings and there would be no unified
 * file output.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - spdlog sinks: https://github.com/gabime/spdlog/wiki/3.-Custom-sinks
 * - std::make_shared: https://en.cppreference.com/w/cpp/memory/make_shared
 * - std::unordered_map:
 * https://en.cppreference.com/w/cpp/container/unordered_map
 * - Exceptions / try-catch: https://en.cppreference.com/w/cpp/error/try_catch
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include "utils/Logger/logger.hpp"  // Declarations of Logger and its constants.

// The sink that writes to a file and rotates it when it grows too large.
#include <spdlog/sinks/rotating_file_sink.h>
// The sink that writes coloured output to the console.
#include <spdlog/sinks/stdout_color_sinks.h>

// Provides Configurator::ServerConfig with the log settings.
#include "configurator/configurator.hpp"
#include "store/store.hpp"  // Store locates the shared Configurator instance.
// Logger backed by asynchronous spdlog; not a crow handler.

// Constructor performs all logger setup and registration.
Logger::Logger() {
  try {
    auto configurator =
        Store::getObject<Configurator>();  // Fetch the process-wide
                                           // Configurator from the service
                                           // locator.
    auto server_config =
        configurator
            ->get<Configurator::ServerConfig>();  // Read the server settings,
                                                  // including log options.
    // The sink receives a full path assembled from two independent config
    // keys.
    auto file_path =
        fmt::format("{}{}",
                    server_config.log_dir,
                    server_config.log_file);  // Join directory and filename
                                              // into one path.

    // Initialize thread pool for async logging
    spdlog::init_thread_pool(THREADPOOL,
                             THREADS);  // Create the shared queue and worker
                                        // threads used by async loggers.

    // Console sink setup with custom pattern
    auto console_sink = std::make_shared<
        spdlog::sinks::stdout_color_sink_mt>();  // A thread-safe coloured
                                                 // console sink (the _mt
                                                 // means multi-threaded).
    console_sink->set_pattern(
        fmt::
            format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v",
                   get_color(Color::Magenta),
                   get_color(Color::Reset),
                   get_color(Color::Red),
                   get_color(Color::Reset)));  // Sets the line format:
                                               // timestamp, name, severity
                                               // (coloured) and message.

    // File sink setup
    auto file_sink = std::make_shared<
        spdlog::sinks::
            rotating_file_sink_mt>(fmt::format("{}", file_path),
                                   MAX_LOG_SIZE,
                                   MAX_LOG_FILES);  // A thread-safe sink that
                                                    // rotates the log file at
                                                    // MAX_LOG_SIZE, keeping
                                                    // MAX_LOG_FILES files.

    file_sink->set_pattern(
        fmt::
            format("{}[%Y-%m-%d %H:%M:%S]{} {}[Project Valhalla]{} %^[%l]%$ %v",
                   get_color(Color::Magenta),
                   get_color(Color::Reset),
                   get_color(Color::Red),
                   get_color(
                       Color::Reset)));  // Give the file the same layout as the
                                         // console for consistency.
    std::vector<std::shared_ptr<spdlog::sinks::sink>>
        sinks;  // Holds the sinks that are actually enabled by
                // configuration.
    // Only include the console sink when
    // the config asks for it.
    if (server_config.log_to_console) {
      sinks.push_back(
          console_sink);  // Add the console sink to the enabled set.
    }

    // Only include the file sink when the
    // config asks for it.
    if (server_config.log_to_file) {
      sinks.push_back(
          file_sink);  // Add the rotating file sink to the enabled set.
    }

    // Create the async logger with the thread pool and sinks.
    // overrun_oldest keeps logging non-blocking under load: if the queue is
    // full, the oldest queued message is dropped rather than stalling the
    // calling thread.
    logger_ = std::make_shared<
        spdlog::async_logger>("Project Valhalla",
                              sinks.begin(),
                              sinks.end(),
                              spdlog::thread_pool(),
                              spdlog::async_overflow_policy::
                                  overrun_oldest);  // Builds the named async
                                                    // logger over the chosen
                                                    // sinks and shared pool.

    // Register logger
    spdlog::register_logger(logger_);  // Adds the logger to spdlog's global
                                       // registry under its name.
    // Optional: set as the default logger
    spdlog::set_default_logger(
        logger_);  // Makes this the logger used by spdlog's free functions.
    // Tells Trantor to forward its own
    // records to this logger too.
    // Catches spdlog setup errors, notably
    // a duplicate registration.
    trantor::Logger::enableSpdLog(logger_);
  } catch (const spdlog::spdlog_ex& e) {
    // Report via stdout, not the logger being constructed, to avoid
    // depending on the very object whose initialisation failed.
    fmt::print("SPDLog initialization failed: {}",
               e.what());  // Print the error directly because the logger is not
                           // usable yet.
  }
}
// Receives Trantor log records and forwards them to the spdlog backend.
// Called for each Trantor
// record that needs logging.
void Logger::log(const std::string& message, trantor::Logger::LogLevel level) {
  // Unknown or out-of-range levels fall through the default branch and are
  // dropped.
  // Choose the spdlog severity that matches the Trantor level.
  switch (level) {
    case trantor::Logger::LogLevel::kDebug:  // Debug level.
      logger_->debug(message);              // Forward as a spdlog debug record.
      break;                                // Leave the switch.
    case trantor::Logger::LogLevel::kInfo:  // Info level.
      logger_->info(message);               // Forward as a spdlog info record.
      break;                                // Leave the switch.
    case trantor::Logger::LogLevel::kWarn:  // Warning level.
      logger_->warn(message);  // Forward as a spdlog warning record.
      break;                   // Leave the switch.
    case trantor::Logger::LogLevel::kError:  // Error level.
      logger_->error(message);  // Forward as a spdlog error record.
      break;                    // Leave the switch.
    case trantor::Logger::LogLevel::kFatal:  // Fatal level.
      logger_->critical(message);  // Forward as a spdlog critical record
                                   // (spdlog's name for fatal).
      break;                       // Leave the switch.
    default:                       // Any level not listed above.
      break;                       // Ignore it silently.
  }
}

// Looks up the escape sequence for one colour.
std::string Logger::get_color(Color color) {
  static const std::unordered_map<Logger::Color, std::string> color_map = {
      // A read-only map built once from colour names to ANSI codes.
      {Logger::Color::Red, "\033[1;91m"},     // Bright Red for errors
      {Logger::Color::Green, "\033[1;92m"},   // Bright Green for success
      {Logger::Color::Yellow, "\033[1;93m"},  // Bright Yellow for warnings
      {Logger::Color::Blue,
       "\033[1;34m"},  // Bold Blue for informational messages
      {Logger::Color::Magenta,
       "\033[38;5;140m"},  // Soft Purple for debug messages
      {Logger::Color::White,
       "\033[1;97m"},  // Bright White for general or unspecified messages
      {Logger::Color::Cyan,
       "\033[1;96m"},  // Bold Cyan for optional info or context
      {Logger::Color::Reset, "\033[0m"}  // Reset
  };
  return color_map.at(color);  // .at returns the code and throws
                               // std::out_of_range if the colour is missing.
}
