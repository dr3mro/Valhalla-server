#pragma once

#include "gatekeeper/keeprbase/keeprbase.hpp"
namespace api
{
    namespace v2
    {
        class TokenManager : public KeeprBase
        {
           public:
            TokenManager()          = default;
            virtual ~TokenManager() = default;
            bool generateToken(std::optional<Types::ClientLoginData>& clientLoginData);
            bool isTokenValid(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
        };
    }  // namespace v2
}  // namespace api