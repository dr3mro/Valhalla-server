#pragma once

#include <jsoncons/json.hpp>

#include "entities/appointments/base/appointment.hpp"

using json = jsoncons::json;

class RadiologyCenterAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "radiologycenters_appointments";

   public:
    template <typename T>
    RadiologyCenterAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    virtual ~RadiologyCenterAppointment() override = default;
};
