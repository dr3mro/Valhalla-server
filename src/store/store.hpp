#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
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
            std::scoped_lock lock(inventoryMutex);
            if (inventory.find(typeid(T)) != inventory.end())
            {
                throw std::runtime_error("Object type already registered." + std::string(typeid(T).name()));
            }
        }

        auto object = std::make_shared<T>(std::forward<Args>(args)...);

        {
            std::scoped_lock lock(inventoryMutex);
            inventory[typeid(T)] = object;
        }

        return object;
    }

    template <typename T, typename... Args>
    [[nodiscard("Warning: You should never discard the returned object")]] static std::shared_ptr<T> getObject(Args&&... args)
    {
        {
            std::scoped_lock lock(inventoryMutex);
            auto             object = inventory.find(typeid(T));
            if (object != inventory.end())
            {
                return std::static_pointer_cast<T>(object->second);
            }
        }

        // Construct outside the lock (construction may be expensive), but
        // re-check under the lock before publishing so concurrent first access
        // cannot install two different instances or throw "already registered".
        auto candidate = std::make_shared<T>(std::forward<Args>(args)...);

        {
            std::scoped_lock lock(inventoryMutex);
            auto             object = inventory.find(typeid(T));
            if (object != inventory.end())
            {
                return std::static_pointer_cast<T>(object->second);
            }
            inventory[typeid(T)] = candidate;
        }
        return candidate;
    }

    // Like getObject<T>(), but cheap enough for the request path: each thread keeps its own pointer and only goes back to the
    // Store (mutex + type lookup) after replaceObject() has changed something. Use it where an object is looked up for every
    // request (member initializers of objects built per request); long-lived objects can keep calling getObject<T>() once.
    template <typename T>
    [[nodiscard]] static std::shared_ptr<T> cachedObject()
    {
        thread_local std::shared_ptr<T> cached;
        thread_local uint64_t           seen = 0;
        const uint64_t                  now  = generation.load(std::memory_order_acquire);
        if (!cached || seen != now)
        {
            cached = getObject<T>();
            seen   = now;
        }
        return cached;
    }

    // Replace an already-registered singleton without throwing.
    // Used by test harnesses to inject fakes (see tests/support/fake_db.hpp).
    template <typename T>
    static void replaceObject(std::shared_ptr<T> obj)
    {
        std::scoped_lock lock(inventoryMutex);
        inventory[typeid(T)] = obj;
        generation.fetch_add(1, std::memory_order_release);  // makes every thread's cachedObject<T>() look again
    }

   private:
    static std::unordered_map<std::type_index, std::shared_ptr<void>> inventory;
    static std::mutex                                                 inventoryMutex;
    static std::atomic<uint64_t>                                      generation;  // bumped by replaceObject()
};
