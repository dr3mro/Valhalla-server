#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class RadiologyCenterAppointment : public Entity
{
   private:
    static constexpr auto TABLENAME = "radiologycenters_appointments";

   public:
    RadiologyCenterAppointment() : Entity(TABLENAME) {}

    template <typename T>
    RadiologyCenterAppointment(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~RadiologyCenterAppointment() override = default;
};
