#pragma once

#include <unistd.h>  // getpid

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

/// RAII unique temporary directory; removed recursively on destruction.
class TempDir
{
   public:
    TempDir()
    {
        static unsigned long counter = 0;
        path_                        = fs::temp_directory_path() / ("valhalla_tests_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
        fs::create_directories(path_);
    }

    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&)                 = delete;
    TempDir& operator=(TempDir&&)      = delete;

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const { return path_; }
    [[nodiscard]] std::string     string() const { return path_.string(); }

   private:
    fs::path path_;
};
