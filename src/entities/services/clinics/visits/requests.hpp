#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Requests : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_requests";
    static constexpr auto ORGNAME   = "clinics";

   public:
    // Requests() : Case(TABLENAME) {}

    template <typename T>
    Requests(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getOrgName() { return ORGNAME; }

    virtual ~Requests() override = default;
};
