#pragma once
#include <json/value.h>

#include <jsoncons/json.hpp>
namespace api
{
    namespace v2
    {

        class JsonHelper
        {
           public:
            JsonHelper()          = default;
            virtual ~JsonHelper() = default;
            static Json::Value jsonify(const std::string& message);
            static std::string stringify(const Json::Value& json);
            static std::string stringify(const jsoncons::json& json);
        };
    }  // namespace v2
}  // namespace api