#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Health : public Case
{
   private:
    static constexpr auto TABLENAME = "patients_health";

   public:
    // Health() : Case(TABLENAME) {}

    template <typename T>
    Health(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~Health() override = default;
};
