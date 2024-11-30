#include "server/server.hpp"

int main(int /*argc*/, char** /*argv*/)
{
    Server server;
    server.run();
    return EXIT_SUCCESS;
}
