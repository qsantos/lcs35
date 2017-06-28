#define _POSIX_C_SOURCE 200809L

// C90
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// C99
#include <stdint.h>
#include <inttypes.h>

// POSIX 2008
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>

// other
#include <gmp.h>
#include <cpuid.h>

// fix: MinGW generates erroneous warning (in fpclassify(), isnan(), signbit())
#ifdef __MINGW32__
#undef __mingw_choose_expr  // set to __builtin_choose_expr in math.h
#define __mingw_choose_expr(C, E1, E2) ((C) ? E1 : E2)
#endif

enum log_level {
    FATAL,
    ERR,
    WARN,
    INFO,
    DEBUG,
};
int debug_level = INFO;
#define LOG(level, ...) do { \
        if (level <= debug_level) { \
            fprintf(stderr, #level " %s:%i: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

// fsync() for Windows
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
int fsync(int fd) {
    HANDLE handle = (HANDLE) _get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    if (FlushFileBuffers(handle) == 0) {
        if (GetLastError() == ERROR_INVALID_HANDLE) {
            errno = EBADF;
        } else {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}
#endif

#ifndef _GNU_SOURCE
static int asprintf(char** strp, const char* fmt, ...) {
    va_list ap1, ap2;
    va_start(ap1, fmt);
    va_copy(ap2, ap1);

    size_t n_bytes = (size_t) vsnprintf(NULL, 0, fmt, ap1) + 1;
    va_end(ap1);

    *strp = malloc(n_bytes);
    if (*strp == NULL) {
        LOG(INFO, "could not allocate memory (%s)", strerror(errno));
        return -1;
    }

    int ret = vsnprintf(*strp, n_bytes, fmt, ap2);
    va_end(ap2);
    return ret;
}
#endif

size_t get_brand_string(char output[static 49]) {
    /* Extract CPU brand string from CPUID instruction */

    // check existence of feature
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    eax = (unsigned int) __get_cpuid_max(0x80000000, NULL);
    if (eax < 0x80000004) {
        output[0] = '\0';
        return 0;
    }

    // obtain brand string by multiple calls to CPUID
    char buffer[48];
    for (unsigned int i = 0; i < 3; i += 1) {
        __cpuid(0x80000002 + i, eax, ebx, ecx, edx);
        ((unsigned int*)buffer)[4*i+0] = eax;
        ((unsigned int*)buffer)[4*i+1] = ebx;
        ((unsigned int*)buffer)[4*i+2] = ecx;
        ((unsigned int*)buffer)[4*i+3] = edx;
    }

    // remove occasional leading spaces
    size_t n_leading_spaces = strspn(buffer, " ");
    size_t n_characters = strlen(buffer + n_leading_spaces);
    memcpy(output, buffer + n_leading_spaces, n_characters);
    output[n_characters] = '\0';
    return n_characters;
}

uint64_t min(uint64_t a, uint64_t b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

uint64_t powm(uint64_t b, uint64_t e, uint64_t m) {
    /* Compute b^e mod m */
    uint64_t r = 1;
    while (e) {
        if (e & 1) {
            r = (r*b) % m;
        }
        b = (b*b) % m;
        e >>= 1;
    }
    return r;
}

double real_clock(void) {
    /* Clock that measures wall-clock time */
    struct timeval now;
    if (gettimeofday(&now, NULL) != 0) {
        LOG(WARN, "could not read current time (%s)", strerror(errno));
        return NAN;
    }
    return (double) now.tv_sec + (double) now.tv_usec / 1e6;
}

size_t getnline(char* s, size_t n, FILE* f) {
    /* Read until end of line or n-1 next characters */
    size_t r = 0;
    while (r < n-1) {
        int c = fgetc(f);
        if (c < 0) {
            break;
        }
        s[r] = (char) c;
        r += 1;
        if (c == '\n') {
            break;
        }
    }
    s[r] = 0;
    return r;
}

int human_time_relative(char* s, size_t n, double secs) {
    /* Format into human-friendly relative time */
    if (!isfinite(secs)) {
        return snprintf(s, n, "%f", secs);
    }

    int seconds = (int) secs;
    if (seconds < 2) {
        return snprintf(s, n, "%.1f second", secs);
    }

    int minutes = seconds / 60;
    seconds %= 60;
    if (minutes < 1) {
        return snprintf(s, n, "%i seconds", seconds);
    }

    int hours = minutes / 60;
    minutes %= 60;
    int days = hours / 24;
    hours %= 24;
    if (days < 1) {
        return snprintf(s, n, "%02i:%02i:%02i", hours, minutes, seconds);
    }
    if (days < 1) {
        return snprintf(s, n, "1 day %02i:%02i:%02i", hours, minutes, seconds);
    }

    int years = days / 365;
    days %= 365;
    if (years < 1) {
        return snprintf(s, n, "%i days %02i:%02i:%02i", days, hours, minutes, seconds);
    }
    if (years < 2) {
        return snprintf(s, n, "1 year %i days", days);
    }

    return snprintf(s, n, "%i years %i days", years, days);
}

int human_time_absolute(char* s, size_t n, double secs) {
    /* Format into human friendly absolute time
     *
     * secs is the number of seconds in the future */
    if (!isfinite(secs)) {
        // snprintf() can only fail on an invalid format string
        return snprintf(s, n, "%f", secs);
    }

    time_t timestamp = time(NULL);
    timestamp += (time_t) secs;
    struct tm* tm = localtime(&timestamp);
    if (secs < 86400.) {
        return (int) strftime(s, n, "%Y-%m-%d %H:%M:%S", tm);
    } else {
        return (int) strftime(s, n, "%Y-%m-%d", tm);
    }
}

size_t human_time_both(char* s, size_t n, double secs) {
    size_t n_printed = 0;
    n_printed += (size_t) human_time_relative(s + n_printed, n - n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, " (");
    n_printed += (size_t) human_time_absolute(s + n_printed, n - n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, ")");
    return n_printed;
}

struct session {
    uint64_t t;  // target exponent
    uint64_t i;  // current exponent
    uint64_t c;  // control modulus
    mpz_t n;  // modulus (product of two primes)
    mpz_t w;  // computed power of 2
    mpz_t n_times_c;  // pre-computed value of n*c for convenience
};

int check_consistency(const struct session* session) {
    /* Consistency check of w using prime factor c of n */
    // because c is prime:
    // 2^(2^i) mod c = 2^(2^i mod phi(c)) = 2^(2^i mod (c-1))
    uint64_t reduced_e = powm(2, session->i, session->c-1);  // 2^i mod phi(c)
    uint64_t check = powm(2, reduced_e, session->c);  // 2^(2^i) mod c
    uint64_t w_mod_c = mpz_fdiv_ui(session->w, session->c);  // w mod c
    if (w_mod_c != check) {
        LOG(WARN, "inconsistency detected (%"PRIu64" != %"PRIu64")", check, w_mod_c);
        return -1;
    }
    return 0;
}

int resume(const char* savefile, struct session* session) {
    /* Resume progress from savefile
     *
     * returns 1 if session was resumed
     * returns 0 if no session was found
     * returns -1 if an error was encountered */

    // first, does the file exists?
    FILE* f = fopen(savefile, "r");
    if (f == NULL) {
        if (errno != ENOENT) {
            // savefile may exist but another error stops us from reading it
            LOG(WARN, "could not open '%s' for reading (%s)", savefile, strerror(errno));
            return -1;
        }
        // savefile does not exist, nothing to resume
        return 0;
    }

    // second, is it a regular file?
    // this is required so that rename() can work properly
    struct stat fileinfo;
    int ret = fstat(fileno(f), &fileinfo);
    if (ret < 0) {
        LOG(WARN, "could not stat '%s' (%s)", savefile, strerror(errno));
        return -1;
    }
    if (!S_ISREG(fileinfo.st_mode)) {
        LOG(WARN, "'%s' is not a regular file", savefile);
        return -1;
    }

    // savefile exist and is a regular file; we may use it to resume the
    // previous session and to save checkpoints

    // each line contains one paramater in ASCII decimal representation
    // in order: t, i, c, n, w

    // t
    if (fscanf(f, "%"SCNu64"\n", &session->t) < 0) {
        if (errno == 0) {
            LOG(WARN, "unexpected end of file while reading t");
        } else {
            LOG(WARN, "failed to scan t (%s)", strerror(errno));
        }
        return -1;
    }

    // i
    if (fscanf(f, "%"SCNu64"\n", &session->i) < 0) {
        if (errno == 0) {
            LOG(WARN, "unexpected end of file while reading i");
        } else {
            LOG(WARN, "failed to scan i (%s)", strerror(errno));
        }
        return -1;
    }

    // c
    if (fscanf(f, "%"SCNu64"\n", &session->c) < 0) {
        if (errno == 0) {
            LOG(WARN, "unexpected end of file while reading c");
        } else {
            LOG(WARN, "failed to scan c (%s)", strerror(errno));
        }
        return -1;
    }

    char line[1024];

    // n
    getnline(line, sizeof(line), f);
    if (mpz_set_str(session->n, line, 10) < 0) {
        LOG(WARN, "invalid decimal number n = %s", line);
        return -1;
    }

    // w
    getnline(line, sizeof(line), f);
    if (mpz_set_str(session->w, line, 10) < 0) {
        LOG(WARN, "invalid decimal number w = %s", line);
        return -1;
    }

    if (fclose(f) < 0) {
        LOG(WARN, "failed to close '%s' (%s)", savefile, strerror(errno));
        return -1;
    }

    // finally, does the data look good?
    if (check_consistency(session) < 0) {
        LOG(WARN, "data from the '%s' looks incorrect", savefile);
        return -1;
    }

    // successfully resumed
    return 1;
}

int checkpoint(const char* savefile, const char* tmpfile,
               const struct session* session) {
    /* Save progress into savefile */

    // write in temporary file for atomic updates using rename()
    // this require both files to be on the same filesystem
    FILE* f = fopen(tmpfile, "wb");
    if (f == NULL) {
        LOG(WARN, "could not open '%s' for writing (%s)", tmpfile, strerror(errno));
        return -1;
    }

    // each line contains one paramater in ASCII decimal representation
    // in order: t, i, c, n, w

    // t
    if (fprintf(f, "%"PRIu64"\n", session->t) < 0) {
        LOG(WARN, "could not write t to temporary file (%s)", strerror(errno));
        return -1;
    }

    // i
    if (fprintf(f, "%"PRIu64"\n", session->i) < 0) {
        LOG(WARN, "could not write i to temporary file (%s)", strerror(errno));
        return -1;
    }

    // c
    if (fprintf(f, "%"PRIu64"\n", session->c) < 0) {
        LOG(WARN, "could not write c to temporary file (%s)", strerror(errno));
        return -1;
    }

    // n
    char* str_n = mpz_get_str(NULL, 10, session->n);
    if (str_n == NULL) {
        LOG(WARN, "failed to convert n to decimal");
        return -1;
    }
    if (fprintf(f, "%s\n", str_n) < 0) {
        LOG(WARN, "failed to write n to temporary file (%s)", strerror(errno));
        return -1;
    }
    free(str_n);

    // w
    char* str_w = mpz_get_str(NULL, 10, session->w);
    if (str_w == NULL) {
        LOG(WARN, "failed to convert w to decimal");
        return -1;
    }
    if (fprintf(f, "%s\n", str_w) < 0) {
        LOG(WARN, "failed to write w to temporary file (%s)", strerror(errno));
        return -1;
    }
    free(str_w);

    // actually write to disk before replacing previous files
    fflush(f);  // flush user-space buffers
    fsync(fileno(f));  // flush kernel buffers and disk cache

    if (fclose(f) < 0) {
        LOG(WARN, "failed to close '%s' (%s)", tmpfile, strerror(errno));
        return -1;
    }

    // ensure an atomic update using rename() on POSIX systems
    if (rename(tmpfile, savefile) < 0) {
        if (errno == EEXIST) {
            // on non-POSIX systems (i.e. Windows), `rename()` might not accept
            // to replace existing `oldpath` with `newpath`; we will have to
            // hope that `remove()` followed by `rename()` is sufficient; if a
            // crash happen in between, the user will have to rename the file
            // by themselves
            if (remove(savefile) < 0) {
                LOG(WARN, "failed to remove '%s' for replacement (%s)",
                         savefile, strerror(errno));
                return -1;
            }
            if (rename(tmpfile, savefile) < 0) {
                LOG(WARN, "failed to move '%s' to '%s' (%s)", tmpfile, savefile,
                         strerror(errno));
                return -1;
            }
        } else {
            LOG(WARN, "failed to replace '%s' by '%s' (%s)", savefile,tmpfile,
                     strerror(errno));
            return -1;
        }
    }

    return 0;
}

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
        if (checkpoint(savefile, tmpfile, &session) < 0) {
            LOG(FATAL, "failed to save checkpoint!");
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
