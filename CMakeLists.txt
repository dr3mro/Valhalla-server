cmake_minimum_required(VERSION 3.30...4.2)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_BINARY_DIR}/generators/conan_toolchain.cmake")

# Set the project name
set(TARGET "server")

project(${TARGET})

# Build logic lives in focused modules under cmake/. Include order matters:
# Dependencies defines the package variables used by Linking, and Doxygen is
# tag-gated and independent of the other targets.
include(cmake/CompilerSetup.cmake)
include(cmake/ProjectSetup.cmake)
include(cmake/CoreTargets.cmake)
include(cmake/Dependencies.cmake)

# Doxygen reference: the target is defined only when HEAD carries a git tag.
include(cmake/Doxygen.cmake)

include(cmake/ApiEmbed.cmake)
include(cmake/ApiDocs.cmake)
include(cmake/ConanVariables.cmake)
include(cmake/Linking.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Testing.cmake)
