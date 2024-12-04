
#pragma once

#include <cstdint>

#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/global/global.hpp"

template <typename T>
class ClinicController : public EntityController<T>, public ClinicControllerBase
{
   private:
    // Primary template for other entities
    template <typename U = T>
    void CreateImpl(CALLBACK_ &&callback, std::string_view data)
        requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
    {
        jsoncons::json request_json = jsoncons::json::parse(data);
        bool           success      = false;

        Validator::Rule rule((Validator::Rule::Action::IGNORE_IF_MISSING_FROM_SCHEMA |
                              Validator::Rule::Action::IGNORE_IF_NOT_NULLABLE_IN_SCHEMA),
                             {"id"});

        api::v2::Http::Error error;
        try
        {
            std::optional<uint64_t> id = request_json.at("id").as<uint64_t>();
            if (!id.has_value())
            {
                callback(api::v2::Http::Status::BAD_REQUEST, "id not provided.");
                return;
            }

            success = Validator::validateDatabaseCreateSchema(T::getTableName(), request_json, error, rule);
            if (!success)
            {
                callback(error.code, error.message);
                return;
            }

            Types::Create_t clinic_create_data = Types::Create_t(request_json, id.value());

            T entity(clinic_create_data);
            Controller::Create(entity, std::move(callback));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename U = T>
    void CreateImpl(CALLBACK_ &&callback, std::string_view data)
        requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
    {
        EntityController<T>::Create(std::move(callback), data);
    }

    template <typename U = T>
    void DeleteImpl(CALLBACK_ &&callback, const std::optional<uint64_t> id)
        requires(std::is_same<U, Patient>::value || std::is_same<U, Visits>::value)
    {
        EntityController<T>::Delete(std::move(callback), id);
    }

    template <typename U = T>
    void DeleteImpl(CALLBACK_ &&callback, const std::optional<uint64_t> id)
        requires(!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value)
    {
        (void)id;
        callback(api::v2::Http::Status::BAD_REQUEST,
                 fmt::format("Delete is NOT implemented for entity type {}", T::getTableName()));
    }

    template <typename U = T>
    void SearchImpl(CALLBACK_ &&callback, std::string_view data)
        requires std::is_same<U, Patient>::value
    {
        EntityController<T>::Search(std::move(callback), data);
    }

    template <typename U = T>
    void SearchImpl(CALLBACK_ &&callback, std::string_view data)
        requires(!std::is_same<U, Patient>::value)
    {
        (void)data;
        callback(api::v2::Http::Status::BAD_REQUEST,
                 fmt::format("Search is NOT implemented for entity type {}", T::getTableName()));
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    void GetVisitsImpl(CALLBACK_ &&callback, const std::optional<uint64_t> id)
        requires std::is_same<U, Patient>::value
    {
        try
        {
            if (!id.has_value())
            {
                callback(api::v2::Http::Status::BAD_REQUEST, "Missing patient id.");
                return;
            }
            T entity((Types::Data_t(id.value())));
            Controller::GetVisits(entity, std::move(callback));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    void GetVisitsImpl(CALLBACK_ &&callback, const std::optional<uint64_t> id)
        requires(!std::is_same<U, Patient>::value)
    {
        (void)id;
        callback(api::v2::Http::Status::BAD_REQUEST,
                 fmt::format("GetVisit is NOT implemented for entity type {}", T::getTableName()));
    }

   public:
    explicit ClinicController() = default;

    virtual ~ClinicController() override = default;

    void Create(CALLBACK_ &&callback, std::string_view data) final
    {
        // here we call the actual implementation of the Create method
        CreateImpl(std::move(callback), data);
    }

    void Read(CALLBACK_ &&callback, std::string_view data) final
    {
        // Read for all clinic entities
        EntityController<T>::Read(std::move(callback), data);
    }
    void Update(CALLBACK_ &&callback, std::string_view data, const std::optional<uint64_t> id) final
    {
        // Update for all clinic entities
        EntityController<T>::Update(std::move(callback), data, id);
    }

    void Delete(CALLBACK_ &&callback, const std::optional<uint64_t> id) final { DeleteImpl(std::move(callback), id); }

    void Search(CALLBACK_ &&callback, std::string_view data) final
    {
        // search only for Patient
        SearchImpl(std::move(callback), data);
    }

    void GetVisits(CALLBACK_ &&callback, const std::optional<uint64_t> id) final
    {
        // GetVisits only for Patient
        GetVisitsImpl(std::move(callback), id);
    }
};
