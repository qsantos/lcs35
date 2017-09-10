#define _POSIX_C_SOURCE 200809L

// local includes
#include "util.h"
#include "time.h"
#include "session.h"
#include "socket.h"

// POSIX
#include <unistd.h>

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

static int get_work(const char* host, const char* port, struct session* session) {
    int server = tcp_connect(host, port);
    if (server < 0) {
        LOG(WARN, "failed to connect to %s:%s", host, port);
        return -1;
    }
    ssize_t n = write(server, "resume:", 7);
    if (n < 0) {
        LOG(WARN, "failed to send command to supervisor");
        return -1;
    }
    char buffer[1024];
    n = read(server, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        LOG(WARN, "failed to obtain response from supervisor");
        return -1;
    }
    buffer[n] = 0;
    LOG(DEBUG, "buffer: <%s>", buffer);
    if (close(server) < 0) {
        LOG(WARN, "failed to close connection to supervisor");
        return -1;
    }

    char* str_w;
    uint64_t i = strtoul(buffer, &str_w, 0);
    session->i = i;
    if (*str_w != ':') {
        LOG(WARN, "missing colon after i in data");
        return -1;
    }
    str_w += 1;
    if (mpz_set_str(session->w, str_w, 0) < 0) {
        LOG(WARN, "missing to parse w");
        return -1;
    }
    if (session_check(session) != 0) {
        LOG(WARN, "inconsistent input from supervisor");
        return -1;
    }

    return 0;
}

static int save_work(const char* host, const char* port, struct session* session) {
    if (session_check(session) != 0) {
        LOG(WARN, "inconsistency detected");
        return -1;
    }

    // prepare message
    char* str_w = mpz_get_str(NULL, 10, session->w);
    if (str_w == NULL) {
        LOG(WARN, "failed to convert w to decimal");
        return -1;
    }
    char buffer[1024];
    ssize_t n = snprintf(buffer, sizeof(buffer), "save:%#"PRIx64":%s", session->i, str_w);
    if (n < 0) {
        LOG(WARN, "failed to prepare message");
        free(str_w);
        return -1;
    }
    free(str_w);

    // send to supervisor
    int server = tcp_connect(host, port);
    if (server < 0) {
        LOG(WARN, "failed to connect to %s:%s", host, port);
        return -1;
    }
    n = write(server, buffer, (size_t) n);
    if (n < 0) {
        LOG(WARN, "failed to send command to supervisor");
        return -1;
    }
    if (close(server) < 0) {
        LOG(WARN, "failed to close connection to supervisor");
        return -1;
    }

    return 0;
}

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
const char* supervisor_host;
const char* supervisor_port;
struct session* session = NULL;
void handle_sigint(int sig){
    if (sig != SIGINT) {
        return;
    }
    fprintf(stderr, "\r\33[K");  // clear line
    if (save_work(supervisor_host, supervisor_port, session) < 0) {
        LOG(FATAL, "failed to save work on supervisor");
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
    if (argc != 3) {
        fprintf(stderr, "Usage: %s supervisor-ip port", argv[0]);
        exit(EXIT_FAILURE);
    }
    supervisor_host = argv[1];
    supervisor_port = argv[2];

    // display brand string
    char brand_string[49];
    get_brand_string(brand_string);
    printf("%s\n", brand_string);

    session = session_new();
    if (get_work(supervisor_host, supervisor_port, session) < 0) {
        LOG(FATAL, "failed to get work from supervisor");
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
            if (save_work(supervisor_host, supervisor_port, session) < 0) {
                LOG(FATAL, "failed to save work on supervisor");
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
    return EXIT_SUCCESS;
}
