#include "store.hpp"

// Define the static member variable
std::unordered_map<std::type_index, std::shared_ptr<void>> Store::inventory;
