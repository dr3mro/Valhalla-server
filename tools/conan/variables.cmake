message(STATUS "Injecting custom CMake variables for dependencies")

set(ZLIB_BUILD_EXAMPLES OFF)
set(SKIP_BUILD_TEST ON)
set(BUILD_SHARED_LIBS OFF)
set(YAML_CPP_BUILD_TOOLS OFF)
set(ZLIB_BUILD_TESTS OFF)
set(JWT_BUILD_EXAMPLES OFF)
set(JWT_BUILD_TESTS OFF)
set(BUILD_TESTING OFF)
set(XXHASH_BUILD_XXHSUM OFF)
set(BUILD_SHARED_LIBS OFF)
set(XXH_STATIC_LINKING_ONLY ON)
set(XXH_INLINE_ALL ON)
set(XXHASH_BUILD_ENABLE_INLINE_API OFF)
set(SODIUM_DISABLE_TESTS ON)
set(CARES_STATIC ON)
set(CARES_STATIC_PIC ON)
set(CARES_SHARED OFF)
set(CARES_BUILD_TESTS OFF)
set(CARES_BUILD_TOOLS OFF)
set(BUILD_EXAMPLES OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_EXAMPLES OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_FMT_EXTERNAL ON)
