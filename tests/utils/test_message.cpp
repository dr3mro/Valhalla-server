/**
 * @file test_message.cpp
 * @brief Catch2 unit tests for Message, the server's logging facade.
 * @details Message offers static functions that log at several severities and
 * return an ANSI colour for each log level. Without a running server the only
 * reliable contract is that these calls never throw and that every supported
 * level has a non-empty colour string. Awkward inputs (empty text, newlines,
 * very long strings) are checked as well.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Nearly every component logs through Message rather than calling trantor
 * directly. If a logging call could throw, it would crash the very code path it
 * was trying to report on. A missing colour would also spoil the readable
 * console output operators rely on during startup and debugging. These tests
 * guard that simple "always accepts, never throws" promise.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 assertion macros (CHECK_FALSE, CHECK_NOTHROW):
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - Static member functions:
 * https://en.cppreference.com/w/cpp/language/static#Static_member_functions
 * - ANSI escape codes for colour:
 * https://en.wikipedia.org/wiki/ANSI_escape_code
 * - std::vector: https://en.cppreference.com/w/cpp/container/vector
 * - std::string: https://en.cppreference.com/w/cpp/string/basic_string
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// trantor::Logger::LogLevel, the severity values tested by get_color.
#include <trantor/utils/Logger.h>

// Brings in TEST_CASE, CHECK_NOTHROW, CHECK_FALSE and CAPTURE.
#include <catch2/catch_test_macros.hpp>
#include <string>  // std::string, both the message type and the colour type.
#include <vector>  // std::vector holds the list of log levels to iterate over.

#include "utils/message/message.hpp"  // The Message facade under test.

// Message is a thin logging facade over trantor's logger. Without a running
// server its only observable contract is "does not throw and always accepts a
// message" - the log output itself goes to the console/file sink.

// Every severity helper must accept a plain message without throwing.
TEST_CASE("Message logging levels accept messages without throwing",
          "[utils][message]") {
  CHECK_NOTHROW(Message::DebugMessage(
      "debug"));  // Verbose developer detail must be accepted.
  CHECK_NOTHROW(Message::InfoMessage(
      "info"));  // Normal informational log must be accepted.
  CHECK_NOTHROW(
      Message::WarningMessage("warning"));  // Warning level must be accepted.
  CHECK_NOTHROW(
      Message::ErrorMessage("error"));  // Handled-error level must be accepted.
  CHECK_NOTHROW(Message::CriticalMessage(
      "critical"));  // The most severe level must be accepted.
  CHECK_NOTHROW(
      Message::ConfMessage("config"));  // Startup config notice (printed to
                                        // stdout) must not throw.
  CHECK_NOTHROW(Message::InitMessage(
      "init"));  // Startup init notice must not throw either.
}

// The helpers flatten newlines and handle long text, so odd-looking strings
// must still be safe.
TEST_CASE("Message logging is tolerant of awkward strings",
          "[utils][message]") {
  CHECK_NOTHROW(Message::InfoMessage(""));  // An empty message is still valid.
  CHECK_NOTHROW(Message::InfoMessage(
      "line1\nline2\twith\ttabs"));  // Newlines and tabs get flattened; no
                                     // throw.
  CHECK_NOTHROW(Message::InfoMessage(
      std::string(2000,
                  'x')));  // std::string(2000,'x') makes 2000 copies of x; long
                           // input must not throw.
}

// get_color must return a non-empty ANSI colour string for each level it
// supports.
TEST_CASE("Message::get_color returns a colour for every log level",
          "[utils][message]") {
  const std::vector<trantor::Logger::LogLevel> levels = {
      // List every severity from the lowest to the highest.
      trantor::Logger::LogLevel::kTrace,  // Lowest severity: very detailed
      // tracing.
      trantor::Logger::LogLevel::kDebug,  // Developer debugging detail.
      trantor::Logger::LogLevel::kInfo,   // Everyday information.
      trantor::Logger::LogLevel::kWarn,   // Something unexpected but not
      // fatal.
      trantor::Logger::LogLevel::kError,  // A failure that was handled.
      trantor::Logger::LogLevel::kFatal,  // Highest severity: a fatal error.
  };

  // Visit each level once; the loop stops
  // after the last element.
  for (const auto level : levels) {
    CAPTURE(static_cast<int>(level));  // If a check fails, Catch2 prints this
                                       // number to show which level failed.
    CHECK_FALSE(
        Message::get_color(level).empty());  // empty() would mean no colour was
                                             // assigned, which is a bug.
  }
}
