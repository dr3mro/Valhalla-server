#!/bin/bash
conan profile detect --force
conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20  -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]"
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=gnu20  -c "trantor/*:tools.build:cxxflags=[\"-Wno-error=unused-result\"]"
cmake -S. -Bbuild/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake -S. -Bbuild/Debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build/Release -j 10
cmake --build build/Debug -j 10
