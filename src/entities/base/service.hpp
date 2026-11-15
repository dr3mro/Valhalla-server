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
    Service(const Service&)            = delete;
    Service(Service&&)                 = delete;
    Service& operator=(const Service&) = delete;
    Service& operator=(Service&&)      = delete;

    template <typename T>
    Service(const T& data, const std::string& tablename) : Entity(data, tablename)
    {
    }
    // Service(const std::string &tablename) : Entity(tablename) {};
    ~Service() override = default;
    // The nominee id (as JSON text) and the entity id are bound.
    std::optional<std::string> getSqlAddStaffStatement(pqxx::params& params)
    {
        try
        {
            auto   idata = std::get<StaffData>(getData());
            Binder binder(params);
            // Bound in a fixed order (argument evaluation order is unspecified).
            const std::string nominee_ph = binder.bind(std::to_string(idata.nominee_id));
            const std::string entity_ph  = binder.bind(idata.entity_id);
            return fmt::format(
                "UPDATE {} SET mjson = jsonb_set(mjson, '{{payload,providers,Doctors}}', "
                "((mjson->'payload'->'providers'->'Doctors') || to_jsonb({}::text))) WHERE id={} RETURNING id;",
                tablename, nominee_ph, entity_ph);
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage("Error creating query for add member.");
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

    std::optional<std::string> getSqlRemoveStaffStatement(pqxx::params& params)
    {
        try
        {
            auto   idata = std::get<StaffData>(getData());
            Binder binder(params);
            // Bound in a fixed order (argument evaluation order is unspecified).
            const std::string nominee_ph = binder.bind(std::to_string(idata.nominee_id));
            const std::string entity_ph  = binder.bind(idata.entity_id);
            return fmt::format(
                "UPDATE {} SET mjson = jsonb_set(mjson, '{{payload,providers,Doctors}}', "
                "(SELECT jsonb_agg(elem) FROM jsonb_array_elements(mjson->'payload'->'providers'->'Doctors') AS elem "
                "WHERE elem <> to_jsonb({}::text))) WHERE id={} RETURNING id;",
                tablename, nominee_ph, entity_ph);
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage("Error creating query for remove member.");
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }
    static std::optional<std::string> getServicePermissionsQuery(const std::string& service_name, std::uint64_t service_id)
    {
        return fmt::format("SELECT owner_id,admin_id,staff FROM {} WHERE id = '{}' LIMIT 1;", service_name, service_id);
    }

   private:
};
