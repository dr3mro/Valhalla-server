#pragma once

#include <cstddef>
#include <utility>

#include "concurrency/boundedexecutor.hpp"
#include "utils/global/callback.hpp"

// The pool for deliberately slow password work (hashing and verification), so it never runs on a Drogon IO thread.
// See BoundedExecutor for the queue and inline-mode rules.
class PasswordWorker : public BoundedExecutor
{
   public:
    PasswordWorker();  // sizes come from the Configurator (PASSWORD_WORKERS, PASSWORD_QUEUE_MAX)
    PasswordWorker(std::size_t workers, std::size_t queue_max);
};

// Runs body(callback) on the password worker; a full queue answers 503 and body never runs.
template <typename Body>
void runOnPasswordWorker(PasswordWorker& worker, CALLBACK_&& callback, Body&& body)
{
    runOnExecutor(worker, std::move(callback), "Too many password operations in progress, try again shortly.", std::forward<Body>(body));
}
