/**
 * @brief Represents the context of a permission-related operation, including the type of operation, the source
 * (client), and the destination.
 *
 * The `Context` struct encapsulates the necessary information to describe a permission-related operation, such as
 * creating, reading, updating, deleting, or searching permissions. It includes the type of operation, the source
 * (client) of the operation, and the destination (target) of the operation.
 *
 * The `Source` struct represents the client initiating the permission-related operation, which can be either a user or
 * a provider. The `Destination` struct represents the target of the operation, which can be one of several types (user,
 * provider, clinic, pharmacy, laboratory, radiology, patient, staff, or appointment).
 *
 * This context information is crucial for the permission management system to determine the appropriate access control
 * and authorization rules to apply.
 */
#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <jsoncons/basic_json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "utils/global/global.hpp"
#include "utils/message/message.hpp"
struct Context
{
    using Source = struct Source
    {
        enum class Type
        {
            USER,
            PROVIDER
        };

        Type     clientType;
        uint64_t clientID;

        static inline const std::unordered_map<std::string, Type> sourceTypeMap = {
            {"providers", Type::PROVIDER}, {"users", Type::USER}};

        explicit Source(const std::string& type, uint64_t id) : clientID(id)
        {
            try
            {
                auto it = sourceTypeMap.find(type);
                if (it != sourceTypeMap.end())
                {
                    clientType = it->second;
                }
                else
                {
                    throw std::runtime_error("Invalid client type");
                }
            }
            catch (const std::exception& e)
            {
                CRITICALMESSAGE
            }
        }
    };

    using Destination = struct Destination
    {
        enum class Type
        {
            USER,
            PROVIDER,
            CLINIC,
            PHARMACY,
            LABORATORY,
            RADIOLOGY,
            PATIENT,
            STAFF,
            APPOINTMENT
        };

        Type     destinationType;
        uint64_t destinationID;

        static inline const std::unordered_map<std::string, Type> destinationTypeMap = {{"users", Type::USER},
            {"providers", Type::PROVIDER}, {"clinics", Type::CLINIC}, {"pharmacies", Type::PHARMACY},
            {"laboratories", Type::LABORATORY}, {"radiologycenters", Type::RADIOLOGY}, {"patients", Type::PATIENT},
            {"staff", Type::STAFF}, {"appointments", Type::APPOINTMENT}};

        static inline const std::unordered_map<std::string, std::string> destination_id_map = {{"users", "id"},
            {"providers", "id"}, {"clinics", "id"}, {"pharmacies", "id"}, {"laboratories", "id"},
            {"radiologycenters", "id"}, {"patients", "clinic_id"}, {"staff", "entity_id"},
            {"appointments", "entity_id"}, {"visits", "clinic_id"}, {"visitDrugs", "clinic_id"},
            {"requests", "clinic_id"}, {"prescriptions", "clinic_id"}, {"paidservices", "clinic_id"},
            {"reports", "clinic_id"}, {"patientdrugs", "clinic_id"}, {"patienttests", "clinic_id"},
            {"health", "clinic_id"}, {"patientreports", "clinic_id"}, {"patientdrugs", "clinic_id"}};

        explicit Destination(const std::string& type, std::string_view body)
        {
            try
            {
                auto destenation_type_iterator = destinationTypeMap.find(type);
                if (destenation_type_iterator != destinationTypeMap.end())
                {
                    destinationType = destenation_type_iterator->second;
                }
                else
                {
                    throw std::runtime_error("Invalid destination type");
                    return;
                }

                std::optional<jsoncons::json> body_j = jsoncons::json::parse(body);
                if (!body_j.has_value())
                {
                    throw std::runtime_error("Invalid body");
                    return;
                }

                auto destenation_id_key_iterator = destination_id_map.find(type);
                if (destenation_id_key_iterator != destination_id_map.end())
                {
                    destinationID = body_j->at(destenation_id_key_iterator->second).as<uint64_t>();
                }
                else
                {
                    throw std::runtime_error("Invalid destination type");
                    return;
                }
            }
            catch (const std::exception& e)
            {
                CRITICALMESSAGE
            }
        }
    };

    enum class Type : uint8_t
    {
        NONE   = 0,       // 00000000
        READ   = 1 << 1,  // 00000010
        WRITE  = 1 << 2,  // 00000100
        DELETE = 1 << 3,  // 00001000
        ADMIN  = 1 << 4,  // 00010000
        OWNER  = 1 << 5,  // 00100000
        SUPER  = 1 << 6   // 01000000
    };

    friend Type operator|(Type a, Type b) { return static_cast<Type>(static_cast<int>(a) | static_cast<int>(b)); }

    // Overload & operator
    friend Type operator&(Type a, Type b) { return static_cast<Type>(static_cast<int>(a) & static_cast<int>(b)); }

    // Overload |= operator
    friend Type& operator|=(Type& a, Type b)
    {
        a = a | b;
        return a;
    }

    // Overload &= operator
    friend Type& operator&=(Type& a, Type b)
    {
        a = a & b;
        return a;
    }

    // Invert operator
    friend Type operator~(Type a) { return static_cast<Type>(~static_cast<int>(a)); }

    // Check if any flag is set
    explicit operator bool() const { return static_cast<int>(taskType) != 0; }

    explicit Context(const Type type, const Source& source, const Destination& destination)
        : taskType(type), taskSource(source), taskDestination(destination)
    {
    }
    Context()          = delete;
    virtual ~Context() = default;
    bool hasPower(const Type& check) const { return (taskType & check) != Type::NONE; }

    Type        taskType;
    Source      taskSource;
    Destination taskDestination;
};
