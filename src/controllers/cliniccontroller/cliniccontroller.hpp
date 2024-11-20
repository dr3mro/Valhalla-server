
#pragma once

#include <cstdint>

#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/helper/helper.hpp"

using json = jsoncons::json;

template <typename T>
class ClinicController : public EntityController<T>, public ClinicControllerBase
{
   private:
    // Primary template for other entities
    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> CreateImpl(
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
    {
        jsoncons::json request_json = jsoncons::json::parse(data);
        try
        {
            std::optional<uint64_t> id = request_json.at("id").as<uint64_t>();
            if (!id.has_value())
            {
                Helper::errorResponse(drogon::k400BadRequest, "id not provided.", callback);
                return;
            }

            T entity(Types::Create_t(request_json, id.value()));

            if (entity.template check_id_exists<Types::Create_t>())
            {
                Helper::errorResponse(drogon::k409Conflict, "id already exists.", callback);
                return;
            }

            Controller::Create(entity, callback);
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> CreateImpl(
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
    {
        EntityController<T>::Create(std::move(callback), data);
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> DeleteImpl(
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
    {
        EntityController<T>::Delete(std::move(callback), id);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> DeleteImpl(
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
    {
        (void)id;
        Helper::failureResponse(fmt::format("Delete is NOT implemented for entity type {}", T::getTableName()), callback);
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> SearchImpl(std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                                                       std::string_view                                       data)
    {
        EntityController<T>::Search(std::move(callback), data);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value, void> SearchImpl(std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                                                        std::string_view                                       data)
    {
        (void)data;
        Helper::failureResponse(fmt::format("Search is NOT implemented for entity type {}", T::getTableName()), callback);
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> GetVisitsImpl(std::function<void(const drogon::HttpResponsePtr &)> &&callback,
                                                                          const std::optional<uint64_t>                          id)
    {
        try
        {
            if (!id.has_value())
            {
                Helper::errorResponse(drogon::k400BadRequest, "Missing patient id.", callback);
                return;
            }
            T entity((Types::Data_t(id.value())));
            Controller::GetVisits(entity, std::move(callback));
        }
        catch (const std::exception &e)
        {
            Helper::failureResponse(e.what(), callback);
        }
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    typename std::enable_if<!std::is_same<U, Patient>::value, void>::type GetVisitsImpl(
        std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id)
    {
        (void)id;
        Helper::failureResponse(fmt::format("GetVisit is NOT implemented for entity type {}", T::getTableName()), callback);
    }

   public:
    explicit ClinicController() = default;

    virtual ~ClinicController() override = default;

    void Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) final
    {
        // here we call the actual implementation of the Create method
        CreateImpl(std::move(callback), data);
    }

    void Read(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) final
    {
        // Read for all clinic entities
        EntityController<T>::Read(std::move(callback), data);
    }
    void Update(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) final
    {
        // Update for all clinic entities
        EntityController<T>::Update(std::move(callback), data);
    }

    void Delete(std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id) final
    {
        DeleteImpl(std::move(callback), id);
    }

    void Search(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) final
    {
        // search only for Patient
        SearchImpl(std::move(callback), data);
    }

    void GetVisits(std::function<void(const drogon::HttpResponsePtr &)> &&callback, const std::optional<uint64_t> id) final
    {
        // GetVisits only for Patient
        GetVisitsImpl(std::move(callback), id);
    }
};
