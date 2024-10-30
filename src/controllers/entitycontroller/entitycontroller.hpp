
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "entities/base/entity.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   private:
    T entity;

   public:
    EntityController()           = default;
    ~EntityController() override = default;
    // CRUDS
    void Create(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void Read(const crow::request &req, crow::response &res, const jsoncons::json &criteria) override;
    void Update(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) override;
    void Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json) override;

   protected:
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
void EntityController<T>::Create(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        auto nextID = getNextID();
        if (!nextID.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Failed to generate next ID");
            return;
        }
        Entity::CreateData createData(body, nextID.value());

        T entity(createData);
        Controller::Create(res, entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Read(const crow::request &req, crow::response &res, const json &criteria)
{
    (void)req;
    json response;
    try
    {
        uint64_t                 id   = criteria.at("id").as<uint64_t>();
        std::vector<std::string> data = criteria.at("schema").as<std::vector<std::string>>();

        Entity::ReadData readData(data, id);
        T                service(readData);
        Controller::Read(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Update(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json                    data(body);
        std::optional<uint64_t> id = data.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }
        Entity::UpdateData updateData(data, id.value());
        T                  service(updateData);
        Controller::Update(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  service(deleteData);
        Controller::Delete(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json)
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
            Controller::Search(res, service);
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}
