/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file envloader.cpp
 * @brief Typed environment-variable parsing helpers used by configuration
 * sections.
 * @details This file implements the non-template functions declared in
 * envloader.hpp: reading a plain string, a boolean, a duration in seconds, and
 * a comma-separated set of names. It also contains two shared helpers:
 * parseSet() splits text into trimmed tokens, and optimize_performance()
 * replaces a zero "auto" count with a value based on the CPU count. Every
 * parser returns a supplied default when the variable is missing or malformed.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the implementation of the configuration engine's middle layer.
 * Configurator's sections call these functions in their constructors, so this
 * file decides what happens when a deployment sets a variable to an unexpected
 * value (for example a negative timeout or the word "TRUE"): it logs a clear
 * message and falls back to a safe default rather than crashing. Without it,
 * section constructors would each need their own error-prone parsing code.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - std::chrono durations (lengths of time):
 * https://en.cppreference.com/w/cpp/chrono/duration
 * - std::unordered_set (a collection of unique values):
 * https://en.cppreference.com/w/cpp/container/unordered_set
 * - std::stringstream (reading text as if it were a stream):
 * https://en.cppreference.com/w/cpp/io/basic_stringstream
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declaration this file implements; included first so the compiler
// checks it stands alone.
#include "envloader.hpp"

// Brings in the formatting machinery used indirectly by Message logging calls.
#include <fmt/core.h>

// std::exception is the base type caught when a numeric conversion fails.
#include <exception>
// std::stringstream lets parseSet() read a string piece by piece, like a file.
#include <sstream>
// std::thread::hardware_concurrency reports how many CPUs the machine has.
#include <thread>

// Reads a string variable: return the stored text verbatim, or the default when
// it is absent.
std::string EnvLoader::getEnvironmentVariable(
    const std::string& key,
    const std::string& defaultValue)  // Returns the raw text or the fallback
                                      // when the name is missing.
{
  auto value = get(key);  // Fetch the raw value through EnvVars; an empty
                          // optional means "not set".
  return value.has_value() ? value.value()
                           : defaultValue;  // Pick the stored text if present,
                                            // otherwise the caller's default.
}

// Reads a boolean variable: only the exact word "true" counts as true.
bool EnvLoader::getEnvironmentVariable(
    const std::string& key,
    bool defaultValue)  // True for "true", false for any other present value,
                        // default when unset.
{
  auto value = get(key);  // Fetch the raw value through EnvVars.
  // Only the exact literal "true" means true; any other present value is
  // false. The default applies exclusively when the variable is not set at
  // all.
  return value.has_value() ? value.value() == "true"
                           : defaultValue;  // Compare as text; the default is
                                            // used only for a missing variable.
}

// Reads a duration in whole seconds, rejecting negative or invalid numbers.
std::chrono::seconds EnvLoader::getEnvironmentVariable(
    const std::string& key,
    const std::chrono::seconds&
        defaultValue)  // Interprets the value as seconds, or keeps the default
                       // on bad input.
{
  auto value = get(key);   // Fetch the raw value through EnvVars.
  if (!value.has_value())  // Variable absent: there is nothing to convert.
  {
    return defaultValue;  // Hand back the safe fallback.
  }
  try  // Parsing text to a number can fail, so recover instead of letting an
       // exception escape.
  {
    auto count = std::stoll(value.value());  // Convert the text to a 64-bit
                                             // integer (wider than stoi's int).
    // Durations are stored as unsigned seconds; a negative input cannot be
    // represented, so keep the default instead of wrapping it through the
    // cast.
    if (count < 0)  // A negative length of time makes no sense for a
                    // timeout/duration.
    {
      Message::ErrorMessage("Negative duration for env var " + key +
                            "; using default.");  // Log the offending variable
                                                  // so it can be fixed.
      return defaultValue;  // Refuse the negative value and keep the safe
                            // default.
    }
    return std::chrono::seconds(
        static_cast<uint64_t>(count));  // Wrap the count in a type that clearly
                                        // represents "this many seconds".
  }
  catch (const std::exception&)  // std::stoll throws when the text is not
                                 // numeric or is out of range.
  {
    Message::ErrorMessage("Invalid duration value for env var " + key +
                          "; using default.");  // Report the bad variable name.
    return defaultValue;  // Recover so malformed settings cannot stop
                          // startup.
  }
}

// Reads a comma-separated variable into a set; missing or empty means an empty
// set.
std::unordered_set<std::string> EnvLoader::getEnvironmentVariable(
    const std::string&
        key)  // Returns a set of unique tokens, or an empty set when unset.
{
  auto value = get(key);  // Fetch the raw value through EnvVars.
  // An unset variable is treated the same as an empty list, not as an error.
  return value.has_value()
             ? parseSet(value.value())
             : parseSet("");  // Parse the text, or explicitly parse an empty
                              // string to get an empty set.
}

// Splits a comma-separated string into a trimmed set of unique, non-empty
// tokens.
std::unordered_set<std::string> EnvLoader::parseSet(
    const std::string& str)  // Turns "a, b, c" into {"a","b","c"} with spaces
                             // and blanks removed.
{
  std::unordered_set<std::string>
      result;  // The output set; duplicates disappear automatically because a
               // set holds unique values only.
  std::stringstream ss(str);  // Wrap the text so getline can pull out
                              // comma-delimited pieces one at a time.
  std::string       item;     // Temporary holder for each piece as it is read.
  while (std::getline(ss,
                      item,
                      ','))  // Keep reading until no more comma-separated
                             // pieces remain; each pass handles one token.
  {
    // Trim any leading or trailing whitespace
    item.erase(item.find_last_not_of(" \n\r\t") +
               1);  // Cut off trailing spaces/newlines by erasing from just
                    // after the last non-space character.
    item.erase(0,
               item.find_first_not_of(
                   " \n\r\t"));  // Cut off leading spaces/newlines by erasing
                                 // up to the first non-space character.
    // Drop empty tokens so "a,,b" and a string of only whitespace yield no
    // empty member.
    if (!item.empty())  // Skip pieces that are empty after trimming (the
                        // gap between two commas, for example).
    {
      result.insert(item);  // Add the cleaned token to the set.
    }
  }
  return result;  // Hand back all distinct, non-empty tokens to the caller.
}

// Replaces a zero "auto" count with a multiple of the machine's CPU count.
void EnvLoader::optimize_performance(
    uint16_t&     parameter,
    const uint8_t factor)  // Grows a zero value to CPUs times factor, leaving
                           // explicit values alone.
{
  uint16_t ncpus = (std::thread::hardware_concurrency() > 0)
                       ? std::thread::hardware_concurrency()
                       : 1;  // Ask the OS for the CPU count, treating an
                             // unknown count (0) as one CPU.
  // Zero is the "let the runtime size it" sentinel; a nonzero explicit value
  // is respected.
  if (parameter < 1)  // Only an unset/zero count needs auto-sizing; a
                      // positive value was chosen deliberately.
  {
    parameter = ncpus * factor;  // Scale with the hardware so a bigger machine
                                 // gets more workers/connections by default.
  }
}
