#pragma once
#include <optional>

#include "entities/appointments/base/appointment.hpp"
#include "entities/base/case.hpp"
#include "entities/base/client.hpp"
#include "entities/base/service.hpp"

template <typename T>
concept Client_t = std::is_base_of_v<Client, T>;

template <typename T>
concept Service_t = std::is_base_of_v<Service, T>;

template <typename T>
concept Case_t = std::is_base_of_v<Case, T>;

template <typename T>
concept Appointment_t = std::is_base_of_v<Appointment, T>;

namespace api
{
    namespace v2
    {
        namespace Types
        {
            using ClientLoginData = struct ClientLoginData
            {
                std::optional<std::string>          token;
                std::optional<std::string>          username;
                std::optional<std::string>          group;
                std::optional<uint64_t>             clientId;
                std::optional<std::string>          lastLogoutTime;
                std::optional<std::string>          nowLoginTime;
                bool                                is_active;
                bool                                passwordMatch;
                std::optional<std::string>          passwordHash;
                std::chrono::system_clock::duration expireTime;
                std::optional<std::string>          ip_address;
            };

            using Credentials = struct Credentials
            {
                std::string username;  ///< The user's username.
                std::string password;  ///< The user's password.
            };
        }  // namespace Types
    }  // namespace v2
}  // namespace api
