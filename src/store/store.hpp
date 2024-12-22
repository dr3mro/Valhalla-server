#pragma once

#include <memory>
#include <mutex>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
class Store
{
   public:
    Store()          = default;
    virtual ~Store() = default;

    // Delete copy constructor and copy assignment operator
    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    // Default move constructor and move assignment operator
    Store(Store&&)            = default;
    Store& operator=(Store&&) = default;

    template <typename T, typename... Args>
    static std::shared_ptr<T> registerObject(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(inventoryMutex);
            if (inventory.find(typeid(T)) != inventory.end())
            {
                throw std::runtime_error("Object type already registered." + std::string(typeid(T).name()));
            }
        }

        auto object = std::make_shared<T>(std::forward<Args>(args)...);

        {
            std::lock_guard<std::mutex> lock(inventoryMutex);
            inventory[typeid(T)] = object;
        }

        return object;
    }

    template <typename T, typename... Args>
    [[nodiscard("Warning: You should never discard the returned object")]] static std::shared_ptr<T> getObject(Args&&... args)
    {
        {
            std::lock_guard<std::mutex> lock(inventoryMutex);
            auto                        object = inventory.find(typeid(T));
            if (object != inventory.end())
            {
                return std::static_pointer_cast<T>(object->second);
            }
        }
        return registerObject<T>(std::forward<Args>(args)...);
    }

   private:
    static std::unordered_map<std::type_index, std::shared_ptr<void>> inventory;
    static std::mutex                                                 inventoryMutex;
};
