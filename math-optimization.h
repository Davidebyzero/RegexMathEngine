extern void init_isPrime();
extern int isPrime(Uint64 n);

#ifdef USE_GMP

#include <gmp.h>

inline void mpz_set_uint64(mpz_t dest, Uint64 src)
{
#if GMP_LIMB_BITS == 64
	dest->_mp_d[0] = src;
#elif 0
    mpz_import(dest, 1, -1, sizeof(Uint64), 0, 0, &src);
#else
	/* mpz_import is terribly slow */
	mpz_set_ui(dest, (uint32)(src >> 32));
	mpz_mul_2exp(dest, dest, 32);
	mpz_add_ui(dest, dest, (uint32)src);
#endif
}

#endif
