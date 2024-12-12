#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class PaidServices : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_paidservices";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // PaidServices() : Case(TABLENAME) {}

    template <typename T>
    PaidServices(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~PaidServices() override = default;
};
