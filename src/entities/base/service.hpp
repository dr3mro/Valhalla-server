#pragma once

#include <fmt/core.h>

#include <cstdint>
#include <exception>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>

#include "entities/base/entity.hpp"
#include "entities/base/types.hpp"
#include "utils/message/message.hpp"

using StaffData = api::v2::Types::StaffData;

class Service : public Entity
{
   public:
    Service(const Service &)            = delete;
    Service(Service &&)                 = delete;
    Service &operator=(const Service &) = delete;
    Service &operator=(Service &&)      = delete;

    template <typename T>
    Service(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    // Service(const std::string &tablename) : Entity(tablename) {};
    ~Service() override = default;
    std::optional<std::string> getSqlAddStaffStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto idata = std::get<StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set(mjson, "
                     "'{{payload,providers,Doctors}}', "
                     "((mjson->'payload'->'providers'->'Doctors') || "
                     "'\"{}\"')::jsonb) WHERE ID={} RETURNING id;",
                tablename, idata.nominee_id, idata.nominee_group, idata.entity_id);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage("Error creating query for add member.");
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlRemoveStaffStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto idata = std::get<StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set("
                     "mjson, '{{payload,providers,Doctors}}', "
                     "(SELECT jsonb_agg(elem) FROM "
                     "jsonb_array_elements(mjson->'payload'->'providers'->'Doctors')"
                     " AS elem "
                     "WHERE elem <> '\"{}\"') "
                     ") WHERE ID={} RETURNING id;",
                tablename, idata.nominee_id, idata.entity_id);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage("Error creating query for remove member.");
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
        return query;
    }
    static std::optional<std::string> getServicePermissionsQuery(const std::string &service_name, std::uint64_t service_id)
    {
        return fmt::format("SELECT owner_id,admin_id,staff FROM {} WHERE id = '{}' LIMIT 1;", service_name, service_id);
    }

   private:
};
