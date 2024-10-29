#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Prescriptions : public Entity
{
   private:
    static constexpr auto TABLENAME = "prescriptions";

   public:
    Prescriptions() : Entity(TABLENAME) {}

    template <typename T>
    Prescriptions(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Prescriptions() override = default;
};
