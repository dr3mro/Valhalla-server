#!/bin/sh
# /**
#  * @file entrypoint.sh
#  * @brief Container entrypoint: configures an ephemeral in-container PostgreSQL (tmpfs when available), initializes/restores the Valhalla database, and supervises the server with SIGHUP restarts and shutdown-time dump persistence.
#  * @details Configure and run an in-container PostgreSQL, load the Valhalla schema, then supervise the server with restart and shutdown handling.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the program Docker runs when the container starts.
#  * It sets up a throwaway database, loads data, and keeps the server alive until it is stopped.
#  * Without it, the container would have no database and the server could not start.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - PostgreSQL documentation: https://www.postgresql.org/docs/current/
#  * - pg_ctl: https://www.postgresql.org/docs/current/app-pg-ctl.html
#  * - psql: https://www.postgresql.org/docs/current/app-psql.html
#  * - pg_dump: https://www.postgresql.org/docs/current/app-pgdump.html
#  * - POSIX signals: https://www.gnu.org/software/libc/manual/html_node/Signals.html
#  * - posix su-exec: https://github.com/ncopa/su-exec
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# valhalla-entrypoint: persist-v1
# (valhalla.sh looks for the line above to know this image supports
#  VALHALLA_PERSIST, saving on shutdown and SIGHUP server restarts.)
# -e aborts startup on any failure and -u catches unset settings before serving.
set -eu  # e = stop on any error, u = error on unset variables.

# The bundled database is the only one this image talks to
export DB_HOST=127.0.0.1  # everything in this container talks to the bundled database on localhost.
export DB_PORT="${DB_PORT:-5432}"  # ${DB_PORT:-5432} means 'use DB_PORT if set, otherwise 5432'.
export DB_NAME="${DB_NAME:-postgres}"  # default database name is postgres.
export DB_USER="${DB_USER:-postgres}"  # default user is postgres.
# All DB settings are overridable but default to the bundled local cluster.
export DB_PASS="${DB_PASS:-postgres}"  # this password is only for the throwaway local cluster.
export DB_SSL=false  # TLS is off because the database is reachable only inside the container.

# Persistence (off by default): with VALHALLA_PERSIST=1 the database is loaded
# from VALHALLA_PERSIST_FILE at start (instead of init.sql) and dumped back to
# it when the container stops. Mount a host directory at its location.
PERSIST="${VALHALLA_PERSIST:-0}"  # 0 = persistence off; 1 = load a dump at start and save one on stop.
PERSIST_FILE="${VALHALLA_PERSIST_FILE:-/persist/valhalla.sql}"  # where that dump lives; mount a host folder here to keep the data.

# Never fall back to the public default secret
if [ -z "${TOKEN_SECRET:-}" ]; then  # if no secret was supplied, generate one instead of reusing a public default.
    # Generate a per-run secret via /dev/urandom rather than reusing a public default.
    TOKEN_SECRET="$(od -An -tx1 -N32 /dev/urandom | tr -d ' \n')"  # od reads 32 random bytes; tr converts them to a hex string.
    export TOKEN_SECRET  # export so the server process inherits the generated secret.
    echo "TOKEN_SECRET not set: generated a random one for this run" >&2  # tell the operator that a temporary secret was created.
fi

# Use /dev/shm (tmpfs) when available so the whole cluster lives in RAM.
# Fall back to the normal data directory if /dev/shm is missing or too small.
if [ -d /dev/shm ] && [ "$(df -P /dev/shm | awk 'NR==2{print $4}')" -gt 65536 ] 2>/dev/null; then  # check /dev/shm exists and has more than about 64 MB free.
    PGDATA=/dev/shm/pgdata  # /dev/shm is RAM-backed, so a database stored there is very fast.
else
    PGDATA=/var/lib/postgresql/data  # fallback is the normal disk-backed PostgreSQL directory.
fi

# Fresh cluster on every start: a restarted (not removed) container still has
# the old data dir and socket lock. With persistence the data comes back from
# the dump below.
rm -rf "$PGDATA" /run/postgresql  # remove leftover data and socket locks from a previous run of this container.
mkdir -p "$PGDATA" /run/postgresql  # recreate the empty directories.
# PostgreSQL refuses to run as root; hand these dirs to the postgres user.
chown postgres:postgres "$PGDATA" /run/postgresql  # PostgreSQL refuses to run as root, so give the dirs to the postgres user.
chmod 700 "$PGDATA"  # 700 means only the owner may read or write the data directory.

# Initialize the throwaway cluster as postgres with trust local auth.
# su-exec drops root privileges; initdb creates a brand-new database cluster.
su-exec postgres initdb -D "$PGDATA" -U postgres --auth-local=trust --auth-host=scram-sha-256 \
    --encoding=UTF8 --no-locale >/dev/null  # send noisy initialization output to /dev/null.

# The cluster itself is throwaway (rebuilt on every start), so durability
# settings are off for throughput; persistence works through dumps.
# pg_ctl starts the cluster with speed settings (durability is off, see above).
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

# @brief Run psql as the postgres user with stop-on-error and quiet warnings.
# @param psql arguments (database selection, SQL, variables).
# @return psql's status.
psql_admin() {  # define a helper that runs psql as postgres and stops on SQL errors.
    # PGOPTIONS quiets informational messages from the server.
    PGOPTIONS='-c client_min_messages=warning' \
        su-exec postgres psql -X -q -v ON_ERROR_STOP=1 -p "$DB_PORT" "$@"  # -X skips user config, -q is quiet, ON_ERROR_STOP makes SQL errors fatal.
}

# Conditional DDL uses psql's gexec meta-command so existing roles/DBs are kept.
psql_admin -d postgres -v user="$DB_USER" -v pass="$DB_PASS" -v db="$DB_NAME" <<'SQL'  # the SQL block creates the role and database only if they do not already exist.
SELECT format('CREATE ROLE %I', :'user')
WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = :'user') \gexec
ALTER ROLE :"user" WITH LOGIN SUPERUSER PASSWORD :'pass';
SELECT format('CREATE DATABASE %I OWNER %I', :'db', :'user')
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = :'db') \gexec
SQL

# Load the saved database when persistence is on and a dump exists;
# otherwise start from init.sql (the default).
if [ "$PERSIST" = 1 ] && [ -s "$PERSIST_FILE" ]; then  # restore a saved dump only when persistence is on and the file has content.
    echo "valhalla: restoring the database from $PERSIST_FILE" >&2  # -s tests that a file exists and is not empty.
    if ! psql_admin -U "$DB_USER" -d "$DB_NAME" -f "$PERSIST_FILE"; then  # feed the saved SQL dump into the chosen database.
        echo "valhalla: restoring $PERSIST_FILE failed; refusing to start with a partial database" >&2
        exit 1  # refuse to start with a half-restored database.
    fi
else
    [ "$PERSIST" = 1 ] && echo "valhalla: persistence on, no saved database yet ($PERSIST_FILE); using init.sql" >&2  # explain why the schema from init.sql is used instead.
    psql_admin -U "$DB_USER" -d "$DB_NAME" -f /app/init.sql  # load the schema from the container's own copy of init.sql.
fi

# init.sql hardcodes the "postgres" database; apply the setting to the one actually in use
# The SQL below reapplies the configured date format because init.sql hardcodes one.
psql_admin -d postgres -v db="$DB_NAME" <<'SQL'
ALTER DATABASE :"db" SET datestyle = 'iso, dmy';
SQL

# Log/upload dirs default under the working directory; override via SERVER_* vars.
LOG_DIR="${SERVER_LOG_DIR:-./logs/}"  # folder for server logs; override with SERVER_LOG_DIR.
UPLOAD_DIR="${SERVER_UPLOAD_DIR:-./uploads/}"  # folder for uploaded files; override with SERVER_UPLOAD_DIR.
mkdir -p "$LOG_DIR" "$UPLOAD_DIR"  # create both folders if they are missing.
# The server runs as the valhalla user and must be able to write these dirs.
chown -R valhalla:valhalla "$LOG_DIR" "$UPLOAD_DIR"  # the server runs as 'valhalla', so it must own these folders.

# save_db: dumps the database to PERSIST_FILE atomically (temp file + rename)
# and gives it to the owner of the mounted directory.
# @brief Dump the DB to PERSIST_FILE atomically when persistence is enabled.
# @return 0 always; a failed dump keeps the previous one.
save_db() {
    [ "$PERSIST" = 1 ] || return 0  # do nothing at all when persistence is turned off.
    dir="$(dirname "$PERSIST_FILE")"  # the folder that will hold the dump.
    mkdir -p "$dir"  # make sure it exists, for example after mounting a fresh volume.
    tmp="$PERSIST_FILE.tmp.$$"  # build a unique temp filename; $$ is this shell's process ID.
    # pg_dump exports the database as SQL text into the temporary file.
    if su-exec postgres pg_dump -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" \
        --clean --if-exists --no-owner --no-privileges >"$tmp" 2>/dev/null && [ -s "$tmp" ]; then  # --clean/--if-exists make the dump reusable; -s checks it is not empty.
        owner="$(stat -c '%u:%g' "$dir" 2>/dev/null || echo 0:0)"  # use the mounted folder's owner so the host can write the dump.
        chown "$owner" "$tmp" 2>/dev/null || true  # chown can fail on some filesystems; that is not fatal.
        mv -f "$tmp" "$PERSIST_FILE"  # rename atomically so a partial dump is never mistaken for a good one.
        echo "valhalla: saved the database to $PERSIST_FILE" >&2  # tell the operator the dump was saved.
    else
        rm -f "$tmp"  # delete the incomplete temporary file.
        echo "valhalla: saving the database to $PERSIST_FILE failed; kept the previous dump" >&2  # keep the previous good dump when saving fails.
    fi
}

SERVER_PID=""  # an empty PID means no server has been started yet.
# @brief Launch the server as the valhalla user in the background.
# @param Command and arguments to execute.
# @return Nothing; records the PID in SERVER_PID.
start_server() {  # define a launcher that runs the server as the valhalla user in the background.
    su-exec valhalla "$@" &  # su-exec switches user without leaving an extra process behind.
    SERVER_PID=$!  # $! is the process ID of the background server.
}

# @brief Terminate the background server and wait for it to exit.
# @return Nothing.
stop_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then  # kill -0 only checks that a process exists; it does not send a signal.
        kill -TERM "$SERVER_PID" 2>/dev/null || true  # TERM asks the process to shut down gracefully.
        wait "$SERVER_PID" 2>/dev/null || true  # wait for the server to actually exit.
    fi
    SERVER_PID=""  # forget the old process ID.
}

# On stop: end the server, save the database, stop PostgreSQL.
# @brief TERM/INT handler: stop server, save DB, stop PostgreSQL, exit 0.
# @return Does not return.
shutdown() {
    stop_server  # stop the server first.
    save_db  # save the database before PostgreSQL is stopped.
    su-exec postgres pg_ctl -D "$PGDATA" -m fast -w stop >/dev/null 2>&1 || true  # stop PostgreSQL quickly, ignoring any errors.
    exit 0  # exit successfully.
}

# SIGHUP restarts only the server (e.g. after loading a dump, so its
# in-memory caches start clean); the database keeps running.
RESTART=0  # flag set by the HUP trap to request a server restart.
trap 'shutdown' TERM INT  # TERM and INT mean 'shut down cleanly'.
trap 'RESTART=1; stop_server' HUP  # HUP means 'reload': stop the server but keep PostgreSQL running.

start_server "$@"  # start the command passed to the container.
# Supervisor loop: wait for the server, restart on SIGHUP, otherwise persist and shut down.
while :; do  # supervise the server forever.
    status=0  # assume success unless the server reports an error.
    wait "$SERVER_PID" 2>/dev/null || status=$?  # wait returns early on a trapped signal, so capture the real status.
    if [ "$RESTART" = 1 ]; then  # if HUP arrived, restart the server instead of shutting down.
        RESTART=0  # clear the flag so the next HUP can be handled.
        echo "valhalla: restarting the server" >&2
        start_server "$@"  # launch the server again with the original command.
        continue  # go back to waiting for it.
    fi
    # wait returns early when a trapped signal arrives; keep waiting while the
    # server is still alive.
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then  # the server is still alive, so keep waiting.
        continue
    fi
    # The server exited on its own: save what we have and stop.
    save_db  # the server exited by itself: save data and stop PostgreSQL.
    su-exec postgres pg_ctl -D "$PGDATA" -m fast -w stop >/dev/null 2>&1 || true  # fast shutdown mode for PostgreSQL.
    exit "$status"  # exit with the server's own status.
done
