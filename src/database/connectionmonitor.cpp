#include "database/connectionmonitor.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include <chrono>
#include <exception>
#include <memory>
#include <thread>

#include "database/database.hpp"  // IWYU pragma: keep
#include "utils/message/message.hpp"

void ConnectionMonitor::start()  // NOLINT
{
    if (monitor_thread.joinable())
    {
        return;
    }

    should_monitor = true;
    monitor_thread = std::thread(
        [this]()  // NOLINT
        {
            int                  retry_count = 0;
            std::chrono::seconds current_backoff{10};  // NOLINT

            while (should_monitor)
            {
                if (!db_ptr->check_connection())
                {
                    Message::WarningMessage("Database connection lost. Attempting to reconnect...");

                    while (!db_ptr->check_connection() && retry_count < config.max_retry_attempts)
                    {
                        try
                        {
                            db_ptr->reconnect();
                            if (db_ptr->get_connection()->is_open())
                            {
                                // connection = new_connection;
                                if (db_ptr->check_connection())
                                {
                                    Message::InfoMessage("Successfully reconnected to database");
                                    retry_count     = 0;
                                    current_backoff = std::chrono::seconds{10};  // NOLINT
                                    break;
                                }

                                Message::ErrorMessage("Failed to reconnect to database");
                                current_backoff *= 2;
                                std::this_thread::sleep_for(current_backoff);
                            }
                        }
                        catch (const std::exception &e)
                        {
                            Message::ErrorMessage(fmt::format("Reconnection attempt {} failed:", retry_count + 1));
                            Message::CriticalMessage(e.what());

                            retry_count++;
                            std::this_thread::sleep_for(current_backoff);

                            // Implement exponential backoff
                            current_backoff *= 2;
                            if (current_backoff > config.max_backoff)
                            {
                                current_backoff = config.max_backoff;
                            }
                        }
                    }

                    if (retry_count >= config.max_retry_attempts)
                    {
                        Message::CriticalMessage("Maximum retry attempts reached. Stopping connection monitor.");
                        should_monitor = false;
                        break;
                    }
                }
                std::this_thread::sleep_for(config.check_interval);
            }
        });
}

void ConnectionMonitor::stop()
{
    should_monitor = false;
    if (monitor_thread.joinable())
    {
        monitor_thread.join();
    }
}
