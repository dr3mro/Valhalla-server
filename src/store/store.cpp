#include "store.hpp"

// Static member definitions
std::unordered_map<std::type_index, std::shared_ptr<void>> Store::inventory;
std::mutex                                                 Store::inventoryMutex;
