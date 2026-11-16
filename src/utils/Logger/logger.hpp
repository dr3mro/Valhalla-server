/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file logger.hpp
 * @brief Drogon/Trantor logging bridged to an asynchronous spdlog logger.
 * @details Declares the Logger class, which owns one asynchronous spdlog logger
 *          and configures it at construction time. The logger can write to a
 *          rotating file, a coloured console, or both, based on the server
 *          configuration. Constructing it initialises spdlog's worker thread
 * pool, registers the logger by name, makes it spdlog's default and installs it
 *          as Trantor's spdlog backend. The log() method forwards a Trantor
 * record at the matching spdlog severity. A colour enum and several size/thread
 *          constants support the setup.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This class is the single logging hub for the whole process. Framework
 * messages from Drogon/Trantor, utility messages from Message and application
 * messages all end up in the same sinks with the same format. It is meant to be
 * created once as a process-wide singleton through Store; spdlog registration
 * is global, so two instances would clash. Without it there would be no
 * unified, non-blocking log output for the server.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::shared_ptr: https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - spdlog asynchronous logging:
 * https://github.com/gabime/spdlog/wiki/5.-Asynchronous-logging
 * - enum types: https://en.cppreference.com/w/cpp/language/enum
 * - static constexpr members:
 * https://en.cppreference.com/w/cpp/language/static#Static_data_members
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// Brings in trantor::Logger::LogLevel used by log().
#include <drogon/drogon.h>
// Provides spdlog::async_logger and the thread-pool support.
#include <spdlog/async.h>
// The core spdlog API (register_logger, set_default_logger, etc.).
#include <spdlog/spdlog.h>

// Provides fixed-size integer types such as std::uint8_t used by the colour
// enum.
#include <cstddef>
#include <memory>  // std::shared_ptr is used to own the logger.
// std::string is the message type and the colour-map value type.
#include <string>

// Logger routes Trantor log records through an asynchronous spdlog backend.
/**
 * @class Logger
 * @brief Routes Drogon/Trantor log records through an asynchronous spdlog
 * logger.
 * @details Owns one spdlog `async_logger` writing to a rotating file sink
 * and/or a colour console sink, selected from Configurator's ServerConfig.
 *          Construction initialises spdlog's shared thread pool, registers the
 *          logger by name, and installs it as both spdlog's default logger and
 *          Trantor's spdlog backend, so framework, utility and application
 *          records all use the same sink configuration.
 *
 *          Intended as a process-wide singleton created by Server through
 *          `Store`; spdlog registration is global, so more than one instance
 *          would try to register the same name twice. Logging itself is
 *          thread-safe via spdlog's sinks and worker pool.
 * @note The file path is `log_dir` concatenated with `log_file` from
 *       ServerConfig; rotation occurs at MAX_LOG_SIZE bytes across
 * MAX_LOG_FILES files.
 * @see Message, Server, Configurator
 */
class Logger  // Owns the process-wide spdlog logger; create only one instance.
{
 public:
  /**
   * @brief Builds, configures and globally registers the asynchronous logger.
   * @details Reads ServerConfig from Configurator (via Store), initialises
   * the spdlog thread pool (queue size THREADPOOL, THREADS workers), builds
   *          the console and rotating-file sinks with a shared pattern, adds
   *          only the sinks enabled by `log_to_console` / `log_to_file`, then
   *          registers the logger as "Project Valhalla" and enables Trantor's
   *          spdlog forwarding.
   * @post spdlog has a registered default logger and Trantor routes to it.
   * @warning On spdlog_ex (e.g. a duplicate registration) the error is
   * printed to stdout and construction succeeds with logger_ unset; later
   *          log() calls would then dereference a null pointer. Construct
   * once, before any component starts logging.
   */
  Logger();  // Constructor does all the setup and global registration.

  /**
   * @brief Forwards a Trantor log record at the matching spdlog severity.
   * @param[in] message Text to log.
   * @param[in] level Trantor severity; kDebug/kInfo/kWarn/kError/kFatal map
   * to debug/info/warn/error/critical respectively.
   * @note Unrecognised levels are silently ignored.
   */
  void log(const std::string& message,
      trantor::Logger::LogLevel
          level);  // Maps a Trantor level to the matching spdlog call.

 private:
  /**
   * @brief The shared async logger, also registered globally with spdlog.
   */
  std::shared_ptr<spdlog::async_logger>
      logger_;  // Shared ownership of the logger so spdlog and this class can
                // both hold it.
  /**
   * @brief Maximum size in bytes of a single log file before rotation.
   */
  static constexpr uint64_t MAX_LOG_SIZE = static_cast<uint64_t>(
      1024 * 1024 * 5);  // 5 MiB: when the active file reaches this size,
                         // spdlog starts a new one.
  /**
   * @brief Capacity of the spdlog asynchronous message queue.
   */
  static constexpr uint64_t THREADPOOL =
      8;  // Queue holds up to 8 pending messages before the overflow policy
          // kicks in.
  /**
   * @brief Number of spdlog worker threads draining the queue.
   */
  static constexpr uint64_t THREADS =
      2;  // Two background threads write messages out, so callers are not
          // blocked by disk I/O.
  /**
   * @brief Number of rotated log files retained on disk.
   */
  static constexpr uint64_t MAX_LOG_FILES =
      3;  // Keep at most three files; older ones are discarded as rotation
          // continues.

  /**
   * @brief ANSI colours used when formatting log patterns and messages.
   */
  enum Color : std::uint8_t  // Named colours stored compactly as 8-bit
                             // numbers; used to build ANSI escape sequences.
  {
    Red,      // Bright red, used for errors.
    Green,    // Bright green, used for success.
    Yellow,   // Bright yellow, used for warnings.
    Blue,     // Bold blue, used for informational messages.
    Magenta,  // Soft purple, used for debug messages.
    White,    // Bright white, used for general messages.
    Reset,    // Resets terminal colour back to normal.
    Cyan      // Bold cyan, used for optional context.
  };
  /**
   * @brief Returns the ANSI escape sequence for a log colour.
   * @param[in] color Colour to look up.
   * @return Escape sequence for `color`.
   * @throws std::out_of_range If `color` is absent from the internal map
   *         (all declared enumerators are present).
   */
  static std::string get_color(
      Color color);  // Looks up the terminal escape code for one of the
                     // colours above.
};
