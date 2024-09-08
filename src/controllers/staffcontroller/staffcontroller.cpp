#include "staffcontroller.hpp"

#include "utils/communicate/communicate.hpp"
template <typename T>
void StaffController<T>::AddStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json              data(body);
        json              payload = data.at("payload");
        Entity::StaffData staffData(payload);

        T service(staffData);
        Controller::addStaff(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
    }
}

template <typename T>
void StaffController<T>::RemoveStaffFromEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json              data(body);
        json              payload = data.at("payload");
        Entity::StaffData staffData(payload);

        T service(staffData);
        Controller::removeStaff(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
    }
}

template <typename T>
void StaffController<T>::InviteStaffToEntity(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json              data(body);
        json              payload = data.at("payload");
        Entity::StaffData staffData(payload);

        T service(staffData);
        if (service.toInviteJson(data).parse_status)
        {
            auto status = Communicate::sendRequest(email_sender_daemon_config_.host.data(), email_sender_daemon_config_.port,
                                                   email_sender_daemon_config_.message_queue_path.data(), "POST", data.to_string().c_str());
            res.code    = 200;
            res.write(data.to_string());
            res.end();
        }
        else
        {
            res.code = 400;
            res.write("Failed to create invite json");
            res.end();
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::sendErrorResponse(std::ref(res), std::ref(response), "Failure", fmt::format("Failed: {}", e.what()), -2, 500);
    }
}