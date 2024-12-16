#pragma once

#include <cstdint>

#include "entities/base/entity.hpp"
#include "utils/global/global.hpp"
class Case : public Entity
{
   public:
    static constexpr auto ORGNAME = "clinics";
    template <typename Data>
    Case(const Data& data, const std::string& tablename) : Entity(data, tablename)
    {
    }
    static constexpr auto getOrgName() { return ORGNAME; }
    virtual ~Case() = default;

   protected:
    static std::optional<std::string> getPermissionsQueryForCreatePatientImpl(const uint64_t id)
    {
        auto query = fmt::format(R"(
        SELECT
        owner_id,
        admin_id,
        staff
        FROM
        {}
        WHERE
        id = {};)",
            ORGNAME, id);
        // std::cout << query << std::endl;
        return query;
    }
    static std::optional<std::string> getPermissionsQueryForCreateImpl(const uint64_t id, const std::string& tablename)
    {
        // create and return the query
        auto query = fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
            ORGNAME, tablename, id);
        // std::cout << query << std::endl;
        return query;
    }

    static std::optional<std::string> getPermissionsQueryForReadImp(const uint64_t id, const std::string& tablename)
    {
        // create and return the query
        auto query = fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
            ORGNAME, tablename, id);
        // std::cout << query << std::endl;
        return query;
    }

    static std::optional<std::string> getPermissionsQueryForUpdateImpl(const uint64_t id, const std::string& tablename)
    {
        // create and return the query
        auto query = fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
            ORGNAME, tablename, id);
        // std::cout << query << std::endl;
        return query;
    }

    static std::optional<std::string> getPermissionsQueryForDeleteImp(const uint64_t id, const std::string& tablename)
    {
        // create and return the query
        auto query = fmt::format(R"(
        SELECT
        c.owner_id,
        c.admin_id,
        c.staff,
        p.id AS patient_id,
        p.clinic_id AS clinic_id
        FROM
        {} c
        LEFT JOIN {} p ON c.id = p.clinic_id
        WHERE
        p.id = {};)",
            ORGNAME, tablename, id);
        // std::cout << query << std::endl;
        return query;
    }
};
