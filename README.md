# Project Valhalla

A high-performance, multi-tenant healthcare backend written in modern C++20. Valhalla exposes a versioned REST API for managing medical service providers (clinics, laboratories, pharmacies, radiology centers), their staff, patients, visits, and appointments — with authentication, role-based permissions, and built-in abuse protection baked into the request pipeline.

> **Status:** Active development. The API surface and data model are still evolving (see [`Todo.txt`](Todo.txt)).

---

## Quick Start

Run the server with its own bundled PostgreSQL in a container, using Docker or Apple Container (Apple Silicon Macs). You need one of them installed, plus `curl` (and `python3` with Apple Container). The tool checks and tells you what's missing.

**1. Install the `vdt` command** — the Valhalla Deployment Tool (no checkout needed):

```bash
curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
```

This downloads the latest tool from this repository: the `vdt` command goes in `~/.local/bin` and its files in `~/.local/share/vdt`. It adds `~/.local/bin` to `PATH` (if it isn't already) and tab completion in `~/.zshrc` and/or `~/.bashrc`. Open a new terminal afterwards.

**2. Run it:**

```bash
vdt run       # pull the image and start the server on http://127.0.0.1:8080
vdt status    # is it up, which version, and are there updates?
vdt doc       # open the interactive API docs in your browser
```

**3. Day to day:**

```bash
vdt update    # newest image, same settings, and the newest vdt tool; frees the old image's disk space
vdt show      # follow the log
vdt psql      # SQL shell on the bundled database
vdt dump      # save the database to ~/.local/share/vdt/dumps
vdt update --persist   # keep the database across stop/restart/update from now on
vdt test      # run the end-to-end API tests against it
vdt payload   # browse the JSON request payloads the tests send (fzf); --filled shows one with sample values, --raw prints the template for curl
vdt stop      # stop it; vdt run starts it again
vdt help      # everything else
```

Settings: the installer creates `~/.local/share/vdt/config` (`.env` format) with defaults for a small local container (2 server threads, 2 database connections, and raised rate limits so the API docs' Benchmark and Test console panels don't get you blocked). Every variable in it is passed to the server (see [Configuration](#configuration)); edit it and run `vdt update` to apply. A `.env` in the directory you run `vdt` from is used instead (e.g. in your checkout of this repository).

Update the tool with `vdt install` (or `vdt install v1.2.3` for a specific tag): it downloads the new version first, and only then removes the current copy and installs, so a failed download changes nothing. `vdt version` shows what's installed. Remove it with `vdt uninstall`, which works offline. Both keep your `.env` and `dumps/`.

From a checkout of this repository, `./valhalla.sh` is the same tool (the installed `vdt` command is this file under another name). Details: [Manage it with `valhalla.sh`](#manage-it-with-valhallash). Without the tool: [Run with Docker](#run-with-docker). To build and run natively: [Getting Started](#getting-started).

---

## Table of Contents

- [Quick Start](#quick-start)
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
    - [Manage it with `valhalla.sh`](#manage-it-with-valhallash)
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
- **Role-based access control** — bitmask permission levels (`CAN_READ`, `CAN_WRITE`, `CAN_DELETE`, `IS_ADMIN`, `IS_OWNER`, `IS_SUPER`) enforced by a permission manager.
- **Security gatekeeper** layered into the request flow:
  - DoS / rate-limiting detector with configurable rate limits, ban durations, and IP whitelist/blacklist.
  - Every SQL value is a bound parameter (`pqxx::params`); identifiers come from schema-validated keys or allowlists.
  - Password hashing with `libsodium` (Argon2id), off the request threads.
  - Session and token managers.
- **PostgreSQL persistence** via `libpqxx`, with a connection pool, a connection watchdog, and a database schema validator.
- **CORS support** with pre-routing OPTIONS handling and standard CORS headers on every response.
- **Structured logging** through `spdlog`, to console and/or file.
- **12-factor configuration** — every setting is read from environment variables with sensible defaults.

## Architecture

Requests flow through a layered pipeline. Drogon's IO threads only parse, route and answer; everything that can block (database calls, password hashing) runs on separate bounded worker pools.

```
HTTP request
   │
   ▼
Drogon IO threads (SERVER_THREADS)
   ├─ CORS pre-routing advice (handles OPTIONS)
   ├─ Rate-limit / DoS filter       (in memory)
   ├─ Token + session filter        (in memory, SECURE routes only)
   └─ Route (api/v2/routes): picks the controller, copies what it needs from the request
   │
   ▼  submit()  ── queue full ──►  503 + Retry-After
DB executor (DB_WORKERS threads, queue DB_QUEUE_MAX)
   Controller (controllers/*)  ──►  permission check (RBAC, cached)  ──►  Entity (entities/*)
   │
   ▼
Database controller  ──►  Connection pool (DB_MAX_CONN)  ──►  PostgreSQL
   │                      └─ no free connection within DB_ACQUIRE_TIMEOUT_MS ──►  503

Login, client create and client update hand their password work on to:
Password worker (PASSWORD_WORKERS threads, queue PASSWORD_QUEUE_MAX)  ──►  Argon2id hash / verify
   (a full queue answers 503)
```

- **Routes** (`src/api/v2/routes`) declare endpoints and mark each as `SECURE` or `INSECURE`. A small registry maps a path parameter (e.g. `clientType`, `serviceType`) to the concrete controller, so one route class serves many entity types. `executeControllerMethod` submits the whole controller call to the DB executor, with the request data, arguments and response callback captured by value, because the answer is sent from a worker thread.
- **Controllers** (`src/controllers`) implement CRUD/search logic against a generic base.
- **Entities** (`src/entities`) model the domain: people (users, providers), services (clinics, laboratories, pharmacies, radiology centers), patients, visits, and appointments.
- **Gatekeeper** (`src/gatekeeper`) holds the cross-cutting security concerns: DoS detector, token and session managers, the permission manager with its idle-expiry cache, and password hashing (Argon2id, with legacy scrypt hashes still accepted at login).
- **Concurrency** (`src/concurrency`) has `BoundedExecutor`, the fixed-size thread pool with a bounded queue that `submit()` never blocks on. The DB executor (`src/database/dbexecutor`) and the password worker are two instances. With zero workers a task runs inline, which the unit tests use.
- **Database** (`src/database`) has the connection pool (`BlockingPool`, waits with a timeout and throws `DatabaseBusy`, which becomes 503), the watchdog that reconnects dropped connections, and the handler that borrows a connection for one query.
- **Configurator** (`src/configurator`) loads and validates configuration from the environment at startup.
- **Store** (`src/store`) provides shared singleton object access. Objects built for every request use `Store::cachedObject<T>()`, which keeps a per-thread pointer and only goes back to the Store when an object has been replaced (tests replace the database controller with a fake).

## Tech Stack

| Area             | Technology                              |
| ---------------- | --------------------------------------- |
| Language         | C++20 (C17 for C sources)               |
| Web framework    | Drogon + Trantor                        |
| Database         | PostgreSQL via libpqxx                  |
| Auth             | jwt-cpp                                 |
| Crypto / hashing | libsodium                               |
| JSON             | jsoncons, picojson (jsoncpp via Drogon) |
| Config           | Environment variables                   |
| Logging          | spdlog / fmt                            |
| HTTP client      | libcurl, c-ares                         |
| Hashing / utils  | xxhash, cppcodec, zlib                  |
| Testing          | Catch2                                  |
| Build            | CMake (≥ 4.0) + Conan 2                 |

## Getting Started

### Prerequisites

- A C++20-capable compiler (GCC 13+ / Clang 17+)
- [CMake](https://cmake.org/) ≥ 4.0 and [Ninja](https://ninja-build.org/) (used by `build.sh`)
- [Conan](https://conan.io/) 2.x (package manager)
- A reachable [PostgreSQL](https://www.postgresql.org/) instance
- _(optional)_ `ccache` — used automatically if found, to speed up rebuilds
- _(optional)_ Docker or Apple `container`, to run the all-in-one image (see [Run with Docker](#run-with-docker))

Supported distributions: **Debian 13 (trixie)+ / Ubuntu 24.04+** and **Fedora 40+**. Older releases ship GCC < 13, and most distributions still package CMake 3.x, so you will likely need to install CMake 4 separately; see the notes below.

### Build from Source

#### Debian / Ubuntu

```bash
# 1. Toolchain and build helpers (Conan compiles most dependencies from source)
sudo apt update
sudo apt install -y build-essential git curl zip unzip tar pkg-config \
    cmake ninja-build ccache make autoconf automake libtool bison flex perl \
    uuid-dev libssl-dev python3 python3-pip pipx \
    postgresql postgresql-client

# 2. Check versions: you need GCC >= 13 and CMake >= 4.0
g++ --version | head -1
cmake --version | head -1
```

> The packaged `cmake` is 3.x on Ubuntu 24.04 (3.28) and Debian 13 (3.31). Install CMake 4 with `pipx install cmake` (then `pipx ensurepath` and open a new shell), or from the [Kitware APT repository](https://apt.kitware.com/). If `g++` is older than 13, install `g++-13` (`sudo apt install g++-13`) and export `CC=gcc-13 CXX=g++-13` before the steps below.

#### Fedora

```bash
# 1. Toolchain and build helpers
sudo dnf install -y gcc gcc-c++ make git curl zip unzip tar pkgconf-pkg-config \
    cmake ninja-build ccache autoconf automake libtool bison flex perl-core \
    libuuid-devel openssl-devel python3 python3-pip pipx \
    postgresql-server postgresql

# 2. Check versions: you need GCC >= 13 and CMake >= 4.0
g++ --version | head -1
cmake --version | head -1
```

> If `cmake --version` is below 4.0, install CMake 4 with `pipx install cmake` (then `pipx ensurepath` and open a new shell).

#### Install Conan and build (both distributions)

```bash
pipx install conan
pipx ensurepath          # then open a new shell so ~/.local/bin is on PATH
conan profile detect --force
```

`conan profile detect` produces a profile with `compiler.libcxx=libstdc++11`, which is what you want on Linux. The project builds as GNU C++20, so the commands below pass `-s compiler.cppstd=gnu20`.

Install dependencies and configure both build types:

```bash
# Resolve and build third-party dependencies (the first run takes a while)
TRANTOR_FLAGS='trantor/*:tools.build:cxxflags=["-Wno-error=unused-result"]'
conan install . --build=missing -s build_type=Debug   -s compiler.cppstd=gnu20 -c "$TRANTOR_FLAGS"
conan install . --build=missing -s build_type=Release -s compiler.cppstd=gnu20 -c "$TRANTOR_FLAGS"

# Configure CMake
cmake -S . -B build/Debug   -DCMAKE_BUILD_TYPE=Debug   -G Ninja
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -G Ninja

# Compile
cmake --build build/Debug   -j"$(nproc)"
cmake --build build/Release -j"$(nproc)"
```

Or run the convenience script `./build.sh`, which goes further than the steps above:

1. Runs the same Conan, CMake and build steps for Debug and Release.
2. Runs the unit tests for both (`ctest --preset conan-release` and `ctest --preset conan-debug`).
3. **Starts the Release server in the background** (`./run_release.sh &`) and runs the API test suite (`./test_api.sh`) against it.

Step 3 needs a running, initialised PostgreSQL (see [Database setup](#database-setup)), and it leaves the server running afterwards. Stop it with `pkill -f build/Release/server`.

```bash
./build.sh
```

The resulting binary is named `server` and is placed in the corresponding build directory (`build/Release/server`).

### Database setup

The server needs a PostgreSQL database initialised with `init.sql`. Skip this section if you already have a database, or if you use the all-in-one Docker image (see [Run with Docker](#run-with-docker)).

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

Create a `.env` file in the project root. It is read by `run_release.sh` and ignored by Git, so local secrets stay out of commits. See [Configuration](#configuration) for every variable:

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

To use a different file, run `ENV_FILE=/path/to/file ./run_release.sh`. Or run the binary directly with the variables exported in your shell: `./build/Release/server`.

Verify it is up:

```bash
curl http://127.0.0.1:8080/api/v2/hello
```

Then open the interactive API docs at <http://127.0.0.1:8080/api/v2/doc>. If the port is not reachable from another machine, open it in the firewall (`sudo ufw allow 8080/tcp` on Debian/Ubuntu, `sudo firewall-cmd --add-port=8080/tcp --permanent && sudo firewall-cmd --reload` on Fedora).

### Run with Docker

Every release tag publishes an all-in-one image to Docker Hub: the server **plus** its own PostgreSQL, so no external database is needed. It is a single multi-arch image (`linux/amd64` and `linux/arm64`); Docker and Apple Container pull the right one automatically. It's meant for demos, testing, and CI.

> **Which should I use?** The Docker image works on any OS. The Alpine tarballs attached to each GitHub Release run only on Alpine Linux. Anywhere else, without Docker, [build from source](#build-from-source).

> **The database is ephemeral.** Every container start, _including a restart_, begins with a fresh, empty database initialised from `init.sql`. Nothing is kept. For persistent data, run the server against your own PostgreSQL instead (see [Run the server](#run-the-server)).

**Docker** (Linux, macOS, Windows):

```bash
docker run --rm -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest
```

**Apple Container** (Apple Silicon Macs):

```bash
container run --rm -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest
```

`MAX_REQUESTS` and `MAX_FPS` raise the DoS protection limits (defaults `10000` and `100`) so the **Benchmark** and **Test console** panels in the API docs don't get your IP rate-limited or banned. Leave them out for normal use.

Then check it is up:

```bash
curl http://127.0.0.1:8080/api/v2/hello
```

The interactive API docs are at <http://127.0.0.1:8080/api/v2/doc>.

**Pin a version** instead of `latest`. Every release tag, including pre-releases such as `1.2.3-beta`, is published under its own name and also moves `latest`:

```bash
docker pull dr3mro/valhalla-server-run:v1.2.3
container image pull dr3mro/valhalla-server-run:v1.2.3
```

**Give it a name to launch later.** Create the container once, with your settings, then start and stop it by name. Leave out `--rm`, or the container is deleted when it stops.

Docker:

```bash
# one time: create it with your settings, without starting it
docker create --name valhalla -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest

docker start valhalla         # launch (add -a to attach to its output)
docker logs -f valhalla       # follow logs
docker stop valhalla          # stop
docker rm valhalla            # delete it, e.g. to change settings
```

Apple Container (same commands):

```bash
container create --name valhalla -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest

container start valhalla
container logs -f valhalla
container stop valhalla
container rm valhalla
```

- The name keeps everything you gave `create` (port, variables, image). To change any of it, `rm` the container and `create` it again.
- The database is still wiped on every `start`, so a named container doesn't keep data between launches.
- `start` reuses the image the container was created from; see [Update to the latest version](#update-to-the-latest-version) below.
- If you'd rather type a single word, add a shell alias to `~/.zshrc` or `~/.bashrc`, then just run `valhalla` (Ctrl+C to stop):

  ```bash
  alias valhalla='docker run --rm -p 8080:8080 -e TOKEN_SECRET=change-me-to-a-long-random-string -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 dr3mro/valhalla-server-run:latest'
  ```

#### Update to the latest version

Docker and Apple Container don't update images on their own; `latest` on your machine is whatever you last pulled. Pull the new image, then recreate the container from it. Nothing is lost, because the database is wiped on every start anyway.

Docker:

```bash
docker pull dr3mro/valhalla-server-run:latest

# if you use a named container (see above), recreate it from the new image
docker stop valhalla
docker rm valhalla
docker create --name valhalla -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest
docker start valhalla

docker image prune -f         # optional: remove the old, now-untagged image
```

Apple Container:

```bash
container image pull dr3mro/valhalla-server-run:latest

container stop valhalla
container rm valhalla
container create --name valhalla -p 8080:8080 \
  -e TOKEN_SECRET=change-me-to-a-long-random-string \
  -e MAX_REQUESTS=1000000 -e MAX_FPS=3000000 \
  dr3mro/valhalla-server-run:latest
container start valhalla

container image prune         # optional: remove unused images
```

Or let [`valhalla.sh`](#manage-it-with-valhallash) do all of it: `./valhalla.sh update`.

If you run with `--rm` or the shell alias, the pull is all you need: the next `run` uses the new image. To see which version is running, check the `Version` line in the startup banner (`docker logs valhalla` or `container logs valhalla`).

**Settings.** Every variable in [Configuration](#configuration) can be passed with `-e`. Inside this image:

| Variable                              | Default                              | Notes                                                                                       |
| ------------------------------------- | ------------------------------------ | ------------------------------------------------------------------------------------------- |
| `TOKEN_SECRET`                        | _(random per start)_                 | Set it if tokens must survive a restart or be shared with other services                    |
| `DB_NAME` / `DB_USER` / `DB_PASS`     | `postgres` / `postgres` / `postgres` | Created inside the container on start; any value works, quotes included                     |
| `DB_HOST`                             | `127.0.0.1`                          | Fixed: the image always uses its bundled database                                           |
| `SERVER_PORT`                         | `8080`                               | If you change it, change the `-p` mapping to match, e.g. `-p 9000:9000 -e SERVER_PORT=9000` |
| `FRONTEND_HOST` / `EMAIL_SENDER_HOST` | Docker-network IPs                   | Set these if you use invite links or email                                                  |

Details:

- The server runs as the non-root user `valhalla`; PostgreSQL only listens inside the container (`127.0.0.1`).
- The image has a health check (`GET /api/v2/hello`), so `docker ps` shows `healthy` once it is ready, typically a few seconds after start.
- If `TOKEN_SECRET` is not set, the container generates a random one on each start (and says so in the log) rather than using the insecure built-in default.

**Build the image yourself** from the repository root:

```bash
docker build -f docker/Dockerfile -t valhalla-server-run:local .
container build -f docker/Dockerfile -t valhalla-server-run:local .
```

The build compiles every C++ dependency from source on Alpine and runs the unit tests; the build fails if a test fails. The first build takes a long time; later builds reuse the cached dependency layer until `conanfile.txt` or `conan.lock` changes.

**Release downloads.** Each GitHub Release also attaches `server-alpine-amd64.tar.gz` and `server-alpine-arm64.tar.gz`. These are built on Alpine (musl libc) and only run on Alpine-based systems; everywhere else use the Docker image, or [build from source](#build-from-source).

#### Manage it with `valhalla.sh`

`valhalla.sh` in the repository root wraps the commands above for both Docker and Apple Container. It picks Apple Container when it's installed and Docker otherwise (force one with `--runtime docker` or `--runtime container`).

**Install it as the `vdt` command** (no checkout needed). This downloads the latest `valhalla.sh`, the API test scripts and their payloads from this repository. The command goes in `~/.local/bin/vdt` (a link); the files go in `~/.local/share/vdt`, without the `.sh` extension (`vdt`, `test_api`, `schemathesis_test`) and executable, plus a copy of the installer. It adds `~/.local/bin` to `PATH` (if it isn't already) and tab completion in `~/.zshrc` and/or `~/.bashrc`:

```bash
curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
```

Then open a new terminal and use `vdt` instead of `./valhalla.sh` (e.g. `vdt run`, `vdt status`). Afterwards the tool manages itself with the same installer. The installer keeps a copy of itself next to the tool, so `uninstall` works offline; `install` downloads the latest installer first, then uninstalls the current copy and installs the new one (if the download fails, nothing is removed):

```bash
vdt install            # update to the latest version (your config and dumps/ are kept)
vdt install v1.2.3     # a specific tag, branch or commit
vdt uninstall          # remove it and its lines in ~/.zshrc / ~/.bashrc (asks first)
```

The installed copy has everything except `build`, which needs a full checkout of the repository. From a checkout, every command works the same as `./valhalla.sh <command>`:

```bash
./valhalla.sh run             # create (pulling the image if needed) or start the container
./valhalla.sh update          # pull :latest, recreate the container with the same settings, free old images
./valhalla.sh update v1.2.3   # same, for a specific tag
./valhalla.sh build           # build the image from this checkout and run it
./valhalla.sh stop            # stop it without removing it (run starts it again)
./valhalla.sh restart         # restart it (the database is wiped, as on every start)
./valhalla.sh status          # state, image, port, health and version
./valhalla.sh outdated        # is there a newer image on Docker Hub?
./valhalla.sh show            # follow the log (Ctrl+C stops following, not the server)
./valhalla.sh logs 50         # last 50 log lines
./valhalla.sh stats           # live CPU and memory use
./valhalla.sh env             # what a new container would get from .env, secrets hidden
./valhalla.sh doc             # open the API docs in the browser
./valhalla.sh psql            # psql on the bundled database
./valhalla.sh dump            # save the database to ~/.local/share/vdt/dumps
./valhalla.sh restore FILE    # load a dump back (e.g. after restart or update)
./valhalla.sh shell           # a shell inside the container
./valhalla.sh test            # run test_api.sh against it (test fuzz / test all for Schemathesis)
./valhalla.sh secret          # write a random TOKEN_SECRET to .env
./valhalla.sh install         # install or update the `vdt` command from GitHub
./valhalla.sh uninstall       # remove the installed `vdt` command
./valhalla.sh delete          # remove the container (asks first)
./valhalla.sh clean           # remove the container and all local images of it (asks first)
./valhalla.sh version         # the tool's version
```

Options go before or after the command: `--name`, `--port`, `--runtime`, `--env-file`, `-n`/`--dry-run` (see what any command would change, without changing anything), `-y`/`--yes` and `-p`/`--persist` / `--no-persist` (see Persistence below). The first time you run it, it adds tab completion to `~/.zshrc` and/or `~/.bashrc`; open a new terminal to use it (set `VALHALLA_NO_COMPLETION=1` to skip).

Each command first checks the tools it needs (`curl`, `python3` for Apple Container, `jq` for `test`, `fzf` for `payload`, …) and lists any that are missing, with how to install them.

Every `run`, `update` and `restart` waits until `/api/v2/hello` answers and prints the version line from the log.

It passes every variable in a settings file to the container, using the first it finds: `.env` in the directory you run it from, `.env` next to the script (your checkout), or `~/.local/share/vdt/config` (same format). The installer creates that last one with defaults for a small local container: `SERVER_THREADS = 2`, `DB_MAX_CONN = 2`, and raised `MAX_REQUESTS`/`MAX_FPS`. It's yours from then on: kept on update, and on uninstall unless it's still the unedited default. The script itself hard-codes no server settings; anything no `.env` sets falls back to the server's defaults (4 threads, 10 connections, `MAX_REQUESTS` 10000, `MAX_FPS` 100). Only one file is used; they aren't merged. Variables set in the shell win over `.env` for a single run (`SERVER_THREADS=4 ./valhalla.sh update`). Quoted values, spaces around `=` and ` # comments` are fine. After editing `.env`, run `./valhalla.sh update` to apply it. Use another file with `--env-file path` (or `VALHALLA_ENV_FILE`), or skip it with `--env-file none`.

**Disk space.** After `update` and `build`, the tool removes its own older images that no container uses: never the image the new container runs, and never the `-deps` build image. It then frees leftover layers. On Docker that's only the dangling images that came from this repository; Apple Container can't tell where an image came from, so there it runs `container image prune`, which removes only dangling (untagged, unused) images. It reports the image disk usage before and after. `-n` shows what it would remove, and `--no-prune` (or `VALHALLA_NO_PRUNE=1`) keeps everything.

**Update notices.** Every few hours (`VALHALLA_UPDATE_INTERVAL`, default 6 hours) a background job checks, when you're online, whether Docker Hub has a newer image than your container runs and whether GitHub has a newer version of the tool than your installed copy. The next commands then show a small table with the installed and latest versions and the command that updates them: `vdt update` covers both — the image and the `vdt` tool itself (it runs the installer after the container is recreated). `vdt install` only updates the tool, and `vdt update --image-only` / `--tool-only` pick one side. The check never delays a command. The exception is `vdt status`, which checks right away when there's an internet connection (a second or two) and ends with an `Updates:` line: up to date, available (with the table), or not checked when offline. The table only appears in an interactive terminal, styled as far as it supports: colors, Unicode box drawing, emoji, or Nerd Font icons at level 2 (`VALHALLA_NERD=1` forces them). Turn it off with `VALHALLA_NO_UPDATE_CHECK=1`. A checkout, or a copy installed from one, isn't checked for tool updates.

**Login token secret.** `TOKEN_SECRET` signs the JWT login tokens, and the tool never lets it change by itself between starts (which would log everyone out). The installer generates one into `~/.local/share/vdt/config`. A container gets the first of: `--token-secret`, `TOKEN_SECRET` in the shell, the `.env` in use, `~/.local/share/vdt/config` (checked even when a `.env` is in use), or the replaced container's secret. If there's none, one is generated once and saved to `config`. `vdt env` shows which one is used, and `vdt secret` replaces it (asking first).

**Persistence.** Without it, the bundled database starts from `init.sql` on every container start. Add `--persist` once (`vdt run --persist`, `vdt update --persist`, or `VALHALLA_PERSIST=1` in `.env`) and the database is kept in `~/.local/share/vdt/dumps/<name>-persist.sql`, which is mounted into the container. The image's entrypoint loads it at start instead of `init.sql` and saves it whenever the container stops, including restarts outside the tool (Docker Desktop, a reboot). The tool also saves it before `stop`, `restart`, `update`, `build` and `delete` (the previous dump is kept as `.prev`), and if that save fails, nothing is stopped. The setting is stored in the container, so later commands remember it; `--no-persist` turns it off. `vdt status` shows whether it's on and when the database was last saved. Restoring a dump made by a different image prints a warning, because if that image changed the schema the old schema comes back. Images released before this feature still work: the tool restores the dump itself after the container starts.

In a terminal, `vdt` messages and `help` (or `--help`) use colors, Unicode box drawing, BMP symbols and emoji as far as the terminal supports them, and help is paged with `less` when it's longer than the screen; piped or with `NO_COLOR=1` output is plain text. Force a style with `VALHALLA_FANCY=0` (plain) to `3` (everything).

Full help (`./valhalla.sh help`):

```text
Valhalla Deployment Tool (vdt): run and manage the Valhalla server locally.
Quick start:
  run              Start the server (pulls the image on first use).
  doc              Open the API docs in the browser.
  status           Check state, health and version.
  logs [N]         Print the last N log lines (default 100).
  stop             Stop the container (run starts it again).
  update           Pull the newest image and update the tool.

  vdt [options] <command> [args]      (installed)

Container:
  run [tag]        Start the container. Creates it (pulling the image if it's
                   missing) if it doesn't exist; starts it if it's stopped.
  update [tag]     Pull the image and recreate the container with the same
                   settings (re-reading .env), then remove the old image
                   to free disk space (--no-prune keeps it); also updates
                   the vdt tool itself when GitHub has a newer one
                   (--image-only / --tool-only update just one).
  build [tag]      Build the image from this checkout (version from
                   `git describe`) as valhalla-server-run:<tag> (default
                   local), then recreate the container from it and remove
                   the old image (--no-prune keeps it).
  stop             Stop the container without removing it (run starts it).
  restart          Restart the container.
  delete           Remove the container (the image stays; asks first).
  clean            Remove the container and every local image of the repo
                   (asks first).
Inspect:
  status           Show state, image, port, health and version, and check
                   for updates now (when there's an internet connection).
  outdated         Compare the container's image with Docker Hub.
  show             Follow the container's log (Ctrl+C stops following; the
                   container keeps running).
  logs [N]         Print the last N log lines (default 100).
  stats            Live CPU and memory use.
  env              Print the variables a new container would get (secrets
                   hidden), without changing anything.
  doc              Open the API docs in the browser (fails if not running).
Database (reset on every start unless persistence is on, see --persist):
  psql             Open psql on the database inside the container.
  dump [file]      Save the database (default
                   ~/.local/share/vdt/dumps/<name>-<time>.sql).
  restore <file>   Load a dump into the running container, then restart the
                   server process so it starts from the restored data.
Other:
  shell            Open a shell inside the container.
  test [api|fuzz|all]
                   Run test_api.sh (api, default) and/or
                   schemathesis_test.sh (fuzz) against the container.
  payload [group] [name]
                   Browse the JSON request payloads test_api.sh sends
                   (interactive with fzf); with group and name, print one
                   directly (--raw prints the file as-is, --filled
                   substitutes sample values for the placeholders).
  secret           Generate a new random TOKEN_SECRET and save it where it's
                   read from (normally ~/.local/share/vdt/config;
                   asks before replacing one, which logs everyone out).
  version          Show the tool's version.
  completion [zsh|bash]
                   Print the shell completion script.
  help             Show this help.
Tool (both run install.sh):
  install [ref]    Install or update the `vdt` command (~/.local/bin,
                   files in ~/.local/share/vdt; an installed copy
                   updates itself in place — `update` does this too).
                   Downloads first; only then
                   removes the current copy and installs, so a failed
                   download changes nothing. ref: branch, tag or commit
                   (default main).
  uninstall        Remove the installed `vdt` command and its lines in
                   ~/.zshrc / ~/.bashrc (asks first; keeps config, dumps/).
                   Uses the installer copy kept in the install directory,
                   so it works offline; downloads it only if missing.

tag defaults to "latest" (build: "local").

Options (before or after the command):
  --name NAME          container name     (env VALHALLA_NAME, default valhalla)
  --port PORT          host port          (env VALHALLA_PORT, default 8080;
                       for an existing container, the port it was created with)
  --runtime RT         docker | container (env VALHALLA_RUNTIME)
  --env-file FILE      settings file, or "none" (env VALHALLA_ENV_FILE)
  -n, --dry-run        show what a command would change, without changing
                       anything (read-only commands just run)
  -y, --yes            don't ask for confirmation
  -p, --persist        keep the database across stop/restart/update/build/
                       delete (see Persistence below); stored in the
                       container, so later commands remember it
  --no-persist         turn persistence off (with update/build: recreate
                       without it)
  --no-prune           update/build: keep the replaced images (env
                       VALHALLA_NO_PRUNE=1). Otherwise unused older images
                       of this tool are removed, plus dangling layers (on
                       Docker only those from this repo; Apple container's
                       prune removes all dangling images).
  --image-only         update: only the image and container, skip the tool
  --tool-only          update: only the vdt tool, leave the container
                       alone (needs no runtime or container)
  --token-secret S     JWT signing secret for a new container (wins over
                       the env file and config; visible in your shell
                       history, so prefer the config file)
  -h, --help           show this help

Other settings (environment variables):
  VALHALLA_IMAGE       image repo (default docker.io/dr3mro/valhalla-server-run)
  VALHALLA_CPUS        CPUs (default 4)
  VALHALLA_MEMORY      memory, GB (default 1)
  VALHALLA_DEPS_IMAGE  build: deps image, or "local" to build the deps from
                       source (default: <repo>-deps:<arch> from Docker Hub)
  VALHALLA_BUILD_MEMORY  build: Apple builder memory, GB (default 8)
  VALHALLA_NO_COMPLETION=1  don't add shell completion to ~/.zshrc/~/.bashrc
  VALHALLA_PERSIST=1   same as --persist when a container is created (also
                       works in the env file)
  VALHALLA_DATA_DIR    where dumps go (default ~/.local/share/vdt;
                       dumps in its dumps/ folder)
  VALHALLA_WAIT        seconds to wait for the server to answer (default 60)
  VALHALLA_NO_UPDATE_CHECK=1  don't check for or show updates
  VALHALLA_UPDATE_INTERVAL  seconds between update checks (default 21600)
  VALHALLA_NERD=0|1    use Nerd Font icons (default at level 2; 1 forces)
  VALHALLA_FANCY       output style: 0 plain, 1 colors, 2 + unicode
                       (BMP symbols), 3 + emoji (default: what the
                       terminal supports; NO_COLOR=1 also gives plain).
                       Applies to every message, report and help screen.

Persistence: the bundled database starts from init.sql on every container
start. With persistence on, it's kept in
~/.local/share/vdt/dumps/<name>-persist.sql (mounted at /persist):
the image's entrypoint loads it at start instead of init.sql and saves it
when the container stops, also when that happens outside this tool (Docker
Desktop restart, reboot). This tool also saves it before stop, restart,
update, build and delete (the previous dump is kept as .prev); if that
fails, nothing is stopped. Images from before this feature fall back to the
tool restoring the dump after start. Turning it on (run/update --persist)
restores an existing saved file, or saves the current data if there's none
yet. A dump made by a different image prints a warning: if that image
changed the schema, the old schema comes back.

Updates: every few hours a background job checks (when online) whether a
newer Docker image or tool version exists; the next commands then show a
table with the installed and latest versions (in a terminal only, styled
as far as it supports). The check never delays a command.

Runtime: Apple `container` if installed, otherwise docker.

Dependencies: before running, each command checks the tools and files it
needs (e.g. curl, python3 for Apple container, jq for test, fzf for
payload, docker/Dockerfile for build) and lists every missing one with how
to install it.

Shell completion: on first use the script adds one line to ~/.zshrc and/or
~/.bashrc (whichever exist) to enable tab completion; open a new terminal to
use it. Remove the line marked "vdt completion" to undo.

Server variables: every KEY=VALUE in the env file is passed to the
container through a private env file, so values don't show up in the
process list (comments, including " # ..." after a value, and blank lines
skipped; spaces around "=" and surrounding quotes removed). The env file is
the first of: .env in the current directory, .env next to this script,
~/.local/share/vdt/config. The installer creates that file with defaults
for a small local container: SERVER_THREADS=2, DB_MAX_CONN=2, and raised
MAX_REQUESTS/MAX_FPS so the API docs' Benchmark and Test console panels
don't get your IP rate-limited. Edit it freely; it's kept on update.
MAX_REQUESTS, MAX_FPS, PERMISSION_CACHE_TTL, DB_ACQUIRE_TIMEOUT_MS, PASSWORD_WORKERS, PASSWORD_QUEUE_MAX, DB_WORKERS, DB_QUEUE_MAX, DB_IO_FASTPATH, TOKEN_SECRET, SERVER_THREADS and DB_MAX_CONN set in
the shell win over the env file for one run (e.g. SERVER_THREADS=4
./valhalla.sh update). Variables set nowhere aren't passed, so the server
uses its own defaults (in brackets below).

TOKEN_SECRET (signs the login tokens) is never generated per start. The
installer generates one into ~/.local/share/vdt/config. A container
gets the first of: --token-secret, TOKEN_SECRET in the shell, the env file
in use, ~/.local/share/vdt/config (also when a .env is in use), the
replaced container's secret; otherwise one is generated once and saved to
config. `vdt env` shows which one is used.
SERVER_PORT from the env file is the
port published inside the container (default 8080). DB_HOST and DB_SSL are
always overridden by the image, which runs its own Postgres.

Server variables the image understands (server default in brackets):
  Rate limiting / DoS
    MAX_REQUESTS          max requests per IP per PERIOD        [10000]
    PERIOD                rate-limit window, seconds            [30]
    MAX_FPS               max fingerprints per IP per PERIOD    [100]
    RL_DURATION           rate-limit duration, seconds          [30]
    BAN_DURATION          ban duration, seconds                 [3600]
    CLN_FRQ               cleanup interval, seconds             [30]
    WHITELIST             comma-separated IP patterns           []
    BLACKLIST             comma-separated IP patterns           []
  Database (the image runs its own Postgres; DB_HOST/DB_SSL are ignored)
    DB_PORT               [5432]      DB_NAME   [postgres]
    DB_USER               [postgres]  DB_PASS   [postgres]
    DB_MAX_CONN           connection pool size, keep >= SERVER_THREADS+1 [10]
    DB_ACQUIRE_TIMEOUT_MS ms to wait for a free connection, then 503   [3000]
  Caching
    PERMISSION_CACHE_TTL  idle seconds a cached permission is kept, 0 = off [300]
  Passwords (hashing runs on its own threads, never on request threads)
    PASSWORD_WORKERS      threads for hashing/login checks, keep < DB_MAX_CONN [2]
    PASSWORD_QUEUE_MAX    waiting operations before new ones get 503      [64]
  Database executor (blocking database work runs here, never on request threads)
    DB_WORKERS            threads for database work [DB_MAX_CONN]
    DB_QUEUE_MAX          waiting requests before new ones get 503 [256]
    DB_IO_FASTPATH        try each request on the IO thread first, queue it only if it needs the database [false]
  Server
    SERVER_HOST           listen address (keep 0.0.0.0)         [0.0.0.0]
    SERVER_PORT           listen port inside the container      [8080]
    SERVER_THREADS        worker threads                        [4]
    SERVER_NAME           [ProjectValhalla]
    SERVER_DESCRIPTION    [ProjectValhalla API]
    SERVER_DEBUG_LEVEL    [1]
    SERVER_LOG_TO_CONSOLE [true]      SERVER_LOG_TO_FILE  [true]
    SERVER_LOG_DIR        [./logs/]   SERVER_LOG_FILE     [server.log]
    SERVER_UPLOAD_DIR     [./uploads/]
  Tokens
    TOKEN_SECRET          JWT signing secret (image generates one if unset)
    TOKEN_VALIDITY        minutes                               [43200]
    TOKEN_ISSUER          [ProjectValhalla]
    TOKEN_TYPE            [JWS]
  Frontend (invite links)
    FRONTEND_HOST         [http://172.20.0.5]
    FRONTEND_PORT         [3000]
    FRONTEND_INVITE_PATH  [invite]
  Email sender
    EMAIL_SENDER_HOST     [http://172.20.0.6]
    EMAIL_SENDER_PORT     [5000]
    EMAIL_SENDER_QUEUE_PATH [/enqueue]

Note: without persistence the database lives only inside the container and
is reset on every start.
```

## Configuration

All configuration is supplied through environment variables; defaults live in `src/configurator/defaults.hpp`. The frontend and email-sender defaults point at fixed Docker-network IPs (`172.20.0.5`/`.6`), and the token secret and database credentials are development values, so override them for any real deployment.

### Server

| Variable             | Default               | Description                                |
| -------------------- | --------------------- | ------------------------------------------ |
| `SERVER_HOST`        | `0.0.0.0`             | Bind address                               |
| `SERVER_PORT`        | `8080`                | Listen port                                |
| `SERVER_THREADS`     | `4`                   | Worker thread count                        |
| `SERVER_UPLOAD_DIR`  | `./uploads/`          | File upload directory                      |
| `SERVER_NAME`        | `ProjectValhalla`     | Name shown in the startup banner           |
| `SERVER_VERSION`     | `1.0.0`               | Version string shown in the startup banner |
| `SERVER_DESCRIPTION` | `ProjectValhalla API` | Description shown in the startup banner    |

### Logging

| Variable                | Default      | Description                                                              |
| ----------------------- | ------------ | ------------------------------------------------------------------------ |
| `SERVER_DEBUG_LEVEL`    | `1`          | Minimum level: `0` debug, `1` info, `2` warning, `3` error, `4` critical |
| `SERVER_LOG_TO_CONSOLE` | `true`       | Log to stdout                                                            |
| `SERVER_LOG_TO_FILE`    | `true`       | Log to a file                                                            |
| `SERVER_LOG_DIR`        | `./logs/`    | Log directory                                                            |
| `SERVER_LOG_FILE`       | `server.log` | Log file name                                                            |

### Database (PostgreSQL)

| Variable      | Default    | Description                                                                                                          |
| ------------- | ---------- | -------------------------------------------------------------------------------------------------------------------- |
| `DB_HOST`     | `0.0.0.0`  | Database host. The default only works when PostgreSQL runs on the same machine; set it explicitly (e.g. `127.0.0.1`) |
| `DB_PORT`     | `5432`     | Database port                                                                                                        |
| `DB_NAME`     | `postgres` | Database name                                                                                                        |
| `DB_USER`     | `postgres` | Database user                                                                                                        |
| `DB_PASS`     | `postgres` | Database password                                                                                                    |
| `DB_SSL`      | `false`    | Enable SSL                                                                                                           |
| `DB_MAX_CONN` | `10`       | Max pooled connections; keep it at least `SERVER_THREADS + 1` (the watchdog holds one)                                |
| `DB_ACQUIRE_TIMEOUT_MS` | `3000` | Milliseconds a request waits for a free connection before it gets `503` with `Retry-After`                       |

### Caching

| Variable               | Default | Description                                                                 |
| ---------------------- | ------- | --------------------------------------------------------------------------- |
| `PERMISSION_CACHE_TTL` | `300`   | Seconds an unused cached permission is kept; each use renews it; `0` disables |

### Passwords

| Variable             | Default | Description                                                                                          |
| -------------------- | ------- | ---------------------------------------------------------------------------------------------------- |
| `PASSWORD_WORKERS`   | `2`     | Threads that hash and verify passwords (Argon2id), so request threads are never blocked; keep it below `DB_MAX_CONN`; `0` runs on the request thread |
| `PASSWORD_QUEUE_MAX` | `64`    | Password operations that may wait for a worker; further ones get `503` with `Retry-After`            |
| `DB_WORKERS`         | `DB_MAX_CONN` | Threads that run the blocking database work (permission check, query), so request threads are never blocked by a slow query; `0` runs on the request thread |
| `DB_QUEUE_MAX`       | `256`   | Requests that may wait for a database worker; further ones get `503` with `Retry-After`              |
| `DB_IO_FASTPATH`     | `false` | Run each request on the IO thread first; requests refused without a query (bad body, permission cache) answer there, the rest move to a database worker. Turn on only if the benchmark shows a gain |

### Authentication (JWT)

| Variable         | Default                  | Description                          |
| ---------------- | ------------------------ | ------------------------------------ |
| `TOKEN_SECRET`   | _(insecure dev default)_ | **Must** be overridden in production |
| `TOKEN_VALIDITY` | `43200`                  | Token lifetime in minutes (30 days)  |
| `TOKEN_ISSUER`   | `ProjectValhalla`        | JWT issuer claim                     |
| `TOKEN_TYPE`     | `JWS`                    | JWT type                             |

### DoS detector / rate limiting

| Variable       | Default   | Description                                                                  |
| -------------- | --------- | ---------------------------------------------------------------------------- |
| `MAX_REQUESTS` | `10000`   | Max repeat-count for one fingerprint before the IP is **banned**             |
| `PERIOD`       | `30`      | Window length (seconds)                                                      |
| `MAX_FPS`      | `100`     | Max distinct fingerprints per IP before the IP is **rate-limited**           |
| `RL_DURATION`  | `30`      | Rate-limit duration (seconds)                                                |
| `BAN_DURATION` | `3600`    | Ban duration (seconds)                                                       |
| `CLN_FRQ`      | `30`      | Cleanup frequency (seconds)                                                  |
| `WHITELIST`    | _(empty)_ | IPs exempt from rate limiting and bans (comma-separated patterns, see below) |
| `BLACKLIST`    | _(empty)_ | IPs always rejected (comma-separated patterns, see below)                    |

`WHITELIST` and `BLACKLIST` entries are **regular expressions**, matched anywhere in the client IP. A `.` matches any character, and an unanchored entry matches as a substring: `10.0.0.1` also matches `10.0.0.123`. For exact matches, escape the dots and anchor the entry, e.g. `WHITELIST='^127\.0\.0\.1$,^192\.168\.1\.[0-9]+$'`.

### Frontend & email sender

| Variable                  | Default             | Description                               |
| ------------------------- | ------------------- | ----------------------------------------- |
| `FRONTEND_HOST`           | `http://172.20.0.5` | Frontend base URL (used for invite links) |
| `FRONTEND_PORT`           | `3000`              | Frontend port                             |
| `FRONTEND_INVITE_PATH`    | `invite`            | Frontend path used in staff invite links  |
| `EMAIL_SENDER_HOST`       | `http://172.20.0.6` | Email sender daemon host                  |
| `EMAIL_SENDER_PORT`       | `5000`              | Email sender daemon port                  |
| `EMAIL_SENDER_QUEUE_PATH` | `/enqueue`          | Email enqueue endpoint                    |

### Testing

| Variable              | Default | Description                                                     |
| --------------------- | ------- | --------------------------------------------------------------- |
| `VALHALLA_TEST_NO_DB` | `false` | Start without connecting to a database (used by the unit tests) |

> ⚠️ The default `TOKEN_SECRET` and database credentials are for local development only. Always set strong values before deploying.

## API Overview

All endpoints live under the `/api/v2` prefix (for example `http://127.0.0.1:8080/api/v2/hello`). Routes marked 🔒 require a valid `Authorization: Bearer <token>` header (obtained from `/clients/{type}/login`); 🔓 routes are public. Request and response bodies are JSON.

### Interactive API docs

The server ships its own API documentation, compiled into the binary:

| URL                               | What it is                                                                                                                                                                                                                                                                                                                                                                                 |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `http://<host>:<port>/api/v2/doc` | Swagger UI for every endpoint, plus an **API Guide** panel (permissions, data model, validation, errors, rate limiting, recipes, troubleshooting) and a **Test console** that runs all endpoints in order against the live server with dummy data, and a **Benchmark** panel (latency percentiles and requests/s per endpoint; writes are created with unique data and deleted afterwards) |

With the defaults that is <http://127.0.0.1:8080/api/v2/doc>; the startup banner prints the exact URL. From the page you can also download the **OpenAPI 3** spec and a **Postman collection** (also kept in `docs/openapi.json` and `docs/valhalla.postman_collection.json`).

The page is embedded into the binary, and every `cmake --build` regenerates it from `tools/docs/` first (the `valhalla_docs` target) (this needs `bash`, `python3` and Node.js/`npm`; if one is missing, the committed `docs/api.html` is embedded as is). Pass `-DVALHALLA_BUILD_DOCS=OFF` to skip regeneration. To regenerate by hand:

```bash
tools/docs/build.sh        # regenerates docs/api.html, openapi.json and the Postman collection
./run_release.sh --build   # re-embed the page and restart
```

To include real response samples in the docs, run `SAMPLES_OUT=api_tests/samples.jsonl ./test_api.sh` against a running server before `tools/docs/build.sh`.

### Clients — `/clients/{clientType}/...` (`clientType` ∈ `users`, `providers`)

| Method | Path                             | Auth | Description                   |
| ------ | -------------------------------- | ---- | ----------------------------- |
| POST   | `/clients/{clientType}/create`   | 🔓   | Register an account           |
| POST   | `/clients/{clientType}/login`    | 🔓   | Authenticate, receive a token |
| POST   | `/clients/{clientType}/logout`   | 🔒   | Invalidate the session        |
| POST   | `/clients/{clientType}/suspend`  | 🔒   | Suspend an account            |
| POST   | `/clients/{clientType}/activate` | 🔒   | Re-activate an account        |
| POST   | `/clients/{clientType}/read`     | 🔒   | Read account data             |
| PUT    | `/clients/{clientType}/update`   | 🔒   | Update an account             |
| DELETE | `/clients/{clientType}/delete`   | 🔒   | Delete an account             |
| POST   | `/clients/{clientType}/search`   | 🔒   | Search accounts               |
| GET    | `/clients/{clientType}/services` | 🔒   | List the account's services   |

### Services — `/services/{serviceType}/...` (`serviceType` ∈ `clinics`, `pharmacies`, `laboratories`, `radiologycenters`)

| Method | Path                             | Auth | Description                   |
| ------ | -------------------------------- | ---- | ----------------------------- |
| POST   | `/services/{serviceType}/create` | 🔒   | Create a service              |
| POST   | `/services/{serviceType}/read`   | 🔒   | Read a service                |
| PUT    | `/services/{serviceType}/update` | 🔒   | Update a service              |
| DELETE | `/services/{serviceType}/delete` | 🔒   | Delete a service (owner only) |
| POST   | `/services/{serviceType}/search` | 🔒   | Search services               |

### Staff — `/staff/services/{serviceType}/staff/...`

| Method | Path                                         | Auth | Description                 |
| ------ | -------------------------------------------- | ---- | --------------------------- |
| POST   | `/staff/services/{serviceType}/staff/invite` | 🔒   | Invite someone to the staff |
| POST   | `/staff/services/{serviceType}/staff/add`    | 🔒   | Add a staff member          |
| POST   | `/staff/services/{serviceType}/staff/remove` | 🔒   | Remove a staff member       |

These routes are registered with Drogon's `METHOD_ADD` (`src/api/v2/routes/staff.hpp`), which puts the controller path (`/api/v2/staff`) in front, hence the doubled `staff` segment. The interactive docs still show the shorter `/services/{serviceType}/staff/...` form, which does not match the server.

### Clinic records — `/clinic/{resource}/...`

Each resource supports `create` (POST), `read` (POST), `update` (PUT) and `delete` (DELETE) under `/clinic/{resource}/`, all 🔒. Resources:

| Resource        | Description                                                                                |
| --------------- | ------------------------------------------------------------------------------------------ |
| `patients`      | Patient records (also `POST /clinic/patients/search` and `GET /clinic/patients/getvisits`) |
| `health`        | A patient's health history                                                                 |
| `patientdrugs`  | A patient's drug list                                                                      |
| `reports`       | A patient's reports                                                                        |
| `visits`        | Visits                                                                                     |
| `requests`      | Investigation requests for a visit                                                         |
| `prescriptions` | Prescriptions for a visit                                                                  |
| `paidservices`  | Paid services for a visit                                                                  |

`delete` is not supported for `health`, `patientdrugs`, `reports`, `requests`, `prescriptions` and `paidservices`; they are removed with their parent record.

### Appointments — `/appointments/{serviceType}/...` (`serviceType` ∈ `clinics`, `pharmacies`, `laboratories`, `radiologycenters`)

| Method | Path                                 | Auth | Description           |
| ------ | ------------------------------------ | ---- | --------------------- |
| POST   | `/appointments/{serviceType}/create` | 🔒   | Create an appointment |
| POST   | `/appointments/{serviceType}/read`   | 🔒   | Read an appointment   |
| PUT    | `/appointments/{serviceType}/update` | 🔒   | Update an appointment |
| DELETE | `/appointments/{serviceType}/delete` | 🔒   | Delete an appointment |

The interactive docs currently only show the `clinics` variant.

### Misc

| Method | Path     | Auth | Description                                |
| ------ | -------- | ---- | ------------------------------------------ |
| GET    | `/hello` | 🔓   | Health check                               |
| GET    | `/doc`   | 🔓   | Interactive API documentation (Swagger UI) |

> Unknown routes return a JSON `404` (`"Endpoint not found."`). Rate-limit, ban and blacklist replies from the DoS filter are plain text.

## Project Layout

```
src/
├── api/v2/             # REST API: routes, filters, middlewares, helpers
├── controllers/        # Business logic per resource (clients, clinics, services, …)
├── entities/           # Domain models (people, services, patients, visits, appointments)
├── gatekeeper/         # Security: DoS detector, tokens, sessions, RBAC, password hashing and its worker pool
├── concurrency/        # BoundedExecutor: thread pool with a bounded queue
├── configurator/       # Environment-driven configuration + defaults
├── database/           # Connection pool, watchdog, handlers, DB executor
├── validator/          # Request + database-schema validation
├── store/              # Shared object store
├── server/             # Server bootstrap (entry point)
└── utils/              # Logging, caching, messaging, JSON helpers, thread-safe queue
tests/                  # Catch2 unit tests (+ vendored Catch CMake helpers)
docs/                   # Generated API docs: api.html (embedded), openapi.json, Postman collection
tools/
├── cmake/              # CMake helpers (HTML embedding, docs generation)
├── conan/              # Conan helper variables, lockfile update script
└── docs/               # API docs generator (build.sh, build_docs.py, Swagger UI assets)
api_tests/              # Request payloads and samples used by test_api.sh
docker/                 # All-in-one Alpine image (Dockerfile + entrypoint)
.github/                # Release workflow and Dependabot config
conan.lock              # Pinned dependency versions (all platforms)
init.sql                # Database schema
build.sh                # Full build + unit tests + API tests
run_release.sh          # Run build/Release/server with .env loaded
db_init.sh              # Load init.sql into a local PostgreSQL
test_api.sh             # End-to-end API test suite against a running server
fuzz_auth.sh, sqlmap_test.sh, schemathesis_test.sh  # Security and fuzz testing helpers
```

## Front-end development (CORS and Docker)

The API listens on `http://localhost:8080`. CORS is open: `Access-Control-Allow-Origin: *`, no credentials, methods `GET, POST, PUT, DELETE, HEAD, OPTIONS`, request headers `Content-Type, Authorization, Accept, X-Requested-With` (a preflight echoes `Access-Control-Request-Headers`), and exposed response headers `Retry-After, Content-Length`. Preflight responses are cached for 7200 seconds.

In Docker bridge mode every request arrives from one gateway IP, so the rate limiter treats all front-end traffic as a single client. On native Linux Docker that address is often `172.17.0.1`, but on Docker Desktop for macOS or Windows it is usually different. There is no existing log line that prints the peer IP, so the reliable way to learn it is to run a container with `WHITELIST=".*"` once and inspect the rate-limit rejection, or to propose (not implemented here) a `Message::DebugMessage` in the `RateLimit` filter. Do not add a debug endpoint or new logging in this change.

Whitelist entries are unanchored regular expressions, so `172.17.0.1` also matches `172.17.0.10` through `172.17.0.199`. To whitelist only the gateway, anchor the pattern. In a shell or `--env-file` value the backslashes stay literal:

```
WHITELIST='^172\.17\.0\.1$'
```

In a dotenv or compose file the same value is written without extra escaping:

```
WHITELIST=^172\.17\.0\.1$
```

Raise the dev rate limits as well (`MAX_REQUESTS`, `MAX_FPS`) if React StrictMode double requests trip the limiter. A commented example for a dev `.env` (no secrets):

```
# WHITELIST=^172\.17\.0\.1$
# MAX_REQUESTS=1000000
# MAX_FPS=1000000
```

Put these settings where the container or server actually reads them: a project `.env` for the native binary, `--env-file FILE` / `VALHALLA_ENV_FILE` for `valhalla.sh`, or `-e` / `--env-file` for `docker run`. `docker/entrypoint.sh` reads them from the process environment.

`WHITELIST` disables DoS checks for matching IPs, so it is for development only. Never whitelist a broad pattern in production.

## Development

### Known limitation: malformed requests (HTTP 400)

Drogon 1.9.14 writes the response for a malformed request line (for example `GET /bad http/1.1`) directly to the connection during parsing. That path bypasses both the custom error handler and the pre-sending advice, so these 400 responses contain no CORS headers. No supported hook adds headers there in this Drogon version, so front-end code that triggers such an error will still see a browser CORS failure.

- **Code style:** enforced via `.clang-format`; static analysis configured through `.clang-tidy` and `cppcheck.supp`.
- **Editor support:** `.clangd` provides LSP configuration; `.vscode/` and `.zed/` settings are included. `CMAKE_EXPORT_COMPILE_COMMANDS` is on, so `compile_commands.json` is generated for tooling.
- **Warnings:** built with `-Wall -Wextra -pedantic`.
- **Versioning:** the current Git tag is embedded into the binary at build time via `get_tag.sh`.
- **Dependencies:** `conan.lock` pins the exact version and recipe revision of every Conan package, for the Alpine Docker builds (amd64 and arm64) and for local macOS/Linux builds. Conan uses it automatically. To change or update dependencies, edit `conanfile.txt` if needed, run `tools/conan/update_lock.sh`, and commit both files. Don't use a plain `conan lock create .`: it locks only your own platform, and the Docker build would then fail.
- **Build options:** `-DENABLE_TESTS=OFF` skips the unit tests (default `ON`); `-DVALHALLA_BUILD_DOCS=OFF` skips regenerating the API docs (default `ON`). Debug builds on Linux enable AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer.
- **Tests:** a Catch2 suite lives in `tests/`. It runs without a database, so it works on any machine. `build.sh` runs it through the `conan-debug`/`conan-release` CTest presets, and building the Docker image runs it too, on Alpine.

```bash
# Run tests (after building)
ctest --test-dir build/Debug --output-on-failure
```

## Contributing

Contributions are welcome. Please open an issue to discuss substantial changes first, keep commits focused, and run `clang-format` before submitting a pull request. See [`Todo.txt`](Todo.txt) for current work in progress.

## License

Released under the [MIT License](LICENSE).
