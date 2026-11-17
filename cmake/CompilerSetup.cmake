# Compiler toolchain and global compile settings, moved verbatim from the root
# CMakeLists.txt. Include after project() so compiler IDs are available.

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
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# set C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# Link-time optimization for Release builds
# (skipped when the toolchain does not support it)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(STATUS "LTO not supported: ${ipo_error}")
  endif()
endif()

add_compile_options(-fdiagnostics-color=always -fdiagnostics-show-option)

# Cargo-like diagnostics: carets, ranges, unicode art, template trees
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
  AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
  add_compile_options(-fdiagnostics-text-art-charset=unicode
                      -fdiagnostics-show-caret -fdiagnostics-show-labels)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fcolor-diagnostics -fdiagnostics-show-template-tree
                      -fdiagnostics-fixit-info -fcaret-diagnostics-max-lines=0)
endif()

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

# Copy compile_commands.json to project root so clangd/LSP can find it
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/build/compile_commands.json
    COMMENT "Copying compile_commands.json to project build dir"
)
