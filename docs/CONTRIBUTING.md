# Contributing

Thanks for helping improve Valhalla. This document covers how the code is
organised and the conventions that keep the documentation and source
consistent. For build and install instructions see `README.md`; for a map of
the runtime components see `ARCHITECTURE.md`.

## Code organisation

```
src/
  api/v2/        HTTP routes, request filters, middlewares, response helpers
  concurrency/   bounded worker executor
  configurator/  environment-driven configuration groups + defaults
  controllers/   CRUDS controller hierarchy (sync + coroutine variants)
  database/      sync connection pool and async Drogon client paths
  entities/      domain value types and SQL statement builders
  gatekeeper/    authentication, authorisation, sessions, tokens, DoS checks
  server/        server bootstrap, CORS, startup banner
  store/         thread-safe singleton service registry
  utils/         logging, JSON, caching, queues, shared type definitions
  validator/     request validation and database-schema metadata
  main.cpp       process entry point
tests/           Catch2 suite and support helpers
tools/           CMake helpers, API-doc generation, k6 load scripts
scripts/         build/run/QA/test shell scripts
```

New source files under `src/` are picked up automatically by CMake's recursive
glob (`src/*.cpp`); there is no manual source list to edit.

## Coding conventions

- C++20. Build with `-Wall -Wextra -pedantic`; do not introduce new warnings.
- Formatting is defined by `.clang-format` (LLVM-derived, 4-space indent,
  brace-on-next-line for functions). Run `clang-format -i` on touched files.
- `.clang-tidy` configures static analysis; address findings in code you touch.
- `compile_commands.json` is generated for clangd; editor configs for VS Code
  and Zed are included under `.vscode/` and `.zed/`.

## Documentation conventions

All public and private declarations are documented with Doxygen `/** ... */`
blocks so `doxygen Doxyfile` produces a complete reference. Follow these rules:

1. **Document intent, not syntax.** Explain *why* a lock, branch, or type is
   needed, not that the line increments a counter.
2. **Complete contracts.** Each function documents parameters (`@param[in]`,
   `@param[out]`, `@param[in,out]`), return value (`@return`), side effects,
   pre/postconditions, exceptions (`@throws`), and thread-safety or ownership
   requirements where they are known.
3. **Evidence only.** Never invent behaviour, guarantees, or exceptions. If a
   property cannot be established from the implementation, say so explicitly.
4. **Preserve accurate existing comments.** Correct them only when the code
   provides clear evidence.
5. **Classes** carry `@class` and `@brief` plus a `@details` paragraph covering
   responsibility, lifecycle, ownership and thread-safety.
6. **Members and non-obvious locals** get a short comment explaining what they
   represent and why they exist; do not add comments that merely restate code.
7. **Ownership and concurrency matter.** Call out `unique_ptr` vs `shared_ptr`,
   borrowed references, `std::move`, `const` correctness, atomic memory
   ordering, and lock ordering wherever they affect maintenance.

### Shell scripts

Shell scripts under `scripts/`, `tools/`, `docker/`, `.github/` and the
repository root use the same header fields, written as `#`-prefixed lines
immediately after the shebang (`@file`, `@brief`, `@author`, `@date`), plus
`# @brief` / `# @param` blocks above each function and `#` WHY comments for
non-obvious commands. Doxygen has no shell parser, so `.sh` files are not part
of the Doxygen `INPUT`; they are documented in-source and read directly.

Regenerate the API reference documentation with:

```bash
doxygen Doxyfile
# open docs/doxygen/html/index.html
```

## Build and test

Dependencies are managed with Conan and pinned in `conan.lock`. Typical local
flow (see `README.md` for the full, platform-specific sequence):

```bash
conan install . --build=missing -s build_type=Debug -of build/Debug
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --test-dir build/Debug --output-on-failure
```

`build.sh` wraps these steps and runs the tests via CTest presets. Tests run
without a database (`VALHALLA_TEST_NO_DB=true`), so they work on any machine.
Useful build options: `-DENABLE_TESTS=OFF` and `-DVALHALLA_BUILD_DOCS=OFF`.

## Contribution workflow

1. Open an issue to discuss substantial changes before implementing them.
2. Keep commits focused; one logical change per pull request.
3. Add or update Doxygen comments alongside code changes, and update
   `ARCHITECTURE.md` when component relationships change.
4. Run `clang-format`, build and run the test suite before submitting.
5. Confirm documentation changes do not alter behaviour; the audit in
   `DOCUMENTATION_AUDIT.md` describes how this is verified.

## Open questions

See the "Open questions" section at the end of `DOCUMENTATION_PROGRESS.md`.
