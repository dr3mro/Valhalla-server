#pragma once
#include <cstdint>

#include "entities/base/client.hpp"
#include "utils/message/message.hpp"

class Provider : public Client
{
   public:
    template <typename ClientData_t>
    Provider(const ClientData_t &data) : Client(data, TABLENAME)
    {
    }
    // Provider() : Client(std::string(TABLENAME)) {}
    virtual ~Provider() = default;
    static constexpr auto      getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto provider_id = std::get<Types::Data_t>(getData()).get_id();

            if (!provider_id.has_value())
            {
                Message::ErrorMessage(
                    fmt::format("Failed to get services for provider with id {}.", provider_id.value()));
                return std::nullopt;
            }

            uint64_t _id = provider_id.value();

            query = fmt::format(
                R"(
                        WITH vars AS (
                            SELECT {} AS id  -- Convert integer to JSONB
                        )
                        SELECT combined.id, combined.name, combined.type  -- Qualify "id" with "combined"
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
                        JOIN vars ON jsonb_path_exists(combined.staff, '$.** ? (@ == "{}")'::jsonpath)
                                OR combined.admin_id = vars.id
                                OR combined.owner_id = vars.id;
                        )",
                _id, _id);
        }
        catch (const std::exception &e)
        {
            Message::ErrorMessage(fmt::format("Failed to create query for GetServicesStatement {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }

        return query;
    }

   private:
    static constexpr auto TABLENAME = "providers";
};
