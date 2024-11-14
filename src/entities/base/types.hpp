#pragma once
#include <fmt/format.h>

#include <cstddef>
#include <jsoncons/json.hpp>
#include <regex>
#include <utility>
#include <variant>
#include <vector>

#include "configurator/configurator.hpp"
#include "store/store.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"
using json = jsoncons::json;

#include "utils/resthelper/resthelper.hpp"
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
        virtual ~Entity_t() = default;
        S        data;
        uint64_t id;
    };

    using Create_t = struct Create_t : public Entity_t<jsoncons::json>
    {
        Create_t(const json &data, const uint64_t id) : Entity_t(data, id) {}
        ~Create_t() override = default;
    };

    using Read_t = struct Read_t : public Entity_t<const std::vector<std::string>>
    {
        Read_t(const std::vector<std::string> &_data, const uint64_t _id) : Entity_t(_data, _id) {}
        ~Read_t() override = default;
    };

    using Update_t = struct Update_t : public Entity_t<jsoncons::json>
    {
        Update_t(const json &_data, const uint64_t _id) : Entity_t(_data, _id) {}
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

        Search_t(const json &search_j, bool &success)
        {
            try
            {
                keyword   = search_j.at("keyword").as<std::string>();
                filter    = search_j.at("filter").as<std::string>();
                order_by  = search_j.at("order_by").as<std::string>();
                direction = search_j.at("direction").as<short>() == 0 ? "ASC" : "DESC";
                limit     = search_j.at("limit").as<size_t>();
                offset    = search_j.at("offset").as<size_t>();
            }
            catch (const std::exception &e)
            {
                success = false;
                throw std::runtime_error(std::string(e.what()));
            }
            success = true;
        }
    };

    struct ClientData
    {
       public:
        ClientData(const json &data, crow::response &res, bool &success)
        {
            for (const auto &item : data.object_range())
            {
                try
                {
                    std::optional<std::string> value = item.value().as<std::string>();
                    if (value.has_value() && !value->empty())
                    {
                        auto pattern_item =
                            std::find_if(validators.begin(), validators.end(), [&](const auto &validator) { return validator.first == item.key(); });

                        if (pattern_item != validators.end())
                        {
                            std::regex pattern(pattern_item->second);
                            if (!std::regex_match(value.value(), pattern))
                            {
                                throw std::runtime_error(fmt::format("Value({}) is invalid.", value.value(), item.key()));
                            }
                        }

                        if (item.key() == "password")
                        {
                            value = passwordCrypt->hashPassword(value.value());
                        }

                        db_data.push_back({item.key(), value.value()});
                    }
                }
                catch (const std::exception &e)
                {
                    RestHelper::failureResponse(res, e.what());
                    return;
                }
            }
            success = true;
        }
        const std::vector<std::pair<std::string, std::string>> &get_data() const { return db_data; }

       protected:
       private:
        std::shared_ptr<PasswordCrypt>                   passwordCrypt = Store::getObject<PasswordCrypt>();
        std::vector<std::pair<std::string, std::string>> db_data;

        const std::map<std::string, std::string> validators = {
            {"username", "^[a-z][a-z0-9_]*$"},
            {"password", "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[!@#$%^&*])[A-Za-z\\d!@#$%^&*]{8,}$"},
            {"phone", R"(^\+?(\d{1,3})?[-.\s]?(\(?\d{3}\)?)?[-.\s]?\d{3}[-.\s]?\d{4}$)"},
            {"email", R"((\w+)(\.\w+)*@(\w+)(\.\w+)+)"},
            {"dob", R"(^(0[1-9]|[12]\d|3[01])-(0[1-9]|1[0-2])-\d{4}$)"},
            {"gender", "^(male|female)$"},
        };
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

        StaffData(const json &json)
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
        bool toInviteJson(json &invite_json)
        {
            std::string encoded_invite_data = crow::utility::base64encode(invite_json.to_string(), invite_json.to_string().size());
            try
            {
                invite_json["subject"]  = fmt::format("Invite to {}", project_name);
                invite_json["template"] = "invite_staff_to_entity.txt";
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
        std::shared_ptr<Configurator>       cfg_         = Store::getObject<Configurator>();
        const Configurator::ServerConfig   &servercfg_   = cfg_->get<Configurator::ServerConfig>();
        const Configurator::FrontEndConfig &frontendcfg_ = cfg_->get<Configurator::FrontEndConfig>();
    };

    using EntityType =
        std::variant<Create_t, Read_t, Update_t, Delete_t, Data_t, Search_t, ClientData, LogoutData, SuspendData, Credentials, StaffData>;
};
