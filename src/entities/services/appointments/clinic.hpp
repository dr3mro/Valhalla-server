#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class ClinicAppointment : public Entity
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    ClinicAppointment() : Entity(TABLENAME) {}

    template <typename T>
    ClinicAppointment(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~ClinicAppointment() override = default;
};
