#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include "gatekeeper/permissionmanager/context.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManager::hasPermission(const Context& context)
{
    if (context.hasPower(Context::Type::SUPER))
    {
        return true;
    }

    if (context.hasPower(Context::Type::ADMIN))
    {
        return true;
    }

    if (context.hasPower(Context::Type::OWNER))
    {
        return true;
    }

    /* BLUEPRINTS
     * // [x] Check if SuperUser or Admin or Admin then return true
     * // [ ] If not, check if the user has the permission
     * // [ ] So we need to get the permission from destination ...
     * // [ ] we also need to cache the permission for later use ...
     */
    return true;
}