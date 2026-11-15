#include "gatekeeper/passwordcrypt/passwordworker.hpp"

#include <cstddef>

#include "configurator/configurator.hpp"
#include "store/store.hpp"

namespace
{
    const Configurator::PasswordWorkerConfig& config() { return Store::getObject<Configurator>()->get<Configurator::PasswordWorkerConfig>(); }
}  // namespace

PasswordWorker::PasswordWorker() : BoundedExecutor(config().workers, config().queue_max, "Password worker") {}

PasswordWorker::PasswordWorker(std::size_t workers, std::size_t queue_max) : BoundedExecutor(workers, queue_max, "Password worker") {}
