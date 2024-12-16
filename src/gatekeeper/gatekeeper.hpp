#pragma once
#include <drogon/DrTemplateBase.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <string_view>

#include "gatekeeper/dosdetector/dosdetector.hpp"
#include "gatekeeper/permissionmanager/permissionmanager.hpp"
#include "gatekeeper/sessionmanager/sessionmanager.hpp"
#include "gatekeeper/tokenmanager/tokenmanager.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/concepts.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

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
            void logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& ip_address, const std::string& group);

            bool isAuthenticationValid(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message);
            void removeSession(std::optional<uint64_t> client_id, const std::string& group);

            template <typename T>
            bool canCreate(const Requester& requester, const std::optional<jsoncons::json>& data_j, Http::Error& error);

            template <typename T>
            bool canRead(const Requester& requester, uint64_t entity_id, Http::Error& error);

            template <typename T>
            bool canUpdate(const Requester& requester, uint64_t entity_id, Http::Error& error);

            template <typename T>
            bool canDelete(const Requester& requester, uint64_t entity_id, Http::Error& error);

            template <typename T>
            bool canManageStaff(const Requester& requester, uint64_t id, Http::Error& error);

            template <Client_t T>
            bool canToggleActive(const Requester& requester, uint64_t id, Http::Error& error);

            template <Client_t T>
            bool canGetServices(const Requester& requester, uint64_t id, Http::Error& error);

            DOSDetector::Status isDosAttack(const DOSDetector::Request& request);

           private:
            std::optional<jsoncons::json>      parse_data(std::string_view data, std::string& message, bool& success);
            std::optional<Types::Credentials>  parse_credentials(std::string_view data, std::string& message, bool& success);
            std::shared_ptr<SessionManager>    sessionManager_    = Store::getObject<SessionManager>();
            std::shared_ptr<TokenManager>      tokenManager_      = Store::getObject<TokenManager>();
            std::shared_ptr<DOSDetector>       dosDetector_       = Store::getObject<DOSDetector>();
            std::shared_ptr<PermissionManager> permissionManager_ = Store::getObject<PermissionManager>();
        };
    }  // namespace v2
}  // namespace api
