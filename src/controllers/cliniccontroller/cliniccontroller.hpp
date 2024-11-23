
#pragma once

#include <cstdint>

#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/global/global.hpp"

using json = jsoncons::json;

template <typename T, typename CALLBACK>
class ClinicController : public EntityController<T, CALLBACK>, public ClinicControllerBase<CALLBACK>
{
   private:
    // Primary template for other entities
    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> CreateImpl(CALLBACK &&callback, std::string_view data)
    {
        jsoncons::json                  request_json = jsoncons::json::parse(data);
        bool                            success      = false;
        std::unordered_set<std::string> exclude{"id"};
        api::v2::Global::HttpError      error;
        try
        {
            std::optional<uint64_t> id = request_json.at("id").as<uint64_t>();
            if (!id.has_value())
            {
                callback(400, "id not provided.");
                return;
            }

            success = Validator::validateDatabaseSchema(T::getTableName(), request_json, error, exclude, false);
            if (!success)
            {
                callback(400, error.message);
                return;
            }

            Types::Create_t clinic_create_data = Types::Create_t(request_json, id.value());

            T entity(clinic_create_data);

            if (entity.template check_id_exists<Types::Create_t>())
            {
                callback(409, "id already exists.");
                return;
            }

            Controller::Create(entity, std::move(callback));
        }
        catch (const std::exception &e)
        {
            CRITICALMESSAGERESPONSE
        }
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> CreateImpl(CALLBACK &&callback, std::string_view data)
    {
        EntityController<T, CALLBACK>::Create(std::move(callback), data);
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> DeleteImpl(CALLBACK                    &&callback,
                                                                                                         const std::optional<uint64_t> id)
    {
        EntityController<T, CALLBACK>::Delete(std::move(callback), id);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> DeleteImpl(CALLBACK                    &&callback,
                                                                                                           const std::optional<uint64_t> id)
    {
        (void)id;
        callback(400, fmt::format("Delete is NOT implemented for entity type {}", T::getTableName()));
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> SearchImpl(CALLBACK &&callback, std::string_view data)
    {
        EntityController<T, CALLBACK>::Search(std::move(callback), data);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value, void> SearchImpl(CALLBACK &&callback, std::string_view data)
    {
        (void)data;
        callback(400, fmt::format("Search is NOT implemented for entity type {}", T::getTableName()));
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> GetVisitsImpl(CALLBACK &&callback, const std::optional<uint64_t> id)
    {
        try
        {
            if (!id.has_value())
            {
                callback(400, "Missing patient id.");
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
    typename std::enable_if<!std::is_same<U, Patient>::value, void>::type GetVisitsImpl(CALLBACK &&callback, const std::optional<uint64_t> id)
    {
        (void)id;
        callback(400, fmt::format("GetVisit is NOT implemented for entity type {}", T::getTableName()));
    }

   public:
    explicit ClinicController() = default;

    virtual ~ClinicController() override = default;

    void Create(CALLBACK &&callback, std::string_view data) final
    {
        // here we call the actual implementation of the Create method
        CreateImpl(std::move(callback), data);
    }

    void Read(CALLBACK &&callback, std::string_view data) final
    {
        // Read for all clinic entities
        EntityController<T, CALLBACK>::Read(std::move(callback), data);
    }
    void Update(CALLBACK &&callback, std::string_view data, const std::optional<uint64_t> id) final
    {
        // Update for all clinic entities
        EntityController<T, CALLBACK>::Update(std::move(callback), data, id);
    }

    void Delete(CALLBACK &&callback, const std::optional<uint64_t> id) final { DeleteImpl(std::move(callback), id); }

    void Search(CALLBACK &&callback, std::string_view data) final
    {
        // search only for Patient
        SearchImpl(std::move(callback), data);
    }

    void GetVisits(CALLBACK &&callback, const std::optional<uint64_t> id) final
    {
        // GetVisits only for Patient
        GetVisitsImpl(std::move(callback), id);
    }
};
