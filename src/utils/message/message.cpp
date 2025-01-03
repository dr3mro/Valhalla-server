#include "message.hpp"

#include <drogon/drogon.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <trantor/utils/Logger.h>

#include <algorithm>
#include <string>
#include <unordered_map>

void Message::DebugMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kDebug); }
void Message::InfoMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kInfo); }
void Message::WarningMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kWarn); }
void Message::ErrorMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kError); }
void Message::CriticalMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kFatal); }
void Message::ConfMessage(const std::string& status_message) { MessageImpl(status_message, "config"); }
void Message::InitMessage(const std::string& status_message) { MessageImpl(status_message, "init"); }

std::string Message::get_color(trantor::Logger::LogLevel level)
{
    static const std::unordered_map<trantor::Logger::LogLevel, std::string> color_map = {
        {trantor::Logger::LogLevel::kTrace, "\033[38;5;140m"},  // Soft Purple (for subtle debug messages)
        {trantor::Logger::LogLevel::kDebug, "\033[38;5;140m"},  // Soft Purple (for subtle debug messages)
        {trantor::Logger::LogLevel::kInfo, "\033[1;34m"},       // Bold Blue (clear and calm for info)
        {trantor::Logger::LogLevel::kWarn, "\033[1;93m"},       // Bright Yellow (attention-grabbing for warnings)
        {trantor::Logger::LogLevel::kError, "\033[1;91m"},      // Bright Red (strong and clear for errors)
        {trantor::Logger::LogLevel::kFatal, "\033[38;5;196m"}   // messages)
    };
    return color_map.at(level);
}

void Message::MessageImpl(const std::string& _status_message, trantor::Logger::LogLevel _level)
{
    trantor::Logger(_level).stream() << fmt::format("{}{}{}", get_color(_level), removeNewLine(_status_message), "\033[0m");
}
void Message::MessageImpl(const std::string& _status_message, const std::string& _type) /*NOLINT */
{
    fmt::print("{}{}{}{}{}\n",
        fmt::format(fg(fmt::color::indian_red),
            "[Project Valhalla]"),                 // The server name in red
        fmt::format(fg(fmt::color::white), " ["),  // the left square bracket
        fmt::format("{}", fmt::format(fg(fmt::color::green), "{}",
                              _type)),             // The message type in green
        fmt::format(fg(fmt::color::white), "] "),  // the right square bracket
        removeNewLine(_status_message));           // The message
}

std::string Message::removeNewLine(const std::string& _str)
{
    std::string status_message = _str;
    std::ranges::replace(status_message, '\n', ' ');
    return status_message;
}
