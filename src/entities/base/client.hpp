/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file client.hpp
 * @brief Username column name used by Client::exists() and the client
 * validation layer.
 * @details This header defines the Client entity for "people" tables such as
 * users and providers. It replaces the generic JSON-based create and update
 * builders from Entity with versions that consume already-validated (column,
 * value) pairs, so untrusted JSON never reaches SQL generation. It also builds
 * the UPDATE that toggles the active flag (suspend/activate), and can check
 * whether a username is already taken in the table.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * User and provider accounts need stricter handling than ordinary records:
 * their input is validated and password-hashed before insertion, they can be
 * suspended, and usernames must be unique. Controllers use Client (via User and
 * Provider) to produce those specialised statements while reusing
 * read/delete/search from Entity. Without it, account creation would bypass
 * validation and there would be no safe way to suspend a client.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - C++ concept: a named compile-time requirement on a template type; here it
 * restricts Client helpers to payload types that really carry client data or an
 * id.
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation.
 * - "final" on a virtual function means subclasses may not override it again -
 * a guard that keeps the validated create/update path from being bypassed.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// Core formatting helpers for building statements and log messages.
#include <fmt/core.h>
#include <sys/types.h>  // System type definitions needed by included headers.

// std::ranges::find_if, used to locate the username entry.
#include <algorithm>
#include <cstdint>    // uint64_t, the id type.
#include <exception>  // std::exception, caught by the builders.
#include <iterator>   // Iterator helpers used by range-based code.
// jsoncons::json, referenced through the payload types.
#include <jsoncons/json.hpp>
#include <memory>  // std::shared_ptr, used indirectly by the cached controller.
#include <optional>     // std::optional for ids.
#include <string>       // std::string for SQL text and the username.
#include <type_traits>  // std::is_same, used to define the concept below.
#include <vector>       // std::vector collects column names and values.

// Entity and Binder, the base class and SQL helper.
#include "entities/base/entity.hpp"
// The client payload types (CreateClient_t, UpdateClient_t, ...).
#include "entities/base/types.hpp"
#include "fmt/format.h"  // fmt::format / fmt::join for statement assembly.
// Shared global helpers used by this hierarchy.
#include "utils/global/global.hpp"
#include "utils/message/message.hpp"  // Message logging for build failures.
/**
 * @brief Username column name used by Client::exists() and the client
 * validation layer.
 */
#define USERNAME \
  "username"  // A macro constant so the column name is spelled identically
              // everywhere it is used.

namespace api  // Outer namespace for the whole API.
{
  namespace v2  // Version 2 of the API.
  {
    /**
     * @brief Constrains Client helpers to the payload types that carry
     * client data or a client id.
     * @details Satisfied by CreateClient_t and UpdateClient_t (which expose
     * a validated data_set), and by SuspendData and Data_t (which carry an
     * id only).
     */
    template <typename T>
    concept ClientData_t =
        std::is_same<T, Types::CreateClient_t>::value ||
        std::is_same<T, Types::UpdateClient_t>::value ||
        std::is_same<T, Types::SuspendData>::value ||
        std::is_same<T,
                     Types::Data_t>::value;  // A compile-time whitelist: only
                                             // these four payloads make sense
                                             // for Client helpers.

    /**
     * @class Client
     * @brief Entity for client-style tables (users, providers) with
     * dedicated create/update builders.
     * @details Replaces Entity's generic JSON-based create/update builders
     * with versions that consume the validated (column, value) pairs held
     * by CreateClient_t/UpdateClient_t. Read, delete and search remain
     * inherited from Entity. It also builds the active-flag toggle used to
     * suspend/activate a client and can test whether a username is already
     * taken. Concrete tables (User, Provider) derive from this class and
     * supply the table name.
     * @note All values in generated statements are bound through Binder;
     * column names come from the validated data_set. The id used by
     * updates/suspends is cached in client_id.
     * @see Entity, Types::CreateClient_t, Types::UpdateClient_t
     */
    class Client : public Entity  // Inherits CRUD; overrides only the parts
                                  // that need validation.
    {
     public:
      /**
       * @brief Non-copyable and non-movable; construct in place for the
       * request that uses it.
       */
      Client(const Client&)            = delete;  // No copy.
      Client(Client&&)                 = delete;  // No move.
      Client& operator=(const Client&) = delete;  // No copy assignment.
      Client& operator=(Client&&)      = delete;  // No move assignment.
      // Client(const std::string &tablename) : Entity(tablename) {};

      /**
       * @brief Stores the client payload and table name in the base
       * Entity.
       * @tparam T A ClientData_t payload (CreateClient_t, UpdateClient_t,
       * SuspendData or Data_t).
       * @param[in] data Operation payload.
       * @param[in] tablename Client table name (for example "users" or
       * "providers").
       */
      template <typename T>
      Client(const T& data, const std::string& tablename)
          : Entity(data,
                   tablename)  // Forward both to the base; the table will be
                               // "users" or "providers".
      {
      }

      /**
       * @brief Client-specific INSERT builder; final so subclasses cannot
       * bypass validation.
       * @param[out] params Cleared and filled with the bound column
       * values.
       * @return INSERT ... RETURNING id, or std::nullopt on failure.
       */
      std::optional<std::string> getSqlCreateStatement(pqxx::params& params)
          final  // "final" locks this validated implementation in place.
      {
        Binder binder(params);  // Fresh binder clears params and
                                // restarts numbering.
        return buildClientCreateStatement(
            binder);  // Delegate to the validated-pair builder below.
      }

      /**
       * @brief Client-specific UPDATE builder; final so subclasses cannot
       * bypass validation.
       * @param[out] params Cleared and filled with the bound SET values
       * and id.
       * @return UPDATE ... RETURNING id, or std::nullopt if no id is
       * present.
       */
      std::optional<std::string> getSqlUpdateStatement(pqxx::params& params)
          final  // "final" again: updates must use validated data too.
      {
        Binder binder(params);  // Fresh binder.
        return buildClientUpdateStatement(
            binder);  // Delegate to the validated-pair UPDATE builder.
      }

      /**
       * @brief Builds the statement that sets the client's active flag to
       * state.
       * @details Reads the client id from the SuspendData payload into
       * client_id, then binds the state and id in a fixed order before
       * formatting the UPDATE.
       * @param[in] state Desired active value: false suspends, true
       * activates.
       * @param[out] params Cleared and filled with the bound state and
       * id.
       * @return UPDATE ... RETURNING id,active, or std::nullopt if the
       * payload is not SuspendData or another exception occurs.
       */
      std::optional<std::string> getSqlToggleSuspendStatement(
          bool          state,
          pqxx::params& params)  // One shared builder for both suspend
                                 // and activate.
      {
        try
        {
          Types::SuspendData suspenddata = std::get<Types::SuspendData>(
              getData());  // The payload must be SuspendData, or
                           // std::get throws.
          client_id = suspenddata.client_id;  // Cache the id so callers can
                                              // retrieve it with getClientId().
          Binder binder(params);  // Fresh binder for the two parameters.
          // Bound in a fixed order (argument evaluation order is
          // unspecified).
          const std::string state_ph =
              binder.bind(state);  // Bind the boolean first -> $1.
          const std::string id_ph =
              binder.bind(client_id.value());  // Bind the id second -> $2.
          return fmt::format(
              "UPDATE {} SET active={} WHERE id={} RETURNING "
              "id,active;",
              tablename,
              state_ph,
              id_ph);  // RETURNING confirms the new state.
        }
        catch (const std::exception&
                   e)  // Wrong payload type or a formatting problem.
        {
          Message::ErrorMessage(
              fmt::
                  format("Failed to create Sql suspend statement for table {}.",
                         tablename));          // Log the table.
          Message::CriticalMessage(e.what());  // Log details.
          return std::nullopt;                 // Signal failure.
        }
      }

      /**
       * @brief Builds the statement that deactivates the client (active =
       * false).
       * @param[out] params Cleared and filled with the bound state and
       * id.
       * @return The suspend UPDATE, or std::nullopt on failure.
       */
      std::optional<std::string> getSqlSuspendStatement(pqxx::params& params)
      {
        return getSqlToggleSuspendStatement(false, params);
      }  // Thin wrapper: suspend means active = false.

      /**
       * @brief Builds the statement that reactivates the client (active =
       * true).
       * @param[out] params Cleared and filled with the bound state and
       * id.
       * @return The activate UPDATE, or std::nullopt on failure.
       */
      std::optional<std::string> getSqlActivateStatement(pqxx::params& params)
      {
        return getSqlToggleSuspendStatement(true, params);
      }  // Thin wrapper: activate means active = true.

      /**
       * @brief Returns the client id cached by the last update or suspend
       * builder call.
       * @return The cached id, or nullopt if no builder has set it yet.
       */
      std::optional<uint64_t> getClientId() const
      {
        return client_id;
      }  // Lets callers know which id the built statement targets.

      /**
       * @brief Checks the database for a client with the username in the
       * validated payload.
       * @tparam T A payload type exposing get_data_set() (CreateClient_t
       * or UpdateClient_t).
       * @return true only when a username entry exists and the database
       * reports a match; false when there is no username entry or the
       * lookup finds nothing.
       * @note Logs nothing on a negative result; database failures
       * collapse to false.
       * @throws std::bad_variant_access if T is not the held alternative.
       */
      template <typename T>
      bool exists()  // Used to reject duplicate usernames before inserting.
      {
        auto client_data = std::get<T>(getData())
                               .get_data_set();  // The validated column/value
                                                 // pairs from the payload.
        auto iterator = std::ranges::find_if(client_data,
                                             [&](const auto& item)
                                             {
                                               return item.first == USERNAME;
                                             });  // Find the pair whose column
                                                  // is "username".

        if (iterator != client_data.end())  // Only check when the payload
                                            // actually contains a username.
        {
          std::string username =
              iterator->second;  // The value part of the pair.
          auto result =
              databaseController()
                  ->checkItemExists(tablename,
                                    USERNAME,
                                    username);  // Ask the database whether that
                                                // username already exists.
          return result.value_or(false);  // A failed/unset lookup counts as
                                          // "does not exist".
        }
        return false;  // No username in the payload means nothing to
                       // check.
      }
      /**
       * @brief Default destructor; the cached id needs no cleanup.
       */
      ~Client() override = default;  // No resources owned by this class.

     protected:
      // Statement builders (see Base::Binder).
      /**
       * @brief Builds the client INSERT from validated column/value
       * pairs.
       * @details Keys come from the validated data_set; values are bound.
       * Returns only the generated id so callers can look the new row up
       * separately.
       * @tparam Binder Binder-like type providing bind().
       * @param[in,out] binder Appends values and returns their
       * placeholders.
       * @return INSERT ... RETURNING id, or std::nullopt if the payload
       * is not CreateClient_t or another exception occurs.
       */
      template <typename Binder>
      std::optional<std::string> buildClientCreateStatement(
          Binder& binder)  // Builds the INSERT for a validated account
                           // payload.
      {
        try
        {
          auto clientdata = std::get<Types::CreateClient_t>(
              getData());  // Must be a create payload; std::get
                           // throws otherwise.

          std::vector<std::string>
              keys_arr;  // Column names from the validated set.
          std::vector<std::string>
              values_arr;  // Matching placeholders in the same order.

          for (const auto& item :
               clientdata.get_data_set())  // Walk each validated
                                           // (column, value) pair.
          {
            keys_arr.push_back(item.first);  // The column name is trusted
                                             // because it came from validation.
            values_arr.push_back(
                binder.bind(item.second));  // The value is bound safely as a
                                            // parameter.
          }

          return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;",
                             tablename,
                             fmt::join(keys_arr, ","),
                             fmt::join(values_arr,
                                       ","));  // RETURNING id gives back the
                                               // new account id.
        }
        catch (
            const std::exception& e)  // Wrong payload type or formatting error.
        {
          Message::ErrorMessage(
              fmt::format("Failed to create Sql create statement for table {}.",
                          tablename));         // Log the table.
          Message::CriticalMessage(e.what());  // Log details.
          return std::nullopt;                 // Signal failure.
        }
      }

      /**
       * @brief Builds the client UPDATE from validated column/value pairs
       * and the payload id.
       * @details Caches the id in client_id and aborts when it is
       * missing, so no unscoped UPDATE is emitted. Each value is bound;
       * column names come from the validated data_set.
       * @tparam Binder Binder-like type providing bind().
       * @param[in,out] binder Appends SET values and the id, returning
       * their placeholders.
       * @return UPDATE ... RETURNING id, or std::nullopt if the id is
       * missing, the payload is not UpdateClient_t, or another exception
       * occurs.
       */
      template <typename Binder>
      std::optional<std::string> buildClientUpdateStatement(
          Binder& binder)  // Builds the UPDATE for a validated account
                           // payload.
      {
        try
        {
          auto clientdata = std::get<Types::UpdateClient_t>(getData())
                                .get_data_set();  // Validated (column, value)
                                                  // pairs to set.
          client_id       = std::get<Types::UpdateClient_t>(getData())
                                .get_id();  // Cache the target id for callers.
          if (!client_id.has_value())  // Without an id the UPDATE would hit
                                       // every row, so stop.
          {
            Message::ErrorMessage(
                fmt::format("Failed to update client data. No id "
                            "provided."));  // Log the missing id.
            return std::nullopt;            // No statement returned.
          }

          std::vector<std::string>
              set_clauses;  // One "column = $n" string per field.
          for (auto it = clientdata.begin(); it != clientdata.end();
               ++it)  // Walk every validated pair.
          {
            set_clauses.push_back(
                fmt::format("{} = {}",
                            it->first,
                            binder.bind(
                                it->second)));  // Column name is trusted; value
                                                // is bound.
          }

          return fmt::format("UPDATE {} SET {} WHERE id={} RETURNING id;",
                             tablename,
                             fmt::join(set_clauses, ", "),
                             binder.bind(
                                 client_id
                                     .value()));  // Bind the id last so its
                                                  // placeholder follows the SET
                                                  // values.
        }
        catch (
            const std::exception& e)  // Wrong payload type or formatting error.
        {
          Message::ErrorMessage(
              fmt::format("Failed to create Sql update statement for table {}.",
                          tablename));         // Log the table.
          Message::CriticalMessage(e.what());  // Log details.
          return std::nullopt;                 // Signal failure.
        }
      }

      /**
       * @brief Id extracted by the most recent update or suspend builder
       * call.
       */
      std::optional<uint64_t> client_id;
      /*NOLINT*/  // Cached target id; optional because no builder may
                  // have run yet. The lint marker suppresses a style
                  // warning.
    };
  }  // namespace v2
}  // namespace api
