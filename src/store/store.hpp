/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file store.hpp
 * @brief Process-wide service locator keyed by C++ type.
 * @details This file declares Store, the one place in the server that keeps a
 * single shared object for each C++ type. Callers ask for an object by its type
 * (for example "the database client"), and Store either hands back the object
 * it already made or builds it the first time. It also offers a cheap
 * per-thread cache for busy request code and a way to swap an object for a fake
 * one during tests.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Store is the server's service locator. Controllers and other long-lived
 * objects call Store::getObject<T>() or Store::cachedObject<T>() to get shared
 * helpers such as the database client instead of building their own. Because
 * the object is built only once, expensive resources are never duplicated.
 * Tests call replaceObject<T>() to install a fake without changing production
 * code. Without Store, every caller would have to pass the same helper around
 * by hand and could easily end up with a different copy.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Service locator: one well-known place you ask for shared objects, by type
 * instead of by name. https://en.wikipedia.org/wiki/Service_locator_pattern
 * - thread_local: storage that has one separate copy per running thread.
 * https://en.cppreference.com/w/cpp/language/storage_duration
 * - Atomic memory order: rules saying when one thread's writes become visible
 * to another thread. https://en.cppreference.com/w/cpp/atomic/memory_order
 * - RAII: using a small local object to lock a mutex and unlock it
 * automatically at the end of a scope.
 * https://en.cppreference.com/w/cpp/language/raii
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Compiler-enforced header guard: include this file at most once
              // per translation unit.

// std::atomic: a counter that several threads can safely change at the same
// time.
#include <atomic>
// std::uint64_t: the fixed-width integer used by the generation counter.
#include <cstdint>
// std::shared_ptr / std::make_shared: shared ownership of the stored objects.
#include <memory>
// std::mutex / std::scoped_lock: protect the shared registry from concurrent
// access.
#include <mutex>
// std::runtime_error: thrown when a type is registered a second time.
#include <stdexcept>
// std::string: used to build the "already registered" error message.
#include <string>
// std::type_index: a hashable id for a C++ type, used as the map key.
#include <typeindex>
// std::unordered_map: the type-to-object registry itself.
#include <unordered_map>

/**
 * @class Store
 * @brief Process-wide service locator that maps a type to one shared instance.
 *
 * @details The class has no non-static state; the actual registry is a set of
 * file-scope static members defined in store.cpp. Access is guarded by a single
 * mutex, and a separate atomic generation counter lets per-thread caches notice
 * replacements. Objects are owned through shared_ptr<void> and recovered by
 * static_pointer_cast using typeid(T) as the key, so a lookup by type resolves
 * to the concrete pointer stored for that type.
 *
 *          In plain words: a "singleton" is one single object shared by the
 * entire program. You never create a Store object; you just call its static
 * functions. A shared_ptr is a smart pointer that keeps its object alive for as
 * long as anyone still holds it, then destroys it automatically when the last
 * holder lets go, so no manual delete is needed. typeid(T) is a small tag that
 * identifies a C++ type while the program runs, which lets the map be keyed by
 * type instead of by a name. A mutex is a lock that only one thread may hold at
 * a time, which stops two threads from changing the map at once and corrupting
 * it. static_pointer_cast simply relabels the generic stored pointer as a T
 * pointer; it performs no runtime type check.
 * @note registerObject refuses duplicates; getObject lazily constructs on first
 * use; replaceObject overwrites without throwing and is intended for injecting
 * test doubles. ("Lazily" means the object is not built until somebody actually
 * asks for it; a "test double" is a fake object used in place of the real one
 * during tests.)
 * @see tests/support/fake_db.hpp
 */
class Store
{
 public:
  /**
   * Default construction; all state is static, so a Store object holds
   * nothing of its own.
   */
  Store() = default;
  /**
   * Virtual destructor; no owned resources to release (virtual so derived
   * classes clean up correctly).
   */
  virtual ~Store() = default;

  // Delete copy constructor and copy assignment operator
  /**
   * Copy construction is disabled; Store is a pure namespace-like facade over
   * static state.
   */
  Store(const Store&) = delete;
  /**
   * Copy assignment is disabled; copying would not copy any state anyway.
   */
  Store& operator=(const Store&) = delete;

  // Default move constructor and move assignment operator
  /**
   * Move construction is defaulted; there is no data to move.
   */
  Store(Store&&) = default;
  /**
   * Move assignment is defaulted; there is no data to move.
   */
  Store& operator=(Store&&) = default;

  /**
   * @brief Explicitly creates and registers a singleton, rejecting a type
   * that is already registered.
   *
   * @details The duplicate check and the insertion use separate lock scopes
   * so T's constructor runs without the mutex held. That keeps arbitrary
   * constructor code, which may re-enter Store, from deadlocking on the
   *          inventory lock. ("Deadlock" means two operations waiting on each
   * other forever.)
   * @tparam T Type to register; used as the inventory key.
   * @tparam Args Constructor argument types for T.
   * @param[in] args Arguments forwarded to T's constructor (passed on
   * unchanged).
   * @return A shared_ptr owning the newly created object, so the caller can
   * use it too.
   * @throws std::runtime_error if an object of type T is already registered.
   */
  template <typename T, typename... Args>
  static std::shared_ptr<T> registerObject(Args&&... args)
  {
    {
      std::scoped_lock lock(inventoryMutex);  // lock only for the check, then
                                              // release at the closing brace
      if (inventory.find(typeid(T)) !=
          inventory.end())  // end() means "not found", so anything else
                            // means a duplicate
      {
        throw std::runtime_error(
            "Object type already registered." +
            std::string(typeid(T).name()));  // refuse: there may be
                                             // only one per type
      }
    }

    auto object = std::make_shared<T>(
        std::forward<Args>(args)...);  // build outside the lock: constructors
                                       // may be slow or call back into Store

    {
      std::scoped_lock lock(inventoryMutex);  // lock again briefly, now only to
                                              // publish the finished object
      inventory[typeid(T)] = object;  // operator[] inserts the new entry
                                      // under this type's key
    }

    return object;  // caller gets shared ownership; the registry keeps its
                    // own copy alive too
  }

  /**
   * @brief Returns the singleton for T, constructing it on first use.
   *
   * @details The fast path looks up the type under the mutex and returns the
   * existing instance. On a miss the object is constructed outside the lock
   * (see the inline comment below) and then re-checked under the lock before
   *          being published, so concurrent first calls cannot install two
   * different instances. Constructor arguments are therefore only used on the
   * first successful construction and ignored on later calls.
   * @tparam T Type to look up or create; its type_index is the inventory key.
   * @tparam Args Constructor argument types for T.
   * @param[in] args Arguments forwarded to T's constructor when a new object
   * is needed.
   * @return A shared_ptr to the registered singleton; ownership is shared
   * with all other callers and with the Store.
   * @note Marked nodiscard because callers are expected to use the shared
   * instance; the Store itself retains a strong reference in the inventory.
   * ("Strong reference" means a shared_ptr that keeps the object alive.)
   * @see registerObject(), cachedObject(), replaceObject()
   */
  template <typename T, typename... Args>
  [[nodiscard(
      "Warning: You should never discard the returned object")]] static std::
      shared_ptr<T>
      getObject(Args&&... args)
  {
    {
      std::scoped_lock lock(inventoryMutex);  // hold the lock only while
                                              // searching the shared map
      auto             object = inventory.find(
          typeid(T));  // find gives an iterator pointing at the entry, or end()
      if (object !=
          inventory.end())  // found: somebody already registered this type
      {
        return std::static_pointer_cast<T>(
            object->second);  // relabel the stored void pointer as T
                              // and return it
      }
    }

    // Construct outside the lock (construction may be expensive), but
    // re-check under the lock before publishing so concurrent first access
    // cannot install two different instances or throw "already registered".
    auto candidate = std::make_shared<T>(
        std::forward<Args>(args)...);  // build a possible new instance
                                       // without blocking other threads

    {
      std::scoped_lock lock(inventoryMutex);  // re-check while locked: another
                                              // thread may have won the race
      auto object = inventory.find(typeid(T));  // look for the type again
      if (object != inventory.end())  // another thread published one first
      {
        return std::static_pointer_cast<T>(
            object->second);  // return their instance; our candidate is
                              // discarded
      }
      inventory[typeid(T)] = candidate;  // no one else did, so publish ours
    }
    return candidate;  // return the object we created; the registry also
                       // holds a copy
  }

  /**
   * @brief Thread-local cached lookup of T, refreshed only when the Store
   * generation changes.
   *
   * @details Each thread keeps its own shared_ptr and the generation value it
   * last saw. The generation is loaded with acquire semantics, pairing with
   * the release fetch_add in replaceObject(), so a thread that observes a new
   *          generation also observes the replacement. A mismatch or an empty
   * cache triggers a full getObject<T>() lookup; otherwise the cached pointer
   * is returned without touching the mutex.
   *
   *          In plain words: looking in the registry needs a lock, which is
   * too slow to do for every web request. So each thread remembers the
   * pointer it got last time. "Acquire" and "release" are a pair of promises:
   * after replaceObject() publishes a new pointer with "release", any thread
   * that reads the new number with "acquire" is also guaranteed to see that
   * new pointer, not a stale one. This is cheap because a normal read of the
   * counter is enough most of the time; the lock is only used when something
   * actually changed.
   * @tparam T Type to look up; on first use it must be constructible by
   * getObject<T>() with no arguments.
   * @return A shared_ptr to the current singleton for T.
   * @note The returned pointer is nodiscard; callers should use it instead of
   * re-fetching through getObject().
   * @see getObject(), replaceObject()
   */
  // Like getObject<T>(), but cheap enough for the request path: each thread
  // keeps its own pointer and only goes back to the Store (mutex + type
  // lookup) after replaceObject() has changed something. Use it where an
  // object is looked up for every request (member initializers of objects
  // built per request); long-lived objects can keep calling getObject<T>()
  // once.
  template <typename T>
  [[nodiscard]] static std::shared_ptr<T> cachedObject()
  {
    thread_local std::shared_ptr<T>
        cached;  // one copy per thread, so reading it needs no lock at all
    thread_local uint64_t seen = 0;  // the generation this thread last saw;
                                     // 0 means "never checked yet"
    const uint64_t        now  = generation.load(
        std::memory_order_acquire);  // acquire: pairs with release in
                                     // replaceObject()
    if (!cached || seen != now)      // empty cache, or the registry was changed
                                     // since last time
    {
      cached = getObject<T>();  // pay for the full locked lookup and
                                // remember the answer
      seen   = now;             // mark our cache as up to date with the current
                                // generation
    }
    return cached;  // fast path: hand back this thread's own pointer
                    // without touching the mutex
  }

  /**
   * @brief Overwrites the singleton registered for T and invalidates every
   * thread-local cache.
   *
   * @details Uses operator[] on the inventory, so it works whether or not T
   * was registered before. The generation is incremented with release
   * semantics so threads that acquire it in cachedObject() see the new
   * pointer and re-fetch from the Store. ("Invalidates" means it makes every
   * thread's saved pointer out of date.)
   * @tparam T Type whose registered instance is replaced.
   * @param[in] obj New instance to register; may be null, in which case the
   * key maps to an empty shared_ptr.
   * @note Does not throw: this is intended for test harnesses injecting
   * fakes.
   * @see cachedObject()
   */
  // Replace an already-registered singleton without throwing.
  // Used by test harnesses to inject fakes (see tests/support/fake_db.hpp).
  template <typename T>
  static void replaceObject(std::shared_ptr<T> obj)
  {
    std::scoped_lock lock(inventoryMutex);  // only one thread may change
                                            // the registry at a time
    inventory[typeid(T)] =
        obj;  // overwrite, or insert if there was no entry before
    generation
        .fetch_add(1,
                   std::memory_order_release);  // makes every thread's
                                                // cachedObject<T>() look again;
                                                // release publishes the new
                                                // pointer
  }

 private:
  /**
   * Type-to-instance registry; typeid(T) is the key and the value is
   * type-erased until a caller casts it back.
   */
  static std::unordered_map<std::type_index, std::shared_ptr<void>>
      inventory;  // the actual "type -> object" table
  /**
   * Guards every read and write of inventory.
   */
  static std::mutex
      inventoryMutex;  // blocks other threads while the map is being used
  /**
   * Invalidation counter read by cachedObject() and bumped by
   * replaceObject().
   */
  static std::atomic<uint64_t>
      generation;  // bumped by replaceObject(); a changed value tells caches
                   // to refresh
};
