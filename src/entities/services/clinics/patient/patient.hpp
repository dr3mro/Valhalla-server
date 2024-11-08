#pragma once

#include <jsoncons/json.hpp>

#include "entities/appointments/base/appointment.hpp"
#include "entities/base/case.hpp"

using json = jsoncons::json;

class Patient : public Case
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    // Patient() : Case(TABLENAME) {}
    virtual ~Patient() override = default;

    template <typename T>
    Patient(const T& _data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    std::string           getSqlGetVisitsStatement()
    {
        return fmt::format("SELECT visits FROM clinics_visits WHERE patient_id = {} ;", std::get<Types::Data_t>(getData()).id);
    }
};
