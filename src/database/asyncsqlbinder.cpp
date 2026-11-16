#include "database/asyncsqlbinder.hpp"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/coroutine.h>

#include <cstddef>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

#include <pqxx/pqxx>

namespace asyncdb
{
    std::vector<std::optional<std::string>> toTextParams(const pqxx::params& params)
    {
        std::vector<std::optional<std::string>> result;
        const auto                              c = params.make_c_params(std::source_location::current());

        result.reserve(c.values.size());
        for (std::size_t i = 0; i < c.values.size(); ++i)
        {
            if (c.values[i] == nullptr)
            {
                result.emplace_back(std::nullopt);
            }
            else
            {
                result.emplace_back(std::string(c.values[i], static_cast<std::size_t>(c.lengths[i])));
            }
        }
        return result;
    }

    void bindParams(drogon::orm::internal::SqlBinder& binder, const pqxx::params& params)
    {
        const auto c = params.make_c_params(std::source_location::current());

        for (std::size_t i = 0; i < c.values.size(); ++i)
        {
            if (c.values[i] == nullptr)
            {
                binder << nullptr;
            }
            else if (c.formats[i] == 1)
            {
                auto buffer = std::make_shared<std::vector<char>>(c.values[i], c.values[i] + c.lengths[i]);
                binder << drogon::orm::RawParameter{buffer, buffer->data(), static_cast<int>(buffer->size()), 1};
            }
            else
            {
                binder << std::string(c.values[i], static_cast<std::size_t>(c.lengths[i]));
            }
        }
    }

    drogon::orm::internal::SqlAwaiter execSqlCoro(drogon::orm::DbClient& client, const std::string& sql, const pqxx::params& params)
    {
        auto binder = client << sql;
        bindParams(binder, params);
        return drogon::orm::internal::SqlAwaiter(std::move(binder));
    }
}  // namespace asyncdb
