/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file envloader.hpp
 * @brief Base for configuration sections that read typed values from the
 * process environment.
 * @details This header defines EnvLoader, a reusable base class that turns raw
 * environment strings into the specific types the server uses: integers,
 * strings, booleans, durations (lengths of time) and sets of comma-separated
 * names. Each conversion has a fallback default so a missing or badly written
 * variable never stops the server from starting; problems are logged instead.
 * Configuration sections inherit from this class and call its helpers.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Every section of Configurator (server, database, DoS limits, tokens, and so
 * on) derives from EnvLoader, so this is the shared engine behind all
 * configuration loading. It is the middle layer: EnvVars below it reads the raw
 * text, and the Configurator sections above it call getEnvironmentVariable() to
 * obtain ready-to-use typed values. Without it, each section would have to
 * duplicate parsing and error handling, and a single malformed variable could
 * crash startup.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Environment variables: https://man7.org/linux/man-pages/man7/environ.7.html
 * - Function templates:
 * https://en.cppreference.com/w/cpp/language/function_template
 * - std::chrono durations (lengths of time):
 * https://en.cppreference.com/w/cpp/chrono/duration
 * - std::unordered_set (a collection of unique values):
 * https://en.cppreference.com/w/cpp/container/unordered_set
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this file at most once per translation unit, no matter
              // how many times it is included.

// std::chrono::seconds represents durations measured in whole seconds.
#include <chrono>
// std::exception is the base type caught when a numeric conversion fails.
#include <exception>
// std::string holds variable names, values and the tokens of a parsed set.
#include <string>
// std::unordered_set stores unique fingerprints/tokens with fast lookup.
#include <unordered_set>

// Provides EnvVars, the base class with the raw get()/set() helpers used here.
#include "envvars/envvars.hpp"
// Provides Message::ErrorMessage used to log bad environment values.
#include "utils/message/message.hpp"

/**
 * @class EnvLoader
 * @brief Base for configuration sections that read typed values from the
 * process environment.
 *
 * @details Extends EnvVars with a family of getEnvironmentVariable() overloads
 * that convert a raw string into the target type and fall back to a
 * caller-supplied default when the variable is absent (or, for numeric/duration
 * types, malformed). Conversion failures are reported through
 * Message::ErrorMessage rather than thrown, so a bad environment cannot prevent
 * startup. Derived classes must implement printValues() and typically call
 * these accessors from their constructor initializer lists.
 *
 * @note Numeric overloads parse through std::stoi, so integer values outside
 * the range of a signed int are rejected even when the destination type is
 * wider.
 * @warning A present boolean variable equals true only when it is exactly
 * "true"; any other present value is interpreted as false (not as the supplied
 * default).
 * @see EnvVars, Configurator
 */
class EnvLoader
    : public EnvVars  // Inherits the raw environment accessors and adds typed,
                      // default-aware conversions on top.
{
 public:  // The conversion helpers below are available to derived sections
          // and their callers.
  /** @brief Default constructor; no state of its own beyond the inherited
   * helpers. */
  explicit EnvLoader() = default;  // Nothing to initialise; the inherited
                                   // EnvVars has no data members.
  /** @brief Virtual destructor so sections can be destroyed through this
   * base. */
  virtual ~EnvLoader() = default;  // Virtual so deleting through an EnvLoader*
                                   // runs the derived section's destructor too.

  /**
   * @brief Reads an integer-like environment variable, converting with
   * std::stoi.
   *
   * @details When the variable is absent the default is returned directly.
   * When present, the value is parsed as a signed int and static_cast to T. A
   * parse failure is logged and the default returned. In plain language: "if
   * the deployment set this variable, try to read it as a whole number; if
   * that fails for any reason, quietly use the safe default."
   *
   * @tparam T Destination type (typically an unsigned integer or small enum);
   * must be constructible by static_cast from the std::stoi result.
   * @param[in] key Environment-variable name.
   * @param[in] defaultValue Value used when the variable is absent or
   * unparsable.
   * @return The parsed value, or @p defaultValue.
   * @note Because parsing goes through std::stoi, values greater than INT_MAX
   * (or less than INT_MIN) fall back to the default even for 64-bit
   * destinations.
   * @see getEnvironmentVariable(const std::string&, const
   * std::chrono::seconds&)
   */
  template <typename T>  // T is filled in from the type of defaultValue at
                         // each call site (e.g. uint32_t or uint16_t).
  T getEnvironmentVariable(const std::string& key,
      T defaultValue)  // Returns the parsed number or
                       // defaultValue if the variable
                       // is missing or invalid.
  {
    auto value = get(key);  // Fetch the raw text via the inherited helper;
                            // an empty optional means "not set".
    if (!value)  // No value stored under this name, so there is nothing to
                 // parse.
    {
      return defaultValue;  // Give the caller the safe fallback
                            // immediately.
    }
    try  // Parsing text can fail (e.g. "abc"), so guard it and recover
         // instead of letting an exception escape.
    {
      return static_cast<T>(std::stoi(
          value.value()));  // Convert text to int then to the requested
                            // type (uint32_t, uint8_t, etc.).
    }
    catch (const std::exception&)  // std::stoi throws when the text is not
                                   // a number or is out of int range.
    {
      Message::ErrorMessage(
          "Invalid numeric value for env var " + key +
          "; using default.");  // Log the bad variable name so an
                                // operator can spot the mistake.
      return defaultValue;      // Recover with the default so a bad setting
                                // cannot prevent startup.
    }
  }

  /**
   * @brief Reads a raw string environment variable.
   * @param[in] key Environment-variable name.
   * @param[in] defaultValue Value used when the variable is not set.
   * @return The variable's exact string value, or @p defaultValue when
   * absent.
   */
  std::string                     getEnvironmentVariable(const std::string& key,
      const std::string& defaultValue);  // Returns the text unchanged, or the
                                                             // fallback when the name is absent.
  /**
   * @brief Reads a boolean environment variable.
   * @param[in] key Environment-variable name.
   * @param[in] defaultValue Value used only when the variable is absent.
   * @return True if the variable is present and equals the literal "true";
   * false if it is present with any other value; @p defaultValue if it is
   * absent.
   * @note Matching is case-sensitive; "True", "1" and "TRUE" all evaluate to
   * false.
   */
  bool                            getEnvironmentVariable(const std::string& key,
      bool defaultValue);  // True only for the exact word "true"; the default
                                                      // applies only when unset.
  /**
   * @brief Reads a duration environment variable expressed as a whole number
   * of seconds.
   * @param[in] key Environment-variable name.
   * @param[in] defaultValue Value used when the variable is absent or
   * invalid.
   * @return The parsed duration, or @p defaultValue.
   * @note A negative count is rejected (with an error log) and the default is
   * kept, since durations are stored as unsigned seconds.
   */
  std::chrono::seconds            getEnvironmentVariable(const std::string& key,
      const std::chrono::seconds&
          defaultValue);  // Interprets the value as whole seconds, rejecting
                                     // negatives and bad text.
  /**
   * @brief Reads a comma-separated environment variable into a set of
   * strings.
   * @param[in] key Environment-variable name.
   * @return Parsed set, or an empty set when the variable is absent or empty.
   * @see parseSet
   */
  std::unordered_set<std::string> getEnvironmentVariable(
      const std::string& key);  // Splits "a, b, c" into a set of tokens;
                                // missing means an empty set.

  /**
   * @brief Logs this section's effective values.
   * @details Implemented by every Configurator section; secret-bearing fields
   * must be redacted by the implementation.
   */
  virtual void printValues()
      const = 0;  // Pure virtual: every section must supply its own logger;
                  // secrets must be hidden.

 protected:  // Helpers below are for derived configuration sections only, not
             // for outside callers.
  /**
   * @brief Auto-sizes a zero-valued count from the machine's CPU count.
   * @param[in,out] parameter Count to adjust; when it is zero it becomes
   * @c hardware_concurrency()*@p factor.
   * @param[in] factor Multiplier applied to the CPU count.
   * @note A hardware_concurrency() of zero is treated as one CPU.
   */
  void                            optimize_performance(uint16_t& parameter,
      uint8_t factor);  // Replaces a zero "auto" value with CPUs times factor.
  /**
   * @brief Splits a comma-separated string into a trimmed set of non-empty
   * tokens.
   * @param[in] str Raw comma-separated value; may be empty.
   * @return Set of tokens with surrounding whitespace removed; empty tokens
   * are dropped.
   */
  std::unordered_set<std::string> parseSet(
      const std::string& str);  // Turns "a, , b" into {"a","b"}, trimming
                                // spaces and ignoring blanks.
};
