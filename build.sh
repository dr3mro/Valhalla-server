#!/bin/bash
conan profile detect --force
conan install . --build=missing -s build_type=Release
conan install . --build=missing -s build_type=Debug
cmake -S. -Bbuild/Release -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake -S. -Bbuild/Debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build/Release
cmake --build build/Debug
