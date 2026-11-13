#pragma once

#include <cstdlib>
#include <optional>
#include <string>

/// RAII guard that sets (or unsets) an environment variable and restores the
/// previous state on destruction. Never touches `.env`; only the process env.
class EnvGuard
{
   public:
    /// Sets `key` to `value` for the lifetime of the guard.
    EnvGuard(const std::string& key, const std::string& value) : key_(key)
    {
        const char* previous = std::getenv(key.c_str());
        if (previous != nullptr)
        {
            hadPrevious_ = true;
            previous_    = previous;
        }
        setenv(key.c_str(), value.c_str(), 1);
    }

    /// Unsets `key` for the lifetime of the guard.
    explicit EnvGuard(const std::string& key) : key_(key)
    {
        const char* previous = std::getenv(key.c_str());
        if (previous != nullptr)
        {
            hadPrevious_ = true;
            previous_    = previous;
        }
        unsetenv(key.c_str());
    }

    EnvGuard(const EnvGuard&)            = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;
    EnvGuard(EnvGuard&&)                 = delete;
    EnvGuard& operator=(EnvGuard&&)      = delete;

    ~EnvGuard()
    {
        if (hadPrevious_)
        {
            setenv(key_.c_str(), previous_.c_str(), 1);
        }
        else
        {
            unsetenv(key_.c_str());
        }
    }

   private:
    std::string key_;
    std::string previous_;
    bool        hadPrevious_ = false;
};
