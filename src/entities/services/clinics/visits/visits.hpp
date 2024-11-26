#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Visits : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_visits";

   public:
    // Visits() : Case(TABLENAME) {}

    template <typename T>
    Visits(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~Visits() override = default;
};
