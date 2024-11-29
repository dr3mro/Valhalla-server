#pragma once

#include <cstdint>
#include <jsoncons/json.hpp>
#include <memory>

#include "entities/base/entity.hpp"
#include "entities/base/types.hpp"
#include "fmt/format.h"
#include "utils/global/global.hpp"
#include "utils/message/message.hpp"
#define USERNAME "username"

namespace api
{
    namespace v2
    {
        template <typename T>
        concept ClientData_t = std::is_same<T, Types::CreateClient_t>::value || std::is_same<T, Types::UpdateClient_t>::value ||
                               std::is_same<T, Types::SuspendData>::value || std::is_same<T, Types::Data_t>::value;

        class Client : public Entity
        {
           public:
            // Client(const std::string &tablename) : Entity(tablename) {};

            template <typename T>
            Client(const T &data, const std::string &tablename) : Entity(data, tablename)
            {
            }

            std::optional<std::string> getSqlCreateStatement() final
            {
                auto clientdata = std::get<Types::CreateClient_t>(getData());

                try
                {
                    std::vector<std::string> keys_arr;
                    std::vector<std::string> values_arr;

                    for (auto &it : clientdata.get_data_set())
                    {
                        keys_arr.push_back(it.first);
                        values_arr.push_back(it.second);
                    }

                    std::string columns = fmt::format("{}", fmt::join(keys_arr, ","));
                    std::string values  = fmt::format("'{}'", fmt::join(values_arr, "','"));

                    return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;", tablename, columns, values);
                }
                catch (const std::exception &e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql create statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }
                return std::nullopt;
            }

            std::optional<std::string> getSqlUpdateStatement() final
            {
                std::optional<std::string> query;

                try
                {
                    auto                    clientdata = std::get<Types::UpdateClient_t>(getData()).get_data_set();
                    std::optional<uint64_t> id         = std::get<Types::UpdateClient_t>(getData()).get_id();
                    if (!id.has_value())
                    {
                        Message::ErrorMessage(fmt::format("Failed to update client data. No id provided."));
                        return std::nullopt;
                    }
                    std::string update_column_values;

                    for (auto it = clientdata.begin(); it != clientdata.end(); ++it)
                    {
                        update_column_values.append(fmt::format(" {} = '{}' ", it->first, it->second));
                        if (std::next(it) != clientdata.end())
                        {
                            update_column_values.append(",");
                        }
                    }

                    query = fmt::format("UPDATE {} set {} WHERE id={} returning id;", tablename, update_column_values, id.value());
                }
                catch (const std::exception &e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql update statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }
                return query;
            }

            std::optional<std::string> getSqlToggleSuspendStatement(bool state)
            {
                std::optional<std::string> query;
                try
                {
                    Types::SuspendData suspenddata = std::get<Types::SuspendData>(getData());
                    query = fmt::format("UPDATE {} SET active={} where id={} returning id,active;", tablename, state ? "true" : "false",
                                        suspenddata.client_id);
                }
                catch (const std::exception &e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql suspend statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }

                return query;
            }

            std::optional<std::string> getSqlSuspendStatement() { return getSqlToggleSuspendStatement(false); }

            std::optional<std::string> getSqlActivateStatement() { return getSqlToggleSuspendStatement(true); }

            template <typename T>
            bool exists()
            {
                auto client_data = std::get<T>(getData()).get_data_set();
                auto it          = std::ranges::find_if(client_data, [&](const auto &item) { return item.first == USERNAME; });

                if (it != client_data.end())
                {
                    std::string username = it->second;

                    auto result = databaseController->checkItemExists(tablename, USERNAME, username);
                    return result.value_or(false);
                }
                return false;
            }
            virtual ~Client() override = default;
        };
    }  // namespace v2
}  // namespace api
