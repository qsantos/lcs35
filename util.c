#define _POSIX_C_SOURCE 200809L

#include "util.h" // source header

// libraries
#include <cpuid.h>

// C90
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int debug_level = FATAL;

extern int parse_debug_args(int* argc, char** argv) {
    int final_position_of_current_argument = 1;
    for (int i = 1; i < *argc; i += 1) {
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            debug_level = NONE;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            debug_level = ERR;
        } else if (strcmp(argv[i], "-vv") == 0) {
            debug_level = WARN;
        } else if (strcmp(argv[i], "-vvv") == 0) {
            debug_level = INFO;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_level = DEBUG;
        } else {
            argv[final_position_of_current_argument] = argv[i];
            final_position_of_current_argument += 1;
        }
    }
    argv[final_position_of_current_argument] = NULL;
    *argc = final_position_of_current_argument;
    return *argc;
}

#ifndef _GNU_SOURCE
extern int asprintf(char** strp, const char* fmt, ...) {
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

extern size_t get_brand_string(char output[static 49]) {
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

extern int compat_rename(const char* srcfile, const char* dstfile) {
    // ensure an atomic update using rename() on POSIX systems
    if (rename(srcfile, dstfile) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        LOG(WARN, "failed to replace '%s' by '%s' (%s)", dstfile,srcfile,
                 strerror(errno));
        return -1;
    }

    // on non-POSIX systems (i.e. Windows), `rename()` might not accept
    // to replace existing `oldpath` with `newpath`; we will have to
    // hope that `remove()` followed by `rename()` is sufficient; if a
    // crash happen in between, the user will have to rename the file
    // by themselves

    if (remove(dstfile) != 0) {
        LOG(WARN, "failed to remove '%s' for replacement (%s)",
                 dstfile, strerror(errno));
        return -1;
    }

    if (rename(srcfile, dstfile) != 0) {
        LOG(WARN, "failed to move '%s' to '%s' (%s)", srcfile, dstfile,
                 strerror(errno));
        return -1;
    }

    return 0;
}
