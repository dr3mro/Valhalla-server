#include "server/server.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    Server server;
    return server.run();
}
