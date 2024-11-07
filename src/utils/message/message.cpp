#include "message.hpp"

#include <iostream>

#include "fmt/core.h"

using std::cout;

const std::unordered_map<Message::Level, std::string> Message::color_map = {
    {Level::Fatal, "\033[1;91m"},    // Bright Red
    {Level::Failure, "\033[31m"},    // Dark Red
    {Level::Error, "\033[1;31m"},    // Bold Red
    {Level::Warning, "\033[1;33m"},  // Bold Yellow
    {Level::Success, "\033[1;32m"},  // Bold Green
    {Level::Info, "\033[1;36m"},     // Bold Cyan
    {Level::Debug, "\033[1;35m"},    // Bold Magenta
    {Level::Reset, "\033[0m"}        // Reset
};

void Message::FatalMessage(const std::string& status_message) { MessageImpl(status_message, Level::Fatal); }

void Message::FailureMessage(const std::string& status_message) { MessageImpl(status_message, Level::Failure); }

void Message::ErrorMessage(const std::string& status_message) { MessageImpl(status_message, Level::Error); }

void Message::WarningMessage(const std::string& status_message) { MessageImpl(status_message, Level::Warning); }

void Message::SuccessMessage(const std::string& status_message) { MessageImpl(status_message, Level::Success); }

void Message::InfoMessage(const std::string& status_message) { MessageImpl(status_message, Level::Info); }

void Message::DebugMessage(const std::string& status_message) { MessageImpl(status_message, Level::Debug); }

void Message::MessageImpl(const std::string& status_message, Level level)
{
    fmt::print("{}{}{}\n", color_map.at(level), status_message, color_map.at(Level::Reset));
}
