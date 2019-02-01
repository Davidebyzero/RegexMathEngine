/**
 * License:
 *   This Source Code Form is subject to the terms of
 *   the Mozilla Public License, v. 2.0. If a copy of
 *   the MPL was not distributed with this file, You
 *   can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 *   David Ellsworth <davide.by.zero@gmail.com>
 */

#include "regex.h"
#include "math-optimization.h"

#ifdef USE_GMP

#pragma comment(lib, "libgmp-10.lib")

static int isPrime_is_initialized = false;
static mpz_t mpzN;

void init_isPrime()
{
    if (isPrime_is_initialized)
        return;
    mpz_init(mpzN);
#if GMP_LIMB_BITS == 64
    mpzN->_mp_size = 1;
#endif
    isPrime_is_initialized = true;
}

int isPrime(Uint64 n)
{
    mpz_set_uint64(mpzN, n);
    return mpz_probab_prime_p(mpzN, 12); // 12 is enough for < 2^64, according to http://www.trnicely.net/misc/mpzspsp.html and to Jiang and Deng (2014) (doi:10.1090/S0025-5718-2014-02830-5)
}

#else

#include <math.h>

void init_isPrime()
{
}

// from https://stackoverflow.com/a/31758727/161468
int isPrime(Uint64 n)
{
    if (n<=3 || n==5)
        return n>1;

    if (n%2==0 || n%3==0 || n%5==0)
        return false;

    if (n<=30)
        return n==7 || n==11 || n==13 || n==17 || n==19 || n==23 || n==29;

    Uint64 sq = (Uint64)ceil(sqrt((double)n));
    for (Uint64 i=7; i<=sq; i+=30)
        if (n%(i   )==0 || n%(i+ 4)==0 || n%(i+ 6)==0 || n%(i+10)==0 ||
            n%(i+12)==0 || n%(i+16)==0 || n%(i+22)==0 || n%(i+24)==0)
            return false;

    return true;
}

#endif
