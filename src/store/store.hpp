#pragma once

#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

class Store
{
   public:
    Store()  = default;
    ~Store() = default;

    template <typename T, typename... Args>
    static void registerObject(Args &&...args)
    {
        inventory[typeid(T)] = std::make_shared<T>(std::forward<Args>(args)...);
    }
    template <typename T>
    static std::shared_ptr<T> getObject()
    {
        auto it = inventory.find(typeid(T));
        if (it != inventory.end())
        {
            return std::static_pointer_cast<T>(it->second);
        }
        throw std::runtime_error("Object not found");
    }

   private:
    static std::unordered_map<std::type_index, std::shared_ptr<void>> inventory;
};