
#pragma once

#include <cstdint>

#include "controllers/cliniccontroller/cliniccontrollerbase.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "entities/services/clinics/visits/visits.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
class ClinicController : public EntityController<T>, public ClinicControllerBase
{
   private:
    // Primary template for other entities
    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> CreateImpl(const crow::request &req,
                                                                                                           crow::response      &res,
                                                                                                           const json          &request_json)
    {
        (void)req;
        try
        {
            std::optional<uint64_t> id = request_json.at("id").as<uint64_t>();
            if (!id.has_value())
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "id not provided.");
                return;
            }

            T entity((typename U::Create_t(request_json, id.value())));

            if (entity.check_id_exists())
            {
                RestHelper::errorResponse(res, crow::status::BAD_REQUEST, "id already exists.");
                return;
            }

            Controller::Create(res, entity);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> CreateImpl(const crow::request &req,
                                                                                                         crow::response      &res,
                                                                                                         const json          &request_json)
    {
        EntityController<T>::Create(req, res, request_json);
    }

    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value || std::is_same<U, Visits>::value, void> DeleteImpl(
        const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params)
    {
        EntityController<T>::Delete(req, res, params);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value && !std::is_same<U, Visits>::value, void> DeleteImpl(
        const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params)
    {
        (void)req;
        (void)params;
        RestHelper::failureResponse(res, "Delete is NOT implemented for this entity");
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> SearchImpl(const crow::request &req, crow::response &res, const json &request_json)
    {
        EntityController<T>::Search(req, res, request_json);
    }

    template <typename U = T>
    std::enable_if_t<!std::is_same<U, Patient>::value, void> SearchImpl(const crow::request &req, crow::response &res, const json &request_json)
    {
        (void)req;
        (void)request_json;
        RestHelper::failureResponse(res, "Search is NOT implemented for this entity");
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    std::enable_if_t<std::is_same<U, Patient>::value, void> GetVisitsImpl(const crow::request &req, crow::response &res,
                                                                          const std::unordered_map<std::string, std::string> &params)
    {
        (void)req;
        try
        {
            uint64_t id = std::stoull(params.at("id"));
            T        entity((typename T::Data_t(id)));
            Controller::GetVisits(res, entity);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    typename std::enable_if<!std::is_same<U, Patient>::value, void>::type GetVisitsImpl(const crow::request &req, crow::response &res,
                                                                                        const std::unordered_map<std::string, std::string> &params)
    {
        (void)req;
        (void)params;
        RestHelper::failureResponse(res, "GetVisits is NOT implemented for this entity");
    }

   public:
    explicit ClinicController() = default;

    virtual ~ClinicController() override = default;

    void Create(const crow::request &req, crow::response &res, const json &request_json) final
    {
        // here we call the actual implementation of the Create method
        CreateImpl(req, res, request_json);
    }

    void Read(const crow::request &req, crow::response &res, const json &request_json) final
    {
        // Read for all clinic entities
        EntityController<T>::Read(req, res, request_json);
    }
    void Update(const crow::request &req, crow::response &res, const json &request_json) final
    {
        // Update for all clinic entities
        EntityController<T>::Update(req, res, request_json);
    }

    void Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params) final
    {
        DeleteImpl(req, res, params);
    }

    void Search(const crow::request &req, crow::response &res, const json &request_json) final
    {
        // search only for Patient

        SearchImpl(req, res, request_json);
    }

    void GetVisits(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params) final
    {
        // GetVisits only for Patient
        GetVisitsImpl(req, res, params);
    }
};
