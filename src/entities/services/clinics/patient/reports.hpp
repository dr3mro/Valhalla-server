#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Reports : public Case
{
   private:
    static constexpr auto TABLENAME = "patients_reports";

   public:
    Reports() : Case(TABLENAME) {}

    template <typename T>
    Reports(const T &_data) : Case(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    virtual ~Reports() override = default;
};
