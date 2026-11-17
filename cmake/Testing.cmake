# Catch2 unit tests and CTest registration.

# Unit tests. ON by default; CI/Docker release builds may
# pass -DENABLE_TESTS=OFF.
option(ENABLE_TESTS
"Build the Catch2 unit test suite and register it with CTest" ON)
if(ENABLE_TESTS)
  # enable_testing() here creates build/<type>/CTestTestfile.cmake at the root,
  # which ctest needs to descend into tests/. (tools/conan/variables.cmake sets
  # BUILD_TESTING OFF above, which would otherwise suppress it.)
  enable_testing()
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests)
else()
  message(STATUS "Tests disabled (ENABLE_TESTS=OFF)")
endif()
