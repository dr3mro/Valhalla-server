#pragma once

// IO-thread fast path. A request is first tried on the Drogon IO thread inside a DbProbe. Everything that answers from memory
// (bad JSON, failed validation, a permission refused from the permission cache) finishes there. The first call that would
// need a database connection throws WouldBlock instead of waiting, before anything was written, and the request is then
// handed to the DB executor and run again from the start.
//
// WouldBlock is deliberately not a std::exception, so the controllers' catch (const std::exception&) blocks let it through.
struct WouldBlock
{
};

class DbProbe
{
   public:
    DbProbe() { active_ = true; }
    ~DbProbe() { active_ = false; }
    DbProbe(const DbProbe&)            = delete;
    DbProbe& operator=(const DbProbe&) = delete;

    // Database access calls this first: inside a probe it throws WouldBlock.
    static void check()
    {
        if (active_)
        {
            throw WouldBlock{};
        }
    }

   private:
    static inline thread_local bool active_ = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
};
