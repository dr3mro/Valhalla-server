/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file global_env.cpp
 * @brief Sets process-wide environment variables once, before any test runs, to
 * give the suite deterministic configuration.
 * @details A single static object is constructed before main(), so its
 * constructor runs before the first test. It writes known token settings, tells
 * worker pools to stay inline, and switches off real database access. It also
 * clears database-related variables that a developer might have set in their
 * shell, so results do not depend on the machine running the tests.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * The Configurator and other singletons read their settings when they are first
 * created. If tests used whatever happened to be in the environment, they could
 * behave differently on different machines and accidentally open real database
 * connections. This file pins the configuration so the suite stays hermetic and
 * reproducible everywhere.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static initialisation before main():
 * https://en.cppreference.com/w/cpp/language/initialization#Static_initialization
 * - getenv/setenv/unsetenv:
 * https://en.cppreference.com/w/cpp/utility/program/getenv
 * - Singletons and the Store pattern (used by Configurator and friends).
 * - Catch2 test cases and sections:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides setenv and unsetenv for writing the process environment.
#include <cstdlib>

namespace  // Anonymous namespace keeps this setup object private to this file.
{
  // Deterministic process-wide configuration for tests that go through the
  // Store singletons (Configurator, DOSDetector, SessionManager, ...).
  //
  // Static initialisation runs before main(), so this is guaranteed to be
  // in place before the first Store::getObject<Configurator>() anywhere in
  // the suite - independent of test ordering across translation units.
  //
  // - TOKEN_*: known token parameters for the JWT verifier (the .env file is
  //   never read).
  // - CLN_FRQ=1: keeps the DOSDetector cleanup thread's real-time cadence
  //   short so the process can shut down promptly after the run.
  // - VALHALLA_TEST_NO_DB: hermeticity seam (see DatabaseController::
  //   ensureBackends()). The connection pool is never created, so no test
  //   ever opens a socket to PostgreSQL - even when a dev database is
  //   running on localhost - and query paths report "unavailable"
  //   deterministically instead of calling exit(EXIT_FAILURE).
  struct TestEnvironmentSetup  // A type whose constructor performs the
                               // one-time setup.
  {
    TestEnvironmentSetup()  // Runs automatically before main() because a
                            // static instance is defined below.
    {
      setenv("TOKEN_SECRET", "valhalla-unit-test-secret-key",
          1);  // Fixed secret so token tests know what to sign with.
      setenv("TOKEN_ISSUER", "valhalla-unit-test-issuer",
          1);                          // Fixed issuer claimed by test tokens.
      setenv("TOKEN_TYPE", "JWS", 1);  // Fixed token type string.
      setenv("TOKEN_VALIDITY", "60", 1);  // Tokens last 60 minutes in tests.
      setenv("CLN_FRQ", "1",
          1);  // Short cleanup cadence so shutdown is quick.
      // 0 workers: password hashing/verification runs inline, so tests
      // stay synchronous.
      setenv("PASSWORD_WORKERS", "0",
          1);  // No background password threads; work happens on the
               // calling thread.
      setenv("DB_WORKERS", "0",
          1);  // No background database threads; work happens inline.
      setenv("VALHALLA_TEST_NO_DB", "true",
          1);  // Flag that stops any real PostgreSQL connection from
               // being opened.

      // Make sure nothing leaks in from the developer's shell.
      unsetenv("WHITELIST");    // Remove any whitelist that could change
                                // rate-limit behaviour.
      unsetenv("BLACKLIST");    // Remove any blacklist that could change
                                // rate-limit behaviour.
      unsetenv("DB_HOST");      // Remove database host so no real connection
                                // details are used.
      unsetenv("DB_PORT");      // Remove database port for the same reason.
      unsetenv("DB_NAME");      // Remove database name for the same reason.
      unsetenv("DB_USER");      // Remove database user for the same reason.
      unsetenv("DB_PASS");      // Remove database password for the same reason.
      unsetenv("DB_SSL");       // Remove SSL setting so defaults apply.
      unsetenv("DB_MAX_CONN");  // Remove connection-count override so
                                // defaults apply.
    }
  };

  const TestEnvironmentSetup
      testEnvironmentSetup;  // The single static instance that triggers setup
                             // before main.
}  // namespace
