
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
    void Create(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void Read(const crow::request &req, crow::response &res, const jsoncons::json &criteria) override;
    void Update(const crow::request &req, crow::response &res, const jsoncons::json &body) override;
    void Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json) override;
    void Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json) override;
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

template <typename T>
void ServiceController<T>::Create(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        auto nextID = this->getNextID();
        if (!nextID.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Failed to generate next ID");
            return;
        }
        Entity::CreateData createData(body, nextID.value());

        T service(createData);
        Controller::Create(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void ServiceController<T>::Read(const crow::request &req, crow::response &res, const json &criteria)
{
    (void)req;
    json response;
    try
    {
        uint64_t                 id   = criteria.at("id").as<uint64_t>();
        std::vector<std::string> data = criteria.at("schema").as<std::vector<std::string>>();

        Entity::ReadData readData(data, id);
        T                service(readData);
        Controller::Read(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void ServiceController<T>::Update(const crow::request &req, crow::response &res, const jsoncons::json &body)
{
    (void)req;
    json response;
    try
    {
        json                    data(body);
        std::optional<uint64_t> id = data.find("id")->value().as<uint64_t>();
        if (!id.has_value())
        {
            RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
            return;
        }
        Entity::UpdateData updateData(data, id.value());
        T                  service(updateData);
        Controller::Update(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void ServiceController<T>::Delete(const crow::request &req, crow::response &res, const jsoncons::json &delete_json)
{
    (void)req;
    try
    {
        Entity::DeleteData deleteData(delete_json);
        T                  service(deleteData);
        Controller::Delete(res, service);
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}

template <typename T>
void ServiceController<T>::Search(const crow::request &req, crow::response &res, const jsoncons::json &search_json)
{
    (void)req;
    json response;
    try
    {
        bool               success = false;
        Entity::SearchData searchData(search_json, success);
        if (success)
        {
            T service(searchData);
            Controller::Search(res, service);
        }
    }
    catch (const std::exception &e)
    {
        RestHelper::failureResponse(res, e.what());
    }
}
