/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file patient.hpp
 * @brief Case entity for the `patients` table; the patient record owned by a
 * clinic.
 * @details This header declares the Patient class, a small C++ type that stands
 * for one patient in the `patients` database table. A patient belongs directly
 * to a clinic, so this class stores the clinic id in a create payload and
 * checks it directly against the `clinics` table. It holds no data of its own;
 * it names the table and leaves SQL generation to its parent Case. It is also
 * the only case entity that builds a "get visits" query, and it supplies
 * permission-checking SQL helpers for create, read, update and delete.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Patient is a central clinic-owned "case" type exposed through the clinic
 * routes as ClinicController<Patient> under the `patients` service type; those
 * routes also cover search and getvisits. When a client adds or changes a
 * patient, PermissionManager uses the static getPermissionsQueryFor* helpers
 * here to confirm the requester belongs to the clinic named in the payload. The
 * GetVisits helpers return the SQL that lists a patient's visits, used by the
 * visits feature. Without this file there would be no C++ type connecting the
 * `patients` table to the CRUD, permission and visits machinery, so patient and
 * visit endpoints could not work.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - static constexpr: a value fixed at compile time that belongs to the class,
 * not to one object. https://en.cppreference.com/w/cpp/language/static
 * - JSON: a text format used to carry request data; this project uses the
 * jsoncons library to read it. https://jsoncons.github.io/jsoncons/
 * - `= delete`: explicitly forbidding an operation such as copying, so the
 * compiler rejects it with a clear error. https://en.cppreference.com/w/cpp
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compiler-enforced header guard: include this file at most once
              // per translation unit.

// fmt formatting library: used by the Case base when building SQL text.
#include <fmt/core.h>

// std::uint64_t: the fixed-width integer type used for database ids.
#include <cstdint>
// std::exception: base type caught when reading/converting JSON fails.
#include <exception>
// jsoncons core JSON type used by the data wrappers.
#include <jsoncons/basic_json.hpp>
// jsoncons JSON library: reads the create payload from which a key is
// extracted.
#include <jsoncons/json.hpp>
// std::optional: a value that may be absent, used for return values and ids.
#include <optional>
#include <string>  // std::string: the SQL statements and error text built here.

// Case is the parent class that supplies CRUD and permission-query builders.
#include "entities/base/case.hpp"
// api::v2::Types wrappers, including the Data_t alias used by the visits
// lookup.
#include "entities/base/types.hpp"

/** @brief Local alias for the id-only data wrapper used by Patient's visits
 * lookup. */
using Data_t = api::v2::Types::Data_t;  // Shorter name for the wrapper that
                                        // carries just a patient id.

/**
 * @class Patient
 * @brief Case entity for the `patients` table; the patient record owned by a
 * clinic.
 * @details A thin value type binding the generic Case/Entity CRUD machinery to
 * one table. SQL generation is inherited; this class fixes the table name and
 * overrides create authorisation: because a patient belongs directly to a
 * clinic, the create payload's `clinic_id` is checked against `clinics` without
 * the patient-table join used by the other Case types. This is also the only
 * Case that builds a visits query; the controller uses it in
 * GetVisits/GetVisitsAsync (guarded by `requires std::is_same<T, Patient>`).
 *          The static getPermissionsQueryFor* helpers are passed to
 * PermissionManager for api::v2::Case_t T. The type is registered as
 * ClinicController<Patient> under the `patients` serviceType, which also owns
 * search and getvisits routes. Ownership: the wrapper is copied into Entity and
 * is const thereafter; no mutable state and no locking of its own. Copy and
 * move are explicitly deleted: create a fresh instance per operation.
 * @note No default constructor: Entity needs a data wrapper to build statements
 * from.
 * @see Case, Entity, PermissionManager, ClinicController
 */
class Patient : public Case  // Inherits the CRUD builders and adds
                             // patient-specific permission and visits helpers.
{
 private:  // Implementation details kept hidden from outside callers.
  /** @brief Table bound to this entity; used by CRUD builders and the
   * permission joins. */
  static constexpr auto TABLENAME =
      "patients";  // The SQL table this class reads and writes; the two
                   // spaces align the '=' with the constant below.
  /** @brief Create-payload key: the clinic the new patient belongs to,
   * checked directly in `clinics`. */
  static constexpr auto CREATE_KEY =
      "clinic_id";  // Name of the JSON field carrying the owning clinic's id,
                    // checked straight against the clinics table.

 public:  // Public interface: construction, lookups, visits query and
          // permission builders.
  /** @brief Patients are neither copyable nor movable; construct one per
   * operation. */
  Patient(const Patient&) = delete;  // Copying is forbidden because the stored
                                     // wrapper should not be duplicated.
  /** @brief See the deleted copy constructor. */
  Patient(Patient&&)      = delete;  // Moving is also forbidden; build a fresh
                                     // instance for each request instead.
  /** @brief See the deleted copy constructor. */
  Patient& operator=(
      const Patient&) = delete;  // Copy assignment is forbidden for the same
                                 // reason as copy construction.
  /** @brief See the deleted copy constructor. */
  Patient& operator=(
      Patient&&) = delete;  // Move assignment is forbidden as well.

  /**
   * @brief Constructs a patient entity bound to `patients`.
   * @details Forwards the wrapper and table name to Case, which stores the
   * wrapper in Entity. GetVisits additionally requires that wrapper to be a
   * Data_t holding the patient id.
   * @tparam T A type accepted by EntityType, e.g. api::v2::Types::Create_t or
   * Data_t.
   * @param[in] _data Operation data to copy into the entity.
   */
  template <typename T>  // The wrapper type is deduced from the caller's
                         // argument.
  explicit Patient(const T& _data)
      : Case(_data,
            TABLENAME)  // `explicit` prevents accidental implicit conversion;
                        // data and table name go up to Case.
  {
  }
  /**
   * @brief Default destructor; the entity owns nothing beyond the data
   * wrapper held by Entity.
   */
  ~Patient() override =
      default;  // `override` confirms the parent has a virtual destructor; `=
                // default` keeps the compiler-generated cleanup.

  /**
   * @brief Returns the table this entity is bound to.
   * @return The constant `patients` string.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Lets callers (e.g. PermissionManager) build SQL for the correct table.
  /**
   * @brief Returns the create-payload key used to resolve the owning clinic.
   * @return The constant `clinic_id` string.
   */
  static constexpr auto getCreateKey()
  {
    return CREATE_KEY;
  }  // Exposes the JSON field name so the controller can read the clinic id
     // consistently.

  /**
   * @brief Builds the SELECT that lists all visits of this patient.
   * @details Resets `params`, binds the patient id held in the Data_t
   * wrapper, and returns the query over `clinics_visits`.
   * Controller::GetVisits / GetVisitsAsync execute it with those same params
   * and serialise the rows back to the caller.
   * @param[in,out] params Bound parameters, cleared before the patient id is
   * appended.
   * @return The `SELECT * FROM clinics_visits WHERE patient_id = $1;`
   * statement.
   * @pre The entity must hold a Data_t with an id.
   * @throws std::bad_variant_access if the stored wrapper is not a Data_t.
   * @throws std::bad_optional_access if that wrapper has no id.
   * @note These exceptions are not caught here; the controller's handler
   * catches them.
   */
  std::optional<std::string> getSqlGetVisitsStatement(
      pqxx::params& params)  // Returns the SQL text; `params` receives the
                             // bound id separately.
  {
    params = {};  // Reset the parameter list so no value from an earlier
                  // call leaks into this query.
    params.append(std::get<Data_t>(getData())
            .get_id()
            .value());  // Pull the Data_t wrapper from Entity, take its id,
                        // and bind it as the first query parameter ($1).
    return std::string(
        // `$1` is a placeholder filled by the bound id; this avoids SQL
        // injection.
        "SELECT * FROM clinics_visits WHERE patient_id = $1;");
  }

  /**
   * @brief Builds the SQL that resolves the owning clinic for a create
   * request.
   * @details Reads `clinic_id` from `data_j` and calls the Case base's
   * patient-specific helper, which selects owner/admin/staff directly from
   * `clinics` where id equals that value. PermissionManager runs the returned
   * SELECT before the insert; no insert occurs here.
   * @param[in] data_j Create payload; must be engaged and contain an
   * unsigned-integer clinic_id (the optional is dereferenced without a
   * has_value() check).
   * @return The SELECT statement, or std::nullopt if reading or converting
   * the key throws; the exception is logged via CRITICALMESSAGE.
   * @see Case::getPermissionsQueryForCreatePatientImpl
   */
  static std::optional<std::string> getPermissionsQueryForCreate(
      const std::optional<jsoncons::json>&
          data_j)  // Returns a query string, or nothing if the payload is
                   // malformed.
  {
    uint64_t id;  // Will hold the clinic id pulled from the JSON payload.
    try  // Reading JSON can fail (missing field or wrong type), so guard
         // it.
    {
      id = data_j->at(CREATE_KEY)
               .as<uint64_t>();  // Dereference the optional payload, look
                                 // up "clinic_id" and convert it to an
                                 // integer.
    }
    catch (const std::exception& e)  // Any read/conversion problem lands here.
    {
      CRITICALMESSAGE  // Log the failure (macro uses the caught
                       // exception) so the problem is visible in server
                       // logs.
          return std::nullopt;  // No trustworthy id means no safe query
                                // can be built, so report failure to the
                                // caller.
    }

    return getPermissionsQueryForCreatePatientImpl(
        id);  // Patients own their clinic link directly, so no join table
              // is needed; Case builds the SELECT from `clinics`.
  }

  /**
   * @brief Builds the SQL that fetches the owning clinic's permission columns
   * for a row id.
   * @details Delegates to the Case base, joining `clinics` with `patients` on
   * clinic_id.
   * @param[in] id Row id to authorise.
   * @return The SELECT statement; the base helper has no failure path despite
   * the optional return.
   */
  static std::optional<std::string> getPermissionsQueryForRead(
      const uint64_t id)
  {
    return getPermissionsQueryForReadImp(id, TABLENAME);
  }  // One-line pass-through to Case, swapping in this class's table name.

  /**
   * @brief Builds the SQL that fetches the owning clinic's permission columns
   * for an update.
   * @param[in] id Row id to authorise.
   * @return The SELECT statement; the base helper has no failure path despite
   * the optional return.
   */
  static std::optional<std::string> getPermissionsQueryForUpdate(
      const uint64_t id)
  {
    return getPermissionsQueryForUpdateImpl(id, TABLENAME);
  }  // Same idea for update: build the permission SELECT with our table name.

  /**
   * @brief Builds the SQL that fetches the owning clinic's permission columns
   * for a delete.
   * @param[in] id Row id to authorise.
   * @return The SELECT statement; the base helper has no failure path despite
   * the optional return.
   */
  static std::optional<std::string> getPermissionsQueryForDelete(
      const uint64_t id)
  {
    return getPermissionsQueryForDeleteImp(id, TABLENAME);
  }  // Same idea for delete: build the permission SELECT with our table name.
};
