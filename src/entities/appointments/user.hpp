#pragma once

#include <jsoncons/json.hpp>

#include "entities/appointments/base/appointment.hpp"

class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    template <typename T>
    explicit ClinicAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }

    ClinicAppointment(const ClinicAppointment &)            = delete;
    ClinicAppointment(ClinicAppointment &&)                 = delete;
    ClinicAppointment &operator=(const ClinicAppointment &) = delete;
    ClinicAppointment &operator=(ClinicAppointment &&)      = delete;

    static constexpr auto getTableName() { return TABLENAME; }
    ~ClinicAppointment() override = default;
};
