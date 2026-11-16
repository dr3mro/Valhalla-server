/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file env_guard.hpp
 * @brief A small RAII helper that changes an environment variable for one test
 * and restores it afterwards.
 * @details Tests often need to pretend a setting (like a token secret) has a
 * particular value. This guard sets or unsets that variable when it is created,
 * and puts the old value back when it is destroyed. Using a guard means cleanup
 * happens automatically even if a test fails part-way through.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The server reads many settings from environment variables. If one test
 * changed a variable and never restored it, later tests could silently read the
 * wrong configuration and fail for reasons unrelated to what they test. This
 * guard keeps tests independent, which is essential for a reliable test suite.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (cleanup in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - Deleted functions (prevent copying/moving):
 * https://en.cppreference.com/w/cpp/language/function#Deleted_functions
 * - Environment variables in C:
 * https://en.cppreference.com/w/cpp/utility/program/getenv
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

// Provides getenv, setenv and unsetenv for reading/writing the process
// environment.
#include <cstdlib>
// Support for values that may be absent (used by callers of this header).
#include <optional>
// std::string, used to remember the key and its previous value.
#include <string>

/**
 * RAII guard that sets (or unsets) an environment variable and restores the
 * previous state on destruction. Never touches `.env`; only the process env.
 * @brief Temporarily sets or unsets one environment variable.
 * @details The constructor changes the variable; the destructor puts it back
 * exactly as it was, whether that means restoring an old value or removing it
 * again.
 */
class EnvGuard
{
 public:
  /**
   * Sets `key` to `value` for the lifetime of the guard.
   */
  EnvGuard(const std::string& key, const std::string& value)
      : key_(key)  // Remember which variable to restore later.
  {
    const char* previous =
        std::getenv(key.c_str());  // Read the current value, if any, before
                                   // overwriting it.
    if (previous !=
        nullptr)  // Only meaningful if the variable was actually set.
    {
      hadPrevious_ = true;      // Record that we must restore a value later.
      previous_    = previous;  // Copy the value; the pointer from getenv
                                // may go away.
    }
    setenv(key.c_str(),
           value.c_str(),
           1);  // Set the new value; the 1 means "overwrite any existing
                // value".
  }

  /**
   * Unsets `key` for the lifetime of the guard.
   */
  explicit EnvGuard(const std::string& key)
      : key_(key)  // `explicit` stops accidental implicit conversions to
                   // EnvGuard.
  {
    const char* previous =
        std::getenv(key.c_str());  // Read the old value so we can put it back.
    if (previous != nullptr)       // Skip recording if there was no old value.
    {
      hadPrevious_ = true;      // There is something to restore on destruction.
      previous_    = previous;  // Copy it into owned storage.
    }
    unsetenv(
        key.c_str());  // Remove the variable for the duration of the guard.
  }

  EnvGuard(const EnvGuard&) =
      delete;  // No copying: two guards for one key would restore twice.
  EnvGuard& operator=(const EnvGuard&) =
      delete;  // No copy assignment, for the same reason.
  EnvGuard(EnvGuard&&) =
      delete;  // No moving: the destructor must stay tied to one object.
  EnvGuard& operator=(EnvGuard&&) =
      delete;  // No move assignment, to keep ownership unambiguous.

  ~EnvGuard()  // Runs at scope exit, even when a test throws, so the
               // environment is always restored.
  {
    if (hadPrevious_)  // The variable existed before, so restore its
                       // original value.
    {
      setenv(key_.c_str(),
             previous_.c_str(),
             1);  // Put the saved value back (overwrite).
    }
    else  // The variable did not exist before.
    {
      unsetenv(key_.c_str());  // Remove it again to leave no trace.
    }
  }

 private:
  std::string key_;       // The name of the environment variable we changed.
  std::string previous_;  // A copy of its old value, so we do not rely on
                          // getenv's pointer.
  bool hadPrevious_ = false;  // Whether there was an old value worth restoring.
};
