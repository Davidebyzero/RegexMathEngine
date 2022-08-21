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

template Uint   readNumericConstant<Uint  >(const char *&buf);
template Uint32 readNumericConstant<Uint32>(const char *&buf);
template Uint64 readNumericConstant<Uint64>(const char *&buf);

Uint intLength(Uint32 i)
{
	// DJE 2012.10.25: made this more efficient (by unrolled binary search) and fixed handling of negative numbers
	// (Note: log10 would be going in totally the wrong direction - that'd be much slower)
	// see also http://stackoverflow.com/questions/1068849/how-do-i-determine-the-number-of-decimal-digits-of-an-integer-in-c/13080148#13080148
	if              (i < 100000) {
		if          (i < 1000) {
			{{}} if (i < 10)         return 1;
			else if (i < 100)        return 2;
			else                     return 3;
		} else {
			{{}} if (i < 10000)      return 4;
			else                     return 5;
		}
	} else {
		if          (i < 10000000) {
			{{}} if (i < 1000000)    return 6;
			else                     return 7;
		} else {
			{{}} if (i < 100000000)  return 8;
			else if (i < 1000000000) return 9;
			else                     return 10;
		}
	}
}
Uint intLength(Uint64 i)
{
	// DJE 2012.10.25: made this more efficient (by unrolled binary search) and fixed handling of negative numbers
	// (Note: log10 would be going in totally the wrong direction - that'd be much slower)
	// see also http://stackoverflow.com/questions/1068849/how-do-i-determine-the-number-of-decimal-digits-of-an-integer-in-c/13080148#13080148
    if                  (i < 10000000000uLL ) {
	    if              (i < 100000uLL) {
		    if          (i < 1000uLL) {
			    {{}} if (i < 10uLL)         return 1;
			    else if (i < 100uLL)        return 2;
			    else                     return 3;
		    } else {
			    {{}} if (i < 10000uLL)      return 4;
			    else                     return 5;
		    }
	    } else {
		    if          (i < 10000000uLL) {
			    {{}} if (i < 1000000uLL)    return 6;
			    else                     return 7;
		    } else {
			    {{}} if (i < 100000000uLL)  return 8;
			    else if (i < 1000000000uLL) return 9;
			    else                     return 10;
		    }
	    }
    } else {
	    if              (i < 1000000000000000uLL) {
		    if          (i < 10000000000000uLL) {
			    {{}} if (i < 100000000000uLL)         return 11;
			    else if (i < 1000000000000uLL)        return 12;
			    else                                  return 13;
		    } else {
			    {{}} if (i < 100000000000000uLL)      return 14;
			    else                                  return 15;
		    }
	    } else {
		    if          (i < 100000000000000000uLL) {
			    {{}} if (i < 10000000000000000uLL)    return 16;
			    else                                  return 17;
		    } else {
			    {{}} if (i < 1000000000000000000uLL)  return 18;
			    else if (i < 10000000000000000000uLL) return 19;
			    else                                  return 20;
		    }
	    }
    }
}
