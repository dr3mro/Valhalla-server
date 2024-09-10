#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class PharmacyAppointment : public Entity
{
   private:
    static constexpr auto TABLENAME = "pharmacies_appointments";

   public:
    PharmacyAppointment() : Entity(TABLENAME) {}

    template <typename T>
    PharmacyAppointment(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~PharmacyAppointment() override = default;
};
