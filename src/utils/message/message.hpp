/**
 * @file message.hpp
 * @brief Static helpers for coloured console/status logging.
 * @details Declares the Message class: a stateless facade with functions to log
 *          at debug, info, warning, error or fatal severity, plus ConfMessage
 * and InitMessage for startup notices. Severity messages go through
 *          trantor::Logger and therefore reach the configured Logger/spdlog
 *          sinks; the two startup notices print straight to the terminal with a
 *          "[Project Valhalla]" prefix. A small colour map and a
 * newline-flattening helper support the formatting.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the everyday logging interface used all over the code. Instead of
 * every component calling trantor or spdlog directly, they call
 * Message::InfoMessage, Message::ErrorMessage and so on. That centralises
 * colour choices and message formatting, and makes sure records flow into the
 * one logger configured at startup. ConfMessage/InitMessage give friendly,
 * clearly labelled startup output.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static member functions:
 * https://en.cppreference.com/w/cpp/language/static#Static_member_functions
 * - Function overloading (two MessageImpl versions):
 * https://en.cppreference.com/w/cpp/language/overload_resolution
 * - ANSI escape codes for colour:
 * https://en.wikipedia.org/wiki/ANSI_escape_code
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.
// trantor::Logger is the logging backend used by the severity helpers.
#include <trantor/utils/Logger.h>

#include <string>  // std::string is the message type passed to every helper.
/**
 * @class Message
 * @brief Stateless facade for emitting coloured status messages.
 * @details Severity helpers (Debug through Critical) write through
 *          trantor::Logger, so they flow into the configured Logger/spdlog
 *          sinks. ConfMessage and InitMessage are startup/status notices that
 *          instead print directly to stdout with a `[Project Valhalla]` prefix.
 *          All methods are static; instances carry no state.
 * @see Logger
 */
class Message  // A class used only through static functions; instances hold no
               // data.
{
 public:
  /**
   * @brief Default construction; Message is stateless.
   */
  Message() = default;  // Default constructor exists but is rarely used since
                        // all methods are static.
  /**
   * @brief Virtual destructor; there are no owned resources to release.
   */
  virtual ~Message() = default;  // Virtual so derived classes could be
                                 // destroyed safely; nothing to clean up here.
  /**
   * @brief Copy construction; the type is stateless.
   */
  Message(const Message& other) =
      default;  // Copying an object with no data is harmless.
  /**
   * @brief Move construction; the type is stateless.
   */
  Message(Message&& other) =
      default;  // Moving an object with no data is harmless.
  /**
   * @brief Copy assignment; the type is stateless.
   */
  Message& operator=(const Message& other) =
      default;  // Assignment copies nothing.
  /**
   * @brief Move assignment; the type is stateless.
   */
  Message& operator=(Message&& other) =
      default;  // Move assignment moves nothing.

  /**
   * @brief Logs a message at debug severity.
   * @param[in] status_message Text to log.
   */
  static void DebugMessage(
      const std::string& status_message);  // Verbose detail for developers.
  /**
   * @brief Logs a message at info severity.
   * @param[in] status_message Text to log.
   */
  static void InfoMessage(
      const std::string& status_message);  // Normal, everyday information.
  /**
   * @brief Logs a message at warning severity.
   * @param[in] status_message Text to log.
   */
  static void WarningMessage(
      const std::string&
          status_message);  // Something unexpected but not fatal.
  /**
   * @brief Logs a message at error severity.
   * @param[in] status_message Text to log.
   */
  static void ErrorMessage(
      const std::string& status_message);  // A failure that was handled.
  /**
   * @brief Logs a message at fatal severity (used for critical errors).
   * @param[in] status_message Text to log.
   */
  static void CriticalMessage(
      const std::string&
          status_message);  // The most severe level, for critical failures.
  /**
   * @brief Prints a configuration notice directly to stdout.
   * @param[in] status_message Text to print.
   */
  static void ConfMessage(
      const std::string& status_message);  // A startup notice tagged "config".
  /**
   * @brief Prints an initialisation notice directly to stdout.
   * @param[in] status_message Text to print.
   */
  static void InitMessage(
      const std::string& status_message);  // A startup notice tagged "init".
  /**
   * @brief Returns the ANSI colour associated with a Trantor log level.
   * @param[in] level Severity to colourise.
   * @return ANSI escape sequence for `level`.
   * @throws std::out_of_range For a level absent from the internal map.
   */
  static std::string get_color(
      trantor::Logger::LogLevel level);  // Looks up the escape code that
                                         // gives each severity its colour.

 private:
  /**
   * @brief Formats and emits a coloured message through trantor::Logger.
   * @param[in] _status_message Text to log, with newlines flattened.
   * @param[in] _level Severity of the record.
   */
  static void MessageImpl(
      const std::string& _status_message,
      trantor::Logger::LogLevel
          _level);  // Shared implementation for the five severity helpers.
  /**
   * @brief Prints a tagged notice to stdout via fmt::print.
   * @param[in] _status_message Text to print, with newlines flattened.
   * @param[in] _type Tag shown in brackets (e.g. "config", "init").
   */
  static void MessageImpl(
      const std::string& _status_message,
      const std::string&
          _type);  // Shared implementation for the two startup notices.
  /**
   * @brief Replaces newline characters with spaces for single-line output.
   * @param[in] _str Original text.
   * @return Text with all newline characters replaced by spaces.
   */
  static std::string removeNewLine(
      const std::string& _str);  // Keeps every record on one line so log
                                 // prefixes stay meaningful.
};
