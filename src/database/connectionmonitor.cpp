#include "database/connectionmonitor.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>

#include "database/database.hpp"  // IWYU pragma: keep
#include "utils/message/message.hpp"
ConnectionMonitor::ConnectionMonitor()  // NOLINT
{
    if (monitor_thread.joinable())
    {
        throw std::logic_error("Monitor thread already running!");
    }

    // Start the monitoring thread
    monitor_thread = std::thread(
        [this]()
        {
            while (should_monitor)
            {
                try
                {
                    auto db_ptr = databaseConnectionPool->get_connection();

                    if (!db_ptr->check_connection())
                    {
                        Message::WarningMessage("Database connection lost. Attempting to reconnect...");
                        Message::CriticalMessage(fmt::format("Database connection {} link is lost.", static_cast<const void *>(db_ptr.get())));
                        try
                        {
                            if (db_ptr->reconnect())
                            {
                                Message::InfoMessage(fmt::format("Database connection id: {} link is re-established", static_cast<void *>(db_ptr.get())));
                                reconnect_all();
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

                    databaseConnectionPool->return_connection(db_ptr);
                    std::this_thread::sleep_for(check_interval);
                }
                catch (const std::exception &e)
                {
                    Message::CriticalMessage(fmt::format("Monitoring exception: {}", e.what()));
                }
            }
        });
}

ConnectionMonitor::~ConnectionMonitor()
{
    // Ensure proper cleanup
    should_monitor = false;
    if (monitor_thread.joinable())
    {
        monitor_thread.join();
    }
}

void ConnectionMonitor::reconnect_all()
{
    auto db_ptr = databaseConnectionPool->get_connection();

    while (!db_ptr->check_connection())
    {
        if (db_ptr->reconnect())
        {
            Message::InfoMessage(fmt::format("Database connection id: {} link is re-established", static_cast<void *>(db_ptr.get())));
        }
        databaseConnectionPool->return_connection(db_ptr);
        db_ptr = databaseConnectionPool->get_connection();
    };

    databaseConnectionPool->return_connection(db_ptr);
}