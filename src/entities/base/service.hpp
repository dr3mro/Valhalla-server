#pragma once

#include <jsoncons/json.hpp>

#include "entities/base/entity.hpp"

using json = jsoncons::json;

class Service : public Entity
{
   public:
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

    template <typename T>
    Service(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    Service(const std::string &tablename) : Entity(tablename) {};
    ~Service() override = default;
    std::optional<std::string> getSqlAddStaffStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto idata = std::any_cast<StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set(mjson, '{{payload,providers,Doctors}}', ((mjson->'payload'->'providers'->'Doctors') || "
                     "'\"{}\"')::jsonb) WHERE ID={} RETURNING id;",
                tablename, idata.nominee_id, idata.nominee_group, idata.entity_id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for add member " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    }

    std::optional<std::string> getSqlRemoveStaffStatement()
    {
        std::optional<std::string> query;
        try
        {
            auto idata = std::any_cast<StaffData>(getData());
            query      = fmt::format(
                "UPDATE {} SET mjson = jsonb_set("
                     "mjson, '{{payload,providers,Doctors}}', "
                     "(SELECT jsonb_agg(elem) FROM jsonb_array_elements(mjson->'payload'->'providers'->'Doctors') AS elem "
                     "WHERE elem <> '\"{}\"') "
                     ") WHERE ID={} RETURNING id;",
                tablename, idata.nominee_id, idata.entity_id);
        }
        catch (const std::exception &e)
        {
            std::cerr << "faild to create query for remove member " << tablename << e.what() << '\n';
            return std::nullopt;
        }
        return query;
    }

   private:
};
