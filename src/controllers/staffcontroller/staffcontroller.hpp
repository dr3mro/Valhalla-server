
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/staffcontroller/staffcontrollerBase.hpp"
#include "entities/base/entity.hpp"
#include "utils/communicate/communicate.hpp"
#include "utils/helper/helper.hpp"

template <typename T>
class StaffController : public StaffControllerBase, public Controller
{
   public:
    StaffController() : cfg_(Store::getObject<Configurator>()), email_sender_daemon_config_(cfg_->get<Configurator::EmailSenderConfig>()) {}
    virtual ~StaffController() override = default;

    // CRUDS
    void AddStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void RemoveStaffFromEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void InviteStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;

   private:
    std::shared_ptr<Configurator>   cfg_;
    Configurator::EmailSenderConfig email_sender_daemon_config_;
};

template <typename T>
void StaffController<T>::AddStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    jsoncons::json staff_j;

    try
    {
        staff_j                  = jsoncons::json::parse(data);
        json             payload = staff_j.at("payload");
        Types::StaffData staffData(payload);

        T staff(staffData);
        Controller::addStaff(staff, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}
template <typename T>
void StaffController<T>::RemoveStaffFromEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
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
        Helper::failureResponse(e.what(), callback);
    }
}
template <typename T>
void StaffController<T>::InviteStaffToEntity(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
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
                Helper::successResponse(response.value(), callback);
            }
            else
            {
                Helper::errorResponse(drogon::k400BadRequest, "Failed to send invite.", callback);
            }
        }
        else
        {
            Helper::errorResponse(drogon::k400BadRequest, "Failed to create invite json.", callback);
        }
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}
