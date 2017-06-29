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

void show_progress(uint64_t i, uint64_t t, uint64_t* prev_i, double* prev_time) {
    /* Show progress */

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
    fprintf(stderr, "%9.6f%% (%#.12"PRIx64" / %#.12"PRIx64") ETA: %s", progress, i, t, human_time);

    // update timer
    *prev_i = i;
    *prev_time = now;
}

void usage(const char* name) {
    fprintf(stderr, "Usage: %s savefile\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    if (setlocale(LC_ALL, "") == NULL) {
        LOG(WARN, "failed to set locale (%s)", strerror(errno));
    }

    // parse arguments
    const char* savefile = "savefile";
    if (argc == 2) {
        savefile = argv[1];
    }

    // prepare name of intermediate file used for atomic updates
    char* tmpfile;
    int ret = asprintf(&tmpfile, "%s.new", savefile);
    if (ret < 0) {
        LOG(FATAL, "could not prepare name for intermediate file");
        exit(EXIT_FAILURE);
    }

    // display brand string
    char brand_string[49];
    get_brand_string(brand_string);
    printf("%s\n", brand_string);

    // start or resume session
    struct session* session = session_new();
    if (session_load(session, savefile) < 0) {
        LOG(FATAL, "failed to resume session");
        exit(EXIT_FAILURE);
    }

    // initialize timer
    uint64_t prev_i = session->i;
    double prev_time = real_clock();
    show_progress(session->i, session->t, &prev_i, &prev_time);

    while (session->i < session->t) {
        uint64_t stepsize = min(session->t - session->i, 1ULL<<20);

        // w = w^(2^stepsize) mod (n*c);
        mpz_t tmp;
        mpz_init(tmp);
        mpz_setbit(tmp, stepsize);
        mpz_powm(session->w, session->w, tmp, session->n_times_c);
        mpz_clear(tmp);

        session->i += stepsize;

        // clear line in case errors are to be printed
        fprintf(stderr, "\r\33[K");

        if (session_check(session) < 0) {
            LOG(FATAL, "an error happened during computation");
            exit(EXIT_FAILURE);
        }
        if (session_save(session, tmpfile) < 0) {
            LOG(FATAL, "failed to create new checkpoint!");
            exit(EXIT_FAILURE);
        }
        if (compat_rename(tmpfile, savefile) < 0) {
            LOG(FATAL, "failed to replace old checkpoint!");
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
    free(tmpfile);
    return EXIT_SUCCESS;
}
