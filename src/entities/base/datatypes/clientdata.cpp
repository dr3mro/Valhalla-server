#include "clientdata.hpp"

#include "utils/resthelper/resthelper.hpp"

ClientData::ClientData(const json &_data, crow::response &res, bool &success)
{
    for (const auto &item : _data.object_range())
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
const std::vector<std::pair<std::string, std::string>> &ClientData::get_db_data() const { return db_data; }