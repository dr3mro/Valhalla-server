
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
#include "entities/base/entity.hpp"
#include "entities/services/clinics/patient/patient.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>

class ServiceController : public EntityController<T>, public ServiceControllerBase
{
   public:
    explicit ServiceController() = default;

    ~ServiceController() override = default;

    // CRUDS
    void Create(const crow::request &req, crow::response &res, const json &request_json) final;
    void Read(const crow::request &req, crow::response &res, const json &request_json) final;
    void Update(const crow::request &req, crow::response &res, const json &request_json) final;
    void Delete(const crow::request &req, crow::response &res, const json &request_json) final;
    void Search(const crow::request &req, crow::response &res, const json &request_json) final;
    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    typename std::enable_if<std::is_same<U, Patient>::value, void>::type GetVisits(const crow::request &req, crow::response &res,
                                                                                   const jsoncons::json &request_json)
    {
        (void)req;
        try
        {
            typename T::PatientData patientData(request_json);
            T                       patient(patientData);
            Controller::GetVisits(res, patient);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }
};

template <typename T>
void ServiceController<T>::Create(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Create(req, res, request_json);
}
template <typename T>
void ServiceController<T>::Read(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Read(req, res, request_json);
}

template <typename T>
void ServiceController<T>::Update(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Update(req, res, request_json);
}

template <typename T>
void ServiceController<T>::Delete(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Delete(req, res, request_json);
}

template <typename T>
void ServiceController<T>::Search(const crow::request &req, crow::response &res, const json &request_json)
{
    EntityController<T>::Search(req, res, request_json);
}
