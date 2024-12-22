#pragma once
#include <json/value.h>

#include <jsoncons/json.hpp>
namespace api::v2
{
    class JsonHelper
    {
       public:
        JsonHelper()          = delete;
        virtual ~JsonHelper() = delete;

        JsonHelper(JsonHelper&&)      = delete;
        JsonHelper(const JsonHelper&) = delete;

        JsonHelper& operator=(JsonHelper&&)      = delete;
        JsonHelper& operator=(const JsonHelper&) = delete;

        static Json::Value jsonify(const std::string& message);
        static std::string stringify(const Json::Value& json);
        static std::string stringify(const jsoncons::json& json);
    };
}  // namespace api::v2
