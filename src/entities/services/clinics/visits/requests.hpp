#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Requests : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_requests";

   public:
    // Requests() : Case(TABLENAME) {}

    template <typename T>
    Requests(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~Requests() override = default;
};
