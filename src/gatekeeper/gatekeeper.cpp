#include "gatekeeper.hpp"

void login(CALLBACK_&& callback, std::string_view data) {}
void logout(CALLBACK_&& callback, std::string_view data) {}

bool isAuthenticated(CALLBACK_&& callback, std::string_view data) { return false; }
bool hasPermission(CALLBACK_&& callback, std::string_view data) { return false; }

bool isDosAttack(CALLBACK_&& callback, std::string_view data) { return false; }