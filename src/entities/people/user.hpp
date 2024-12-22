#pragma once
#include "entities/base/client.hpp"
#include "utils/message/message.hpp"

class User : public Client
{
   public:
    template <typename ClientData_t>
    User(const ClientData_t &data) : Client(data, TABLENAME)
    {
    }
    // User() : Client(std::string(TABLENAME)) {}
    ~User() override = default;
    static constexpr auto      getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement()
    {
        std::optional<std::string> query;
        try
        {
            client_id = std::get<Types::Data_t>(getData()).get_id();
            query     = fmt::format("SELECT id from users WHERE id = {}", client_id.value());
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
    static constexpr auto TABLENAME = "users";
};
