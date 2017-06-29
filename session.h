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
};

extern int session_check(const struct session* session);
extern int session_load(struct session* session, const char* filename);
extern int session_save(const struct session* session, const char* filename);

#endif
