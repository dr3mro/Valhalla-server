
#pragma once
#include <fmt/format.h>

#include <functional>
#include <jsoncons/json.hpp>

#include "controllers/appointmentcontroller/appointmentcontrollerbase.hpp"
#include "controllers/base/controller/controller.hpp"
#include "entities/base/entity.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>

class AppointmentController : public AppointmentControllerBase, public Controller
{
   public:
    explicit AppointmentController() = default;

    ~AppointmentController() override = default;

    // CRUDS
    void CreateAppointment(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void ReadAppointment(const crow::request &req, crow::response &res, const jsoncons::json &criteria) override;
    void UpdateAppointment(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void DeleteAppointment(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) override;
    void SearchAppointment(const crow::request &req, crow::response &res, const jsoncons::json &search_json) override;

   private:
    T                       entity;
    std::optional<uint64_t> getNextID() override
    {
        try
        {
            json json_nextval = databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", entity.getTableName()));

            if (json_nextval.empty())
            {
                std::cerr << fmt::format("json_nextval is empty\n");
            }

            auto obj = json_nextval.find("nextval");
            if (obj != json_nextval.object_range().end())
            {
                return obj->value().as<uint64_t>();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << fmt::format("Failed: {}\n", e.what());
        }
        return std::nullopt;
    }
};

template <typename T>
void AppointmentController<T>::CreateAppointment(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        auto nextID = getNextID();
        if (!nextID.has_value())
        {
            RestHelper::errorResponse(std::ref(res), crow::status::NOT_ACCEPTABLE, "Failed to generate next ID");
            return;
        }
        Entity::CreateData createData(body, nextID.value());

        T entity(createData);
        Controller::Create(std::ref(res), entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void AppointmentController<T>::ReadAppointment(const crow::request &req, crow::response &res, const json &criteria)
{
    (void)req;
    json response;
    try
    {
        uint64_t                 id   = criteria.at("id").as<uint64_t>();
        std::vector<std::string> data = criteria.at("schema").as<std::vector<std::string>>();

        Entity::ReadData readData(data, id);
        T                entity(readData);
        Controller::Read(std::ref(res), entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void AppointmentController<T>::UpdateAppointment(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json                    data(body);
        std::optional<uint64_t> id = data.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(std::ref(res), crow::status::NOT_ACCEPTABLE, "No ID provided");
            return;
        }
        Entity::UpdateData updateData(data, id.value());
        T                  entity(updateData);
        Controller::Update(std::ref(res), entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void AppointmentController<T>::DeleteAppointment(const crow::request &req, crow::response &res, const jsoncons::json &delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  entity(deleteData);
        Controller::Delete(std::ref(res), entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void AppointmentController<T>::SearchAppointment(const crow::request &req, crow::response &res, const jsoncons::json &search_json)
{
    (void)req;
    json response;
    try
    {
        bool                   success = false;
        typename T::SearchData searchData(search_json, success);
        if (success)
        {
            T entity(searchData);
            Controller::Search(std::ref(res), entity);
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}
