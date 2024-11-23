
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
        bool                            success = false;
        api::v2::Global::HttpError      error;
        std::unordered_set<std::string> exclude;
        auto                            next_id = this->template getNextID<T>(error);
        if (!next_id.has_value())
        {
            callback(error.code, fmt::format("Failed to generate next ID, {}.", error.message));
            return;
        }

        std::optional<jsoncons::json> request_json = jsoncons::json::parse(data);

        if (!request_json.has_value())
        {
            callback(400, "Invalid request body.");
            return;
        }

        success = Validator::validateDatabaseSchema(T::getTableName(), request_json.value(), error, exclude);

        if (!success)
        {
            callback(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        Types::Create_t entity_data = Types::Create_t(request_json.value(), next_id.value());

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
        request_json                           = jsoncons::json::parse(data);
        uint64_t                        id     = request_json.at("id").as<uint64_t>();
        std::unordered_set<std::string> schema = request_json.at("schema").as<std::unordered_set<std::string>>();
        api::v2::Global::HttpError      error;

        if (!Validator::ensureAllKeysExist(schema, std::format("{}_safe", T::getTableName()), error))
        {
            callback(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

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
    try
    {
        bool                            success = false;
        api::v2::Global::HttpError      error;
        std::unordered_set<std::string> exclude{};

        if (!id.has_value())
        {
            callback(400, "No id provided.");
            return;
        }

        std::optional<jsoncons::json> request_json = jsoncons::json::parse(data);

        if (!request_json.has_value())
        {
            callback(400, "Invalid request body.");
            return;
        }

        success = Validator::validateDatabaseSchema(T::getTableName(), request_json.value(), error, exclude);

        if (!success)
        {
            callback(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        Types::Update_t entity_data = Types::Update_t(request_json.value(), id.value());
        T               entity(entity_data);

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
