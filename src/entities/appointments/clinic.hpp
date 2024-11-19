#pragma once

#include "entities/appointments/base/appointment.hpp"
template <typename T>
class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    ClinicAppointment(const T &_data) : Appointment(_data, TABLENAME) {}
    static constexpr auto getTableName() { return TABLENAME; }
    virtual ~ClinicAppointment() override = default;
};
