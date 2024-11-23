#pragma once

#include <jsoncons/basic_json.hpp>

#include "utils/global/types.hpp"
class Validator
{
   public:
    Validator()          = default;
    virtual ~Validator() = default;
    static bool validateDatabaseSchema(const std::string &tablename, const jsoncons::json &data, api::v2::Global::HttpError &error,
                                       const std::unordered_set<std::string> &exclude);
};