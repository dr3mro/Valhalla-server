/**
 * @file store.cpp
 * @brief Defines the Store's single shared registry, lock and generation
 * counter.
 * @details This file contains no functions. It only gives real storage to the
 * three static variables that store.hpp declares: a map from a C++ type to its
 * one shared object, a mutex protecting that map, and a counter used to refresh
 * per-thread caches. The counter starts at 1 so that a brand-new thread, whose
 * remembered value starts at 0, always performs a lookup the first time it asks
 * for an object.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * These few definitions are the physical storage behind the service locator.
 * Every call to Store::getObject(), Store::cachedObject() or
 * Store::replaceObject() reads or changes these variables. Without this file
 * the header would only declare names and the linker would report "undefined
 * reference" errors. Tests also depend on the counter starting at 1, so a
 * freshly installed fake is noticed on the very first cached lookup.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - None difficult here; this file only provides real storage (a "definition")
 * for names declared in the header.
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// the class declaration whose static members are defined below
#include "store.hpp"

#include <atomic>   // std::atomic: the generation counter's type.
#include <cstdint>  // std::uint64_t: the counter's fixed-width integer type.
// std::shared_ptr: the map stores objects through shared ownership.
#include <memory>
#include <mutex>          // std::mutex: the type of the registry lock.
#include <typeindex>      // std::type_index: the type used as the map key.
#include <unordered_map>  // std::unordered_map: the type of the registry map.

// Static member definitions
/**
 * Type-keyed singleton registry shared by every translation unit.
 */
std::unordered_map<std::type_index, std::shared_ptr<void>>
    Store::inventory;  // empty at start; entries are added on first use
/**
 * Serialises access to the registry.
 */
std::mutex Store::inventoryMutex;  // only one thread may read or edit inventory
                                   // at a time
/**
 * Starts at 1 so a fresh thread's cached "seen" value of 0 is always stale on
 * its first cachedObject() call.
 */
std::atomic<uint64_t> Store::generation{
    1};  // bumped by replaceObject() to invalidate thread-local caches
