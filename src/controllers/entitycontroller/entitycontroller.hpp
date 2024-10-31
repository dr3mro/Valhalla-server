
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "entities/base/entity.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
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
            T    entity;
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
    try
    {
        auto next_id = getNextID();
        if (!next_id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Failed to generate next ID");
            return;
        }
        T entity((typename T::Create_t(request_json, next_id.value())));
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
        uint64_t                 id     = request_json.at("id").as<uint64_t>();
        std::vector<std::string> schema = request_json.at("schema").as<std::vector<std::string>>();

        T entity((typename T::Read_t(schema, id)));
        Controller::Read(res, entity);
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
    try
    {
        json                    data_j(request_json);
        std::optional<uint64_t> id = data_j.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }

        T entity((typename T::Update_t(data_j, id.value())));
        Controller::Update(res, entity);
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
        json                    data_j(request_json);
        std::optional<uint64_t> id = data_j.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }

        T entity((typename T::Delete_t(id.value())));
        Controller::Delete(res, entity);
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
        bool success = false;
        T    entity((typename T::Search_t(request_json, success)));

        if (success)
        {
            Controller::Search(res, entity);
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}
