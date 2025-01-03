#pragma once

#include <fmt/core.h>

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>

#include "entities/base/case.hpp"
#include "entities/base/types.hpp"

using Data_t = api::v2::Types::Data_t;

class Patient : public Case
{
   private:
    static constexpr auto TABLENAME  = "patients";
    static constexpr auto CREATE_KEY = "clinic_id";

   public:
    Patient(const Patient&)            = delete;
    Patient(Patient&&)                 = delete;
    Patient& operator=(const Patient&) = delete;
    Patient& operator=(Patient&&)      = delete;

    template <typename T>
    explicit Patient(const T& _data) : Case(_data, TABLENAME)
    {
    }
    ~Patient() override = default;

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getCreateKey() { return CREATE_KEY; }

    std::string getSqlGetVisitsStatement()
    {
        return fmt::format("SELECT * FROM clinics_visits WHERE patient_id = {} ;", std::get<Data_t>(getData()).get_id().value());
    }

    static std::optional<std::string> getPermissionsQueryForCreate(const std::optional<jsoncons::json>& data_j)
    {
        uint64_t id;
        try
        {
            id = data_j->at(CREATE_KEY).as<uint64_t>();
        }
        catch (const std::exception& e)
        {
            CRITICALMESSAGE
            return std::nullopt;
        }

        return getPermissionsQueryForCreatePatientImpl(id);
    }

    static std::optional<std::string> getPermissionsQueryForRead(const uint64_t id) { return getPermissionsQueryForReadImp(id, TABLENAME); }

    static std::optional<std::string> getPermissionsQueryForUpdate(const uint64_t id) { return getPermissionsQueryForUpdateImpl(id, TABLENAME); }

    static std::optional<std::string> getPermissionsQueryForDelete(const uint64_t id) { return getPermissionsQueryForDeleteImp(id, TABLENAME); }
};
