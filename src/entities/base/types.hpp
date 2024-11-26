#pragma once
#include <fmt/format.h>

#include <cppcodec/base64_rfc4648.hpp>
#include <cstddef>
#include <jsoncons/json.hpp>
#include <utility>
#include <variant>

#include "configurator/configurator.hpp"
#include "store/store.hpp"
#include "utils/global/httpcodes.hpp"
#include "utils/global/types.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
#include "utils/validator/validator.hpp"

class Types
{
   public:
    Types()          = default;
    virtual ~Types() = default;

    template <typename S>
    struct Entity_t
    {
       public:
        Entity_t(const S &_data, const uint64_t _id) : data(_data), id(_id) {}
        std::optional<uint64_t> get_id() const { return id; }
        const S                &get_data() const { return this->data; }
        virtual ~Entity_t() = default;

       private:
        const S                       data;
        const std::optional<uint64_t> id;
    };

    using Create_t = struct Create_t : public Entity_t<jsoncons::json>
    {
        Create_t(const jsoncons::json &_data, const uint64_t id) : Entity_t(_data, id) {}
        ~Create_t() override = default;
    };

    using Read_t = struct Read_t : public Entity_t<const std::unordered_set<std::string>>
    {
        Read_t(const std::unordered_set<std::string> &_data, const uint64_t _id) : Entity_t(_data, _id) {}
        ~Read_t() override = default;
    };

    using Update_t = struct Update_t : public Entity_t<jsoncons::json>
    {
        Update_t(const jsoncons::json &_data, const uint64_t _id) : Entity_t(_data, _id) {}
        ~Update_t() override = default;
    };

    using Delete_t = struct Delete_t : public Entity_t<char>
    {
        Delete_t(const uint64_t _id) : Entity_t('\0', _id) {}
        ~Delete_t() override = default;
    };

    using Data_t = struct Data_t : public Entity_t<char>
    {
        Data_t(const uint64_t _id) : Entity_t('\0', _id) {}
        ~Data_t() override = default;
    };

    using Search_t = struct Search_t
    {
        std::string keyword;
        std::string filter;
        std::string order_by;
        std::string direction;
        size_t      limit;
        size_t      offset;

        Search_t(const jsoncons::json &search_j, bool &success)
        {
            try
            {
                keyword   = search_j.at("keyword").as<std::string>();
                filter    = search_j.at("filter").as<std::string>();
                order_by  = search_j.at("order_by").as<std::string>();
                direction = search_j.at("direction").as<short>() == 0 ? "ASC" : "DESC";
                limit     = search_j.at("limit").as<size_t>();
                offset    = search_j.at("offset").as<size_t>();
                success   = validate(search_j);
            }
            catch (const std::exception &e)
            {
                success = false;
                throw std::runtime_error(std::string(e.what()));
            }
        }
        bool validate(const jsoncons::json &search_j)
        {
            std::unordered_set<std::string> keys = {"keyword", "filter", "order_by", "direction", "limit", "offset"};
            return std::ranges::all_of(keys, [&search_j](const std::string &key) { return search_j.find(key).has_value(); });
        }
    };

    struct ClientData_t
    {
       public:
        ClientData_t(std::string_view _data, const std::optional<uint64_t> _id, api::v2::Global::HttpError &error, bool &success) : id(_id)
        {
            try
            {
                data_j = jsoncons::json::parse(_data);
                if (!data_j.has_value())
                {
                    error = {.code = HttpStatus::BAD_REQUEST, .message = "Failed to parse body."};
                    Message::ErrorMessage(error.message);
                    success = false;
                    return;
                }
            }
            catch (const std::exception &e)
            {
                error   = {.code = HttpStatus::INTERNAL_SERVER_ERROR, .message = fmt::format("Failed while parsing client data: {}.", e.what())};
                success = false;
                Message::CriticalMessage(error.message);
                return;
            }

            success = true;
        }

        const std::optional<jsoncons::json> &get_data_json() const { return data_j; }
        std::optional<uint64_t>              get_id() const { return id; }

       protected:
        std::unordered_set<std::pair<std::string, std::string>> data_set;
        std::optional<jsoncons::json>                           data_j;
        std::optional<uint64_t>                                 id;

        bool hashPassword()
        {
            auto passwd_itr = std::find_if(data_set.begin(), data_set.end(), [&](const auto &item) { return item.first == "password"; });
            if (passwd_itr != data_set.end())
            {
                std::string                password_raw    = passwd_itr->second;
                std::optional<std::string> hashed_password = passwordCrypt->hashPassword(password_raw);
                if (!hashed_password.has_value())
                {
                    return false;
                }
                data_set.erase(passwd_itr);
                data_set.emplace("password", hashed_password.value());
                return true;
            }
            return false;
        }

       private:
        std::shared_ptr<PasswordCrypt> passwordCrypt = Store::getObject<PasswordCrypt>();
    };

    struct CreateClient_t : public ClientData_t
    {
       public:
        CreateClient_t(std::string_view _data, const std::string &tablename, api::v2::Global::HttpError &error, bool &success)
            : ClientData_t(_data, std::nullopt, error, success)
        {
            try
            {
                success = Validator::validateDatabaseCreateSchema(tablename, data_j, error);
                if (!success)
                {
                    Message::ErrorMessage(error.message);
                    return;
                }

                success = Validator::clientRegexValidation(data_j, error, data_set);
                if (!success)
                {
                    Message::ErrorMessage(error.message);
                    return;
                }
                hashPassword();
            }
            catch (const std::exception &e)
            {
                error   = {.code = HttpStatus::INTERNAL_SERVER_ERROR, .message = fmt::format("Failed while parsing client data: {}.", e.what())};
                success = false;
                Message::CriticalMessage(error.message);
                return;
            }

            success = true;
        }
        const std::unordered_set<std::pair<std::string, std::string>> &get_data_set() const { return data_set; }
    };

    struct UpdateClient_t : public ClientData_t
    {
       public:
        UpdateClient_t(std::string_view _data, const std::optional<uint64_t> _id, const std::string &tablename, api::v2::Global::HttpError &error,
                       bool &success, const std::unordered_set<std::string> &exclude)
            : ClientData_t(_data, _id, error, success)
        {
            try
            {
                success = Validator::validateDatabaseUpdateSchema(tablename, get_data_json(), error, exclude);
                if (!success)
                {
                    Message::ErrorMessage(error.message);
                    return;
                }

                success = Validator::clientRegexValidation(data_j, error, data_set);
                if (!success)
                {
                    Message::ErrorMessage(error.message);
                    return;
                }
            }
            catch (const std::exception &e)
            {
                error   = {.code = HttpStatus::INTERNAL_SERVER_ERROR, .message = fmt::format("Failed while parsing client data: {}.", e.what())};
                success = false;
                Message::CriticalMessage(error.message);
                return;
            }

            success = true;
        }
        const std::unordered_set<std::pair<std::string, std::string>> &get_data_set() const { return data_set; }
    };

    struct LogoutData
    {
        std::optional<std::string> token;
        LogoutData(const std::optional<std::string> &_token) { token = _token; }
    };

    struct SuspendData
    {
        uint64_t client_id;
        SuspendData(const uint64_t _client_id) { client_id = _client_id; }
    };
    struct Credentials
    {
        std::string username;  ///< The user's username.
        std::string password;  ///< The user's password.
    };
    struct StaffData
    {
        bool        parse_status = false;  // success or failure to parse the json
        uint64_t    nominee_id;
        uint64_t    entity_id;
        std::string role;
        std::string email;
        std::string nominee_group;
        std::string entity_name;
        std::string project_name;

        StaffData(const jsoncons::json &json)
        {
            try
            {
                nominee_id    = json.at("nominee_id").as<uint64_t>();
                entity_id     = json.at("entity_id").as<uint64_t>();
                role          = json.at("role").as<std::string>();
                email         = json.at("email").as<std::string>();
                nominee_group = json.at("nominee_group").as<std::string>();
                entity_name   = json.at("entity_name").as<std::string>();
                project_name  = servercfg_.name;
                parse_status  = true;
            }
            catch (const std::exception &e)
            {
                fmt::print("Error while parsing json: {}", std::string(e.what()));
                parse_status = false;
            }
        }

        bool toInviteJson(jsoncons::json &invite_json)
        {
            try
            {
                std::string encoded_invite_data = cppcodec::base64_rfc4648::encode(invite_json.to_string());
                invite_json["subject"]          = fmt::format("Invite to {}", project_name);
                invite_json["template"]         = "invite_staff_to_entity.txt";
                invite_json["link"] = fmt::format("{}:{}/{}/{}", frontendcfg_.host, frontendcfg_.port, frontendcfg_.invite_path, encoded_invite_data);
                invite_json["project_name"]  = project_name;
                invite_json["generate_body"] = "";
                return true;
            }
            catch (const std::exception &e)
            {
                fmt::print("Error while creating invite json: {}", std::string(e.what()));
                return false;
            }
        }
        StaffData() = default;

       private:
        const std::shared_ptr<Configurator> cfg_         = Store::getObject<Configurator>();
        const Configurator::ServerConfig   &servercfg_   = cfg_->get<Configurator::ServerConfig>();
        const Configurator::FrontEndConfig &frontendcfg_ = cfg_->get<Configurator::FrontEndConfig>();
    };

    using EntityType = std::variant<Create_t, Read_t, Update_t, Delete_t, Data_t, Search_t, CreateClient_t, UpdateClient_t, LogoutData, SuspendData,
                                    Credentials, StaffData>;
};
