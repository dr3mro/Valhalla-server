#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class VisitDrugs : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_patients_visitdrugs";

   public:
    // VisitDrugs() : Case(TABLENAME) {}

    template <typename T>
    VisitDrugs(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~VisitDrugs() override = default;
};
