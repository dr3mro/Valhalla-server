#pragma once
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <optional>

#include "gatekeeper/types.hpp"
#include "store/store.hpp"

namespace api
{
    namespace v2
    {
        class KeeprBase
        {
           public:
            KeeprBase()          = default;
            virtual ~KeeprBase() = default;

           protected:
            bool        setNowLoginTimeGetLastLogoutTime(std::optional<Types::ClientLoginData>& clientLoginData);
            bool        getLastLogoutTimeIfActive(std::optional<Types::ClientLoginData>& clientLoginData);
            std::string current_time_to_utc_string();
            void        setNowLogoutTime(uint64_t id, const std::string& group);
            std::optional<std::string> getLastLoginTime(uint64_t id, const std::string& group);
            bool decodeToken(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message,
                             jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoedToken);
            bool validateToken(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message,
                               jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken);
            jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> createTokenVerifier(
                const std::optional<Types::ClientLoginData>& clientLoginData);

            std::shared_ptr<DatabaseController>         databaseController = Store::getObject<DatabaseController>();
            const std::shared_ptr<Configurator>         configurator_      = Store::getObject<Configurator>();
            const Configurator::TokenManagerParameters& tokenManagerParameters_ =
                configurator_->get<Configurator::TokenManagerParameters>();
        };
    }  // namespace v2
}  // namespace api