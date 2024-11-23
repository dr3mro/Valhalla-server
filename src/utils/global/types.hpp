#pragma once
#include <string>
namespace api
{
    namespace v2
    {
        class Global
        {
           public:
            using HttpError = struct HttpError
            {
                int         code;
                std::string message;
            };
        };
        using ColumnInfo = struct ColumnInfo
        {
            std::string Name;
            std::string DataType;
            std::string Constraint;
            bool        isNullable;

            bool operator==(const ColumnInfo &other) const
            {
                return Name == other.Name && DataType == other.DataType && isNullable == other.isNullable;
            }
        };

    }  // namespace v2
}  // namespace api

namespace std
{
    template <>
    struct hash<api::v2::ColumnInfo>
    {
        size_t operator()(const api::v2::ColumnInfo &column) const
        {
            // Combine hashes of Name, DataType, and isNullable
            size_t h1 = hash<std::string>()(column.Name);
            size_t h2 = hash<std::string>()(column.DataType);
            size_t h3 = hash<bool>()(column.isNullable);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    template <>
    struct hash<std::pair<std::string, std::string>>
    {
        size_t operator()(const std::pair<std::string, std::string> &pair) const
        {
            // Combine hashes of Name, DataType, and isNullable
            size_t h1 = hash<std::string>()(pair.first);
            size_t h2 = hash<std::string>()(pair.second);
            return h1 ^ (h2 << 1);
        }
    };
}  // namespace std
