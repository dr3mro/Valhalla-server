
#include "controllers/staffcontroller/staffcontroller.hpp"

#include <drogon/HttpTypes.h>
#include <fmt/format.h>

#include <cstdint>
#include <exception>
#include <jsoncons/basic_json.hpp>
#include <jsoncons/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "configurator/configurator.hpp"
#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "entities/base/types.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "store/store.hpp"
#include "utils/communicate/communicate.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"

template <typename T>
StaffController<T>::StaffController()
    : cfg_(Store::getObject<Configurator>()),
      email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()),
      gateKeeper(Store::getObject<GateKeeper>())
{
}

template <typename T>
void StaffController<T>::AddStaffToEntity(CALLBACK_&& callback, [[maybe_unused]] const Requester&& requester, std::string_view data)
{
    jsoncons::json staff_j;
    Http::Error    error;
    try
    {
        staff_j                  = jsoncons::json::parse(data);
        jsoncons::json   payload = staff_j.at("payload");
        Types::StaffData staffData(payload);

        T staff(staffData);

        Controller::addStaff(staff, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T>
void StaffController<T>::RemoveStaffFromEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    try
    {
        Http::Error      error;
        jsoncons::json   staff_j = jsoncons::json::parse(data);
        jsoncons::json   payload = staff_j.at("payload");
        uint64_t         _id     = staff_j.at("id").as<uint64_t>();
        Types::StaffData staffData(payload);

        T staff(staffData);
        if (!gateKeeper->canManageStaff<T>(requester, _id, error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }
        Controller::removeStaff(staff, std::move(callback));
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T>
void StaffController<T>::InviteStaffToEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data)
{
    try
    {
        Http::Error                error;
        jsoncons::json             staff_j = jsoncons::json::parse(data);
        uint64_t                   _id     = staff_j.at("nominee_id").as<uint64_t>();
        Types::StaffData           staffData(staff_j);
        std::optional<std::string> response;
        T                          staff(staffData);

        if (!gateKeeper->canManageStaff<T>(requester, _id, error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }

        if (staffData.parse_status && staffData.toInviteJson(staff_j))
        {
            response = Communicate::sendRequest(email_sender_daemon_config_.host, email_sender_daemon_config_.port,
                email_sender_daemon_config_.message_queue_path, drogon::HttpMethod::Post, staff_j.to_string());

            if (response.has_value())
            {
                std::move(callback)(api::v2::Http::Status::OK, response.value());
            }
            else
            {
                std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Failed to send invite.");
            }
        }
        else
        {
            std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Failed to create invite json.");
        }
    }
    catch (const std::exception& e)
    {
        CRITICALMESSAGERESPONSE
    }
}

#include "entities/services/clinics/clinics.hpp"   //IWYU pragma : keep
#include "entities/services/laboratories.hpp"      // IWYU pragma : keep
#include "entities/services/pharmacies.hpp"        // IWYU pragma: keep
#include "entities/services/radiologycenters.hpp"  //IWYU pragma : keep

#define INSTANTIATE_STAFF_CONTROLLER(TYPE) /*NOLINT*/                                                                                     \
    template StaffController<TYPE>::StaffController();                                                                                    \
    template void StaffController<TYPE>::AddStaffToEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data);      \
    template void StaffController<TYPE>::RemoveStaffFromEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data); \
    template void StaffController<TYPE>::InviteStaffToEntity(CALLBACK_&& callback, const Requester&& requester, std::string_view data);

// Instantiate for all entity types

INSTANTIATE_STAFF_CONTROLLER(Clinics)
INSTANTIATE_STAFF_CONTROLLER(Pharmacies)
INSTANTIATE_STAFF_CONTROLLER(Laboratories)
INSTANTIATE_STAFF_CONTROLLER(RadiologyCenters)
