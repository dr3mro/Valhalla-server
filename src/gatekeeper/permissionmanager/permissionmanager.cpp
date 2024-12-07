#include "gatekeeper/permissionmanager/permissionmanager.hpp"

#include "gatekeeper/permissionmanager/context.hpp"
#include "gatekeeper/permissionmanager/permissions.hpp"

using PowerLevel = Permissions::PowerLevel;

using namespace api::v2;
bool PermissionManager::hasPermission(const Context& context)
{
    Context::Type taskType = context.taskType;
    if ((taskType & Context::Type::SUPER) != Context::Type::NONE)
    {
        return true;
    }

    if ((taskType & Context::Type::ADMIN) != Context::Type::NONE)
    {
        return true;
    }

    if ((taskType & Context::Type::OWNER) != Context::Type::NONE)
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