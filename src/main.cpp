#include <memory>

#include "server/server.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    std::unique_ptr<Server> server = std::make_unique<Server>();
    return server->run();
}
