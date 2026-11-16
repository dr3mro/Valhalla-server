/**
 * @file provider.hpp
 * @brief Convenience alias for the id-only payload used by get-services
 * lookups.
 * @details This header defines the Provider entity for the "providers" table.
 * It inherits the full validated Client behaviour (create/update with
 * validation, suspend/activate, username checks) and adds one query:
 * getSqlGetServicesStatement, which lists every clinic, pharmacy, radiology
 * centre and laboratory where the provider is an owner, an admin, or listed in
 * the JSON staff array. The provider id is bound twice because it is needed as
 * a number and as text.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * When a provider logs in, the API needs to know which organisations they
 * belong to so it can show the right workspace. Controllers construct Provider
 * and call getSqlGetServicesStatement to obtain that list. Without it there
 * would be no way to resolve a provider's memberships across the four service
 * tables in one query.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation. Here there are two markers for the same id because one is used
 * as a bigint and the other as text.
 * - UNION ALL: combines the result rows of several SELECT statements into one
 * list.
 * - JSONB: a PostgreSQL binary JSON column; jsonb_path_exists searches inside
 * it with a path.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once           // Include this header at most once per source file.
#include <fmt/core.h>  // fmt::format is used for the error log message.

#include <cstdint>    // uint64_t, the id type.
#include <exception>  // std::exception, caught by the query builder.
#include <optional>   // std::optional for ids and the builder return type.
#include <string>     // std::string for SQL text.

// Client, the base class with validated account builders.
#include "entities/base/client.hpp"
// Types::Data_t, the id-only payload used here.
#include "entities/base/types.hpp"
#include "message/message.hpp"  // Message logging for build failures.

/**
 * @brief Convenience alias for the id-only payload used by get-services
 * lookups.
 */
using Data_t = api::v2::Types::Data_t;  // Short global name for the payload
                                        // that carries just a provider id.

/**
 * @brief Convenience alias for the base Client class.
 */
using Client = api::v2::Client;  // Short global name so Provider can derive
                                 // from Client without the full namespace.

/**
 * @class Provider
 * @brief Client entity for the providers table with a get-services query.
 * @details Extends Client with a query that returns every service (clinic,
 * pharmacy, radiology centre or laboratory) where the provider is an owner,
 * admin or listed in the JSON staff array. The provider id is bound twice
 * because it is used both as a numeric key and as a JSON text value.
 * @see Client, Types::Data_t
 */
// Inherits validated account behaviour; adds
// the services lookup.
class Provider : public Client {
 public:
  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  Provider(const Provider&) = delete;  // No copy: one entity per request.
  Provider(Provider&&)      = delete;  // No move.
  Provider& operator=(const Provider&) = delete;  // No copy assignment.
  Provider& operator=(Provider&&)      = delete;  // No move assignment.

  /**
   * @brief Stores an id-only payload, binding it to the providers table.
   * @tparam ClientData_t A ClientData_t payload (typically Data_t for
   * get-services).
   * @param[in] data Operation payload carrying the provider id.
   */
  template <typename ClientData_t>
  explicit Provider(const ClientData_t& data)
      // "explicit" prevents implicit conversions;
      // TABLENAME fixes the target table.
      : Client(data,
               TABLENAME) {
  }
  // Provider() : Client(std::string(TABLENAME)) {}
  /**
   * @brief Default destructor; no state beyond the base Client.
   */
  ~Provider() override = default;  // Nothing extra to clean up.

  /**
   * @brief Returns the providers table name.
   */
  static constexpr auto getTableName() {
    return TABLENAME;
  }  // Static accessor for the table name.

  /**
   * @brief Builds a query listing the services the provider belongs to.
   * @details Reads the id from the Data_t payload into client_id, then binds
   * it as a bigint
   * ($1) and as text ($2). The text form is matched against the JSON staff
   * arrays via jsonb_path_exists; owner_id and admin_id are compared against
   * the numeric id. Results from the four service tables are combined with
   * UNION ALL.
   * @param[out] params Cleared and filled with the numeric id and its text
   * form.
   * @return The SELECT statement, or std::nullopt if no id was supplied or an
   * exception occurs.
   * @note Bindings are made into explicit params entries because the two uses
   * need different SQL types.
   */
  // Builds the "which organisations is this
  // provider in?" query.
  std::optional<std::string> getSqlGetServicesStatement(pqxx::params& params) {
    try {
      client_id = std::get<Data_t>(getData())
                      .get_id();  // Pull the provider id from the id-only
                                  // payload into the cached member.
      // Without an id the query cannot be scoped.
      if (!client_id.has_value()) {
        Message::ErrorMessage(
            // Log the caller mistake.
            "Failed to get services for provider: no id.");
        return std::nullopt;  // No statement returned.
      }
      params = {};  // Empty the caller's container so numbering starts fresh.
      params.append(client_id.value());  // $1: numeric id, used in the
                                         // bigint comparisons.
      params.append(std::to_string(
          client_id.value()));  // $2: the same id as text, compared with
                                // the JSON staff entries.
      // The SQL first builds a "vars" row holding the numeric id, then
      // UNION ALLs the id/name/ staff/admin_id/owner_id columns from all
      // four service tables. The JOIN keeps a service when the provider's
      // text id appears anywhere in its staff JSON (jsonb_path_exists) OR
      // the numeric id matches admin_id or owner_id. This returns every
      // membership in one go.
      return std::string(
          R"(
                        WITH vars AS (
                            SELECT $1::bigint AS id
                        )
                        SELECT combined.id, combined.name, combined.type
                        FROM (
                            SELECT id, name, staff, admin_id, owner_id, 'Clinic' AS type
                            FROM clinics
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'Pharmacy' AS type
                            FROM pharmacies
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'RadiologyCenter' AS type
                            FROM radiologycenters
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'Laboratory' AS type
                            FROM laboratories
                        ) AS combined
                        JOIN vars ON jsonb_path_exists(combined.staff, '$.** ? (@ == $v)'::jsonpath, jsonb_build_object('v', $2::text))
                                OR combined.admin_id = vars.id
                                // the caller.
                                // Wrong payload type or a formatting problem.
                                OR combined.owner_id = vars.id;
                        )");  // Wrap the raw SQL literal in a std::string for
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          fmt::format("Failed to create query for GetServicesStatement {}.",
                      tablename));         // Log the table.
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }

 private:
  /**
   * @brief Database table holding providers.
   */
  static constexpr auto TABLENAME =
      "providers";  // The exact table this entity targets.
};
