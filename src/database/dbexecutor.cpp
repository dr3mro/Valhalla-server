#include "database/dbexecutor.hpp"

#include <cstddef>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

namespace
{
    const Configurator::DbExecutorConfig& config() { return Store::getObject<Configurator>()->get<Configurator::DbExecutorConfig>(); }
}  // namespace

DbExecutor::DbExecutor() : BoundedExecutor(config().workers, config().queue_max, "DB executor"), io_fast_path_(config().io_fast_path) {}

DbExecutor::DbExecutor(std::size_t workers, std::size_t queue_max, bool io_fast_path) : BoundedExecutor(workers, queue_max, "DB executor"), io_fast_path_(io_fast_path) {}
