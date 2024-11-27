
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   public:
    EntityController()          = default;
    virtual ~EntityController() = default;
    // CRUDS

    void Create(CALLBACK_ &&callback, std::string_view data) override;
    void Read(CALLBACK_ &&callback, std::string_view data) override;
    void Update(CALLBACK_ &&callback, std::string_view data, std::optional<uint64_t> id) override;
    void Delete(CALLBACK_ &&callback, std::optional<uint64_t> id) override;
    void Search(CALLBACK_ &&callback, std::string_view data) override;

   protected:
};

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Create(CALLBACK_ &&callback, std::string_view data)
{
    try
    {
        bool                            success = false;
        api::v2::Http::Error            error;
        std::unordered_set<std::string> exclude;
        auto                            next_id = this->template getNextID<T>(error);

        if (!next_id.has_value())
        {
            callback(error.code, fmt::format("Failed to generate next ID, {}.", error.message));
            return;
        }

        std::optional<jsoncons::json> request_j = jsoncons::json::parse(data);

        if (!request_j.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
            return;
        }

        success = Validator::validateDatabaseCreateSchema(T::getTableName(), request_j, error);

        if (!success)
        {
            callback(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        Types::Create_t entity_data = Types::Create_t(request_j.value(), next_id.value());

        T entity(entity_data);

        Controller::Create(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Read(CALLBACK_ &&callback, std::string_view data)
{
    CTRACK;
    try
    {
        jsoncons::json                  request_j = jsoncons::json::parse(data);
        uint64_t                        id        = request_j.at("id").as<uint64_t>();
        std::unordered_set<std::string> schema    = request_j.at("schema").as<std::unordered_set<std::string>>();
        api::v2::Http::Error            error;

        if (!Validator::validateDatabaseReadSchema(schema, std::format("{}_safe", T::getTableName()), error))
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

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Update(CALLBACK_ &&callback, std::string_view data, const std::optional<uint64_t> id)
{
    try
    {
        bool                            success = false;
        api::v2::Http::Error            error;
        std::unordered_set<std::string> exclude{};

        if (!id.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "No id provided.");
            return;
        }

        std::optional<jsoncons::json> request_json = jsoncons::json::parse(data);

        if (!request_json.has_value())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
            return;
        }

        success = Validator::validateDatabaseUpdateSchema(T::getTableName(), request_json, error, exclude);

        if (!success)
        {
            callback(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        Types::Update_t entity_data = Types::Update_t(request_json.value(), id.value());

        T entity(entity_data);

        if (!entity.template check_id_exists<Types::Update_t>())
        {
            callback(api::v2::Http::Status::BAD_REQUEST, "ID does not exist");
            return;
        }

        Controller::Update(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Delete(CALLBACK_ &&callback, const std::optional<uint64_t> id)
{
    try
    {
        if (!id.has_value())
        {
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
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

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Search(CALLBACK_ &&callback, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json = jsoncons::json::parse(data);
        bool success = false;
        T    entity((Types::Search_t(request_json, success)));

        if (success)
        {
            Controller::Search(entity, std::move(callback));
        }
        else
        {
            callback(api::v2::Http::Status::NOT_ACCEPTABLE, "Failed to search");
        }
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
