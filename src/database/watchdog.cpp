#include "database/watchdog.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
#include "database/databasehandler.hpp"
#include "store/store.hpp"
#include "utils/message/message.hpp"

WatchDog::WatchDog() : databaseConnectionPool(Store::getObject<DatabaseConnectionPool>())
{
    if (monitor_thread.joinable())
    {
        throw std::logic_error("WatchDog thread already running!");
    }

    // Start the monitoring thread
    monitor_thread = std::thread(
        [this]()
        {
            while (should_monitor)
            {
                try
                {
                    std::unique_ptr<DatabaseHanndler> connectionHanndler = std::make_unique<DatabaseHanndler>();

                    if (connectionHanndler->get_connection() == nullptr)
                    {
                        Message::WarningMessage("WatchDog could not acquire a database connection within timeout.");
                        std::this_thread::sleep_for(check_interval);
                        continue;
                    }

                    if (!connectionHanndler->get_connection()->check_connection())
                    {
                        Message::WarningMessage("Database connection lost. Attempting to reconnect...");
                        Message::CriticalMessage(
                            fmt::format("Database connection {} link is lost.", static_cast<const void *>(connectionHanndler->get_connection().get())));
                        try
                        {
                            if (connectionHanndler->get_connection()->reconnect())
                            {
                                Message::InfoMessage(fmt::format(
                                    "Database connection id: {} link is re-established", static_cast<void *>(connectionHanndler->get_connection().get())));
                                databaseConnectionPool->reconnect_all();
                            }
                            else
                            {
                                Message::ErrorMessage("Failed to reconnect to database");
                            }
                        }
                        catch (const std::exception &e)
                        {
                            Message::CriticalMessage(fmt::format("Reconnect exception: {}", e.what()));
                        }
                    }

                    std::this_thread::sleep_for(check_interval);
                }
                catch (const std::exception &e)
                {
                    Message::CriticalMessage(fmt::format("Monitoring exception: {}", e.what()));
                }
            }
        });
}

WatchDog::~WatchDog()
{
    // Ensure proper cleanup
    should_monitor = false;
    if (monitor_thread.joinable())
    {
        monitor_thread.join();
    }
}
