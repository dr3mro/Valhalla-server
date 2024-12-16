#include "gatekeeper.hpp"

#include <cstdint>
#include <pqxx/internal/libpq-forward.hxx>
#include <utility>

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
#include "utils/global/concepts.hpp"
#include "utils/global/global.hpp"
#include "utils/global/requester.hpp"

using namespace api::v2;

void GateKeeper::login(CALLBACK_&& callback, std::string_view data, const std::string& ip_address, std::string_view group)
{
    try
    {
        bool                              success = false;
        std::string                       message;
        std::optional<Types::Credentials> credentials = parse_credentials(data, message, success);

        if (!success || !credentials.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "credentials parse error : " + message);
            return;
        }

        std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
        clientLoginData->group                                = group;
        clientLoginData->ip_address                           = ip_address;

        success = sessionManager_->login(credentials, clientLoginData, message);

        if (!success || !clientLoginData.has_value())
        {
            callback(Http::Status::UNAUTHORIZED, "login failed: " + message);
            return;
        }

        success = tokenManager_->generateToken(clientLoginData);
        if (!success)
        {
            callback(Http::Status::INTERNAL_SERVER_ERROR, "failed to generate token");
            return;
        }

        jsoncons::json token_object;
        token_object["token"]     = clientLoginData->token;
        token_object["username"]  = clientLoginData->username;
        token_object["client_id"] = clientLoginData->clientId;
        token_object["group"]     = clientLoginData->group;
        token_object["ipAddress"] = clientLoginData->ip_address;

        callback(Http::Status::OK, token_object.as<std::string>());
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

void GateKeeper::logout(CALLBACK_&& callback, const std::optional<std::string>& token, const std::string& ip_address, const std::string& group)
{
    try
    {
        std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
        clientLoginData->token                                = token;
        clientLoginData->group                                = group;
        clientLoginData->ip_address                           = ip_address;
        sessionManager_->logout(std::move(callback), clientLoginData);
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

bool GateKeeper::isAuthenticationValid(std::optional<Types::ClientLoginData>& clientLoginData, std::string& message)
{
    if (sessionManager_->clientHasValidSession(clientLoginData, message))
    {
        return true;
    }
    else if (tokenManager_->isTokenValid(clientLoginData, message))
    {
        return sessionManager_->storeSession(clientLoginData, message);
    }
    return false;
}

void GateKeeper::removeSession(std::optional<uint64_t> client_id, const std::string& group)
{
    std::optional<Types::ClientLoginData> clientLoginData = Types::ClientLoginData{};
    clientLoginData->clientId                             = client_id;
    clientLoginData->group                                = group;
    sessionManager_->removeSession(clientLoginData);
}

DOSDetector::Status GateKeeper::isDosAttack(const DOSDetector::Request& request) { return dosDetector_->is_dos_attack(request); }

std::optional<jsoncons::json> GateKeeper::parse_data(std::string_view data, std::string& message, bool& success)
{
    success = false;
    jsoncons::json j;
    try
    {
        j = jsoncons::json::parse(data);
    }
    catch (const std::exception& e)
    {
        message = fmt::format("Json parsing error : {} - {}", message, e.what());
        return false;
    }
    success = true;
    return j;
}

std::optional<Types::Credentials> GateKeeper::parse_credentials(std::string_view data, std::string& message, bool& success)
{
    success = false;

    std::optional<jsoncons::json> credentials_j = parse_data(data, message, success);
    if (!success || !credentials_j.has_value())
        return std::nullopt;

    Types::Credentials credentials;
    try
    {
        credentials.username = credentials_j.value().at("username").as<std::string>();
        credentials.password = credentials_j.value().at("password").as<std::string>();
    }
    catch (const std::exception& e)
    {
        message = fmt::format("Credentials parsing error : {} - {}", message, e.what());
        return std::nullopt;
    }
    success = true;
    return credentials;
}

template <typename T>
bool GateKeeper::canCreate(const Requester& requester, const std::optional<jsoncons::json>& data_j, Http::Error& error)
{
    return permissionManager_->canCreate<T>(requester, data_j, error);
}

template <typename T>
bool GateKeeper::canRead(const Requester& requester, const uint64_t entity_id, Http::Error& error)
{
    return permissionManager_->canRead<T>(requester, entity_id, error);
}

template <typename T>
bool GateKeeper::canUpdate(const Requester& requester, const uint64_t entity_id, Http::Error& error)
{
    return permissionManager_->canUpdate<T>(requester, entity_id, error);
}

template <typename T>
bool GateKeeper::canDelete(const Requester& requester, const uint64_t entity_id, Http::Error& error)
{
    return permissionManager_->canDelete<T>(requester, entity_id, error);
}

template <typename T>
bool GateKeeper::canManageStaff(const Requester& requester, uint64_t id, Http::Error& error)
{
    return permissionManager_->canManageStaff<T>(requester, id, error);
}

template <Client_t T>
bool GateKeeper::canToggleActive(const Requester& requester, const uint64_t id, Http::Error& error)
{
    return permissionManager_->canToggleActive<T>(requester, id, error);
}

template <Client_t T>
bool GateKeeper::canGetServices(const Requester& requester, const uint64_t id, Http::Error& error)
{
    return permissionManager_->canGetServices<T>(requester, id, error);
}

// canCreate specializations
#define INSTANTIATE_GATEKEEPER_CRUD(TYPE)                                                                            \
    template bool GateKeeper::canCreate<TYPE>(const Requester&, const std::optional<jsoncons::json>&, Http::Error&); \
    template bool GateKeeper::canRead<TYPE>(const Requester&, const uint64_t entity_id, Http::Error&);               \
    template bool GateKeeper::canUpdate<TYPE>(const Requester&, const uint64_t entity_id, Http::Error&);             \
    template bool GateKeeper::canDelete<TYPE>(const Requester&, const uint64_t entity_id, Http::Error&);

#define INSTANTIATE_GATEKEEPER_CLIENT(TYPE)                                                                            \
    INSTANTIATE_GATEKEEPER_CRUD(TYPE)                                                                                  \
    template bool GateKeeper::canGetServices<TYPE>(const Requester& requester, const uint64_t id, Http::Error& error); \
    template bool GateKeeper::canToggleActive<TYPE>(const Requester& requester, const uint64_t id, Http::Error& error);

#define INSTANTIATE_GATEKEEPER_ENTITY(TYPE) \
    INSTANTIATE_GATEKEEPER_CRUD(TYPE)       \
    template bool GateKeeper::canManageStaff<TYPE>(const Requester&, const uint64_t entity_id, Http::Error&);

// Usage:
INSTANTIATE_GATEKEEPER_CLIENT(User)
INSTANTIATE_GATEKEEPER_CLIENT(Provider)

INSTANTIATE_GATEKEEPER_ENTITY(Clinics)
INSTANTIATE_GATEKEEPER_ENTITY(Pharmacies)
INSTANTIATE_GATEKEEPER_ENTITY(Laboratories)
INSTANTIATE_GATEKEEPER_ENTITY(RadiologyCenters)

INSTANTIATE_GATEKEEPER_CRUD(Patient)
INSTANTIATE_GATEKEEPER_CRUD(Health)
INSTANTIATE_GATEKEEPER_CRUD(PatientDrugs)
INSTANTIATE_GATEKEEPER_CRUD(Reports)
INSTANTIATE_GATEKEEPER_CRUD(Visits)
INSTANTIATE_GATEKEEPER_CRUD(VisitDrugs)
INSTANTIATE_GATEKEEPER_CRUD(Prescriptions)
INSTANTIATE_GATEKEEPER_CRUD(Requests)
INSTANTIATE_GATEKEEPER_CRUD(PaidServices)
INSTANTIATE_GATEKEEPER_CRUD(ClinicAppointment)
INSTANTIATE_GATEKEEPER_CRUD(PharmacyAppointment)
INSTANTIATE_GATEKEEPER_CRUD(LaboratoryAppointment)
INSTANTIATE_GATEKEEPER_CRUD(RadiologyCenterAppointment)
