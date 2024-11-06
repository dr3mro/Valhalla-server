
#pragma once

#include "controllers/entitycontroller/entitycontroller.hpp"
#include "controllers/servicecontroller/servicecontrollerbase.hpp"
#include "utils/resthelper/resthelper.hpp"

using json = jsoncons::json;

template <typename T>
class ServiceController : public EntityController<T>, public ServiceControllerBase
{
   public:
    explicit ServiceController() = default;

    virtual ~ServiceController() override = default;

    // CRUDS
    void Create(const crow::request &req, crow::response &res, const json &request_json) final;
    void Read(const crow::request &req, crow::response &res, const json &request_json) final;
    void Update(const crow::request &req, crow::response &res, const json &request_json) final;
    void Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params) final;
    void Search(const crow::request &req, crow::response &res, const json &request_json) final;
};

#include "controllers/servicecontroller/servicecontroller.tpp"
