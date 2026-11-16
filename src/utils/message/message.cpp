/**
 * @file message.cpp
 * @brief Implementation for message.
 * @details Implements the Message facade. The five severity helpers forward to
 * a private MessageImpl that writes a coloured, single-line record through
 *          trantor::Logger. The two startup helpers use a second MessageImpl
 * that prints directly to stdout. get_color holds the severity-to-colour map,
 *          and removeNewLine flattens embedded newlines.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the implementation behind nearly every log line the server produces.
 * Code throughout the project calls Message::InfoMessage/ErrorMessage/etc.;
 * those calls end up here, get coloured and cleaned, and flow into the
 * configured logger. Keeping the formatting in one place means all log output
 * looks the same and startup banners are clearly distinguished from runtime
 * records.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Function overloading:
 * https://en.cppreference.com/w/cpp/language/overload_resolution
 * - std::unordered_map:
 * https://en.cppreference.com/w/cpp/container/unordered_map
 * - std::ranges algorithms (std::ranges::replace):
 * https://en.cppreference.com/w/cpp/algorithm/ranges/replace
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The declarations of all Message functions implemented here.
#include "message.hpp"

// The Drogon framework headers used alongside the logger backend.
#include <drogon/drogon.h>
#include <fmt/color.h>   // fmt's colour helpers (fg) for the terminal prefix.
#include <fmt/core.h>    // Core fmt formatting primitives.
#include <fmt/format.h>  // fmt::format is used to build the coloured strings.
// trantor::Logger is the backend that receives the records.
#include <trantor/utils/Logger.h>

#include <algorithm>  // Provides std::ranges::replace used by removeNewLine.
#include <string>     // std::string operations used throughout.
#include <unordered_map>  // The static colour map keyed by log level.

void Message::DebugMessage(const std::string& status_message)
{
  MessageImpl(status_message, trantor::Logger::LogLevel::kDebug);
}  // Forwards text with the debug level.
void Message::InfoMessage(const std::string& status_message)
{
  MessageImpl(status_message, trantor::Logger::LogLevel::kInfo);
}  // Forwards text with the info level.
void Message::WarningMessage(const std::string& status_message)
{
  MessageImpl(status_message, trantor::Logger::LogLevel::kWarn);
}  // Forwards text with the warning level.
void Message::ErrorMessage(const std::string& status_message)
{
  MessageImpl(status_message, trantor::Logger::LogLevel::kError);
}  // Forwards text with the error level.
void Message::CriticalMessage(const std::string& status_message)
{
  MessageImpl(status_message, trantor::Logger::LogLevel::kFatal);
}  // Forwards text with the fatal level.
void Message::ConfMessage(const std::string& status_message)
{
  MessageImpl(status_message, "config");
}  // Startup notice tagged "config", printed to stdout.
void Message::InitMessage(const std::string& status_message)
{
  MessageImpl(status_message, "init");
}  // Startup notice tagged "init", printed to stdout.

std::string Message::get_color(
    trantor::Logger::LogLevel level)  // Takes the severity and returns the
                                      // matching ANSI escape sequence.
{
  static const std::unordered_map<trantor::Logger::LogLevel, std::string>
      color_map =
          {// A read-only map built once, pairing each level with its colour.
           {trantor::Logger::LogLevel::kTrace,
            "\033[38;5;140m"},  // Soft Purple (for subtle debug messages)
           {trantor::Logger::LogLevel::kDebug,
            "\033[38;5;140m"},  // Soft Purple (for subtle debug messages)
           {trantor::Logger::LogLevel::kInfo,
            "\033[1;34m"},  // Bold Blue (clear and calm for info)
           {trantor::Logger::LogLevel::kWarn,
            "\033[1;93m"},  // Bright Yellow (attention-grabbing for
                            // warnings)
           {trantor::Logger::LogLevel::kError,
            "\033[1;91m"},  // Bright Red (strong and clear for errors)
                            // messages)
           {trantor::Logger::LogLevel::kFatal, "\033[38;5;196m"}};
  return color_map.at(level);  // .at looks up the level and throws
                               // std::out_of_range if it is somehow missing.
}

void Message::MessageImpl(
    const std::string& _status_message,
    trantor::Logger::LogLevel
        _level)  // Implementation shared by the severity helpers.
{
  // Route through Trantor so these records reach the configured spdlog sinks.
  trantor::Logger(_level).stream()
      << fmt::format("{}{}{}",
                     get_color(_level),
                     removeNewLine(_status_message),
                     "\033[0m");  // Builds "colour + cleaned text + reset" and
                                  // streams it into the trantor log at the
                                  // chosen level.
}
void Message::MessageImpl(
    const std::string& _status_message,
    const std::string&
        _type) /*NOLINT */  // Implementation shared by the two startup notices.
{
  // Startup notices bypass the logger and go straight to stdout with a fixed
  // prefix.
  fmt::print("{}{}{}{}{}\n",  // Prints five coloured pieces followed by a
                              // newline.
             fmt::format(fg(fmt::color::indian_red),  // Start the first piece
                                                      // in red.
                         "[Project Valhalla]"),       // The server name in red
             fmt::format(fg(fmt::color::white),
                         " ["),  // the left square bracket
             fmt::format("{}",
                         fmt::format(fg(fmt::color::green),
                                     "{}",
                                     _type)),  // The message type in green
             fmt::format(fg(fmt::color::white),
                         "] "),                // the right square bracket
             removeNewLine(_status_message));  // The message
}

std::string Message::removeNewLine(
    const std::string& _str)  // Takes text that might span lines.
{
  // Keep each record on a single line so log prefixes stay meaningful.
  std::string status_message =
      _str;  // Work on a copy so the caller's original string is untouched.
  std::ranges::replace(status_message,
                       '\n',
                       ' ');  // Replace every newline with a space to flatten
                              // multi-line messages.
  return status_message;      // Returns the single-line version for
                              // logging/printing.
}
