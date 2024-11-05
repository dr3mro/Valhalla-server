#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Prescriptions : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_prescriptions";

   public:
    Prescriptions() : Case(TABLENAME) {}

    template <typename T>
    Prescriptions(const T &_data) : Case(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~Prescriptions() override = default;
};
