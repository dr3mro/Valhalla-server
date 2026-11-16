/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file temp_dir.hpp
 * @brief A small RAII helper that creates a unique temporary directory and
 * deletes it when finished.
 * @details Constructing a TempDir makes a fresh directory under the system temp
 * folder; its path can be read with path() or string(). When the object goes
 * out of scope, the destructor removes the directory and everything inside it,
 * so tests never leave files behind.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * Some code writes files to disk (for example caches or generated output).
 * Tests need a safe place to write without touching real user data or colliding
 * with other tests. This helper guarantees a unique location per test and
 * cleans up automatically, preventing leftover files from making later tests or
 * the developer's machine dirty.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - RAII (cleanup in the destructor):
 * https://en.cppreference.com/w/cpp/language/raii
 * - std::filesystem paths and directories:
 * https://en.cppreference.com/w/cpp/filesystem/path
 * - Deleted functions (prevent copying/moving):
 * https://en.cppreference.com/w/cpp/language/function#Deleted_functions
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */

#pragma once  // Include this header at most once per translation unit.

#include <unistd.h>  // getpid

// Chrono support for callers that create time-based files in the directory.
#include <chrono>
#include <cstdint>  // Fixed-width integer types.
// std::filesystem, used to build and remove the directory.
#include <filesystem>
#include <string>  // std::string / std::to_string, used to build a unique name.

namespace fs =
    std::filesystem;  // Short alias so the code below is easier to read.

/**
 * RAII unique temporary directory; removed recursively on destruction.
 * @brief Creates a private scratch directory and cleans it up automatically.
 * @details The name includes the process id and a counter, so two directories
 * created in the same run (or in concurrent test processes) never share a path.
 */
class TempDir
{
 public:
  TempDir()  // Creates the directory immediately so it is ready to use.
  {
    static unsigned long counter = 0;  // Shared counter makes each directory
                                       // name unique within the process.
    path_ = fs::temp_directory_path() /
            ("valhalla_tests_" + std::to_string(::getpid()) + "_" +
             std::to_string(++counter));  // Build a path like
                                          // /tmp/valhalla_tests_1234_1.
    fs::create_directories(
        path_);  // Actually create the directory (and any missing parents).
  }

  TempDir(const TempDir&) = delete;  // No copying: two objects would try to
                                     // delete the same directory.
  TempDir& operator=(const TempDir&) =
      delete;  // No copy assignment, for the same reason.
  TempDir(TempDir&&) =
      delete;  // No moving: ownership must stay with one object.
  TempDir& operator=(TempDir&&) =
      delete;  // No move assignment, to keep cleanup unambiguous.

  ~TempDir()  // Runs at scope exit to remove the directory and its contents.
  {
    std::error_code
        ec;  // Error-code overload avoids throwing from a destructor.
    fs::remove_all(path_,
                   ec);  // Delete the directory recursively; any error is
                         // stored in `ec` and ignored.
  }

  [[nodiscard]] const fs::path& path() const
  {
    return path_;
  }  // Give callers the path object (e.g. to append filenames).
  [[nodiscard]] std::string string() const
  {
    return path_.string();
  }  // Give callers the path as a plain text string.

 private:
  fs::path path_;  // Stores the full path to this object's temporary directory.
};
