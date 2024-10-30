
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
    void Create(const crow::request &req, crow::response &res, const json &request_json) override;
    void Read(const crow::request &req, crow::response &res, const json &request_json) override;
    void Update(const crow::request &req, crow::response &res, const json &request_json) override;
    void Delete(const crow::request &req, crow::response &res, const json &request_json) override;
    void Search(const crow::request &req, crow::response &res, const json &request_json) override;

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
void EntityController<T>::Create(const crow::request &req, crow::response &res, const json &request_json)
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
        typename T::CreateData createData(request_json, nextID.value());

        T entity(createData);
        Controller::Create(res, entity);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Read(const crow::request &req, crow::response &res, const json &request_json)
{
    (void)req;
    json response;
    try
    {
        uint64_t                 id   = request_json.at("id").as<uint64_t>();
        std::vector<std::string> data = request_json.at("schema").as<std::vector<std::string>>();

        typename T::ReadData readData(data, id);
        T                    service(readData);
        Controller::Read(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Update(const crow::request &req, crow::response &res, const json &request_json)
{
    (void)req;
    json response;
    try
    {
        json                    data(request_json);
        std::optional<uint64_t> id = data.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }
        typename T::UpdateData updateData(data, id.value());
        T                      service(updateData);
        Controller::Update(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Delete(const crow::request &req, crow::response &res, const json &request_json)
{
    (void)req;
    try
    {
        typename T::DeleteData deleteData(request_json);
        T                      service(deleteData);
        Controller::Delete(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void EntityController<T>::Search(const crow::request &req, crow::response &res, const json &request_json)
{
    (void)req;
    json response;
    try
    {
        bool                   success = false;
        typename T::SearchData searchData(request_json, success);
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
