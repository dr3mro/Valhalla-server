#include "objectfactory/objectfactory.hpp"
#include "server/server.hpp"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ObjectFactory factory;

    Server server;

    server.run();

    return EXIT_SUCCESS;
}
