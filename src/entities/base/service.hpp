/**
 * @file service.hpp
 * @brief Convenience alias for the staff-invitation payload consumed by Service
 * builders.
 * @details This header defines the Service entity for organisation tables such
 * as clinics, pharmacies, laboratories and radiology centres. On top of the
 * inherited CRUD statements it adds two builders that edit the JSONB "staff"
 * array of a service row to add or remove a team member, and a static query
 * that PermissionManager uses to read a service's owner, admin and staff.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Services (organisations) need to manage teams, and permission checks need to
 * know who owns or administers a service. Controllers use Service to build the
 * jsonb_set UPDATEs that change the staff array and the SELECT that feeds
 * PermissionManager. Without it there would be no safe way to add/remove staff
 * or to authorise service-level operations.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - JSONB: PostgreSQL's binary JSON column type; jsonb_set changes a value at a
 * path inside it and the -> operator walks into a JSON object.
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation.
 * - Type alias (using StaffData = ...): a short local name for a longer
 * qualified type; no new type is created.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

#include <fmt/core.h>  // fmt::format is used to assemble the SQL statements.

#include <cstdint>    // uint64_t and std::uint64_t, used for ids.
#include <exception>  // std::exception, caught by the builders.
// jsoncons::json, referenced through the StaffData payload.
#include <jsoncons/json.hpp>
#include <optional>  // The builders return std::optional<std::string>.
#include <string>    // std::string for SQL text and table names.

// Entity and Binder, the base class and SQL helper.
#include "entities/base/entity.hpp"
// Types::StaffData, the staff-invitation payload.
#include "entities/base/types.hpp"
#include "utils/message/message.hpp"  // Message logging for build failures.

/**
 * @brief Convenience alias for the staff-invitation payload consumed by Service
 * builders.
 */
using StaffData =
    api::v2::Types::StaffData;  // Short global name so the builders below can
                                //  just write StaffData.

/**
 * @class Service
 * @brief Entity for service/organisation tables (clinics, pharmacies,
 * laboratories, ...).
 * @details Adds builders that mutate the JSONB "staff" array of a service row
 * to add or remove a nominee, plus a static query used by PermissionManager to
 * read a service's owner/admin/staff. CRUD statements remain inherited from
 * Entity.
 * @note The staff builders bind the nominee and entity ids through Binder; the
 * static permission query interpolates the service name and id into its text
 * and is used as a permission cache key.
 * @see Entity, api::v2::Types::StaffData
 */
// Inherits all CRUD builders; adds
// staff-specific and permission helpers.
class Service : public Entity {
 public:
  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  Service(const Service&) =
      delete;  // No copy: the entity belongs to one request.
  Service(Service&&)                 = delete;  // No move.
  Service& operator=(const Service&) = delete;  // No copy assignment.
  Service& operator=(Service&&)      = delete;  // No move assignment.

  /**
   * @brief Stores the payload and the service table name in the base Entity.
   * @tparam T Payload type held by EntityType (StaffData for the staff
   * builders).
   * @param[in] data Operation payload.
   * @param[in] tablename Service table name.
   */
  template <typename T>
  Service(const T& data, const std::string& tablename)
      // Forward both values to the base unchanged.
      : Entity(data, tablename) {
  }
  // Service(const std::string &tablename) : Entity(tablename) {};
  /**
   * @brief Default destructor; no resources beyond the base Entity.
   */
  ~Service() override = default;  // The base destructor is sufficient.
  // The nominee id (as JSON text) and the entity id are bound.
  /**
   * @brief Builds the UPDATE that appends a nominee id to the service's
   * Doctors staff array.
   * @details Uses jsonb_set on the mjson payload path
   * {payload,providers,Doctors}, concatenating the nominee id as a JSON text
   * value. Both the nominee id (as text) and the entity id are bound.
   * @param[out] params Cleared and filled with the bound nominee and entity
   * ids.
   * @return UPDATE ... RETURNING id, or std::nullopt if the payload is not
   * StaffData or an exception occurs.
   */
  // Builds the "add team member" statement.
  std::optional<std::string> getSqlAddStaffStatement(pqxx::params& params) {
    try {
      auto idata =
          std::get<StaffData>(getData());  // The payload must be StaffData;
                                           // std::get throws otherwise.
      Binder binder(params);  // Fresh binder for the two parameters.
      // Bound in a fixed order (argument evaluation order is
      // unspecified).
      const std::string nominee_ph = binder.bind(std::to_string(
          idata.nominee_id));  // Convert the id to text because the staff
                               // array holds text values; bind as $1.
      const std::string entity_ph =
          binder.bind(idata.entity_id);  // Bind the service row id as $2.
      // The SQL calls jsonb_set on mjson at the path
      // {payload,providers,Doctors}, concatenating
      // (||) the existing array with the nominee id wrapped by
      // to_jsonb(...::text). This adds the member without rebuilding the
      // whole array; the WHERE id clause scopes it to one service.
      return fmt::format(
          "UPDATE {} SET mjson = jsonb_set(mjson, "
          "'{{payload,providers,Doctors}}', "
          "((mjson->'payload'->'providers'->'Doctors') || "
          // Two adjacent string literals form one SQL
          // string; the placeholders are filled in order.
          // Wrong payload type or formatting error.
          "to_jsonb({}::text))) WHERE id={} RETURNING id;",
          tablename,
          nominee_ph,
          entity_ph);
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          "Error creating query for add member.");  // Log a
                                                    // human-readable
                                                    // failure.
      Message::CriticalMessage(e.what());  // Log the exception details.
      return std::nullopt;                 // Signal failure.
    }
  }

  /**
   * @brief Builds the UPDATE that removes a nominee id from the service's
   * Doctors staff array.
   * @details Rebuilds the array with jsonb_agg over the elements that do not
   * equal the bound nominee id (as JSON text). The nominee and entity ids are
   * bound in a fixed order.
   * @param[out] params Cleared and filled with the bound nominee and entity
   * ids.
   * @return UPDATE ... RETURNING id, or std::nullopt if the payload is not
   * StaffData or an exception occurs.
   */
  // Builds the "remove team member" statement.
  std::optional<std::string> getSqlRemoveStaffStatement(pqxx::params& params) {
    try {
      auto idata =
          std::get<StaffData>(getData());  // Must be a StaffData payload.
      Binder binder(params);  // Fresh binder for the two parameters.
      // Bound in a fixed order (argument evaluation order is
      // unspecified).
      const std::string nominee_ph = binder.bind(
          std::to_string(idata.nominee_id));  // Nominee id as text -> $1.
      const std::string entity_ph =
          binder.bind(idata.entity_id);  // Service row id -> $2.
      // The SQL expands the Doctors array with jsonb_array_elements,
      // keeps only elements that do NOT equal the nominee id, and
      // re-aggregates them with jsonb_agg. jsonb_set then writes that
      // smaller array back at the same path for the one service row.
      return fmt::format(
          "UPDATE {} SET mjson = jsonb_set(mjson, "
          "'{{payload,providers,Doctors}}', "
          "(SELECT jsonb_agg(elem) FROM "
          "jsonb_array_elements(mjson->'payload'->'providers'->'Doctors')"
          " AS elem "
          // Placeholders fill the nominee comparison and the
          // row id.
          // Wrong payload type or formatting error.
          "WHERE elem <> to_jsonb({}::text))) WHERE id={} RETURNING id;",
          tablename,
          nominee_ph,
          entity_ph);
    } catch (const std::exception& e) {
      Message::ErrorMessage(
          // Log a human-readable failure.
          "Error creating query for remove member.");
      Message::CriticalMessage(e.what());  // Log details.
      return std::nullopt;                 // Signal failure.
    }
  }
  /**
   * @brief Builds the permission lookup for a service row by id.
   * @details Returns the owner/admin/staff columns used by PermissionManager
   * to authorise service-level actions. The table name and id are formatted
   * into the query text rather than bound; the resulting text also serves as
   * the permission cache key.
   * @param[in] service_name Name of the service/organisation table.
   * @param[in] service_id Primary key of the service row.
   * @return A SELECT statement (always produced by this implementation).
   */
  // Static: called directly by the permission
  // layer, no object needed.
  static std::optional<std::string> getServicePermissionsQuery(
      const std::string& service_name, std::uint64_t service_id) {
    return fmt::format(
        "SELECT owner_id,admin_id,staff FROM {} WHERE id = '{}' LIMIT 1;",
        service_name,
        service_id);  // Read who owns/administers/staffs the one service
                      // row; LIMIT 1 because ids are unique.
  }

 private:  // No private members; this section is intentionally empty but kept
           // for symmetry with siblings.
};

// (Intentionally left blank: all helpers above are public or inherited.)
