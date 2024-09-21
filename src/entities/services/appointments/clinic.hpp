#pragma once

#include <jsoncons/json.hpp>

#include "entities/services/appointments/base/appointment.hpp"

using json = jsoncons::json;

class ClinicAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    ClinicAppointment() : Appointment(TABLENAME) {}

    template <typename T>
    ClinicAppointment(const T& _data) : Appointment(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~ClinicAppointment() override = default;
};
