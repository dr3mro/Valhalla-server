/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file user.hpp
 * @brief Appointment entity defined in this header, bound to the clinics
 * appointment table.
 * @details This is a minimal concrete specialisation of Appointment. It fixes
 * only the table name
 * ("clinics_appointments") and provides a constructor, a table-name accessor
 * and a destructor. It does not define an organisation name or the static
 * permission-query helpers that the other appointment specialisations expose.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It gives callers a ready-made Appointment for the clinics appointment table
 * when only the table name is needed. Note that the class is named
 * ClinicAppointment, the same name used in entities/appointments/clinic.hpp, so
 * including both headers together would conflict. It is kept as a lightweight
 * alternative for code paths that do not need permission helpers.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - std::optional: https://en.cppreference.com/w/cpp/utility/optional
 * - Static constexpr member: a compile-time constant stored on the class,
 * usable without an object.
 * - One Definition Rule: two different classes with the same name in the same
 * namespace will clash if both headers are included in one translation unit;
 * that is the caveat noted for this file.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per source file.

// jsoncons::json, kept available for payloads passed to the base constructor.
#include <jsoncons/json.hpp>

// Appointment, the base class providing the CRUD statement builders.
#include "entities/appointments/base/appointment.hpp"

/**
 * @class ClinicAppointment
 * @brief Appointment entity defined in this header, bound to the clinics
 * appointment table.
 * @details A minimal Appointment specialisation fixing only TABLENAME and
 * exposing getTableName(); it does not define an organisation name or the
 * static permission-query helpers found in the other appointment
 * specialisations.
 * @note The class name collides with ClinicAppointment in
 * entities/appointments/clinic.hpp.
 * @see Appointment
 */
class ClinicAppointment : public Appointment  // Public inheritance keeps the
                                              // base CRUD interface available.
{
 private:
  /**
   * @brief Database table holding clinic appointments.
   */
  static constexpr auto TABLENAME =
      "clinics_appointments";  // The exact table this class reads and writes.

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
                    TABLENAME)  // "explicit" requires a direct call; TABLENAME
                                // fixes the target table.
  {
  }

  /**
   * @brief Non-copyable and non-movable; construct in place for the request
   * that uses it.
   */
  ClinicAppointment(const ClinicAppointment&) = delete;  // No copy.
  ClinicAppointment(ClinicAppointment&&)      = delete;  // No move.
  ClinicAppointment& operator=(const ClinicAppointment&) =
      delete;  // No copy assignment.
  ClinicAppointment& operator=(ClinicAppointment&&) =
      delete;  // No move assignment.

  /**
   * @brief Returns the clinic appointment table name.
   */
  static constexpr auto getTableName()
  {
    return TABLENAME;
  }  // Static accessor for the table name.
  /**
   * @brief Default destructor; no state beyond the base Appointment.
   */
  ~ClinicAppointment() override = default;  // Nothing extra to clean up.
};
