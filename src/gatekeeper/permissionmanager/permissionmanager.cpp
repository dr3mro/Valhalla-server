#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include "gatekeeper/permissionmanager/permissions.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManager::hasPermission([[maybe_unused]] const Context& context) { return true; }

// TODO: Implement this