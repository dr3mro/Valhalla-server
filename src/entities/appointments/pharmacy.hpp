/**
 * @file pharmacy.hpp
 * @brief Appointment entity bound to the pharmacies organisation and its
 * appointment table.
 * @details This is a thin concrete specialisation of Appointment. It fixes the
 * table name
 * ("pharmacies_appointments") and organisation name ("pharmacies"), forwards a
 * payload to the base constructor, and exposes two static permission-query
 * helpers by calling the shared Appointment implementations. It adds no state
 * or logic of its own.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Controllers that work with pharmacy appointments construct this class so the
 * generic Appointment code is tied to the right table and organisation.
 * PermissionManager calls its two static helpers to authorise appointment
 * actions. Without it, the shared permission logic would have no
 * pharmacy-specific table and organisation names to work with.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static constexpr member: a compile-time constant stored on the class,
 * usable without an object.
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
 * @class PharmacyAppointment
 * @brief Appointment entity bound to the pharmacies organisation and its
 * appointment table.
 * @details Fixes TABLENAME and ORGNAME and forwards the two permission-query
 * requests to the shared Appointment implementations.
 * @see Appointment
 */
class PharmacyAppointment
    : public Appointment  // Public inheritance keeps the base CRUD interface
                          // available.
{
 private:
  /**
   * @brief Database table holding pharmacy appointments.
   */
  static constexpr auto TABLENAME =
      "pharmacies_appointments";  // The exact table this class reads and
                                  // writes.

  /**
   * @brief Plural organisation name used for permission service lookup.
   */
  static constexpr auto ORGNAME =
      "pharmacies";  // The owning organisation for permission queries.

 public:
  /**
   * @brief Stores the appointment payload, binding it to the pharmacy
   * appointment table.
   * @tparam T Payload type held by EntityType.
   * @param[in] _data Operation payload.
   */
  template <typename T>
  explicit PharmacyAppointment(const T& _data)
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
  PharmacyAppointment(const PharmacyAppointment&) = delete;
  PharmacyAppointment(PharmacyAppointment&&)      = delete;  // No move.
  PharmacyAppointment& operator=(const PharmacyAppointment&) =
      delete;  // No copy assignment.
  PharmacyAppointment& operator=(PharmacyAppointment&&) =
      delete;  // No move assignment.

  /**
   * @brief Returns the pharmacy appointment table name.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Static accessor for the table name.

  /**
   * @brief Returns the owning organisation name ("pharmacies").
   */
  static constexpr auto getOrgName()
  {
    return ORGNAME;
  }  // Static accessor for the organisation name.

  /**
   * @brief Default destructor; no state beyond the base Appointment.
   */
  ~PharmacyAppointment() override = default;  // Nothing extra to clean up.

  /**
   * @brief Builds the permission query for a pharmacy service row.
   * @param[in] service_name Organisation table name.
   * @param[in] service_id Organisation row id.
   * @return SELECT of the pharmacy's owner/admin/staff.
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
   * @brief Builds the permission query that resolves the owning pharmacy from
   * an appointment id.
   * @param[in] appointment_id Appointment row id.
   * @return SELECT of the owning pharmacy's owner/admin/staff.
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
