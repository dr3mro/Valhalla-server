#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Patient : public Entity
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    Patient() : Entity(TABLENAME) {}
    template <typename T>
    Patient(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~Patient() override = default;
};
