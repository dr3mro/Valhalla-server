#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Reports : public Entity
{
   private:
    static constexpr auto TABLENAME = "patients_reports";

   public:
    Reports() : Entity(TABLENAME) {}

    template <typename T>
    Reports(const T &_data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Reports() override = default;
};
