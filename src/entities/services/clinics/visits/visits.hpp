#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Visits : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_visits";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // Visits() : Case(TABLENAME) {}

    template <typename T>
    Visits(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~Visits() override = default;
};
