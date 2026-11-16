#!/usr/bin/env bash
# /**
#  * @file build.sh
#  * @brief Configure, build (Release and Debug), and test the Valhalla server via Conan, CMake/Ninja and CTest.
#  * @details Read the Conan settings, generate the API docs, configure two separate build folders (Release and Debug), compile both, and run the tests for each.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the project's one-command build.
#  * CI and the ./run.sh task runner call it whenever a fresh build is needed.
#  * Without it, dependencies would be missing, docs would be stale, and no binaries or test results would exist.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Conan (C++ package manager): https://docs.conan.io/2/
#  * - CMake (build configuration): https://cmake.org/cmake/help/latest/
#  * - Ninja (fast build executor): https://ninja-build.org/manual.html
#  * - CTest (test runner): https://cmake.org/cmake/help/latest/manual/ctest.1.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Abort on any failing command, undefined variable, or failed pipeline stage so a broken build cannot appear successful.
set -euo pipefail  # e = stop on error, u = error on unset variable, pipefail = catch failures inside pipelines.
# Detect the default Conan profile; --force overwrites any existing profile so settings match the current toolchain.
conan profile detect --force  # A profile records compiler/architecture settings Conan uses to pick the right binaries.
# Install dependencies for both build types: gnu20 is required by the sources, and the conf
# suppresses an unused-result error originating only from the third-party trantor package.
conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20  -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]"  # -s sets a build setting; -c passes a config override to a named dependency.
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20  -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]"  # Same dependency install again, but for the Debug build type.
# Regenerate the OpenAPI spec and embedded docs before building.
if [[ -x tools/docs/build.sh ]]; then  # -x means 'this file exists and is executable'.
    # Docs generation is non-fatal: fall back to the committed docs rather than aborting the build.
    tools/docs/build.sh || echo "warning: docs build failed; continuing with committed docs" >&2  # >&2 sends the warning to stderr so it is not mixed with normal output.
fi

# Configure separate out-of-source build trees with the Ninja generator, one per build type.
cmake -S. -Bbuild/Release -DCMAKE_BUILD_TYPE=Release -G Ninja  # -S = source folder, -B = build folder, -G = which build tool to generate files for.
cmake -S. -Bbuild/Debug -DCMAKE_BUILD_TYPE=Debug -G Ninja  # Debug keeps symbols and no optimizations, which makes crashes easier to investigate.
# Build both configurations in parallel (10 jobs) to speed up the full matrix.
cmake --build build/Release -j 10  # -j 10 runs up to 10 compile jobs at once to use more CPU cores.
cmake --build build/Debug -j 10  # Build the Debug tree with the same parallelism.

# Run tests through the Conan-generated CTest presets so each uses its matching build tree.
ctest --preset conan-release  # 'conan-release' is a ready-made CTest preset created by Conan, not written here.
ctest --preset conan-debug  # Use the preset that matches the Debug build tree.
