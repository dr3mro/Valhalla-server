# Conan package discovery and include paths for server_core.

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
    Drogon)

# Find all packages at once
foreach(PACKAGE ${CONAN_PACKAGES})
  find_package(${PACKAGE} CONFIG REQUIRED COMPONENTS INCLUDE_DIRS LIBRARIES)
  list(APPEND PACKAGE_INCLUDES ${${PACKAGE}_INCLUDE_DIRS})
  list(APPEND PACKAGE_LIBS ${${PACKAGE}_LIBRARIES})
endforeach()

# Add includes (PUBLIC so the unit tests inherit them via server_core)
target_include_directories(server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
                                              ${PACKAGE_INCLUDES})
