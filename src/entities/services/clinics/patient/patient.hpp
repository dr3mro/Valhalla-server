#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Patient : public Case
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    template <typename T>
    Patient(const T& _data) : Case(_data, TABLENAME)
    {
    }
    virtual ~Patient() override = default;

    static constexpr auto getTableName() { return TABLENAME; }
    std::string           getSqlGetVisitsStatement()
    {
        return fmt::format("SELECT visits FROM clinics_visits WHERE patient_id = {} ;", std::get<Types::Data_t>(getData()).get_id().value());
    }
};
