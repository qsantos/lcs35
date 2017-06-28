#define _POSIX_C_SOURCE 200809L

#include "session.h" // source header

// local includes
#include "util.h"

// POSIX 2008
#include <sys/stat.h>
#include <unistd.h>

// C99
#include <inttypes.h>

// C90
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// fsync() for Windows
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
static int fsync(int fd) {
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

static uint64_t powm(uint64_t b, uint64_t e, uint64_t m) {
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

static size_t getnline(char* s, size_t n, FILE* f) {
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

extern int check_consistency(const struct session* session) {
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

extern int resume(const char* savefile, struct session* session) {
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

extern int checkpoint(const char* savefile, const char* tmpfile,
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

