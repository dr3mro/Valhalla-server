#!/bin/sh
# valhalla-entrypoint: persist-v1
# (valhalla.sh looks for the line above to know this image supports
#  VALHALLA_PERSIST, saving on shutdown and SIGHUP server restarts.)
set -eu

# The bundled database is the only one this image talks to
export DB_HOST=127.0.0.1
export DB_PORT="${DB_PORT:-5432}"
export DB_NAME="${DB_NAME:-postgres}"
export DB_USER="${DB_USER:-postgres}"
export DB_PASS="${DB_PASS:-postgres}"
export DB_SSL=false

# Persistence (off by default): with VALHALLA_PERSIST=1 the database is loaded
# from VALHALLA_PERSIST_FILE at start (instead of init.sql) and dumped back to
# it when the container stops. Mount a host directory at its location.
PERSIST="${VALHALLA_PERSIST:-0}"
PERSIST_FILE="${VALHALLA_PERSIST_FILE:-/persist/valhalla.sql}"

# Never fall back to the public default secret
if [ -z "${TOKEN_SECRET:-}" ]; then
    TOKEN_SECRET="$(od -An -tx1 -N32 /dev/urandom | tr -d ' \n')"
    export TOKEN_SECRET
    echo "TOKEN_SECRET not set: generated a random one for this run" >&2
fi

# Use /dev/shm (tmpfs) when available so the whole cluster lives in RAM.
# Fall back to the normal data directory if /dev/shm is missing or too small.
if [ -d /dev/shm ] && [ "$(df -P /dev/shm | awk 'NR==2{print $4}')" -gt 65536 ] 2>/dev/null; then
    PGDATA=/dev/shm/pgdata
else
    PGDATA=/var/lib/postgresql/data
fi

# Fresh cluster on every start: a restarted (not removed) container still has
# the old data dir and socket lock. With persistence the data comes back from
# the dump below.
rm -rf "$PGDATA" /run/postgresql
mkdir -p "$PGDATA" /run/postgresql
chown postgres:postgres "$PGDATA" /run/postgresql
chmod 700 "$PGDATA"

su-exec postgres initdb -D "$PGDATA" -U postgres --auth-local=trust --auth-host=scram-sha-256 \
    --encoding=UTF8 --no-locale >/dev/null

# The cluster itself is throwaway (rebuilt on every start), so durability
# settings are off for throughput; persistence works through dumps.
su-exec postgres pg_ctl -D "$PGDATA" -w -t 30 \
    -o "-c listen_addresses=127.0.0.1 -p $DB_PORT \
        -c fsync=off \
        -c synchronous_commit=off \
        -c full_page_writes=off \
        -c wal_level=minimal \
        -c max_wal_senders=0 \
        -c checkpoint_completion_target=0.9 \
        -c shared_buffers=128MB \
        -c work_mem=16MB" start

psql_admin() {
    PGOPTIONS='-c client_min_messages=warning' \
        su-exec postgres psql -X -q -v ON_ERROR_STOP=1 -p "$DB_PORT" "$@"
}

psql_admin -d postgres -v user="$DB_USER" -v pass="$DB_PASS" -v db="$DB_NAME" <<'SQL'
SELECT format('CREATE ROLE %I', :'user')
WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = :'user') \gexec
ALTER ROLE :"user" WITH LOGIN SUPERUSER PASSWORD :'pass';
SELECT format('CREATE DATABASE %I OWNER %I', :'db', :'user')
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = :'db') \gexec
SQL

# Load the saved database when persistence is on and a dump exists;
# otherwise start from init.sql (the default).
if [ "$PERSIST" = 1 ] && [ -s "$PERSIST_FILE" ]; then
    echo "valhalla: restoring the database from $PERSIST_FILE" >&2
    if ! psql_admin -U "$DB_USER" -d "$DB_NAME" -f "$PERSIST_FILE"; then
        echo "valhalla: restoring $PERSIST_FILE failed; refusing to start with a partial database" >&2
        exit 1
    fi
else
    [ "$PERSIST" = 1 ] && echo "valhalla: persistence on, no saved database yet ($PERSIST_FILE); using init.sql" >&2
    psql_admin -U "$DB_USER" -d "$DB_NAME" -f /app/init.sql
fi

# init.sql hardcodes the "postgres" database; apply the setting to the one actually in use
psql_admin -d postgres -v db="$DB_NAME" <<'SQL'
ALTER DATABASE :"db" SET datestyle = 'iso, dmy';
SQL

LOG_DIR="${SERVER_LOG_DIR:-./logs/}"
UPLOAD_DIR="${SERVER_UPLOAD_DIR:-./uploads/}"
mkdir -p "$LOG_DIR" "$UPLOAD_DIR"
chown -R valhalla:valhalla "$LOG_DIR" "$UPLOAD_DIR"

# save_db: dumps the database to PERSIST_FILE atomically (temp file + rename)
# and gives it to the owner of the mounted directory.
save_db() {
    [ "$PERSIST" = 1 ] || return 0
    dir="$(dirname "$PERSIST_FILE")"
    mkdir -p "$dir"
    tmp="$PERSIST_FILE.tmp.$$"
    if su-exec postgres pg_dump -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
        --clean --if-exists --no-owner --no-privileges >"$tmp" 2>/dev/null && [ -s "$tmp" ]; then
        owner="$(stat -c '%u:%g' "$dir" 2>/dev/null || echo 0:0)"
        chown "$owner" "$tmp" 2>/dev/null || true
        mv -f "$tmp" "$PERSIST_FILE"
        echo "valhalla: saved the database to $PERSIST_FILE" >&2
    else
        rm -f "$tmp"
        echo "valhalla: saving the database to $PERSIST_FILE failed; kept the previous dump" >&2
    fi
}

SERVER_PID=""
start_server() {
    su-exec valhalla "$@" &
    SERVER_PID=$!
}

stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -TERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    SERVER_PID=""
}

# On stop: end the server, save the database, stop PostgreSQL.
shutdown() {
    stop_server
    save_db
    su-exec postgres pg_ctl -D "$PGDATA" -m fast -w stop >/dev/null 2>&1 || true
    exit 0
}

# SIGHUP restarts only the server (e.g. after loading a dump, so its
# in-memory caches start clean); the database keeps running.
RESTART=0
trap 'shutdown' TERM INT
trap 'RESTART=1; stop_server' HUP

start_server "$@"
while :; do
    status=0
    wait "$SERVER_PID" 2>/dev/null || status=$?
    if [ "$RESTART" = 1 ]; then
        RESTART=0
        echo "valhalla: restarting the server" >&2
        start_server "$@"
        continue
    fi
    # wait returns early when a trapped signal arrives; keep waiting while the
    # server is still alive.
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        continue
    fi
    # The server exited on its own: save what we have and stop.
    save_db
    su-exec postgres pg_ctl -D "$PGDATA" -m fast -w stop >/dev/null 2>&1 || true
    exit "$status"
done
