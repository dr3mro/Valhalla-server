#include "database/connectionmonitor.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <chrono>
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
            std::chrono::seconds current_backoff{1};
            bool                 isConnected = false;

            while (should_monitor)
            {
                try
                {
                    auto db_ptr = databaseConnectionPool->get_connection();
                    isConnected = db_ptr->check_connection();

                    if (!isConnected)
                    {
                        Message::ErrorMessage("Database connection lost. Attempting to reconnect...");

                        // Attempt to reconnect
                        while (!isConnected && should_monitor)
                        {
                            try
                            {
                                std::this_thread::sleep_for(current_backoff);
                                isConnected = db_ptr->reconnect();

                                if (isConnected)
                                {
                                    current_backoff = std::chrono::seconds{1};  // Reset backoff
                                    Message::InfoMessage("Database connection re-established");
                                    break;
                                }

                                Message::ErrorMessage("Failed to reconnect to database");
                            }
                            catch (const std::exception &e)
                            {
                                Message::CriticalMessage(fmt::format("Reconnect exception: {}", e.what()));
                            }

                            // Exponential backoff
                            current_backoff = std::min(current_backoff * 2, config.max_backoff);
                        }
                    }

                    databaseConnectionPool->return_connection(db_ptr);
                    std::this_thread::sleep_for(config.check_interval);
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
