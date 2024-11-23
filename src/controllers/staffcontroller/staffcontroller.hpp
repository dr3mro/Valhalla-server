
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "utils/communicate/communicate.hpp"
#include "utils/global/global.hpp"

template <typename T, typename CALLBACK>
class StaffController : public StaffControllerBase<CALLBACK>, public Controller
{
   public:
    StaffController() : cfg_(Store::getObject<Configurator>()), email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()) {}
    virtual ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(CALLBACK &&callback, std::string_view data) override;
    void RemoveStaffFromEntity(CALLBACK &&callback, std::string_view data) override;
    void InviteStaffToEntity(CALLBACK &&callback, std::string_view data) override;

   private:
    std::shared_ptr<Configurator>   cfg_;
    Configurator::EmailSenderConfig email_sender_daemon_config_;
};

template <typename T, typename CALLBACK>
void StaffController<T, CALLBACK>::AddStaffToEntity(CALLBACK &&callback, std::string_view data)
{
    jsoncons::json staff_j;

    try
    {
        staff_j                  = jsoncons::json::parse(data);
        json             payload = staff_j.at("payload");
        Types::StaffData staffData(payload);

        T staff(staffData);
        Controller::addStaff(staff, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T, typename CALLBACK>
void StaffController<T, CALLBACK>::RemoveStaffFromEntity(CALLBACK &&callback, std::string_view data)
{
    jsoncons::json staff_j;

    try
    {
        staff_j                  = jsoncons::json::parse(data);
        json             payload = staff_j.at("payload");
        Types::StaffData staffData(payload);

        T staff(staffData);
        Controller::removeStaff(staff, callback);
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T, typename CALLBACK>
void StaffController<T, CALLBACK>::InviteStaffToEntity(CALLBACK &&callback, std::string_view data)
{
    jsoncons::json staff_j;
    try
    {
        staff_j = jsoncons::json::parse(data);
        Types::StaffData           staffData(staff_j);
        std::optional<std::string> response;
        T                          staff(staffData);
        if (staffData.parse_status && staffData.toInviteJson(staff_j))
        {
            response = Communicate::sendRequest(email_sender_daemon_config_.host.data(), email_sender_daemon_config_.port,
                                                email_sender_daemon_config_.message_queue_path.data(), drogon::HttpMethod::Post,
                                                staff_j.to_string().c_str());

            if (response.has_value())
            {
                callback(200, response.value());
            }
            else
            {
                callback(400, "Failed to send invite.");
            }
        }
        else
        {
            callback(400, "Failed to create invite json.");
        }
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
