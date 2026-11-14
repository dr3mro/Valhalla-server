# Project Valhalla

A high-performance, multi-tenant healthcare backend written in modern C++20. Valhalla exposes a versioned REST API for managing medical service providers (clinics, laboratories, pharmacies, radiology centers), their staff, patients, visits, and appointments — with authentication, role-based permissions, and built-in abuse protection baked into the request pipeline.

> **Status:** Active development. The API surface and data model are still evolving (see [`Todo.txt`](Todo.txt)).

---

## Quick Start

Run the server with its own bundled PostgreSQL in a container, using Docker or Apple Container (Apple Silicon Macs). You need one of them installed, plus `curl` (and `python3` with Apple Container). The tool checks and tells you what's missing.

**1. Install the `valhalla` command** (no checkout needed):

```bash
curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
```

This downloads the latest tool from this repository into `~/.bin/valhalla` and adds it to `PATH`, with tab completion, in `~/.zshrc` and/or `~/.bashrc`. Open a new terminal afterwards.

**2. Run it:**

```bash
valhalla run       # pull the image and start the server on http://127.0.0.1:8080
valhalla status    # is it up, and which version?
valhalla doc       # open the interactive API docs in your browser
```

**3. Day to day:**

```bash
valhalla update    # newest image, same settings
valhalla show      # follow the log
valhalla psql      # SQL shell on the bundled database
valhalla dump      # save the database (it's reset on every start)
valhalla test      # run the end-to-end API tests against it
valhalla stop      # stop it; valhalla run starts it again
valhalla help      # everything else
```

Settings: `valhalla` reads `.env` from the directory you run it in (e.g. your checkout of this repository), or else `~/.bin/valhalla/.env` (see [Configuration](#configuration)); every variable in it is passed to the server. Run `valhalla update` after editing it.

Update the tool with `valhalla install` (or `valhalla install v1.2.3` for a specific tag): it downloads the latest installer, removes the current copy and installs the new one. Remove it with `valhalla uninstall`, which works offline. Both keep your `.env` and `dumps/`.

From a checkout of this repository, `./valhalla.sh` is the same tool. Details: [Manage it with `valhalla.sh`](#manage-it-with-valhallash). Without the tool: [Run with Docker](#run-with-docker). To build and run natively: [Getting Started](#getting-started).

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
| JSON              | jsoncons, picojson (jsoncpp via Drogon) |
| Config            | Environment variables |
| Logging           | spdlog / fmt |
| HTTP client       | libcurl, c-ares |
| Hashing / utils   | xxhash, cppcodec, zlib |
| Testing           | Catch2 |
| Build             | CMake (≥ 4.0) + Conan 2 |

## Getting Started

### Prerequisites

- A C++20-capable compiler (GCC 13+ / Clang 17+)
- [CMake](https://cmake.org/) ≥ 4.0 and [Ninja](https://ninja-build.org/) (used by `build.sh`)
- [Conan](https://conan.io/) 2.x (package manager)
- A reachable [PostgreSQL](https://www.postgresql.org/) instance
- *(optional)* `ccache` — used automatically if found, to speed up rebuilds
- *(optional)* Docker or Apple `container`, to run the all-in-one image (see [Run with Docker](#run-with-docker))

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

> **The database is ephemeral.** Every container start, *including a restart*, begins with a fresh, empty database initialised from `init.sql`. Nothing is kept. For persistent data, run the server against your own PostgreSQL instead (see [Run the server](#run-the-server)).

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

| Variable | Default | Notes |
|----------|---------|-------|
| `TOKEN_SECRET` | *(random per start)* | Set it if tokens must survive a restart or be shared with other services |
| `DB_NAME` / `DB_USER` / `DB_PASS` | `postgres` / `postgres` / `postgres` | Created inside the container on start; any value works, quotes included |
| `DB_HOST` | `127.0.0.1` | Fixed: the image always uses its bundled database |
| `SERVER_PORT` | `8080` | If you change it, change the `-p` mapping to match, e.g. `-p 9000:9000 -e SERVER_PORT=9000` |
| `FRONTEND_HOST` / `EMAIL_SENDER_HOST` | Docker-network IPs | Set these if you use invite links or email |

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

**Install it as the `valhalla` command** (no checkout needed). This downloads the latest `valhalla.sh` (installed as `valhalla`), the API test scripts (as `test_api` and `schemathesis_test`) and their payloads from this repository into `~/.bin/valhalla`, makes them executable, and adds that directory to `PATH` with tab completion in `~/.zshrc` and/or `~/.bashrc`:

```bash
curl -fsSL https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh | sh
```

Then open a new terminal and use `valhalla` instead of `./valhalla.sh` (e.g. `valhalla run`, `valhalla status`). Afterwards the tool manages itself with the same installer. The installer keeps a copy of itself next to the tool, so `uninstall` works offline; `install` downloads the latest installer first, then uninstalls the current copy and installs the new one (if the download fails, nothing is removed):

```bash
valhalla install            # update to the latest version (your .env and dumps/ are kept)
valhalla install v1.2.3     # a specific tag, branch or commit
valhalla uninstall          # remove it and its lines in ~/.zshrc / ~/.bashrc (asks first)
```

The installed copy has everything except `build`, which needs a full checkout of the repository. From a checkout, every command works the same as `./valhalla.sh <command>`:

```bash
./valhalla.sh run             # create (pulling the image if needed) or start the container
./valhalla.sh update          # pull :latest and recreate the container with the same settings
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
./valhalla.sh dump            # save the database to dumps/
./valhalla.sh restore FILE    # load a dump back (e.g. after restart or update)
./valhalla.sh shell           # a shell inside the container
./valhalla.sh test            # run test_api.sh against it (test fuzz / test all for Schemathesis)
./valhalla.sh secret          # write a random TOKEN_SECRET to .env
./valhalla.sh install         # install or update the `valhalla` command from GitHub
./valhalla.sh uninstall       # remove the installed `valhalla` command
./valhalla.sh delete          # remove the container
./valhalla.sh clean           # remove the container and all local images of it
```

Options go before or after the command: `--name`, `--port`, `--runtime`, `--env-file`, `-n`/`--dry-run` (see what `run`, `update`, `build`, `restore`, `secret`, `install` or `uninstall` would do) and `-y`/`--yes`. The first time you run it, it adds tab completion to `~/.zshrc` and/or `~/.bashrc`; open a new terminal to use it (set `VALHALLA_NO_COMPLETION=1` to skip).

Each command first checks the tools it needs (`curl`, `python3` for Apple Container, `jq` for `test`, …) and lists any that are missing, with how to install them.

Every `run`, `update` and `restart` waits until `/api/v2/hello` answers and prints the version line from the log.

It passes every variable in `.env` to the container: the `.env` in the directory you run it from if there is one, otherwise the one next to the script (for the installed `valhalla`, `~/.bin/valhalla/.env`). That way the server runs with the same settings as a native build, including `SERVER_THREADS` and `DB_MAX_CONN`; anything `.env` doesn't set falls back to the server's defaults (4 threads, 10 connections). Variables set in the shell win over `.env` for a single run (`SERVER_THREADS=4 ./valhalla.sh update`). Quoted values and spaces around `=` are fine. After editing `.env`, run `./valhalla.sh update` to apply it. Use another file with `VALHALLA_ENV_FILE=path`, or skip it with `VALHALLA_ENV_FILE=none`.

Full help (`./valhalla.sh help`):

```text
Run the Valhalla server image locally with Docker or Apple's `container`.

  valhalla [options] <command> [args]      (installed)
  ./valhalla.sh [options] <command> [args] (from a checkout)

Container:
  run [tag]        Start the container. Creates it (pulling the image if it's
                   missing) if it doesn't exist; starts it if it's stopped.
  update [tag]     Pull the image and recreate the container with the same
                   settings (re-reading .env). Keeps the old container's
                   TOKEN_SECRET unless .env or the shell sets one.
  build [tag]      Build the image from this checkout (version from
                   `git describe`) as valhalla-server-run:<tag> (default
                   local), then recreate the container from it.
  stop             Stop the container without removing it (run starts it).
  restart          Restart the container.
  delete           Remove the container (the image stays).
  clean            Remove the container and every local image of the repo.
Inspect:
  status           Show state, image, port, health and version.
  outdated         Compare the container's image with Docker Hub.
  show             Follow the container's log (Ctrl+C stops following; the
                   container keeps running).
  logs [N]         Print the last N log lines (default 100).
  stats            Live CPU and memory use.
  env              Print the variables a new container would get (secrets
                   hidden), without changing anything.
  doc              Open the API docs in the browser (fails if not running).
Database (reset on every start, so dump before restart/update):
  psql             Open psql on the database inside the container.
  dump [file]      Save the database (default dumps/<name>-<time>.sql).
  restore <file>   Load a dump into the running container.
Other:
  shell            Open a shell inside the container.
  test [api|fuzz|all]
                   Run test_api.sh (api, default) and/or
                   schemathesis_test.sh (fuzz) against the container.
  secret           Generate a random TOKEN_SECRET and write it to the env
                   file (asks before replacing an existing one).
  completion [zsh|bash]
                   Print the shell completion script.
  help             Show this help.
Tool (both run install.sh):
  install [ref]    Install or update the `valhalla` command in
                   ~/.bin/valhalla (an installed copy updates itself in
                   place): downloads the latest installer, uninstalls the
                   current copy, then installs. ref: branch, tag or commit
                   (default main).
  uninstall        Remove the installed `valhalla` command and its lines in
                   ~/.zshrc / ~/.bashrc (asks first; keeps .env, dumps/).
                   Uses the installer copy kept in the install directory,
                   so it works offline; downloads it only if missing.

tag defaults to "latest" (build: "local").

Options (before or after the command):
  --name NAME          container name     (env VALHALLA_NAME, default valhalla)
  --port PORT          host port          (env VALHALLA_PORT, default 8080)
  --runtime RT         docker | container (env VALHALLA_RUNTIME)
  --env-file FILE      env file, or "none" (env VALHALLA_ENV_FILE)
  -n, --dry-run        show what run/update/build/restore/secret/install/
                       uninstall would do
  -y, --yes            don't ask for confirmation
  -h, --help           show this help

Other settings (environment variables):
  VALHALLA_IMAGE       image repo (default docker.io/dr3mro/valhalla-server-run)
  VALHALLA_CPUS        CPUs (default 4)
  VALHALLA_MEMORY      memory, GB (default 1)
  VALHALLA_DEPS_IMAGE  build: deps image, or "local" to build the deps from
                       source (default: <repo>-deps:<arch> from Docker Hub)
  VALHALLA_BUILD_MEMORY  build: Apple builder memory, GB (default 8)
  VALHALLA_NO_COMPLETION=1  don't add shell completion to ~/.zshrc/~/.bashrc

Runtime: Apple `container` if installed, otherwise docker.

Dependencies: before running, each command checks the tools and files it
needs (e.g. curl, python3 for Apple container, jq for test, docker/Dockerfile
for build) and lists every missing one with how to install it.

Shell completion: on first use the script adds one line to ~/.zshrc and/or
~/.bashrc (whichever exist) to enable tab completion; open a new terminal to
use it. Remove the line marked "valhalla completion" to undo.

Server variables: every KEY=VALUE in the env file (default: .env in the
current directory if there is one, otherwise .env next to this script) is
passed to the container (comments and blank lines skipped,
spaces around "=" and surrounding quotes removed). Precedence, lowest to
highest:
  built-in defaults (MAX_REQUESTS=1000000, MAX_FPS=3000000)
  < env file
  < MAX_REQUESTS / MAX_FPS / TOKEN_SECRET / SERVER_THREADS / DB_MAX_CONN
    set in the shell (e.g. SERVER_THREADS=4 ./valhalla.sh update).
Variables set nowhere aren't passed, so the server uses its own defaults.
Without any TOKEN_SECRET, update keeps the old container's and a new
container lets the image generate one. SERVER_PORT from the env file is the
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
    DB_MAX_CONN           connection pool size                  [10]
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

Note: the database lives inside the container and is reset on every start.
```

## Configuration

All configuration is supplied through environment variables; defaults live in `src/configurator/defaults.hpp`. The frontend and email-sender defaults point at fixed Docker-network IPs (`172.20.0.5`/`.6`), and the token secret and database credentials are development values, so override them for any real deployment.

### Server

| Variable             | Default          | Description |
|----------------------|------------------|-------------|
| `SERVER_HOST`        | `0.0.0.0`        | Bind address |
| `SERVER_PORT`        | `8080`           | Listen port |
| `SERVER_THREADS`     | `4`              | Worker thread count |
| `SERVER_UPLOAD_DIR`  | `./uploads/`     | File upload directory |
| `SERVER_NAME`        | `ProjectValhalla` | Name shown in the startup banner |
| `SERVER_VERSION`     | `1.0.0`          | Version string shown in the startup banner |
| `SERVER_DESCRIPTION` | `ProjectValhalla API` | Description shown in the startup banner |

### Logging

| Variable                | Default      | Description |
|-------------------------|--------------|-------------|
| `SERVER_DEBUG_LEVEL`    | `1`          | Minimum level: `0` debug, `1` info, `2` warning, `3` error, `4` critical |
| `SERVER_LOG_TO_CONSOLE` | `true`       | Log to stdout |
| `SERVER_LOG_TO_FILE`    | `true`       | Log to a file |
| `SERVER_LOG_DIR`        | `./logs/`    | Log directory |
| `SERVER_LOG_FILE`       | `server.log` | Log file name |

### Database (PostgreSQL)

| Variable       | Default        | Description |
|----------------|----------------|-------------|
| `DB_HOST`      | `0.0.0.0`      | Database host. The default only works when PostgreSQL runs on the same machine; set it explicitly (e.g. `127.0.0.1`) |
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
| `TOKEN_VALIDITY`| `43200`                              | Token lifetime in minutes (30 days) |
| `TOKEN_ISSUER`  | `ProjectValhalla`                    | JWT issuer claim |
| `TOKEN_TYPE`    | `JWS`                                | JWT type |

### DoS detector / rate limiting

| Variable        | Default | Description |
|-----------------|---------|-------------|
| `MAX_REQUESTS`  | `10000` | Max repeat-count for one fingerprint before the IP is **banned** |
| `PERIOD`        | `30`    | Window length (seconds) |
| `MAX_FPS`       | `100`   | Max distinct fingerprints per IP before the IP is **rate-limited** |
| `RL_DURATION`   | `30`    | Rate-limit duration (seconds) |
| `BAN_DURATION`  | `3600`  | Ban duration (seconds) |
| `CLN_FRQ`       | `30`    | Cleanup frequency (seconds) |
| `WHITELIST`     | *(empty)* | IPs exempt from rate limiting and bans (comma-separated patterns, see below) |
| `BLACKLIST`     | *(empty)* | IPs always rejected (comma-separated patterns, see below) |

`WHITELIST` and `BLACKLIST` entries are **regular expressions**, matched anywhere in the client IP. A `.` matches any character, and an unanchored entry matches as a substring: `10.0.0.1` also matches `10.0.0.123`. For exact matches, escape the dots and anchor the entry, e.g. `WHITELIST='^127\.0\.0\.1$,^192\.168\.1\.[0-9]+$'`.

### Frontend & email sender

| Variable                  | Default              | Description |
|---------------------------|----------------------|-------------|
| `FRONTEND_HOST`           | `http://172.20.0.5`  | Frontend base URL (used for invite links) |
| `FRONTEND_PORT`           | `3000`               | Frontend port |
| `FRONTEND_INVITE_PATH`    | `invite`             | Frontend path used in staff invite links |
| `EMAIL_SENDER_HOST`       | `http://172.20.0.6`  | Email sender daemon host |
| `EMAIL_SENDER_PORT`       | `5000`               | Email sender daemon port |
| `EMAIL_SENDER_QUEUE_PATH` | `/enqueue`           | Email enqueue endpoint |

### Testing

| Variable              | Default | Description |
|-----------------------|---------|-------------|
| `VALHALLA_TEST_NO_DB` | `false` | Start without connecting to a database (used by the unit tests) |

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

### Staff — `/staff/services/{serviceType}/staff/...`

| Method | Path                                          | Auth | Description |
|--------|-----------------------------------------------|------|-------------|
| POST   | `/staff/services/{serviceType}/staff/invite`  | 🔒   | Invite someone to the staff |
| POST   | `/staff/services/{serviceType}/staff/add`     | 🔒   | Add a staff member |
| POST   | `/staff/services/{serviceType}/staff/remove`  | 🔒   | Remove a staff member |

These routes are registered with Drogon's `METHOD_ADD` (`src/api/v2/routes/staff.hpp`), which puts the controller path (`/api/v2/staff`) in front, hence the doubled `staff` segment. The interactive docs still show the shorter `/services/{serviceType}/staff/...` form, which does not match the server.

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

### Appointments — `/appointments/{serviceType}/...`  (`serviceType` ∈ `clinics`, `pharmacies`, `laboratories`, `radiologycenters`)

| Method | Path                                   | Auth | Description |
|--------|----------------------------------------|------|-------------|
| POST   | `/appointments/{serviceType}/create`   | 🔒   | Create an appointment |
| POST   | `/appointments/{serviceType}/read`     | 🔒   | Read an appointment |
| PUT    | `/appointments/{serviceType}/update`   | 🔒   | Update an appointment |
| DELETE | `/appointments/{serviceType}/delete`   | 🔒   | Delete an appointment |

The interactive docs currently only show the `clinics` variant.

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

## Development

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
