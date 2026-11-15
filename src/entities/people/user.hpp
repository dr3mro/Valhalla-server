#pragma once
#include <fmt/core.h>

#include <exception>
#include <optional>
#include <string>

#include "entities/base/client.hpp"
#include "entities/base/types.hpp"
#include "utils/message/message.hpp"

using Data_t = api::v2::Types::Data_t;
using Client = api::v2::Client;

class User : public Client
{
   public:
    User(const User&)            = delete;
    User(User&&)                 = delete;
    User& operator=(const User&) = delete;
    User& operator=(User&&)      = delete;
    template <typename ClientData_t>
    explicit User(const ClientData_t& data) : Client(data, TABLENAME)
    {
    }
    // User() : Client(std::string(TABLENAME)) {}
    ~User() override = default;

    static constexpr auto      getTableName() { return TABLENAME; }
    std::optional<std::string> getSqlGetServicesStatement(pqxx::params& params)
    {
        try
        {
            client_id = std::get<Data_t>(getData()).get_id();
            params    = {};
            params.append(client_id.value());
            return std::string("SELECT id FROM users WHERE id = $1");
        }
        catch (const std::exception& e)
        {
            Message::ErrorMessage(fmt::format("Failed to create query for GetServicesStatement {}.", tablename));
            Message::CriticalMessage(e.what());
            return std::nullopt;
        }
    }

   private:
    static constexpr auto TABLENAME = "users";
};
