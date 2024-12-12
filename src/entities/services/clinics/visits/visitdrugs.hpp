#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class VisitDrugs : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_patients_visitdrugs";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // VisitDrugs() : Case(TABLENAME) {}

    template <typename T>
    VisitDrugs(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~VisitDrugs() override = default;
};
