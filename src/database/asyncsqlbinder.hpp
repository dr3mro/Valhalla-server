/**
 * @file asyncsqlbinder.hpp
 * @brief Adapter between the pqxx::params produced by entity SQL builders and
 * Drogon's incremental SQL binder.
 * @details This header declares three functions that bridge two database
 * libraries. Entity code already builds its query parameters with libpqxx's
 * pqxx::params; these functions copy those parameters into Drogon's binder so
 * the same entity code works on both the synchronous and the asynchronous
 * database path. One function also exposes the parameters as plain text, which
 * is handy for tests that have no real database client.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Whenever a controller runs SQL asynchronously it calls asyncdb::execSqlCoro()
 * from this module. That function is the only way the SQL text and its bound
 * values reach Drogon, so this file is the seam that lets the existing entity
 * SQL builders keep producing pqxx::params unchanged. It protects the server
 * from SQL injection because values are bound separately from the statement
 * text, and it preserves binary data by copying exact byte lengths. Without
 * this adapter, every entity builder would have to be rewritten for the async
 * path, and binary values or nulls could easily be corrupted as they cross
 * between the two libraries.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Drogon DbClient and SQL binding:
 * https://drogon.docsforge.com/master/database/
 * - C++20 coroutines and co_await:
 * https://en.cppreference.com/w/cpp/language/co_await
 * - libpqxx parameters (pqxx::params): https://pqxx.org/
 * - std::optional (a value that may be absent):
 * https://en.cppreference.com/w/cpp/utility/optional
 * - Parameterised queries and SQL injection:
 * https://owasp.org/www-community/attacks/SQL_Injection
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Ensure the declarations are seen only once per compiled source
              // file.

// DbClient, whose operator<< creates a binder for a statement.
#include <drogon/orm/DbClient.h>
// SqlBinder and RawParameter, the objects values are bound into.
#include <drogon/orm/SqlBinder.h>
// SqlAwaiter, the awaitable object returned by execSqlCoro().
#include <drogon/utils/coroutine.h>

// std::optional, used to represent a SQL NULL as "no value".
#include <optional>
// pqxx::params, the parameter type produced by the entity SQL builders.
#include <pqxx/pqxx>
#include <string>  // std::string, the text form of a bound value.
#include <vector>  // std::vector, the ordered list returned by toTextParams().

/**
 * @namespace asyncdb
 * @brief Adapter between the pqxx::params produced by entity SQL builders and
 * Drogon's incremental SQL binder.
 * @details Entity builders keep constructing `pqxx::params`; this module turns
 * that list into the bind calls Drogon needs, so no entity code changes for the
 * async path. Parameter order is always the order in which the builder appended
 *          values, and parameters are transported as text unless pqxx reports a
 * binary format.
 * @see execSqlCoro, bindParams, toTextParams
 */
namespace asyncdb
{
  /**
   * @brief Converts pqxx parameters into an ordered list of optional
   * plain-text values.
   * @details Reads the C parameter view so each value is copied using its
   * reported length (binary-safe). Null parameters become `std::nullopt`.
   * Useful for the vector overload of `DbClient::execSqlCoro` and for unit
   * tests that cannot construct a client.
   * @param[in] params Parameters to convert; not modified.
   * @return One entry per parameter, in append order; empty for an empty
   * parameter list.
   */
  [[nodiscard]] std::vector<std::optional<std::string>> toTextParams(
      const pqxx::params& params);  // Each element is a string or "nothing";
                                    // the vector preserves the builder's order.

  /**
   * @brief Binds a pqxx parameter list into an already-created Drogon SQL
   * binder.
   * @details Nulls are bound as SQL NULL; binary-format parameters are bound
   * as a `RawParameter` whose bytes are held in a `shared_ptr`-owned buffer
   * so they stay valid after this call; everything else is bound as a text
   * string. Values are bound in append order.
   * @param[in,out] binder Binder that already holds the statement text.
   * @param[in] params Parameters to bind; not modified.
   * @pre The number and order of parameters match the placeholders in
   * `binder`'s statement; this function does not check.
   */
  void bindParams(drogon::orm::internal::SqlBinder& binder,
                  const pqxx::params& params);  // `binder` is changed in place;
                                                // `params` is only read.

  /**
   * @brief Prepares a parameterised statement on a Drogon client and returns
   * its awaiter.
   * @details Creates a binder with `client << sql`, binds `params` in append
   * order via `bindParams`, then moves the binder into an `SqlAwaiter`. The
   * statement executes when the caller co_awaits the returned awaiter, which
   * owns the binder and its bound parameters until then.
   * @param[in] client Drogon client to execute on; must outlive the awaited
   * operation.
   * @param[in] sql Statement using `$1`, `$2`, ... placeholders.
   * @param[in] params Values for those placeholders.
   * @return Awaiter that performs the statement when awaited.
   * @note Calling this function alone does not execute SQL; the return value
   * must be co_awaited.
   */
  [[nodiscard]] drogon::orm::internal::SqlAwaiter execSqlCoro(
      drogon::orm::DbClient& client,
      const std::string&  sql,  // The client reference must stay alive; the SQL
                                // text uses numbered placeholders such as $1.
      const pqxx::params& params);  // The values for those placeholders, in
                                    // the exact order they were appended.
}  // namespace asyncdb

/** @brief Convenience alias matching the Phase 2 vocabulary; denotes the same
 * namespace as `asyncdb`. */
namespace AsyncSqlBinder = asyncdb;  // Older name kept so code written during
                                     // the async migration still compiles.
