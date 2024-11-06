#pragma once

#include <jsoncons/json.hpp>

#include "entities/appointments/base/appointment.hpp"

using json = jsoncons::json;

class PharmacyAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "pharmacies_appointments";

   public:
    PharmacyAppointment() : Appointment(TABLENAME) {}

    template <typename T>
    PharmacyAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    virtual ~PharmacyAppointment() override = default;
};
