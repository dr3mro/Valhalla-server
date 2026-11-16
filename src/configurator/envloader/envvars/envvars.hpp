/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file envvars.hpp
 * @brief Thin protected wrapper around the process environment (getenv/setenv).
 * @details This small header declares two helper functions that read and write
 * environment variables, which are name/value pairs stored by the operating
 * system for this running program. It does no conversion or validation itself;
 * it only fetches the raw text string. A higher-level class (EnvLoader) builds
 * on top of this to turn that text into numbers, booleans, and other types the
 * server needs.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the very bottom layer of the configuration system. The server keeps
 * almost every setting (ports, database credentials, timeouts) in environment
 * variables so deployments can change behaviour without editing code or
 * rebuilding. EnvVars is the only place that talks directly to the operating
 * system's environment. EnvLoader calls get() to read values, and tests call
 * set() to prepare temporary values. Without this file there would be no safe,
 * uniform way for the rest of the configuration code to reach those settings.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - std::optional (a value that may be missing):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per translation unit, no matter
              // how many times it is included.

// std::optional lets a function return "a string OR nothing" without using a
// special sentinel value.
#include <optional>
// std::string holds the variable names and the text values read from the
// environment.
#include <string>

/**
 * @class EnvVars
 * @brief Thin protected wrapper around the process environment (getenv/setenv).
 *
 * @details Provides the raw accessors on which EnvLoader builds its typed
 * conversions. The helpers are static and protected so only derived
 * configuration types (or test code deriving from them) can read or modify the
 * environment; callers are expected to use the EnvLoader overloads instead.
 *
 * @note The set of environment variables is process-global. Reading is safe
 * while no other thread mutates the environment; mutation is intended for
 * setup/test code before threads start, not for the request path.
 * @see EnvLoader
 */
class EnvVars  // Provides only protected static helpers meant for configuration
               // classes that inherit from it.
{
 protected:  // Only this class and classes that inherit from it may use the
             // helpers below; outside code cannot.
  // Method to get an environment variable by name
  /**
   * @brief Reads a raw environment variable.
   * @details Asks the operating system for the value stored under @p key. In
   * plain words: "is there a variable with this name, and if so what text
   * does it hold?" The answer is wrapped in an optional so that "not set" is
   * clearly different from "set to empty text".
   * @param[in] key Variable name.
   * @return The value as a string, or std::nullopt when the variable is not
   * set.
   */
  static std::optional<std::string> get(
      const std::string& key);  // Returns the raw text, or an empty optional
                                // when the name is not present.

  // Method to set an environment variable
  /**
   * @brief Sets (or overwrites) an environment variable in the current
   * process.
   * @param[in] key Variable name.
   * @param[in] value New value.
   * @note On failure an error message is logged; the call does not throw.
   * @warning Mutating the process environment is not synchronized with other
   * threads.
   * @note The only caller in this repository is a test that derives a helper
   * to expose this protected method; production configuration is read-only.
   */
  static void set(const std::string& key,
      const std::string& value);  // Stores value under key; used by tests to
                                  // fake settings, not on the request path.
};
