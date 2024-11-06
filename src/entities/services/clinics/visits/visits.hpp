#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Visits : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_visits";

   public:
    Visits() : Case(TABLENAME) {}

    template <typename T>
    Visits(const T &_data) : Case(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    virtual ~Visits() override = default;
};
