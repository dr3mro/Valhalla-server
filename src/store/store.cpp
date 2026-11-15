#include "store.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

// Static member definitions
std::unordered_map<std::type_index, std::shared_ptr<void>> Store::inventory;
std::mutex                                                 Store::inventoryMutex;
std::atomic<uint64_t>                                      Store::generation{1};
