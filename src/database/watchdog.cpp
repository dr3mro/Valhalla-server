#include "database/watchdog.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include "database/database.hpp"
#include "database/databaseconnectionpool.hpp"
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
                    std::shared_ptr<Database> db_ptr = databaseConnectionPool->get_connection();

                    if (db_ptr == nullptr)
                    {
                        Message::WarningMessage("WatchDog could not acquire a database connection within timeout.");
                        std::this_thread::sleep_for(check_interval);
                        continue;
                    }

                    if (!db_ptr->check_connection())
                    {
                        Message::WarningMessage("Database connection lost. Attempting to reconnect...");
                        Message::CriticalMessage(fmt::format("Database connection {} link is lost.", static_cast<const void *>(db_ptr.get())));
                        try
                        {
                            if (db_ptr->reconnect())
                            {
                                Message::InfoMessage(fmt::format("Database connection id: {} link is re-established", static_cast<void *>(db_ptr.get())));
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

                    databaseConnectionPool->return_connection(std::move(db_ptr));
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
