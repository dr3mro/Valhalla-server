#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class PatientDrugs : public Entity
{
   private:
    static constexpr auto TABLENAME = "visitdrugs";

   public:
    PatientDrugs() : Entity(TABLENAME) {}

    template <typename T>
    PatientDrugs(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~PatientDrugs() override = default;
};
