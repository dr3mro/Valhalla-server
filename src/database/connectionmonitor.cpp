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
                        try
                        {
                            if (db_ptr->reconnect())
                            {
                                Message::InfoMessage("Database connection re-established");
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
