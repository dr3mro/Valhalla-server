#pragma once
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/kazuho-picojson/traits.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <memory>
#include <optional>
#include <string>

#include "configurator/configurator.hpp"
#include "controllers/databasecontroller/databasecontroller.hpp"
#include "gatekeeper/types.hpp"
#include "store/store.hpp"

namespace api::v2
{
    class KeeprBase
    {
       public:
        KeeprBase() : configurator_(Store::getObject<Configurator>()), tokenManagerParameters_(configurator_->get<Configurator::TokenManagerParameters>()) {}
        KeeprBase(const KeeprBase& other)                = default;
        KeeprBase& operator=(const KeeprBase& other)     = delete;
        KeeprBase(KeeprBase&& other) noexcept            = default;
        KeeprBase& operator=(KeeprBase&& other) noexcept = delete;
        virtual ~KeeprBase()                             = default;

       protected:
        bool                          setNowLoginTimeGetLastLogoutTime(std::optional<Types::ClientLoginData>& clientLoginData);
        bool                          getLastLogoutTimeIfActive(std::optional<Types::ClientLoginData>& clientLoginData);
        static std::string            current_time_to_utc_string();
        void                          setNowLogoutTime(uint64_t _id, const std::string& _group);
        std::optional<std::string>    getLastLoginTime(uint64_t _id, const std::string& _group);
        std::optional<jsoncons::json> getPasswordHashForUserName(const std::string& username, const std::string& _group);
        static bool                   decodeToken(
                              std::optional<Types::ClientLoginData>& clientLoginData, std::string& message, jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decoedToken);
        bool validateToken(
            std::optional<Types::ClientLoginData>& clientLoginData, std::string& message, jwt::decoded_jwt<jwt::traits::kazuho_picojson>& decodedToken);
        [[nodiscard]] jwt::verifier<jwt::default_clock, jwt::traits::kazuho_picojson> createTokenVerifier(
            const std::optional<Types::ClientLoginData>& clientLoginData) const;
        [[nodiscard]] Configurator::TokenManagerParameters getTokenManagerParameters() const { return tokenManagerParameters_; }

       private:
        std::shared_ptr<DatabaseController>  databaseController = Store::getObject<DatabaseController>();
        std::shared_ptr<Configurator>        configurator_;
        Configurator::TokenManagerParameters tokenManagerParameters_;
    };
}  // namespace api::v2
