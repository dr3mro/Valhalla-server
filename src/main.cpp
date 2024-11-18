#include "objectfactory/objectfactory.hpp"
#include "server/server2.hpp"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ObjectFactory factory;

    Server2 server;

    server.run();

    return EXIT_SUCCESS;
}
