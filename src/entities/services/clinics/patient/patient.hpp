#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Patient : public Entity
{
   private:
    static constexpr auto TABLENAME = "patients";

   public:
    struct Data
    {
        uint64_t id;
        Data(const jsoncons::json& criteria) { id = criteria.at("id").as<uint64_t>(); }
    };

    Patient() : Entity(TABLENAME) {}
    ~Patient() override = default;

    template <typename T>
    Patient(const T& _data) : Entity(_data, TABLENAME)
    {
    }

    std::string getTableName() { return TABLENAME; }
    std::string getSqlGetVisitsStatement(const uint64_t id) { return fmt::format("SELECT * FROM visits WHERE id = {} ;", id); }
};
