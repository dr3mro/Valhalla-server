#include "tokenmanager.hpp"

using namespace api::v2;

std::optional<std::string> TokenManager::generateToken(std::string_view data) { return std::nullopt; }
bool                       TokenManager::isTokenValid(std::string_view token) { return false; }