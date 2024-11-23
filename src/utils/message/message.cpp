#include "message.hpp"

#include <drogon/drogon.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <iostream>

using std::cout;

static char           CONFIG[] = "config";
static char           INIT[]   = "init";
static constexpr char RESET[]  = "\033[0m";  // Reset

const std::unordered_map<trantor::Logger::LogLevel, std::string> Message::color_map = {
    {trantor::Logger::LogLevel::kDebug, "\033[38;5;140m"},  // Soft Purple (for subtle debug messages)
    {trantor::Logger::LogLevel::kInfo, "\033[1;34m"},       // Bold Blue (clear and calm for info)
    {trantor::Logger::LogLevel::kWarn, "\033[1;93m"},       // Bright Yellow (attention-grabbing for warnings)
    {trantor::Logger::LogLevel::kError, "\033[1;91m"},      // Bright Red (strong and clear for errors)
    {trantor::Logger::LogLevel::kFatal, "\033[38;5;196m"}   // Vibrant Red (high intensity for critical messages)
};

void Message::DebugMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kDebug); }
void Message::InfoMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kInfo); }
void Message::WarningMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kWarn); }
void Message::ErrorMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kError); }
void Message::CriticalMessage(const std::string& status_message) { MessageImpl(status_message, trantor::Logger::LogLevel::kFatal); }
void Message::ConfMessage(const std::string& status_message) { MessageImpl(status_message, CONFIG); }
void Message::InitMessage(const std::string& status_message) { MessageImpl(status_message, INIT); }

void Message::MessageImpl(const std::string& status_message, trantor::Logger::LogLevel level)
{
    trantor::Logger(level).stream() << fmt::format("{}{}{}", color_map.at(level), status_message, RESET);
}
void Message::MessageImpl(const std::string& status_message, const std::string& type)
{
    fmt::print("{}{}{}{}{}\n", fmt::format(fg(fmt::color::indian_red), "[Project Valhalla]"),  // The server name in red
               fmt::format(fg(fmt::color::white), " ["),                                       // the left square bracket
               fmt::format("{}", fmt::format(fg(fmt::color::green), "{}", type)),              // The message type in green
               fmt::format(fg(fmt::color::white), "] "),                                       // the right square bracket
               status_message);                                                                // the messsage
}
