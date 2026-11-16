/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file appointment.hpp
 * @brief Base entity for appointment tables shared by the service-specific
 * appointment classes.
 * @details Appointment extends the generic Entity with two static
 * permission-query builders. One fetches the owner, admin and staff of a
 * service (organisation) by its id; the other finds the owning service from an
 * appointment id by joining the appointment table to its organisation table.
 * Concrete classes fix the table and organisation names and expose these
 * helpers to PermissionManager, which uses them to authorise appointment
 * operations.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Appointments belong to a service (a clinic, pharmacy, laboratory or radiology
 * centre), so access must be checked against that service's owner/admin/staff
 * rather than the appointment alone. Controllers call the queries produced here
 * before allowing an appointment to be created or read. Without Appointment,
 * each service-specific appointment class would duplicate this ownership lookup
 * and permission checks would be inconsistent.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static member function: belongs to the class, not an object, so it is
 * called as Class::fn().
 * - SQL JOIN: combines rows from two tables using a related column (here a
 * foreign key to the service id), letting us find the owning service from an
 * appointment id.
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation. (These permission queries are the exception: they use only
 * internal, trusted names and ids.)
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.
// fmt::format is used to fill names and ids into the permission queries.
#include <fmt/core.h>

#include <cstdint>   // std::uint64_t, used for service and appointment ids.
#include <optional>  // The query builders return std::optional<std::string>.
#include <string>    // std::string for SQL text and table names.

// Entity, the base class providing the CRUD statement builders.
#include "entities/base/entity.hpp"

/**
 * @class Appointment
 * @brief Base entity for appointment tables shared by the service-specific
 * appointment classes.
 * @details Extends Entity with static permission-query builders: one fetches
 * the owner/admin/staff of a service, the other resolves the owning service
 * from an appointment id by joining the appointment table to its organisation
 * table. Concrete classes fix the table and organisation names and expose these
 * helpers to PermissionManager.
 * @note These queries interpolate names and ids into the SQL text via
 * fmt::format and take no pqxx::params; they are called with internal, trusted
 * values and the text doubles as a cache key.
 * @see Entity, PermissionManager
 */
class Appointment : public Entity  // Inherits CRUD; adds permission helpers
                                   // specific to appointments.
{
 public:
  /**
   * @brief Stores the appointment payload and table name in the base Entity.
   * @tparam T Payload type held by EntityType.
   * @param[in] _data Operation payload.
   * @param[in] _tablename Appointment table name.
   */
  template <typename T>
  explicit Appointment(const T& _data, const std::string& _tablename)
      : Entity(_data,
               _tablename)  // "explicit" requires an exact call, avoiding
                            // accidental implicit conversions.
  {
  }

  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  Appointment(const Appointment&) = delete;  // No copy: one entity per request.
  Appointment(Appointment&&)      = delete;  // No move.
  Appointment& operator=(const Appointment&) = delete;  // No copy assignment.
  Appointment& operator=(Appointment&&)      = delete;  // No move assignment.

  /**
   * @brief Default destructor; no resources beyond the base Entity.
   */
  ~Appointment() override = default;  // Base destructor handles cleanup.

 protected:
  /**
   * @brief Builds a permission query for a service row by id.
   * @param[in] service_name Organisation table name (for example "clinics").
   * @param[in] service_id Primary key of the service row.
   * @return SELECT of owner_id, admin_id and staff for the given service.
   */
  static std::optional<std::string> getServicePermissionsQueryImpl(
      const std::string& service_name,
      std::uint64_t service_id)  // "Impl" suffix: shared implementation used
                                 // by concrete subclasses.
  {
    // The SQL selects owner_id, admin_id and staff from the given
    // organisation table for one service id. PermissionManager runs this to
    // see who is allowed to act on appointments in that service. Names and
    // ids are internal and trusted, so they are formatted into the text.
    return fmt::format(R"(
        SELECT
        owner_id,
        admin_id,
        staff
        FROM
        {}
        WHERE
        id = {};)",
                       service_name,
                       service_id);  // Fill in the organisation table name and
                                     // the service id.
  }

  /**
   * @brief Builds a permission query that resolves a service from an
   * appointment id.
   * @details Joins the organisation table to the appointment table on the
   * singular foreign key column and returns the owning service's
   * owner/admin/staff for the given appointment.
   * @param[in] org_name Plural organisation table name (for example
   * "clinics").
   * @param[in] appointments_table Appointment table name.
   * @param[in] appointment_id Primary key of the appointment row.
   * @return SELECT statement identifying the owning service's permission
   * fields.
   */
  static std::optional<std::string> getPermissionsQueryForReadImpl(
      const std::string& org_name,
      const std::string& appointments_table,
      std::uint64_t appointment_id)  // Finds which service owns an appointment,
                                     // then its permission fields.
  {
    // org_name is plural (e.g. "clinics") but the FK column is singular
    // (e.g. "clinic_id")
    std::string fk_col =
        org_name.back() == 's'
            ? org_name.substr(0, org_name.size() - 1) + "_id"
            : org_name + "_id";  // Derive "clinic_id" from "clinics" by
                                 // dropping the trailing 's'; handles names
                                 // without an 's' too.
    // The SQL joins organisation s to appointment a on a.<fk_col> = s.id,
    // then filters to the given appointment id, returning the service's
    // owner/admin/staff. This is how the server maps "can this person read
    // appointment X?" to "do they belong to the service that owns X?".
    return fmt::format(R"(
        SELECT
        s.owner_id,
        s.admin_id,
        s.staff
        FROM
        {} s
        JOIN {} a ON a.{} = s.id
        WHERE
        a.id = {};)",
                       org_name,
                       appointments_table,
                       fk_col,
                       appointment_id);  // Fill in the organisation table,
                                         // appointment table, foreign-key
                                         // column and appointment id.
  }
};
