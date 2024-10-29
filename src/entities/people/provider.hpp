#pragma once
#include "entities/base/client.hpp"

class Provider : public Client
{
   public:
    Provider(const ClientData &user_data) : Client(user_data, TABLENAME) {}
    Provider(const Credentials &credentials) : Client(credentials, TABLENAME) {}
    template <typename T>
    Provider(const T &data) : Client(data, TABLENAME)
    {
    }
    ~Provider() = default;

    std::optional<std::string> getSqlGetServicesStatement()
    {
        std::optional<std::string> query;
        try
        {
            uint64_t provider_id = std::any_cast<uint64_t>(getData());
            query                = fmt::format(
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
                        JOIN vars ON jsonb_path_exists(combined.staff, '$.** ? (@ == "1000")'::jsonpath)
                                OR combined.admin_id = vars.id
                                OR combined.owner_id = vars.id;
                        )",
                provider_id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "failed to create query for GetServicesStatement " << tablename << e.what() << '\n';
            return std::nullopt;
        }

        return query;
    }

   private:
    static constexpr auto TABLENAME = "providers";
};
