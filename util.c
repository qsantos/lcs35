#define _POSIX_C_SOURCE 200809L

#include "util.h" // source header

// libraries
#include <cpuid.h>

// C90
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

int debug_level = INFO;

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
