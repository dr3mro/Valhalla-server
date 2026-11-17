/**
 * @file main.cpp
 * @brief Process entry point: build the Server and translate uncaught
 * exceptions into a failure exit status.
 *
 * @details This file contains nothing but the program's main() function. It
 * creates a single Server object on the heap using a smart pointer, then hands
 * control to Server::run(). If anything throws a C++ exception during setup or
 * shutdown, it prints a short reason to standard error and exits with a failure
 * status.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is where the whole process begins. The operating system calls main()
 * first; main() creates the Server, and the Server in turn configures and runs
 * Drogon (the web framework) together with all the routes, filters and database
 * helpers. Without this file there would be no starting point, so nothing would
 * ever listen for HTTP requests. main() is also the last line of defence: it
 * turns an uncaught exception into a readable error message and a non-zero exit
 * code, which is what process supervisors and Docker expect when a program
 * fails.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII / smart pointers (std::unique_ptr):
 * https://en.cppreference.com/w/cpp/memory/unique_ptr
 * - Exceptions and try/catch:
 * https://en.cppreference.com/w/cpp/language/exceptions
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

// for EXIT_FAILURE: the conventional "something went wrong" exit code
#include <cstdlib>
// for std::exception, the base type of the standard exceptions we catch here
#include <exception>
// for std::cerr, the error-output stream used to report failures
#include <iostream>
// for std::unique_ptr and std::make_unique used to own the Server
#include <memory>

// the Server class whose run() method starts the whole application
#include "server/server.hpp"

/**
 * @brief Constructs the Server and transfers control to it.
 *
 * @details There is no command-line parsing: every setting is read from the
 * environment by Configurator while the Server is constructed. The unique_ptr
 * owns the Server for the whole process; because Server::run() blocks until the
 * event loops stop, the object survives long enough to finish its post-run
 * shutdown sequence.
 * @param[in] argc Unused; retained only for the standard signature.
 * @param[in] argv Unused; retained only for the standard signature.
 * @return The status returned by Server::run(), or EXIT_FAILURE if a
 * std::exception.
 * @see Server::run()
 */
// Program entry point; argc/argv are kept only because the
// C++ standard requires this shape.
int main([[maybe_unused]] int    argc,
         [[maybe_unused]] char** argv) {
  // Guard the entire startup and shutdown so a thrown exception cannot
  // crash the process silently.
  try {
    std::unique_ptr<Server> server =
        std::make_unique<Server>();  // Own the Server on the heap; a
                                     // unique_ptr frees it automatically at
                                     // scope exit.
    // Start the web server and pass its exit status back to
    // the OS; this call blocks until the event loops stop.
    // Reached only if an exception
    // escaped the try block above.
    return server->run();
  } catch (const std::exception& exception) {
    std::cerr << "Server Error: " << exception.what()
              << '\n';    // Print the reason to stderr, keeping errors
                          // separate from normal logs.
    return EXIT_FAILURE;  // Tell the OS or supervisor the process failed,
                          // instead of accidentally reporting success.
  } catch (...) {
    std::cerr << "Unknown Error: The Process Exited Unexpectedly\n";
    return EXIT_FAILURE;
  }
}
