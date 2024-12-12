#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class PatientDrugs : public Case
{
   private:
    static constexpr auto TABLENAME = "patients_drugs";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // PatientDrugs() : Case(TABLENAME) {}

    template <typename T>
    PatientDrugs(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~PatientDrugs() override = default;
};
