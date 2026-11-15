#pragma once
#include <fmt/core.h>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>

#include "entities/base/client.hpp"
#include "entities/base/types.hpp"
#include "utils/message/message.hpp"

using Data_t = api::v2::Types::Data_t;
using Client = api::v2::Client;

class Provider : public Client
{
   public:
    Provider(const Provider&)            = delete;
    Provider(Provider&&)                 = delete;
    Provider& operator=(const Provider&) = delete;
    Provider& operator=(Provider&&)      = delete;

    template <typename ClientData_t>
    explicit Provider(const ClientData_t& data) : Client(data, TABLENAME)
    {
    }
    // Provider() : Client(std::string(TABLENAME)) {}
    ~Provider() override = default;
    static constexpr auto      getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement(pqxx::params& params)
    {
        try
        {
            client_id = std::get<Data_t>(getData()).get_id();
            if (!client_id.has_value())
            {
                Message::ErrorMessage("Failed to get services for provider: no id.");
                return std::nullopt;
            }
            params = {};
            params.append(client_id.value());                    // $1: numeric id
            params.append(std::to_string(client_id.value()));    // $2: the same id as text, compared with the JSON staff entries
            return std::string(
                R"(
                        WITH vars AS (
                            SELECT $1::bigint AS id
                        )
                        SELECT combined.id, combined.name, combined.type
                        FROM (
                            SELECT id, name, staff, admin_id, owner_id, 'Clinic' AS type
                            FROM clinics
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'Pharmacy' AS type
                            FROM pharmacies
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'RadiologyCenter' AS type
                            FROM radiologycenters
                            UNION ALL
                            SELECT id, name, staff, admin_id, owner_id, 'Laboratory' AS type
                            FROM laboratories
                        ) AS combined
                        JOIN vars ON jsonb_path_exists(combined.staff, '$.** ? (@ == $v)'::jsonpath, jsonb_build_object('v', $2::text))
                                OR combined.admin_id = vars.id
                                OR combined.owner_id = vars.id;
                        )");
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create query for GetServicesStatement {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

   private:
    static constexpr auto TABLENAME = "providers";
};
