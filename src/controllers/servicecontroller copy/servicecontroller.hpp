
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
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

    // Only enable GetVisits if T is of type Patient
    template <typename U = T>
    typename std::enable_if<std::is_same<U, Patient>::value, void>::type GetVisits(const crow::request &req, crow::response &res,
                                                                                   const jsoncons::json &criteria)
    {
        (void)req;
        try
        {
            typename T::PatientData patientData(criteria);
            T                       patient(patientData);
            Controller::GetVisits(res, patient);
        }
        catch (const std::exception &e)
        {
            RestHelper::failureResponse(res, e.what());
        }
    }
};
