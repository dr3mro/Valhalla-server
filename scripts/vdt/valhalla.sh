#!/usr/bin/env bash
# /**
#  * @file valhalla.sh
#  * @brief CLI for managing Valhalla Docker deployment, database and API test workflows.
#  * @details This is the one command-line tool for running the Valhalla server with Docker or Apple's container runtime.
#  *          It can build the server image, create, start, stop, restart and delete the container, inspect it, and show logs and status.
#  *          It also manages the bundled PostgreSQL database (dump and restore), shell completion, update checks and the API test suites.
#  *          Everything is driven by a single command word plus options, so the operator never has to remember long, error-prone Docker commands.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * It is the operator's main tool for building, running, stopping, deploying, inspecting and testing the server, its database, its docs and its tests.
#  * Without it there is no simple, repeatable way to launch the container with the right port, environment and persistence, so the server could not be operated or updated reliably.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash functions and case statements: https://www.gnu.org/software/bash/manual/bash.html
#  * - Docker volumes / networks: https://docs.docker.com/storage/volumes/
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Valhalla Deployment Tool (vdt): run and manage the Valhalla server locally.
# Quick start:
#   run              Start the server (pulls the image on first use).
#   doc              Open the API docs in the browser.
#   status           Check state, health and version.
#   logs [N]         Print the last N log lines (default 100).
#   stop             Stop the container (run starts it again).
#   update           Pull the newest image and update the tool.
#
#   @USAGE@
#
# Container:
#   run [tag]        Start the container. Creates it (pulling the image if it's
#                    missing) if it doesn't exist; starts it if it's stopped.
#   update [tag]     Pull the image and recreate the container with the same
#                    settings (re-reading .env), then remove the old image
#                    to free disk space (--no-prune keeps it); also updates
#                    the vdt tool itself when GitHub has a newer one
#                    (--image-only / --tool-only update just one).
#   build [tag]      Build the image from this checkout (version from
#                    `git describe`) as valhalla-server-run:<tag> (default
#                    local), then recreate the container from it and remove
#                    the old image (--no-prune keeps it).
#   stop             Stop the container without removing it (run starts it).
#   restart          Restart the container.
#   delete           Remove the container (the image stays; asks first).
#   clean            Remove the container and every local image of the repo
#                    (asks first).
# Inspect:
#   status           Show state, image, port, health and version, and check
#                    for updates now (when there's an internet connection).
#   outdated         Compare the container's image with Docker Hub.
#   show             Follow the container's log (Ctrl+C stops following; the
#                    container keeps running).
#   logs [N]         Print the last N log lines (default 100).
#   stats            Live CPU and memory use.
#   env              Print the variables a new container would get (secrets
#                    hidden), without changing anything.
#   doc              Open the API docs in the browser (fails if not running).
# Database (reset on every start unless persistence is on, see --persist):
#   psql             Open psql on the database inside the container.
#   dump [file]      Save the database (default
#                    ~/.local/share/vdt/dumps/<name>-<time>.sql).
#   restore <file>   Load a dump into the running container, then restart the
#                    server process so it starts from the restored data.
# Other:
#   shell            Open a shell inside the container.
#   test [api|fuzz|all]
#                    Run test_api.sh (api, default) and/or
#                    schemathesis_test.sh (fuzz) against the container.
#   payload [group] [name]
#                    Browse the JSON request payloads test_api.sh sends
#                    (interactive with fzf); with group and name, print one
#                    directly (--raw prints the file as-is, --filled
#                    substitutes sample values for the placeholders).
#   secret           Generate a new random TOKEN_SECRET and save it where it's
#                    read from (normally ~/.local/share/vdt/config;
#                    asks before replacing one, which logs everyone out).
#   version          Show the tool's version.
#   completion [zsh|bash]
#                    Print the shell completion script.
#   help             Show this help.
# Tool (both run install.sh):
#   install [ref]    Install or update the `vdt` command (~/.local/bin,
#                    files in ~/.local/share/vdt; an installed copy
#                    updates itself in place — `update` does this too).
#                    Downloads first; only then
#                    removes the current copy and installs, so a failed
#                    download changes nothing. ref: branch, tag or commit
#                    (default main).
#   uninstall        Remove the installed `vdt` command and its lines in
#                    ~/.zshrc / ~/.bashrc (asks first; keeps config, dumps/).
#                    Uses the installer copy kept in the install directory,
#                    so it works offline; downloads it only if missing.
#
# tag defaults to "latest" (build: "local").
#
# Options (before or after the command):
#   --name NAME          container name     (env VALHALLA_NAME, default valhalla)
#   --port PORT          host port          (env CONTAINER_PORT or CONTAINER_PORT, default 8080;
#                        for an existing container, the port it was created with)
#   --runtime RT         docker | container (env VALHALLA_RUNTIME)
#   --env-file FILE      settings file, or "none" (env VALHALLA_ENV_FILE)
#   -n, --dry-run        show what a command would change, without changing
#                        anything (read-only commands just run)
#   -y, --yes            don't ask for confirmation
#   -p, --persist        keep the database across stop/restart/update/build/
#                        delete (see Persistence below); stored in the
#                        container, so later commands remember it
#   --no-persist         turn persistence off (with update/build: recreate
#                        without it)
#   --no-prune           update/build: keep the replaced images (env
#                        VALHALLA_NO_PRUNE=1). Otherwise unused older images
#                        of this tool are removed, plus dangling layers (on
#                        Docker only those from this repo; Apple container's
#                        prune removes all dangling images).
#   --image-only         update: only the image and container, skip the tool
#   --tool-only          update: only the vdt tool, leave the container
#                        alone (needs no runtime or container)
#   --token-secret S     JWT signing secret for a new container (wins over
#                        the env file and config; visible in your shell
#                        history, so prefer the config file)
#   -h, --help           show this help
#
# Other settings (environment variables):
#   VALHALLA_IMAGE       image repo (default docker.io/dr3mro/valhalla-server-run)
#   VALHALLA_CPUS        CPUs (default 4)
#   VALHALLA_MEMORY      memory, GB (default 1)
#   VALHALLA_DEPS_IMAGE  build: deps image, or "local" to build the deps from
#                        source (default: <repo>-deps:<arch> from Docker Hub)
#   VALHALLA_BUILD_MEMORY  build: Apple builder memory, GB (default 8)
#   VALHALLA_NO_COMPLETION=1  don't add shell completion to ~/.zshrc/~/.bashrc
#   VALHALLA_PERSIST=1   same as --persist when a container is created (also
#                        works in the env file)
#   VALHALLA_DATA_DIR    where dumps go (default ~/.local/share/vdt;
#                        dumps in its dumps/ folder)
#   VALHALLA_WAIT        seconds to wait for the server to answer (default 60)
#   VALHALLA_NO_UPDATE_CHECK=1  don't check for or show updates
#   VALHALLA_UPDATE_INTERVAL  seconds between update checks (default 21600)
#   VALHALLA_NERD=0|1    use Nerd Font icons (default at level 2; 1 forces)
#   VALHALLA_FANCY       output style: 0 plain, 1 colors, 2 + unicode
#                        (BMP symbols), 3 + emoji (default: what the
#                        terminal supports; NO_COLOR=1 also gives plain).
#                        Applies to every message, report and help screen.
#
# Persistence: the bundled database starts from init.sql on every container
# start. With persistence on, it's kept in
# ~/.local/share/vdt/dumps/<name>-persist.sql (mounted at /persist):
# the image's entrypoint loads it at start instead of init.sql and saves it
# when the container stops, also when that happens outside this tool (Docker
# Desktop restart, reboot). This tool also saves it before stop, restart,
# update, build and delete (the previous dump is kept as .prev); if that
# fails, nothing is stopped. Images from before this feature fall back to the
# tool restoring the dump after start. Turning it on (run/update --persist)
# restores an existing saved file, or saves the current data if there's none
# yet. A dump made by a different image prints a warning: if that image
# changed the schema, the old schema comes back.
#
# Updates: every few hours a background job checks (when online) whether a
# newer Docker image or tool version exists; the next commands then show a
# table with the installed and latest versions (in a terminal only, styled
# as far as it supports). The check never delays a command.
#
# Runtime: Apple `container` if installed, otherwise docker.
#
# Dependencies: before running, each command checks the tools and files it
# needs (e.g. curl, python3 for Apple container, jq for test, fzf for
# payload, docker/Dockerfile for build) and lists every missing one with how
# to install it.
#
# Shell completion: on first use the script adds one line to ~/.zshrc and/or
# ~/.bashrc (whichever exist) to enable tab completion; open a new terminal to
# use it. Remove the line marked "vdt completion" to undo.
#
# Server variables: every KEY=VALUE in the env file is passed to the
# container through a private env file, so values don't show up in the
# process list (comments, including " # ..." after a value, and blank lines
# skipped; spaces around "=" and surrounding quotes removed). The env file is
# the first of: .env in the current directory, .env next to this script,
# ~/.local/share/vdt/config. The installer creates that file with defaults
# for a small local container: SERVER_THREADS=2, DB_MAX_CONN=2, and raised
# MAX_REQUESTS/MAX_FPS so the API docs' Benchmark and Test console panels
# don't get your IP rate-limited. Edit it freely; it's kept on update.
# MAX_REQUESTS, MAX_FPS, PERMISSION_CACHE_TTL, DB_ACQUIRE_TIMEOUT_MS, PASSWORD_WORKERS, PASSWORD_QUEUE_MAX, DB_WORKERS, DB_QUEUE_MAX, DB_IO_FASTPATH, DB_ASYNC, TOKEN_SECRET, SERVER_THREADS and DB_MAX_CONN set in
# the shell win over the env file for one run (e.g. SERVER_THREADS=4
# ./valhalla.sh update). Variables set nowhere aren't passed, so the server
# uses its own defaults (in brackets below).
#
# TOKEN_SECRET (signs the login tokens) is never generated per start. The
# installer generates one into ~/.local/share/vdt/config. A container
# gets the first of: --token-secret, TOKEN_SECRET in the shell, the env file
# in use, ~/.local/share/vdt/config (also when a .env is in use), the
# replaced container's secret; otherwise one is generated once and saved to
# config. `vdt env` shows which one is used.
# SERVER_PORT from the env file is the
# port published inside the container (default 8080). DB_HOST and DB_SSL are
# always overridden by the image, which runs its own Postgres.
#
# Server variables the image understands (server default in brackets):
#   Rate limiting / DoS
#     MAX_REQUESTS          max requests per IP per PERIOD        [10000]
#     PERIOD                rate-limit window, seconds            [30]
#     MAX_FPS               max fingerprints per IP per PERIOD    [100]
#     RL_DURATION           rate-limit duration, seconds          [30]
#     BAN_DURATION          ban duration, seconds                 [3600]
#     CLN_FRQ               cleanup interval, seconds             [30]
#     WHITELIST             comma-separated IP patterns           []
#     BLACKLIST             comma-separated IP patterns           []
#   Database (the image runs its own Postgres; DB_HOST/DB_SSL are ignored)
#     DB_PORT               [5432]      DB_NAME   [postgres]
#     DB_USER               [postgres]  DB_PASS   [postgres]
#     DB_MAX_CONN           connection pool size, keep >= SERVER_THREADS+1 [10]
#     DB_ACQUIRE_TIMEOUT_MS ms to wait for a free connection, then 503   [3000]
#     DB_ASYNC              async database access (Drogon DbClient) [true], false = legacy synchronous pool
#   Caching
#     PERMISSION_CACHE_TTL  idle seconds a cached permission is kept, 0 = off [300]
#   Passwords (hashing runs on its own threads, never on request threads)
#     PASSWORD_WORKERS      threads for hashing/login checks, keep < DB_MAX_CONN [2]
#     PASSWORD_QUEUE_MAX    waiting operations before new ones get 503      [64]
#   Database executor (blocking database work runs here, never on request threads)
#     DB_WORKERS            threads for database work [DB_MAX_CONN]
#     DB_QUEUE_MAX          waiting requests before new ones get 503 [256]
#     DB_IO_FASTPATH        try each request on the IO thread first, queue it only if it needs the database [false]
#   Server
#     SERVER_HOST           listen address (keep 0.0.0.0)         [0.0.0.0]
#     SERVER_PORT           listen port inside the container      [8080]
#     SERVER_THREADS        worker threads                        [4]
#     SERVER_NAME           [ProjectValhalla]
#     SERVER_DESCRIPTION    [ProjectValhalla API]
#     SERVER_DEBUG_LEVEL    [1]
#     SERVER_LOG_TO_CONSOLE [true]      SERVER_LOG_TO_FILE  [true]
#     SERVER_LOG_DIR        [./logs/]   SERVER_LOG_FILE     [server.log]
#     SERVER_UPLOAD_DIR     [./uploads/]
#   Tokens
#     TOKEN_SECRET          JWT signing secret (image generates one if unset)
#     TOKEN_VALIDITY        minutes                               [43200]
#     TOKEN_ISSUER          [ProjectValhalla]
#     TOKEN_TYPE            [JWS]
#   Frontend (invite links)
#     FRONTEND_HOST         [http://172.20.0.5]
#     FRONTEND_PORT         [3000]
#     FRONTEND_INVITE_PATH  [invite]
#   Email sender
#     EMAIL_SENDER_HOST     [http://172.20.0.6]
#     EMAIL_SENDER_PORT     [5000]
#     EMAIL_SENDER_QUEUE_PATH [/enqueue]
#
# Note: without persistence the database lives only inside the container and
# is reset on every start.

# Abort on the first failing command and propagate failures through pipes.
# -u is deliberately omitted: optional settings are read via ${VAR:-}, and an
# unset variable is the normal "not configured" state throughout this script.
set -eo pipefail  # Abort on the first failing command and treat failures in a pipe as errors; -u is left off so unset variables simply mean "not configured".

# Resolve symlinks (e.g. ~/bin/valhalla.sh -> repo) so ROOT is the repository.
SCRIPT="$0"  # Remember the path used to launch this script before resolving any symlinks.
while [[ -L $SCRIPT ]]; do
  link="$(readlink "$SCRIPT")"  # The path a symlink points at; it can be absolute or relative.
  [[ $link == /* ]] && SCRIPT="$link" || SCRIPT="$(dirname "$SCRIPT")/$link"  # Turn a relative link target into an absolute path by joining it to the link's folder.
done
SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"  # Replace SCRIPT with a single absolute path to the real file.
ROOT="$(dirname "$SCRIPT")"  # ROOT starts as the script's own folder; it is lifted to the repo root below when needed.
# When run from a checkout, the script lives under scripts/vdt; lift ROOT to the repo root.
if [[ -d "$ROOT/../../.git" ]]; then ROOT="$(cd "$ROOT/../.." && pwd)"; fi  # In a checkout the script lives under scripts/vdt, so move ROOT up two levels to the repo root.
# How the user called it (vdt, valhalla.sh, ./valhalla.sh), for messages.
PROG="$0"  # Keep the exact name the user typed; it is used in messages and help.
[[ $PROG == */* && $(command -v "$(basename "$PROG")" 2>/dev/null) == "$PROG" ]] && PROG="$(basename "$PROG")"  # If launched by a path that is on PATH, display just the bare command name.
# Called by path (e.g. the rc file's eval of the install directory): show the
# command's own name.
case "$(basename "$PROG")" in
  vdt | valhalla.sh) PROG="$(basename "$PROG")" ;;  # Known entry-point names: display the command's own name.
esac

# All tunable defaults live here so every command sees the same values;
# ${VAR:-default} also applies the default when a variable is set but empty.
IMAGE_REPO="${VALHALLA_IMAGE:-docker.io/dr3mro/valhalla-server-run}"  # Published image repository to pull from; override with VALHALLA_IMAGE.
LOCAL_REPO="valhalla-server-run"  # Image repository name used by the build command for locally built images.
NAME="${VALHALLA_NAME:-valhalla}"  # Container name; override with VALHALLA_NAME or --name.
PORT="${CONTAINER_PORT:-8080}"  # Host port to publish, from the environment or default 8080; --port overrides it.
PORT_EXPLICIT="${CONTAINER_PORT:+1}" # set: --port / CONTAINER_PORT wins over the container's port
CPUS="${VALHALLA_CPUS:-4}"  # CPU limit for a new container (default 4).
MEMORY_GB="${VALHALLA_MEMORY:-1}"  # Memory limit in gigabytes for a new container (default 1).
# One place for dumps, whether this runs from a checkout or the installed
# copy.
DATA_DIR="${VALHALLA_DATA_DIR:-$HOME/.local/share/vdt}"  # Home folder for dumps, the config file and update state.
DUMP_DIR="$DATA_DIR/dumps"  # Folder that holds database dumps and the persistence file.
WAIT_SECONDS="${VALHALLA_WAIT:-60}"  # Seconds to wait for the server to become healthy before giving up.
STOP_TIMEOUT=30 # seconds the entrypoint gets to save the database on stop
# Marker the entrypoint writes into /entrypoint.sh; its presence tells this
# tool whether the image saves/restores persistence itself or needs help.
ENTRYPOINT_MARK="valhalla-entrypoint: persist-v1"
# Inspect output is cached to a file rather than a variable: the cache must
# survive the subshells used for $(...) and be shared by all inspection helpers.
INSPECT_CACHE="${TMPDIR:-/tmp}/valhalla-inspect.$$"
ENV_TMP=""  # Path to the temporary private env file; set only while a container is being created.
# Cleanup net for early exits and failures; exec-based commands bypass the
# trap, so they call forget_inspect explicitly before exec.
trap 'rm -f "$INSPECT_CACHE" ${ENV_TMP:+"$ENV_TMP"}' EXIT

# Env file: --env-file / VALHALLA_ENV_FILE, else the first of: .env in the
# current directory, .env next to this script, $DATA_DIR/config (the
# installer creates that one with small defaults).
if [[ -n ${VALHALLA_ENV_FILE:-} ]]; then
  ENV_FILE="$VALHALLA_ENV_FILE"  # Use the explicitly requested env file (--env-file or VALHALLA_ENV_FILE).
else
  ENV_FILE=""  # Start with no file, then search the default locations below.
  for f in "$PWD/config" "$PWD/.env" "$ROOT/config" "$ROOT/.env" "$DATA_DIR/config"; do  # Check these locations in order and use the first settings file that exists.
    [[ -f $f ]] && { ENV_FILE="$f"; break; }  # Take the first existing file and stop searching.
  done
  [[ -n $ENV_FILE ]] || ENV_FILE="$PWD/config"  # If none exists, remember a default path even though the file may not exist yet.
fi
ENV_FILE_EXPLICIT="${VALHALLA_ENV_FILE:+1}"  # Marker: the user chose the file, so a missing file is an error.
DRY_RUN=0  # 1 means show what would change without changing anything (-n / --dry-run).
ASSUME_YES=0  # 1 means answer yes to every confirmation prompt (-y / --yes).
PERSIST=""     # --persist (1) / --no-persist (0); empty: keep the container's setting
CLI_TOKEN_SECRET="" # --token-secret
NO_PRUNE="${VALHALLA_NO_PRUNE:-0}" # --no-prune: keep old images after update/build
UPDATE_SCOPE=""   # --image-only / --tool-only: update just one side of update
RAW_PAYLOAD=0     # --raw: payload prints the payload file as-is
FILLED_PAYLOAD=0  # --filled: payload substitutes sample values for placeholders
OLD_PERSIST="" # VALHALLA_PERSIST of the container being replaced

# Commands: name|short description (single source for help-free tools such
# as shell completion).
COMMANDS=(
  "run|create or start the container"
  "update|pull the image, recreate the container and update the tool"
  "build|build the image from this checkout and run it"
  "stop|stop the container without removing it"
  "restart|restart the container"
  "delete|remove the container"
  "clean|remove the container and its local images"
  "status|state, image, port, health and version"
  "outdated|compare the container's image with Docker Hub"
  "show|follow the container log"
  "logs|print the last N log lines"
  "stats|live CPU and memory use"
  "env|variables a new container would get"
  "doc|open the API docs in the browser"
  "psql|psql on the bundled database"
  "dump|save the database to a file"
  "restore|load a dump into the container"
  "shell|shell inside the container"
  "test|run the API tests against the container"
  "payload|browse the JSON request payloads used by the API"
  "secret|write a random TOKEN_SECRET to the env file"
  "install|install or update the vdt command from GitHub"
  "uninstall|remove the installed vdt command"
  "version|show the tool version"
  "completion|print the shell completion script"
  "help|show help"
)

# Options: names|description|value kind (empty: flag; value, runtime, file).
# Single source for shell completion; keep in sync with the parser below.
OPTIONS=(
  "--name|container name|value"
  "--port|host port|value"
  "--runtime|docker or container|runtime"
  "--env-file|env file, or none|file"
  "-n --dry-run|show what would happen|"
  "-y --yes|don't ask for confirmation|"
  "-p --persist|keep the database across stop/restart/update|"
  "--no-persist|turn persistence off|"
  "--token-secret|JWT signing secret for this container|value"
  "--no-prune|keep old images after update/build|"
  "--image-only|update: only the image, skip the tool|"
  "--tool-only|update: only the tool, skip the image|"
  "--raw|payload: print the payload file as-is|"
  "--filled|payload: substitute sample values for placeholders|"
  "-h --help|show help|"
)

# Parsed env file (bash 3.2 has no associative arrays: parallel arrays).
ENV_KEYS=()  # Parallel array of setting names read from the env file (bash 3.2 has no associative arrays).
ENV_VALS=()  # Parallel array of values, at the same indexes as ENV_KEYS.

# usage_line: the usage line for the help, named for how the script was
# invoked (vdt when installed, valhalla.sh / ./valhalla.sh from a checkout).
# @brief Print the usage line shown in help, named for how the script was invoked.
# @return 0 on success.
usage_line() {
  if [[ -f $ROOT/VERSION ]]; then  # A VERSION file exists only in an installed copy, not in a source checkout.
    printf '%s [options] <command> [args]      (installed)' "$PROG"  # Label the invocation as an installed copy.
  else
    printf '%s [options] <command> [args] (from a checkout)' "$PROG"  # Label the invocation as running straight from a checkout.
  fi
}

# help_text: the help from this script's header, with the usage line filled
# in for the current invocation.
# NOTE: help_text harvests lines 2 up to the first blank line of this file, so the expanded
# Doxygen header block at the very top now also shows up in the rendered help output.
# @brief Emit the script header help text with @USAGE@ replaced by the current usage line.
# @return 0 on success.
help_text() {
  local u  # Holds the usage line after escaping it for sed.
  u="$(usage_line | sed -e 's/[&\\]/\\&/g')"  # Escape & and backslash so the value can be substituted safely below.
  sed -n '2,/^$/s/^# \{0,1\}//p' "$SCRIPT" | sed "s|@USAGE@|$u|"  # Print the header comment lines, strip the leading #, and fill in the @USAGE@ placeholder.
}

# fancy_level: how much decoration the terminal can show.
#   0 plain (not a terminal, NO_COLOR, TERM=dumb), 1 colors,
#   2 colors + unicode (UTF-8 locale), 3 + emoji (a terminal known to draw
#   them; fonts can't be checked, so this goes by the terminal).
# VALHALLA_FANCY=0..3 forces a level. Takes 1 if stdout is a terminal: it
# runs inside $(...), where its own stdout is a pipe.
# @brief Determine the decoration level from the terminal and environment: 0 plain, 1 colors, 2 unicode, 3 emoji.
# @param is_tty 1 when stdout is a terminal; callers pass 0 inside command substitution.
# @return 0; prints the chosen level.
fancy_level() {
  local is_tty="${1:-0}"  # 1 only when the caller knows stdout is a real terminal (0 inside $(...)).
  if [[ -n ${VALHALLA_FANCY:-} ]]; then
    case "$VALHALLA_FANCY" in 0 | 1 | 2 | 3) echo "$VALHALLA_FANCY" ;; *) echo 0 ;; esac  # Accept only the valid forced levels; anything else means plain output.
    return  # A forced level wins, so stop here.
  fi
  [[ $is_tty == 1 && -z ${NO_COLOR:-} && ${TERM:-dumb} != dumb ]] || { echo 0; return; }  # Plain output when not a terminal, when NO_COLOR is set, or when TERM is dumb.
  local colors  # Number of colors tput reports for this terminal.
  colors="$(tput colors 2>/dev/null || echo 0)"  # Ask the terminal for its color count; assume 0 if tput cannot tell.
  [[ $colors =~ ^[0-9]+$ && $colors -ge 8 ]] || { echo 0; return; }  # At least 8 colors are needed for any decoration at all.
  local loc="${LC_ALL:-${LC_CTYPE:-${LANG:-}}}"  # Locale used to decide whether unicode (UTF-8) is available.
  if [[ ! $loc =~ [Uu][Tt][Ff]-?8 ]] && [[ "$(locale charmap 2>/dev/null)" != UTF-8 ]]; then  # Without a UTF-8 locale only colors (level 1) are safe to print.
    echo 1  # Colors but no unicode.
    return  # Done; no need to check terminal names.
  fi
  case "${TERM_PROGRAM:-}" in
    iTerm.app | Apple_Terminal | vscode | WezTerm | ghostty | Hyper | Tabby | WarpTerminal | rio | zed) echo 3; return ;;  # These terminal apps are known to draw emoji correctly.
  esac
  case "${TERM:-}" in
    xterm-kitty | xterm-ghostty | wezterm | alacritty | foot*) echo 3; return ;;  # More terminals known to handle emoji.
    linux) echo 1; return ;; # the Linux console has neither the glyphs nor emoji
  esac
  if [[ -n ${WT_SESSION:-} || -n ${KONSOLE_VERSION:-} || ${VTE_VERSION:-0} -ge 5000 ]]; then  # Windows Terminal, Konsole and modern VTE also draw emoji.
    echo 3  # Use the emoji level for these terminals.
    return  # Done.
  fi
  # Modern terminals (256+ colors, UTF-8) usually draw emoji even when they
  # are not on the known-terminal list above; fall back to BMP symbols only
  # for the low-color case.
  if [[ $colors -ge 256 ]]; then  # A 256-color terminal usually draws emoji even if it is not named above.
    echo 3  # Use the emoji level.
    return  # Done.
  fi
  echo 2  # Fallback: colors plus plain unicode symbols, but no emoji.
}

# nerd_font: is a Nerd Font installed? (Installed isn't proof the terminal
# uses it, so VALHALLA_NERD=0/1 overrides.)
# @brief Detect an installed Nerd Font, honoring the VALHALLA_NERD=0/1 override.
# @return 0 when available, 1 otherwise.
nerd_font() {
  case "${VALHALLA_NERD:-}" in 1) return 0 ;; 0) return 1 ;; esac  # An explicit override is checked before any detection.
  if [[ $(uname -s) == Darwin ]]; then  # On macOS, look for a Nerd Font in the font folders.
    local f  # Loop variable holding each font file path.
    for f in "$HOME"/Library/Fonts/* /Library/Fonts/*; do  # Search both the user and the system font folders.
      [[ $f == *[Nn]erd* ]] && return 0  # A font whose name contains "nerd" means one is installed.
    done
    return 1  # No Nerd Font found on macOS.
  else
    command -v fc-list >/dev/null 2>&1 && fc-list 2>/dev/null | grep -qi 'nerd'  # On Linux, ask fontconfig whether a Nerd Font is installed.
  fi
}

# ── fancy output (colors, unicode, emoji, Nerd Font icons) ─────────────────
# One style engine for everything the tool prints. Levels come from
# fancy_level: 0 plain, 1 colors, 2 + unicode glyphs, 3 + emoji; level 2
# prefers Nerd Font glyphs when one is installed (VALHALLA_NERD=1 forces them).
# stdout and stderr are judged separately, so piped output stays clean.
# Data-producing functions (inspect results, env values, completions) never
# use these.
# @brief Define color and icon variables with the given prefix for the given decoration level.
# @param PREFIX variable-name prefix, S_ for stdout or E_ for stderr.
# @param LEVEL fancy level from 0 (plain) to 3 (emoji).
# @return 0 on success.
style_set() { # PREFIX LEVEL: defines ${PREFIX}R/ERR/... colors and ${PREFIX}i_* icons.
  local p="$1" l="$2"  # Prefix for the variable names (S_ or E_) and the chosen decoration level.
  if [[ $l -ge 1 ]]; then  # Level 1 and above define real color escape codes.
    if [[ "$(tput colors 2>/dev/null || echo 8)" -ge 256 ]]; then  # Use the richer 256-color palette when the terminal supports it.
      printf -v "${p}ERR"  '\033[38;5;211m'
      printf -v "${p}WRN"  '\033[38;5;223m'
      printf -v "${p}OK"   '\033[38;5;151m'
      printf -v "${p}INFO" '\033[38;5;159m'
      printf -v "${p}ACC"  '\033[38;5;183m'
      printf -v "${p}HEAD" '\033[38;5;153m'
      printf -v "${p}VAL"  '\033[38;5;255m'
      printf -v "${p}LBL"  '\033[38;5;189m'
      printf -v "${p}DIM"  '\033[2m'
      printf -v "${p}B"    '\033[1m'
    else
      printf -v "${p}ERR"  '\033[31m'
      printf -v "${p}WRN"  '\033[33m'
      printf -v "${p}OK"   '\033[32m'
      printf -v "${p}INFO" '\033[36m'
      printf -v "${p}ACC"  '\033[35m'
      printf -v "${p}HEAD" '\033[34m'
      printf -v "${p}VAL"  '\033[1;37m'
      printf -v "${p}LBL"  '\033[2m'
      printf -v "${p}DIM"  '\033[2m'
      printf -v "${p}B"    '\033[1m'
    fi
    printf -v "${p}R" '\033[0m'  # The reset code returns text to its normal color.
  else  # Plain level: every style code is set to an empty string.
    printf -v "${p}R" '' ; printf -v "${p}ERR" '' ; printf -v "${p}WRN" ''
    printf -v "${p}OK" '' ; printf -v "${p}INFO" '' ; printf -v "${p}ACC" ''
    printf -v "${p}HEAD" '' ; printf -v "${p}VAL" '' ; printf -v "${p}LBL" ''
    printf -v "${p}DIM" '' ; printf -v "${p}B" ''
  fi
  # Level 3 uses emoji, matching the help renderer. Nerd Font glyphs remain
  # the level-2 fallback, or can be explicitly forced with VALHALLA_NERD=1.
  local v  # Loop variable over the icon variable names.
  for v in NERD i_ok i_err i_warn i_info i_q i_pull i_save i_trash i_restart i_box i_server i_runtime i_rocket i_book i_link i_key i_wait i_stop i_step i_version; do  # Start every icon variable as empty before setting the ones this level uses.
    printf -v "${p}${v}" ''  # Give each icon variable an empty default.
  done
  printf -v "${p}GAP" ''  # GAP is the spacing after an icon; empty until a level sets it.
  printf -v "${p}VERSION_GAP" ''  # VERSION_GAP is the wider spacing used for the version row.
  if [[ $l -ge 2 ]]; then  # Icons only appear at level 2 or above.
    if [[ $l -lt 3 || ${VALHALLA_NERD:-} == 1 ]] && nerd_font; then  # Nerd Font glyphs at level 2 (or when forced); otherwise emoji at level 3.
      printf -v "${p}NERD" '1'  # Flag that Nerd Font glyphs are in use.
      printf -v "${p}GAP" '   '  # Nerd glyphs are one column wide, so they use a small three-space gap.
      printf -v "${p}VERSION_GAP" '   '  # Use the same gap for the version row.
      printf -v "${p}i_ok"     '%s' $'\xef\x80\x8c'   # nf-fa-check
      printf -v "${p}i_err"    '%s' $'\xef\x80\x8d'   # nf-fa-times
      printf -v "${p}i_warn"   '%s' $'\xef\x81\xb1'   # nf-fa-warning
      printf -v "${p}i_info"   '%s' $'\xef\x84\xa9'   # nf-fa-info
      printf -v "${p}i_q"      '%s' $'\xef\x84\xa8'   # nf-fa-question
      printf -v "${p}i_pull"   '%s' $'\xef\x80\x99'   # nf-fa-download
      printf -v "${p}i_save"   '%s' $'\xef\x87\x80'   # nf-fa-database
      printf -v "${p}i_trash"  '%s' $'\xef\x87\xb8'   # nf-fa-trash
      printf -v "${p}i_restart" '%s' $'\xef\x80\xa1'  # nf-fa-refresh
      printf -v "${p}i_box"    '%s' $'\xef\x86\xb2'   # nf-fa-cube
      printf -v "${p}i_server" '%s' $'\xef\x88\xb3'   # nf-fa-server
      printf -v "${p}i_runtime" '%s' $'\xef\x84\xa9'   # nf-fa-info
      printf -v "${p}i_rocket" '%s' $'\xef\x84\xb5'   # nf-fa-rocket
      printf -v "${p}i_book"   '%s' $'\xef\x80\xad'   # nf-fa-book
      printf -v "${p}i_link"   '%s' $'\xef\x83\x81'   # nf-fa-link
      printf -v "${p}i_key"    '%s' $'\xef\x82\x84'   # nf-fa-key
      printf -v "${p}i_wait"   '%s' $'\xef\x89\x94'   # nf-fa-hourglass-half
      printf -v "${p}i_stop"   '%s' $'\xef\x81\x8d'   # nf-fa-stop
      printf -v "${p}i_step"   '%s' '▸'
      printf -v "${p}i_version" '%s' $'\xef\x80\xab'  # nf-fa-tag
    elif [[ $l -ge 3 ]]; then  # Level 3 uses emoji instead of Nerd Font glyphs.
      printf -v "${p}GAP" '  '  # Emoji are two columns wide, so use a two-space gap.
      printf -v "${p}VERSION_GAP" '   '  # Use a slightly wider gap for the version row.
      printf -v "${p}i_ok"     '%s' '✅'   # Standard emoji icon set, one icon per message category.
      printf -v "${p}i_err"    '%s' '❌'
      printf -v "${p}i_warn"   '%s' '🚧'
      printf -v "${p}i_info"   '%s' '💡'
      printf -v "${p}i_q"      '%s' '❓'
      printf -v "${p}i_pull"   '%s' '📥'
      printf -v "${p}i_save"   '%s' '💾'
      printf -v "${p}i_trash"  '%s' '🗑'
      printf -v "${p}i_restart" '%s' '🔄'
      printf -v "${p}i_box"    '%s' '📦'
      printf -v "${p}i_server" '%s' '⚙️'
      printf -v "${p}i_runtime" '%s' '🐳'
      printf -v "${p}i_rocket" '%s' '🚀'
      printf -v "${p}i_book"   '%s' '📖'
      printf -v "${p}i_link"   '%s' '🔗'
      printf -v "${p}i_key"    '%s' '🔑'
      printf -v "${p}i_wait"   '%s' '⌛'
      printf -v "${p}i_stop"   '%s' '🛑'
      printf -v "${p}i_step"   '%s' '▸'
      printf -v "${p}i_version" '%s' '🏷'
    else  # Level 2 with no Nerd Font falls back to plain unicode symbols.
      printf -v "${p}GAP" ' '  # Unicode symbols are one column wide, so use a one-space gap.
      printf -v "${p}VERSION_GAP" ' '  # Use the same gap for the version row.
      printf -v "${p}i_ok"     '%s' '✓'
      printf -v "${p}i_err"    '%s' '✗'
      printf -v "${p}i_warn"   '%s' '⚠'
      printf -v "${p}i_info"   '%s' 'ℹ'
      printf -v "${p}i_runtime" '%s' 'ℹ'
      printf -v "${p}i_server"   '%s' ' '
      printf -v "${p}i_q"      '%s' '?'
      printf -v "${p}i_pull"   '%s' '↓'
      printf -v "${p}i_restart" '%s' '↻'
      printf -v "${p}i_step"   '%s' '▸'
      printf -v "${p}i_version" '%s' '#'
    fi
  fi
}

# The style variables are set dynamically in style_set; declare them so
# the linter can see every name.
# shellcheck disable=SC2034  # set dynamically in style_set
S_R='' S_ERR='' S_WRN='' S_OK='' S_INFO='' S_ACC='' S_HEAD='' S_VAL='' S_LBL='' S_DIM='' S_B='' S_NERD='' S_GAP='' S_VERSION_GAP='' S_i_ok='' S_i_err='' S_i_warn='' S_i_info='' S_i_q='' S_i_pull='' S_i_save='' S_i_trash='' S_i_restart='' S_i_box='' S_i_server='' S_i_runtime='' S_i_rocket='' S_i_book='' S_i_link='' S_i_key='' S_i_wait='' S_i_stop='' S_i_step='' S_i_version='' E_R='' E_ERR='' E_WRN='' E_OK='' E_INFO='' E_ACC='' E_HEAD='' E_VAL='' E_LBL='' E_DIM='' E_B='' E_NERD='' E_GAP='' E_VERSION_GAP='' E_i_ok='' E_i_err='' E_i_warn='' E_i_info='' E_i_q='' E_i_pull='' E_i_save='' E_i_trash='' E_i_restart='' E_i_box='' E_i_server='' E_i_runtime='' E_i_rocket='' E_i_book='' E_i_link='' E_i_key='' E_i_wait='' E_i_stop='' E_i_step='' E_i_version=''

# style_init: judge stdout and stderr separately, so only real terminals
# get decoration and piped output stays byte-clean.
# @brief Initialize separate S_ (stdout) and E_ (stderr) style sets by judging each stream independently.
# @return 0 on success.
style_init() {
  local o=0 e=0  # Start by assuming neither output stream is a terminal.
  [[ -t 1 ]] && o=1  # Flag stdout when it is a terminal.
  [[ -t 2 ]] && e=1  # Flag stderr when it is a terminal.
  style_set S_ "$(fancy_level "$o")"  # Build the stdout style set from stdout's decoration level.
  style_set E_ "$(fancy_level "$e")"  # Build the stderr style set from stderr's decoration level.
}
style_init  # Set up the styles once at startup for all later messages.

# ── styled message primitives (all write to stderr) ────────────────────────
# die/warn keep their literal "error:"/"warning:" words; step/ok/info add a
# contextual glyph. At level 0 every line comes out plain, like before.
# @brief Choose a context glyph for a message from its leading words.
# @param text the message text to classify.
# @param prefix variable prefix to read the glyph from, S_ or E_.
# @return 0; prints the glyph, or nothing when no category matches.
message_icon() {
  local text="$1" prefix="$2" name var  # Message text, variable prefix (S_ or E_), matched icon name and the final variable name.
  case "$text" in  # Classify the message by its opening words to choose a fitting icon.
    Starting*|Creating*|Recreating*|Building*) name=i_box ;;
    Pulling*|Downloading*|Updating*|Installing*) name=i_pull ;;
    Restarting*) name=i_restart ;;
    Stopping*|Stopped*) name=i_stop ;;
    Removing*|Removed*|"Couldn't remove"*) name=i_trash ;;
    Saving*|Saved*|Dumping*|Dumped*|Restoring*|Restored*) name=i_save ;;
    Running*test*|Running*installer*) name=i_book ;;
    Opening*) name=i_link ;;
    *TOKEN_SECRET*) name=i_key ;;
    *restart*) name=i_restart ;;
    *stop*) name=i_stop ;;
    *remove*) name=i_trash ;;
    *dump*|*database*|*restore*) name=i_save ;;
    *test*) name=i_book ;;
    *build*) name=i_box ;;
    *open*) name=i_link ;;
    *) name= ;;
  esac
  [[ -n $name ]] || return 0  # No category matched, so print no icon.
  var="${prefix}${name}"  # Build the name of the variable holding that icon.
  printf '%s' "${!var}"  # Print the icon via indirect expansion of the variable name.
}
# @brief Print a styled error to stderr and terminate immediately.
# @param * message words to print.
# @return Does not return; exits 1.
die() { printf '%s\n' "${E_ERR}${E_i_err:+$E_i_err$E_GAP}error:${E_R} $*" >&2; exit 1; }
# @brief Print a styled warning to stderr without stopping.
# @param * message words to print.
# @return 0 on success.
warn() { printf '%s\n' "${E_WRN}${E_i_warn:+$E_i_warn$E_GAP}warning:${E_R} $*" >&2; }
# @brief Print a plain note to stderr, used for parser warnings.
# @param * message words to print.
# @return 0 on success.
note() { echo "$*" >&2; }
# @brief Print a progress step to stderr, with a contextual glyph when available.
# @param * message words to print.
# @return 0 on success.
step() {
  local icon; icon="$(message_icon "$*" E_)"; [[ -n $icon ]] || icon="$E_i_step"  # Pick a contextual icon for this message, or fall back to the step icon.
  printf '%s\n' "${E_ACC}${icon:+$icon$E_GAP}${E_R}$*" >&2
}
# @brief Print a success message to stderr.
# @param * message words to print.
# @return 0 on success.
ok() {
  local icon; icon="$(message_icon "$*" E_)"; [[ -n $icon ]] || icon="$E_i_ok"  # Pick a contextual icon, or fall back to the success icon.
  printf '%s\n' "${E_OK}${icon:+$icon$E_GAP}${E_R}$*" >&2
}
# @brief Print an informational message to stderr.
# @param * message words to print.
# @return 0 on success.
info() {
  local icon; icon="$(message_icon "$*" E_)"; [[ -n $icon ]] || icon="$E_i_info"  # Pick a contextual icon, or fall back to the info icon.
  printf '%s\n' "${E_INFO}${icon:+$icon$E_GAP}${E_R}$*" >&2
}
# stdout twins for command results (styled only when stdout is a terminal).
# @brief Print a success result to stdout, styled only when stdout is a terminal.
# @param * message words to print.
# @return 0 on success.
say_ok() {
  local icon; icon="$(message_icon "$*" S_)"; [[ -n $icon ]] || icon="$S_i_ok"  # Pick a contextual icon, or fall back to the success icon.
  printf '%s\n' "${S_OK}${icon:+$icon$S_GAP}${S_R}$*"
}
# @brief Print an informational result to stdout, styled only when stdout is a terminal.
# @param * message words to print.
# @return 0 on success.
say_info() {
  local icon; icon="$(message_icon "$*" S_)"; [[ -n $icon ]] || icon="$S_i_info"  # Pick a contextual icon, or fall back to the info icon.
  printf '%s\n' "${S_INFO}${icon:+$icon$S_GAP}${S_R}$*"
}


# help_cols: the name-column width that aligns every list in the help
# (longest of the command heads, option heads and env var names, clamped).
# @brief Compute the aligned name-column width shared by every list in the help.
# @return 0; prints the clamped width.
help_cols() { help_text | awk '
    /^  .*\[options\] <command> / { next } # usage line, not a list entry
    /^  [a-z][a-z-]*( |$)/ || /^  -/ {
      line = $0; sub(/^  /, "", line)
      if (match(line, /  +/)) head = substr(line, 1, RSTART - 1); else head = line
      if (length(head) > w) w = length(head)
      next
    }
    /^  [A-Z][A-Z0-9_]+(=[^ ]*)? / {
      line = $0; sub(/^  /, "", line)
      split(line, parts, / +/)
      if (length(parts[1]) > w) w = length(parts[1])
    }
    END { if (w < 18) w = 18; if (w > 26) w = 26; print w }
  '
}

# quick_cols: the name-column width for the quick start box (its own, so the
# box stays compact instead of using the global help column).
# @brief Compute the command-column width for the compact quick-start box.
# @return 0; prints the width.
quick_cols() { help_text | awk '
    /^Quick start:$/ { q = 1; next }
    q && /^[[:space:]]*$/ { q = 0 }
    q && /^  / {
      line = $0; sub(/^  /, "", line)
      if (match(line, /  +/)) head = substr(line, 1, RSTART - 1); else head = line
      if (length(head) > w) w = length(head)
    }
    END { if (w < 6) w = 6; print w }
  '
}

# render_help LEVEL WNAME QCOL: decorates help_text for the terminal. WNAME is
# the aligned name column, QCOL the command column inside the quick start box;
# descriptions all start at the same column and long paragraphs wrap to the
# terminal width.
# @brief Decorate the help text with colors, boxes, glyphs and wrapping via awk.
# @param level fancy level.
# @param wname aligned name-column width.
# @param qcol command-column width inside the quick-start box.
# @return 0 on success.
render_help() {
  local level="$1" wname="$2" qcol="$3" e
  e="$(printf '\033')"
  local c256=0
  [[ "$(tput colors 2>/dev/null || echo 8)" -ge 256 ]] && c256=1
  awk -v L="$level" -v N="$wname" -v QC="$qcol" -v E="$e" -v C256="$c256" -v W="$(tput cols 2>/dev/null || echo 80)" '
    function c(code) { return E "[" code "m" }
    function rule(n,   r, i) { r = ""; for (i = 0; i < n; i++) r = r H; return r }
    # Colors (256-color palette when available).
    BEGIN {
      R = c(0); B = c(1); D = c(2)
      if (C256) {
        TITLE = c("1;38;5;153"); HEAD = c("1;38;5;183"); CMD = c("1;38;5;151"); ARG = c("38;5;223")
        OPT = c("38;5;159"); ENV = c("38;5;183"); DEF = c("38;5;250"); CODE = c("38;5;216"); SUB = c("38;5;223"); FRAME = c("1;38;5;147")
      } else {
        TITLE = c("34"); HEAD = c("35"); CMD = c("32"); ARG = c("33")
        OPT = c("36"); ENV = c("35"); DEF = c("2"); CODE = c("33"); SUB = c("33"); FRAME = c("35")
      }
      U = (L >= 2); M = (L >= 3); EG = M ? "   " : " "; EG = M ? "  " : " "
      # The marker replaces the 2-space indent, so it is 2 columns wide.
      H = U ? "─" : "-"; BUL = U ? "▸ " : "* "
      if (W > 100) W = 100
      if (W < 40) W = 40
      # Quick start box: full width up to 88 columns; the contents get BOXW-4.
      BOXW = (W > 88) ? 88 : W
      CW = BOXW - 4
      INQ = 0
      # One description column for every list, so the eye has a single rail.
      DESCP = ""; for (i = 0; i < N + 4; i++) DESCP = DESCP " "
    }
    function emoji(t) {
      if (t ~ /^Quick start/) { if (M) return "⚡️" EG; return "" }
      if (!M) return ""
      if (t ~ /^Container/) return "📦" EG
      if (t ~ /^Inspect/) return "🔍" EG
      if (t ~ /^Database/) return "💾" EG
      if (t ~ /^Other settings/) return "🌱" EG
      if (t ~ /^Other/) return "🧰" EG
      if (t ~ /^Tool/) return "🔧" EG
      if (t ~ /^Options/) return "🎛" EG
      if (t ~ /^Persistence/) return "💽" EG
      if (t ~ /^Runtime/) return "🐳" EG
      if (t ~ /^Dependencies/) return "🧩" EG
      if (t ~ /^Shell completion/) return "💡" EG
      if (t ~ /^Server variables the image/) return "📋" EG
      if (t ~ /^Server variables/) return "📜" EG
      if (t ~ /^Note/) return "📝" EG
      if (t ~ /^Updates/) return "🔄" EG
      if (t ~ /^tag defaults/) return "🏷" EG
      return "◆" EG
    }
    # Groups inside the server variable list.
    function group_emoji(t) {
      if (!M) return ""
      if (t ~ /^Rate limiting/) return "🚦" EG
      if (t ~ /^Database/) return "🐘" EG
      if (t ~ /^Server/) return "🚀" EG
      if (t ~ /^Tokens/) return "🔐" EG
      if (t ~ /^Frontend/) return "🌐" EG
      if (t ~ /^Email/) return "📧" EG
      return "◆" EG
    }
    # `code` -> colored, backticks dropped.
    function inline(t,   out, i, parts, n) {
      n = split(t, parts, "`")
      out = parts[1]
      for (i = 2; i <= n; i++) out = out ((i % 2 == 0) ? CODE : R) parts[i]
      return out R
    }
    # [defaults] at the end of a line -> dim.
    function defaults(t) { gsub(/\[[^]]*\]/, DEF "&" R, t); return t }
    # spaces to reach the description column from a name of length len.
    function gap(len,   n, s) {
      n = N - len + 2; if (n < 2) n = 2
      s = ""; while (n-- > 0) s = s " "
      return s
    }
    function spaces(n,   s) { s = ""; while (n-- > 0) s = s " "; return s }
    # A glyph per quick-start command: astral emoji at level 3, BMP symbols at
    # level 2 (fonts are not checked; both sets are width 1 or 2 as assumed).
    function qicon(h) {
      if (M) {
        if (h ~ /^run/) return "🚀"
        if (h ~ /^doc/) return "📖"
        if (h ~ /^status/) return "🩺"
        if (h ~ /^show|^logs/) return "📜"
        if (h ~ /^stop/) return "🛑"
        if (h ~ /^restart/) return "🔁"
        if (h ~ /^update/) return "🔄"
        if (h ~ /^build/) return "🔨"
        if (h ~ /^test/) return "🧪"
        if (h ~ /^psql/) return "🐘"
        if (h ~ /^shell/) return "🐚"
        if (h ~ /^install/) return "📥"
        return ""
      }
      if (U) {
        if (h ~ /^run/) return "▸"
        if (h ~ /^doc/) return "ℹ"
        if (h ~ /^status/) return "◉"
        if (h ~ /^show|^logs/) return "≣"
        if (h ~ /^stop/) return "⏹"
        if (h ~ /^restart/) return "↺"
        if (h ~ /^update/) return "↻"
        if (h ~ /^build/) return "⚒"
        if (h ~ /^test/) return "⚗"
        if (h ~ /^psql/) return "⌗"
        if (h ~ /^shell/) return "❯"
        if (h ~ /^install/) return "⤓"
        return "▸"
      }
      return ""
    }
    # One quick-start line: left border, prefix, description, right border.
    # PVIS is the visible width of the (colored) prefix.
    function qline(pre, pvis, text,   pad) {
      pad = CW - pvis - length(text)
      if (pad < 0) pad = 0
      print TITLE (U ? "│" : "|") R " " pre inline(text) spaces(pad) " " TITLE (U ? "│" : "|") R
    }

    NR == 1 {
      title = $0
      split_at = index(title, "(vdt)")
      title_before = (split_at ? substr(title, 1, split_at - 1) : title)
      title_vdt = (split_at ? substr(title, split_at, 5) : "")
      title_after = (split_at ? substr(title, split_at + 5) : "")
      if (U) {
        line = rule(length(title) + 2 + (M ? 4 : 0))
        print FRAME "╭" line "╮" R
        print FRAME "│ " R (M ? "🏰" EG : "") TITLE title_before HEAD title_vdt TITLE title_after " " FRAME "│" R
        print FRAME "╰" line "╯" R
      } else {
        print TITLE title_before HEAD title_vdt TITLE title_after R
        print FRAME rule(length(title)) R
      }
      next
    }
    # usage line: "  vdt [options] <command> [args]      (installed)"
    /^  .*\[options\] <command> / {
      line = $0
      sub(/^  /, "", line)
      split(line, w, " ")
      rest = substr(line, length(w[1]) + 1)
      gsub(/\[[^]]*\]|<[^>]*>/, ARG "&" R, rest)
      print "  " CMD w[1] R rest
      next
    }
    # quick start: common tasks in a rounded box (ASCII when Unicode is out)
    /^Quick start:$/ {
      print ""
      print HEAD emoji($0) $0 R
      print TITLE (U ? "╭" : "+") rule(BOXW - 2) (U ? "╮" : "+") R
      INQ = 1
      next
    }
    INQ && /^[[:space:]]*$/ {
      print TITLE (U ? "╰" : "+") rule(BOXW - 2) (U ? "╯" : "+") R
      INQ = 0
      print ""
      next
    }
    INQ && /^  / {
      line = $0; sub(/^  /, "", line)
      if (match(line, /  +/)) { head = substr(line, 1, RSTART - 1); desc = substr(line, RSTART + RLENGTH) }
      else { head = line; desc = "" }
      split(head, w, " ")
      args = substr(head, length(w[1]) + 1)
      gsub(/\[[^]]*\]|<[^>]*>/, ARG "&" R, args)
      icon = qicon(head)
      iw = (icon == "") ? 0 : (M ? 3 : 2) # emoji is 2 columns wide, BMP 1
      qgap = QC - length(head) + 2; if (qgap < 2) qgap = 2
      pre = (icon != "" ? icon " " : "") CMD "vdt " w[1] R args spaces(qgap)
      pvis = iw + length("vdt ") + length(head) + qgap
      avail = CW - pvis; if (avail < 12) avail = 12
      if (desc == "") { qline(pre, pvis, ""); next }
      firstline = 1
      rest = desc
      while (length(rest) > avail) {
        nw = split(rest, words, " ")
        cur = ""
        for (i = 1; i <= nw; i++) {
          cand = (cur == "") ? words[i] : cur " " words[i]
          if (length(cand) > avail && cur != "") break
          cur = cand
        }
        qline(firstline ? pre : spaces(pvis), pvis, cur)
        firstline = 0
        rest = substr(rest, length(cur) + 2)
      }
      qline(firstline ? pre : spaces(pvis), pvis, rest)
      next
    }
    # section heading: "Something:" alone on a line
    /^[A-Za-z][^:]*:$/ {
      print ""
      print HEAD emoji($0) $0 R
      if (U) print D rule(W - 2) R
      next
    }
    # paragraph label: "Persistence: text..."
    /^[A-Z][A-Za-z ]+: / && !/^  / {
      label = $0; sub(/:.*/, "", label)
      rest = substr($0, length(label) + 2)
      print ""
      print HEAD emoji(label) label ":" R inline(rest)
      next
    }
    # "tag defaults to ..." note
    /^tag defaults/ { print D emoji($0) $0 R; next }
    # option lines: "  --name NAME   desc" / "  -n, --dry-run   desc"
    /^  -/ {
      line = $0; sub(/^  /, "", line)
      if (match(line, /  +/)) { head = substr(line, 1, RSTART - 1); desc = substr(line, RSTART + RLENGTH) }
      else { head = line; desc = "" }
      rawlen = length(head)
      gsub(/-?-[a-z][a-z-]*/, OPT "&" R, head)
      gsub(/ [A-Z][A-Z]+/, ARG "&" R, head)
      if (desc != "") print BUL head gap(rawlen) inline(defaults(desc))
      else print BUL head
      next
    }
    # environment variables: "  VALHALLA_X   desc"
    /^  [A-Z][A-Z0-9_]+(=[^ ]*)? / {
      line = $0; sub(/^  /, "", line)
      split(line, parts, / +/)
      rest = substr(line, length(parts[1]) + 1); sub(/^ +/, "", rest)
      print BUL ENV parts[1] R gap(length(parts[1])) inline(defaults(rest))
      next
    }
    # server variable list: "    MAX_REQUESTS   desc   [default]"
    /^    [A-Z][A-Z0-9_]+ / {
      line = $0; sub(/^    /, "", line)
      split(line, parts, / +/)
      # Dim [defaults] first, on the raw text: color codes contain "[" too.
      rest = defaults(substr(line, length(parts[1]) + 1))
      gsub(/[A-Z][A-Z0-9_][A-Z0-9_][A-Z0-9_]+/, ENV "&" R, rest)
      print "    " ENV parts[1] R rest
      next
    }
    # sub-group inside a section: "  Rate limiting / DoS"
    /^  [A-Z][a-z]/ && !/  [^ ].*  / {
      t = $0; sub(/^  /, "", t)
      print "  " SUB group_emoji(t) t R
      next
    }
    # command lines: "  run [tag]   desc"
    /^  [a-z][a-z-]*( |$)/ {
      line = $0; sub(/^  /, "", line)
      if (match(line, /  +/)) { head = substr(line, 1, RSTART - 1); desc = substr(line, RSTART + RLENGTH) }
      else { head = line; desc = "" }
      split(head, w, " ")
      args = substr(head, length(w[1]) + 1)
      gsub(/\[[^]]*\]|<[^>]*>/, ARG "&" R, args)
      if (desc != "") print BUL CMD w[1] R args gap(length(head)) inline(defaults(desc))
      else print BUL CMD w[1] R args
      next
    }
    # hand-indented continuation lines: realign to the description column
    /^ {8}/ { sub(/^ */, ""); print DESCP inline($0); next }
    # plain paragraphs: the header text is pre-wrapped, keep it as is
    { print inline($0) }
  '
}

# @brief Display help, paged through less when stdout is a fancy terminal and less is available.
# @return 0 on success.
usage() {
  local level is_tty=0 wname qcol  # Decoration level, terminal flag and the two computed column widths.
  [[ -t 1 ]] && is_tty=1  # Detect a terminal to decide how much help decoration to use.
  level="$(fancy_level "$is_tty")"  # Compute that decoration level.
  if [[ $level -le 0 ]]; then  # Plain output: print the raw help text with no rendering.
    help_text  # Print help straight from the header comment.
    return 0  # Nothing more to do.
  fi
  wname="$(help_cols)"  # Width of the aligned name column.
  qcol="$(quick_cols)"  # Width of the command column inside the quick-start box.
  if [[ -t 1 ]] && command -v less >/dev/null 2>&1; then  # Page the help through less only on a real terminal when less exists.
    # -R keeps colors, -F quits right away when it fits, -X leaves it on screen.
    help_text | render_help "$level" "$wname" "$qcol" | less -RFX  # Render the help with colors and pipe it into less.
  else
    help_text | render_help "$level" "$wname" "$qcol"  # Render and print the help directly when less is unavailable.
  fi
}

# @brief Ask a [y/N] question on the terminal unless --yes was already given.
# @param 1 the question to display.
# @return 0 when confirmed, 1 when declined; dies when there is no TTY.
confirm() {
  [[ $ASSUME_YES == 1 ]] && return 0  # With --yes, skip the question entirely.
  # With no TTY there is no one to ask; fail with the --yes hint instead of
  # blocking on a read that would hit EOF.
  [[ -t 0 ]] || die "$1 (re-run with --yes to confirm)"  # With no terminal there is no one to ask, so fail with a --yes hint.
  local answer  # Holds the user's typed reply.
  printf '%s' "${E_ACC}${E_i_q:+$E_i_q$E_GAP}${E_B}$1${E_R} ${E_DIM}[y/N]${E_R} " >&2  # Draw the question with a [y/N] hint on stderr.
  read -r answer  # Read one line of input without interpreting backslashes.
  [[ $answer == y || $answer == Y || $answer == yes ]]  # Confirm only for y, Y or yes.
}

# ── options ────────────────────────────────────────────────────────────────
# Options are accepted before or after the command. Non-option words collect
# in ARGS; -- stops parsing so later words are treated as literal arguments.
ARGS=()  # Non-option words (the command and its arguments) collect here.
while [[ $# -gt 0 ]]; do  # Consume positional parameters until none are left.
  case "$1" in  # Match the current option word.
    --name) [[ $# -ge 2 ]] || die "--name needs a value"; NAME="$2"; shift 2 ;;  # --name needs a following value; store it and skip both words.
    --name=*) NAME="${1#*=}"; shift ;;  # --name=VALUE form: strip everything through the equals sign.
    --port) [[ $# -ge 2 ]] || die "--port needs a value"; PORT="$2"; PORT_EXPLICIT=1; shift 2 ;;  # Store the port and mark it explicit so it beats the container's own port.
    --port=*) PORT="${1#*=}"; PORT_EXPLICIT=1; shift ;;  # --port=VALUE form.
    --runtime) [[ $# -ge 2 ]] || die "--runtime needs a value"; VALHALLA_RUNTIME="$2"; shift 2 ;;  # Choose docker or container; stored in the runtime environment variable.
    --runtime=*) VALHALLA_RUNTIME="${1#*=}"; shift ;;  # --runtime=VALUE form.
    --env-file) [[ $# -ge 2 ]] || die "--env-file needs a value"; ENV_FILE="$2"; ENV_FILE_EXPLICIT=1; shift 2 ;;  # Choose an env file; explicit means a missing file is an error.
    --env-file=*) ENV_FILE="${1#*=}"; ENV_FILE_EXPLICIT=1; shift ;;  # --env-file=VALUE form.
    -n | --dry-run) DRY_RUN=1; shift ;;  # Show what would change without actually changing anything.
    -y | --yes) ASSUME_YES=1; shift ;;  # Automatically confirm confirmation prompts.
    -p | --persist) PERSIST=1; shift ;;  # Turn persistence on for this command.
    --token-secret) [[ $# -ge 2 ]] || die "--token-secret needs a value"; CLI_TOKEN_SECRET="$2"; shift 2 ;;  # JWT signing secret for a new container; the highest-precedence source.
    --token-secret=*) CLI_TOKEN_SECRET="${1#*=}"; shift ;;  # --token-secret=VALUE form.
    --no-persist) PERSIST=0; shift ;;  # Turn persistence off, for example to recreate a container without it.
    --no-prune) NO_PRUNE=1; shift ;;  # Keep replaced images after update or build.
    --image-only) UPDATE_SCOPE="${UPDATE_SCOPE:+$UPDATE_SCOPE }image"; shift ;;  # Restrict update to the image side; append to any scope already chosen.
    --tool-only) UPDATE_SCOPE="${UPDATE_SCOPE:+$UPDATE_SCOPE }tool"; shift ;;  # Restrict update to the tool side.
    --raw) RAW_PAYLOAD=1; shift ;;  # payload: print the payload file exactly as stored.
    --filled) FILLED_PAYLOAD=1; shift ;;  # payload: substitute sample values for {{...}} placeholders.
    -h | --help) ARGS=(help); break ;;  # Treat help as the command and stop parsing options.
    --) shift; ARGS+=("$@"); break ;;  # End of options; every remaining word is a literal argument.
    -*) die "unknown option: $1 (see: $PROG help)" ;;  # Reject any unrecognised dash-prefixed option right away.
    *) ARGS+=("$1"); shift ;;  # A non-option word becomes the command or one of its arguments.
  esac
done  # All command-line arguments have been parsed.
# Validate inputs before touching Docker: a bad name, port or wait value would
# otherwise fail late with a confusing runtime error or an unreachable server.
[[ $PORT =~ ^[0-9]+$ ]] || die "--port must be a number, got '$PORT'"  # A non-numeric port would fail later with a confusing runtime error.
case "$UPDATE_SCOPE" in  # Only one, or neither, of the two update-scope flags is valid.
  "" | image | tool) ;; # single flag (or none) is fine
  *) die "--image-only and --tool-only can't be used together" ;;  # The two scope flags are contradictory when combined.
esac
[[ $NAME =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]] ||  # Docker and container names allow only letters, digits, dot, underscore and dash.
  die "--name may only contain letters, digits, '.', '_' and '-' (got '$NAME')"  # Explain the naming rule to the user.
[[ $WAIT_SECONDS =~ ^[0-9]+$ ]] || die "VALHALLA_WAIT must be a number of seconds, got '$WAIT_SECONDS'"  # The wait value must be a whole number of seconds.

# set_urls: health and docs URLs for the current PORT.
# @brief Set HEALTH_URL and DOC_URL for the current PORT.
# @return 0 on success.
set_urls() {
  HEALTH_URL="http://127.0.0.1:${PORT}/api/v2/hello"  # Local URL used to check whether the server is up.
  DOC_URL="http://127.0.0.1:${PORT}/api/v2/doc"  # Local URL of the interactive API documentation.
}
set_urls  # Compute the URLs now that the final port is known.

# ── shell completion ───────────────────────────────────────────────────────
# @brief Print all command names from COMMANDS, space-separated, for shell completion.
# @return 0 on success.
command_names() {
  local entry  # Loop variable over the COMMANDS table.
  for entry in "${COMMANDS[@]}"; do printf '%s ' "${entry%%|*}"; done  # Print just each command name (the text before the | separator).
}

# @brief Generate the zsh completion script for vdt from the COMMANDS/OPTIONS tables.
# @return 0 on success.
completion_zsh() {
  local entry name
  name="$(basename "$PROG")"
  printf '# zsh completion for %s (generated by: %s completion zsh)\n' "$PROG" "$PROG"
  cat <<'EOF'
_vdt() {
  local -a cmds
  cmds=(
EOF
  local desc q="'"
  for entry in "${COMMANDS[@]}"; do
    desc="${entry#*|}"
    desc="${desc//$q/$q\\$q$q}"
    printf "    '%s:%s'\n" "${entry%%|*}" "$desc"
  done
  cat <<'EOF'
  )
  local script=${words[1]} root
  [[ $script == */* ]] || script=${commands[$script]:-$script}
  root=${${script:A}:h}
  local -a opts optdesc
  opts=(
EOF
  local entry names desc kind spec n q="'"
  for entry in "${OPTIONS[@]}"; do
    names="${entry%%|*}"; desc="${entry#*|}"; kind="${desc#*|}"; desc="${desc%%|*}"
    desc="${desc//$q/$q\\$q$q}"
    case "$kind" in
      value) spec=":${names#--}:" ;;
      runtime) spec=":runtime:(docker container)" ;;
      file) spec=":file:_files" ;;
      *) spec="" ;;
    esac
    if [[ $names == *" "* ]]; then
      printf "    '(%s)'{%s}'[%s]'\n" "$names" "${names// /,}" "$desc"
    elif [[ -n $kind ]]; then
      printf "    '%s=[%s]%s'\n" "$names" "$desc" "$spec"
    else
      printf "    '%s[%s]'\n" "$names" "$desc"
    fi
  done
  echo "  )"
  echo "  optdesc=("
  for entry in "${OPTIONS[@]}"; do
    names="${entry%%|*}"; desc="${entry#*|}"; desc="${desc%%|*}"
    desc="${desc//$q/$q\\$q$q}"
    for n in $names; do printf "    '%s:%s'\n" "$n" "$desc"; done
  done
  echo "  )"
  cat <<'EOF'
  local curcontext=$curcontext state line
  _arguments -C -s $opts '1:command:->cmd' '*::arg:->args'
  case $state in
    cmd) _describe -t commands 'vdt command' cmds ;;
    args)
      # Options are accepted after the command too.
      case ${words[CURRENT-1]} in
        --runtime) compadd docker container; return ;;
        --env-file) _files; return ;;
        --name|--port) _message 'value'; return ;;
      esac
      if [[ $PREFIX == -* ]]; then
        _describe -t options 'option' optdesc
        return
      fi
      case ${line[1]} in
        run|update|build|outdated|install)
          local -a tags
          tags=(latest local ${(f)"$(git -C $root tag --sort=-v:refname 2>/dev/null | head -n 20)"})
          _describe -t tags 'image tag' tags ;;
        restore) _files -g '*.sql(-.)' ;;
        dump) _files ;;
        logs) _message 'number of lines (default 100)' ;;
        test)
          local -a kinds
          kinds=('api:test_api.sh' 'fuzz:schemathesis_test.sh' 'all:both')
          _describe -t kinds 'test suite' kinds ;;
        payload)
          local dir pd
          for pd in "$root/api_tests/payloads" "$HOME/.local/share/vdt/api_tests/payloads"; do
            [[ -d $pd ]] && { dir=$pd; break }
          done
          if [[ -z $dir ]]; then
            _message 'payload group'
          elif [[ -d $dir/$words[CURRENT-1] ]]; then
            local -a names
            names=("$dir/$words[CURRENT-1]/"*.json(.N:t:r))
            _describe -t payloads 'payload' names
          else
            local -a groups
            groups=("$dir/"*(/N:t))
            _describe -t groups 'payload group' groups
          fi ;;
        completion) compadd zsh bash ;;
      esac ;;
  esac
}
EOF
  echo '(( $+functions[compdef] )) || { autoload -Uz compinit && compinit -i; }'
  printf 'compdef _vdt %s %q\n' "$name" "$SCRIPT"
}

# @brief Generate the bash completion script for vdt from the COMMANDS/OPTIONS tables.
# @return 0 on success.
completion_bash() {
  local entry name
  name="$(basename "$PROG")"
  printf '# bash completion for %s (generated by: %s completion bash)\n' "$PROG" "$PROG"
  cat <<'EOF'
_vdt() {
  local cur prev words cword
  cur="${COMP_WORDS[COMP_CWORD]}"
  prev="${COMP_WORDS[COMP_CWORD-1]}"
EOF
  local entry all=""
  for entry in "${OPTIONS[@]}"; do all+="${entry%%|*} "; done
  printf '  local opts="%s"\n' "${all% }"
  printf '  local cmds="%s"\n' "$(command_names)"
  cat <<'EOF'
  case "$prev" in
    --runtime) COMPREPLY=($(compgen -W "docker container" -- "$cur")); return ;;
    --env-file) COMPREPLY=($(compgen -f -- "$cur")); return ;;
    --name | --port) return ;;
  esac
  local i cmd="" root
  root="$(cd "$(dirname "$(command -v "${COMP_WORDS[0]}" 2>/dev/null || echo "${COMP_WORDS[0]}")")" 2>/dev/null && pwd)"
  for ((i = 1; i < COMP_CWORD; i++)); do
    case "${COMP_WORDS[i]}" in
      --name | --port | --runtime | --env-file) ((i++)) ;;
      -*) ;;
      *) cmd="${COMP_WORDS[i]}"; break ;;
    esac
  done
  if [[ $cur == -* ]]; then
    COMPREPLY=($(compgen -W "$opts" -- "$cur")); return
  fi
  case "$cmd" in
    "") COMPREPLY=($(compgen -W "$cmds" -- "$cur")) ;;
    run | update | build | outdated | install)
      COMPREPLY=($(compgen -W "latest local $(git -C "$root" tag 2>/dev/null)" -- "$cur")) ;;
    restore | dump) COMPREPLY=($(compgen -f -- "$cur")) ;;
    test) COMPREPLY=($(compgen -W "api fuzz all" -- "$cur")) ;;
    payload)
      local pd dir=""
      for pd in "$root/api_tests/payloads" "$HOME/.local/share/vdt/api_tests/payloads"; do
        [[ -d $pd ]] && { dir=$pd; break; }
      done
      if [[ -z $dir ]]; then
        COMPREPLY=()
      elif [[ -d $dir/$prev ]]; then
        COMPREPLY=($(compgen -W "$(ls "$dir/$prev" 2>/dev/null | sed 's/[.]json$//')" -- "$cur"))
      else
        COMPREPLY=($(compgen -W "$(ls "$dir" 2>/dev/null)" -- "$cur"))
      fi ;;
    completion) COMPREPLY=($(compgen -W "zsh bash" -- "$cur")) ;;
  esac
}
EOF
  printf 'complete -F _vdt %s %q\n' "$name" "$SCRIPT"
}

# install_completion: once, add an eval line to ~/.zshrc and ~/.bashrc
# (whichever exist). The marker makes it idempotent.
# @brief Add an idempotent completion-eval line to existing ~/.zshrc and ~/.bashrc.
# @return 0; skipped when VALHALLA_NO_COMPLETION=1.
install_completion() {
  [[ ${VALHALLA_NO_COMPLETION:-0} == 1 ]] && return 0
  local shell rc tmp marker="# vdt completion"
  for shell in zsh bash; do
    rc="$HOME/.${shell}rc"
    [[ -f $rc ]] || continue
    grep -qF "$marker" "$rc" 2>/dev/null && continue
    # shellcheck disable=SC2016  # $(...) is meant to run in the rc file
    if printf '\n%s (added by %s; remove to undo)\n[ -x %q ] && eval "$(%q completion %s)"\n' \
      "$marker" "$SCRIPT" "$SCRIPT" "$SCRIPT" "$shell" >>"$rc" 2>/dev/null; then
      info "Added tab completion to $rc (open a new terminal to use it)."
    fi
  done
}

# ── dependency check ───────────────────────────────────────────────────────
MISSING=()
WARNINGS=()

# install_hint TOOL: how to get TOOL.
# @brief Print installation guidance for a missing tool or repository file.
# @param 1 name of the missing dependency.
# @return 0 on success.
install_hint() {
  local brew="" mac=""
  command -v brew >/dev/null 2>&1 && brew="brew install"
  [[ $(uname -s) == Darwin ]] && mac=1
  case "$1" in
    container)
      if [[ -n $mac ]]; then echo "https://github.com/apple/container/releases${brew:+ or: $brew container}"
      else echo "Apple container is macOS only; use docker: VALHALLA_RUNTIME=docker"
      fi
      ;;
    docker) echo "https://docs.docker.com/get-docker/" ;;
    "container or docker")
      if [[ -n $mac ]]; then echo "Apple container: https://github.com/apple/container/releases; Docker: https://docs.docker.com/get-docker/"
      else echo "Docker: https://docs.docker.com/engine/install/"
      fi
      ;;
    python3) if [[ -n $mac ]]; then echo "${brew:+$brew python  (or: }xcode-select --install${brew:+)}"; else echo "${brew:-your package manager:} python3"; fi ;;
    git) if [[ -n $mac ]]; then echo "${brew:+$brew git  (or: }xcode-select --install${brew:+)}"; else echo "${brew:-your package manager:} git"; fi ;;
    xdg-open) echo "xdg-utils from your package manager" ;;
    "openssl or python3") echo "${brew:+$brew openssl or }install python3" ;;
    docker/Dockerfile) echo "build needs a full checkout: git clone https://github.com/dr3mro/Valhalla-server" ;;
    *.sh | */*) echo "missing next to $SCRIPT; reinstall, or restore it with: git checkout -- $1" ;;
    *) echo "${brew:-your package manager:} $1" ;;
  esac
}

# @brief Add each command not found on PATH to the MISSING list.
# @param @ one or more command names to require.
# @return 0 on success.
need() {
  local t
  for t; do command -v "$t" >/dev/null 2>&1 || MISSING+=("$t"); done
}

# need_any LABEL TOOL...: at least one of TOOL.
# @brief Require at least one of several tools, otherwise add LABEL to MISSING.
# @param label name to report when none is present.
# @param @ candidate command names.
# @return 0 when one exists, else 1.
need_any() {
  local label="$1" t
  shift
  for t; do command -v "$t" >/dev/null 2>&1 && return 0; done
  MISSING+=("$label")
}

# need_file PATH: a file in the repository.
# @brief Add PATH to MISSING when ROOT/PATH does not exist.
# @param 1 repository-relative file path.
# @return 0 on success.
need_file() { [[ -f $ROOT/$1 ]] || MISSING+=("$1"); }

# script_path NAME: NAME.sh next to this script (repository) or NAME
# (installed by install.sh, without the extension).
# @brief Locate a companion script in either the installed (flat) or checkout (nested) layout.
# @param 1 script base name without extension.
# @return 0 with the path on stdout when found, 1 otherwise.
script_path() {
  # Installed layout is flat ($ROOT/test_api); repository layout is nested.
  local cand
  for cand in \
      "$ROOT/$1.sh" \
      "$ROOT/$1" \
      "$ROOT/scripts/test/$1.sh" \
      "$ROOT/scripts/test/$1" \
      "$ROOT/scripts/run/$1.sh" \
      "$ROOT/scripts/run/$1" \
      "$ROOT/scripts/build/$1.sh" \
      "$ROOT/scripts/build/$1"; do
    if [[ -x $cand ]]; then echo "$cand"; return 0; fi
  done
  return 1
}

# need_script NAME: like need_file, for companion scripts.
# @brief Add NAME.sh to MISSING when script_path cannot locate the companion script.
# @param 1 script base name.
# @return 0 on success.
need_script() { script_path "$1" >/dev/null || MISSING+=("$1.sh"); }

# want TOOL REASON: optional; only a warning.
# @brief Record a missing optional tool in WARNINGS rather than failing.
# @param 1 command name.
# @param 2 reason it would be useful.
# @return 0 on success.
want() { command -v "$1" >/dev/null 2>&1 || WARNINGS+=("$1 not found: $2"); }

# check_deps COMMAND [ARG]: lists every missing dependency of COMMAND at once
# and exits if a required one is missing.
# @brief Verify all dependencies of a command at once and exit if a required one is missing.
# @param command the selected command name.
# @param arg first command argument, e.g. the test kind.
# @return 0 when satisfied; exits 1 with install hints otherwise.
check_deps() {
  # Required tools are command-specific: payload needs only fzf, while container
  # commands need a runtime. Decide them here instead of failing on first use.
  local cmd="$1" arg="${2:-}"  # Command name and its first argument (for example the test kind).

  case "$cmd" in  # Decide whether this command needs a runtime at all.
    secret | test | install | uninstall | version | payload) ;;  # These commands never talk to a container.
    *)
      if [[ -n ${VALHALLA_RUNTIME:-} ]]; then  # A specifically chosen runtime must exist on the system.
        need "$VALHALLA_RUNTIME"  # Require exactly the runtime the user selected.
      elif [[ -z $RT ]]; then  # No runtime chosen yet, so require at least one.
        MISSING+=("container or docker")  # Report that either runtime would be acceptable.
      fi
      [[ $RT == container ]] && need python3  # Apple container's inspect helpers need python3 to parse JSON.
      ;;
  esac

  case "$cmd" in  # Commands that call the health or docs URL also need curl.
    run | update | build | restart | status | doc) need curl ;;  # These commands probe the server over HTTP.
    outdated) need curl; want python3 "optional Hub-tag lookup" ;;  # Hub lookups need curl; python3 only optionally improves tag detection.
  esac

  case "$cmd" in  # Command-specific file and tool requirements.
    build)  # Building needs the Dockerfile.
      need_file docker/Dockerfile
      want git "the image version will be \"dev\""
      ;;
    doc)  # Opening docs needs a browser opener.
      if [[ $(uname -s) == Darwin ]]; then need open  # On macOS, use the open command.
      else need_any "xdg-open" xdg-open wslview  # On Linux or WSL, one of these openers is needed.
      fi
      ;;
    install | uninstall | update) need curl ;;  # These download the installer or image over the network.
    test)  # Tests need curl plus their own scripts and tools.
      need curl
      case "${arg:-api}" in
        api | all) need jq; need_script test_api ;;
      esac
      case "${arg:-api}" in
        fuzz | all) need python3; need_script schemathesis_test ;;
      esac
      ;;
    payload)  # Payload browsing needs fzf.
      need fzf  # The interactive picker program.
      command -v batcat >/dev/null 2>&1 || want bat "nicer payload colors" # Debian/Ubuntu name it batcat  # bat is optional; batcat is its Debian/Ubuntu name.
      ;;
  esac

  local w  # Loop variable over optional-tool warnings.
  for w in "${WARNINGS[@]}"; do warn "$w"; done  # Show optional-tool warnings without failing.
  [[ ${#MISSING[@]} -eq 0 ]] && return 0  # All required dependencies are present.

  # Accumulate every missing dependency and report them in one pass with install
  # hints, so the user can fix everything before re-running the command.
  printf '%s\n' "${E_ERR}${E_i_err:+$E_i_err$E_GAP}error: '$cmd' needs these, which are missing:${E_R}" >&2
  local m  # Loop variable over missing dependencies.
  for m in "${MISSING[@]}"; do  # Print each missing item with a hint on how to install it.
    printf '%s\n' "  ${E_ERR}${E_i_err:+$E_i_err$E_GAP}${E_R}$m  ${E_DIM}($(install_hint "$m"))${E_R}" >&2
  done
  exit 1  # Stop: the command cannot run safely without its dependencies.
}

# ── runtime selection ──────────────────────────────────────────────────────
# resolve_runtime: sets RT (empty if none is installed); no side effects.
# @brief Choose RT from VALHALLA_RUNTIME/--runtime, or auto-detect container/docker.
# @return 0 on success; dies on an invalid runtime name.
resolve_runtime() {
  RT="${VALHALLA_RUNTIME:-}"  # Start with the user's explicit choice, if any.
  case "$RT" in  # Only docker and container are valid values.
    "" | docker | container) ;;
    *) die "runtime must be 'docker' or 'container', got '$RT'" ;;
  esac
  # Auto-detect only when the user did not choose; prefer Apple container when
  # both runtimes are installed on macOS.
  if [[ -z $RT ]]; then  # Auto-detect only when the user did not choose.
    if command -v container >/dev/null 2>&1; then  # Prefer Apple's container when it is installed.
      RT=container
    elif command -v docker >/dev/null 2>&1; then  # Otherwise fall back to docker.
      RT=docker
    fi
  fi
}

# pick_runtime: RT is known and installed (check_deps ran); start Apple's
# container service if needed.
# @brief Start the Apple container system service when RT is container and it is not running.
# @return 0 on success.
pick_runtime() {
  if [[ $RT == container ]] && ! container system status >/dev/null 2>&1; then  # The container service may need starting before first use.
    step "Starting the container system service"  # Tell the user what is happening.
    container system start  # Start the service.
  fi
}

# rt CMD...: runs `docker CMD...` or `container CMD...`.
# @brief Run a command through the active runtime (docker or container).
# @param @ runtime command and its arguments.
# @return The runtime command's exit status.
rt() { if [[ $RT == docker ]]; then docker "$@"; else container "$@"; fi; }

# ── runtime-specific primitives ────────────────────────────────────────────
# apple_inspect PYTHON-EXPR: evaluates EXPR on the container's inspect JSON (d).
# inspect_json: the container's inspect JSON, cached in a file for this run
# (each `container inspect` is slow; a file also works inside $(...)).
# @brief Print the container's inspect JSON, fetching and caching it once per run.
# @return 0 on success, 1 if inspect fails.
inspect_json() {
  # Populate the cache once per run: repeated inspect calls are slow, and several
  # helpers need the same snapshot after a container change.
  if [[ ! -s $INSPECT_CACHE ]]; then  # Fetch only when there is no cached snapshot yet.
    rt inspect "$NAME" >"$INSPECT_CACHE" 2>/dev/null || { rm -f "$INSPECT_CACHE"; return 1; }  # Store the inspect output; on failure clear the cache and report failure.
  fi
  cat "$INSPECT_CACHE"  # Hand the cached JSON back to the caller.
}

# forget_inspect: call after anything that changes the container.
# @brief Invalidate the inspect cache after any operation that changes the container.
# @return 0 on success.
forget_inspect() { rm -f "$INSPECT_CACHE"; }

# apple_inspect PYTHON-EXPR: evaluates EXPR on the container's inspect JSON (d).
# @brief Evaluate a Python expression against the inspect JSON on Apple container.
# @param 1 Python expression; the inspect data is bound to d.
# @return 0; prints the result, or nothing on failure.
apple_inspect() {
  inspect_json | EXPR="$1" python3 -c '
import json, os, sys
d = json.load(sys.stdin); d = d[0] if isinstance(d, list) else d
print(eval(os.environ["EXPR"]))' 2>/dev/null || true
}

# @brief Test whether the container exists.
# @return 0 when inspect succeeds, non-zero otherwise.
exists() { inspect_json >/dev/null; }

# state: prints "running" when running, anything else otherwise.
# @brief Print the container state, normally the literal word running.
# @return 0 on success.
state() {
  if [[ $RT == docker ]]; then  # Docker can report the status directly.
    docker container inspect -f '{{.State.Status}}' "$NAME" 2>/dev/null || true
  else
    apple_inspect 'd.get("status", {}).get("state", "")'  # Apple container needs Python to read the status from the JSON.
  fi
}

# @brief Test whether the container is currently running.
# @return 0 when running, 1 otherwise.
is_running() { [[ "$(state)" == running ]]; }

# @brief Die with a hint when the container does not exist.
# @return 0 when it exists.
require_exists() { exists || die "container $NAME doesn't exist; use: $PROG run"; }
# @brief Die with a hint unless the container exists and is running.
# @return 0 when running.
require_running() {
  require_exists
  is_running || die "container $NAME is not running; use: $PROG run"
}

# container_env KEY: prints KEY's value in the existing container, if any.
# @brief Print the value of a KEY in the existing container's environment.
# @param 1 environment variable name.
# @return 0; prints nothing when the container is absent.
container_env() {
  exists || return 0  # No container means there is no environment to read.
  if [[ $RT == docker ]]; then  # Docker prints environment lines one per line.
    docker container inspect -f '{{range .Config.Env}}{{println .}}{{end}}' "$NAME" 2>/dev/null |
      sed -n "s/^$1=//p" | head -n1
  else
    KEY="$1" apple_inspect 'next((e[len(os.environ["KEY"]) + 1:] for e in d.get("configuration", {}).get("initProcess", {}).get("environment", []) if e.startswith(os.environ["KEY"] + "=")), "")'  # Apple container reads the env array from the inspect JSON.
  fi
}

# @brief Print TOKEN_SECRET from the existing container.
# @return 0 on success.
old_secret() { container_env TOKEN_SECRET; }

# container_port: the host port the existing container publishes.
# @brief Print the host port published by the existing container.
# @return 0 on success.
container_port() {
  if [[ $RT == docker ]]; then  # Docker reports port bindings in the inspect data.
    docker container inspect -f '{{range $p, $b := .HostConfig.PortBindings}}{{(index $b 0).HostPort}} {{end}}' "$NAME" 2>/dev/null |
      awk '{print $1}'  # Take the first host port when several are bound.
  else
    apple_inspect '(d.get("configuration", {}).get("publishedPorts") or [{}])[0].get("hostPort", "")'  # Apple container stores the published port in its config.
  fi
}

# adopt_container_port: unless --port/CONTAINER_PORT was given, talk to the
# port the container was created with.
# @brief Use the port the container was created with, unless --port/CONTAINER_PORT was given.
# @return 0 on success.
adopt_container_port() {
  [[ -n $PORT_EXPLICIT ]] && return 0  # An explicit --port always wins.
  local p
  p="$(container_port)"  # Otherwise ask the container which host port it publishes.
  if [[ $p =~ ^[0-9]+$ ]]; then  # Only adopt a plausible numeric port.
    PORT="$p"  # Use that port for health checks and URLs.
    set_urls  # Recompute the URLs with the adopted port.
  fi
  return 0
}

# entrypoint_persists: does the running container's entrypoint handle
# persistence (load at start, save on stop, SIGHUP restarts)?
# @brief Test whether the running image's entrypoint implements persistence itself.
# @return 0 when the persistence marker is present, 1 otherwise.
entrypoint_persists() {
  rt exec "$NAME" grep -qF "$ENTRYPOINT_MARK" /entrypoint.sh 2>/dev/null  # The marker inside the image tells us it handles persistence itself.
}

# container_image: image reference the container was created from.
# @brief Print the image reference the container was created from.
# @return 0 on success.
container_image() {
  if [[ $RT == docker ]]; then
    docker container inspect -f '{{.Config.Image}}' "$NAME" 2>/dev/null || true
  else
    apple_inspect 'd.get("configuration", {}).get("image", {}).get("reference", "")'
  fi
}

# container_digest: index digest (sha256:...) of the container's image.
# @brief Print the index digest (sha256:...) of the container's image.
# @return 0 on success.
container_digest() {
  if [[ $RT == docker ]]; then
    local id
    id="$(docker container inspect -f '{{.Image}}' "$NAME" 2>/dev/null)" || return 0  # Get the image ID the container uses.
    docker image inspect -f '{{range .RepoDigests}}{{println .}}{{end}}' "$id" 2>/dev/null |  # Read that image's registry digests.
      sed -n 's/.*@//p' | head -n1
  else
    apple_inspect 'd.get("configuration", {}).get("image", {}).get("descriptor", {}).get("digest", "")'  # Apple container exposes the digest directly in its config.
  fi
}

# exec_it CMD...: runs CMD interactively in the container (replaces this shell).
# @brief Replace this shell with an interactive exec in the container, adding a TTY when available.
# @param @ command and arguments to run in the container.
# @return Does not return; replaces the process via exec.
exec_it() {
  forget_inspect # exec skips the EXIT trap
  local -a tty=(-i)  # Interactive by default so shells behave normally.
  [[ -t 0 && -t 1 ]] && tty=(-it)  # Add a pseudo-terminal only when both streams are terminals.
  if [[ $RT == docker ]]; then
    exec docker exec "${tty[@]}" "$NAME" "$@"  # Replace this shell with the interactive command.
  else
    exec container exec "${tty[@]}" "$NAME" "$@"
  fi
}

# @brief Test whether an image is available locally.
# @param 1 image reference.
# @return 0 when present, non-zero otherwise.
image_present() { rt image inspect "$1" >/dev/null 2>&1; }

# @brief Pull an image using the active runtime.
# @param 1 image reference.
# @return The pull command's exit status.
pull() {
  step "Pulling $1 ($RT)"
  if [[ $RT == docker ]]; then docker pull "$1"; else container image pull "$1"; fi
}

# @brief Force-remove the container and invalidate the inspect cache.
# @return 0; removal errors are ignored.
remove_container() {
  if [[ $RT == docker ]]; then
    docker rm -f "$NAME" >/dev/null 2>&1 || true
  else
    container delete --force "$NAME" >/dev/null 2>&1 || true
  fi
  forget_inspect
}

# @brief Start the container and invalidate the inspect cache.
# @return 0 on success.
start_container() { rt start "$NAME" >/dev/null; forget_inspect; }
# stop_container: graceful stop with time for the entrypoint to save.
# @brief Stop the container gracefully, giving the entrypoint STOP_TIMEOUT seconds to save.
# @return 0 on success.
stop_container() { rt stop -t "$STOP_TIMEOUT" "$NAME" >/dev/null; forget_inspect; }
# @brief Print the container's logs from both streams.
# @return The runtime command's exit status.
logs() { rt logs "$NAME" 2>&1; }

# db_args: prints "user db port" of the bundled database.
# @brief Print 'user db port' of the bundled database using the container's environment.
# @return 0, with postgres/5432 defaults.
db_settings() {
  local user db port  # Database user, name and port read from the container environment.
  user="$(container_env DB_USER)"  # Fall back to postgres when the container did not set it.
  db="$(container_env DB_NAME)"
  port="$(container_env DB_PORT)"
  echo "${user:-postgres} ${db:-postgres} ${port:-5432}"  # Print defaults for anything the container did not set.
}

# ── persistence ────────────────────────────────────────────────────────────
# With persistence on, the database is dumped to persist_file before the
# container stops or is replaced, and restored after it starts. The setting
# lives in the container as VALHALLA_PERSIST=1, so later commands remember it.
# @brief Print the path of the persistence dump for the current container name.
# @return 0 on success.
persist_file() { echo "$DUMP_DIR/${NAME}-persist.sql"; }

# migrate_persist_file: earlier versions kept the persist dump next to the
# script ($ROOT/dumps); move it to DUMP_DIR once.
# @brief Move a legacy persistence dump from ROOT/dumps into DUMP_DIR once.
# @return 0 on success.
migrate_persist_file() {
  local old="$ROOT/dumps/${NAME}-persist.sql" new
  new="$(persist_file)"
  [[ $old != "$new" && -f $old && ! -f $new ]] || return 0
  mkdir -p "$DUMP_DIR"
  mv "$old" "$new"
  [[ -f $old.prev ]] && mv "$old.prev" "$new.prev"
  step "Moved the saved database from $old to $new"
}

# persist_on: is persistence on for the existing container (or forced by flag)?
# @brief Test whether persistence is on, from the flags or the container's setting.
# @return 0 when enabled, 1 otherwise.
persist_on() {
  case "$PERSIST" in 1) return 0 ;; 0) return 1 ;; esac
  [[ "$(container_env VALHALLA_PERSIST)" == 1 ]]
}

# persist_wanted: should the container about to be created persist?
# --persist/--no-persist, else VALHALLA_PERSIST from the env file or shell,
# else the replaced container's setting.
# @brief Decide whether a container about to be created should persist, from flags, env or the old container.
# @return 0 when wanted, 1 otherwise.
persist_wanted() {
  case "$PERSIST" in 1) return 0 ;; 0) return 1 ;; esac
  local v
  v="$(env_get VALHALLA_PERSIST || true)"
  [[ -n ${VALHALLA_PERSIST:-} ]] && v="$VALHALLA_PERSIST"
  [[ -n $v ]] || v="$OLD_PERSIST"
  [[ $v == 1 ]]
}

# dump_to FILE: pg_dump of the bundled database into FILE (0 on success).
# dump_to FILE: pg_dump of the bundled database into FILE (0 on success);
# records the image digest next to it (FILE.image) for restore checks.
# @brief pg_dump the bundled database into FILE and record the image digest alongside it.
# @param file destination SQL file path.
# @return 0 on success; 1 and removes FILE on failure.
dump_to() {
  local file="$1" user db port digest  # Destination file, DB settings and the recorded image digest.
  read -r user db port <<<"$(db_settings)"  # Split the DB settings into three variables.
  mkdir -p "$(dirname "$file")"  # Create the destination folder if it does not exist.
  if rt exec "$NAME" su-exec postgres pg_dump -p "$port" -U "$user" -d "$db" \
    --clean --if-exists --no-owner --no-privileges >"$file" 2>/dev/null && [[ -s $file ]]; then
    digest="$(container_digest)"  # Remember which image produced the dump.
    [[ -n $digest ]] && echo "$digest" >"$file.image"  # Save the digest in a sidecar file for later restore checks.
    return 0
  fi
  rm -f "$file"  # A failed dump must not leave a partial file behind.
  return 1
}

# check_dump_image FILE: warns when FILE was made by a different image (its
# schema may differ from the running one). Prints nothing when unknown.
# @brief Test whether a dump was made by a different image than the current container.
# @param file dump file whose .image sidecar is checked.
# @return 0 when images differ, 1 when equal or unknown.
dump_image_differs() {
  local file="$1" saved now
  [[ -f $file.image ]] || return 1
  saved="$(cat "$file.image")"
  now="$(container_digest)"
  [[ -n $now && -n $saved && $now != "$saved" ]]
}

# restore_from FILE: loads FILE into the bundled database (0 on success).
# restore_from FILE: loads FILE into the bundled database (0 on success),
# then restarts the server process so its in-memory caches start clean
# (images whose entrypoint supports it; older ones keep running as is).
# @brief Load a SQL dump into the bundled database and restart the server process when supported.
# @param file SQL dump to load.
# @return 0 on success, 1 on psql or health-check failure.
restore_from() {
  local file="$1" user db port  # Dump file to load and the database settings.
  # ON_ERROR_STOP makes psql fail on the first SQL error, so a partially loaded
  # dump is reported instead of silently counting as a successful restore.
  read -r user db port <<<"$(db_settings)"  # Split the DB settings into three variables.
  if [[ $RT == docker ]]; then  # Feed the SQL file into psql through standard input.
    docker exec -i "$NAME" su-exec postgres psql -q -v ON_ERROR_STOP=1 -p "$port" -U "$user" -d "$db" <"$file" >/dev/null || return 1
  else
    container exec -i "$NAME" su-exec postgres psql -q -v ON_ERROR_STOP=1 -p "$port" -U "$user" -d "$db" <"$file" >/dev/null || return 1
  fi
  if entrypoint_persists; then  # Newer images can restart the server themselves.
    step "Restarting the server so it starts from the restored data"
    rt exec "$NAME" kill -HUP 1 >/dev/null 2>&1 || true  # SIGHUP tells the entrypoint to restart the server process.
    sleep 1  # Give the restart a moment before checking health.
    wait_healthy >/dev/null || return 1  # Fail the restore if the server does not come back.
  else
    info "this image can't restart the server alone; cached sessions may be stale until: $PROG restart"  # Older images need a manual restart to clear cached sessions.
  fi
  return 0
}

# persist_save: dumps the running database to persist_file (the previous
# dump is kept as .prev). Dies if the dump fails, before anything is stopped.
# @brief Save the running database to persist_file, rotating the previous dump to .prev.
# @return 0 on success; dies before stopping anything if the dump fails.
persist_save() {
  is_running || return 0
  local file
  file="$(persist_file)"
  migrate_persist_file
  [[ -f $file ]] && mv -f "$file" "$file.prev"
  [[ -f $file.image ]] && mv -f "$file.image" "$file.prev.image"
  step "Saving the database to ${E_VAL}$file${E_R}"
  if ! dump_to "$file"; then
    [[ -f $file.prev ]] && mv -f "$file.prev" "$file"
    [[ -f $file.prev.image ]] && mv -f "$file.prev.image" "$file.image"
    die "couldn't save the database, so nothing was stopped (use --no-persist to skip saving)"
  fi
}

# save_before_replace: should recreate/plan save the running database first?
# Yes if the old container persisted (unless --no-persist). When --persist
# turns it on for a container that didn't persist, only if nothing is saved
# yet: an existing saved file is what the new container restores.
# @brief Decide whether the running database must be saved before the container is replaced.
# @return 0 when a save is required, 1 otherwise.
save_before_replace() {
  [[ $PERSIST == 0 ]] && return 1
  [[ $OLD_PERSIST == 1 ]] && return 0
  [[ $PERSIST == 1 && ! -f "$(persist_file)" ]]
}

# persist_load: restores persist_file into the freshly started container.
# persist_load: after the container started. Images whose entrypoint handles
# persistence already loaded the dump at start; for older images, load it now.
# @brief Restore the saved dump into a freshly started container when the entrypoint did not already do so.
# @return 0 on success.
persist_load() {
  local file
  file="$(persist_file)"
  if entrypoint_persists; then
    if logs | grep -q "valhalla: restoring the database"; then
      ok "The database was restored from $file at start"
    fi
    return 0
  fi
  if [[ ! -f $file ]]; then
    info "Persistence is on; no saved database yet ($file)"
    return 0
  fi
  step "Restoring the database from $file"
  restore_from "$file" || warn "restoring $file failed; the database is the image's fresh one"
}

# warn_dump_image: before a container starts from a saved dump.
# @brief Warn when the saved dump was created by a different image and may restore an old schema.
# @param 1 digest of the image about to be used.
# @return 0 on success.
warn_dump_image() {
  local file
  file="$(persist_file)"
  if [[ -f $file.image && -n ${1:-} && "$(cat "$file.image")" != "$1" ]]; then
    warn "$file was saved by a different image; if this image changed the database
         schema, the old schema comes back (start fresh with --no-persist)"
  fi
}

# ── env file ───────────────────────────────────────────────────────────────
# load_env: parses ENV_FILE into ENV_KEYS/ENV_VALS. Later duplicates win.
# parse_env_line LINE: sets PK/PV from a KEY=VALUE line (spaces around "=",
# quotes and " # comments" handled); returns 1 for blank/comment/invalid lines.
# @brief Parse a KEY=VALUE line into PK/PV, stripping quotes, whitespace and trailing comments.
# @param line raw line from the env file.
# @return 0 when parsed; 1 for blank, comment or invalid lines.
parse_env_line() {
  local line="${1%$'\r'}" q rest  # Strip a Windows carriage return and set up quote-handling variables.
  PK=""; PV=""  # Clear the parsed key and value output variables.
  [[ $line =~ ^[[:space:]]*(#|$) ]] && return 1  # Blank lines and comment lines are ignored.
  [[ $line == *=* ]] || return 1  # A settings line must contain an equals sign.
  PK="${line%%=*}"  # The key is everything before the first equals sign.
  PV="${line#*=}"  # The value is everything after it.
  PK="${PK#"${PK%%[![:space:]]*}"}"; PK="${PK%"${PK##*[![:space:]]}"}"
  PK="${PK#export }"  # Drop an optional leading export keyword.
  PV="${PV#"${PV%%[![:space:]]*}"}"; PV="${PV%"${PV##*[![:space:]]}"}"
  # Quote handling follows the .env convention: quoted values keep their interior
  # and drop trailing comments; unquoted values end at " # ...".
  q="${PV:0:1}"  # The first character decides whether the value is quoted.
  if [[ ($q == '"' || $q == "'") && ${PV:1} == *"$q"* ]]; then
    # Quoted: keep what's inside the quotes; anything after (a comment) goes.
    rest="${PV:1}"
    PV="${rest%%"$q"*}"
  else
    # Unquoted: " # comment" ends the value.
    PV="${PV%%[[:space:]]#*}"  # An unquoted value ends at a " # comment".
    PV="${PV%"${PV##*[![:space:]]}"}"
  fi
  [[ $PK =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || { note "warning: skipping invalid key '$PK'"; return 1; }  # Skip keys that are not valid variable names.
  return 0
}

# @brief Parse ENV_FILE into the parallel ENV_KEYS/ENV_VALS arrays; later duplicates win.
# @return 0; dies when an explicitly requested env file is missing.
load_env() {
  [[ $ENV_FILE == none ]] && return 0
  if [[ ! -f $ENV_FILE ]]; then
    [[ -n $ENV_FILE_EXPLICIT ]] && die "env file not found: $ENV_FILE"
    return 0
  fi
  local line
  while IFS= read -r line || [[ -n $line ]]; do
    parse_env_line "$line" || continue
    env_set "$PK" "$PV"
  done <"$ENV_FILE"
  info "Env file: ${E_VAL}$ENV_FILE${E_R} (${#ENV_KEYS[@]} variables)"
}

# env_file_get FILE KEY: prints KEY's value from FILE (exit 1 if not set).
# @brief Read a KEY's value directly from a given env file.
# @param file file to inspect.
# @param 2 environment variable name.
# @return 0 with value printed when set, 1 otherwise.
env_file_get() {
  local line found=1 val=""
  [[ -f $1 ]] || return 1
  while IFS= read -r line || [[ -n $line ]]; do
    parse_env_line "$line" 2>/dev/null || continue
    [[ $PK == "$2" ]] && { val="$PV"; found=0; }
  done <"$1"
  [[ $found == 0 ]] && printf '%s' "$val"
  return "$found"
}

CONFIG_FILE="$DATA_DIR/config"

# new_secret: 64 random hex characters.
# @brief Generate a random 64-character hex token secret.
# @return 0; prints the secret.
new_secret() {
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -hex 32
  else
    od -An -tx1 -N32 /dev/urandom | tr -d ' \n'
  fi
}

# save_secret_to_config SECRET: adds or replaces TOKEN_SECRET in CONFIG_FILE
# (created owner-only if missing).
# @brief Add or replace TOKEN_SECRET in CONFIG_FILE, creating it owner-only if needed.
# @param secret value to store.
# @return 0 on success.
save_secret_to_config() {
  local secret="$1" tmp
  mkdir -p "$(dirname "$CONFIG_FILE")"
  if [[ ! -f $CONFIG_FILE ]]; then
    (umask 077 && : >"$CONFIG_FILE")
  fi
  if grep -qE '^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=' "$CONFIG_FILE"; then
    tmp="$(mktemp "$CONFIG_FILE.XXXXXX")"
    SECRET="$secret" awk '
      /^[[:space:]]*(export[[:space:]]+)?TOKEN_SECRET[[:space:]]*=/ && !done { print "TOKEN_SECRET = \"" ENVIRON["SECRET"] "\""; done = 1; next }
      { print }' "$CONFIG_FILE" >"$tmp"
    chmod 600 "$tmp"
    mv "$tmp" "$CONFIG_FILE"
  else
    printf '\n# JWT signing secret (keep it private; changing it logs everyone out)\nTOKEN_SECRET = "%s"\n' "$secret" >>"$CONFIG_FILE"
  fi
}

# resolve_token_secret SAVE [OLD]: sets TOKEN_VALUE and TOKEN_SOURCE. Order: --token-secret, TOKEN_SECRET in the shell, the env file in
# use, the install directory's config, the old container's (saved to config),
# else a new one (saved to config). With SAVE=0 nothing is written, and a
# secret that would be generated stays empty.
# @brief Resolve the TOKEN_SECRET source by precedence and optionally persist it to config.
# @param save 1 to actually save a reused or generated secret.
# @param old fallback secret from the container being replaced.
# @return 0; sets TOKEN_VALUE and TOKEN_SOURCE.
resolve_token_secret() {
  local save="$1" old="${2:-}" v  # Whether to actually save, the old container's secret and a scratch value.
  # Precedence is narrowest-scope first: flag, shell, chosen env file, install
  # config, old container, then a generated secret, so an explicit choice is
  # never silently overridden by a broader default.
  TOKEN_VALUE=""  # Default: no secret chosen yet.
  if [[ -n $CLI_TOKEN_SECRET ]]; then  # The --token-secret flag has the highest priority.
    TOKEN_SOURCE="--token-secret"; TOKEN_VALUE="$CLI_TOKEN_SECRET"; return 0
  fi
  if [[ -n ${TOKEN_SECRET:-} ]]; then  # Next, a secret exported in the shell.
    TOKEN_SOURCE="the shell"; TOKEN_VALUE="$TOKEN_SECRET"; return 0
  fi
  if v="$(env_get TOKEN_SECRET)" && [[ -n $v ]]; then  # Next, a value from the env file currently in use.
    TOKEN_SOURCE="$ENV_FILE"; TOKEN_VALUE="$v"; return 0
  fi
  if [[ $CONFIG_FILE != "$ENV_FILE" ]] && v="$(env_file_get "$CONFIG_FILE" TOKEN_SECRET)" && [[ -n $v ]]; then  # Next, the install directory config, even when a .env is in use.
    TOKEN_SOURCE="$CONFIG_FILE"; TOKEN_VALUE="$v"; return 0
  fi
  if [[ -n $old ]]; then  # Next, reuse the old container's secret and save it.
    TOKEN_SOURCE="the old container ($([[ $save == 1 ]] && echo saved || echo "would be saved") to $CONFIG_FILE)"; TOKEN_VALUE="$old"
    [[ $save == 1 ]] && save_secret_to_config "$old"
    return 0
  fi
  if [[ $save == 1 ]]; then  # Last resort: generate a brand-new secret.
    TOKEN_SOURCE="generated (saved to $CONFIG_FILE)"
  else
    TOKEN_SOURCE="would be generated once and saved to $CONFIG_FILE"
  fi
  if [[ $save == 1 ]]; then
    TOKEN_VALUE="$(new_secret)"  # Generate the secret and then save it to config.
    save_secret_to_config "$TOKEN_VALUE"
  fi
  return 0
}
# env_get KEY: prints the value; exit 1 if the key isn't set.
# @brief Print the composed value for KEY.
# @param 1 environment variable name.
# @return 0 with value printed when set, 1 otherwise.
env_get() {
  local i
  for i in "${!ENV_KEYS[@]}"; do
    if [[ ${ENV_KEYS[$i]} == "$1" ]]; then
      printf '%s' "${ENV_VALS[$i]}"
      return 0
    fi
  done
  return 1
}

# env_set KEY VALUE: adds or replaces a value.
# @brief Add or replace a value in the composed environment.
# @param 1 environment variable name.
# @param 2 value to store.
# @return 0 on success.
env_set() {
  local i
  for i in "${!ENV_KEYS[@]}"; do
    if [[ ${ENV_KEYS[$i]} == "$1" ]]; then
      ENV_VALS[i]="$2"
      return 0
    fi
  done
  ENV_KEYS+=("$1")
  ENV_VALS+=("$2")
}

# compose_env [OLD_TOKEN_SECRET] [SAVE]: applies shell overrides and the
# token secret on top of the env file, giving the final container
# environment. SAVE=1 lets it save a reused or generated secret to config.
# @brief Build the final container environment from the env file, shell overrides, persistence and token secret.
# @param old_secret fallback secret from the old container.
# @param save 1 to allow saving a reused or generated secret.
# @return 0; updates ENV_KEYS/ENV_VALS.
compose_env() {
  local old_secret_value="${1:-}" save="${2:-0}"  # Fallback secret from the old container and whether saving is allowed.

  # Env file < shell. Anything set in neither isn't passed, so the server
  # uses its own defaults (the installer's .env sets small local ones).
  [[ -n ${SERVER_THREADS:-} ]] && env_set SERVER_THREADS "$SERVER_THREADS"  # Each setting exported in the shell overrides the env-file value.
  [[ -n ${DB_MAX_CONN:-} ]] && env_set DB_MAX_CONN "$DB_MAX_CONN"
  [[ -n ${DB_ACQUIRE_TIMEOUT_MS:-} ]] && env_set DB_ACQUIRE_TIMEOUT_MS "$DB_ACQUIRE_TIMEOUT_MS"
  [[ -n ${MAX_REQUESTS:-} ]] && env_set MAX_REQUESTS "$MAX_REQUESTS"
  [[ -n ${MAX_FPS:-} ]] && env_set MAX_FPS "$MAX_FPS"
  [[ -n ${PERMISSION_CACHE_TTL:-} ]] && env_set PERMISSION_CACHE_TTL "$PERMISSION_CACHE_TTL"
  [[ -n ${PASSWORD_WORKERS:-} ]] && env_set PASSWORD_WORKERS "$PASSWORD_WORKERS"
  [[ -n ${PASSWORD_QUEUE_MAX:-} ]] && env_set PASSWORD_QUEUE_MAX "$PASSWORD_QUEUE_MAX"
  [[ -n ${DB_WORKERS:-} ]] && env_set DB_WORKERS "$DB_WORKERS"
  [[ -n ${DB_QUEUE_MAX:-} ]] && env_set DB_QUEUE_MAX "$DB_QUEUE_MAX"
  [[ -n ${DB_IO_FASTPATH:-} ]] && env_set DB_IO_FASTPATH "$DB_IO_FASTPATH"
  # Record the decision in the container env (1 or 0) so later commands inherit
  # persistence without re-reading flags or the env file.
  [[ -n ${DB_ASYNC:-} ]] && env_set DB_ASYNC "$DB_ASYNC"
  if persist_wanted; then  # Record the persistence decision in the container's own environment.
    env_set VALHALLA_PERSIST 1  # Turn persistence on.
    env_set VALHALLA_PERSIST_FILE "/persist/${NAME}-persist.sql"  # Path inside the container where the dump is kept.
  elif env_get VALHALLA_PERSIST >/dev/null || [[ -n $OLD_PERSIST ]]; then
    env_set VALHALLA_PERSIST 0  # Explicitly record persistence as off.
  fi
  # Always pass a secret, so the entrypoint never makes a throwaway one.
  resolve_token_secret "$save" "$old_secret_value"  # Decide which token secret source to use.
  [[ -n $TOKEN_VALUE ]] && env_set TOKEN_SECRET "$TOKEN_VALUE"  # Always pass a secret so the image never creates a throwaway one.
  return 0
}

# print_env: the composed environment, sorted, secrets hidden.
# @brief Print the composed environment sorted, hiding values whose key looks secret-like.
# @return 0 on success.
print_env() {
  local i key
  for i in "${!ENV_KEYS[@]}"; do
    key="${ENV_KEYS[$i]}"
    if [[ $key =~ (PASS|SECRET|KEY) ]]; then
      echo "$key=<hidden>"
    else
      echo "$key=${ENV_VALS[$i]}"
    fi
  done | sort
}

# create_container <image> [fallback TOKEN_SECRET, e.g. the old container's]
# create_container <image> [fallback TOKEN_SECRET, e.g. the old container's]
# @brief Create the container from an image with a private env file, persistence mount and resource limits.
# @param image image reference to run.
# @param fallback_secret old container's TOKEN_SECRET to reuse when needed.
# @return 0 on success.
create_container() {
  local image="$1" i  # Image to run and a loop index over the environment variables.
  compose_env "${2:-}" 1  # Build the final environment and allow saving a generated secret.
  [[ $TOKEN_SOURCE == generated* || $TOKEN_SOURCE == "the old container"* ]] &&  # Tell the user when a secret was generated or reused.
    info "TOKEN_SECRET: $TOKEN_SOURCE"

  local server_port server_host  # Port the server listens on inside the container and its bind address.
  server_port="$(env_get SERVER_PORT || echo 8080)"  # From the env file, or the default 8080.
  server_host="$(env_get SERVER_HOST || echo 0.0.0.0)"  # From the env file, or all interfaces.
  # By default, publish the container on CONTAINER_PORT from the env file (if set),
  # otherwise on the same port the server listens on.
  if [[ "${PORT_EXPLICIT:-0}" != "1" ]]; then  # Use the env file's host port only when --port was not given.
    PORT="$(env_get CONTAINER_PORT || echo "$server_port")"  # Publish the configured port, defaulting to the server's own port.
  fi
  case "$server_host" in  # Warn about a bind address the published port cannot reach.
    127.* | localhost | ::1)
      warn "SERVER_HOST=$server_host only listens inside the container; the published port won't reach it. Use 0.0.0.0." ;;
  esac

  # Variables go through a private env file, not -e KEY=VALUE, so secrets
  # don't show up in the process list.
  ENV_TMP="$(mktemp "${TMPDIR:-/tmp}/valhalla-env.XXXXXX")"  # A private temp file keeps secrets out of the process list.
  chmod 600 "$ENV_TMP"  # Only the owner may read it.
  for i in "${!ENV_KEYS[@]}"; do  # Write each KEY=VALUE pair into the private file.
    printf '%s=%s\n' "${ENV_KEYS[$i]}" "${ENV_VALS[$i]}" >>"$ENV_TMP"
  done

  local -a args=(-d --name "$NAME" -p "${PORT}:${server_port}" --env-file "$ENV_TMP")  # Base run arguments: detached, named, published port and private env file.
  if [[ "$(env_get VALHALLA_PERSIST || true)" == 1 ]]; then  # With persistence on, mount the dump folder into the container.
    migrate_persist_file
    mkdir -p "$DUMP_DIR"
    # :z relabels the folder for SELinux (Fedora, RHEL); without it the
    # container can't write there. Only Docker on Linux needs or knows it.
    if [[ $RT == docker && $(uname -s) == Linux ]]; then
      args+=(-v "$DUMP_DIR:/persist:z")  # The :z suffix relabels the folder for SELinux on Linux.
    else
      args+=(-v "$DUMP_DIR:/persist")
    fi
  # Resource flags differ by runtime: Docker uses --cpus/--memory, Apple
  # container uses -c/-m.
  fi
  if [[ $RT == docker ]]; then  # Resource flags differ between the two runtimes.
    args+=(--cpus "$CPUS" --memory "${MEMORY_GB}g")
  else
    args+=(-c "$CPUS" -m "${MEMORY_GB}G")
  fi
  rt run "${args[@]}" "$image" >/dev/null  # Actually create and start the container.
  rm -f "$ENV_TMP"  # Remove the temporary env file now that it is no longer needed.
  ENV_TMP=""  # Clear the variable so the EXIT trap does not try to remove it twice.
  forget_inspect  # The cached inspect data is now stale.
}

# plan_recreate <image> <action>: dry-run output for run/update/build.
# plan_recreate <image> <action>: dry-run output for run/update/build.
# @brief Print a dry-run plan of a create/recreate without changing anything.
# @param image image reference that would be used.
# @param action description of the image-fetch step.
# @return 0 on success.
plan_recreate() {
  OLD_PERSIST="$(container_env VALHALLA_PERSIST)"
  compose_env "$(old_secret)"
  printf '%s\n' "${S_B}Dry run${S_R} ($RT), nothing changed. Would:"
  printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}$2"
  if exists && is_running && save_before_replace; then
    printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}save the database to ${S_VAL}$(persist_file)${S_R}"
  fi
  exists && printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}stop and remove container ${S_VAL}$NAME${S_R}"
  printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}create ${S_VAL}$NAME${S_R} from $1: port ${PORT}->$(env_get SERVER_PORT || echo 8080), ${CPUS} CPUs, ${MEMORY_GB} GB"
  if persist_wanted; then
    printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}mount $DUMP_DIR at /persist; the database is loaded from ${S_VAL}$(persist_file)${S_R}"
  fi
  printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}TOKEN_SECRET from: ${S_VAL}$TOKEN_SOURCE${S_R}"
  printf '%s\n' "  with these variables:"
  print_env | sed 's/^/    /'
  [[ ${PLAN_PRUNE:-0} == 1 ]] && prune_images
  return 0
}

# ── helpers ────────────────────────────────────────────────────────────────
# @brief Poll the health URL until the server answers or WAIT_SECONDS elapses, dumping logs on failure.
# @return 0 when healthy, 1 on timeout.
wait_healthy() {
  local waited=0  # Elapsed-seconds counter.
  printf '%s' "${E_ACC}${E_i_wait:+$E_i_wait$E_GAP}${E_R}Waiting for the server" >&2
  # Poll in short intervals: the server needs time to boot, and dots give
  # feedback without flooding the output.
  while [[ $waited -lt $WAIT_SECONDS ]]; do  # Keep probing until the timeout; stop as soon as the server answers.
    if curl -fsS "$HEALTH_URL" >/dev/null 2>&1; then  # A successful HTTP call means the server is ready.
      printf '\n' >&2
      ok "up at ${E_VAL}$HEALTH_URL${E_R}"
      server_version_line
      return 0
    fi
    printf '%s' "${E_DIM}.${E_R}" >&2  # Print a dot as progress feedback.
    sleep 2  # Wait before the next probe.
    waited=$((waited + 2))  # Count the two seconds we just waited.
  done
  printf '%s\n' "${E_ERR}${E_i_err:+$E_i_err$E_GAP}not answering after ${WAIT_SECONDS}s${E_R} (raise it with VALHALLA_WAIT=SECONDS). Recent logs:" >&2
  logs | tail -n 20  # On failure, show recent logs to help diagnose the problem.
  return 1
}

# server_version_line: the version the server prints in its startup banner.
# @brief Print the server version parsed from its startup banner in the logs.
# @return 0 on success.
server_version_line() {
  local line
  line="$(logs | sed -E $'s/\x1b\\[[0-9;]*m//g' | grep -m1 -E ' - Version +:' || true)"
  [[ -n $line ]] || line="$(logs | sed -E $'s/\x1b\\[[0-9;]*m//g' | grep -m1 -E '\] Version: ' || true)"
  if [[ -n $line ]]; then
    line="$(printf '%s\n' "$line" | sed -E 's/[[:space:]]*:[[:space:]]*/: /')"
    if [[ -t 1 && -n ${S_i_version:-} ]]; then
      line="${line# - }"
      printf '%s%s\n' "$S_i_version$S_VERSION_GAP" "$line"
    else
      echo "$line"
    fi
  fi
  return 0
}

# run_success: the "it's up" banner with the URLs, after run/update/build/
# restart. Always printed (plain when stdout isn't a terminal), because the
# URLs are the point.
# @brief Print the 'Valhalla is up' banner with API and docs URLs.
# @return 0 on success.
run_success() {
  is_running || return 0
  {
    echo
    printf '%s\n' "${S_ACC}${S_i_rocket:+$S_i_rocket$S_GAP}${S_B}Valhalla is up${S_R} ${S_OK}${S_i_ok:+$S_i_ok$S_GAP}${S_R}"
    printf '  %s%s%-10s%s %s\n' "$S_LBL" "${S_i_link:+$S_i_link$S_GAP}" "API:" "$S_R" "${S_VAL}${HEALTH_URL}${S_R}"
    printf '  %s%s%-10s%s %s\n' "$S_LBL" "${S_i_book:+$S_i_book$S_GAP}" "Docs:" "$S_R" "${S_INFO}${DOC_URL}${S_R}"
    printf '  %s%s%-10s%s %s\n' "$S_LBL" "${S_i_server:+$S_i_server$S_GAP }" "Server:" "$S_R" "${S_VAL}$NAME${S_R} ${S_DIM}(running)${S_R}"
  }
}

# @brief Print IMAGE_REPO with the requested tag, defaulting to latest.
# @param 1 image tag.
# @return 0 on success.
image_ref() { echo "${IMAGE_REPO}:${1:-latest}"; }

# @brief Normalize uname -m output to arm64 or amd64 for deps image tags.
# @return 0; prints the architecture.
host_arch() {
  case "$(uname -m)" in
    arm64 | aarch64) echo arm64 ;;
    x86_64 | amd64) echo amd64 ;;
    *) uname -m ;;
  esac
}

# registry_digest <repo> <tag>: index digest on Docker Hub (anonymous).
# @brief Look up an image's index digest on Docker Hub with an anonymous token.
# @param repo repository name.
# @param tag image tag.
# @return 0 with the digest printed, 1 on failure.
registry_digest() {
  local repo="${1#docker.io/}" tag="$2" token
  [[ $repo == */* ]] || repo="library/$repo"
  # Anonymous registry access needs a short-lived bearer token; the manifest
  # HEAD reply carries the index digest in Docker-Content-Digest.
  token="$(curl -fsS -m 10 "https://auth.docker.io/token?service=registry.docker.io&scope=repository:${repo}:pull" |
    sed -n 's/.*"token": *"\([^"]*\)".*/\1/p')" || return 1
  [[ -n $token ]] || return 1
  curl -fsSI -m 10 \
    -H "Authorization: Bearer $token" \
    -H "Accept: application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json, application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json" \
    "https://registry-1.docker.io/v2/${repo}/manifests/${tag}" |
    tr -d '\r' | sed -n 's/^[Dd]ocker-[Cc]ontent-[Dd]igest: //p'
}

# ensure_builder: Apple's default builder (2 GB) runs out of memory compiling
# the server; give it VALHALLA_BUILD_MEMORY GB.
# ensure_builder: Apple's default builder (2 GB) runs out of memory compiling
# the server; give it VALHALLA_BUILD_MEMORY GB. This restarts the builder and
# affects other builds too, so it says what it changes.
# @brief Restart Apple's builder with more memory when compiling the server needs it.
# @return 0; no-op for Docker.
ensure_builder() {
  [[ $RT == container ]] || return 0
  local want_gb="${VALHALLA_BUILD_MEMORY:-8}" have cpus
  have="$(container builder status --format json 2>/dev/null | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin); d = d[0] if isinstance(d, list) else d
    print(d["configuration"]["resources"]["memoryInBytes"] // (1 << 30))
except Exception:
    print(0)' 2>/dev/null || echo 0)"
  [[ $have -ge $want_gb ]] && return 0
  cpus="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
  info "The Apple builder has ${have} GB; compiling needs about ${want_gb} GB."
  info "Restarting it with ${want_gb} GB and ${cpus} CPUs (this applies to all your builds;"
  info "set VALHALLA_BUILD_MEMORY to change, or restart it yourself with: container builder start)"
  container builder stop >/dev/null 2>&1 || true
  container builder start --cpus "$cpus" --memory "${want_gb}g" >/dev/null
}

# ── image cleanup ──────────────────────────────────────────────────────────
# After update/build: remove this tool's older images that no container uses
# (never the one the container runs, never the -deps build image), then free
# leftover layers. Docker: only dangling images that came from this repo.
# Apple container can't filter by origin; its prune only removes dangling
# (untagged, unreferenced) images.

# images_size: the runtime's total image size, for the before/after report.
# @brief Print the runtime's total image size for before/after cleanup reports.
# @return 0 on success.
images_size() {
  if [[ $RT == docker ]]; then
    docker system df --format '{{.Type}} {{.Size}}' 2>/dev/null | sed -n 's/^Images //p'
  else
    container system df 2>/dev/null | awk '$1 == "Images" {print $4, $5}'
  fi
}

# old_image_refs: repo:tag of this tool's run images that no container uses,
# except KEEP (the image the container runs now).
# @brief Print this tool's run image references unused by any container, except KEEP.
# @param keep image reference that must be preserved.
# @return 0 on success.
old_image_refs() {
  local keep="${1#docker.io/}" used refs ref
  local pattern="^((docker\.io/)?${IMAGE_REPO#docker.io/}|${LOCAL_REPO}):"
  if [[ $RT == docker ]]; then
    used="$(docker ps -a --format '{{.Image}}' 2>/dev/null | sed 's|^docker\.io/||' | sort -u || true)"
    refs="$(docker image ls --format '{{.Repository}}:{{.Tag}}' 2>/dev/null | grep -E "$pattern" | grep -v ':<none>$' || true)"
  else
    used="$(container list --all 2>/dev/null | awk 'NR > 1 {print $2}' | sed 's|^docker\.io/||' | sort -u || true)"
    refs="$(container image list 2>/dev/null | awk 'NR > 1 && $2 != "<none>" {print $1 ":" $2}' | grep -E "$pattern" || true)"
  fi
  while IFS= read -r ref; do
    [[ -n $ref ]] || continue
    ref="${ref#docker.io/}"
    [[ $ref == "$keep" ]] && continue
    grep -qxF "$ref" <<<"$used" && continue
    echo "$ref"
  done <<<"$refs"
}

# prune_images: the cleanup after update/build.
# @brief Remove unused older images and dangling layers after update/build unless --no-prune.
# @return 0 on success.
prune_images() {
  [[ $NO_PRUNE == 1 ]] && return 0
  local keep before after ref id removed=0
  # Never remove the image the current container runs, even when its tag matches
  # the "old image" pattern.
  keep="$(container_image)"
  if [[ $DRY_RUN == 1 ]]; then
    printf '%s\n' "  ${S_ACC}${S_i_step:+$S_i_step$S_GAP}${S_R}then remove unused older images:"
    old_image_refs "$keep" | sed 's/^/    /'
    if [[ $RT == docker ]]; then
      echo "    and dangling images left by earlier pulls of $IMAGE_REPO"
    else
      echo "    and dangling images (container image prune)"
    fi
    return 0
  fi
  before="$(images_size)"
  while IFS= read -r ref; do
    [[ -n $ref ]] || continue
    if rt image rm "$ref" >/dev/null 2>&1 || rt image delete "$ref" >/dev/null 2>&1; then
      say_ok "Removed old image $ref"
      removed=$((removed + 1))
    fi
  done < <(old_image_refs "$keep")
  if [[ $RT == docker ]]; then
    # Docker cannot report origin for dangling layers, so keep only those whose
    # RepoDigests point at this repo; Apple container has no such filter.
    for id in $(docker image ls -q --filter dangling=true 2>/dev/null); do
      docker image inspect -f '{{range .RepoDigests}}{{println .}}{{end}}' "$id" 2>/dev/null |
        grep -qE "(^|/)${IMAGE_REPO#docker.io/}@" || continue
      docker image rm "$id" >/dev/null 2>&1 && removed=$((removed + 1))
    done
  else
    container image prune >/dev/null 2>&1 || true
  fi
  after="$(images_size)"
  if [[ -n $before && -n $after && $before != "$after" ]]; then
    ok "Freed image space: $before -> $after"
  elif [[ $removed -gt 0 ]]; then
    ok "Removed $removed old image(s)"
  fi
  return 0
}

# ── update check ───────────────────────────────────────────────────────────
# A background job (at most every VALHALLA_UPDATE_INTERVAL seconds) checks
# GitHub and Docker Hub and writes UPDATE_STATE; every run only reads that
# file, so commands never wait on the network. The notice goes to stderr and
# only to an interactive terminal.
REPO_SLUG="dr3mro/Valhalla-server"
UPDATE_STATE="$DATA_DIR/update-state"
UPDATE_INTERVAL="${VALHALLA_UPDATE_INTERVAL:-21600}"

# @brief Read one KEY=value field from the cached update-state file.
# @param 1 field name.
# @return 0; prints the value or nothing.
state_get() { [[ -f $UPDATE_STATE ]] && sed -n "s/^$1=//p" "$UPDATE_STATE" | head -n1; return 0; }

# forget_update_state: after update/build/install, so the next run re-checks
# instead of showing what was just installed.
# @brief Delete the cached update state so the next run re-checks.
# @return 0 on success.
forget_update_state() { rm -f "$UPDATE_STATE"; }

# tool_installed_sha: commit that last touched valhalla.sh for this installed
# copy; empty for a checkout or a copy installed from a local checkout
# (nothing to compare).
# @brief Print the commit SHA of an installed copy, empty for a checkout or local install.
# @return 0 on success.
tool_installed_sha() {
  [[ -f $ROOT/VERSION ]] || return 0
  local v
  v="$(cat "$ROOT/VERSION")"
  [[ $v == local* ]] && return 0
  echo "${v##* }"
}

# latest_tool_sha: commit that last touched valhalla.sh on GitHub's main;
# empty when unreachable. Only commits affecting valhalla.sh are considered,
# so unrelated commits do not trigger an update.
# @brief Fetch the latest commit touching valhalla.sh on GitHub main.
# @return 0; prints the SHA or nothing when unreachable.
latest_tool_sha() {
  curl -fsSL -m 8 "https://api.github.com/repos/$REPO_SLUG/commits?path=scripts/vdt/valhalla.sh&per_page=1" 2>/dev/null |
    sed -n 's/.*"sha": *"\([0-9a-f]\{40\}\)".*/\1/p' | head -n1
}

# update_refresh: the background check. Writes UPDATE_STATE atomically.
# @brief Background check of GitHub and Docker Hub that writes results atomically to UPDATE_STATE under a lock.
# @return 0 on success.
update_refresh() {
  local lock="$UPDATE_STATE.lock" now tmp
  mkdir -p "$DATA_DIR"
  # One check at a time; a lock older than 2 minutes is stale.
  if ! mkdir "$lock" 2>/dev/null; then
    [[ -n $(find "$lock" -maxdepth 0 -mmin +2 2>/dev/null) ]] || return 0
    rmdir "$lock" 2>/dev/null; mkdir "$lock" 2>/dev/null || return 0
  fi
  now="$(date +%s)"
  tmp="$UPDATE_STATE.tmp.$$"
  {
    echo "checked_at=$now"
    if ! curl -fsS -m 4 -o /dev/null https://api.github.com 2>/dev/null; then
      echo "online=0"
    else
      echo "online=1"
      local latest_sha release installed ref repo tag running latest version
      latest_sha="$(latest_tool_sha)"
      [[ $latest_sha =~ ^[0-9a-f]{40}$ ]] || latest_sha=""
      release="$(curl -fsSL -m 8 "https://api.github.com/repos/$REPO_SLUG/releases/latest" 2>/dev/null |
        sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n1 || true)"
      # No GitHub releases: the highest version tag instead.
      if [[ -z $release ]]; then
        release="$(curl -fsSL -m 8 "https://api.github.com/repos/$REPO_SLUG/tags?per_page=100" 2>/dev/null |
          sed -n 's/.*"name": *"\(v\{0,1\}[0-9][0-9.]*\)".*/\1/p' | sort -V | tail -n1 || true)"
      fi
      installed="$(tool_installed_sha)"
      echo "release=$release"
      echo "tool_installed=$installed"
      echo "tool_latest=$latest_sha"
      if [[ -n $installed && -n $latest_sha && $installed != "$latest_sha" ]]; then
        echo "tool_update=1"
      else
        echo "tool_update=0"
      fi
      # The container's image, if it's this repo's published image.
      if [[ -n ${RT:-} ]] && exists; then
        ref="$(container_image)"
        repo="${ref%:*}"; tag="${ref##*:}"
        if [[ ${repo#docker.io/} == "${IMAGE_REPO#docker.io/}" ]]; then
          running="$(container_digest)"
          latest="$(registry_digest "$repo" latest 2>/dev/null || true)"
          version="$(server_version_line | sed -E 's/.*[Vv]ersion[^:]*:[[:space:]]*//; s/[[:space:]]+$//')"
          # Which version tag is "latest"? (it moves with every release,
          # pre-releases included, so ask Docker Hub rather than git tags)
          local latest_version=""
          if [[ -n $latest ]] && command -v python3 >/dev/null 2>&1; then
            latest_version="$(curl -fsSL -m 10 "https://hub.docker.com/v2/repositories/${repo#docker.io/}/tags?page_size=25" 2>/dev/null |
              DIGEST="$latest" python3 -c '
import json, os, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
for t in d.get("results", []):
    if t.get("name") != "latest" and t.get("digest") == os.environ["DIGEST"]:
        print(t["name"]); break' 2>/dev/null || true)"
          fi
          echo "image_latest_version=${latest_version:-$release}"
          echo "image_ref=$ref"
          echo "image_version=$version"
          echo "image_running=$running"
          echo "image_latest=$latest"
          if [[ -n $running && -n $latest && $running != "$latest" ]]; then
            echo "image_update=1"
          else
            echo "image_update=0"
          fi
          echo "image_tag=$tag"
        fi
      fi
    fi
  } >"$tmp" 2>/dev/null
  # Rename into place atomically so a concurrent reader never sees a half-written
  # state file.
  mv -f "$tmp" "$UPDATE_STATE"
  rmdir "$lock" 2>/dev/null || true
}

# schedule_update_check: start update_refresh in the background when the
# saved result is older than UPDATE_INTERVAL.
# @brief Start update_refresh in the background when the cached result is older than the interval.
# @return 0 on success.
schedule_update_check() {
  [[ ${VALHALLA_NO_UPDATE_CHECK:-0} == 1 ]] && return 0
  local checked now
  checked="$(state_get checked_at)"
  now="$(date +%s)"
  if [[ $checked =~ ^[0-9]+$ ]] && ((now - checked < UPDATE_INTERVAL)); then
    return 0
  fi
  (update_refresh >/dev/null 2>&1 </dev/null &)
  return 0
}

# show_update_notice: prints the update table (stderr, terminals only).
# @brief Print a styled update table to stderr on interactive terminals.
# @return 0 on success.
show_update_notice() {
  [[ ${VALHALLA_NO_UPDATE_CHECK:-0} == 1 ]] && return 0
  [[ -t 2 ]] || return 0
  local tool image
  tool="$(state_get tool_update)"; image="$(state_get image_update)"
  [[ $tool == 1 || $image == 1 ]] || return 0

  # Colors and icons come from the shared style engine (stderr judged).
  local R="$E_R" B="$E_B" D="$E_DIM" T="$E_ACC" H1="$E_HEAD" OLD="$E_ERR" NEW="$E_OK" OK="$E_INFO"

  # Icons: Nerd Font glyphs (1 column), emoji (2 columns) or none.
  local i_gift="" i_img="" i_tool="" iw=0
  if [[ -n $E_NERD ]]; then
    i_gift=$'\xef\x81\xab'; i_img=$'\xef\x8c\x88'; i_tool=$'\xef\x82\xad'; iw=2   # nf-fa-gift, nf-linux-docker, nf-fa-wrench
  elif [[ -n $E_i_rocket ]]; then
    i_gift="🎁"; i_img="🐳"; i_tool="🔧"; iw=3
  fi

  # Rows: name|installed|latest
  local -a rows=()
  local inst lat
  if [[ $image == 1 ]]; then
    inst="$(state_get image_version)"; [[ -n $inst ]] || inst="$(state_get image_running | cut -c8-19)"
    lat="$(state_get image_latest_version)"; [[ -n $lat ]] || lat="$(state_get image_latest | cut -c8-19)"
    rows+=("img|Docker image|${inst:-?}|${lat:-newer}")
  fi
  if [[ $tool == 1 ]]; then
    inst="$(state_get tool_installed | cut -c1-7)"; lat="$(state_get tool_latest | cut -c1-7)"
    rows+=("tool|vdt tool|${inst:-?}|${lat:-newer}")
  fi

  # Column widths (text only; icons are added with their own width).
  local w1=9 w2=9 w3=6 r name a b k
  for r in "${rows[@]}"; do
    IFS='|' read -r k name a b <<<"$r"
    ((${#name} + iw > w1)) && w1=$((${#name} + iw))
    ((${#a} > w2)) && w2=${#a}
    ((${#b} > w3)) && w3=${#b}
  done
  local tl tr bl br hz vt lj rj tj bj xj
  if [[ -n $E_i_step ]]; then # level >= 2: unicode allowed
    tl="╭"; tr="╮"; bl="╰"; br="╯"; hz="─"; vt="│"; lj="├"; rj="┤"; tj="┬"; bj="┴"; xj="┼"
  else
    tl="+"; tr="+"; bl="+"; br="+"; hz="-"; vt="|"; lj="+"; rj="+"; tj="+"; bj="+"; xj="+"
  fi
  # @brief Print a horizontal rule made of the given number of repeated characters.
  # @param n rule length.
  # @return 0 on success.
  rule() { local n=$1 out=""; while ((n-- > 0)); do out+="$hz"; done; printf '%s' "$out"; }
  # @brief Print text padded to a fixed width for a status table cell.
  # @param text cell contents.
  # @param width target width.
  # @param len visible length of text.
  # @return 0 on success.
  pad() { printf '%s%*s' "$1" $(($2 - $3)) ""; } # text, width, visible length

  local inner=$((w1 + w2 + w3 + 8)) title="Update available" icon_w=0
  [[ ${#rows[@]} -gt 1 ]] && title="Updates available"
  [[ -n $i_gift ]] && icon_w=$iw
  {
    echo
    printf '%s%s%s%s\n' "$T" "$tl" "$(rule $((inner)))" "$tr$R"
    printf '%s%s %s%s%s %s%s\n' "$T" "$vt" "${i_gift:+$i_gift }" "$B" "$(pad "$title" $((inner - 2 - icon_w)) ${#title})" "$T$vt" "$R"
    printf '%s%s%s%s%s%s%s%s%s\n' "$T" "$lj" "$(rule $((w1 + 2)))" "$tj" "$(rule $((w2 + 2)))" "$tj" "$(rule $((w3 + 2)))" "$rj" "$R"
    printf '%s%s%s %s %s%s %s %s%s %s %s%s%s\n' "$T" "$vt" "$R$H1" "$(pad Component "$w1" 9)" "$T$vt" "$R$H1" "$(pad Installed "$w2" 9)" "$T$vt" "$R$H1" "$(pad Latest "$w3" 6)" "$T" "$vt" "$R"
    printf '%s%s%s%s%s%s%s%s%s\n' "$T" "$lj" "$(rule $((w1 + 2)))" "$xj" "$(rule $((w2 + 2)))" "$xj" "$(rule $((w3 + 2)))" "$rj" "$R"
    for r in "${rows[@]}"; do
      IFS='|' read -r k name a b <<<"$r"
      local ic=""; [[ $k == img ]] && ic="$i_img"; [[ $k == tool ]] && ic="$i_tool"
      local cell="$name" vis=${#name}
      if [[ -n $ic ]]; then cell="$ic $name"; vis=$((${#name} + iw)); fi
      printf '%s%s%s %s %s%s %s%s %s%s %s%s %s%s\n' "$T" "$vt" "$R" "$(pad "$cell" "$w1" "$vis")" "$T$vt" "$R$OLD" "$(pad "$a" "$w2" ${#a})" "$R" "$T$vt" "$R$NEW" "$(pad "$b" "$w3" ${#b})" "$R" "$T$vt" "$R"
    done
    printf '%s%s%s%s%s%s%s%s%s\n' "$T" "$bl" "$(rule $((w1 + 2)))" "$bj" "$(rule $((w2 + 2)))" "$bj" "$(rule $((w3 + 2)))" "$br" "$R"
    local how=""
    [[ $image == 1 || $tool == 1 ]] && how="$PROG update"
    [[ $image != 1 && $tool == 1 ]] && how="$how (or $PROG install for just the tool)"
    printf '  %sUpdate with:%s %s%s%s   %s(hide: VALHALLA_NO_UPDATE_CHECK=1)%s\n\n' "$D" "$R" "$OK" "$how" "$R" "$D" "$R"
  } >&2
  return 0
}

# status_updates: `status` checks for updates now (only with internet) and
# reports the result; offline it shows what an earlier check found.
# @brief Check for updates immediately for the status command and report availability or offline state.
# @return 0 on success.
status_updates() {
  [[ ${VALHALLA_NO_UPDATE_CHECK:-0} == 1 ]] && return 0
  if ! curl -fsS -m 1 -o /dev/null https://api.github.com 2>/dev/null; then
    printf '%s%s%-10s%s %s\n' "$S_LBL" "${S_i_restart:+$S_i_restart$S_GAP}" "Updates:" "$S_R" "not checked (no internet connection)"
    show_update_notice
    return 0
  fi
  # Let a running background check finish rather than racing it.
  local waited=0
  while [[ -d $UPDATE_STATE.lock ]] && ((waited < 20)); do sleep 1; waited=$((waited + 1)); done
  update_refresh
  local tool image
  tool="$(state_get tool_update)"; image="$(state_get image_update)"
  if [[ $tool == 1 || $image == 1 ]]; then
    printf '%s%s%-10s%s %s\n' "$S_LBL" "${S_i_restart:+$S_i_restart$S_GAP}" "Updates:" "$S_R" "${S_OK}${S_i_ok:+$S_i_ok$S_GAP}available${S_R}"
    if [[ -t 2 ]]; then
      show_update_notice
    else
      [[ $image == 1 ]] && echo "  Docker image $(state_get image_version) -> $(state_get image_latest_version) ($PROG update)"
      [[ $tool == 1 ]] && echo "  vdt tool $(state_get tool_installed | cut -c1-7) -> $(state_get tool_latest | cut -c1-7) ($PROG update, or install for just the tool)"
    fi
  else
    local what=""
    [[ -n $(state_get image_version) ]] && what="image $(state_get image_version)"
    if [[ -n $(state_get tool_installed) ]]; then
      what="${what:+$what, }tool $(state_get tool_installed | cut -c1-7)"
    elif [[ ! -f $ROOT/VERSION ]]; then
      what="${what:+$what; }tool not checked: running from a checkout"
    fi
    printf '%s%s%-10s%s %s\n' "$S_LBL" "${S_i_restart:+$S_i_restart$S_GAP}" "Updates:" "$S_R" "${S_OK}${S_i_ok:+$S_i_ok$S_GAP}up to date${what:+ ${S_DIM}($what)${S_R}}"
  fi
  return 0
}

# ── commands ───────────────────────────────────────────────────────────────
# @brief Implement run: start an existing container or pull and create a new one, then wait for health.
# @param 1 optional image tag.
# @return 0 on success.
cmd_run() {
  local image  # Fully qualified image reference (repository:tag).
  image="$(image_ref "${1:-}")"  # Build the repository:tag value from the optional tag argument.
  if exists; then  # A container with this name already exists; run never replaces it.
    # run never replaces a container; a different tag is ignored with a pointer to
    # update, which is the command that recreates.
    if [[ -n ${1:-} && "$(container_image)" != "$image" ]]; then  # Warn when the requested tag differs from the existing container's image.
      info "$NAME already exists from $(container_image); $1 is ignored (use: $PROG update $1)"  # Point the user to the update command for replacing the container.
    fi
    if is_running; then  # Already up: just verify health and print the banner.
      wait_healthy  # Confirm the running server actually answers before declaring success.
      run_success  # Print the URLs banner.
      return  # Nothing needed to start.
    fi
    if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the start without doing it.
      say_info "Dry run ($RT): would start the existing container $NAME."  # Report the planned action.
      return  # Stop before making any real change.
    fi
    step "Starting existing container $NAME"  # Tell the user what is happening.
    start_container  # Start it and invalidate the cached inspect data.
    wait_healthy  # Wait until the API answers.
    persist_on && persist_load  # With persistence on, restore the saved database.
    run_success  # Print the URLs.
    return 0  # Done.
  else
    if [[ $DRY_RUN == 1 ]]; then
      local action="use local image $image"  # Default plan: the image is already available locally.
      image_present "$image" || action="pull $image"  # If the image is missing, the plan is to pull it first.
      plan_recreate "$image" "$action"  # Print the full dry-run plan.
      return  # No changes made.
    fi
    # Pull only when the image is absent; an already-present local image is used
    # as-is (offline-friendly).
    image_present "$image" || pull "$image"  # Pull only when missing, so an already-local image works offline.
    step "Creating container $NAME from $image"  # Announce the container creation.
    create_container "$image"  # Create and start the container with env, mount and limits.
  fi
  adopt_container_port  # Use the container's real port unless one was given explicitly.
  wait_healthy  # Wait for the API.
  persist_on && persist_load  # Restore saved data when persistence is on.
  run_success  # Print the URLs.
  return 0  # Done.
}

# recreate <image>: replaces the container with one from <image>, keeping its
# TOKEN_SECRET and, with persistence, its data.
# recreate <image>: replaces the container with one from <image>, keeping its
# TOKEN_SECRET and, with persistence, its data.
# @brief Replace the container from a new image, preserving its token secret and, with persistence, its data.
# @param image image reference for the new container.
# @return 0 on success.
recreate() {
  local image="$1" secret  # New image reference and the old container's token secret.
  secret="$(old_secret)"  # Remember the secret so existing login sessions survive the recreate.
  OLD_PERSIST="$(container_env VALHALLA_PERSIST)"  # Record the old container's persistence setting.
  if exists && save_before_replace; then  # Save the data first when the old container used persistence.
    persist_save  # Dump the database (dies before stopping if the dump fails).
  elif exists && is_running && [[ $PERSIST == 1 && $OLD_PERSIST != 1 ]]; then  # Turning persistence on: warn that the current data is not saved.
    warn "Turning persistence on: the new container gets the saved database $(persist_file);
    the current one isn't saved (run '$PROG dump' first to keep it)"
  fi
  # Stop gracefully first: an entrypoint with persistence saves on stop,
  # which a forced removal would skip.
  if exists && is_running; then stop_container; fi  # Stop gracefully so the entrypoint can save persistence.
  step "Recreating container $NAME from $image"  # Announce the replacement.
  remove_container  # Force-remove the old container.
  create_container "$image" "$secret"  # Create the new container, reusing the old token secret.
  adopt_container_port  # Follow the container's published port.
  wait_healthy  # Wait for the API.
  if persist_on; then  # With persistence, warn about image changes and restore the data.
    warn_dump_image "$(container_digest)"  # Warn if the dump was made by a different image.
    persist_load  # Restore the saved database.
  fi
  # Clear the cached notice so the next command does not announce the update
  # that was just applied.
  forget_update_state  # Clear the notice so we do not announce the update we just applied.
  prune_images  # Remove replaced and unused images to free disk space.
  run_success  # Print the URLs.
  return 0  # Done.
}

# update_tool_step: the tool half of update. Runs after the image half (the
# installer replaces the running script, so it goes last — safe because the
# installer downloads first and only then removes the current copy and
# installs, so this script keeps reading its old, open file).
# Skipped for a checkout or a locally-installed copy (tool_installed_sha is
# empty: nothing to compare against), when the tool is already at main's
# commit, and when GitHub can't be reached. Never fails the update: the image
# half has already succeeded when this runs.
# @brief Update the installed vdt tool after the image half of update; never fails the overall update.
# @return 0 on success.
update_tool_step() {
  local installed latest  # Installed commit SHA and the latest commit SHA.
  installed="$(tool_installed_sha)"  # Empty for a checkout or a locally installed copy.
  if [[ -z $installed && ! -f $ROOT/VERSION ]]; then  # A checkout has no installed copy to update.
    info "Tool: not an installed copy (checkout) — nothing to update. Run '$PROG install' to install the command."  # Explain how to install the command if wanted.
    return 0
  fi
  if [[ -z $installed ]]; then  # Locally installed copy: refresh it by running the installer.
    step "Updating the vdt tool, like '$PROG install'"  # Announce the tool update.
    if ! install_tool main; then  # If the tool update fails, only warn: the image half already succeeded.
      warn "the image was updated, but the tool update failed; run '$PROG install'"  # Tell the user how to retry the tool update.
    fi
    return 0
  fi
  latest="$(latest_tool_sha)"  # Ask GitHub for the newest commit that touched this script.
  [[ $latest =~ ^[0-9a-f]{40}$ ]] || latest=""  # Ignore malformed replies from GitHub.
  if [[ -z $latest ]]; then  # Offline or GitHub error: warn but keep the successful image update.
    warn "couldn't reach GitHub to check for a newer vdt tool; the image was updated"  # Explain that only the tool check was skipped.
    return 0
  fi
  if [[ $installed == "$latest" ]]; then  # Already at the newest commit.
    ok "Tool: already up to date ($(echo "$latest" | cut -c1-7))."  # Report that no change is needed.
    return 0
  fi
  step "Updating the vdt tool ($(echo "$installed" | cut -c1-7) -> $(echo "$latest" | cut -c1-7)), like '$PROG install'"  # Announce the short SHA change.
  if ! install_tool main; then  # Again, a tool failure only warns.
    warn "the image was updated, but the tool update failed; run '$PROG install'"
  fi
}

# @brief Implement update: pull the image, recreate the container and optionally update the tool.
# @param 1 optional image tag.
# @return 0 on success.
cmd_update() {
  local image  # Fully qualified image reference.
  image="$(image_ref "${1:-}")"  # Build repository:tag from the optional tag.
  if [[ $UPDATE_SCOPE == tool ]]; then  # --tool-only: skip all image and container work.
    if [[ $DRY_RUN == 1 ]]; then  # Dry run: just describe the tool update.
      if [[ -z $(tool_installed_sha) ]]; then  # Checkout case: there is no installed copy to update.
        say_info "Dry run, nothing changed. This is a checkout, so there is no installed copy to update (use: $PROG install)."  # Explain there is nothing installed to update.
      else
        say_info "Dry run, nothing changed. Would update the vdt tool in place (like '$PROG install')."  # Describe the in-place tool update.
      fi
      return 0  # No real changes.
    fi
    update_tool_step  # Perform the tool update.
    return  # Done with the tool-only path.
  fi
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: show the full plan including pruning.
    PLAN_PRUNE=1 plan_recreate "$image" "pull $image"  # Include the image pull and prune steps in the plan.
    if [[ $UPDATE_SCOPE != image ]]; then  # Unless --image-only, mention the tool update too.
      say_info "then also update the vdt tool if GitHub has a newer one (skip with: --image-only)"  # Explain the tool step.
    fi
    return  # No real changes.
  fi
  pull "$image"  # Fetch the new image from the registry.
  recreate "$image"  # Replace the container while preserving data and secret.
  [[ $UPDATE_SCOPE == image ]] || update_tool_step  # Update the tool as well unless --image-only was given.
}

# @brief Implement build: build an image from this checkout and recreate the container from it.
# @param 1 optional image tag, default local.
# @return 0 on success.
cmd_build() {
  local tag="${1:-local}" version deps image  # Tag defaults to local; also hold the git version, deps image and final image ref.
  image="${LOCAL_REPO}:${tag}"  # Reference of the locally built image.
  version="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo dev)"  # Version stamp from git, or dev when outside a repository.
  deps="${VALHALLA_DEPS_IMAGE:-${IMAGE_REPO%-run}-deps:$(host_arch)}"  # Prebuilt deps image for this CPU architecture, or local to build from source.

  local -a args=(build -f "$ROOT/docker/Dockerfile" --build-arg "VALHALLA_VERSION=$version" -t "$image")  # Build command with the Dockerfile and version build-argument.
  [[ $deps != local ]] && args+=(--build-arg "DEPS_IMAGE=$deps")  # Only pass DEPS_IMAGE when using a prebuilt deps image.
  args+=("$ROOT")  # The build context is the repository root.

  if [[ $DRY_RUN == 1 ]]; then  # Dry run: print the build command instead of running it.
    say_info "Dry run ($RT), nothing changed. Would build:"  # Announce the plan.
    echo "  $RT ${args[*]}"  # Show the exact build command.
    [[ $deps == local ]] && say_info "(deps built from source: slow)"  # Warn that building the dependencies from source is slow.
    PLAN_PRUNE=1 plan_recreate "$image" "then run the new image"  # Include the recreate and prune steps in the plan.
    return  # No real changes.
  fi

  ensure_builder  # Give Apple's builder enough memory before compiling.
  step "Building $image (version $version, deps: $deps)"  # Announce the build.
  rt "${args[@]}"  # Run the real build through docker or container.
  recreate "$image"  # Replace the container with the newly built image.
}

# @brief Implement stop: optionally save the database, then stop without removing the container.
# @return 0 on success.
cmd_stop() {
  require_exists  # Fail early with a hint if there is no container.
  if ! is_running; then  # Nothing to stop.
    say_info "$NAME is not running."  # Report that it is already stopped.
    return 0  # Done.
  fi
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the save and stop.
    say_info "Dry run ($RT): would $(persist_on && echo "save the database to $(persist_file) and ")stop $NAME."  # Mention the database save when persistence is on.
    return 0
  fi
  persist_on && persist_save  # Save the database before stopping when persistence is on.
  stop_container  # Stop gracefully, giving the entrypoint time to save.
  say_ok "Stopped $NAME (start it again with: $PROG run)"  # Confirm and show how to start it again.
}

# @brief Implement restart: save and restore data when persisting, then restart and wait for health.
# @return 0 on success.
cmd_restart() {
  require_exists  # Need an existing container.
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the restart.
    if persist_on; then  # Describe the save and restore cycle when persisting.
      say_info "Dry run ($RT): would save the database to $(persist_file), restart $NAME and restore it."
    else
      say_info "Dry run ($RT): would restart $NAME (the database starts fresh)."
    fi
    return 0
  fi
  if persist_on; then  # Real path: save data first when persisting.
    step "Restarting $NAME (persistence on: the database is saved and restored)"  # Announce the restart with the persistence note.
    persist_save  # Dump the database before restarting.
  else
    step "Restarting $NAME (the database is reset)"  # Without persistence, warn that the database is reset.
  fi
  is_running && stop_container  # Stop only if it is currently running.
  start_container  # Start it again.
  wait_healthy  # Wait for the API.
  persist_on && persist_load  # Restore the saved database.
  run_success  # Print the URLs.
  return 0  # Done.
}

# @brief Implement delete: confirm, save data when persisting, and remove the container.
# @return 0 on success.
cmd_delete() {
  if ! exists; then  # Nothing to delete.
    say_info "Container $NAME doesn't exist."  # Report and exit cleanly.
    return 0
  fi
  # The database survives only when persistence is on AND the container is
  # running; otherwise removal is final, so the prompt says so explicitly.
  local keep=0  # Flag: 1 means the database can be saved before removal.
  persist_on && is_running && keep=1  # Only a running, persisting container has saveable data.
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the removal.
    say_info "Dry run ($RT): would $([[ $keep == 1 ]] && echo "save the database to $(persist_file) and ")remove $NAME."  # Mention the database save when applicable.
    return 0
  fi
  if [[ $keep == 1 ]]; then  # Data will survive, so say so in the prompt.
    confirm "Remove container $NAME? (its database is saved to $(persist_file) first)" || die "cancelled"  # Ask before removing and abort if declined.
    persist_save  # Save the database first.
    stop_container  # Stop gracefully.
    info "Kept $(persist_file); $PROG run --persist restores it"  # Tell the user how to restore the saved data.
  else
    confirm "Remove container $NAME? Its database is lost (keep it with: $PROG dump)" || die "cancelled"  # Warn that data is lost when not persisting.
  fi
  remove_container  # Actually remove the container.
  say_ok "Removed container $NAME"  # Confirm the removal.
}

# @brief Implement clean: confirm, delete the container and remove all local images of this repo.
# @return 0 on success, 1 if an image could not be removed.
cmd_clean() {
  local refs pattern ref failed=0  # Image references to remove, name pattern, loop variable and a failure flag.
  pattern="^((docker\.io/)?${IMAGE_REPO#docker.io/}|${LOCAL_REPO}):"  # Regular expression matching only this tool's run images.
  if [[ $RT == docker ]]; then  # Docker lists images in one format.
    refs="$(docker image ls --format '{{.Repository}}:{{.Tag}}' | grep -E "$pattern" || true)"  # Collect matching repository:tag values.
  else
    refs="$(container image list 2>/dev/null | awk 'NR>1 {print $1":"$2}' | grep -E "$pattern" || true)"  # Apple container prints a table; take the first two columns.
  fi
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: list what would be removed.
    say_info "Dry run ($RT): would remove container $NAME (if it exists) and these images:"  # Announce the plan.
    if [[ -n $refs ]]; then while IFS= read -r ref; do echo "  $ref"; done <<<"$refs"; else say_info "(none)"; fi  # Show the images, or say none.
    return 0  # No changes.
  fi
  confirm "Remove container $NAME and all local images of $IMAGE_REPO?" || die "cancelled"  # Ask once before deleting the container and images.
  # Reuse delete's save/stop/remove logic; confirmation already happened above,
  # so suppress its second prompt.
  ASSUME_YES=1 cmd_delete  # Reuse delete's save/stop/remove logic without a second prompt.
  if [[ -z $refs ]]; then  # No matching images.
    say_info "No local images of $IMAGE_REPO."  # Report that.
    return 0  # Done; the container was already deleted.
  fi
  # One at a time: an image still used by another container is reported,
  # not fatal.
  while IFS= read -r ref; do  # Remove the images one at a time.
    if rt image rm "$ref" >/dev/null 2>&1 || rt image delete "$ref" >/dev/null 2>&1; then  # Try both runtime command spellings for deleting an image.
      say_ok "Removed image $ref"  # Report success.
    else
      printf '%s\n' "${S_WRN}${S_i_warn:+$S_i_warn$S_GAP}${S_R}Couldn't remove image $ref (still in use?)"  # An image still in use is a warning, not a fatal error.
      failed=1  # Remember that at least one image could not be removed.
    fi
  done <<<"$refs"  # Feed the collected image list into the loop.
  return "$failed"  # Nonzero if any image remained.
}

# @brief Implement status: print runtime, state, image, URLs, persistence, health and version.
# @return 0 on success.
cmd_status() {
  # @brief Print one labeled status row with its contextual icon and alignment.
  # @param 1 row label.
  # @param 2 row value.
  # @param 3 optional icon override.
  # @param 4 optional icon-gap override.
  # @return 0 on success.
  row() {
    local icon gap="${4:-$S_GAP}" override="${3:-}"  # Icon and gap for this row; arguments 3 and 4 allow overrides.
    if [[ -n $override ]]; then icon="$override"; else  # Use a caller-provided icon when one was given.
      case "$1" in  # Otherwise pick an icon based on the row label.
        Runtime:) icon="$S_i_runtime" ;;
        State:) icon="$S_i_wait" ;;
        Updates:) icon="$S_i_restart" ;;
        Server:) icon="$S_i_server" ;;
        Image:) icon="$S_i_box" ;;        URL:) icon="$S_i_link" ;;
        Docs:) icon="$S_i_book" ;;        Persist:) icon="$S_i_save" ;;
        Health:) icon="$S_i_ok" ;;
        Version:) icon="$S_i_version"; gap="$S_VERSION_GAP" ;;
      esac
    fi
    printf '%s%s%-10s%s %s\n' "$S_LBL" "${icon:+$icon$gap}" "$1" "$S_R" "$2"  # Print the aligned label and value.
  }
  row "Runtime:" "$RT"  # Which runtime is active.
  row "Server:" "$NAME" "" "$S_GAP "  # Container name; override the gap for a wider icon.
  if ! exists; then  # No container: show a short status.
    row "State:" "not created ${S_DIM}(use: $PROG run)${S_R}" "$S_i_q"  # Explain how to create the container.
    status_updates  # Also check for updates.
    return 0  # Done.
  fi
  local st  # Current container state.
  st="$(state)"  # Ask the runtime for the state string.
  if [[ $st == running ]]; then  # Show a green running state when it is up.
    row "State:" "${S_OK}${S_i_ok:+$S_i_ok$S_GAP}running${S_R}" "$S_i_ok"  # Mark the running state with the ok icon.
  else
    row "State:" "${st:-unknown}" "$S_i_stop"  # Any other state uses the stop icon.
  fi
  row "Image:" "${S_DIM}$(container_image)${S_R}"  # Image the container runs.
  row "URL:" "${S_VAL}http://127.0.0.1:${PORT}${S_R}"  # Where to reach the server on the host.
  row "Docs:" "${S_INFO}$DOC_URL${S_R}"  # API documentation URL.
  local pf  # Path of the persistence dump.
  pf="$(persist_file)"  # Compute the dump path from the container name.
  if persist_on; then  # Report the persistence state and last save time.
    if [[ -f $pf ]]; then  # A saved file exists.
      row "Persist:" "on, last saved $(date -r "$pf" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?') ${S_DIM}($pf)${S_R}"  # Show when the file was last modified.
    else
      row "Persist:" "on, nothing saved yet ${S_DIM}($pf)${S_R}"  # Persistence is on but no dump exists yet.
    fi
  else
    row "Persist:" "off ${S_DIM}(the database is reset on every start; enable: $PROG update --persist)${S_R}"  # Explain how to enable persistence.
  fi
  if [[ $st == running ]]; then  # Health and version only make sense when running.
    if curl -fsS "$HEALTH_URL" >/dev/null 2>&1; then  # Probe the health endpoint.
      row "Health:" "${S_OK}${S_i_ok:+$S_i_ok$S_GAP}answering at ${S_DIM}$HEALTH_URL${S_R}"  # The server responded.
    else
      row "Health:" "${S_ERR}${S_i_err:+$S_i_err$S_GAP}not answering at ${S_DIM}$HEALTH_URL${S_R}"  # The server did not respond.
    fi
    local ver  # Server version parsed from the logs.
    ver="$(server_version_line | sed -E 's/.*[Vv]ersion[^:]*:[[:space:]]*//')"  # Extract just the version text.
    row "Version:" "${ver:-unknown}"  # Show the version, or unknown.
  fi
  status_updates  # Check for updates at the end.
}

# @brief Implement outdated: compare the running image digest with the tag on Docker Hub.
# @param 1 optional tag, defaulting to the running image's tag.
# @return 0 on success.
cmd_outdated() {
  require_exists  # Need a container to inspect.
  local ref repo tag local_digest remote_digest  # Image reference, repository, tag, local digest and Docker Hub digest.
  ref="$(container_image)"  # The image the container currently runs.
  repo="${ref%:*}"  # Strip off the tag to get the repository.
  tag="${1:-${ref##*:}}"  # Use the requested tag or, by default, the running one.
  [[ $repo == "$LOCAL_REPO" || $repo == "docker.io/library/$LOCAL_REPO" ]] &&  # Locally built images have no Docker Hub counterpart.
    die "$NAME runs a locally built image ($ref); nothing to compare on Docker Hub"  # Explain why comparison is impossible.
  [[ $repo == docker.io/* || $repo != *.*/* ]] || die "only Docker Hub images can be checked ($ref)"  # Only Docker Hub images are supported by this check.
  local_digest="$(container_digest)"  # Digest of the running image.
  remote_digest="$(registry_digest "$repo" "$tag")" || die "couldn't reach Docker Hub"  # Ask Docker Hub for the tag's current digest.
  [[ -n $remote_digest ]] || die "tag $tag not found for $repo"  # An empty result means the tag does not exist.
  printf '%s%-10s%s %s\n' "$S_LBL" "Server:" "$S_R" "$ref"  # Show which image is running.
  echo "  ${S_DIM}running${S_R}    ${local_digest:-unknown}"  # Show the local digest.
  echo "  ${S_DIM}Docker Hub${S_R} ${S_VAL}$tag${S_R}: $remote_digest"  # Show the remote digest.
  if [[ $local_digest == "$remote_digest" ]]; then  # Equal digests mean the image is up to date.
    say_ok "Up to date."  # Report that no update is needed.
  else
    printf '%s\n' "${S_ACC}${S_i_rocket:+$S_i_rocket$S_GAP}${S_B}Update available${S_R}: $PROG update $tag"  # Suggest the update command.
  fi
}

# @brief Implement show: follow the container's logs without returning to this script.
# @return Does not return; exec.
cmd_show() {
  require_exists  # Need a container.
  forget_inspect # exec skips the EXIT trap
  if [[ $RT == docker ]]; then exec docker logs -f "$NAME"; else exec container logs -f "$NAME"; fi  # Follow logs, replacing this shell; Ctrl+C stops following, not the container.
}

# @brief Implement logs: print the last N lines of the container log.
# @param 1 line count, default 100.
# @return 0 on success.
cmd_logs() {
  require_exists  # Need a container.
  local n="${1:-100}"  # Number of lines to print, default 100.
  [[ $n =~ ^[0-9]+$ ]] || die "logs takes a number of lines, got '$n'"  # Reject non-numeric line counts.
  if [[ $RT == docker ]]; then docker logs --tail "$n" "$NAME" 2>&1; else container logs -n "$n" "$NAME" 2>&1; fi  # Use each runtime's own flag for printing the last N lines.
}

# @brief Implement stats: show live CPU and memory usage without returning to this script.
# @return Does not return; exec.
cmd_stats() {
  require_running  # Stats only work on a running container.
  forget_inspect # exec skips the EXIT trap
  if [[ $RT == docker ]]; then exec docker stats "$NAME"; else exec container stats "$NAME"; fi  # Show live CPU and memory, replacing this shell.
}

# @brief Implement env: print the composed container environment with secrets hidden.
# @return 0 on success.
cmd_env() {
  compose_env "$(old_secret)"  # Build the same environment a new container would get, without saving anything.
  print_env  # Print it sorted, with secrets hidden.
  info "TOKEN_SECRET from: $TOKEN_SOURCE"  # Show which source supplies the token secret.
}

# @brief Implement doc: verify the server is answering and open the API docs in a browser.
# @return 0 on success.
cmd_doc() {
  require_running  # Docs need a running server.
  curl -fsS "$HEALTH_URL" >/dev/null 2>&1 ||  # Verify the server answers before opening a browser.
    die "$NAME is running but the server isn't answering at $HEALTH_URL (see: $PROG logs)"  # Tell the user where to look for problems.
  say_info "Opening $DOC_URL"  # Announce the action.
  # macOS: open. Elsewhere xdg-open first: on Debian/Ubuntu `open` can be
  # openvt (from kbd), which would try to start a virtual console.
  if [[ $(uname -s) == Darwin ]] && command -v open >/dev/null 2>&1; then  # On macOS, prefer the open command.
    open "$DOC_URL"  # Open the docs in the default browser.
  elif command -v xdg-open >/dev/null 2>&1; then  # On Linux, use xdg-open.
    xdg-open "$DOC_URL" >/dev/null 2>&1  # Open in the browser, silencing any noise.
  elif [[ $(uname -s) != Darwin ]] && command -v wslview >/dev/null 2>&1; then  # WSL fallback opener.
    wslview "$DOC_URL"  # Open via WSL.
  else
    die "no browser opener (open, xdg-open or wslview) found; browse to $DOC_URL"  # Last resort: print the URL for the user to open manually.
  fi
}

# @brief Implement psql: open an interactive psql session on the bundled database.
# @return Does not return; exec.
cmd_psql() {
  require_running  # Need a running container.
  [[ $DRY_RUN == 1 ]] && { say_info "Dry run ($RT): would open psql in $NAME."; return 0; }  # Dry run: describe the action without opening psql.
  local user db port  # Database connection settings read from the container.
  read -r user db port <<<"$(db_settings)"  # Split the user, database and port into three variables.
  exec_it su-exec postgres psql -p "$port" -U "$user" -d "$db"  # Open psql as the postgres user, replacing this shell.
}

# @brief Implement dump: pg_dump the database to a timestamped or explicitly named file.
# @param 1 optional destination file.
# @return 0 on success.
cmd_dump() {
  require_running  # Need a running database.
  # Timestamp the default filename so repeated dumps never overwrite each other.
  local file="${1:-$DUMP_DIR/${NAME}-$(date +%Y%m%d-%H%M%S).sql}"  # Explicit destination, or a timestamped default so dumps never overwrite each other.
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the dump without running it.
    say_info "Dry run ($RT): would dump the database of $NAME to $file."
    return 0
  fi
  step "Dumping the database from $NAME"  # Announce the dump.
  dump_to "$file" || die "pg_dump failed"  # Fail clearly if pg_dump fails.
  say_ok "Saved $(wc -c <"$file" | tr -d ' ') bytes to $file"  # Report the file size and path.
}

# @brief Implement restore: confirm and load a SQL dump, then restart the server process.
# @param 1 required dump file path.
# @return 0 on success.
cmd_restore() {
  local file="${1:-}"  # The dump file to load.
  [[ -n $file ]] || die "usage: $PROG restore <file>"  # A file argument is required.
  [[ -f $file ]] || die "file not found: $file"  # The file must exist locally.
  require_running  # Need a running container.
  local user db port extra=""  # Database settings and an optional extra warning message.
  read -r user db port <<<"$(db_settings)"  # Split the database settings into three variables.
  if dump_image_differs "$file"; then  # Warn if the dump came from a different image.
    extra=" It was made by a different image; if the schema changed, the old one comes back."  # Extra warning shown in the prompt and the dry-run message.
  fi
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the restore without loading it.
    say_info "Dry run ($RT): would load $file into database $db in $NAME, replacing its contents.$extra"  # Show the plan including any warning.
    return
  fi
  # Restoring overwrites the live database, so require explicit consent unless
  # --yes; a dry run returns before this point.
  confirm "Replace the contents of database $db in $NAME with $file?$extra" || die "cancelled"  # Restoring overwrites data, so require explicit consent.
  step "Restoring $file into $db"  # Announce the restore.
  restore_from "$file" || die "restore failed"  # Load the dump and restart the server process.
  say_ok "Restored $file"  # Confirm the restore.
}

# @brief Implement shell: open an interactive shell inside the container.
# @return Does not return; exec.
cmd_shell() {
  require_running  # Need a running container.
  [[ $DRY_RUN == 1 ]] && { say_info "Dry run ($RT): would open a shell in $NAME."; return 0; }  # Dry run: describe the action without opening a shell.
  exec_it sh  # Open a shell inside the container, replacing this shell.
}

# @brief Implement test: run test_api.sh and/or schemathesis_test.sh against the running server.
# @param 1 test suite: api, fuzz or all; default api.
# @return The last test script's exit status.
cmd_test() {
  local kind="${1:-api}" base status=0  # Test suite (api, fuzz or all), base URL and accumulated exit status.
  case "$kind" in api | fuzz | all) ;; *) die "test takes api, fuzz or all, got '$kind'" ;; esac  # Reject unknown test kinds early.
  base="http://127.0.0.1:${PORT}/api/v2"  # Base URL passed to the test scripts.
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the tests without running them.
    say_info "Dry run: would run the $kind tests against $base (they create and delete their own data)."  # Show the plan.
    return 0
  fi
  # Fail fast with an actionable message instead of letting test scripts hang
  # or produce confusing connection errors.
  curl -fsS "$HEALTH_URL" >/dev/null 2>&1 || die "the server isn't answering at $HEALTH_URL; use: $PROG run"  # Fail fast with a helpful message if the server is down.
  if [[ $kind == api || $kind == all ]]; then  # Run the API test suite when requested.
    step "Running test_api.sh against $base"  # Announce the API tests.
    BASE_URL="$base" "$(script_path test_api)" || status=$?  # Run with BASE_URL set and remember any failure.
  fi
  if [[ $kind == fuzz || $kind == all ]]; then  # Run the fuzz test suite when requested.
    step "Running schemathesis_test.sh against $base"  # Announce the fuzz tests.
    BASE_URL="$base" "$(script_path schemathesis_test)" || status=$?  # Run with BASE_URL set and remember any failure.
  fi
  return "$status"  # Nonzero if either suite failed.
}

# ── payload browsing ───────────────────────────────────────────────────────
# The request bodies test_api.sh sends live in api_tests/payloads
# (<group>/<name>.json); the installed tool ships them too. `payload`
# browses them with fzf, lists one group, or prints one (--raw: as-is).

# payload_dir: where the payload files are (checkout first, installed copy
# second); fails when neither has them.
# @brief Find the directory holding payload JSON files, trying the checkout before the installed copy.
# @return 0 with the path on stdout when found, 1 otherwise.
payload_dir() {
  if [[ -d $ROOT/tests/api_tests/payloads ]]; then
    echo "$ROOT/tests/api_tests/payloads"
    return 0
  elif [[ -d $ROOT/api_tests/payloads ]]; then
    echo "$ROOT/api_tests/payloads"
    return 0
  fi
  if [[ -d $DATA_DIR/api_tests/payloads ]]; then
    echo "$DATA_DIR/api_tests/payloads"
    return 0
  fi
  return 1
}

# @brief Print sorted payload group names under a directory.
# @param dir payload directory.
# @return 0 on success.
payload_groups() { # dir: group names, sorted, one per line
  local d
  for d in "$1"/*/; do [[ -d $d ]] && basename "$d"; done | sort
}

# @brief Print the number of payload JSON files in one group.
# @param dir payload directory.
# @param group group name.
# @return 0 on success.
payload_count() { # dir group: number of payloads in it
  local f n=0
  for f in "$1/$2"/*.json; do [[ -f $f ]] && n=$((n + 1)); done
  echo "$n"
}

# payload_hint NAME: what a placeholder like {{CLINIC_ID}} stands for.
# @brief Print a human-readable explanation of a placeholder name.
# @param 1 placeholder name.
# @return 0 on success.
payload_hint() {
  case "$1" in
    ID) echo "numeric id of the record to read" ;;
    *_ID) echo "numeric id returned by an earlier call" ;;
    *USERNAME) echo "unique username for this test run" ;;
    *PASSWORD) echo "the matching password of the test run" ;;
    RUN) echo "unique id of each test run (usernames, emails)" ;;
    DISPOSABLE_NAME) echo "name of a throwaway record used by a test" ;;
    *) echo "substituted by test_api.sh at run time" ;;
  esac
}

# payload_value NAME run: the sample value --filled substitutes for a
# placeholder, following test_api.sh's conventions (numbers are bare, where
# the template expects them so).
# @brief Print a sample value for a placeholder following test_api.sh conventions.
# @param name placeholder name.
# @param run per-run identifier used in generated values.
# @return 0 on success.
payload_value() {
  local name="$1" run="$2"
  case "$name" in
    ID | *_ID) echo 1 ;;
    RUN) echo "$run" ;;
    PROVIDER_USERNAME) echo "provider$run" ;;
    USER_USERNAME | LOGIN_USERNAME) echo "user$run" ;;
    *USERNAME) echo "provider$run" ;;
    PROVIDER_PASSWORD) echo '1234@Abc!' ;;
    USER_PASSWORD | LOGIN_PASSWORD) echo '123Mm@p0' ;;
    *PASSWORD) echo '123Mm@p0' ;;
    *TOKEN) echo "sample.$run.token" ;;
    DISPOSABLE_NAME) echo "disposable$run" ;;
    *) echo "sample-$name" ;;
  esac
}

# payload_fill FILE: every {{NAME}} replaced by its sample value (payload_value).
# @brief Replace every {{NAME}} placeholder in a payload with its sample value.
# @param file payload file path.
# @return 0; prints the filled JSON.
payload_fill() {
  local file="$1" content name value run
  run="$(date +%s | tail -c 7)"
  content="$(<"$file")"
  for name in $(printf '%s' "$content" | grep -o '{{[A-Z_]*}}' | sort -u | tr -d '{}'); do
    value="$(payload_value "$name" "$run")"
    content="${content//"{{$name}}"/$value}"
  done
  printf '%s' "$content"
}

# payload_colorize FILE: the JSON with syntax colors (plain when not fancy).
# FILE may be "-" for stdin. Uses bat when installed, a small awk colorizer
# otherwise. Template placeholders ({{NAME}}) always stand out, also inside
# strings.
# @brief Print JSON with syntax colors and highlighted placeholders; plain when output is not fancy.
# @param file payload file path, or - for stdin.
# @return 0 on success.
payload_colorize() {
  local file="${1:--}" level colors bat ph_esc r_esc
  level="$(fancy_level "$([[ -t 1 ]] && echo 1 || echo 0)")"
  [[ $level -ge 1 ]] || { cat "$file"; return 0; }
  colors="$(tput colors 2>/dev/null || echo 8)"
  if [[ $colors -ge 256 ]]; then
    ph_esc=$'\033[1;38;5;221m'
  else
    ph_esc=$'\033[1;33m'
  fi
  r_esc=$'\033[0m'
  bat="$(command -v bat 2>/dev/null || command -v batcat 2>/dev/null || true)"
  # bat renders bare placeholders (invalid JSON, e.g. "id": {{CLINIC_ID}}) as
  # noisy per-character runs; those files keep the awk colorizer. Stdin ("-")
  # is only used for --filled, which is valid JSON by construction.
  if [[ -n $bat ]] && { [[ $file == - ]] || ! grep -q ':[[:space:]]*{{' "$file"; }; then
    "$bat" --color=always --style=plain -l json "$file" |
      sed -E "s/\\{\\{[A-Z_]+\\}\\}/${ph_esc}&${r_esc}/g" # escaped: GNU sed rejects a bare {{
    return 0
  fi
  awk -v hi="$colors" '
    BEGIN {
      R = "\033[0m"
      if (hi >= 256) {
        KEY = "\033[1;38;5;111m"; STR = "\033[38;5;114m"; NUM = "\033[38;5;216m"
        BOOL = "\033[1;38;5;213m"; PH = "\033[1;38;5;221m"; DIM = "\033[2m"
      } else {
        KEY = "\033[1;34m"; STR = "\033[1;32m"; NUM = "\033[1;33m"
        BOOL = "\033[1;35m"; PH = "\033[1;33m"; DIM = "\033[2m"
      }
    }
    {
      line = $0
      if (match(line, /^[ \t]*"[^"]*"[ \t]*:/)) {
        head = substr(line, RSTART, RLENGTH)
        rest = substr(line, RSTART + RLENGTH)
        if (match(head, /"[^"]*"/)) {
          key = substr(head, RSTART, RLENGTH)
          sub(/"[^"]*"/, KEY key R, head)
        }
        value = rest
        sub(/^[ \t]+/, "", value)
        sub(/[ \t]*,[ \t]*$/, "", value)
        col = ""
        if (value ~ /^"/) col = STR
        else if (value ~ /^\{\{/) col = NUM
        else if (value ~ /^-?[0-9]/) col = NUM
        else if (value ~ /^(true|false|null)$/) col = BOOL
        if (col != "" && length(value) > 0) {
          i = index(rest, value)
          rest = substr(rest, 1, i - 1) col value R substr(rest, i + length(value))
        }
        line = head rest
      } else if (match(line, /[][{}]/)) {
        b = substr(line, RSTART, 1)
        line = substr(line, 1, RSTART - 1) DIM b R substr(line, RSTART + 1)
      }
      gsub(/\{\{[A-Z_]+\}\}/, PH "&" R, line)
      print line
    }
  ' "$file"
}

# @brief Print a boxed, colorized view of one payload together with its placeholder hints.
# @param group group name.
# @param name payload name.
# @param file payload file path.
# @return 0 on success.
payload_show() { # group name file: the boxed, colorized view of one payload
  local group="$1" name="$2" file="$3" content title bar icon="" ph hint
  local level i pad max phs=() hints=()
  [[ -f $file ]] || die "no payload file: $file"
  content="$(<"$file")"
  level="$(fancy_level "$([[ -t 1 ]] && echo 1 || echo 0)")"
  title="$group/$name.json"
  [[ $level -ge 3 && -n $S_i_book ]] && icon="$S_i_book"
  if [[ $level -ge 1 ]]; then
    pad=2
    [[ -n $icon ]] && pad=6
    bar=""
    for ((i = 0; i < ${#title} + pad; i++)); do bar+="─"; done
    printf '%s\n' "${S_ACC}╭${bar}╮${S_R}"
    printf '%s\n' "${S_ACC}│ ${S_B}${icon:+$icon$S_GAP}$title${S_R}${S_ACC} │${S_R}"
    printf '%s\n' "${S_ACC}╰${bar}╯${S_R}"
  else
    printf '%s\n' "$title"
  fi
  if [[ $content == *'{{'* ]]; then
    for ph in $(printf '%s' "$content" | grep -o '{{[A-Z_]*}}' | sort -u); do
      hint="${ph#??}"
      hint="${hint%??}"
      phs+=("$ph")
      hints+=("$(payload_hint "$hint")")
      (( ${#ph} > max )) && max=${#ph}
    done
    max=${max:-0}
    printf '%s\n' "${S_LBL}Placeholders${S_R}"
    for ((i = 0; i < ${#phs[@]}; i++)); do
      printf '  %s%-*s%s %s%s%s\n' "$S_INFO" "$max" "${phs[i]}" "$S_R" "$S_DIM" "${hints[i]}" "$S_R"
    done
    echo
  fi
  payload_colorize "$file"
  printf '%s\n' "${S_DIM}raw copy: $PROG payload $group $name --raw${S_R}"
}

# @brief Pick a payload group interactively with fzf.
# @param dir payload directory.
# @return 0 on selection, non-zero when the user quits.
payload_group_fzf() { # dir: pick a group (fzf); non-zero when quit
  local dir="$1" dir_q
  # %q output is already sh-safe on its own; adding quotes would break paths
  # with spaces (the backslashes would become literal). fzf quotes {} itself.
  dir_q="$(printf '%q' "$dir")"
  payload_groups "$dir" |
    fzf --prompt 'payload group ▸ ' --reverse --border \
      --header 'enter: select   esc: quit' \
      --preview "ls $dir_q/{} 2>/dev/null | sed 's/[.]json\$//'" \
      --preview-window 'right,60%,wrap'
}

# @brief Pick a payload in a group with fzf, previewing filled and raw forms.
# @param dir payload directory.
# @param group group name.
# @return 0 on selection, non-zero when the user goes back.
payload_name_fzf() { # dir group: pick a payload (fzf); preview shows it filled
  local dir="$1" group="$2" f out="" filled raw view_fancy
  local script_q group_q
  for f in "$dir/$group"/*.json; do
    [[ -f $f ]] || continue
    out+="$(basename "$f" .json)"$'\n'
  done
  [[ -n $out ]] || return 1
  # The preview renders through vdt itself: the filled body by default (every
  # {{NAME}} gets a sample value, so the output is valid JSON and bat colors
  # it cleanly), ctrl-r the raw template view, ctrl-f back to filled. fzf
  # replaces {} with the item already single-quoted; the %q'd static parts
  # are used bare (%q escapes spaces itself).
  view_fancy="$(fancy_level 1)"
  script_q="$(printf '%q' "$SCRIPT")"
  group_q="$(printf '%q' "$group")"
  filled="VALHALLA_FANCY=$view_fancy $script_q payload $group_q {} --filled"
  raw="VALHALLA_FANCY=$view_fancy $script_q payload $group_q {}"
  # enter: the full view replaces the fzf screen ("press any key" returns to
  # the list); esc: back to the group list.
  printf '%s' "$out" |
    fzf --prompt "$group ▸ " --reverse --border \
      --header 'enter: view   ctrl-r: template   ctrl-f: filled   esc: back' \
      --preview "$filled" \
      --preview-window 'right,60%,wrap' \
      --bind "ctrl-r:change-preview($raw)" \
      --bind "ctrl-f:change-preview($filled)" \
      --bind "enter:execute(VALHALLA_FANCY=$view_fancy $script_q payload $group_q {})"
}

# @brief Loop over group and payload pickers until the user quits.
# @param dir payload directory.
# @return 0 on success.
payload_browse() { # dir: group -> payload; enter views in place, esc backs up
  local dir="$1" group
  while :; do
    group="$(payload_group_fzf "$dir")" || return 0
    payload_name_fzf "$dir" "$group" || true
  done
}

# @brief List every payload group with its payload count.
# @param dir payload directory.
# @return 0 on success.
payload_list_all() { # dir: every group with its payload count
  local dir="$1" g n
  local -a lines=()
  lines+=("${S_HEAD}Payload groups${S_R}")
  for g in $(payload_groups "$dir"); do
    n="$(payload_count "$dir" "$g")"
    lines+=("  ${S_VAL}${g}${S_R} ${S_DIM}(${n} payloads)${S_R}")
  done
  printf '%s\n' "${lines[@]}"
}

# @brief List the payload names inside one group.
# @param dir payload directory.
# @param group group name.
# @return 0 on success.
payload_list_group() { # dir group: the payloads in one group
  local dir="$1" group="$2" f name
  local -a lines=()
  lines+=("${S_HEAD}Payloads in $group${S_R}")
  for f in "$dir/$group"/*.json; do
    [[ -f $f ]] || continue
    name="$(basename "$f" .json)"
    lines+=("  ${S_VAL}${name}${S_R}")
  done
  printf '%s\n' "${lines[@]}"
}

# @brief Implement payload: print one payload (raw, filled or styled), list a group, or browse with fzf.
# @param group optional group name, or group/name in one argument.
# @param name optional payload name.
# @return 0 on success.
cmd_payload() {
  local group="${1:-}" name="${2:-}" dir file  # Optional group and name, plus the resolved payload directory and file.
  [[ ${3:-} ]] && die "usage: $PROG payload [group] [name]"  # At most two positional arguments are accepted.
  [[ $RAW_PAYLOAD == 1 && $FILLED_PAYLOAD == 1 ]] &&  # The two display modes are mutually exclusive.
    die "--raw and --filled can't be used together"  # Explain the conflict.
  dir="$(payload_dir)" ||  # Locate the payload files.
    die "no payload files found (install the tool: $PROG install, or run from a checkout)"  # Fail with an install hint when they are missing.
  # "providers/create" (or "providers/create.json") in one argument also works
  if [[ $group == */* ]]; then  # Also accept the group and name together as group/name.
    name="${group#*/}"  # The text after the slash is the payload name.
    group="${group%%/*}"  # The text before the slash is the group name.
  fi
  name="${name%.json}"  # Drop an optional .json suffix.
  if [[ -n $group && -n $name ]]; then  # Both given: print that one payload directly.
    file="$dir/$group/$name.json"  # Build the payload file path.
    [[ -f $file ]] || die "no payload $group/$name.json (see: $PROG payload $group)"  # Fail when that payload does not exist.
    if [[ $RAW_PAYLOAD == 1 ]]; then  # --raw: print the file exactly as stored.
      cat "$file"  # Raw file contents.
    elif [[ $FILLED_PAYLOAD == 1 ]]; then  # --filled: substitute sample values for placeholders.
      payload_fill "$file" | payload_colorize -  # Fill placeholders, then colorize from standard input.
    else
      payload_show "$group" "$name" "$file"  # Default: the boxed, explained view.
    fi
    return 0  # Done.
  fi
  if [[ $RAW_PAYLOAD == 1 || $FILLED_PAYLOAD == 1 ]]; then  # These modes need a specific payload chosen.
    die "--raw/--filled need a group and a name (e.g.: $PROG payload providers create --raw)"  # Explain the required arguments.
  fi
  if [[ -n $group ]]; then  # Only a group given: list its payloads.
    [[ -d $dir/$group ]] || die "no payload group '$group' (see: $PROG payload)"  # Fail when the group does not exist.
    payload_list_group "$dir" "$group"  # List the names in that group.
    return 0  # Done.
  fi
  # No arguments: interactive with fzf, a plain list when piped.
  # Interact with fzf only on a real terminal; piped output gets a plain list so
  # it stays script-friendly.
  if [[ -t 0 && -t 1 ]]; then  # Go interactive only when both input and output are a real terminal.
    payload_browse "$dir"  # Terminal: browse with fzf.
  else
    payload_list_all "$dir"  # Piped: print a plain list that scripts can read.
  fi
}

# @brief Implement secret: generate a new TOKEN_SECRET and save it where the tool reads it from.
# @return 0 on success.
cmd_secret() {
  # Write where the secret is read from: an explicit --env-file, else the
  # env file in use if it sets TOKEN_SECRET, else the install directory's config.
  local target="$CONFIG_FILE" has=0 secret  # File to update, whether it already holds a secret, and the new value.
  if [[ -n $ENV_FILE_EXPLICIT ]]; then  # An explicit --env-file wins as the target.
    [[ $ENV_FILE != none ]] || die "no env file (--env-file none)"  # The value none means there is no file to write to.
    target="$ENV_FILE"  # Write to the explicitly chosen env file.
  elif [[ -f $ENV_FILE ]] && env_file_get "$ENV_FILE" TOKEN_SECRET >/dev/null; then  # Otherwise use the current env file if it already defines a secret.
    target="$ENV_FILE"  # Write to that env file.
  fi
  [[ -f $target ]] && env_file_get "$target" TOKEN_SECRET >/dev/null && has=1  # Record whether we are replacing an existing secret.
  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the change without writing.
    if [[ $has == 1 ]]; then  # Use a different message for replacing versus adding.
      say_info "Dry run: would replace TOKEN_SECRET in $target with a new random value."
    else
      say_info "Dry run: would add a random TOKEN_SECRET to $target."
    fi
    return
  fi
  if [[ $has == 1 ]]; then  # Replacing logs everyone out, so ask first.
    confirm "Replace the TOKEN_SECRET in $target? Tokens signed with the old one stop working." || die "cancelled"  # Warn that tokens signed with the old secret stop working.
  fi
  secret="$(new_secret)"  # Generate a fresh random secret.
  CONFIG_FILE="$target" save_secret_to_config "$secret"  # Save it to the chosen file by temporarily overriding CONFIG_FILE.
  say_ok "$([[ $has == 1 ]] && echo Replaced || echo Added) TOKEN_SECRET in $target. Apply it with: $PROG update"  # Confirm and tell the user how to apply the new secret.
}

INSTALLER_URL="${VALHALLA_INSTALLER_URL:-https://raw.githubusercontent.com/dr3mro/Valhalla-server/main/install.sh}"

# local_installer: the installer kept by install.sh in the install directory
# (or this checkout's install.sh); prints nothing if there's none.
# @brief Print the path to a locally available installer copy, if any.
# @return 0 when found, 1 otherwise.
local_installer() {
  local f
  for f in "$ROOT/installer" "$HOME/.local/share/vdt/installer" "$ROOT/install.sh"; do
    [[ -f $f ]] && { echo "$f"; return 0; }
  done
  return 0
}

# install_target: the install directory; --dir is passed for an installed copy (install.sh writes VERSION),
# so it acts on its own directory; from a checkout the installer's default
# (~/.local/share/vdt) is used.
# @brief Print the install directory: ROOT for an installed copy, the default data dir otherwise.
# @return 0 on success.
install_target() { if [[ -f $ROOT/VERSION ]]; then echo "$ROOT"; else echo "$HOME/.local/share/vdt"; fi; }

# fetch_installer: downloads install.sh from GitHub into a temp file and
# prints its path.
# @brief Download install.sh to a temporary file and print its path.
# @return 0 with the path printed, 1 on failure.
fetch_installer() {
  local tmp
  tmp="$(mktemp)"
  if ! curl -fsSL "$INSTALLER_URL" -o "$tmp"; then
    rm -f "$tmp"
    warn "couldn't download the installer from $INSTALLER_URL"
    return 1
  fi
  echo "$tmp"
}

# install_tool <ref>: download the installer and run it (downloads first; only
# then does it remove the current copy in the install directory and install, so
# a failed download changes nothing). Returns non-zero if the installer failed.
# @brief Download and run install.sh with --replace, removing the temporary installer afterwards.
# @param ref branch, tag or commit to install; defaults to main.
# @return The installer's status, or 1 when download fails.
install_tool() {
  local ref="${1:-main}" new
  local -a args=(--replace --ref "$ref")
  [[ -f $ROOT/VERSION ]] && args+=(--dir "$ROOT")
  # Download before replacing anything: if the download fails, the installed
  # copy is left untouched.
  new="$(fetch_installer)" || return 1
  sh "$new" "${args[@]}"; local rc=$?
  [[ "$new" != "$ROOT/install.sh" ]] && rm -f "$new"
  ((rc == 0)) || return "$rc"
  forget_update_state
}

# @brief Implement install: install or update vdt from GitHub.
# @param 1 optional ref to install, default main.
# @return 0 on success.
cmd_install() {
  local ref="${1:-main}" target  # Branch, tag or commit to install (default main) and the install directory.
  target="$(install_target)"  # Where the vdt command will be installed.
  local -a args=(--replace --ref "$ref")  # Installer arguments: replace the current copy at the chosen ref.
  [[ -f $ROOT/VERSION ]] && args+=(--dir "$ROOT")  # An installed copy updates itself in place.

  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the install without doing it.
    say_info "Dry run, nothing changed. Would download $INSTALLER_URL and run it with: ${args[*]}"  # Show the download URL and installer arguments.
    say_info "(it downloads $ref first; only then does it remove the current copy in $target and install)"  # Emphasise the safe download-first behaviour.
    [[ -f $target/VERSION ]] && say_info "installed now: $(cat "$target/VERSION")"  # Show the version that is currently installed.
    return 0  # No changes.
  fi
  step "Installing vdt ($ref)"  # Announce the install.
  install_tool "$ref" || die "the installer failed"  # Run the installer and fail clearly on error.
  say_ok "Installed vdt to $target"  # Confirm the install.
}

# @brief Implement uninstall: confirm and remove the installed vdt command and its rc-file lines.
# @return 0 on success.
cmd_uninstall() {
  local target installer tmp=""  # Install directory, chosen installer path and an optional temporary download.
  target="$(install_target)"  # Directory to uninstall from.
  local -a dir_args=()  # Installer arguments describing the target directory.
  [[ -f $ROOT/VERSION ]] && dir_args=(--dir "$ROOT")  # An installed copy uninstalls itself.
  installer="$(local_installer)"  # Prefer a local installer so uninstall works offline.

  if [[ $DRY_RUN == 1 ]]; then  # Dry run: describe the removal without doing it.
    say_info "Dry run: would run ${installer:-$INSTALLER_URL (downloaded)} with: --uninstall ${dir_args[*]}"  # Show the plan.
    return 0
  fi
  confirm "Remove the vdt command from $target and its lines in ~/.zshrc / ~/.bashrc? (config and dumps/ are kept)" ||  # Ask before removing; config and dumps are kept.
    die "cancelled"  # Abort if the user declines.
  # Prefer the local installer copy so uninstall works offline; download only
  # when no local copy exists.
  if [[ -z $installer ]]; then  # No local installer: download one.
    tmp="$(fetch_installer)" || die "couldn't download the installer from $INSTALLER_URL"  # Download the installer to a temporary file.
    installer="$tmp"  # Use the downloaded copy.
  fi
  step "Running $installer (--uninstall ${dir_args[*]})"  # Announce the uninstall.
  sh "$installer" --uninstall "${dir_args[@]}" || { [[ -n $tmp ]] && rm -f "$tmp"; die "the installer failed"; }  # Run it and clean up the temporary file on failure.
  [[ -n $tmp ]] && rm -f "$tmp"  # Clean up the temporary file on success too.
  return 0  # Done.
}

# @brief Implement version: print the installed or checkout version.
# @return 0 on success.
cmd_version() {
  if [[ -f $ROOT/VERSION ]]; then  # Installed copies have a VERSION file.
    echo "vdt $(cat "$ROOT/VERSION") (installed in $ROOT)"  # Show the installed version and path.
  else
    echo "valhalla.sh $(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown) (checkout $ROOT)"  # A checkout shows the git-described version.
  fi
}

# @brief Implement completion: print the zsh or bash completion script.
# @param 1 shell name; defaults to the shell in $SHELL.
# @return 0 on success.
cmd_completion() {
  local shell="${1:-}"  # Requested shell name; empty means auto-detect.
  if [[ -z $shell ]]; then  # No argument given: use the value of $SHELL.
    shell="$(basename "${SHELL:-zsh}")"  # Take the shell name from SHELL, defaulting to zsh.
  fi
  case "$shell" in  # Only zsh and bash completions are supported.
    zsh) completion_zsh ;;  # Generate the zsh completion script.
    bash) completion_bash ;;  # Generate the bash completion script.
    *) die "completion supports zsh or bash, got '$shell'" ;;  # Reject other shells.
  esac
}

# ── main ───────────────────────────────────────────────────────────────────
# The first non-option word selects the command; default to help when none was
# given.
command="${ARGS[0]:-help}"  # First non-option word is the command; default to help.
ARGS=("${ARGS[@]:1}")  # The remaining words are that command's arguments.
# Reject command-specific flags before any side effects, so a typo fails with
# a clear message instead of being silently ignored.
if [[ -n $UPDATE_SCOPE && $command != update ]]; then  # The image/tool-only flags only make sense with update.
  die "--image-only/--tool-only are update options (see: $PROG help)"  # Explain the misuse.
fi
if [[ $RAW_PAYLOAD == 1 && $command != payload ]]; then  # --raw is only meaningful for the payload command.
  die "--raw is a payload option (see: $PROG help)"  # Explain the misuse.
fi
if [[ $FILLED_PAYLOAD == 1 && $command != payload ]]; then  # --filled is only meaningful for the payload command.
  die "--filled is a payload option (see: $PROG help)"  # Explain the misuse.
fi

# help and completion need no runtime, dependencies or container, so handle
# them before any environment setup.
case "$command" in  # Commands needing no setup are handled first.
  help) usage; exit 0 ;;  # Print help and leave.
  completion) cmd_completion "${ARGS[@]}"; exit 0 ;;  # Print a completion script and leave.
esac

# Validate against the same COMMANDS list that feeds help and completion, so
# the dispatcher cannot drift from the documented command set.
known=" $(command_names)"  # Space-wrapped list of valid commands for membership tests.
[[ $known == *" $command "* ]] || { die "unknown command: $command (see: $PROG help)"; }  # Reject unknown commands using the same list that feeds help and completion.

# Add the rc-file completion line on first use, before the command runs.
install_completion  # Add tab completion to the rc files on first use.

# The runtime must be known before update notices inspect the container.
resolve_runtime  # Decide docker versus container before inspecting anything.
case "$command" in  # Decide whether to show and schedule update notices.
  install | uninstall | status) ;; # status checks for itself
  payload) ;;                      # needs no image or container
  update)
    # --tool-only updates the tool itself: no notice, like install.
    [[ $UPDATE_SCOPE == tool ]] || { show_update_notice; schedule_update_check; }  # A tool-only update skips notices, just like install.
    ;;
  *)
    show_update_notice  # Show any cached update notice.
    schedule_update_check  # Kick off a background refresh when it is due.
    ;;
esac
# Check dependencies after the command is known, so the error lists exactly
# what this command needs.
check_deps "$command" "${ARGS[0]:-}"  # Verify every tool and file this command needs, with install hints.

# Commands that touch neither image nor container run before runtime startup.
case "$command" in  # Commands that touch neither image nor container run here.
  secret | install | uninstall | version | payload) "cmd_${command}" "${ARGS[@]}"; exit $? ;;  # Dispatch and exit for storage-only commands.
esac

if [[ $command == test && -z $RT ]]; then  # Tests need only curl, not a runtime, since they check health themselves.
  cmd_test "${ARGS[@]}"  # Run the tests.
  exit $?  # Return the test status.
fi
if [[ $command == update && $UPDATE_SCOPE == tool ]]; then  # A tool-only update needs no runtime or container.
  "cmd_${command}" "${ARGS[@]}"  # Dispatch to the update command.
  exit $?  # Return its status.
fi
# Start the container service only now, for commands that actually use it.
pick_runtime  # Start the container service now, only for commands that need it.
# Talk to the port the existing container was created with, unless --port was
# given explicitly.
exists && adopt_container_port  # Use the container's own port unless --port was given.
# Only these commands need the composed env; loading it elsewhere would add an
# unwanted "Env file:" line to read-only output. The final line is the common
# dispatch point: every remaining command is implemented as cmd_<name>.
case "$command" in run | update | build | env) load_env ;; esac  # Only these commands parse the env file, avoiding noise in read-only output.
"cmd_${command}" "${ARGS[@]}"  # Final common dispatch: every remaining command is implemented as cmd_<name>.
