# Project Valhalla

A high-performance, multi-tenant healthcare backend written in modern C++20. Valhalla exposes a versioned REST API for managing medical service providers (clinics, laboratories, pharmacies, radiology centers), their staff, patients, visits, and appointments — with authentication, role-based permissions, and built-in abuse protection baked into the request pipeline.

> **Status:** Active development. The API surface and data model are still evolving (see [`Todo.txt`](Todo.txt)).

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Tech Stack](#tech-stack)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Build from Source](#build-from-source)
  - [Run with Docker](#run-with-docker)
- [Configuration](#configuration)
- [API Overview](#api-overview)
- [Project Layout](#project-layout)
- [Development](#development)
- [Contributing](#contributing)
- [License](#license)

---

## Features

- **REST API (v2)** built on the [Drogon](https://github.com/drogonframework/drogon) asynchronous web framework.
- **Multi-tenant service model** — a single deployment serves multiple service types (clinics, laboratories, pharmacies, radiology centers) through generic, type-parameterized controllers.
- **People & clients** — separate `User` and `Provider` client types, each with full create / login / logout / read / update / delete / search lifecycles.
- **Patients, visits & appointments** — patient records, health data, prescriptions, drugs, paid services, and visit/appointment scheduling per service.
- **Staff management** — invite, add, and remove staff members on a per-service basis.
- **JWT authentication** — stateless session tokens issued and validated via `jwt-cpp`.
- **Role-based access control** — bitmask permission levels (`CAN_READ`, `CAN_WRITE`, `CAN_DELETE`, `IS_ADMIN`) enforced by a permission manager.
- **Security gatekeeper** layered into the request flow:
  - DoS / rate-limiting detector with configurable rate limits, ban durations, and IP whitelist/blacklist.
  - SQL-injection detector.
  - Password hashing with `libsodium`.
  - Session and token managers.
- **PostgreSQL persistence** via `libpqxx`, with a connection pool, a connection watchdog, and a database schema validator.
- **CORS support** with pre-routing OPTIONS handling and standard CORS headers on every response.
- **Structured logging** through `spdlog`, to console and/or file.
- **12-factor configuration** — every setting is read from environment variables with sensible defaults.

## Architecture

Requests flow through a layered pipeline:

```
HTTP request
   │
   ▼
Drogon listener  ──►  CORS pre-routing advice (handles OPTIONS)
   │
   ▼
Gatekeeper (filters/middlewares)
   ├─ DoS / rate-limit detector
   ├─ SQL-injection detector
   ├─ Token / session validation   (SECURE routes only)
   └─ Permission check (RBAC)
   │
   ▼
Route (api/v2/routes)  ──►  Controller (controllers/*)  ──►  Entity (entities/*)
   │
   ▼
Database controller  ──►  Connection pool  ──►  PostgreSQL
```

- **Routes** (`src/api/v2/routes`) declare endpoints and mark each as `SECURE` or `INSECURE`. A small registry maps a path parameter (e.g. `clientType`, `serviceType`) to the concrete controller, so one route class serves many entity types.
- **Controllers** (`src/controllers`) implement CRUD/search logic against a generic base.
- **Entities** (`src/entities`) model the domain: people (users, providers), services (clinics, laboratories, pharmacies, radiology centers), patients, visits, and appointments.
- **Gatekeeper** (`src/gatekeeper`) holds all the cross-cutting security concerns.
- **Configurator** (`src/configurator`) loads and validates configuration from the environment at startup.
- **Store** (`src/store`) provides shared singleton object access for controllers.

## Tech Stack

| Area              | Technology |
|-------------------|------------|
| Language          | C++20 (C17 for C sources) |
| Web framework     | Drogon + Trantor |
| Database          | PostgreSQL via libpqxx |
| Auth              | jwt-cpp |
| Crypto / hashing  | libsodium |
| JSON              | jsoncons, picojson |
| Config            | yaml-cpp + environment variables |
| Logging           | spdlog / fmt |
| HTTP client       | libcurl, c-ares |
| Hashing / utils   | xxhash, cppcodec, zlib |
| Profiling         | ctrack |
| Testing           | Catch2 |
| Build             | CMake (≥ 3.30) + Conan 2 |

## Getting Started

### Prerequisites

- A C++20-capable compiler (GCC 13+ / Clang 17+)
- [CMake](https://cmake.org/) ≥ 3.30
- [Conan](https://conan.io/) 2.x (package manager)
- A reachable [PostgreSQL](https://www.postgresql.org/) instance
- *(optional)* `ccache` — used automatically if found, to speed up rebuilds
- *(optional)* Docker + Docker Compose for containerized runs

### Build from Source

Install Conan and detect a build profile:

```bash
pipx install conan
pipx ensurepath
conan profile detect --force
```

Install dependencies and configure both build types:

```bash
# Resolve and build third-party dependencies
conan install . --build=missing -s build_type=Debug
conan install . --build=missing -s build_type=Release

# Configure CMake
cmake -S . -B build/Debug   -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build/Debug   -j8
cmake --build build/Release -j8
```

Or just run the convenience script, which performs all of the above:

```bash
./build.sh
```

The resulting binary is named `server` and is placed in the corresponding build directory.

### Run with Docker

A Compose profile is provided for a containerized run:

```bash
docker compose --profile run up --build
```

The release `Dockerfile` (`.github/workflows/docker/Dockerfile`) is based on `ubuntu:24.04`, ships the prebuilt `server` binary, and exposes port **8080**.

## Configuration

All configuration is supplied through environment variables. Defaults (see `src/configurator/defaults.hpp`) are tuned for a local Docker network, so you will want to override at least the database and token settings in production.

### Server

| Variable             | Default          | Description |
|----------------------|------------------|-------------|
| `SERVER_HOST`        | `0.0.0.0`        | Bind address |
| `SERVER_PORT`        | `8080`           | Listen port |
| `SERVER_THREADS`     | `4`              | Worker thread count |
| `SERVER_UPLOAD_DIR`  | `./uploads/`     | File upload directory |

### Database (PostgreSQL)

| Variable       | Default        | Description |
|----------------|----------------|-------------|
| `DB_HOST`      | `172.20.0.2`   | Database host |
| `DB_PORT`      | `5432`         | Database port |
| `DB_NAME`      | `postgres`     | Database name |
| `DB_USER`      | `postgres`     | Database user |
| `DB_PASS`      | `postgres`     | Database password |
| `DB_SSL`       | `false`        | Enable SSL |
| `DB_MAX_CONN`  | `10`           | Max pooled connections |

### Authentication (JWT)

| Variable        | Default                              | Description |
|-----------------|--------------------------------------|-------------|
| `TOKEN_SECRET`  | *(insecure dev default)*             | **Must** be overridden in production |

### DoS detector / rate limiting

| Variable        | Default | Description |
|-----------------|---------|-------------|
| `MAX_REQUESTS`  | `10000` | Max requests per period |
| `PERIOD`        | `30`    | Window length (seconds) |
| `MAX_FPS`       | `100`   | Max tracked fingerprints |
| `RL_DURATION`   | `30`    | Rate-limit duration (seconds) |
| `BAN_DURATION`  | `3600`  | Ban duration (seconds) |
| `CLN_FRQ`       | `30`    | Cleanup frequency (seconds) |
| `WHITELIST`     | *(empty)* | Whitelisted IPs |
| `BLACKLIST`     | *(empty)* | Blacklisted IPs |

### Frontend & email sender

| Variable                  | Default              | Description |
|---------------------------|----------------------|-------------|
| `FRONTEND_HOST`           | `http://172.20.0.5`  | Frontend base URL (used for invite links) |
| `FRONTEND_PORT`           | `3000`               | Frontend port |
| `EMAIL_SENDER_HOST`       | `http://172.20.0.6`  | Email sender daemon host |
| `EMAIL_SENDER_PORT`       | `5000`               | Email sender daemon port |
| `EMAIL_SENDER_QUEUE_PATH` | `/enqueue`           | Email enqueue endpoint |

> ⚠️ The default `TOKEN_SECRET` and database credentials are for local development only. Always set strong values before deploying.

## API Overview

All endpoints live under the `api/v2` namespace. Routes are marked **`SECURE`** (require a valid `Authorization: Bearer <token>` header) or **`INSECURE`** (public).

### Clients — `/{clientType}/...`  (`clientType` ∈ `users`, `providers`)

| Method | Path                        | Auth | Description |
|--------|-----------------------------|------|-------------|
| POST   | `/{clientType}/create`      | 🔓   | Register a client |
| POST   | `/{clientType}/login`       | 🔓   | Authenticate, receive a token |
| POST   | `/{clientType}/logout`      | 🔒   | Invalidate the session |
| POST   | `/{clientType}/suspend`     | 🔒   | Suspend a client |
| POST   | `/{clientType}/activate`    | 🔒   | Re-activate a client |
| POST   | `/{clientType}/read`        | 🔒   | Read client data |
| PUT    | `/{clientType}/update`      | 🔒   | Update a client |
| DELETE | `/{clientType}/delete`      | 🔒   | Delete a client |
| POST   | `/{clientType}/search`      | 🔒   | Search clients |
| GET    | `/{clientType}/services`    | 🔒   | List a client's services |

### Services — `/{serviceType}/...`  (`serviceType` ∈ `laboratories`, `pharmacies`, `radiologycenters`)

| Method | Path                      | Auth | Description |
|--------|---------------------------|------|-------------|
| POST   | `/{serviceType}/create`   | 🔒   | Create a service |
| POST   | `/{serviceType}/read`     | 🔒   | Read a service |
| PUT    | `/{serviceType}/update`   | 🔒   | Update a service |
| DELETE | `/{serviceType}/delete`   | 🔒   | Delete a service |
| POST   | `/{serviceType}/search`   | 🔒   | Search services |

### Clinics — `/clinics/...`

| Method | Path                       | Auth | Description |
|--------|----------------------------|------|-------------|
| POST   | `/{serviceType}/create`    | 🔒   | Create a clinic record |
| POST   | `/{serviceType}/read`      | 🔒   | Read a clinic record |
| PUT    | `/{serviceType}/update`    | 🔒   | Update a clinic record |
| DELETE | `/{serviceType}/delete`    | 🔒   | Delete a clinic record |
| POST   | `/patients/search`         | 🔒   | Search patients |
| GET    | `/patients/getvisits`      | 🔒   | List a patient's visits |

### Appointments — `/{entityType}/...`

| Method | Path                      | Auth | Description |
|--------|---------------------------|------|-------------|
| POST   | `/{entityType}/create`    | 🔒   | Create an appointment |
| POST   | `/{entityType}/read`      | 🔒   | Read an appointment |
| PUT    | `/{entityType}/update`    | 🔒   | Update an appointment |
| DELETE | `/{entityType}/delete`    | 🔒   | Delete an appointment |

### Staff — `/services/{serviceType}/staff/...`

| Method | Path                                    | Auth | Description |
|--------|-----------------------------------------|------|-------------|
| POST   | `/services/{serviceType}/staff/invite`  | 🔒   | Invite a staff member |
| POST   | `/services/{serviceType}/staff/add`     | 🔒   | Add a staff member |
| POST   | `/services/{serviceType}/staff/remove`  | 🔒   | Remove a staff member |

### Misc

| Method | Path  | Auth | Description |
|--------|-------|------|-------------|
| GET    | `/`   | 🔓   | Health/hello check |

> Unknown routes return a JSON `404` (`"Endpoint not found."`).

## Project Layout

```
src/
├── api/v2/             # REST API: routes, filters, middlewares, helpers
├── controllers/        # Business logic per resource (clients, clinics, services, …)
├── entities/           # Domain models (people, services, patients, visits, appointments)
├── gatekeeper/         # Security: DoS detector, SQLi detector, tokens, sessions, RBAC, password hashing
├── configurator/       # Environment-driven configuration + defaults
├── database/           # Connection pool, watchdog, handlers
├── validator/          # Request + database-schema validation
├── store/              # Shared object store
├── server/             # Server bootstrap (entry point)
└── utils/              # Logging, caching, messaging, JSON helpers, thread-safe queue
tests/                  # Catch2 test suite
tools/conan/            # Conan helpers
.github/workflows/      # CI (Ubuntu release build) + Docker packaging
```

## Development

- **Code style:** enforced via `.clang-format`; static analysis configured through `.clang-tidy` and `cppcheck.supp`.
- **Editor support:** `.clangd` provides LSP configuration; `.vscode/` and `.zed/` settings are included. `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so `compile_commands.json` is generated for tooling.
- **Warnings:** built with `-Wall -Wextra -pedantic`.
- **Versioning:** the current Git tag is embedded into the binary at build time via `get_tag.sh`.
- **Tests:** a Catch2-based suite lives in `tests/` and is wired into the CMake build.

```bash
# Run tests (after building)
ctest --test-dir build/Debug
```

## Contributing

Contributions are welcome. Please open an issue to discuss substantial changes first, keep commits focused, and run `clang-format` before submitting a pull request. See [`Todo.txt`](Todo.txt) for current work in progress.

## License

No license file is currently included in this repository. Until one is added, the work is "all rights reserved" by default — please contact the author before reuse. Consider adding a `LICENSE` file (e.g. MIT, Apache-2.0, or GPL-3.0) to clarify usage terms.

# Project Valhalla

```
pipx install conan 
pipx ensurepath 

conan profile detect --force

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
