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

#include "tools.h"

template <typename UINT_TYPE>
UINT_TYPE readNumericConstant(const char *&buf)
{
    const UINT_TYPE MAX_UINT = (UINT_TYPE)-1;
    UINT_TYPE n = *buf++ - '0';
    while (inrange(*buf, '0', '9'))
    {
        UINT_TYPE c = *buf - '0';
        // I really wish there were a portable way in C/C++ to detect an overflow such that it gets compiled into
        // the equivalent of tight assembly language code, rather than ad-hoc overflow detection like this
        if (n >= MAX_UINT/10 && (n > MAX_UINT/10 || c > MAX_UINT%10))
            throw ParsingError();
        n = n * 10 + c;
        buf++;
    }
    return n;
}

template Uint32 readNumericConstant<Uint32>(const char *&buf);
template Uint64 readNumericConstant<Uint64>(const char *&buf);
