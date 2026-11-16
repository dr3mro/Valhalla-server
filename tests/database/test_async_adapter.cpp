/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file test_async_adapter.cpp
 * @brief Catch2 unit tests and fixtures for async adapter. Checks that pqxx
 * query parameters become ordered text values for Drogon.
 * @details toTextParams() converts libpqxx query parameters into the plain
 * strings Drogon binds to a statement, and the order must be preserved so each
 * value lands in the right placeholder. The tests also pin down that a SQL NULL
 * (passed as nullptr) stays a "no value" instead of becoming the literal text
 * "null" or an empty string. Finally, an empty parameter list must produce an
 * empty result rather than an error.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It protects asyncsqlbinder.hpp, the small bridge between the pqxx PostgreSQL
 * driver and Drogon's parameter binding on the async DB path. If the order were
 * scrambled, every query would bind the wrong values to the wrong placeholders;
 * if NULL became an empty string, "unknown" data would silently turn into a
 * real value. Either regression would corrupt reads and writes across the whole
 * application.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Catch2 TEST_CASE / SECTION:
 * https://github.com/catchorg/Catch2/blob/devel/docs/test-cases-and-sections.md
 * - Catch2 CHECK / REQUIRE assertion macros:
 * https://github.com/catchorg/Catch2/blob/devel/docs/assertions.md
 * - libpqxx, the PostgreSQL C++ library supplying pqxx::params:
 * https://github.com/jtv/libpqxx
 * - std::optional ("a value that may or may not be present"):
 * https://en.cppreference.com/w/cpp/utility/optional
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Provides TEST_CASE, CHECK, REQUIRE and the other test macros.
#include <catch2/catch_test_macros.hpp>
// std::optional / std::nullopt: represents a value that may be missing (the SQL
// NULL case).
#include <optional>
// libpqxx: the PostgreSQL C++ library that supplies pqxx::params.
#include <pqxx/pqxx>
#include <string>  // std::string: the text values the converter produces.
#include <vector>  // std::vector: the ordered list of converted values.

// The production header under test; declares asyncdb::toTextParams.
#include "database/asyncsqlbinder.hpp"

// toTextParams bridges pqxx parameters to the text values Drogon binds. Two
// contracts matter here: parameter order is preserved and SQL NULL stays
// distinct from an empty string, hence nullptr maps to nullopt.

// Mixed types become strings in order; nullptr must remain nullopt.
TEST_CASE("adapter converts pqxx params to text params in order",
    "[database][async][adapter]")  // Registers the test with Catch2 under tags
                                   // so it can be run by name or filtered by
                                   // tag.
{
  pqxx::params
      params;  // An empty, ordered list of SQL parameters built with libpqxx.
  params.append(
      42);  // Add an integer; the converter must turn it into the text "42".
  params.append(std::string("hello"));  // Add a string; it must survive
                                        // unchanged and land in position 2.
  params.append(nullptr);  // Add a SQL NULL; this must become "no value", not
                           // the word "null" or an empty string.

  const auto values = asyncdb::toTextParams(
      params);  // Call the function under test; 'auto' lets the compiler pick
                // its exact return type.

  REQUIRE(values.size() ==
          3);  // Prove all three parameters were converted; REQUIRE aborts
               // here if not, so later indexing is safe.
  REQUIRE(values[0].has_value());  // The integer slot must hold a value
                                   // before we read it.
  CHECK(values[0].value() ==
        "42");  // Prove the integer became its decimal text form.
  REQUIRE(values[1].has_value());  // The string slot must hold a value before
                                   // we read it.
  CHECK(values[1].value() ==
        "hello");  // Prove the string kept its content and its position.
  CHECK_FALSE(values[2].has_value());  // Prove NULL stayed distinct from an
                                       // empty string (it has no value at all).
}

// Empty input must yield an empty vector, with no parameters to convert.
TEST_CASE("adapter handles an empty parameter list",
    "[database][async][adapter]")  // Registers the edge-case test for a query
                                   // that has zero parameters.
{
  pqxx::params params;  // Nothing is appended, so this really is an empty
                        // parameter list.
  const auto   values = asyncdb::toTextParams(
      params);  // Convert the empty list; the result should be empty too.
  CHECK(values.empty());  // Prove the converter returns an empty vector
                          // instead of crashing or inventing entries.
}
