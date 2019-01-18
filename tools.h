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

#include <stdio.h>
#include <malloc.h>

typedef   signed char            int8;
typedef unsigned char           Uint8;
typedef   signed short int       int16;
typedef unsigned short int      Uint16;
typedef   signed long int        int32;
typedef unsigned long int       Uint32;
typedef   signed long long int 	 int64;
typedef unsigned long long int  Uint64;

typedef unsigned char Uchar;
typedef unsigned int  Uint;

typedef Uint8 bool8;

#define inrange(n,a,b) ((Uint)((n)-(a))<=(Uint)((b)-(a)))
#define inrangex(n,a,b) ((Uint)((n)-(a))<(Uint)((b)-(a)))

#define inrange64(n,a,b) ((Uint64)((n)-(a))<=(Uint64)((b)-(a)))
#define inrangex64(n,a,b) ((Uint64)((n)-(a))<(Uint64)((b)-(a)))

#define MAX_EXTEND(n) ((Uint64)((n)+1)-1)

template <size_t size>
char (*__strlength_helper(char const (&_String)[size]))[size];
#define strlength(_String) (sizeof(*__strlength_helper(_String))-1)

#ifdef _MSC_VER
#define UNREACHABLE_CODE __assume(0)
#else
#define UNREACHABLE_CODE
#endif

#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#elif __has_attribute(always_inline)
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

template <typename UINT_TYPE>
UINT_TYPE readNumericConstant(const char *&buf);

class ParsingError {};

template <typename TYPE>
inline void tellCompilerVariableIsntUninitialized(TYPE &n)
{
}

#ifdef _MSC_VER
#define GETC _getc_nolock  // using this results in a huge speed-up under MS Visual C++
#else
#define GETC getc
#endif

class LineGetter
{
    char *buf;
    size_t currentSize;
public:
    LineGetter(size_t initialSize) : currentSize(initialSize) // initialSize must be nonzero
    {
        buf = (char*)malloc(initialSize);
    }
    char *fgets(FILE *f)
    {
        int ch = GETC(f);
        if (ch == EOF)
            return NULL;
        size_t count = currentSize;
        char *pos = buf;
        for (;;)
        {
            if (ch == '\n' || ch == EOF)
            {
                *pos = '\0';
                return buf;
            }
            *pos++ = ch;
            ch = GETC(f);
            if (--count == 0)
            {
                count = currentSize;
                buf = (char*)realloc(buf, currentSize *= 2);
                pos = buf + count;
            }
        }
    }
};
