#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class PaidServices : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_paidservices";

   public:
    // PaidServices() : Case(TABLENAME) {}

    template <typename T>
    PaidServices(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~PaidServices() override = default;
};
