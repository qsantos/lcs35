#include <gmp.h>

void square(mpz_t x, long t, mpz_t n) {
    mpz_t tmp;
    mpz_init(tmp);
    mpz_setbit(tmp, t);
    mpz_powm(x, x, tmp, n);
}
