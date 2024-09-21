#pragma once

#include <jsoncons/json.hpp>

#include "entities/appointments/base/appointment.hpp"

using json = jsoncons::json;

class LaboratoryAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "laboratories_appointments";

   public:
    LaboratoryAppointment() : Appointment(TABLENAME) {}

    template <typename T>
    LaboratoryAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~LaboratoryAppointment() override = default;
};
