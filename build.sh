#!/bin/bash
conan install . --build=missing -s build_type=Release --profile:build=gcc142R
conan install . --build=missing -s build_type=Debug --profile:build=gcc142D
cmake -S. -Bbuild/Release -DCMAKE_BUILD_TYPE=Release
cmake -S. -Bbuild/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Release
cmake --build build/Debug