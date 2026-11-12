# tests/CMakeLists.txt
#
# Catch2 v3 unit-test suite. Each TEST_CASE is registered as an individual
# CTest entry via catch_discover_tests().
#
# Notes:
# - tools/conan/variables.cmake sets BUILD_TESTING OFF (meant for third-party
#   dependencies). CTest's include(CTest) honours it, so we force it back ON
#   here explicitly, otherwise ctest would discover nothing.

if(NOT TARGET server_core)
    message(FATAL_ERROR "tests/ requires the server_core target (built from src/)")
endif()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(unit_tests ${TEST_SOURCES})

target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Catch2::Catch2WithMain supplies main(); no CATCH_CONFIG_MAIN needed (v3).
target_link_libraries(unit_tests PRIVATE server_core Catch2::Catch2WithMain)

target_compile_features(unit_tests PRIVATE cxx_std_20)

# BUILD_TESTING is set OFF in tools/conan/variables.cmake for third-party deps;
# restore it so CTest actually discovers our tests.
if(NOT BUILD_TESTING)
    set(BUILD_TESTING ON)
    message(STATUS "BUILD_TESTING was OFF (conan variables); re-enabled for the unit tests")
endif()

include(CTest)

# Conan's CMakeDeps generator for Catch2 does not install the CMake helper
# scripts that define catch_discover_tests(), so they are vendored here
# verbatim from the Catch2 3.7.1 Conan package sources (BSD 3-Clause, see
# the file headers).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Catch)

catch_discover_tests(unit_tests)
