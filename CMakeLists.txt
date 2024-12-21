cmake_minimum_required(VERSION 3.30)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")

# Set the project name and version
set(Target "server")

project(${Target})

# Use CCACHE if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# set C standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)

# Set C++ flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

# set C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2")

add_compile_options(-fdiagnostics-color=always -fdiagnostics-show-option)

# set(CMAKE_C_CLANG_TIDY clang-tidy -checks=-*,readability-*)
# set(CMAKE_CXX_CLANG_TIDY clang-tidy -checks=-*,readability-*)

# option(CPPCHECK "Run CppCheck static code analysis" ON)

# if(CPPCHECK) message("Running CppCheck static code analysis...")
# find_program(cppcheck_exe NAMES cppcheck REQUIRED) set(cppcheck_opts
# --enable=all --inline-suppr
# --suppressions-list=${PROJECT_SOURCE_DIR}/cppcheck.supp) set(CMAKE_C_CPPCHECK
# ${cppcheck_exe} --std=c11 ${cppcheck_opts}) set(CMAKE_CXX_CPPCHECK
# ${cppcheck_exe} --std=c++20 ${cppcheck_opts}) endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")

# Suppress developer warnings and deprecated warnings
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS
    1
    CACHE INTERNAL "No dev warnings")
set(CMAKE_WARN_DEPRECATED OFF)
set(CMAKE_COLOR_DIAGNOSTICS ON)

# Get the current Git tag using the script
execute_process(
  COMMAND ${CMAKE_SOURCE_DIR}/get_tag.sh
  OUTPUT_VARIABLE GIT_TAG_MACRO
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Ensure the output variable is trimmed
string(STRIP "${GIT_TAG_MACRO}" GIT_TAG_MACRO)

# Add the Git tag macro to the compile definitions
add_compile_definitions(${GIT_TAG_MACRO})
add_compile_options(-ftemplate-backtrace-limit=0)

# Find all source files
file(GLOB_RECURSE SRC "src/*.cpp")

# Add the executable
add_executable(${Target} ${SRC})

# Define all your packages in a list
set(CONAN_PACKAGES
    cppcodec
    picojson
    fmt
    libpqxx
    jwt-cpp
    jsoncons
    Catch2
    xxHash
    libsodium
    CURL
    ZLIB
    c-ares
    spdlog
    Trantor
    Drogon
    ctrack)

# Find all packages at once
foreach(PACKAGE ${CONAN_PACKAGES})
  find_package(${PACKAGE} CONFIG REQUIRED COMPONENTS INCLUDE_DIRS LIBRARIES)
  list(APPEND PACKAGE_INCLUDES ${${PACKAGE}_INCLUDE_DIRS})
  list(APPEND PACKAGE_LIBS ${${PACKAGE}_LIBRARIES})
endforeach()

# Add includes
target_include_directories(${Target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                             ${PACKAGE_INCLUDES})

include(tools/conan/variables.cmake)

# Link libraries
target_link_libraries(${Target} PRIVATE ${PACKAGE_LIBS})

# Enable AddressSanitizer only for Debug build type on Linux for now
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(LINUX)
    message("Enabling AddressSanitizer for Debug builds on Linux")
    target_compile_options(
      ${Target} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak
                        -fno-sanitize=thread -fno-omit-frame-pointer)
    target_link_options(
      ${Target}
      PRIVATE
      -fsanitize=address
      -fsanitize=undefined
      -fsanitize=leak
      -fno-sanitize=thread
      -fno-omit-frame-pointer)
  endif()
endif()
add_subdirectory(tests)
