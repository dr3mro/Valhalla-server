
#pragma once
#include <fmt/format.h>

#include <cstdint>
#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "gatekeeper/gatekeeper.hpp"
#include "utils/communicate/communicate.hpp"
#include "utils/global/global.hpp"
#include "utils/global/http.hpp"

template <typename T>
class StaffController : public StaffControllerBase, public Controller
{
   public:
    StaffController() : cfg_(Store::getObject<Configurator>()), email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()) {}
    virtual ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void RemoveStaffFromEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;
    void InviteStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data) override;

   private:
    std::shared_ptr<Configurator>   cfg_;
    Configurator::EmailSenderConfig email_sender_daemon_config_;
    std::shared_ptr<GateKeeper>     gateKeeper = Store::getObject<GateKeeper>();
};

template <typename T>
void StaffController<T>::AddStaffToEntity(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, std::string_view data)
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
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T>
void StaffController<T>::RemoveStaffFromEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    try
    {
        Http::Error      error;
        jsoncons::json   staff_j = jsoncons::json::parse(data);
        jsoncons::json   payload = staff_j.at("payload");
        uint64_t         id      = staff_j.at("id").as<uint64_t>();
        Types::StaffData staffData(payload);

        T staff(staffData);
        if (!gateKeeper->canManageStaff<T>(requester, T::getTableName(), id, error))
        {
            callback(error.code, error.message);
            return;
        }
        Controller::removeStaff(staff, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T>
void StaffController<T>::InviteStaffToEntity(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    try
    {
        Http::Error                error;
        jsoncons::json             staff_j = jsoncons::json::parse(data);
        uint64_t                   id      = staff_j.at("nominee_id").as<uint64_t>();
        Types::StaffData           staffData(staff_j);
        std::optional<std::string> response;
        T                          staff(staffData);

        if (!gateKeeper->canManageStaff<T>(requester, T::getTableName(), id, error))
        {
            callback(error.code, error.message);
            return;
        }

        if (staffData.parse_status && staffData.toInviteJson(staff_j))
        {
            response = Communicate::sendRequest(email_sender_daemon_config_.host.data(), email_sender_daemon_config_.port,
                email_sender_daemon_config_.message_queue_path.data(), drogon::HttpMethod::Post, staff_j.to_string().c_str());

            if (response.has_value())
            {
                callback(api::v2::Http::Status::OK, response.value());
            }
            else
            {
                callback(api::v2::Http::Status::BAD_REQUEST, "Failed to send invite.");
            }
        }
        else
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "Failed to create invite json.");
        }
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
