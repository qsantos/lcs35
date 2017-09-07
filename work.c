#define _POSIX_C_SOURCE 200809L

// local includes
#include "util.h"
#include "time.h"
#include "session.h"

// C99
#include <inttypes.h>

// C90
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void show_progress(uint64_t i, uint64_t t,
                          uint64_t* prev_i, double* prev_time) {
    // compute remaining time in seconds
    double now = real_clock();
    double units_per_second = (double) (i - *prev_i) / (now - *prev_time);
    double seconds_left = (double) (t - i) / units_per_second;

    // format estimated remaining time in a human readable manner
    char human_time[1024];
    if (isfinite(seconds_left)) {
        human_time_both(human_time, sizeof(human_time), seconds_left);
    } else {
        snprintf(human_time, sizeof(human_time), "unknown");
    }

    double progress = 100. * (double) i / (double) t;  // progress percentage
    fprintf(stderr, "%9.6f%% (%#.12" PRIx64 " / %#.12" PRIx64 ") ETA: %s",
            progress, i, t, human_time);

    // update timer
    *prev_i = i;
    *prev_time = now;
}

/* when SIGINT is hit, just save the current work and exit
 * NOTE: the handler should be registered *after* the session is fully
 * loaded; otherwise, a badly timed SIGINT might save an empty session */
struct session* session = NULL;
sqlite3* db;
void handle_sigint(int sig){
    if (sig != SIGINT) {
        return;
    }
    fprintf(stderr, "\r\33[K");  // clear line
    if (session_save(session, db) != 0) {
        LOG(FATAL, "failed to update session file!");
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}

extern int main(int argc, char** argv) {
    if (setlocale(LC_ALL, "") == NULL) {
        LOG(WARN, "failed to set locale (%s)", strerror(errno));
    }

    // parse arguments
    parse_debug_args(&argc, argv);
    const char* savefile = "savefile.db";
    if (argc == 2) {
        savefile = argv[1];
    }

    // display brand string
    char brand_string[49];
    get_brand_string(brand_string);
    printf("%s\n", brand_string);

    // open sqlite3 database
    if (sqlite3_open(savefile, &db) != SQLITE_OK) {
        LOG(FATAL, "%s", sqlite3_errmsg(db));
        return 1;
    }

    // create table if necessary
    char* errmsg;
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS checkpoint ("
        "    i INTEGER UNIQUE,"
        "    w TEXT,"
        "    first_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    last_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");", NULL, NULL, &errmsg);
    if (errmsg != NULL) {
        LOG(FATAL, "%s", errmsg);
        return 1;
    }

    session = session_new();

    // try to resume from session file
    int ret = session_load(session, db);
    if (ret == 0) {
        LOG(DEBUG, "session file not found; starting from scratch");
        // continue
    } else if (ret == 1) {
        LOG(DEBUG, "session file valid; resuming from it");
    } else {
        // this should really not happen!
        LOG(FATAL, "session file invalid");
        exit(EXIT_FAILURE);
    }

    // register signal handler for SIGINT
    signal(SIGINT, handle_sigint);

    // initialize timer
    uint64_t prev_i = session->i;
    double prev_time = real_clock();
    show_progress(session->i, session->t, &prev_i, &prev_time);

    while (session_work(session, 1ull<<20) != 0) {
        fprintf(stderr, "\r\33[K");  // clear line for errors messages

        if (session_check(session) != 0) {
            LOG(FATAL, "an error happened during computation");
            exit(EXIT_FAILURE);
        }

        if ((session->i >> 20) % 32 == 0) {
            if (session_save(session, db) != 0) {
                LOG(FATAL, "failed to update session file!");
                exit(EXIT_FAILURE);
            }
        }

        show_progress(session->i, session->t, &prev_i, &prev_time);
    }

    // one can only dream...
    fprintf(stderr, "\r\33[K");  // clear line
    fprintf(stderr, "Calculation complete.\n");
    mpz_mod(session->w, session->w, session->n);
    char* str_w = mpz_get_str(NULL, 10, session->w);
    if (str_w == NULL) {
        LOG(FATAL, "failed to convert w to decimal");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "w = %s\n", str_w);
    free(str_w);

    // clean up
    session_delete(session);
    if (sqlite3_close(db) != SQLITE_OK) {
        LOG(FATAL, "sqlite3_close: %s", sqlite3_errmsg(db));
        return -1;
    }
    return EXIT_SUCCESS;
}
