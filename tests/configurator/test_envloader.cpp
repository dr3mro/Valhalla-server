/**
 * @file test_envloader.cpp
 * @brief Checks how configuration values are read from environment variables,
 * parsed, and defaulted.
 * @details A subclass re-exports the protected helpers so they can be tested
 * directly. Cases cover string, bool, chrono, integer and set-valued getters,
 * strict parsing of "true", safe fallback when a value is malformed, set
 * splitting, and performance tuning. A final end-to-end case checks that the
 * process-wide test environment is picked up by the Configurator singleton.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Nearly every setting — secrets, worker counts, timeouts, database details —
 * arrives through these getters. If a malformed value crashed startup, or a bad
 * value were silently accepted, the server could fail to boot or run with
 * unsafe configuration. These tests pin the parsing and fallback rules,
 * including several historical bugs, so configuration stays robust.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - std::chrono durations: https://en.cppreference.com/w/cpp/chrono
 * - RAII guard (restore the environment in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - try/catch (handling parse errors):
 * https://en.cppreference.com/w/cpp/language/try_catch
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#include <algorithm>  // std::max, used when raising worker counts.
// Catch2's TEST_CASE, SECTION, CHECK and REQUIRE macros.
#include <catch2/catch_test_macros.hpp>
#include <chrono>    // Duration types used by the chrono getters.
#include <cstdint>   // Fixed-width integers such as uint16_t.
#include <optional>  // std::optional for getters that may return nothing.
#include <set>       // Set containers used in comparisons.
#include <string>    // std::string for environment keys and values.
#include <thread>    // std::thread::hardware_concurrency, used for CPU counts.
#include <unordered_set>  // The set-valued getter returns an unordered_set.
#include <vector>         // Vector support for callers of this code.

// Configurator, checked in the end-to-end case.
#include "configurator/configurator.hpp"
// Defaults, the compiled-in fallback values.
#include "configurator/defaults.hpp"
// EnvLoader, the class under test.
#include "configurator/envloader/envloader.hpp"
#include "store/store.hpp"  // Store, used to fetch the Configurator singleton.

// EnvLoader helpers are tested directly through a subclass that re-exports the
// protected members. The "Bug N fixed" notes above some cases document the
// regression each case guards, not merely the expected value.

// Anonymous namespace keeps the helpers private to this file.
namespace {
  /**
   * EnvLoader exposes its helpers as protected members; this subclass
   * re-exports them for direct testing.
   */
  // Inherits so protected helpers can be
  // brought into public view.
  struct TestLoader : EnvLoader {
    using EnvLoader::getEnvironmentVariable;  // the template overload is
                                              // public already
    using EnvLoader::optimize_performance;    // Re-export the worker-count
                                              // tuner.
    using EnvLoader::parseSet;  // Re-export the comma-separated set parser.
    void printValues() const override {
    }  // Pure virtual from the base; empty because tests do not print.
  };

  /**
   * RAII guard that sets an environment variable for the duration of a
   * scope and restores the previous state afterwards.
   */
  // Changes one variable, then undoes the change at scope
  // exit.
  struct ScopedEnv {
    ScopedEnv(const std::string& key, const std::string& value)
        // Remember which key to restore.
        : key_(key) {
      const char* previous =
          std::getenv(key.c_str());  // Read the old value before overwriting.
      hadPrevious_ =
          previous != nullptr;  // Record whether there was an old value.
      // Only copy when there is something to copy.
      if (hadPrevious_) {
        previous_ = previous;  // Store our own copy of the old value.
      }
      setenv(key.c_str(),
             value.c_str(),
             1);  // Install the new value (overwrite).
    }

    // Restore the environment when the test scope ends.
    ~ScopedEnv() {
      // There was an old value, so put it back.
      if (hadPrevious_) {
        // Restore with overwrite.
        // There was no old value.
        setenv(key_.c_str(), previous_.c_str(), 1);
      } else {
        unsetenv(key_.c_str());  // Remove the variable again.
      }
    }

    std::string key_;                  // The name of the variable we changed.
    bool        hadPrevious_ = false;  // Whether an old value existed.
    std::string previous_;             // Our owned copy of that old value.
  };
}  // namespace

// Basic override semantics.
TEST_CASE("EnvLoader string overload returns override or default",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  // Nothing set in the
  // environment.
  SECTION("missing variable yields the default") {
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        std::string("fallback")) ==
          "fallback");  // The supplied default is returned.
  }

  // Variable is set.
  SECTION("present variable overrides the default") {
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET",
                    "override");  // Set it for this section only.
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        std::string("fallback")) ==
          "override");  // The environment value wins.
  }
}

// Only the exact word "true" is true.
TEST_CASE("EnvLoader bool overload parses 'true' strictly",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  // Nothing set.
  SECTION("missing variable yields the default") {
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        true));  // A true default stays true.
    CHECK_FALSE(
        loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                      false));  // A false default stays false.
  }

  // Exact match.
  SECTION("value 'true' maps to true") {
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET",
                    "true");  // Set the value.
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        false));  // It overrides even a true
                                                  // default.
  }

  // Explicit false.
  SECTION("value 'false' maps to false regardless of the default") {
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET",
                    "false");  // Set the value.
    CHECK_FALSE(
        loader
            .getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                    true));  // False wins over a true default.
  }

  // Strict parsing.
  SECTION("any other non-empty value maps to false") {
    // getEnvironmentVariable(bool) compares against the literal "true";
    // everything else (including "1" and "TRUE") is false.
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET",
                    "1");  // "1" is truthy in some languages but not here.
    CHECK_FALSE(
        loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                      true));  // It is treated as false.
  }
}

// Duration parsing.
TEST_CASE("EnvLoader chrono-seconds overload parses integer seconds",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  // Nothing set.
  SECTION("missing variable yields the default") {
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        std::chrono::seconds{7}) ==
          std::chrono::seconds{7});  // Seven seconds is returned.
  }

  // Value is set.
  SECTION("present variable is parsed") {
    // Set "42".
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "42");
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_DEFINITELY_UNSET",
                                        std::chrono::seconds{7}) ==
          std::chrono::seconds{42});  // It becomes 42 seconds.
  }
}

// Integer parsing.
TEST_CASE("EnvLoader integer template overload uses std::stoi",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  SECTION("missing variable yields the default") {
    CHECK(loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET",
                                             3) == 3);
  }  // Nothing set: default of 3.

  // Value is set.
  SECTION("present variable is parsed with stoi") {
    // Set "17".
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "17");
    CHECK(loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET",
                                             3) ==
          17);  // Parsed as the integer 17.
  }
}

// Bug 10 (historical): getEnvironmentVariable<T>() used to call std::stoi
// without catching exceptions, so a malformed env var (non-numeric/overflow)
// aborted server startup instead of falling back to the default. The chrono
// overload used std::stoll and could wrap a negative value. Bug 10 fixed: the
// integer overload now wraps std::stoi in try/catch, and the chrono overload
// wraps std::stoll in try/catch with an explicit negative-value guard. Both
// fall back to the default on error (see envloader.hpp/.cpp).
// Malformed input safety.
TEST_CASE(
    "EnvLoader integer overload falls back to default on non-numeric input",
    "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  // Text where a number is
  // expected.
  SECTION("non-numeric value returns the default") {
    ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET",
                    "not-a-number");  // Set a non-numeric string.
    CHECK(loader.getEnvironmentVariable<int>("VALHALLA_TEST_DEFINITELY_UNSET",
                                             3) ==
          3);  // Falls back to 3 instead of throwing.
  }

  // Negative durations
  // are rejected.
  SECTION("negative chrono value returns the default") {
    ScopedEnv guard("VALHALLA_TEST_NEG_DURATION",
                    "-5");  // Set a negative number.
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_NEG_DURATION",
                                        std::chrono::seconds{10}) ==
          std::chrono::seconds{10});  // Negative is ignored; default used.
  }

  // Text where a duration is expected.
  SECTION("non-numeric chrono value returns the default") {
    ScopedEnv guard("VALHALLA_TEST_BAD_DURATION",
                    "banana");  // Set a non-numeric string.
    CHECK(loader.getEnvironmentVariable("VALHALLA_TEST_BAD_DURATION",
                                        std::chrono::seconds{10}) ==
          std::chrono::seconds{10});  // Falls back to ten seconds.
  }
}

// Set parsing basics.
TEST_CASE("EnvLoader parseSet splits on commas and trims whitespace",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  CHECK(loader.parseSet("a,b,c") ==
        std::unordered_set<std::string>{"a", "b", "c"});  // Simple comma split.
  CHECK(
      loader.parseSet(" a , b ") ==
      std::unordered_set<std::string>{"a",
                                      "b"});  // Surrounding spaces are trimmed.
  CHECK(loader.parseSet("single") ==
        std::unordered_set<std::string>{
            "single"});  // A single token becomes a one-element set.
  CHECK(loader.parseSet("").empty());  // An empty string yields an empty set.
}

// Bug 9 fixed: parseSet now skips empty tokens, so "a,,b" yields exactly
// {"a","b"}.
// Regression guard.
TEST_CASE("EnvLoader parseSet skips empty tokens from consecutive commas",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  const auto parsed =
      loader.parseSet("a,,b");    // Parse a string with a double comma.
  CHECK(parsed.count("a") == 1);  // "a" is present.
  CHECK(parsed.count("b") == 1);  // "b" is present.
  CHECK(parsed.count("") == 0);   // empty string must NOT be present
  CHECK(parsed.size() == 2);      // No extra empty entry snuck in.
}

// Set-valued getter.
TEST_CASE("EnvLoader set-of-strings overload falls back to an empty set",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  const auto unset = loader.getEnvironmentVariable(
      "VALHALLA_TEST_DEFINITELY_UNSET");  // Variable is not set.
  CHECK(unset.empty());                   // The result is an empty set.

  ScopedEnv  guard("VALHALLA_TEST_DEFINITELY_UNSET",
                   "10.0.0.1,10.0.0.2");  // Set two comma-separated values.
  const auto parsed = loader.getEnvironmentVariable(
      "VALHALLA_TEST_DEFINITELY_UNSET");  // Read them back.
  CHECK(parsed ==
        std::unordered_set<std::string>{"10.0.0.1",
                                        "10.0.0.2"});  // Both values are parsed
                                                       // into the set.
}

// EnvVars wrapper around getenv.
TEST_CASE("EnvVars get reads the process environment",
          "[configurator][envloader]") {
  // Subclass to expose the protected get method.
  struct Getter : EnvVars {
    using EnvVars::get;  // Bring get into public scope.
  };

  Getter getter;  // The wrapper under test.

  CHECK_FALSE(getter.get("VALHALLA_TEST_DEFINITELY_UNSET")
                  .has_value());  // Unset variable gives no value.
  // Set it.
  ScopedEnv guard("VALHALLA_TEST_DEFINITELY_UNSET", "hello");
  CHECK(getter.get("VALHALLA_TEST_DEFINITELY_UNSET") ==
        std::optional<std::string>("hello"));  // It is read back as "hello".
}

// Bug 5 fixed: EnvVars::set() now uses setenv() which copies both key and
// value internally — no dangling pointer.
// Regression guard for a dangling pointer.
TEST_CASE("EnvVars::set stores a value that is readable via get after return",
          "[configurator][envloader]") {
  // Subclass to expose the protected methods.
  struct Setter : EnvVars {
    using EnvVars::get;  // Bring get into public scope.
    using EnvVars::set;  // Bring set into public scope.
  };

  Setter s;  // The wrapper under test.
  // Set a value.
  s.set("VALHALLA_TEST_ENVVARS_SET_BUG5", "hello_fixed");
  auto result = s.get("VALHALLA_TEST_ENVVARS_SET_BUG5");  // Read it back after
                                                          // set() returned.
  REQUIRE(result.has_value());  // A value must be present.
  CHECK(result.value() ==
        "hello_fixed");  // It is the value we set, proving no dangling pointer.

  // Cleanup
  unsetenv("VALHALLA_TEST_ENVVARS_SET_BUG5");  // Remove the test variable so
                                               // it does not leak.
}

// Worker-count tuning.
TEST_CASE("EnvLoader optimize_performance raises sub-1 values to ncpus*factor",
          "[configurator][envloader]") {
  TestLoader loader;  // The loader under test.

  // Zero means "let the library decide".
  SECTION("zero is raised") {
    uint16_t value = 0;                     // Start from zero.
    loader.optimize_performance(value, 5);  // Apply the tuner with factor 5.
    const auto ncpus = std::max<
        uint16_t>(1,
                  static_cast<uint16_t>(
                      std::thread::hardware_concurrency()));  // At least one
                                                              // CPU.
    CHECK(value == ncpus * 5);  // It becomes CPUs times the factor.
  }

  // Explicit values are respected.
  SECTION("a positive value is left alone") {
    uint16_t value = 3;                     // A positive setting.
    loader.optimize_performance(value, 5);  // Apply the tuner.
    CHECK(value == 3);                      // It is unchanged.
  }
}

// End-to-end check that the process-wide test environment (set in
// tests/support/global_env.cpp before main) is actually consumed by the
// Configurator singleton: defaults, worker counts and token parameters.
// Whole-config sanity check.
TEST_CASE("Configurator exposes the configured singleton through Store",
          "[configurator][configurator]") {
  auto configurator = Store::getObject<Configurator>();  // Fetch the singleton
                                                         // created at startup.
  REQUIRE(configurator != nullptr);                      // It must exist.

  SECTION(
      // Rate-limit settings.
      "DOSDetectorConfig uses the compiled-in defaults (env unset)") {
    const auto& config =
        configurator->get<Configurator::DOSDetectorConfig>();  // Read that
                                                               // config block.
    CHECK(config.max_requests ==
          Defaults::DosDetector::MAX_REQUESTS_);  // Default request limit.
    CHECK(config.max_fingerprints ==
          Defaults::DosDetector::MAX_FPS_);  // Default fingerprint limit.
    CHECK(config.period ==
          std::chrono::seconds{
              Defaults::DosDetector::PERIOD_});  // Default period.
    CHECK(config.ratelimit_duration ==
          std::chrono::seconds{
              Defaults::DosDetector::RL_DURATION_});  // Default rate-limit
                                                      // window.
    CHECK(config.ban_duration ==
          std::chrono::seconds{
              Defaults::DosDetector::BAN_DURATION_});  // Default ban length.
    CHECK(config.whitelist
              .empty());  // The test environment cleared any whitelist.
    CHECK(config.blacklist.empty());  // Likewise for the blacklist.
  }

  SECTION(
      // Database settings.
      "DatabaseConfig uses the compiled-in defaults (env unset)") {
    const auto& config =
        // Read that config block.
        configurator->get<Configurator::DatabaseConfig>();
    // Default host.
    CHECK(config.host == Defaults::Database::DB_HOST_);
    // Default port.
    CHECK(config.port == Defaults::Database::DB_PORT_);
    CHECK(config.name ==
          Defaults::Database::DB_NAME_);  // Default database name.
    // Default user.
    CHECK(config.user == Defaults::Database::DB_USER_);
    CHECK(config.pass == Defaults::Database::DB_PASS_);  // Default password.
    CHECK_FALSE(config.ssl);  // SSL is off by default.
    CHECK(config.async);      // DB_ASYNC defaults to true since the async plan
                              // (aync_plan.md, phase 5)
    CHECK(config.max_conn ==
          Defaults::Database::DB_MAX_CONN_);  // Default connection count.
    CHECK(config.acquire_timeout_ms ==
          Defaults::Database::DB_ACQUIRE_TIMEOUT_MS_);  // Default timeout
                                                        // from Defaults.
    CHECK(config.acquire_timeout_ms ==
          3000);  // And it equals the expected 3000 ms.
  }

  SECTION(
      "DbExecutorConfig: tests run database work inline, the queue size is "
      // Executor settings.
      "the default") {
    const auto& config =
        configurator
            ->get<Configurator::DbExecutorConfig>();  // Read that config block.
    CHECK(config.workers ==
          0);  // tests/support/global_env.cpp sets DB_WORKERS=0
    CHECK(config.queue_max ==
          Defaults::DbExecutor::QUEUE_MAX_);  // Default queue size.
  }

  SECTION(
      "PasswordWorkerConfig: tests run password work inline, other values "
      // Password settings.
      "are the defaults") {
    const auto& config = configurator->get<
        Configurator::PasswordWorkerConfig>();  // Read that config block.
    CHECK(config.workers ==
          0);  // tests/support/global_env.cpp sets PASSWORD_WORKERS=0
    CHECK(config.queue_max ==
          Defaults::PasswordWorker::QUEUE_MAX_);  // Default queue size.
    CHECK(Defaults::PasswordWorker::WORKERS_ ==
          2);  // The compiled-in default is two workers.
  }

  SECTION(
      // Token settings.
      "TokenManagerParameters come from the test environment") {
    // tests/support/global_env.cpp sets TOKEN_* before the Configurator
    // singleton was created; the values must have been picked up there.
    const auto& config = configurator->get<
        Configurator::TokenManagerParameters>();  // Read that config block.
    CHECK(std::string(config.secret) ==
          "valhalla-unit-test-secret-key");  // The test secret was picked up.
    CHECK(std::string(config.issuer) ==
          "valhalla-unit-test-issuer");        // The test issuer was picked up.
    CHECK(std::string(config.type) == "JWS");  // The token type was picked up.
    CHECK(config.validity == 60);  // The 60-minute validity was picked up.
  }
}
