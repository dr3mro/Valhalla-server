# Project Valhalla

```
conan install . --build=missing  -s build_type=Debug
conan install . --build=missing  -s build_type=Release

cmake -S. -Bbuild/Debug  -DCMAKE_BUILD_TYPE=Debug
cmake -S. -Bbuild/Release  -DCMAKE_BUILD_TYPE=Release

cmake --build build/Debug -j8
cmake --build build/Release -j8

```

### Test docker

```
docker compose --profile run up --build
```
