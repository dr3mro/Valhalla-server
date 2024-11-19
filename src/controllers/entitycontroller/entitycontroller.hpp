
#pragma once
#include <fmt/format.h>

#include <jsoncons/json.hpp>

#include "controllers/base/controller/controller.hpp"
#include "controllers/entitycontroller/entitycontrollerbase.hpp"
#include "entities/base/client.hpp"
#include "entities/base/entity.hpp"
#include "utils/helper/helper.hpp"

using json = jsoncons::json;

template <typename T>
class EntityController : public Controller, public EntityControllerBase
{
   public:
    EntityController()          = default;
    virtual ~EntityController() = default;
    // CRUDS
    void Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data) override;
    // void Read(const crow::request &req, crow::response &res, const json &request_json) override;
    // void Update(const crow::request &req, crow::response &res, const json &request_json) override;
    // void Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params) override;
    // void Search(const crow::request &req, crow::response &res, const json &request_json) override;

   protected:
};

template <typename T>
void EntityController<T>::Create(std::function<void(const drogon::HttpResponsePtr &)> &&callback, std::string_view data)
{
    try
    {
        auto next_id = this->template getNextID<T>();
        if (!next_id.has_value())
        {
            Helper::errorResponse(drogon::k406NotAcceptable, "Failed to generate next ID", callback);
            return;
        }

        T entity((Types::Create_t(data, next_id.value())));
        Controller::Create(entity, callback);
    }
    catch (const std::exception &e)
    {
        Helper::failureResponse(e.what(), callback);
    }
}

// template <typename T>
// void EntityController<T>::Read(const crow::request &req, crow::response &res, const json &request_json)
// {
//     (void)req;
//     json response;
//     try
//     {
//         uint64_t                 id     = request_json.at("id").as<uint64_t>();
//         std::vector<std::string> schema = request_json.at("schema").as<std::vector<std::string>>();

//         T entity((Types::Read_t(schema, id)));
//         Controller::Read(res, entity);
//     }
//     catch (const std::exception &e)
//     {
//         RestHelper::failureResponse(res, e.what());
//     }
// }

// template <typename T>
// void EntityController<T>::Update(const crow::request &req, crow::response &res, const json &request_json)
// {
//     (void)req;
//     try
//     {
//         json                    data_j(request_json);
//         std::optional<uint64_t> id = data_j.find("id")->value().as<uint64_t>();
//         if (!id.has_value())
//         {
//             RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
//             return;
//         }

//         T entity((Types::Update_t(data_j, id.value())));
//         Controller::Update(res, entity);
//     }
//     catch (const std::exception &e)
//     {
//         RestHelper::failureResponse(res, e.what());
//     }
// }

// template <typename T>
// void EntityController<T>::Delete(const crow::request &req, crow::response &res, const std::unordered_map<std::string, std::string> &params)
// {
//     (void)req;
//     try
//     {
//         auto it = params.find("id");
//         if (it == params.end())
//         {
//             RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "No id provided");
//             return;
//         }

//         std::optional<uint64_t> id = std::stoull(it->second);
//         if (!id.has_value())
//         {
//             RestHelper::errorResponse(res, crow::status::NOT_ACCEPTABLE, "Invalid id provided");
//             return;
//         }

//         T entity(Types::Delete_t(id.value()));
//         Controller::Delete(res, entity);
//     }
//     catch (const std::exception &e)
//     {
//         RestHelper::failureResponse(res, e.what());
//     }
// }

// template <typename T>
// void EntityController<T>::Search(const crow::request &req, crow::response &res, const json &request_json)
// {
//     (void)req;
//     json response;
//     try
//     {
//         bool success = false;
//         T    entity((Types::Search_t(request_json, success)));

//         if (success)
//         {
//             Controller::Search(res, entity);
//         }
//     }
//     catch (const std::exception &e)
//     {
//         RestHelper::failureResponse(res, e.what());
//     }
// }
