/**
 * @file laboratory.hpp
 * @brief Appointment entity bound to the laboratories organisation and its
 * appointment table.
 * @details This is a thin concrete specialisation of Appointment. It fixes the
 * table name
 * ("laboratories_appointments") and organisation name ("laboratories"),
 * forwards a payload to the base constructor, and exposes two static
 * permission-query helpers by calling the shared Appointment implementations.
 * It adds no state or logic of its own.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers that work with laboratory appointments construct this class so
 * the generic Appointment code is tied to the right table and organisation.
 * PermissionManager calls its two static helpers to authorise appointment
 * actions. Without it, the shared permission logic would have no
 * laboratory-specific table and organisation names to work with.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static constexpr member: a compile-time constant stored on the class; it
 * can be used without creating an object.
 * - Inheritance: deriving from Appointment means this class automatically gets
 * its CRUD builders.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// std::uint64_t, the type used for ids in the permission helpers.
#include <cstdint>
// std::optional<std::string>, the return type of the helpers.
#include <optional>
#include <string>  // std::string for table names.

// Appointment, the base class with the shared permission-query implementations.
#include "entities/appointments/base/appointment.hpp"

/**
 * @class LaboratoryAppointment
 * @brief Appointment entity bound to the laboratories organisation and its
 * appointment table.
 * @details Fixes TABLENAME and ORGNAME and forwards the two permission-query
 * requests to the shared Appointment implementations.
 * @see Appointment
 */
class LaboratoryAppointment
    : public Appointment  // Public inheritance keeps the base CRUD interface
                          // available.
{
 private:
  /**
   * @brief Database table holding laboratory appointments.
   */
  static constexpr auto TABLENAME =
      "laboratories_appointments";  // The exact table this class reads and
                                    // writes.

  /**
   * @brief Plural organisation name used for permission service lookup.
   */
  static constexpr auto ORGNAME =
      "laboratories";  // The owning organisation for permission queries.

 public:
  /**
   * @brief Stores the appointment payload, binding it to the laboratory
   * appointment table.
   * @tparam T Payload type held by EntityType.
   * @param[in] _data Operation payload.
   */
  template <typename T>
  explicit LaboratoryAppointment(const T& _data)
      : Appointment(_data,
                    TABLENAME)  // "explicit" requires a direct call; TABLENAME
                                // fixes the target table.
  {
  }

  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  // No copy.
  LaboratoryAppointment(const LaboratoryAppointment&) = delete;
  // No move.
  LaboratoryAppointment(LaboratoryAppointment&&) = delete;
  LaboratoryAppointment& operator=(const LaboratoryAppointment&) =
      delete;  // No copy assignment.
  LaboratoryAppointment& operator=(LaboratoryAppointment&&) =
      delete;  // No move assignment.

  /**
   * @brief Returns the laboratory appointment table name.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Static accessor for the table name.

  /**
   * @brief Returns the owning organisation name ("laboratories").
   */
  static constexpr auto getOrgName()
  {
    return ORGNAME;
  }  // Static accessor for the organisation name.

  /**
   * @brief Default destructor; no state beyond the base Appointment.
   */
  ~LaboratoryAppointment() override = default;  // Nothing extra to clean up.

  /**
   * @brief Builds the permission query for a laboratory service row.
   * @param[in] service_name Organisation table name.
   * @param[in] service_id Organisation row id.
   * @return SELECT of the laboratory's owner/admin/staff.
   */
  static std::optional<std::string> getServicePermissionsQuery(
      const std::string& service_name,
      std::uint64_t      service_id)  // Public wrapper with the expected name.
  {
    return getServicePermissionsQueryImpl(service_name,
                                          service_id);  // Delegate to the
                                                        // shared base
                                                        // implementation.
  }

  /**
   * @brief Builds the permission query that resolves the owning laboratory
   * from an appointment id.
   * @param[in] appointment_id Appointment row id.
   * @return SELECT of the owning laboratory's owner/admin/staff.
   */
  static std::optional<std::string> getPermissionsQueryForRead(
      std::uint64_t appointment_id)  // Public wrapper for the owner lookup.
  {
    return getPermissionsQueryForReadImpl(ORGNAME,
                                          TABLENAME,
                                          appointment_id);  // Supply this
                                                            // class's
                                                            // organisation and
                                                            // table names to
                                                            // the base.
  }
};
