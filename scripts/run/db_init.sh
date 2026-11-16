#!/usr/bin/env bash
# The very first line tells the computer to run this file with the Bash shell.

# /**
#  * @file db_init.sh
#  * @brief Initialize the local PostgreSQL instance by applying scripts/db/init.sql with psql.
#  * @details This script sets up the local database for the application.
#  * It finds the repository folder, then uses the psql tool to send the init.sql file to PostgreSQL.
#  * That SQL file creates the tables the server needs in order to store and read data.
#  *
#  * WHY THIS FILE MATTERS IN THE SERVER:
#  * This is the database setup step that must run before the server can be used.
#  * A developer runs it once after installing PostgreSQL, or the other run scripts assume it has been done.
#  * Without it the server would start but every database call would fail because the tables would not exist.
#  *
#  * CONCEPTS TO LEARN (for beginners):
#  * - Bash shell scripts and variables: https://www.gnu.org/software/bash/manual/bash.html
#  * - The psql command-line tool: https://www.postgresql.org/docs/current/app-psql.html
#  *
#  * @author Amr Osman <dr3mro@gmail.com>
#  * @date 2026-11-16
#  */
# Abort on any error, unset variable, or failed pipeline stage so a failed DB init cannot look successful.
set -euo pipefail  # Stop on an error, on an unset variable, or on a failed step in a pipeline.

# Resolve the repository root from this script's own path so init.sql is found from any working directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"  # Absolute path to the repository root, used below to find init.sql.
# Connect over TCP as the default superuser to the default 'postgres' database and apply the init script;
# init.sql creates the application schema, so it must succeed before the server is started.
psql -h 127.0.0.1 -U postgres -d postgres -f "$SCRIPT_DIR/scripts/db/init.sql"  # Send the SQL file to the local database; any failure stops the script.
