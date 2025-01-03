#include "controllers/entitycontroller/entitycontroller.hpp"

#include <fmt/core.h>

#include <cstdint>
#include <exception>
#include <format>
#include <jsoncons/basic_json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "entities/base/types.hpp"
#include "utils/global/callback.hpp"
#include "utils/global/http.hpp"
#include "validator/validator.hpp"

using HttpError = api::v2::Http::Error;
using Create_t  = api::v2::Types::Create_t;
using Update_t  = api::v2::Types::Update_t;
using Delete_t  = api::v2::Types::Delete_t;
using Read_t    = api::v2::Types::Read_t;

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    try
    {
        bool            success = false;
        HttpError       error;
        Validator::Rule rule((Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA | Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA), {"id"});
        auto            next_id = this->template getNextID<T>(error);

        std::optional<jsoncons::json> request_j = jsoncons::json::parse(data);

        if (!request_j.has_value())
        {
            std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
            return;
        }

        success = Validator::validateDatabaseCreateSchema(T::getTableName(), request_j, error, rule);

        if (!success)
        {
            std::move(callback)(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        if (!gateKeeper->canCreate<T>(requester, request_j, error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }

        if (!next_id.has_value())
        {
            std::move(callback)(error.code, fmt::format("Failed to generate next ID, {}.", error.message));
            return;
        }

        Create_t entity_data = Create_t(request_j.value(), next_id.value());

        T entity(entity_data);

        Controller::Create(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data)
{
    try
    {
        jsoncons::json request_j = jsoncons::json::parse(data);
        uint64_t       _id       = request_j.at("id").as<uint64_t>();
        HttpError      error;

        if (!gateKeeper->canRead<T>(requester, _id, error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }

        std::unordered_set<std::string> schema = request_j.at("schema").as<std::unordered_set<std::string>>();

        Validator::Rule rule(Validator::Rule::Action::ASSERT_NOT_PRESENT, {"id", "username", "password", "created_at", "updated_at"});

        if (!Validator::validateDatabaseReadSchema(schema, std::format("{}_safe", T::getTableName()), error, rule))
        {
            std::move(callback)(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        T entity((Read_t(schema, _id)));

        Controller::Read(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}
template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Update(
    CALLBACK_ &&callback, const Requester &&requester, std::string_view data, const std::optional<uint64_t> _id)
{
    try
    {
        bool                 success = false;
        api::v2::Http::Error error;

        if (!_id.has_value())
        {
            std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "No id provided.");
            return;
        }

        std::optional<jsoncons::json> request_json = jsoncons::json::parse(data);

        if (!request_json.has_value())
        {
            std::move(callback)(api::v2::Http::Status::BAD_REQUEST, "Invalid request body.");
            return;
        }

        Validator::Rule rule(Validator::Rule::Action::NONE, {});
        success = Validator::validateDatabaseUpdateSchema(T::getTableName(), request_json, error, rule);

        if (!success)
        {
            std::move(callback)(error.code, fmt::format("Failed to validate request body, {}.", error.message));
            return;
        }

        if (!gateKeeper->canUpdate<T>(requester, _id.value(), error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }

        Update_t entity_data = Update_t(request_json.value(), _id.value());

        T entity(entity_data);

        Controller::Update(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Delete(CALLBACK_ &&callback, const Requester &&requester, const std::optional<uint64_t> _id)
{
    try
    {
        if (!_id.has_value())
        {
            std::move(callback)(api::v2::Http::Status::NOT_ACCEPTABLE, "Invalid id provided");
            return;
        }

        HttpError error;

        if (!gateKeeper->canDelete<T>(requester, _id.value(), error))
        {
            std::move(callback)(error.code, error.message);
            return;
        }

        T entity(Delete_t(_id.value()));

        Controller::Delete(entity, std::move(callback));
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

template <typename T>
inline void __attribute((always_inline)) EntityController<T>::Search(CALLBACK_ &&callback, [[maybe_unused]] const Requester &&requester, std::string_view data)
{
    jsoncons::json request_json;
    try
    {
        request_json = jsoncons::json::parse(data);
        bool success = false;
        T    entity((Search_t(request_json, success)));

        if (success)
        {
            Controller::Search(entity, std::move(callback));
        }
        else
        {
            std::move(callback)(api::v2::Http::Status::NOT_ACCEPTABLE, "Failed to search");
        }
    }
    catch (const std::exception &e)
    {
        CRITICALMESSAGERESPONSE
    }
}

#define INSTANTIATE_ENTITY_CONTROLLER(TYPE) /*NOLINT*/                                                                                                  \
    template void EntityController<TYPE>::Create(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                             \
    template void EntityController<TYPE>::Read(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);                               \
    template void EntityController<TYPE>::Update(CALLBACK_ &&callback, const Requester &&requester, std::string_view data, std::optional<uint64_t> id); \
    template void EntityController<TYPE>::Delete(CALLBACK_ &&callback, const Requester &&requester, std::optional<uint64_t> id);                        \
    template void EntityController<TYPE>::Search(CALLBACK_ &&callback, const Requester &&requester, std::string_view data);

#include "gatekeeper/includes.hpp"  // IWYU pragma: keep
// Instantiate for all entity types
INSTANTIATE_ENTITY_CONTROLLER(User)
INSTANTIATE_ENTITY_CONTROLLER(Provider)
INSTANTIATE_ENTITY_CONTROLLER(Clinics)
INSTANTIATE_ENTITY_CONTROLLER(Pharmacies)
INSTANTIATE_ENTITY_CONTROLLER(Laboratories)
INSTANTIATE_ENTITY_CONTROLLER(RadiologyCenters)
INSTANTIATE_ENTITY_CONTROLLER(Patient)
INSTANTIATE_ENTITY_CONTROLLER(Health)
INSTANTIATE_ENTITY_CONTROLLER(PatientDrugs)
INSTANTIATE_ENTITY_CONTROLLER(Reports)
INSTANTIATE_ENTITY_CONTROLLER(Visits)
INSTANTIATE_ENTITY_CONTROLLER(VisitDrugs)
INSTANTIATE_ENTITY_CONTROLLER(Prescriptions)
INSTANTIATE_ENTITY_CONTROLLER(Requests)
INSTANTIATE_ENTITY_CONTROLLER(PaidServices)
INSTANTIATE_ENTITY_CONTROLLER(ClinicAppointment)
INSTANTIATE_ENTITY_CONTROLLER(PharmacyAppointment)
INSTANTIATE_ENTITY_CONTROLLER(LaboratoryAppointment)
INSTANTIATE_ENTITY_CONTROLLER(RadiologyCenterAppointment)
