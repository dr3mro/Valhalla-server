/**
 * @file visits.hpp
 * @brief Case entity for the `clinics_visits` table (a patient's visit to a
 * clinic).
 * @details This header declares the Visits class, a small C++ type that stands
 * for one patient visit in the `clinics_visits` database table. Unlike the
 * one-row-per-parent case tables, a visit has its own sequence-generated id as
 * well as a patient_id and a clinic_id. The class itself holds no data; it
 * names the table and leaves SQL generation to its parent Case. It also
 * supplies small helper functions that build permission-checking SQL for
 * create, read, update and delete operations.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Visits is one of the clinic-owned "case" types exposed through the clinic
 * routes as ClinicController<Visits> under the `visits` service type. When a
 * client wants to add or change a visit, PermissionManager calls the static
 * getPermissionsQueryFor* helpers here to build a SELECT that starts from the
 * payload's patient_id, joins `patients` to find the owning clinic, then checks
 * whether the requesting user is that clinic's owner, admin or staff. The visit
 * id is also referenced by other case tables (paid services, prescriptions,
 * requests, visit drugs). Without this file there would be no C++ type
 * connecting the `clinics_visits` table to the CRUD and permission machinery.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - static constexpr: a value fixed at compile time that belongs to the class,
 * not to one object. https://en.cppreference.com/w/cpp/language/static
 * - JSON: a text format used to carry request data; this project uses the
 * jsoncons library to read it. https://jsoncons.github.io/jsoncons/
 * - Sequence-generated id: a number the database produces automatically for
 * each new row, used here as the visit's primary key.
 * https://en.cppreference.com/w/cpp
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compiler-enforced header guard: include this file at most once
              // per translation unit.

// jsoncons JSON library: reads the create payload from which a key is
// extracted.
#include <jsoncons/json.hpp>

// Case is the parent class that supplies CRUD and permission-query builders.
#include "entities/base/case.hpp"

/**
 * @class Visits
 * @brief Case entity for the `clinics_visits` table (a patient's visit to a
 * clinic).
 * @details A thin value type that binds the generic Case/Entity CRUD machinery
 * to one table. The table has its own sequence-generated id plus a patient_id
 * and a clinic_id. Unlike the one-row-per-parent case tables,
 * create-authorisation here starts from the payload's patient_id and joins
 * `clinics` with `patients` to find the owning clinic. SQL generation is
 * inherited; this class only fixes the table name, reference table and key. The
 * static getPermissionsQueryFor* helpers are taken as function pointers by
 *          PermissionManager::canCreate/canRead/canUpdate/canDelete for
 * api::v2::Case_t T. Registered as ClinicController<Visits> under the `visits`
 * serviceType. Ownership: the data wrapper is copied into Entity and is const
 * thereafter; no mutable state and no locking of its own. Copy and move are
 * deleted by Case.
 * @note No default constructor: Entity needs a data wrapper to build statements
 * from.
 * @see Case, Entity, PermissionManager, ClinicController, Patient
 */
// Inherits the CRUD builders and adds visit
// permission-query helpers.
class Visits : public Case {
 private:  // Implementation details kept hidden from outside callers.
  /** @brief Table bound to this entity; used by CRUD builders and the
   * read/update/delete permission joins. */
  static constexpr auto TABLENAME =
      "clinics_visits";  // The SQL table this class reads and writes; fixed
                         // at compile time so it can be pasted straight into
                         // queries.
  /** @brief Table whose `id` is the create-payload key and whose `clinic_id`
   * identifies the owning clinic. */
  static constexpr auto KEYREFTABLENAME =
      "patients";  // On create we are given a patient id, so we join
                   // `patients` to discover which clinic owns that patient.
  /** @brief Create-payload key holding the patient id used to resolve clinic
   * ownership. */
  static constexpr auto CREATE_KEY =
      "patient_id";  // Name of the JSON field in a create request that
                     // carries the patient id.

 public:  // Public interface: construction, lookups and permission-query
          // builders.
  // Visits() : Case(TABLENAME) {}  // Deliberately disabled: Entity needs a
  // data wrapper before it can build any statement.

  /**
   * @brief Constructs a visits entity bound to `clinics_visits`.
   * @details Forwards the wrapper and table name to Case, which stores the
   * wrapper in Entity. The wrapper's alternative decides which inherited SQL
   * builder is applicable.
   * @tparam T A type accepted by EntityType, e.g. api::v2::Types::Create_t or
   * Read_t.
   * @param[in] _data Operation data to copy into the entity.
   */
  template <typename T>  // T is deduced from the wrapper the caller passes,
                         // so several wrapper types work here.
  Visits(const T& _data)
      // Pass the data and table name up to Case, which
      // stores them in Entity.
      : Case(_data, TABLENAME) {
  }

  /**
   * @brief Returns the table this entity is bound to.
   * @return The constant `clinics_visits` string.
   */
  static constexpr auto getTableName() {
    return TABLENAME;
  }  // Lets callers (e.g. PermissionManager) build SQL for the correct table.
  /**
   * @brief Returns the create-payload key used to resolve the owning clinic.
   * @return The constant `patient_id` string.
   */
  static constexpr auto getCreateKey() {
    return CREATE_KEY;
  }  // Exposes the JSON field name so the controller can read the patient id
     // consistently.

  /**
   * @brief Default destructor; the entity owns nothing beyond the data
   * wrapper held by Entity.
   */
  ~Visits() override =
      default;  // `override` confirms the parent has a virtual destructor; `=
                // default` keeps the compiler-generated cleanup.

  /**
   * @brief Builds the SQL that resolves the owning clinic for a create
   * request.
   * @details Reads the patient id from `data_j` using CREATE_KEY and
   * delegates to the Case base, which joins `clinics` with `patients` on
   * clinic_id. PermissionManager runs the returned SELECT before the insert;
   * no insert occurs here.
   * @param[in] data_j Create payload; must be engaged and contain an
   * unsigned-integer patient_id (the optional is dereferenced without a
   * has_value() check).
   * @return The SELECT statement, or std::nullopt if reading or converting
   * the key throws; the exception is logged via CRITICALMESSAGE.
   * @see Case::getPermissionsQueryForCreateImpl
   */
  // Returns a query string, or nothing if the payload is
  // malformed.
  static std::optional<std::string> getPermissionsQueryForCreate(
      const std::optional<jsoncons::json>& data_j) {
    uint64_t id;  // Will hold the patient id pulled from the JSON payload;
                  // an unsigned 64-bit integer matches database ids.
    // Reading JSON can fail (missing field or wrong type), so guard
    // it.
    try {
      // Dereference the optional payload, look
      // up the "patient_id" field and convert
      // it to an integer.
      // Any read/conversion problem lands here.
      id = data_j->at(CREATE_KEY).as<uint64_t>();
    } catch (const std::exception& e) {
      CRITICALMESSAGE  // Log the failure (macro uses the caught
                       // exception) so the problem is visible in server
                       // logs.
          return std::nullopt;  // No trustworthy id means no safe query
                                // can be built, so report failure to the
                                // caller.
    }

    return getPermissionsQueryForCreateImpl(id,
                                            KEYREFTABLENAME);  // Hand the id
                                                               // and `patients`
                                                               // table to Case,
                                                               // which writes
                                                               // the join SQL
                                                               // that finds the
                                                               // owning clinic.
  }

  /**
   * @brief Builds the SQL that fetches the owning clinic's permission columns
   * for a row id.
   * @details Delegates to the Case base, joining `clinics` with
   * `clinics_visits` on clinic_id.
   * @param[in] id Row id to authorise.
   * @return The SELECT statement; the base helper has no failure path despite
   * the optional return.
   */
  static std::optional<std::string> getPermissionsQueryForRead(
      const uint64_t id) {
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
      const uint64_t id) {
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
      const uint64_t id) {
    return getPermissionsQueryForDeleteImp(id, TABLENAME);
  }  // Same idea for delete: build the permission SELECT with our table name.
};
