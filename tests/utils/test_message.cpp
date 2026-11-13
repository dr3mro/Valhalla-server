#include <trantor/utils/Logger.h>

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "utils/message/message.hpp"

// Message is a thin logging facade over trantor's logger. Without a running
// server its only observable contract is "does not throw and always accepts a
// message" - the log output itself goes to the console/file sink.
TEST_CASE("Message logging levels accept messages without throwing", "[utils][message]")
{
    CHECK_NOTHROW(Message::DebugMessage("debug"));
    CHECK_NOTHROW(Message::InfoMessage("info"));
    CHECK_NOTHROW(Message::WarningMessage("warning"));
    CHECK_NOTHROW(Message::ErrorMessage("error"));
    CHECK_NOTHROW(Message::CriticalMessage("critical"));
    CHECK_NOTHROW(Message::ConfMessage("config"));
    CHECK_NOTHROW(Message::InitMessage("init"));
}

TEST_CASE("Message logging is tolerant of awkward strings", "[utils][message]")
{
    CHECK_NOTHROW(Message::InfoMessage(""));
    CHECK_NOTHROW(Message::InfoMessage("line1\nline2\twith\ttabs"));
    CHECK_NOTHROW(Message::InfoMessage(std::string(2000, 'x')));
}

TEST_CASE("Message::get_color returns a colour for every log level", "[utils][message]")
{
    const std::vector<trantor::Logger::LogLevel> levels = {
        trantor::Logger::LogLevel::kTrace,
        trantor::Logger::LogLevel::kDebug,
        trantor::Logger::LogLevel::kInfo,
        trantor::Logger::LogLevel::kWarn,
        trantor::Logger::LogLevel::kError,
        trantor::Logger::LogLevel::kFatal,
    };

    for (const auto level : levels)
    {
        CAPTURE(static_cast<int>(level));
        CHECK_FALSE(Message::get_color(level).empty());
    }
}
