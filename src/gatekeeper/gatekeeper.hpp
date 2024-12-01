#pragma once
#include <functional>
#include <jsoncons/json.hpp>
#include <string_view>

#include "entities/base/types.hpp"
#include "gatekeeper/dosdetector/dosdetector.hpp"
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
#include "gatekeeper/tokenmanager/tokenmanager.hpp"
#include "types.hpp"
#include "utils/global/callback.hpp"

namespace api
{
    namespace v2
    {
        class GateKeeper
        {
           public:
            GateKeeper()          = default;
            virtual ~GateKeeper() = default;
            void login(CALLBACK_&& callback, std::string_view data, const std::string& ip_address, std::string_view group);
            void logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& group);

            bool isAuthenticationValid(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            void removeSession(std::optional<uint64_t> client_id, const std::string& group);
            // bool hasPermission(CALLBACK_&& callback, std::string_view data);  // TODO: implement
            DOSDetector::Status isDosAttack(const DOSDetector::Request& request);

           private:
            std::optional<jsoncons::json>     parse_data(std::string_view data, std::string& message, bool& success);
            std::optional<Types::Credentials> parse_credentials(std::string_view data, std::string& message, bool& success);
            std::shared_ptr<SessionManager>   sessionManager_ = Store::getObject<SessionManager>();
            std::shared_ptr<TokenManager>     tokenManager_   = Store::getObject<TokenManager>();
            std::shared_ptr<DOSDetector>      dosDetector_    = Store::getObject<DOSDetector>();
        };
    }  // namespace v2
}  // namespace api
