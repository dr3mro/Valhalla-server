#pragma once
#include "entities/base/client.hpp"
#include "utils/message/message.hpp"

class User : public Client
{
   public:
    User(const Types::ClientData &user_data) : Client(user_data, TABLENAME) {}
    User(const Types::Credentials &credentials) : Client(credentials, TABLENAME) {}
    template <typename T>
    User(const T &data) : Client(data, TABLENAME)
    {
    }
    // User() : Client(std::string(TABLENAME)) {}
    virtual ~User() = default;
    static constexpr auto      getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement()
    {
        std::optional<std::string> query;
        try
        {
            uint64_t user_id = std::get<Types::Data_t>(getData()).id;
            query            = fmt::format("SELECT id from users WHERE id = {}", user_id);
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
