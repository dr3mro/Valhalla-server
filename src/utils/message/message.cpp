#include "message.hpp"

#include <crow/logging.h>
#include <fmt/color.h>
#include <fmt/format.h>

#include <iostream>

using std::cout;

static char           CONFIG[] = "config";
static char           INIT[]   = "init";
static constexpr char RESET[]  = "\033[0m";  // Reset

const std::unordered_map<crow::LogLevel, std::string> Message::color_map = {
    {crow::LogLevel::Debug, "\033[38;5;140m"},    // Soft Purple (for subtle debug messages)
    {crow::LogLevel::Info, "\033[1;34m"},         // Bold Blue (clear and calm for info)
    {crow::LogLevel::Warning, "\033[1;93m"},      // Bright Yellow (attention-grabbing for warnings)
    {crow::LogLevel::Error, "\033[1;91m"},        // Bright Red (strong and clear for errors)
    {crow::LogLevel::Critical, "\033[38;5;196m"}  // Vibrant Red (high intensity for critical messages)
};

void Message::DebugMessage(const std::string& status_message) { MessageImpl(status_message, crow::LogLevel::Debug); }
void Message::InfoMessage(const std::string& status_message) { MessageImpl(status_message, crow::LogLevel::Info); }
void Message::WarningMessage(const std::string& status_message) { MessageImpl(status_message, crow::LogLevel::Warning); }
void Message::ErrorMessage(const std::string& status_message) { MessageImpl(status_message, crow::LogLevel::Error); }
void Message::CriticalMessage(const std::string& status_message) { MessageImpl(status_message, crow::LogLevel::Critical); }
void Message::ConfMessage(const std::string& status_message) { MessageImpl(status_message, CONFIG); }
void Message::InitMessage(const std::string& status_message) { MessageImpl(status_message, INIT); }

void Message::MessageImpl(const std::string& status_message, crow::LogLevel level)
{
    crow::logger(level) << fmt::format("{}{}{}", color_map.at(level), status_message, RESET);
}
void Message::MessageImpl(const std::string& status_message, const std::string& type)
{
    fmt::print("{}{}{}{}{}\n", fmt::format(fg(fmt::color::indian_red), "[Project Valhalla]"),  // The server name in red
               fmt::format(fg(fmt::color::white), " ["),                                       // the left square bracket
               fmt::format("{}", fmt::format(fg(fmt::color::green), "{}", type)),              // The message type in green
               fmt::format(fg(fmt::color::white), "] "),                                       // the right square bracket
               status_message);                                                                // the messsage
}
