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

extern struct session* session_new(void) {
    // allocate memory
    struct session* session = malloc(sizeof(*session));
    if (session == NULL) {
        return NULL;
    }

    // initialize to default values
    mpz_init_set_str(session->c, "2446683847", 10);  // 32 bit prime
    session->t = 79685186856218;
    session->i = 0;
    // 2046-bit RSA modulus
    mpz_init(session->n);
    mpz_init_set_str(session->n,
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
    mpz_init(session->w);
    mpz_init_set_ui(session->w, 2);

    // We exploit a trick offered by Shamir to detect computation errors
    // c is a small prime number (e.g. 32 bits)
    // it is easy to comptue 2^(2^i) mod c using Euler's totient function
    // thus, we work moudulo n * c rather than n
    // at any point, we can then reduce w mod c and compare it to 2^(2^i) mod c
    // in the end, we can reduce w mod n to retrieve the actual result
    mpz_init(session->n_times_c);
    mpz_mul(session->n_times_c, session->n, session->c);

    session->n_validations = 0;
    return session;
}

extern struct session* session_copy(const struct session* session) {
    // allocate memory
    struct session* ret = malloc(sizeof(*session));
    if (ret == NULL) {
        return NULL;
    }

    // copy information
    ret->t = session->t;
    ret->i = session->i;
    mpz_init_set(ret->c, session->c);
    mpz_init_set(ret->n, session->n);
    mpz_init_set(ret->w, session->w);
    mpz_init_set(ret->n_times_c, session->n_times_c);
    ret->n_validations = session->n_validations;
    ret->metadata = session->metadata;

    return ret;
}

extern void session_delete(struct session* session) {
    mpz_clear(session->n_times_c);
    mpz_clear(session->w);
    mpz_clear(session->n);
    mpz_clear(session->c);
    free(session);
}

static void mpz_powm_u64(mpz_t rop, const mpz_t base, uint64_t exp,
                         const mpz_t mod) {
    /* Same as mpz_powm_ui except that exp is of type uint64_t */

    // GMP excepts unsigned long, which might be as small as 32 bits; we pass
    // exp it directly if possible; otherwise, we decompose it in two unsigned
    // long
#if ULONG_MAX >= 18446744073709551615u  // can we pass exp directly to GMP?
    mpz_powm_ui(rop, base, (unsigned long) exp, mod);
#else
    // exp = high32 * 2**32 + low32
    // note that unsigned long might still be more than 32 bits
    unsigned long low32 = (unsigned long) (exp & 0xffffffff);
    unsigned long high32 = (unsigned long) (exp >> 32);

    // rop = base**exp
    //     = base**(high32 * 2**32 + low32)
    //     = ((base**high32)**(2**16))**(2**16) * base**low32
    // (modulo mod)

    // rop might alias base or mod so we do not touch it except at the end
    mpz_t tmp1, tmp2;
    mpz_init(tmp1);
    mpz_init(tmp2);

    // tmp1 = ((base**high32)**(2**16))**(2**16)
    mpz_powm_ui(tmp1, base, high32, mod);
    mpz_powm_ui(tmp1, tmp1, 1ul<<16, mod);
    mpz_powm_ui(tmp1, tmp1, 1ul<<16, mod);

    // tmp2 = base**low32
    mpz_powm_ui(tmp2, base, low32, mod);

    // rop = tmp1 * tmp2
    mpz_mul(tmp1, tmp1, tmp2);
    mpz_mod(rop, tmp1, mod);

    mpz_clear(tmp2);
    mpz_clear(tmp1);
#endif
}

extern int session_check(const struct session* session) {
    /* Consistency check of w using prime factor c of n */
    // because c is prime:
    // 2^(2^i) mod c = 2^(2^i mod phi(c)) = 2^(2^i mod (c-1))

    mpz_t two;
    mpz_init_set_ui(two, 2);

    // quick way: use the fact that c is prime to compute 2^(2^i) mod c
    mpz_t quick_way;
    mpz_init(quick_way);
    mpz_sub_ui(quick_way, session->c, 1);  // phi(c) = c-1 because c is prime
    mpz_powm_u64(quick_way, two, session->i, quick_way);  // 2^i mod phi(c)
    mpz_powm(quick_way, two, quick_way, session->c);  // 2^(2^i) mod c

    // slow way: use the fact that (w mod (n*c)) mod c = w mod c
    mpz_t slow_way;
    mpz_init(slow_way);
    mpz_fdiv_r(slow_way, session->w, session->c);  // w mod c

    int cmp = mpz_cmp(quick_way, slow_way);
    mpz_clear(slow_way);
    mpz_clear(quick_way);
    mpz_clear(two);

    if (cmp != 0) {
        LOG(WARN, "inconsistency detected");
        return -1;
    }
    return 0;
}

extern int session_load(struct session* session, const char* filename) {
    /* Resume progress from file
     *
     * returns 1 if session was resumed
     * returns 0 if no session was found
     * returns -1 if an error was encountered */

    // first, does the file exists?
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        if (errno != ENOENT) {
            // file may exist but another error stops us from reading it
            LOG(WARN, "could not open '%s' for reading (%s)", filename,
                strerror(errno));
            return -1;
        }
        // file does not exist, nothing to resume
        return 0;
    }

    // second, is it a regular file?
    // this is required so that rename() can work properly
    struct stat fileinfo;
    int ret = fstat(fileno(f), &fileinfo);
    if (ret != 0) {
        LOG(WARN, "could not stat '%s' (%s)", filename, strerror(errno));
        return -1;
    }
    if (!S_ISREG(fileinfo.st_mode)) {
        LOG(WARN, "'%s' is not a regular file", filename);
        return -1;
    }

    // file exist and is a regular file; we may use it to resume the
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

    char line[1024];

    // c
    getnline(line, sizeof(line), f);
    if (mpz_set_str(session->c, line, 10) < 0) {
        LOG(WARN, "invalid decimal number c = %s", line);
        return -1;
    }

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

    // n_validations
    if (fscanf(f, "%d\n", &session->n_validations) < 0) {
        if (errno == 0) {
            // backwards-compatibility for session files without n_validations
            session->n_validations = 0;
        } else {
            LOG(WARN, "failed to scan n_validations (%s)", strerror(errno));
            return -1;
        }
    }

    if (fclose(f) != 0) {
        LOG(WARN, "failed to close '%s' (%s)", filename, strerror(errno));
        return -1;
    }

    // finally, does the data look good?
    if (session_check(session) != 0) {
        LOG(WARN, "data from '%s' looks incorrect", filename);
        return -1;
    }

    // update n times c
    mpz_mul(session->n_times_c, session->n, session->c);

    // successfully resumed
    return 1;
}

extern int session_save(const struct session* session, const char* filename) {
    /* Save progress into file */

    // write in temporary file for atomic updates using rename()
    // this require both files to be on the same filesystem
    FILE* f = fopen(filename, "wb");
    if (f == NULL) {
        LOG(WARN, "could not open '%s' for writing (%s)", filename,
            strerror(errno));
        return -1;
    }

    // each line contains one paramater in ASCII decimal representation
    // in order: t, i, c, n, w

    // t
    if (fprintf(f, "%" PRIu64 "\n", session->t) < 0) {
        LOG(WARN, "could not write t to temporary file (%s)", strerror(errno));
        return -1;
    }

    // i
    if (fprintf(f, "%" PRIu64 "\n", session->i) < 0) {
        LOG(WARN, "could not write i to temporary file (%s)", strerror(errno));
        return -1;
    }

    // c
    char* str_c = mpz_get_str(NULL, 10, session->c);
    if (str_c == NULL) {
        LOG(WARN, "failed to convert c to decimal");
        return -1;
    }
    if (fprintf(f, "%s\n", str_c) < 0) {
        LOG(WARN, "failed to write c to temporary file (%s)", strerror(errno));
        return -1;
    }
    free(str_c);

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

    // n_validations
    if (fprintf(f, "%d\n", session->n_validations) < 0) {
        LOG(WARN, "could not write n_validations to temporary file (%s)",
            strerror(errno));
        return -1;
    }

    // actually write to disk before replacing previous files
    fflush(f);  // flush user-space buffers
    fsync(fileno(f));  // flush kernel buffers and disk cache

    if (fclose(f) != 0) {
        LOG(WARN, "failed to close '%s' (%s)", filename, strerror(errno));
        return -1;
    }

    return 0;
}

extern int session_iscompat(const struct session* session1,
                            const struct session* session2) {
    if (mpz_cmp(session1->c, session2->c) != 0) {
        return 0;
    }

    if (mpz_cmp(session1->n, session2->n) != 0) {
        return 0;
    }

    return 1;
}

extern int session_isafter(const struct session* before,
                           const struct session* after) {
    return session_iscompat(before, after) && before->i <= after->i;
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern uint64_t session_work(struct session* session, uint64_t amount) {
    if (amount > ULONG_MAX) {
        LOG(FATAL, "unsupported increment %" PRIu64 " (max: %lu)", amount,
            ULONG_MAX);
        exit(EXIT_FAILURE);
    }

    amount = MIN(amount, session->t - session->i);

    // w = w^(2^amount) mod (n*c);
    mpz_t tmp;
    mpz_init(tmp);
    mpz_setbit(tmp, (unsigned long int) amount);
    mpz_powm(session->w, session->w, tmp, session->n_times_c);
    mpz_clear(tmp);

    session->i += amount;
    return amount;
}
