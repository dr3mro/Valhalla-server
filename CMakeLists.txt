cmake_minimum_required(VERSION 3.30)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/conan_toolchain.cmake")

# Set the project name and version
set(Target "server")

project(${Target})

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

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")

# Suppress developer warnings and deprecated warnings
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
set(CMAKE_WARN_DEPRECATED OFF)

# Get the current Git tag using the script
execute_process(COMMAND ${CMAKE_SOURCE_DIR}/get_tag.sh OUTPUT_VARIABLE GIT_TAG_MACRO OUTPUT_STRIP_TRAILING_WHITESPACE)

# Ensure the output variable is trimmed
string(STRIP "${GIT_TAG_MACRO}" GIT_TAG_MACRO)

# Add the Git tag macro to the compile definitions
add_compile_definitions(${GIT_TAG_MACRO})

# Find all source files
file(GLOB_RECURSE SRC "src/*.cpp" "src/*.tpp")

# Add the executable
add_executable(${Target} ${SRC})

# Define all your packages in a list
set(CONAN_PACKAGES
  picojson
  asio
  fmt
  Crow
  libpqxx
  yaml-cpp
  jwt-cpp
  jsoncons
  Catch2
  xxHash
  libsodium
  CURL
  ZLIB
  spdlog
  OpenSSL
)

# Find all packages at once
foreach(PACKAGE ${CONAN_PACKAGES})
  find_package(${PACKAGE} CONFIG REQUIRED COMPONENTS INCLUDE_DIRS LIBRARIES)
  list(APPEND PACKAGE_INCLUDES ${${PACKAGE}_INCLUDE_DIRS})
  list(APPEND PACKAGE_LIBS ${${PACKAGE}_LIBRARIES})
endforeach()

# Add includes
target_include_directories(${Target} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PACKAGE_INCLUDES}
)

# Link libraries
target_link_libraries(${Target} PRIVATE ${PACKAGE_LIBS})

# Compile definitions
target_compile_definitions(${Target} PUBLIC SPDLOG_FMT_EXTERNAL)
target_compile_definitions(${Target} PUBLIC CROW_ENABLE_COMPRESSION)

# Enable AddressSanitizer only for Debug build type
if(CMAKE_BUILD_TYPE MATCHES Debug)
  target_compile_options(${Target} PRIVATE -g)

  if(LINUX)
    message("Enabling AddressSanitizer for Debug builds on Linux")
    target_compile_options(${Target} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-sanitize=thread -fno-omit-frame-pointer)
    target_link_options(${Target} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-sanitize=thread -fno-omit-frame-pointer)
  endif()
endif()

add_subdirectory(tests)
