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
    ~User() = default;

   private:
    static constexpr auto TABLENAME = "users";
};
