#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class VisitDrugs : public Case
{
   private:
    static constexpr auto TABLENAME       = "patients_visitdrugs";
    static constexpr auto KEYREFTABLENAME = "clinics_visits";
    static constexpr auto CREATE_KEY      = "id";

   public:
    // VisitDrugs() : Case(TABLENAME) {}

    template <typename T>
    VisitDrugs(const T& _data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getCreateKey() { return CREATE_KEY; }

    ~VisitDrugs() override = default;

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

        return getPermissionsQueryForCreateImpl(id, KEYREFTABLENAME);
    }
    static std::optional<std::string> getPermissionsQueryForRead(const uint64_t id) { return getPermissionsQueryForReadImp(id, TABLENAME); }

    static std::optional<std::string> getPermissionsQueryForUpdate(const uint64_t id) { return getPermissionsQueryForUpdateImpl(id, TABLENAME); }

    static std::optional<std::string> getPermissionsQueryForDelete(const uint64_t id) { return getPermissionsQueryForDeleteImp(id, TABLENAME); }
};
