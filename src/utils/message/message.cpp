#include "message.hpp"

#include <iostream>

#include "fmt/core.h"

using std::cout;

const std::unordered_map<Message::Level, std::string> Message::color_map = {
    {Level::Failure, "\033[1;31m"}, {Level::Error, "\033[1;31m"}, {Level::Warning, "\033[1;33m"},
    {Level::Success, "\033[1;32m"}, {Level::Info, "\033[1;34m"},  {Level::Debug, "\033[1;35m"},
};

void Message::FailureMessage(const std::string& status_message) { MessageImpl(status_message, Level::Failure); }

void Message::ErrorMessage(const std::string& status_message) { MessageImpl(status_message, Level::Error); }

void Message::WarningMessage(const std::string& status_message) { MessageImpl(status_message, Level::Warning); }

void Message::SuccessMessage(const std::string& status_message) { MessageImpl(status_message, Level::Success); }

void Message::InfoMessage(const std::string& status_message) { MessageImpl(status_message, Level::Info); }

void Message::DebugMessage(const std::string& status_message) { MessageImpl(status_message, Level::Debug); }

void Message::MessageImpl(const std::string& status_message, Level level)
{
    std::string message = fmt::format("{}{}{}", color_map.at(level), status_message, color_map.at(Level::Info));
    cout << message << '\n';
}
