#pragma once

#include <cstddef>
#include <utility>

#include "concurrency/boundedexecutor.hpp"
#include "utils/global/callback.hpp"

// The pool that runs blocking database work (permission check, query, response) so a slow query never blocks a Drogon IO
// thread. Size: DB_WORKERS (default DB_MAX_CONN, so each worker holds at most one connection), queue: DB_QUEUE_MAX.
class DbExecutor : public BoundedExecutor
{
   public:
    DbExecutor();  // sizes come from the Configurator
    DbExecutor(std::size_t workers, std::size_t queue_max);
};

// Runs body(callback) on the DB workers; a full queue answers 503 and body never runs.
template <typename Body>
void runOnDbExecutor(DbExecutor& executor, CALLBACK_&& callback, Body&& body)
{
    runOnExecutor(executor, std::move(callback), "The server is busy, try again shortly.", std::forward<Body>(body));
}
