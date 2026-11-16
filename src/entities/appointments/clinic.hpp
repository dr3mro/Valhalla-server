/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file clinic.hpp
 * @brief Appointment entity bound to the clinics organisation and its
 * appointment table.
 * @details This is a thin concrete specialisation of Appointment. It fixes the
 * table name
 * ("clinics_appointments") and organisation name ("clinics"), forwards a
 * payload to the base constructor, and exposes two static permission-query
 * helpers by calling the shared Appointment implementations. It adds no state
 * or logic of its own.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers that work with clinic appointments construct this class so the
 * generic Appointment code is tied to the right table and organisation.
 * PermissionManager calls its two static helpers to authorise appointment
 * actions. Without it, the shared permission logic would have no clinic-
 * specific table and organisation names to work with.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static constexpr member: a compile-time constant stored on the class; it
 * can be used as a default argument or template value and needs no object.
 * - static constexpr auto: lets the compiler deduce the type of the constant
 * (here a string literal).
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
 * @class ClinicAppointment
 * @brief Appointment entity bound to the clinics organisation and its
 * appointment table.
 * @details A thin concrete specialisation of Appointment: it fixes TABLENAME
 * and ORGNAME and forwards the two permission-query requests to the shared base
 * implementations.
 * @see Appointment
 */
class ClinicAppointment
    : public Appointment  // "public" inheritance keeps all the base CRUD
                          // builders available.
{
 private:
  /**
   * @brief Database table holding clinic appointments.
   */
  static constexpr auto TABLENAME =
      "clinics_appointments";  // The exact table this class reads and writes;
                               // constant because it never changes.

  /**
   * @brief Plural organisation name used for permission service lookup.
   */
  static constexpr auto ORGNAME = "clinics";  // The owning organisation, used
                                              // to build permission queries.

 public:
  /**
   * @brief Stores the appointment payload, binding it to the clinic
   * appointment table.
   * @tparam T Payload type held by EntityType.
   * @param[in] _data Operation payload.
   */
  template <typename T>
  explicit ClinicAppointment(const T& _data)
      : Appointment(_data,
                    TABLENAME)  // "explicit" prevents accidental conversions;
                                // TABLENAME fixes the table for this class.
  {
  }

  /**
   * @brief No default construction; the payload and table name are required.
   */
  ClinicAppointment() = delete;  // A ClinicAppointment without a payload
                                 // makes no sense, so forbid it.

  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  ClinicAppointment(const ClinicAppointment&) = delete;  // No copy.
  ClinicAppointment(ClinicAppointment&&)      = delete;  // No move.
  ClinicAppointment& operator=(ClinicAppointment&&) =
      delete;  // No move assignment.
  ClinicAppointment& operator=(const ClinicAppointment&) =
      delete;  // No copy assignment.

  /**
   * @brief Returns the clinic appointment table name.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Static accessor for code that needs the table name without an object.

  /**
   * @brief Returns the owning organisation name ("clinics").
   */
  static constexpr auto getOrgName()
  {
    return ORGNAME;
  }  // Static accessor for the organisation name.

  /**
   * @brief Default destructor; no state beyond the base Appointment.
   */
  ~ClinicAppointment() override = default;  // Nothing extra to clean up.

  /**
   * @brief Builds the permission query for a clinic service row.
   * @param[in] service_name Organisation table name.
   * @param[in] service_id Organisation row id.
   * @return SELECT of the clinic's owner/admin/staff.
   */
  static std::optional<std::string> getServicePermissionsQuery(
      const std::string& service_name,
      std::uint64_t      service_id)  // Public wrapper with the name
                                      // PermissionManager expects.
  {
    return getServicePermissionsQueryImpl(service_name,
                                          service_id);  // Delegate all the work
                                                        // to the shared base
                                                        // implementation.
  }

  /**
   * @brief Builds the permission query that resolves the owning clinic from
   * an appointment id.
   * @param[in] appointment_id Appointment row id.
   * @return SELECT of the owning clinic's owner/admin/staff.
   */
  static std::optional<std::string> getPermissionsQueryForRead(
      std::uint64_t appointment_id)  // Public wrapper for the "find owner of
                                     // this appointment" query.
  {
    return getPermissionsQueryForReadImpl(ORGNAME,
                                          TABLENAME,
                                          appointment_id);  // Supply this
                                                            // class's clinic
                                                            // organisation and
                                                            // table names to
                                                            // the base.
  }
};
