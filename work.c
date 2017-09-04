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

    struct session* session = session_new();

    // trying to resume from normal session file
    int ret = session_load(session, savefile);
    if (ret == 0) {
        LOG(DEBUG, "session file not found");
        // continue
    } else if (ret == 1) {
        LOG(DEBUG, "session file valid; resuming from it");
    } else {
        // this should really not happen!
        LOG(FATAL, "session file invalid");
        exit(EXIT_FAILURE);
    }

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

        if (session_save(session, savefile) != 0) {
            LOG(FATAL, "failed to update session file!");
            exit(EXIT_FAILURE);
        }

        show_progress(session->i, session->t, &prev_i, &prev_time);
    }

    // one can only dream...
    fprintf(stderr, "\rCalculation complete.\n");
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
    return EXIT_SUCCESS;
}
