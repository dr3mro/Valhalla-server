#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"
#include "utils/message/message.hpp"

using json = jsoncons::json;

class Service : public Entity
{
   public:
    template <typename T>
    Service(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    // Service(const std::string &tablename) : Entity(tablename) {};
    virtual ~Service() override = default;
    std::optional<std::string> getSqlAddStaffStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto idata = std::get<Types::StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set(mjson, '{{payload,providers,Doctors}}', ((mjson->'payload'->'providers'->'Doctors') || "
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
            auto idata = std::get<Types::StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set("
                     "mjson, '{{payload,providers,Doctors}}', "
                     "(SELECT jsonb_agg(elem) FROM jsonb_array_elements(mjson->'payload'->'providers'->'Doctors') AS elem "
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

   private:
};
