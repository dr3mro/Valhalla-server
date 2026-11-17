# Directory-wide includes, warning suppression and version-file generation.

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/")

# Suppress developer warnings and deprecated warnings
set(
  CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1
  CACHE INTERNAL "No dev warnings")
set(CMAKE_WARN_DEPRECATED OFF)
set(CMAKE_COLOR_DIAGNOSTICS ON)

# Generate version.cpp holding the current Git tag. Only that one file
# recompiles when the tag changes. It is generated now (so it exists) and
# refreshed on every build by the valhalla_version target below, so new
# commits and tags show up without re-running CMake.
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}")
set(BINARY_DIR "${CMAKE_BINARY_DIR}")
include(tools/cmake/gen_version.cmake)
unset(SOURCE_DIR)
unset(BINARY_DIR)
add_compile_options(-ftemplate-backtrace-limit=0)
