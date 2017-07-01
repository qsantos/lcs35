#ifndef UTIL_H
#define UTIL_H

// C90
#include <stdio.h>

// C99
#include <stdint.h>

// fix: MinGW generates erroneous warning (in fpclassify(), isnan(), signbit())
#ifdef __MINGW32__
#undef __mingw_choose_expr  // set to __builtin_choose_expr in math.h
#define __mingw_choose_expr(C, E1, E2) ((C) ? E1 : E2)
#endif

enum log_level {
    NONE,
    FATAL,
    ERR,
    WARN,
    INFO,
    DEBUG,
};

extern int debug_level;

#define LOG(level, ...) do { \
        if (level <= debug_level) { \
            fprintf(stderr, #level " %s:%i: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

extern int parse_debug_args(int* argc, char** argv);

#ifndef _GNU_SOURCE
extern int asprintf(char** strp, const char* fmt, ...);
#endif

extern size_t get_brand_string(char output[static 49]);

/* rename()-compatible wrapper; atomicity guaranteed only on POSIX systems */
extern int compat_rename(const char* srcfile, const char* dstfile);

#endif
