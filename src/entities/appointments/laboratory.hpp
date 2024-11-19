#pragma once

#include "entities/appointments/base/appointment.hpp"

class LaboratoryAppointment : public Appointment
{
   private:
    static constexpr auto TABLENAME = "laboratories_appointments";

   public:
    template <typename T>
    LaboratoryAppointment(const T &_data) : Appointment(_data, TABLENAME)
    {
    }
    static constexpr auto getTableName() { return TABLENAME; }
    virtual ~LaboratoryAppointment() override = default;
};
