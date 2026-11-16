/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file visitdrugs.hpp
 * @brief Case entity for the `patients_visitdrugs` table (drugs recorded for a
 * visit).
 * @details This header declares the VisitDrugs class, a small C++ type that
 * stands for the drugs recorded for one visit in the `patients_visitdrugs`
 * database table. Despite the table name starting with `patients_`, ownership
 * is resolved through the visit (the create payload carries a visit id). The
 * class itself holds no data; it names the table and leaves SQL generation to
 * its parent Case. It also supplies small helper functions that build
 *          permission-checking SQL for create, read, update and delete
 * operations.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * VisitDrugs is one of the clinic-owned "case" types exposed through the clinic
 * routes as ClinicController<VisitDrugs> under the `visitDrugs` service type.
 * When a client wants to add or change a visit's drugs, PermissionManager calls
 * the static getPermissionsQueryFor* helpers here to build a SELECT that finds
 * which clinic owns the visit (via `clinics_visits`), then checks whether the
 * requesting user is that clinic's owner, admin or staff. Without this file
 * there would be no C++ type connecting the `patients_visitdrugs` table to the
 * CRUD and permission machinery, so visit-drug endpoints could not work.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - static constexpr: a value fixed at compile time that belongs to the class,
 * not to one object. https://en.cppreference.com/w/cpp/language/static
 * - JSON: a text format used to carry request data; this project uses the
 * jsoncons library to read it. https://jsoncons.github.io/jsoncons/
 * - Foreign key: a column that points to a row in another table, used here to
 * trace a record back to its owning visit and then to the owning clinic.
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
 * @class VisitDrugs
 * @brief Case entity for the `patients_visitdrugs` table (drugs recorded for a
 * visit).
 * @details A thin value type that binds the generic Case/Entity CRUD machinery
 * to one table. Despite the `patients_` prefix of the table name,
 * create-authorisation is resolved through a visit: CREATE_KEY identifies a
 * `clinics_visits` row, whose clinic_id links to the owning clinic. SQL
 * generation is inherited; this class only fixes the table name and the
 * reference table. The static getPermissionsQueryFor* helpers are taken as
 *          function pointers by
 * PermissionManager::canCreate/canRead/canUpdate/canDelete for api::v2::Case_t
 * T. Registered as ClinicController<VisitDrugs> under the `visitDrugs`
 *          serviceType.
 *          Ownership: the data wrapper is copied into Entity and is const
 * thereafter; no mutable state and no locking of its own. Copy and move are
 * deleted by Case.
 * @note No default constructor: Entity needs a data wrapper to build statements
 * from.
 * @see Case, Entity, PermissionManager, ClinicController
 */
class VisitDrugs : public Case  // Inherits the CRUD builders and adds
                                // visit-drugs permission-query helpers.
{
 private:  // Implementation details kept hidden from outside callers.
  /** @brief Table bound to this entity; used by CRUD builders and the
   * read/update/delete permission joins.
   *  @note No definition of `patients_visitdrugs` exists in
   * scripts/db/init.sql, so its schema cannot be established from this
   * repository.
   */
  static constexpr auto TABLENAME =
      "patients_visitdrugs";  // The SQL table this class reads and writes;
                              // fixed at compile time so it can be pasted
                              // straight into queries.
  /** @brief Table whose `id` is the create-payload key and whose `clinic_id`
   * identifies the owning clinic. */
  static constexpr auto KEYREFTABLENAME =
      "clinics_visits";  // On create we only know a visit id, so we join
                         // `clinics_visits` to discover which clinic owns it.
  /** @brief Create-payload key holding the visit id used to resolve clinic
   * ownership. */
  static constexpr auto CREATE_KEY =
      "id";  // Name of the JSON field in a create request that carries that
             // visit id.

 public:  // Public interface: construction, lookups and permission-query
          // builders.
  // VisitDrugs() : Case(TABLENAME) {}  // Deliberately disabled: Entity needs
  // a data wrapper before it can build any statement.

  /**
   * @brief Constructs a visit-drugs entity bound to `patients_visitdrugs`.
   * @details Forwards the wrapper and table name to Case, which stores the
   * wrapper in Entity. The wrapper's alternative decides which inherited SQL
   * builder is applicable.
   * @tparam T A type accepted by EntityType, e.g. api::v2::Types::Create_t or
   * Read_t.
   * @param[in] _data Operation data to copy into the entity.
   */
  template <typename T>  // T is deduced from the wrapper the caller passes,
                         // so several wrapper types work here.
  VisitDrugs(const T& _data)
      : Case(_data,
            TABLENAME)  // Pass the data and table name up to Case, which
                        // stores them in Entity.
  {
  }

  /**
   * @brief Returns the table this entity is bound to.
   * @return The constant `patients_visitdrugs` string.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Lets callers (e.g. PermissionManager) build SQL for the correct table.
  /**
   * @brief Returns the create-payload key used to resolve the owning clinic.
   * @return The constant `id` string (the visit id).
   */
  static constexpr auto getCreateKey()
  {
    return CREATE_KEY;
  }  // Exposes the JSON field name so the controller can read the visit id
     // consistently.

  /**
   * @brief Default destructor; the entity owns nothing beyond the data
   * wrapper held by Entity.
   */
  ~VisitDrugs() override =
      default;  // `override` confirms the parent has a virtual destructor; `=
                // default` keeps the compiler-generated cleanup.

  /**
   * @brief Builds the SQL that resolves the owning clinic for a create
   * request.
   * @details Reads the visit id from `data_j` using CREATE_KEY and delegates
   * to the Case base, which joins `clinics` with `clinics_visits` on
   * clinic_id. PermissionManager runs the returned SELECT before the insert;
   * no insert occurs here.
   * @param[in] data_j Create payload; must be engaged and contain an
   * unsigned-integer CREATE_KEY (the optional is dereferenced without a
   * has_value() check).
   * @return The SELECT statement, or std::nullopt if reading or converting
   * the key throws; the exception is logged via CRITICALMESSAGE.
   * @see Case::getPermissionsQueryForCreateImpl
   */
  static std::optional<std::string> getPermissionsQueryForCreate(
      const std::optional<jsoncons::json>&
          data_j)  // Returns a query string, or nothing if the payload is
                   // malformed.
  {
    uint64_t id;  // Will hold the visit id pulled from the JSON payload; an
                  // unsigned 64-bit integer matches database ids.
    try  // Reading JSON can fail (missing field or wrong type), so guard
         // it.
    {
      id = data_j->at(CREATE_KEY)
               .as<uint64_t>();  // Dereference the optional payload, look
                                 // up the "id" field and convert it to an
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

    return getPermissionsQueryForCreateImpl(id,
        KEYREFTABLENAME);  // Hand the id and `clinics_visits` table to
                           // Case, which writes the join SQL that finds the
                           // owning clinic.
  }

  /**
   * @brief Builds the SQL that fetches the owning clinic's permission columns
   * for a row id.
   * @details Delegates to the Case base, joining `clinics` with
   * `patients_visitdrugs` on clinic_id.
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
