#include "database/dbexecutor.hpp"

#include <cstddef>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

namespace
{
    const Configurator::DbExecutorConfig& config() { return Store::getObject<Configurator>()->get<Configurator::DbExecutorConfig>(); }
}  // namespace

DbExecutor::DbExecutor() : BoundedExecutor(config().workers, config().queue_max, "DB executor") {}

DbExecutor::DbExecutor(std::size_t workers, std::size_t queue_max) : BoundedExecutor(workers, queue_max, "DB executor") {}
