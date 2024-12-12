#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Prescriptions : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_prescriptions";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // Prescriptions() : Case(TABLENAME) {}

    template <typename T>
    Prescriptions(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~Prescriptions() override = default;
};
