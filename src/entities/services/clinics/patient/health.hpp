#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Health : public Entity
{
   private:
    static constexpr auto TABLENAME = "patients_health";

   public:
    Health() : Entity(TABLENAME) {}

    template <typename T>
    Health(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Health() override = default;
};
