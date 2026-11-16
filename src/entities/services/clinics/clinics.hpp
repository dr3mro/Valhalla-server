/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file clinics.hpp
 * @brief Service entity bound to the `clinics` table; the organisation root for
 * clinic cases.
 * @details This header declares the Clinics class, a very small C++ type that
 * stands for one clinic in the `clinics` database table. Like the other service
 * providers, it holds almost no logic of its own: it only names the table and
 * lets its parent class Service build the SQL. You create one by passing in a
 * data wrapper made from a client request, and the parent stores that wrapper.
 * This table is also special: it is the organisation that every clinic case
 * (patient, visit, report, ...) ultimately belongs to.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Clinics is both a service provider (clinics can be created, read, updated and
 * deleted through the service/clinic routes) and the organisation root used by
 * the permission system. Case::ORGNAME is "clinics", and every Case permission
 * query joins this table to recover the owning clinic's owner, admin and staff
 * columns. The route layer selects ServiceController<Clinics> /
 * StaffController<Clinics> for clinic endpoints. Without this file there would
 * be no C++ type tying the `clinics` table to the CRUD machinery, and the
 * permission system could not tell which clinic owns a given case.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - static constexpr: a value fixed at compile time that belongs to the class,
 * not to one object. https://en.cppreference.com/w/cpp/language/static
 * - JSON: a text format used to carry request data; this project uses the
 * jsoncons library to read it. https://jsoncons.github.io/jsoncons/
 * - Template (`template <typename T>`): a blueprint that works with whatever
 * data type the caller supplies. https://en.cppreference.com/w/cpp
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compiler-enforced header guard: include this file at most once
              // per translation unit.

// Service is the parent class that supplies the inherited CRUD/staff behaviour.
#include "entities/base/service.hpp"

/**
 * @class Clinics
 * @brief Service entity bound to the `clinics` table; the organisation root for
 * clinic cases.
 * @details Like the other service types, this is a thin value type that fixes
 * the table name for the inherited Service/Entity SQL builders. CRUD/search and
 * staff management are inherited. Instances are built from one of the
 * api::v2::Types data wrappers, which Entity stores as an EntityType variant.
 * The type satisfies api::v2::Service_t and is selected through the `clinics`
 * entries of the services and staff route registries. In addition, `clinics` is
 * the organisation referenced by Case::ORGNAME: every Case permission query
 * joins this table to recover the owning clinic's owner/admin/staff columns
 * through the case's clinic_id. Ownership: the wrapper is copied into the base
 * and is const thereafter; no other resources are owned. No mutable state or
 * locking of its own. Copy and move are deleted by Service: construct a fresh
 * instance per operation.
 * @note No default constructor: Entity needs a data wrapper to build statements
 * from.
 * @see Service, Entity, Case, ServiceController, StaffController
 */
class Clinics : public Service  // Inherits every CRUD/search/staff method; this
                                // class only fixes the table name.
{
 private:  // Implementation details kept hidden from outside callers.
  /** @brief Table bound to this entity; also the organisation table used by
   * Case permission queries. */
  static constexpr auto TABLENAME =
      "clinics";  // Fixed table name known at compile time; `auto` keeps the
                  // exact string-literal type, and constexpr lets it
                  // be used directly in SQL and permission queries.

 public:  // Public interface: construction, table-name lookup and destruction.
  /**
   * @brief Constructs a clinic entity bound to `clinics`.
   * @details Forwards the wrapper and table name to Service, which stores the
   * wrapper in Entity. The wrapper's type decides which inherited SQL builder
   * (create/read/update/delete/ search) can legitimately be used with the
   * resulting instance.
   * @tparam T A type accepted by EntityType, e.g. api::v2::Types::Create_t or
   * Read_t.
   * @param[in] _data Operation data to copy into the entity.
   */
  template <typename T>  // T is deduced from the wrapper the caller passes,
                         // so many wrapper types fit this one constructor.
  Clinics(const T& _data)
      : Service(_data,
                TABLENAME)  // Hand the data and table name to Service, which
                            // copies them into Entity.
  {
  }

  /**
   * @brief Returns the table this entity is bound to.
   * @return The constant `clinics` string.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Returns the compile-time table name so callers can build SQL for the
     // right table.

  /**
   * @brief Destructor; the entity owns nothing beyond the data wrapper held
   * by Entity.
   */
  ~Clinics() override =
      default;  // `override` confirms the parent has a virtual destructor; `=
                // default` lets the compiler clean up the stored wrapper.
};
