#!/bin/sh
set -eu

# The bundled database is the only one this image talks to
export DB_HOST=127.0.0.1
export DB_PORT="${DB_PORT:-5432}"
export DB_NAME="${DB_NAME:-postgres}"
export DB_USER="${DB_USER:-postgres}"
export DB_PASS="${DB_PASS:-postgres}"
export DB_SSL=false

# Never fall back to the public default secret; tokens die with the wiped database anyway
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

# Wipe on every start: a restarted (not removed) container still has the old data dir and socket lock
rm -rf "$PGDATA" /run/postgresql
mkdir -p "$PGDATA" /run/postgresql
chown postgres:postgres "$PGDATA" /run/postgresql
chmod 700 "$PGDATA"

su-exec postgres initdb -D "$PGDATA" -U postgres --auth-local=trust --auth-host=scram-sha-256 \
    --encoding=UTF8 --no-locale >/dev/null

# Data is ephemeral: disable all durability guarantees for maximum throughput.
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

psql_admin -U "$DB_USER" -d "$DB_NAME" -f /app/init.sql

# init.sql hardcodes the "postgres" database; apply the setting to the one actually in use
psql_admin -d postgres -v db="$DB_NAME" <<'SQL'
ALTER DATABASE :"db" SET datestyle = 'iso, dmy';
SQL

LOG_DIR="${SERVER_LOG_DIR:-./logs/}"
UPLOAD_DIR="${SERVER_UPLOAD_DIR:-./uploads/}"
mkdir -p "$LOG_DIR" "$UPLOAD_DIR"
chown -R valhalla:valhalla "$LOG_DIR" "$UPLOAD_DIR"

exec su-exec valhalla "$@"
