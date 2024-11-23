
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "utils/global/types.hpp"
template <typename T, typename CALLBACK>
class EntityController : public Controller, public EntityControllerBase<CALLBACK>
{
   public:
    EntityController()          = default;
    virtual ~EntityController() = default;
    // CRUDS

    void Create(CALLBACK &&callback, std::string_view data) override;
    void Read(CALLBACK &&callback, std::string_view data) override;
    void Update(CALLBACK &&callback, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK &&callback, std::optional<uint64_t> id) override;
    void Search(CALLBACK &&callback, std::string_view data) override;

   protected:
};

template <typename T, typename CALLBACK>
void EntityController<T, CALLBACK>::Create(CALLBACK &&callback, std::string_view data)
{
    try
    {
        bool                       success = false;
        api::v2::Global::HttpError error;

        auto next_id = this->template getNextID<T>(error);
        if (!next_id.has_value())
        {
            callback(error.code, fmt::format("Failed to generate next ID, {}.", error.message));
            return;
        }

        Types::Create_t entity_data = Types::Create_t(jsoncons::json::parse(data), next_id.value());
        if (!success)
        {
            callback(error.code, fmt::format("Failed to create entity, {}.", error.message));
            return;
        }

        T entity(entity_data);
        Controller::Create(entity, callback);
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T, typename CALLBACK>
void EntityController<T, CALLBACK>::Read(CALLBACK &&callback, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json                    = jsoncons::json::parse(data);
        uint64_t                 id     = request_json.at("id").as<uint64_t>();
        std::vector<std::string> schema = request_json.at("schema").as<std::vector<std::string>>();

        T entity((Types::Read_t(schema, id)));
        Controller::Read(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T, typename CALLBACK>
void EntityController<T, CALLBACK>::Update(CALLBACK &&callback, std::string_view data, const std::optional<uint64_t> id)
{
    jsoncons::json request_json;
    try
    {
        request_json = jsoncons::json::parse(data);

        if (!id.has_value())
        {
            callback(400, "No id provided.");
            return;
        }

        T entity((Types::Update_t(request_json, id.value())));
        Controller::Update(entity, callback);
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T, typename CALLBACK>
void EntityController<T, CALLBACK>::Delete(CALLBACK &&callback, const std::optional<uint64_t> id)
{
    try
    {
        if (!id.has_value())
        {
            callback(406, "Invalid id provided");
            return;
        }

        T entity(Types::Delete_t(id.value()));
        Controller::Delete(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T, typename CALLBACK>
void EntityController<T, CALLBACK>::Search(CALLBACK &&callback, std::string_view data)
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
            callback(406, "Failed to search");
        }
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
