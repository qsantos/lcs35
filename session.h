#ifndef SESSION_H
#define SESSION_H

// libraries
#include <gmp.h>

// C99
#include <stdint.h>

struct session {
    uint64_t t;  // target exponent
    uint64_t i;  // current exponent
    uint64_t c;  // control modulus
    mpz_t n;  // modulus (product of two primes)
    mpz_t w;  // computed power of 2
    mpz_t n_times_c;  // pre-computed value of n*c for convenience
    int n_validations;  // number of validations from intermediate results
    const char* metadata;
};

extern struct session* session_new(void);
extern struct session* session_copy(const struct session* session);
extern void session_delete(struct session* session);

extern int session_check(const struct session* session);
extern int session_load(struct session* session, const char* filename);
extern int session_save(const struct session* session, const char* filename);

/* test whether the sessions have the same base parameters */
extern int session_iscompat(const struct session* session1,
                            const struct session* session2);

/* test whether the sessions have the same base parameters are that the current
 * exponent on `after` is greater than that of `before` */
extern int session_isafter(const struct session* before,
                           const struct session* after);

extern uint64_t session_work(struct session* session, uint64_t amount);

#endif
