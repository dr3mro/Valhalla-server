#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace api
{
    namespace v2
    {
        class TokenManager
        {
           public:
            TokenManager()          = default;
            virtual ~TokenManager() = default;
            std::optional<std::string> generateToken(std::string_view data);
            bool                       isTokenValid(std::string_view token);
        };
    }  // namespace v2
}  // namespace api