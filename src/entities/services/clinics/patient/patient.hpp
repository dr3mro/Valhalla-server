#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

using json = jsoncons::json;

class Patient : public Case
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    using Data_t = struct Data_t
    {
        uint64_t id;
        Data_t(const uint64_t _id) : id(_id) {}
    };

    Patient() : Case(TABLENAME) {}
    virtual ~Patient() override = default;

    template <typename T>
    Patient(const T& _data) : Case(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }
    std::string getSqlGetVisitsStatement()
    {
        return fmt::format("SELECT visits FROM clinics_visits WHERE patient_id = {} ;", std::any_cast<Data_t>(getData()).id);
    }
};
