#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class PatientDrugs : public Case
{
   private:
    static constexpr auto TABLENAME = "patients_drugs";

   public:
    PatientDrugs() : Case(TABLENAME) {}

    template <typename T>
    PatientDrugs(const T &_data) : Case(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }

    ~PatientDrugs() override = default;
};
