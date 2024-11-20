
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "utils/helper/helper.hpp"

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   public:
    EntityController()          = default;
    virtual ~EntityController() = default;
    // CRUDS

    void Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    void Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::optional<uint64_t> id) override;
    void Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;

   protected:
};

template <typename T>
void EntityController<T>::Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    try
    {
        auto next_id = this->template getNextID<T>();
        if (!next_id.has_value())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Failed to generate next ID", callback);
            return;
        }

        T entity((Types::Create_t(jsoncons::json::parse(data), next_id.value())));
        Controller::Create(entity, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <typename T>
void EntityController<T>::Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json                    = jsoncons::json::parse(data);
        uint64_t                 id     = request_json.at("id").as<uint64_t>();
        std::vector<std::string> schema = request_json.at("schema").as<std::vector<std::string>>();

        T entity((Types::Read_t(schema, id)));
        Controller::Read(entity, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <typename T>
void EntityController<T>::Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json               = jsoncons::json::parse(data);
        std::optional<uint64_t> id = request_json.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            Helper::errorResponse(drogon::k400BadRequest, "No id provided", callback);
            return;
        }

        T entity((Types::Update_t(request_json, id.value())));
        Controller::Update(entity, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <typename T>
void EntityController<T>::Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
{
    try
    {
        if (!id.has_value())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Invalid id provided", callback);
            return;
        }

        T entity(Types::Delete_t(id.value()));
        Controller::Delete(entity, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

template <typename T>
void EntityController<T>::Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json = jsoncons::json::parse(data);
        bool success = false;
        T    entity((Types::Search_t(request_json, success)));

        if (success)
        {
            Controller::Search(entity, callback);
        }
        else
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Failed to search", callback);
        }
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}
