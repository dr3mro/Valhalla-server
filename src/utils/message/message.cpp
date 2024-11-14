#include "message.hpp"

#include <iostream>

#include "crow/logging.h"
#include "fmt/core.h"

using std::cout;

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

void Message::MessageImpl(const std::string& status_message, crow::LogLevel level)
{
    crow::logger(level) << fmt::format("{}{}{}", color_map.at(level), status_message, RESET);
}
