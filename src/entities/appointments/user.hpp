#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class UserAppointment : public Entity
{
   private:
    static constexpr auto TABLENAME = "clinics_appointments";

   public:
    UserAppointment() : Entity(TABLENAME) {}

    template <typename T>
    UserAppointment(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~UserAppointment() override = default;
};
