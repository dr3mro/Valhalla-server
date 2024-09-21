#pragma once

#include <jsoncons/json.hpp>

#include "entities/services/appointments/base/appointment.hpp"

using json = jsoncons::json;

class RadiologyCenterAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "radiologycenters_appointments";

   public:
    RadiologyCenterAppointment() : Appointment(TABLENAME) {}

    template <typename T>
    RadiologyCenterAppointment(const T& _data) : Appointment(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~RadiologyCenterAppointment() override = default;
};
