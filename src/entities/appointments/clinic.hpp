#pragma once

#include "entities/appointments/base/appointment.hpp"

class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    template <typename T>
    ClinicAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    virtual ~ClinicAppointment() override = default;
};
