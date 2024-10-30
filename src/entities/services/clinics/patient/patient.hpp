#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Patient : public Entity
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    struct PatientData
    {
        uint64_t patient_id;
        PatientData(const jsoncons::json& criteria) { patient_id = criteria.at("id").as<uint64_t>(); }
    };

    Patient() : Entity(TABLENAME) {}
    template <typename T>
    Patient(const T& _data) : Entity(_data, TABLENAME)
    {
    }
    std::string getTableName() { return TABLENAME; }
    ~Patient() override = default;

    std::string getSqlGetVisitsStatement(const uint64_t patient_id) { return fmt::format("SELECT * FROM visits WHERE id = {} ;", patient_id); }
};
