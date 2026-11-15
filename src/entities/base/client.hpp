#pragma once

#include <fmt/core.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iterator>
#include <jsoncons/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

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
            Client(const Client&)            = delete;
            Client(Client&&)                 = delete;
            Client& operator=(const Client&) = delete;
            Client& operator=(Client&&)      = delete;
            // Client(const std::string &tablename) : Entity(tablename) {};

            template <typename T>
            Client(const T& data, const std::string& tablename) : Entity(data, tablename)
            {
            }

            std::optional<std::string> getSqlCreateStatement(pqxx::params& params) final
            {
                Binder binder(params);
                return buildClientCreateStatement(binder);
            }

            std::optional<std::string> getSqlUpdateStatement(pqxx::params& params) final
            {
                Binder binder(params);
                return buildClientUpdateStatement(binder);
            }

            std::optional<std::string> getSqlToggleSuspendStatement(bool state, pqxx::params& params)
            {
                try
                {
                    Types::SuspendData suspenddata = std::get<Types::SuspendData>(getData());
                    client_id                      = suspenddata.client_id;
                    Binder binder(params);
                    // Bound in a fixed order (argument evaluation order is unspecified).
                    const std::string state_ph = binder.bind(state);
                    const std::string id_ph    = binder.bind(client_id.value());
                    return fmt::format("UPDATE {} SET active={} WHERE id={} RETURNING id,active;", tablename, state_ph, id_ph);
                }
                catch (const std::exception& e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql suspend statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }
            }

            std::optional<std::string> getSqlSuspendStatement(pqxx::params& params) { return getSqlToggleSuspendStatement(false, params); }

            std::optional<std::string> getSqlActivateStatement(pqxx::params& params) { return getSqlToggleSuspendStatement(true, params); }

            std::optional<uint64_t> getClientId() const { return client_id; }

            template <typename T>
            bool exists()
            {
                auto client_data = std::get<T>(getData()).get_data_set();
                auto iterator    = std::ranges::find_if(client_data, [&](const auto& item) { return item.first == USERNAME; });

                if (iterator != client_data.end())
                {
                    std::string username       = iterator->second;
                    auto        result         = databaseController->checkItemExists(tablename, USERNAME, username);
                    return result.value_or(false);
                }
                return false;
            }
            ~Client() override = default;

           protected:
            // Statement builders (see Base::Binder).
            template <typename Binder>
            std::optional<std::string> buildClientCreateStatement(Binder& binder)
            {
                try
                {
                    auto clientdata = std::get<Types::CreateClient_t>(getData());

                    std::vector<std::string> keys_arr;
                    std::vector<std::string> values_arr;

                    for (const auto& item : clientdata.get_data_set())
                    {
                        keys_arr.push_back(item.first);
                        values_arr.push_back(binder.bind(item.second));
                    }

                    return fmt::format("INSERT INTO {} ({}) VALUES ({}) RETURNING id;", tablename, fmt::join(keys_arr, ","), fmt::join(values_arr, ","));
                }
                catch (const std::exception& e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql create statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }
            }

            template <typename Binder>
            std::optional<std::string> buildClientUpdateStatement(Binder& binder)
            {
                try
                {
                    auto clientdata = std::get<Types::UpdateClient_t>(getData()).get_data_set();
                    client_id       = std::get<Types::UpdateClient_t>(getData()).get_id();
                    if (!client_id.has_value())
                    {
                        Message::ErrorMessage(fmt::format("Failed to update client data. No id provided."));
                        return std::nullopt;
                    }

                    std::vector<std::string> set_clauses;
                    for (auto it = clientdata.begin(); it != clientdata.end(); ++it)
                    {
                        set_clauses.push_back(fmt::format("{} = {}", it->first, binder.bind(it->second)));
                    }

                    return fmt::format("UPDATE {} SET {} WHERE id={} RETURNING id;", tablename, fmt::join(set_clauses, ", "), binder.bind(client_id.value()));
                }
                catch (const std::exception& e)
                {
                    Message::ErrorMessage(fmt::format("Failed to create Sql update statement for table {}.", tablename));
                    Message::CriticalMessage(e.what());
                    return std::nullopt;
                }
            }

            std::optional<uint64_t> client_id; /*NOLINT*/
        };
    }  // namespace v2
}  // namespace api
