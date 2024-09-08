#include "server.hpp"

#include <fmt/color.h>
#include <fmt/core.h>

#include "server/extras/banner.hpp"

#ifndef GIT_TAG
#    define GIT_TAG "unknown"
#endif

Server::Server() : app(std::make_shared<APP>()), routes(std::make_shared<API_V1_Routes>(app)) {}

int Server::run()
{
    print_banner();

    try
    {
        app->loglevel(static_cast<crow::LogLevel>(config_.verbose_level))
            .use_compression(crow::compression::algorithm::GZIP)
            .port(config_.port)
            .multithreaded()
            .concurrency(config_.threads)
            .bindaddr(config_.host.data())
            .server_name(config_.name.data())
            .run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception caught in main: " << e.what() << std::endl;
        return EXIT_FAILURE;  // Exit with error code
    }

    return EXIT_SUCCESS;  // Exit with success code
}
void Server::print_banner()
{
    std::srand(std::time(0));
    // Select a random color
    int        num_colors   = sizeof(colors) / sizeof(colors[0]);
    fmt::color random_color = colors[std::rand() % num_colors];

    // Clean screen
    // std::cout << "\033[2J\033[1;1H" << std::endl;

    // Print Config
    PRINTLINE()
    PRINT("Configuration:", config_.desc, light_blue, yellow)
    PRINTLINE()
    configurator_->printValues();

    // Show ASCII Art
    PRINTLINE()
    PRINT_LOGO(random_color)
    PRINTLINE()
    PRINT(" - Version", GIT_TAG, light_green, yellow)
    PRINT(" - Port", config_.port, light_green, yellow)
    PRINT(" - Threads", config_.threads, light_green, yellow)
    PRINT(" - Database", fmt::format("{} {}", db_config_.max_conn, "connections"), light_green, yellow)
    PRINTLINE()
    std::cout << std::endl;
}
