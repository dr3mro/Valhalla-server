#pragma once

#include "entities/appointments/base/appointment.hpp"

class PharmacyAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "pharmacies_appointments";

   public:
    template <typename T>
    PharmacyAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    virtual ~PharmacyAppointment() override = default;
};
