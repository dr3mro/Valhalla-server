/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file asyncsqlbinder.cpp
 * @brief Implementation for asyncsqlbinder: Copies each parameter's bytes into
 * a string, or records a null when pqxx reports no value.
 * @details This file implements the three bridge functions declared in
 * asyncsqlbinder.hpp. toTextParams() copies parameters into optional strings,
 * bindParams() feeds them into a Drogon SQL binder one at a time, and
 *          execSqlCoro() creates that binder and returns an awaiter. All three
 * work from the C-style parameter view that pqxx exposes, which reports each
 * value's bytes, length and format.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the actual data-copy layer of every asynchronous database query. The
 * entity builders still hand over pqxx::params, and this file is where those
 * values are transformed into Drogon bind calls. Copying by the reported length
 * keeps embedded NUL bytes intact, and putting binary data in a shared buffer
 * keeps it alive until the query finishes rather than only until this function
 * returns. Without these details, async queries would corrupt binary values,
 * mishandle nulls, or read memory that has already gone away.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Drogon SQL binding: https://drogon.docsforge.com/master/database/
 * - C++20 coroutines and co_await:
 * https://en.cppreference.com/w/cpp/language/co_await
 * - std::shared_ptr (shared ownership of a buffer):
 * https://en.cppreference.com/w/cpp/memory/shared_ptr
 * - std::source_location (where a call came from):
 * https://en.cppreference.com/w/cpp/utility/source_location
 * - libpqxx parameters (pqxx::params): https://pqxx.org/
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// Our own declarations, checked against these definitions.
#include "database/asyncsqlbinder.hpp"

// operator<< on the client, which starts building the statement.
#include <drogon/orm/DbClient.h>
// SqlBinder and RawParameter, used when binding binary values.
#include <drogon/orm/SqlBinder.h>
// SqlAwaiter, the object the caller co_awaits.
#include <drogon/utils/coroutine.h>

#include <cstddef>    // std::size_t, the type used for indexes and lengths.
#include <pqxx/pqxx>  // pqxx::params and its make_c_params() C-style view.
// std::source_location, passed to pqxx for better conversion diagnostics.
#include <source_location>
#include <string>   // std::string, the owned copy of each value's text/bytes.
#include <utility>  // std::move, used to hand the binder to the awaiter.
#include <vector>   // std::vector, the container for the converted parameters.

namespace asyncdb
{
  /**
   * @brief Copies each parameter's bytes into a string, or records a null
   * when pqxx reports no value.
   * @details The source location is passed to pqxx for conversion
   * diagnostics. Copying uses the reported length, so embedded NUL bytes
   * survive the round trip.
   */
  std::vector<std::optional<std::string>> toTextParams(
      const pqxx::params& params)
  {
    std::vector<std::optional<std::string>>
        result;  // Start empty; `optional` lets us store "no value" for a
                 // SQL NULL.
    const auto c = params.make_c_params(
        std::source_location::current());  // Ask pqxx for its low-level
                                           // view of the values; the source
                                           // location helps pqxx
    // report where a conversion failed.

    result.reserve(
        c.values.size());  // Allocate space once up front, so adding
                           // entries cannot reallocate the vector midway.
    for (std::size_t i = 0; i < c.values.size();
        ++i)  // Walk the values in append order; stop when every parameter
              // has been copied.
    {
      if (c.values[i] == nullptr)  // A null pointer in the C view is pqxx's way
                                   // of saying "this parameter is SQL NULL".
      {
        result.emplace_back(std::nullopt);  // Record an empty optional rather
                                            // than an empty string, so NULL
                                            // stays distinguishable from "".
      }
      else
      {
        result.emplace_back(std::string(c.values[i],
            static_cast<std::size_t>(
                c.lengths[i])));  // Copy exactly the reported number of
                                  // bytes; the cast makes the length
        // unsigned, so embedded zero bytes are preserved.
      }
    }
    return result;  // The caller gets one entry per parameter, in the
                    // original order.
  }

  /**
   * @brief Emits one bind operation per parameter: NULL, binary (`format ==
   * 1`) or text.
   * @details Binary values are copied into a shared, heap-owned buffer and
   * passed as a RawParameter, so the bytes remain valid for the lifetime of
   * the binder rather than for this stack frame. Text values are copied into
   * a string.
   */
  void bindParams(
      drogon::orm::internal::SqlBinder& binder, const pqxx::params& params)
  {
    const auto c = params.make_c_params(
        std::source_location::current());  // Get the same low-level view as
                                           // toTextParams(), again with the
                                           // call site for diagnostics.

    for (std::size_t i = 0; i < c.values.size();
        ++i)  // Bind parameters one by one, in the order the builder
              // appended them.
    {
      if (c.values[i] ==
          nullptr)  // NULL values need their own bind form; binding an
                    // empty string would be a different value.
      {
        binder << nullptr;  // Drogon's operator<< recognises nullptr and
                            // sends an SQL NULL for this placeholder.
      }
      else if (c.formats[i] == 1)  // In the PostgreSQL protocol, format 1
                                   // means binary data rather than text.
      {
        auto buffer = std::make_shared<std::vector<char>>(c.values[i],
            c.values[i] +
                c.lengths[i]);  // Copy the raw bytes into a heap buffer
                                // shared through a shared_ptr, so they
                                // outlive this function's stack frame.
        binder << drogon::orm::RawParameter{buffer, buffer->data(),
            static_cast<int>(buffer->size()),
            1};  // Pass pointer, length and binary format; the
                 // shared_ptr travels with the parameter, and the cast
                 // suits Drogon's int-based API.
      }
      else
      {
        binder << std::string(c.values[i],
            static_cast<std::size_t>(
                c.lengths[i]));  // Ordinary text is copied into a
                                 // string and bound by value; Drogon
                                 // keeps that copy until the query
                                 // runs.
      }
    }
  }

  /**
   * @brief Builds and binds the statement, then transfers the binder into an
   * awaiter.
   * @details The binder is moved, not copied; no query is sent until the
   * caller co_awaits the returned object.
   */
  drogon::orm::internal::SqlAwaiter execSqlCoro(drogon::orm::DbClient& client,
      const std::string& sql, const pqxx::params& params)
  {
    auto binder = client << sql;  // Drogon's operator<< on a client creates a
                                  // binder already holding the statement text.
    bindParams(binder,
        params);  // Attach the values to that binder in placeholder order;
                  // this performs no network I/O.
    return drogon::orm::internal::SqlAwaiter(
        std::move(binder));  // Move ownership into an awaiter: nothing
                             // executes until the caller co_awaits it, and
                             // the bound data stays alive until then.
  }
}  // namespace asyncdb
