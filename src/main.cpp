#include <exception>
#include <iostream>
#include <memory>

#include "server/server.hpp"

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
    try
    {
        std::unique_ptr<Server> server = std::make_unique<Server>();
        return server->run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Server Error: " << exception.what() << '\n';
        return 1;
    }
}
