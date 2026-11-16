/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file envvars.cpp
 * @brief Process-environment get/set wrappers used by configuration sections.
 * @details This file contains the two small functions declared in envvars.hpp.
 * One reads a name/value pair from the operating system's environment; the
 * other writes (or overwrites) one. Both operate on plain text and leave any
 * type conversion to the class above them. Errors while writing are reported
 * and swallowed rather than thrown.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the lowest level of the configuration stack: the only code that
 * touches the C library functions std::getenv and setenv directly. EnvLoader
 * calls get() every time a configuration section is built, so this file is what
 * actually makes environment-driven configuration possible. If it were missing,
 * no other file could discover the deployment's settings, and the server could
 * only rely on hard-coded defaults.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - std::getenv / std::setenv:
 * https://en.cppreference.com/w/cpp/utility/program/getenv
 * - std::optional (a value that may be missing):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// The class declaration this file implements; included first so the compiler
// checks it stands alone.
#include "envvars.hpp"

// Used to build the error text in set() by filling in the {}.
#include <fmt/format.h>

// Provides std::getenv and setenv, the C functions that access the process
// environment.
#include <cstdlib>

// Provides Message::ErrorMessage used to log a failed set operation.
#include "utils/message/message.hpp"

std::optional<std::string> EnvVars::get(
    const std::string& key)  // Returns the raw value or an empty optional when
                             // the variable is not set.
{
  // Retrieve the value associated with the environment variable key
  const char* _key = key.c_str();  // The C API needs a null-terminated char
                                   // array, so convert the C++ string to one.
  const char* value =
      std::getenv(_key);  // Ask the OS for the value; it returns nullptr when
                          // the name does not exist.

  // If the value exists, copy it into a std::string that outlives this call
  if (value != nullptr)  // A non-null pointer means the variable is present;
                         // otherwise it is absent.
  {
    return std::string(
        value);  // Copy the C string into a C++ string so the caller owns
                 // the memory after getenv's buffer may change.
  }

  // Otherwise, return an empty optional
  return std::nullopt;  // Signal "not set" explicitly, which is clearer than
                        // returning an empty string.
}

void EnvVars::set(const std::string& key,
    const std::string& value)  // Stores value under key; used by tests to fake
                               // settings, not on the request path.
{
  // Overwrite = 1 so repeated calls replace the existing value rather than
  // being ignored.
  if (setenv(key.c_str(), value.c_str(), 1) !=
      0)  // The OS returns 0 on success; any other result means the write
          // failed (bad name, no memory, etc.).
  {
    Message::ErrorMessage(fmt::format("Failed to set environment variable {}.",
        key));  // Log the failure with the variable name; do not throw
                // so callers keep running.
  }
}
