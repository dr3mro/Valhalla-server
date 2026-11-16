/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_asyncdbclient.cpp
 * @brief Catch2 unit tests and fixtures for asyncdbclient. Checks the injected
 * Drogon client holder and the generated PostgreSQL connection string.
 * @details AsyncDbClient stores a Drogon DbClient pointer (which may
 * legitimately be null in tests or when the DB is disabled) and turns a
 * DatabaseConfig into a libpq connection string. The tests prove the stored
 * client is handed back unchanged, that the process-wide singleton can be
 * swapped for a fake and then restored, and that every DB_* setting plus the
 * SSL flag shows up in the generated string. Dummy host and password values are
 * used, so no real credentials or connection are involved.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects asyncdbclient.hpp, the holder through which all async DB calls
 * reach Drogon and the place where configuration becomes a real connection
 * string. If the singleton replacement leaked state, later tests would silently
 * reuse a null client; if a setting were missing or misspelled (host, port,
 * dbname, user, password, sslmode), the server would fail to connect at startup
 * or connect with the wrong security mode.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - libpqxx and its libpq connection-string format (host=... port=... etc.):
 * https://github.com/jtv/libpqxx
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Drogon's database client type, the object AsyncDbClient stores.
#include <drogon/orm/DbClient.h>

// Provides TEST_CASE and the CHECK assertion macro.
#include <catch2/catch_test_macros.hpp>
// std::make_shared / std::shared_ptr: shared ownership of the injected client.
#include <memory>
// std::string and its find()/npos, used to inspect the connection string.
#include <string>

// Configurator::DatabaseConfig, the settings used to build the connection
// string.
#include "configurator/configurator.hpp"
// The production header under test: AsyncDbClient.
#include "database/asyncdbclient.hpp"
// Store, the process-wide singleton registry used as the injection seam.
#include "store/store.hpp"

// AsyncDbClient has two jobs: hold the injected Drogon client and render the
// libpq connection string from DatabaseConfig. The host/password values used
// below are dummy fixtures, not real credentials.

// A null client is accepted deliberately: the disabled/test state needs no
// validation and no connection.
// A null client is accepted deliberately: the disabled/test state needs no
// validation and no connection.
TEST_CASE("AsyncDbClient keeps the client it is given",
          "[database][asyncdb]")  // Registers the test for storing the injected
                                  // client.
{
  AsyncDbClient empty(
      nullptr);  // Construct with a null client; this represents "no DB
                 // attached", which must be allowed.
  CHECK(empty.client() ==
        nullptr);  // Prove the accessor returns exactly what was passed in,
                   // without substituting anything.
}

// Store replacement is the injection seam; the original is restored so the
// test does not leak a null client into other cases.
TEST_CASE("AsyncDbClient replaces the registered singleton in tests",
          "[database][asyncdb]")  // Registers the test for swapping the Store's
                                  // singleton.
{
  auto original = Store::getObject<AsyncDbClient>(
      nullptr);  // Remember the current object so we can put it back; nullptr
                 // is the fallback if none exists.
  auto fake = std::make_shared<AsyncDbClient>(
      nullptr);  // Build a fresh fake with shared ownership, safe to keep in
                 // the Store.
  Store::replaceObject<AsyncDbClient>(
      fake);  // Inject the fake: this is the "seam" that lets tests avoid a
              // real DB client.
  CHECK(Store::getObject<AsyncDbClient>(nullptr) ==
        fake);  // Prove the Store now returns our fake, not the old object.
  Store::replaceObject<AsyncDbClient>(
      original);  // Restore the original so this test leaves no state behind
                  // for other cases.
}

// Each configured field must appear in the generated libpq connection string.
TEST_CASE("AsyncDbClient connection string carries the DB_* settings",
          "[database][asyncdb]")  // Registers the test that checks every config
                                  // value is rendered.
{
  Configurator::DatabaseConfig
      config;  // A fresh, empty settings object to fill in by hand.
  config.host = "db.example";  // Dummy host name (a fake, not a real server).
  config.port = 6543;  // Dummy port number; using an unusual value makes it
                       // easy to spot in the output.
  config.name = "valhalla";  // Database name that must become "dbname=...".
  config.user = "app";       // User name that must become "user=...".
  config.pass = "secret";    // Dummy password that must become "password=...".

  const std::string text = AsyncDbClient::connectionString(
      config);  // Build the connection string from all those fields.
  CHECK(text.find("host=db.example") !=
        std::string::npos);  // find() != npos means the substring is present;
                             // proves the host was included.
  CHECK(text.find("port=6543") !=
        std::string::npos);  // Prove the port was included with its exact
                             // value.
  CHECK(text.find("dbname=valhalla") !=
        std::string::npos);  // Prove the database name was rendered under
                             // libpq's "dbname" key.
  CHECK(text.find("user=app") !=
        std::string::npos);  // Prove the user was included.
  CHECK(text.find("password=secret") !=
        std::string::npos);  // Prove the password was included (dummy value
                             // only; never a real secret).
}

// ssl selects sslmode=require; clearing it selects sslmode=disable.
TEST_CASE("AsyncDbClient connection string honours DB_SSL",
          "[database][asyncdb]")  // Registers the test for the SSL toggle.
{
  Configurator::DatabaseConfig
      config;  // A fresh settings object; only the SSL field matters here.
  config.host = "db.example";  // Supply a host so the string is realistic,
                               // though this test only checks sslmode.
  config.ssl  = true;          // Turn SSL on for the first assertion.

  CHECK(AsyncDbClient::connectionString(config).find("sslmode=require") !=
        std::string::npos);  // Prove "on" becomes sslmode=require (encrypted
                             // connection enforced).

  config.ssl = false;  // Flip the same object to off, reusing it for the
                       // opposite case.
  CHECK(AsyncDbClient::connectionString(config).find("sslmode=disable") !=
        std::string::npos);  // Prove "off" becomes sslmode=disable rather
                             // than being omitted.
}
