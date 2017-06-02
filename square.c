#include <gmp.h>

void square(mpz_t x, long t, mpz_t n) {
    for (long i = 0; i < t; i += 1) {
        mpz_mul(x, x, x);
        mpz_mod(x, x, n);
    }
}
