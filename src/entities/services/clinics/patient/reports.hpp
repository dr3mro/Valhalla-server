#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Reports : public Case
{
   private:
    static constexpr auto TABLENAME = "patients_reports";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // Reports() : Case(TABLENAME) {}

    template <typename T>
    Reports(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~Reports() override = default;
};
