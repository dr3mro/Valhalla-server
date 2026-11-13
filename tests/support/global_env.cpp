#include <cstdlib>

namespace
{
    // Deterministic process-wide configuration for tests that go through the
    // Store singletons (Configurator, DOSDetector, SessionManager, ...).
    //
    // Static initialisation runs before main(), so this is guaranteed to be
    // in place before the first Store::getObject<Configurator>() anywhere in
    // the suite - independent of test ordering across translation units.
    //
    // - TOKEN_*: known token parameters for the JWT verifier (the .env file is
    //   never read).
    // - CLN_FRQ=1: keeps the DOSDetector cleanup thread's real-time cadence
    //   short so the process can shut down promptly after the run.
    // - VALHALLA_TEST_NO_DB: hermeticity seam (see DatabaseController::
    //   ensureBackends()). The connection pool is never created, so no test
    //   ever opens a socket to PostgreSQL - even when a dev database is
    //   running on localhost - and query paths report "unavailable"
    //   deterministically instead of calling exit(EXIT_FAILURE).
    struct TestEnvironmentSetup
    {
        TestEnvironmentSetup()
        {
            setenv("TOKEN_SECRET", "valhalla-unit-test-secret-key", 1);
            setenv("TOKEN_ISSUER", "valhalla-unit-test-issuer", 1);
            setenv("TOKEN_TYPE", "JWS", 1);
            setenv("TOKEN_VALIDITY", "60", 1);
            setenv("CLN_FRQ", "1", 1);
            setenv("VALHALLA_TEST_NO_DB", "true", 1);

            // Make sure nothing leaks in from the developer's shell.
            unsetenv("WHITELIST");
            unsetenv("BLACKLIST");
            unsetenv("DB_HOST");
            unsetenv("DB_PORT");
            unsetenv("DB_NAME");
            unsetenv("DB_USER");
            unsetenv("DB_PASS");
            unsetenv("DB_SSL");
            unsetenv("DB_MAX_CONN");
        }
    };

    const TestEnvironmentSetup testEnvironmentSetup;
}  // namespace
