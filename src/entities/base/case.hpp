/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file case.hpp
 * @brief Base entity for clinic-owned clinical records (patients, visits,
 * reports, ...).
 * @details Case extends the generic Entity with a fixed organisation name
 * ("clinics") and a family of static permission-query builders. Each builder
 * writes a SELECT that finds out who owns, administers or works at the clinic
 * responsible for a record, so PermissionManager can decide whether a requester
 * may create, read, update or delete that record. Concrete record entities
 * inherit from Case and expose these builders.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Clinical records are owned by clinics, not by individual users, so ordinary
 * table-level permissions are not enough. PermissionManager calls the queries
 * produced here to discover the owning clinic's owner_id, admin_id and staff
 * list before authorising an action. Without Case, every clinical entity would
 * have to duplicate this ownership lookup and the server could not enforce
 * "only clinic staff may see this patient's data".
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static member function: a function that belongs to the class, not to an
 * object, so it can be called as Class::function() without constructing
 * anything.
 * - SQL JOIN: combines rows from two tables using a related column (here clinic
 * id); a LEFT JOIN keeps rows from the first table even if no match exists in
 * the second.
 * - SQL parameter binding: put $1, $2, ... markers in the SQL and pass the real
 * values separately; see https://en.cppreference.com/w/cpp or the libpqxx
 * documentation. (These particular queries are the exception: they use only
 * internal, trusted ids and table names.)
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// fmt::format is used to fill ids and table names into the permission queries.
#include <fmt/core.h>

#include <cstdint>   // uint64_t, the type used for record and clinic ids.
#include <optional>  // The builders return std::optional<std::string>.
#include <string>    // std::string holds the generated SQL.

// Entity, the base class that supplies CRUD statement builders.
#include "entities/base/entity.hpp"
// Global helpers used by derived code (kept available for this hierarchy).
#include "utils/global/global.hpp"
/**
 * @class Case
 * @brief Base entity for clinic-owned clinical records (patients, visits,
 * reports, ...).
 * @details Extends Entity with a fixed organisation name ("clinics") and a
 * family of static permission-query builders. Concrete service entities derive
 * from Case and expose those builders to PermissionManager, which executes the
 * returned query to decide whether a requester may create/read/update/delete a
 * record. The builders select the owner, admin and staff columns of the owning
 * clinic (joined to the record table where needed).
 * @note Unlike the CRUD builders in Entity, these queries interpolate the id
 * and table names into the SQL text via fmt::format and take no pqxx::params;
 * they are called with internal, trusted values and the resulting text is also
 * used as a cache key by PermissionManager.
 * @see Entity, PermissionManager
 */
class Case : public Entity  // Inherits the full CRUD interface, then adds
                            // permission-query helpers.
{
 public:
  /**
   * @brief Organisation this case belongs to; used as the permission service
   * name.
   */
  static constexpr auto ORGNAME =
      "clinics";  // A compile-time constant: every clinical record belongs to
                  // the "clinics" organisation.

  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  Case(const Case&) = delete;  // No copy: the entity is tied to one request.
  Case(Case&&)      = delete;  // No move: keep the object where it was built.
  Case& operator=(const Case&) = delete;  // No copy assignment.
  Case& operator=(Case&&)      = delete;  // No move assignment.

  /**
   * @brief Forwards a payload and table name to Entity.
   * @tparam Data Payload type accepted by EntityType.
   * @param[in] data Operation payload.
   * @param[in] tablename Table name, also used as the permission group name.
   */
  template <typename Data>
  Case(const Data& data, const std::string& tablename)
      : Entity(data,
               tablename)  // Just pass both arguments up to the base; Case
                           // itself adds no state.
  {
  }

  /**
   * @brief Returns the owning organisation name ("clinics").
   */
  static constexpr auto getOrgName()
  {
    return ORGNAME;
  }  // Static accessor so callers can ask for the organisation without an
     // object.

  /**
   * @brief Default destructor; Case owns no resources of its own.
   */
  ~Case() override = default;  // The base destructor handles everything;
                               // "override" confirms the signature matches.

 protected:
  /**
   * @brief Builds the permission query for creating a patient row.
   * @details Reads the owner/admin/staff columns of the clinic identified by
   * id.
   * @param[in] id Primary key of the clinic.
   * @return SELECT statement, or std::nullopt if it cannot be built
   * (currently it always returns the formatted text).
   */
  static std::optional<std::string> getPermissionsQueryForCreatePatientImpl(
      const uint64_t id)  // Used when creating the patient itself, where the
                          // clinic id is known directly.
  {
    // The SQL below selects owner_id, admin_id and staff from the clinics
    // table for one clinic (WHERE id = <the clinic id>). Those three
    // columns tell PermissionManager who is allowed to act on a patient in
    // that clinic. Ids and the table name are internal, trusted values, so
    // they are formatted into the text rather than bound; the text is also
    // a cache key.
    auto query = fmt::format(R"(
        SELECT
        owner_id,
        admin_id,
        staff
        FROM
        {}
        WHERE
        id = {};)",
                             ORGNAME,
                             id);  // {} is replaced by the "clinics" table name
                                   // and the clinic id.
    // std::cout << query << std::endl;
    return query;  // Hand the finished statement to the caller (wrapped in
                   // optional).
  }

  /**
   * @brief Builds the permission query for creating a record linked to a
   * patient.
   * @details Joins the clinic table with the record table on clinic_id and
   * filters by the record id, returning the clinic's owner/admin/staff plus
   * patient_id and clinic_id.
   * @param[in] id Primary key of the record (for example a patient id).
   * @param[in] tablename Table holding the record's clinic_id foreign key.
   * @return SELECT statement (always produced by this implementation).
   */
  static std::optional<std::string> getPermissionsQueryForCreateImpl(
      const uint64_t id,
      const std::string&
          tablename)  // Used when the record points at a patient, so the
                      // owning clinic must be found via a join.
  {
    // create and return the query
    // The SQL joins clinics (alias c) to the record table (alias p) on c.id
    // = p.clinic_id, then filters to the one record id. It returns the
    // clinic's owner/admin/staff plus patient_id and clinic_id, giving the
    // permission layer both who is allowed and which patient/clinic is
    // involved. LEFT JOIN keeps the clinic row even if the match is
    // missing.
    auto query = fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
                             ORGNAME,
                             tablename,
                             id);  // Fill in the clinics table, the record
                                   // table and the record id.
    // std::cout << query << std::endl;
    return query;  // Return the statement to PermissionManager.
  }

  /**
   * @brief Builds the permission query for reading a record linked to a
   * patient.
   * @param[in] id Primary key of the record.
   * @param[in] tablename Table holding the record's clinic_id foreign key.
   * @return SELECT statement identifying the owning clinic's
   * owner/admin/staff.
   */
  static std::optional<std::string> getPermissionsQueryForReadImp(
      const uint64_t id,
      const std::string&
          tablename)  // Same shape as create, used for read authorisation.
  {
    // create and return the query
    // Joins clinics to the record table on clinic_id and filters by this
    // record id, returning the clinic's owner/admin/staff so read access
    // can be checked against clinic membership.
    auto query =
        fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
                    ORGNAME,
                    tablename,
                    id);  // Fill in clinics table, record table and record id.
    // std::cout << query << std::endl;
    return query;  // Return the statement.
  }

  /**
   * @brief Builds the permission query for updating a record linked to a
   * patient.
   * @param[in] id Primary key of the record.
   * @param[in] tablename Table holding the record's clinic_id foreign key.
   * @return SELECT statement identifying the owning clinic's
   * owner/admin/staff.
   */
  static std::optional<std::string> getPermissionsQueryForUpdateImpl(
      const uint64_t id,
      const std::string&
          tablename)  // Same shape, used for update authorisation.
  {
    // create and return the query
    // Joins clinics to the record table on clinic_id and filters by this
    // record id, returning the clinic's owner/admin/staff so write access
    // can be checked against clinic membership.
    auto query =
        fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
                    ORGNAME,
                    tablename,
                    id);  // Fill in clinics table, record table and record id.
    // std::cout << query << std::endl;
    return query;  // Return the statement.
  }

  /**
   * @brief Builds the permission query for deleting a record linked to a
   * patient.
   * @param[in] id Primary key of the record.
   * @param[in] tablename Table holding the record's clinic_id foreign key.
   * @return SELECT statement identifying the owning clinic's
   * owner/admin/staff.
   */
  static std::optional<std::string> getPermissionsQueryForDeleteImp(
      const uint64_t id,
      const std::string&
          tablename)  // Same shape, used for delete authorisation.
  {
    // create and return the query
    // Joins clinics to the record table on clinic_id and filters by this
    // record id, returning the clinic's owner/admin/staff so delete access
    // can be checked against clinic membership.
    auto query =
        fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
                    ORGNAME,
                    tablename,
                    id);  // Fill in clinics table, record table and record id.
    // std::cout << query << std::endl;
    return query;  // Return the statement to PermissionManager.
  }
};
