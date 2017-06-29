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

    // initialize to default values
    struct session session = {
        .c = 2446683847,  // 32 bit prime
        .t = 79685186856218,
        .i = 0,
    };
    // 2046-bit RSA modulus
    mpz_init(session.n);
    mpz_init_set_str(session.n,
        "631446608307288889379935712613129233236329881833084137558899"
        "077270195712892488554730844605575320651361834662884894808866"
        "350036848039658817136198766052189726781016228055747539383830"
        "826175971321892666861177695452639157012069093997368008972127"
        "446466642331918780683055206795125307008202024124623398241073"
        "775370512734449416950118097524189066796385875485631980550727"
        "370990439711973361466670154390536015254337398252457931357531"
        "765364633198906465140213398526580034199190398219284471021246"
        "488745938885358207031808428902320971090703239693491996277899"
        "532332018406452247646396635593736700936921275809208629319872"
        "7008292431243681", 10
    );
    // base of computation
    mpz_init(session.w);
    mpz_init_set_ui(session.w, 2);

    if (resume(savefile, &session) < 0) {
        LOG(FATAL, "failed to resume session");
        exit(EXIT_FAILURE);
    }

    // We exploit a trick offered by Shamir to detect computation errors
    // c is a small prime number (e.g. 32 bits)
    // it is easy to comptue 2^(2^i) mod c using Euler's totient function
    // thus, we work moudulo n * c rather than n
    // at any point, we can then reduce w mod c and compare it to 2^(2^i) mod c
    // in the end, we can reduce w mod n to retrieve the actual result
    mpz_init(session.n_times_c);
    mpz_mul_ui(session.n_times_c, session.n, session.c);

    // initialize timer
    uint64_t prev_i = session.i;
    double prev_time = real_clock();
    show_progress(session.i, session.t, &prev_i, &prev_time);

    while (session.i < session.t) {
        uint64_t stepsize = min(session.t - session.i, 1ULL<<20);

        // w = w^(2^stepsize) mod (n*c);
        mpz_t tmp;
        mpz_init(tmp);
        mpz_setbit(tmp, stepsize);
        mpz_powm(session.w, session.w, tmp, session.n_times_c);
        mpz_clear(tmp);

        session.i += stepsize;

        // clear line in case errors are to be printed
        fprintf(stderr, "\r\33[K");

        if (check_consistency(&session) < 0) {
            LOG(FATAL, "an error happened during computation");
            exit(EXIT_FAILURE);
        }
        if (checkpoint(tmpfile, &session) < 0) {
            LOG(FATAL, "failed to create new checkpoint!");
            exit(EXIT_FAILURE);
        }
        if (compat_rename(tmpfile, savefile) < 0) {
            LOG(FATAL, "failed to replace old checkpoint!");
            exit(EXIT_FAILURE);
        }
        show_progress(session.i, session.t, &prev_i, &prev_time);
    }

    // one can only dream...
    fprintf(stderr, "\rCalculation complete.\n");
    mpz_mod(session.w, session.w, session.n);
    char* str_w = mpz_get_str(NULL, 10, session.w);
    if (str_w == NULL) {
        LOG(FATAL, "failed to convert w to decimal");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "w = %s\n", str_w);
    free(str_w);

    // clean up
    mpz_clear(session.n_times_c);
    mpz_clear(session.w);
    mpz_clear(session.n);
    free(tmpfile);
    return EXIT_SUCCESS;
}
