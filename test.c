#define _POSIX_C_SOURCE 200809L

#include "session.h"
#include "time.h"
#include "util.h"

// C90
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(X) if (!(X)) { \
        printf("FAILED(%s:%d): %s\n", __FILE__, __LINE__, #X); \
        exit(EXIT_FAILURE); \
    }

static void test_session(void) {
    // session_new(), session_check(), session_iscompat(), session_isafter()
    struct session* session = session_new();
    ASSERT(session != NULL);
    ASSERT(session_check(session) == 0);
    ASSERT(session_iscompat(session, session));
    ASSERT(session_isafter(session, session));

    // session_copy()
    struct session* copy = session_copy(session);
    ASSERT(copy != NULL);
    ASSERT(session_iscompat(session, copy));
    ASSERT(session_isafter(session, copy));
    ASSERT(session_isafter(copy, session));
    session_delete(copy);

    // alter session for further tests
    session->t = 1000;
    mpz_set_ui(session->c, 127);
    mpz_set_ui(session->n, 521);
    mpz_mul(session->n_times_c, session->n, session->c);
    uint64_t done = session_work(session, 256);
    ASSERT(done == session->i);
    ASSERT(done <= 256);

    // session_save(), session_load()
    char filename[] = "/tmp/savefile.XXXXXX";
    if (mkstemp(filename) < 0) {
        LOG(FATAL, "Failed to create temporary file (%s)\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    session_save(session, filename);
    struct session* restored = session_new();
    ASSERT(session_load(restored, filename) == 1);
    if (remove(filename) != 0) {
        LOG(ERR, "Failed to remove temporary file (%s)\n", strerror(errno));
    }
    ASSERT(session_check(restored) == 0);
    ASSERT(session_iscompat(session, restored));
    ASSERT(session_isafter(session, restored));
    ASSERT(session_isafter(restored, session));
    session_delete(session);
    session = restored;

    // session_work()
    while (session_work(session, 37)) {
        ASSERT(session_check(session) == 0);
    }
    ASSERT(session->i == session->t);
    mpz_t result;
    mpz_init_set_str(result, "65536", 10);
    ASSERT(mpz_cmp(session->w, result) == 0);
    mpz_mod(result, session->w, session->n);
    ASSERT(mpz_get_ui(result) == 411);

    session_delete(session);
}

static void test_time(void) {
#define HTIME(seconds) do { \
        human_time_both(buffer, sizeof(buffer), seconds); \
        printf("%s\n", buffer); \
    } while (0)
    printf("Please check the following ETAs:\n");
    char buffer[1024];
    double minute = 60;
    double hour = 60*minute;
    double day = 24*hour;
    double year = 365 * day;
    HTIME(1033*year + 42*day + 3*hour + 49*minute + 37.7001);
    HTIME(     year + 42*day + 3*hour + 49*minute + 37.7001);
    HTIME(            42*day + 3*hour + 49*minute + 37.7001);
    HTIME(               day + 3*hour + 49*minute + 37.7001);
    HTIME(                     3*hour + 49*minute + 37.7001);
    HTIME(                       hour + 49*minute + 37.7001);
    HTIME(                              49*minute + 37.7001);
    HTIME(                                 minute + 37.7001);
    HTIME(                                          37.7001);
    HTIME(                                           1.7001);
    HTIME(                                           0.7001);
#undef HTIME
}

extern int main(int argc, char** argv) {
    // parse arguments
    parse_debug_args(&argc, argv);

    test_session();
    printf("Automatic tests passed\n");
    test_time();
}
