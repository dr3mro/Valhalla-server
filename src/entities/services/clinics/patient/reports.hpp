#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/case.hpp"

class Reports : public Case
{
   private:
    static constexpr auto TABLENAME       = "patients_reports";
    static constexpr auto KEYREFTABLENAME = "patients";
    static constexpr auto CREATE_KEY      = "id";

   public:
    // Reports() : Case(TABLENAME) {}

    template <typename T>
    Reports(const T& _data) : Case(_data, TABLENAME)
    {
    }

    static constexpr auto getTableName() { return TABLENAME; }
    static constexpr auto getCreateKey() { return CREATE_KEY; }

    virtual ~Reports() override = default;

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
