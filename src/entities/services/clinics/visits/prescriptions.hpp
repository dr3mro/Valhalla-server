#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Prescriptions : public Case
{
   private:
    static constexpr auto TABLENAME = "clinics_prescriptions";

   public:
    // Prescriptions() : Case(TABLENAME) {}

    template <typename T>
    Prescriptions(const T &_data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }

    virtual ~Prescriptions() override = default;
};
