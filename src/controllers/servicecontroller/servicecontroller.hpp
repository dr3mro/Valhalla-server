
#pragma once

#include <crow.h>

#include <jsoncons/json.hpp>

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
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
            T entity((typename T::Data(request_json)));
            Controller::GetVisits(res, entity);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }
};

#include "controllers/servicecontroller/servicecontroller.tpp"