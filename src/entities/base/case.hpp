#pragma once
#include <memory>

#include "controllers/databasecontroller/databasecontroller.hpp"
#include "entities/base/entity.hpp"
#include "store/store.hpp"
class Case : public Entity
{
   public:
    template <typename T>
    Case(const T &data, const std::string &tablename) : Entity(data, tablename)
    {
    }
    Case(const std::string &tablename) : Entity(tablename) {};

    virtual ~Case() = default;

    bool check_id_exists()
    {
        uint64_t id = std::any_cast<Create_t>(getData()).id;

        auto result = databaseController->checkItemExists(tablename, "id", std::to_string(id));
        return result.value_or(false);
    }

   protected:
   private:
    std::shared_ptr<DatabaseController> databaseController = Store::getObject<DatabaseController>();
};
