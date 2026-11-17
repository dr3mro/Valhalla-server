# CMake modules

The root `CMakeLists.txt` only sets the toolchain file, the target name, calls
`project()`, and includes the modules below. Because `include()` does not create
a new scope, variables such as `PACKAGE_LIBS` and targets such as `server_core`
stay visible across modules, so the include order is the only ordering
constraint.

| Module | Responsibility |
|---|---|
| `CompilerSetup.cmake` | ccache, C/C++ standards, warning and diagnostic flags, LTO, `compile_commands.json` and its copy target |
| `ProjectSetup.cmake` | global include dirs, warning suppression, configure-time `version.cpp` |
| `CoreTargets.cmake` | source glob, `server_core`, `valhalla_version`, `server` |
| `Dependencies.cmake` | Conan `find_package` calls and include paths |
| `Doxygen.cmake` | `valhalla_doxygen`, defined only when HEAD has a git tag |
| `ApiEmbed.cmake` | embeds `docs/api.html` into a generated header |
| `ApiDocs.cmake` | `valhalla_docs` regeneration target |
| `ConanVariables.cmake` | wrapper for `tools/conan/variables.cmake`; kept after `find_package` to preserve existing behavior |
| `Linking.cmake` | `add_subdirectory(libs)` and final links on `server_core` |
| `Sanitizers.cmake` | Linux Debug ASan/UBSan/LSan; also kept at its original (late) position for now |
| `Testing.cmake` | `ENABLE_TESTS`, CTest registration, `add_subdirectory(tests)` |

Scripts that are invoked with `cmake -P` at build time (`gen_version.cmake`,
`build_docs.cmake`, `embed_html.cmake`) stay under `tools/cmake/`, which is what
`docker/Dockerfile` copies. Configure-time modules live here under `cmake/`, and
`docker/Dockerfile` copies this directory too.
