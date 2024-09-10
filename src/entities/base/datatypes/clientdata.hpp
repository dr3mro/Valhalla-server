#pragma once

#include <jsoncons/json.hpp>
#include <regex>
#include <string>
#include <vector>

#include "crow/http_response.h"
#include "fmt/format.h"
#include "store/store.hpp"
#include "utils/passwordcrypt/passwordcrypt.hpp"

using json = jsoncons::json;

class ClientData
{
   public:
    explicit ClientData(const json &_data, crow::response &_res, bool &success);
    const std::vector<std::pair<std::string, std::string>> &get_db_data() const;

   private:
    std::vector<std::pair<std::string, std::string>> db_data;
    std::shared_ptr<PasswordCrypt>                   passwordCrypt = Store::getObject<PasswordCrypt>();
    const std::map<std::string, std::string>         validators    = {
        {"username", "^[a-z][a-z0-9_]*$"},
        {"password", "^(?=.*[a-z])(?=.*[A-Z])(?=.*\\d)(?=.*[!@#$%^&*])[A-Za-z\\d!@#$%^&*]{8,}$"},
        {"fullname", "^[A-Z][a-zA-Z]*([ '-][A-Z][a-zA-Z]*)*$"},
        {"phone", R"(^\+?(\d{1,3})?[-.\s]?(\(?\d{3}\)?)?[-.\s]?\d{3}[-.\s]?\d{4}$)"},
        {"email", R"((\w+)(\.\w+)*@(\w+)(\.\w+)+)"},
        {"dob", R"(^(0[1-9]|[12]\d|3[01])-(0[1-9]|1[0-2])-\d{4}$)"},
        {"gender", "^(male|female)$"},
    };
};
