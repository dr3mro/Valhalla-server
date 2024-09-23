
#pragma once
#include <fmt/format.h>

#include <functional>
#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
#include "entities/base/entity.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
/**
 * @brief The ServiceController class is responsible for handling CRUD operations for a service entity.
 *
 * This class inherits from both ServiceControllerBase and Controller, allowing it to handle the specific
 * logic for creating, reading, updating, deleting, and searching service entities.
 *
 * The class provides the following methods:
 * - `CreateService`: Creates a new service entity based on the provided request data.
 * - `ReadService`: Retrieves a service entity based on the provided criteria.
 * - `UpdateService`: Updates an existing service entity based on the provided request data.
 * - `DeleteService`: Deletes a service entity based on the provided criteria.
 * - `SearchService`: Searches for service entities based on the provided criteria.
 *
 * The `getNextID` method is a private helper function that retrieves the next available ID for a new service entity.
 */
class ServiceController : public ServiceControllerBase, public Controller
{
   public:
    explicit ServiceController() = default;

    ~ServiceController() override = default;

    // CRUDS
    void CreateService(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void ReadService(const crow::request &req, crow::response &res, const jsoncons::json &criteria) override;
    void UpdateService(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void DeleteService(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) override;
    void SearchService(const crow::request &req, crow::response &res, const jsoncons::json &search_json) override;

   private:
    T                       service;
    std::optional<uint64_t> getNextID() override
    {
        try
        {
            json json_nextval = databaseController->executeQuery(fmt::format("SELECT NEXTVAL('{}_id_seq');", service.getTableName()));

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
void ServiceController<T>::CreateService(const crow::request &req, crow::response &res, const jsoncons::json &body)
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

        T service(createData);
        Controller::Create(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void ServiceController<T>::ReadService(const crow::request &req, crow::response &res, const json &criteria)
{
    (void)req;
    json response;
    try
    {
        uint64_t                 id   = criteria.at("id").as<uint64_t>();
        std::vector<std::string> data = criteria.at("schema").as<std::vector<std::string>>();

        Entity::ReadData readData(data, id);
        T                service(readData);
        Controller::Read(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void ServiceController<T>::UpdateService(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json                    data(body);
        std::optional<uint64_t> id = data.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(std::ref(res), crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }
        Entity::UpdateData updateData(data, id.value());
        T                  service(updateData);
        Controller::Update(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void ServiceController<T>::DeleteService(const crow::request &req, crow::response &res, const jsoncons::json &delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  service(deleteData);
        Controller::Delete(std::ref(res), service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}

template <typename T>
void ServiceController<T>::SearchService(const crow::request &req, crow::response &res, const jsoncons::json &search_json)
{
    (void)req;
    json response;
    try
    {
        bool               success = false;
        Entity::SearchData searchData(search_json, success);
        if (success)
        {
            T service(searchData);
            Controller::Search(std::ref(res), service);
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(std::ref(res), e.what());
    }
}
