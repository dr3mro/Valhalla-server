#!/bin/bash
conan install . --build=missing -s build_type=Release
conan install . --build=missing -s build_type=Debug
cmake -S. -Bbuild/Release -DCMAKE_BUILD_TYPE=Release
cmake -S. -Bbuild/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Release
cmake --build build/Debug
