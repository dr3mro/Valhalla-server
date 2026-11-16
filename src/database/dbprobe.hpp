/**
 * @file dbprobe.hpp
 * @brief Control-flow signal that a request needs the database and must move
 * off the IO thread.
 * @details This header defines two pieces of the IO-thread fast path:
 * WouldBlock, a small exception-like signal that is deliberately not a
 * std::exception, and DbProbe, an RAII marker that sets a thread-local
 * "probing" flag for its lifetime. While the flag is set, DbProbe::check()
 * throws WouldBlock so that any code about to touch the database stops before
 * it does any work or writes a response.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * It lets simple requests finish without the cost of queueing onto the DB
 * executor. The HTTP wrapper creates a DbProbe on the IO thread and runs the
 * controller once; requests answered from memory (bad JSON, cached permission
 * denial) complete there. The first operation that needs a connection calls
 * check(), throws WouldBlock, and the wrapper re-runs the whole request on a DB
 * worker where no probe is active. WouldBlock is not a std::exception on
 * purpose, so generic error handlers do not swallow this control-flow signal.
 * Without it, every request would pay the queueing cost even when no database
 * access was needed.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (a flag set in the constructor and cleared in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - thread_local storage (one variable instance per thread):
 * https://en.cppreference.com/w/cpp/language/storage_duration
 * - Control-flow exceptions (throwing to change execution path, not to report
 * an error)
 * - Exceptions that do not derive from std::exception:
 * https://en.cppreference.com/w/cpp/error/exception
 * - static inline members: https://en.cppreference.com/w/cpp/language/static
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include guard: define these types at most once per translation
              // unit.

// IO-thread fast path. A request is first tried on the Drogon IO thread inside
// a DbProbe. Everything that answers from memory (bad JSON, failed validation,
// a permission refused from the permission cache) finishes there. The first
// call that would need a database connection throws WouldBlock instead of
// waiting, before anything was written, and the request is then handed to the
// DB executor and run again from the start.
//
// WouldBlock is deliberately not a std::exception, so the controllers' catch
// (const std::exception&) blocks let it through.
/**
 * @struct WouldBlock
 * @brief Control-flow signal that a request needs the database and must move
 * off the IO thread.
 * @details Thrown by DbProbe::check() before any database access or response is
 * written. It deliberately does not derive from `std::exception`, so
 * controllers' generic `catch (const std::exception&)` blocks do not swallow
 * it; only the dedicated `catch (const WouldBlock&)` in the fast-path wrapper
 * handles it.
 * @see DbProbe, DbExecutor::ioFastPath
 */
struct WouldBlock  // An empty tag type used only to unwind the stack back to
                   // the fast-path wrapper.
{
};

/**
 * @class DbProbe
 * @brief RAII marker that puts the current thread in "probe" mode.
 * @details While an instance is alive, DbProbe::check() throws WouldBlock on
 * that thread, preventing any database access from running on a Drogon IO
 * thread. The active flag is `thread_local`, so only the constructing thread is
 * affected. Instances are stack-allocated within a try block in the request
 * wrapper and must not be shared across threads. Non-copyable and
 * non-assignable.
 * @note The destructor unconditionally clears the flag; nested probes on the
 * same thread would clear it early, so usage assumes a single probe per thread
 * at a time.
 * @see WouldBlock
 */
class DbProbe  // Sets a per-thread flag so database access points can detect
               // the fast path.
{
 public:
  /**
   * Marks the current thread as probing for the lifetime of this object.
   */
  DbProbe()
  {
    active_ = true;
  }  // Turning the flag on is all the setup needed; the destructor turns it
     // off.

  /**
   * Clears the probe flag for the current thread.
   */
  ~DbProbe()
  {
    active_ = false;
  }  // Always clears it, including when WouldBlock unwinds the stack.

  DbProbe(const DbProbe&) =
      delete;  // Copying would make it unclear which object clears the flag.
  DbProbe& operator=(const DbProbe&) =
      delete;  // Assignment would have the same ambiguity
               // about flag ownership.

  /**
   * @brief Guards a database-access point against running on an IO thread.
   * @throws WouldBlock If called while a DbProbe is active on this thread.
   * @details Database helpers call this first; in probe mode it throws before
   * any connection is leased, allowing the request to be re-run from the
   * start on a DB worker where no probe is active.
   */
  static void check()  // Static: callers do not need a DbProbe object, just
                       // the flag it sets for the thread.
  {
    if (active_)  // True only while this thread is probing the fast path.
    {
      throw WouldBlock{};  // Abort the attempt before any database work
                           // or response side effect occurs.
    }
  }

 private:
  static inline thread_local bool active_ =
      false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables):
              // one flag per thread, shared by all DbProbe instances on that
              // thread.
};
