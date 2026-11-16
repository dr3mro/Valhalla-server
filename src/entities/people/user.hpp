/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file user.hpp
 * @brief Convenience alias for the id-only payload used by get-services
 * lookups.
 * @details This header defines the User entity for the "users" table. It
 * inherits all the validated Client behaviour (create/update with validation,
 * suspend/activate, username checks) and adds one small query,
 * getSqlGetServicesStatement, which simply selects the user's own id. Despite
 * the method name, it does not join the organisation tables the way Provider's
 * version does.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers construct User whenever they need to work with an ordinary user
 * account, reusing the safe Client builders for CRUD and suspension. The added
 * id-lookup statement provides a minimal get-services answer for users. Without
 * this class there would be no User-specific table binding for the account
 * endpoints.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation.
 * - Type alias (using Data_t = ...): a short local name for a longer qualified
 * type; no new type is created.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once           // Include this header at most once per source file.
#include <fmt/core.h>  // fmt::format is used for the error log message.

#include <exception>  // std::exception, caught by the query builder.
#include <optional>   // std::optional for ids and the builder return type.
#include <string>     // std::string for SQL text.

// Client, the base class with validated account builders.
#include "entities/base/client.hpp"
// Types::Data_t, the id-only payload used here.
#include "entities/base/types.hpp"
#include "utils/message/message.hpp"  // Message logging for build failures.

/**
 * @brief Convenience alias for the id-only payload used by get-services
 * lookups.
 */
using Data_t = api::v2::Types::Data_t;  // Short global name for the payload
                                        // that carries just a user id.

/**
 * @brief Convenience alias for the base Client class.
 */
using Client = api::v2::Client;  // Short global name so User can derive from
                                 // Client without the full namespace.

/**
 * @class User
 * @brief Client entity for the users table with an id lookup query.
 * @details Extends Client and adds a minimal get-services statement that
 * selects the user's id. CRUD and suspend/activate behaviour is inherited
 * unchanged.
 * @see Client, Types::Data_t
 */
class User : public Client  // Inherits validated account behaviour; adds the id
                            // lookup.
{
 public:
  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  User(const User&)            = delete;  // No copy: one entity per request.
  User(User&&)                 = delete;  // No move.
  User& operator=(const User&) = delete;  // No copy assignment.
  User& operator=(User&&)      = delete;  // No move assignment.

  /**
   * @brief Stores an id-only payload, binding it to the users table.
   * @tparam ClientData_t A ClientData_t payload (typically Data_t for
   * get-services).
   * @param[in] data Operation payload carrying the user id.
   */
  template <typename ClientData_t>
  explicit User(const ClientData_t& data)
      : Client(data,
               TABLENAME)  // "explicit" prevents implicit conversions;
                           // TABLENAME fixes the target table.
  {
  }
  // User() : Client(std::string(TABLENAME)) {}
  /**
   * @brief Default destructor; no state beyond the base Client.
   */
  ~User() override = default;  // Nothing extra to clean up.

  /**
   * @brief Returns the users table name.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Static accessor for the table name.

  /**
   * @brief Builds a query that selects the user id from the users table.
   * @details Reads the id from the Data_t payload into client_id and binds it
   * as $1. If the payload has no id, calling value() throws and is caught,
   * yielding std::nullopt.
   * @param[out] params Cleared and filled with the bound user id.
   * @return SELECT id FROM users WHERE id = $1, or std::nullopt on failure.
   */
  std::optional<std::string> getSqlGetServicesStatement(
      pqxx::params& params)  // Builds a minimal id-lookup statement.
  {
    try
    {
      client_id = std::get<Data_t>(getData())
                      .get_id();  // Pull the user id from the id-only
                                  // payload into the cached member.
      params    = {};  // Empty the caller's container so the id becomes $1.
      params.append(client_id.value());  // value() throws if the id is absent;
                                         // the catch below handles that.
      return std::string(
          "SELECT id FROM users WHERE id = $1");  // A simple query whose
                                                  // single parameter is
                                                  // the bound id.
    }
    catch (const std::exception& e)  // Missing id or another error.
    {
      Message::ErrorMessage(
          fmt::format("Failed to create query for GetServicesStatement {}.",
                      tablename));         // Log the table.
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }

 private:
  /**
   * @brief Database table holding users.
   */
  static constexpr auto TABLENAME =
      "users";  // The exact table this entity targets.
};
