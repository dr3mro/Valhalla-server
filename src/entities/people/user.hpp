#pragma once
#include "entities/base/client.hpp"

class User : public Client
{
   public:
    User(const ClientData &user_data) : Client(user_data, TABLENAME) {}
    User(const Credentials &credentials) : Client(credentials, TABLENAME) {}
    template <typename T>
    User(const T &data) : Client(data, TABLENAME)
    {
    }
    User() : Client(std::string(TABLENAME)) {}
    virtual ~User() = default;
    std::string                getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement()
    {
        std::optional<std::string> query;
        try
        {
            uint64_t user_id = std::any_cast<uint64_t>(getData());
            query            = fmt::format("SELECT id from users WHERE id = {}", user_id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for GetServicesStatement " << tablename << e.what() << '\n';
            return std::nullopt;
        }

        return query;
    }

   private:
    static constexpr auto TABLENAME = "users";
};
