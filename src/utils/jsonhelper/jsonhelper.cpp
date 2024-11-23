#include "jsonhelper.hpp"

Json::Value api::v2::JsonHelper::jsonify(const std::string& message)
{
    Json::Value object;
    object["Message"] = message;
    return object;
}
std::string api::v2::JsonHelper::stringify(const jsoncons::json& json)
{
    std::string json_string;
    json.dump_pretty(json_string);
    return json_string;
}
std::string api::v2::JsonHelper::stringify(const Json::Value& json) { return json.toStyledString(); }