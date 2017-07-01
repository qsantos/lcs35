#define _POSIX_C_SOURCE 200809L

#include "time.h" // source header

// local includes
#include "util.h"

// C90
#include <errno.h>
#include <math.h>
#include <time.h>
#include <string.h>

// C99
#include <inttypes.h>

// POSIX 2008
#include <sys/time.h>

extern double real_clock(void) {
    /* Clock that measures wall-clock time */
    struct timeval now;
    if (gettimeofday(&now, NULL) != 0) {
        LOG(WARN, "could not read current time (%s)", strerror(errno));
        return NAN;
    }
    return (double) now.tv_sec + (double) now.tv_usec / 1e6;
}

extern int human_time_relative(char* s, size_t n, double secs) {
#define PRItime "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64
    /* Format into human-friendly relative time */
    if (!isfinite(secs)) {
        return snprintf(s, n, "%f", secs);
    }

    uint64_t seconds = (uint64_t) secs;
    if (seconds < 2) {
        return snprintf(s, n, "%.1f second", secs);
    }

    uint64_t minutes = seconds / 60;
    seconds %= 60;
    if (minutes < 1) {
        return snprintf(s, n, "%" PRIu64 " seconds", seconds);
    }

    uint64_t hours = minutes / 60;
    minutes %= 60;
    uint64_t days = hours / 24;
    hours %= 24;
    if (days < 1) {
        return snprintf(s, n, PRItime, hours, minutes, seconds);
    }
    if (days < 1) {
        return snprintf(s, n, "1 day " PRItime, hours, minutes, seconds);
    }

    uint64_t years = days / 365;
    days %= 365;
    if (years < 1) {
        return snprintf(s, n, "%" PRIu64 " days " PRItime, days, hours,
                        minutes, seconds);
    }
    if (years < 2) {
        return snprintf(s, n, "1 year %" PRIu64 " days", days);
    }

    return snprintf(s, n, "%" PRIu64 " years %" PRIu64 " days", years, days);
#undef PRItime
}

extern int human_time_absolute(char* s, size_t n, double secs) {
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

extern size_t human_time_both(char* s, size_t n, double secs) {
    size_t n_printed = 0;
    n_printed += (size_t) human_time_relative(s+n_printed, n-n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, " (");
    n_printed += (size_t) human_time_absolute(s+n_printed, n-n_printed, secs);
    n_printed += (size_t) snprintf(s + n_printed, n - n_printed, ")");
    return n_printed;
}
