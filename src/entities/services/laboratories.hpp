/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file laboratories.hpp
 * @brief Service entity bound to the `laboratories` table (a laboratory service
 * provider).
 * @details This header declares the Laboratories class, a very small C++ type
 * that stands for one laboratory in the `laboratories` database table. It holds
 * almost no logic of its own. It only names the table and lets its parent class
 * Service do the real work of building SQL statements. You create one by
 * passing in a data wrapper made from a client request, and the parent stores
 * that wrapper for later use.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Laboratories is one of the "service provider" types in the server, next to
 * pharmacies and radiology centres. When a client creates, reads, updates or
 * deletes a laboratory, or manages its staff, the route layer picks this class
 * as ServiceController<Laboratories> / StaffController<Laboratories>. Those
 * controllers call the inherited methods from Service and Entity, which produce
 * the actual SQL. The class also publishes the table name so PermissionManager
 * can check that the requesting staff member is allowed to change this service.
 * Without this file there would be no C++ type tying the `laboratories` table
 * to the generic CRUD machinery, so laboratory endpoints could not work.
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

// jsoncons JSON library: reads the request payloads this entity may be built
// from.
#include <jsoncons/json.hpp>

// Service is the parent class that supplies the inherited CRUD/staff behaviour.
#include "entities/base/service.hpp"

/**
 * @class Laboratories
 * @brief Service entity bound to the `laboratories` table (a laboratory service
 * provider).
 * @details A thin value type that binds the generic Service/Entity SQL
 * machinery to one database table. CRUD/search statement generation and staff
 * management are entirely inherited; this class only fixes the table name and
 * exposes it to the permission layer. Instances are built from one of the
 * api::v2::Types data wrappers (for example Create_t or Read_t), which Entity
 * stores as an EntityType variant. The type satisfies the api::v2::Service_t
 *          concept and is selected through the `laboratories` entries of the
 * services and staff route registries (ServiceController<Laboratories>,
 * StaffController<Laboratories>). Ownership: the wrapper is copied into the
 * base and is const thereafter; no other resources are owned. Instances have no
 * mutable state and do no locking of their own; PermissionManager uses the
 * inherited Service::getServicePermissionsQuery together with getTableName() to
 * authorise staff changes. Copy and move are deleted by Service: construct a
 * fresh instance per operation.
 * @note No default constructor: Entity needs a data wrapper to build statements
 * from.
 * @see Service, Entity, ServiceController, StaffController, PermissionManager
 */
class Laboratories : public Service  // Inherits every CRUD/search/staff method;
                                     // this class only fixes the table name.
{
 private:  // Implementation details kept hidden from outside callers.
  /** @brief Table bound to this entity; also used as the service name in
   * staff permission queries. */
  static constexpr auto TABLENAME =
      "laboratories";  // A fixed table name known at compile time; `auto`
                       // lets the compiler pick the exact string-literal
                       // type, and constexpr means it never
  // changes so it can be used directly in generated SQL and permission
  // checks.

 public:  // Public interface: construction, table-name lookup and destruction.
  // Laboratories() : Service(TABLENAME) {}  // Deliberately disabled: Entity
  // needs a data wrapper before it can build any statement.

  /**
   * @brief Constructs a laboratory entity bound to `laboratories`.
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
  Laboratories(const T& _data)
      : Service(_data,
            TABLENAME)  // Hand the data and table name to Service, which
                        // copies them into Entity.
  {
  }
  /**
   * @brief Returns the table this entity is bound to.
   * @return The constant `laboratories` string.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Returns the compile-time table name so callers (e.g.
     // PermissionManager) can build the right SQL.
  /**
   * @brief Destructor; the entity owns nothing beyond the data wrapper held
   * by Entity.
   */
  ~Laboratories() override =
      default;  // `override` confirms the parent has a virtual destructor; `=
                // default` lets the compiler clean up the stored wrapper.
};
