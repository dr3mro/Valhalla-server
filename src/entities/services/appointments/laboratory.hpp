#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class LaboratoryAppointment : public Entity
{
   private:
    static constexpr auto TABLENAME = "laboratories_appointments";

   public:
    LaboratoryAppointment() : Entity(TABLENAME) {}

    template <typename T>
    LaboratoryAppointment(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~LaboratoryAppointment() override = default;
};
