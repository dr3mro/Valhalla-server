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
    - [Debian / Ubuntu](#debian--ubuntu)
    - [Fedora](#fedora)
  - [Database setup](#database-setup)
  - [Run the server](#run-the-server)
  - [Run with Docker](#run-with-docker)
- [Configuration](#configuration)
- [API Overview](#api-overview)
  - [Interactive API docs](#interactive-api-docs)
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
- [CMake](https://cmake.org/) ≥ 3.30 and [Ninja](https://ninja-build.org/) (used by `build.sh`)
- [Conan](https://conan.io/) 2.x (package manager)
- A reachable [PostgreSQL](https://www.postgresql.org/) instance
- *(optional)* `ccache` — used automatically if found, to speed up rebuilds
- *(optional)* Docker + Docker Compose for containerized runs

Tested distributions: **Debian 13 (trixie)+ / Ubuntu 24.04+** and **Fedora 40+**. Older releases ship GCC < 13 or CMake < 3.30; see the notes below for workarounds.

### Build from Source

#### Debian / Ubuntu

```bash
# 1. Toolchain and build helpers (Conan compiles most dependencies from source)
sudo apt update
sudo apt install -y build-essential git curl zip unzip tar pkg-config \
    cmake ninja-build ccache make autoconf automake libtool bison flex perl \
    uuid-dev libssl-dev python3 python3-pip pipx \
    postgresql postgresql-client

# 2. Check versions: you need GCC >= 13 and CMake >= 3.30
g++ --version | head -1
cmake --version | head -1
```

> If `cmake` is older than 3.30 (e.g. Ubuntu 22.04/Debian 12), install a newer one with `pipx install cmake` (or from the [Kitware APT repository](https://apt.kitware.com/)). If `g++` is older than 13, install `g++-13` (`sudo apt install g++-13`) and export `CC=gcc-13 CXX=g++-13` before the steps below.

#### Fedora

```bash
# 1. Toolchain and build helpers
sudo dnf install -y gcc gcc-c++ make git curl zip unzip tar pkgconf-pkg-config \
    cmake ninja-build ccache autoconf automake libtool bison flex perl-core \
    libuuid-devel openssl-devel python3 python3-pip pipx \
    postgresql-server postgresql

# 2. Check versions: you need GCC >= 13 and CMake >= 3.30
g++ --version | head -1
cmake --version | head -1
```

#### Install Conan and build (both distributions)

```bash
pipx install conan
pipx ensurepath          # then open a new shell so ~/.local/bin is on PATH
conan profile detect --force
```

`conan profile detect` produces a profile with `compiler.libcxx=libstdc++11`, which is what you want on Linux. The project requires C++20; if Conan complains about the language standard, set `compiler.cppstd=20` in `~/.conan2/profiles/default`.

Install dependencies and configure both build types:

```bash
# Resolve and build third-party dependencies (the first run takes a while)
conan install . --build=missing -s build_type=Debug
conan install . --build=missing -s build_type=Release

# Configure CMake
cmake -S . -B build/Debug   -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release

# Compile
cmake --build build/Debug   -j"$(nproc)"
cmake --build build/Release -j"$(nproc)"
```

Or just run the convenience script, which performs all of the above (it uses Ninja):

```bash
./build.sh
```

The resulting binary is named `server` and is placed in the corresponding build directory (`build/Release/server`).

### Database setup

The server needs a PostgreSQL database initialised with `init.sql`. Skip this section if you already have a database, or if you use Docker Compose.

**Debian / Ubuntu** — the service is created and started by the package:

```bash
sudo systemctl enable --now postgresql
```

**Fedora** — initialise the cluster first, then allow password logins over TCP:

```bash
sudo postgresql-setup --initdb
sudo systemctl enable --now postgresql
# Fedora's default pg_hba.conf uses "ident" for 127.0.0.1; switch it to a password method:
sudo sed -i 's/\(127.0.0.1\/32\s\+\)ident/\1scram-sha-256/; s/\(::1\/128\s\+\)ident/\1scram-sha-256/' /var/lib/pgsql/data/pg_hba.conf
sudo systemctl restart postgresql
```

**Both** — set a password for the `postgres` role and load the schema:

```bash
sudo -u postgres psql -c "ALTER USER postgres PASSWORD 'postgres';"   # choose a real password outside local dev
PGPASSWORD=postgres ./db_init.sh                                      # runs init.sql against 127.0.0.1
```

`db_init.sh` connects as `postgres` to the `postgres` database on `127.0.0.1`. To use another database or user, run `psql -h <host> -U <user> -d <db> -f init.sql` yourself.

### Run the server

Create a `.env` file in the project root (it is read by `run_release.sh`; see [Configuration](#configuration) for every variable):

```bash
cat > .env <<'EOF'
SERVER_HOST=0.0.0.0
SERVER_PORT=8080
DB_HOST=127.0.0.1
DB_PORT=5432
DB_NAME=postgres
DB_USER=postgres
DB_PASS=postgres
TOKEN_SECRET=change-me-to-a-long-random-string
EOF
```

Then start it:

```bash
./run_release.sh            # run build/Release/server with the .env loaded
./run_release.sh --build    # rebuild Release first, then run
```

Or run the binary directly with the variables exported in your shell: `./build/Release/server`.

Verify it is up:

```bash
curl http://127.0.0.1:8080/api/v2/hello
```

Then open the interactive API docs at <http://127.0.0.1:8080/api/v2/doc>. If the port is not reachable from another machine, open it in the firewall (`sudo ufw allow 8080/tcp` on Debian/Ubuntu, `sudo firewall-cmd --add-port=8080/tcp --permanent && sudo firewall-cmd --reload` on Fedora).

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
| `DB_HOST`      | `127.0.0.1`    | Database host (`172.20.0.2` in the Docker Compose network) |
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
| `MAX_REQUESTS`  | `10000` | Max repeat-count for one fingerprint before the IP is **banned** |
| `PERIOD`        | `30`    | Window length (seconds) |
| `MAX_FPS`       | `100`   | Max distinct fingerprints per IP before the IP is **rate-limited** |
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

All endpoints live under the `/api/v2` prefix (for example `http://127.0.0.1:8080/api/v2/hello`). Routes marked 🔒 require a valid `Authorization: Bearer <token>` header (obtained from `/clients/{type}/login`); 🔓 routes are public. Request and response bodies are JSON.

### Interactive API docs

The server ships its own API documentation, compiled into the binary:

| URL | What it is |
|-----|------------|
| `http://<host>:<port>/api/v2/doc` | Swagger UI for every endpoint, plus an **API Guide** panel (permissions, data model, validation, errors, rate limiting, recipes, troubleshooting) and a **Test console** that runs all endpoints in order against the live server with dummy data, and a **Benchmark** panel (latency percentiles and requests/s per endpoint; writes are created with unique data and deleted afterwards) |

With the defaults that is <http://127.0.0.1:8080/api/v2/doc>; the startup banner prints the exact URL. From the page you can also download the **OpenAPI 3** spec and a **Postman collection** (also kept in `docs/openapi.json` and `docs/valhalla.postman_collection.json`).

The page is embedded into the binary, and every `cmake --build` regenerates it from `tools/docs/` first (the `valhalla_docs` target) (this needs `bash`, `python3` and Node.js/`npm`; if one is missing, the committed `docs/api.html` is embedded as is). Pass `-DVALHALLA_BUILD_DOCS=OFF` to skip regeneration. To regenerate by hand:

```bash
tools/docs/build.sh        # regenerates docs/api.html, openapi.json and the Postman collection
./run_release.sh --build   # re-embed the page and restart
```

To include real response samples in the docs, run `SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh` against a running server before `tools/docs/build.sh`.

### Clients — `/clients/{clientType}/...`  (`clientType` ∈ `users`, `providers`)

| Method | Path                               | Auth | Description |
|--------|------------------------------------|------|-------------|
| POST   | `/clients/{clientType}/create`     | 🔓   | Register an account |
| POST   | `/clients/{clientType}/login`      | 🔓   | Authenticate, receive a token |
| POST   | `/clients/{clientType}/logout`     | 🔒   | Invalidate the session |
| POST   | `/clients/{clientType}/suspend`    | 🔒   | Suspend an account |
| POST   | `/clients/{clientType}/activate`   | 🔒   | Re-activate an account |
| POST   | `/clients/{clientType}/read`       | 🔒   | Read account data |
| PUT    | `/clients/{clientType}/update`     | 🔒   | Update an account |
| DELETE | `/clients/{clientType}/delete`     | 🔒   | Delete an account |
| POST   | `/clients/{clientType}/search`     | 🔒   | Search accounts |
| GET    | `/clients/{clientType}/services`   | 🔒   | List the account's services |

### Services — `/services/{serviceType}/...`  (`serviceType` ∈ `clinics`, `pharmacies`, `laboratories`, `radiologycenters`)

| Method | Path                              | Auth | Description |
|--------|-----------------------------------|------|-------------|
| POST   | `/services/{serviceType}/create`  | 🔒   | Create a service |
| POST   | `/services/{serviceType}/read`    | 🔒   | Read a service |
| PUT    | `/services/{serviceType}/update`  | 🔒   | Update a service |
| DELETE | `/services/{serviceType}/delete`  | 🔒   | Delete a service (owner only) |
| POST   | `/services/{serviceType}/search`  | 🔒   | Search services |

### Staff — `/services/{serviceType}/staff/...`

| Method | Path                                    | Auth | Description |
|--------|-----------------------------------------|------|-------------|
| POST   | `/services/{serviceType}/staff/invite`  | 🔒   | Invite someone to the staff |
| POST   | `/services/{serviceType}/staff/add`     | 🔒   | Add a staff member |
| POST   | `/services/{serviceType}/staff/remove`  | 🔒   | Remove a staff member |

### Clinic records — `/clinic/{resource}/...`

Each resource supports `create` (POST), `read` (POST), `update` (PUT) and `delete` (DELETE) under `/clinic/{resource}/`, all 🔒. Resources:

| Resource        | Description |
|-----------------|-------------|
| `patients`      | Patient records (also `POST /clinic/patients/search` and `GET /clinic/patients/getvisits`) |
| `health`        | A patient's health history |
| `patientdrugs`  | A patient's drug list |
| `reports`       | A patient's reports |
| `visits`        | Visits |
| `requests`      | Investigation requests for a visit |
| `prescriptions` | Prescriptions for a visit |
| `paidservices`  | Paid services for a visit |

`delete` is not supported for `health`, `patientdrugs`, `reports`, `requests`, `prescriptions` and `paidservices`; they are removed with their parent record.

### Appointments — `/appointments/clinics/...`

| Method | Path                              | Auth | Description |
|--------|-----------------------------------|------|-------------|
| POST   | `/appointments/clinics/create`    | 🔒   | Create an appointment |
| POST   | `/appointments/clinics/read`      | 🔒   | Read an appointment |
| PUT    | `/appointments/clinics/update`    | 🔒   | Update an appointment |
| DELETE | `/appointments/clinics/delete`    | 🔒   | Delete an appointment |

### Misc

| Method | Path     | Auth | Description |
|--------|----------|------|-------------|
| GET    | `/hello` | 🔓   | Health check |
| GET    | `/doc`   | 🔓   | Interactive API documentation (Swagger UI) |

> Unknown routes return a JSON `404` (`"Endpoint not found."`). Rate-limit, ban and blacklist replies from the DoS filter are plain text.

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
