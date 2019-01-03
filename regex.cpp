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

#include "regex.h"
#include "parser.h"
#include "matcher.h"

class Regex
{
    RegexGroup regex;
    Uint numCaptureGroups;
    Uint maxGroupDepth;
public:
    Regex(const char *buf);
    bool MatchNumber(Uint64 input, char basicChar, Uint64 &returnMatch);
    bool MatchString(const char *stringToMatchAgainst, const char *&returnMatch, size_t &returnMatchLength);
};

Regex::Regex(const char *buf) : regex(RegexGroup_NonCapturing)
{
    regex.type = RegexGroup_NonCapturing;
    regex.minCount = 1;
    regex.maxCount = 1;
    regex.lazy = 0;

    RegexParser parser(regex, buf);
    numCaptureGroups = parser.backrefIndex;
    maxGroupDepth    = parser.maxGroupDepth;
}

bool Regex::MatchNumber(Uint64 input, char basicChar, Uint64 &returnMatch)
{
    RegexMatcher<false> match;
    match.basicChar = basicChar;
    Uint64 returnMatchOffset;
    return match.Match(regex, numCaptureGroups, maxGroupDepth, input, returnMatchOffset, returnMatch);
}

bool Regex::MatchString(const char *stringToMatchAgainst, const char *&returnMatch, size_t &returnMatchLength)
{
    RegexMatcher<true> match;
    bool result = match.Match(regex, numCaptureGroups, maxGroupDepth, (Uint64)stringToMatchAgainst, (Uint64 &)returnMatch, (Uint64 &)returnMatchLength);
    (const char *&)returnMatch = stringToMatchAgainst + (size_t)(Uint64 &)returnMatch;
    return result;
}

//#define TEST_NUMBERS_FIBONACCI
#define TEST_FOR_FALSE_POSITIVES

// todo: implement these as class members rather than global variables?
Uint debugTrace = 0;
bool free_spacing_mode = true;
bool emulate_ECMA_NPCGs = true;
bool allow_empty_character_classes = true;
bool allow_molecular_lookahead = false;
bool allow_atomic_groups = false;
bool allow_reset_start = false;
Uint optimizationLevel = 2;

static void printShortUsage(const char *argv0)
{
    fprintf(stderr, "Use the \"--help\" option to see full information on command-line options.\n", argv0);
}

static void printUsage(const char *argv0)
{
    fprintf(stderr, "\
Usage: \"%s\" [OPTION]... [PATTERN]\n\
\n\
Options:\n\
  -f, --file=FILE     Read pattern from file with filename FILE\n\
  --line-buffered     Flush output after each line is printed\n\
  -n, --num=CHAR      Enable numerical mode, which operates on numbers instead\n\
                      of strings; abstractly, a number N represents a string of\n\
                      N identical characters. The parameter CHAR defines which\n\
                      repeated character is to be used. By convention this is\n\
                      usually \"x\", but it is configurable.\n\
  --fs{-|+}           Disables or enables free-spacing mode. In this mode, all\n\
                      whitespace will be ignored unless it occurs inside a\n\
                      parsable unit. \"-\" disables this and \"+\" enables it.\n\
                      By default it is enabled.\n\
  --npcg{-|+}         Specifies the behavior of non-participating capture\n\
                      groups. \"-\" makes them match nothing (as in most regex\n\
                      engines), and \"+\" makes them match an empty string (as\n\
                      in ECMAScript). The default is \"+\".\n\
  --ecc{-|+}          Specifies whether an empty character class, i.e. \"[]\",\n\
                      or \"[^]\", is permitted. \"-\" makes it an error to attempt\n\
                      to use them (as in most regex engines), and \"+\" allows\n\
                      them (as in ECMAScript). The default is \"+\".\n\
  -x EXT,EXT,...      Enable extensions. Currently available extensions are:\n\
                      ml   Molecular (non-atomic) lookahead: (?*...)\n\
                      ag   Atomic Grouping: (?>...)\n\
                      rs   Reset Start: \\K\n\
                      all  Enable all of the above extensions\n\
  -o                  Show only the part of the line that matched\n\
  -O NUMBER           Specifies the optimization level, from 0 to 2. This\n\
                      controls whether optimizations are enabled which skip\n\
                      unnecessary backtracking. The default is the maximum, 2.\n\
                      Currently, -O1 enables simple end-anchor and subtraction\n\
                      optimizations, and -O2 enables Power of 2 optimizations.\n\
  -t NUM0[..NUM1]     (In numerical mode only) Test the range of numbers from\n\
                      NUM0 to NUM1, inclusive. If NUM1 is not specified, only\n\
                      one number, NUM0, shall be tested.\n\
  --trace             Enable printout of debug trace. Use this parameter twice\n\
                      to include a dump of the backtracking stack at every step\n\
                      (which is extremely verbose).\n\
  --verbose           Print both matches and non-matches along with the input\n\
                      number. Currently works only in numerical mode when\n\
                      taking input from standard input.\n\
", argv0);
}

static int loadPatternFile(char *&buf, const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        fprintf(stderr, "Error opening pattern file \"%s\"\n", filename);
        return -1;
    }
    setvbuf(f, NULL, _IONBF, 0);
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    buf = new char [size + 1];
    fseek(f, 0, SEEK_SET);
    if (fread(buf, 1, size, f) != size)
    {
        fprintf(stderr, "Error reading pattern file \"%s\"\n", filename);
        return -1;
    }
    buf[size] = '\0';
    fclose(f);
    return 0;
}

static void errorMoreThanOnePattern(const char *argv0)
{
    fprintf(stderr, "Error: In this version, only one pattern may be specified\n");
    printShortUsage(argv0);
}

int main(int argc, char *argv[])
{
    // crudely implemented getopt-command-line interface; probably replace it with getopt later
    char *buf = NULL;
    char mathMode = '\0'; // if nonzero, enables math mode and specifies what character to use
    bool verbose = false;
    bool lineBuffered = false;
    bool showMatch = false;
    bool optionsDone = false;
    Uint64 testNum0, testNum1;
    Uint testNum_digits;
    int64 testNumInc = 0;
    for (int i=1; i<argc; i++)
    {
        if (argv[i][0]=='-')
        {
            if (argv[i][1]=='-')
            {
                if (strcmp(&argv[i][2], "help")==0)
                {
                    printUsage(argv[0]);
                    return 0;
                }
                else
                if (strncmp(&argv[i][2], "file=", strlength("file="))==0)
                {
                    if (buf)
                    {
                        errorMoreThanOnePattern(argv[0]);
                        return -1;
                    }
                    if (int result = loadPatternFile(buf, argv[i] + 2 + strlength("file=")))
                        return result;
                }
                else
                if (strcmp(&argv[i][2], "verbose")==0)
                {
                    verbose = true;
                }
                else
                if (strcmp(&argv[i][2], "line-buffered")==0)
                {
                    lineBuffered = true;
                }
                else
                if (strncmp(&argv[i][2], "num=", strlength("num="))==0)
                {
                    if (mathMode)
                    {
                        fprintf(stderr, "Error: \"-n\" or \"--num\" is specified more than once\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    char *arg = argv[i] + 2 + strlength("num=");
                    if (!arg[0] || arg[1])
                    {
                        fprintf(stderr, "Error: Argument to \"--num=\" must be a single character\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    mathMode = arg[0];
                }
                else
                if (strncmp(&argv[i][2], "fs", strlength("fs"))==0 &&
                    (argv[i][2 + strlength("fs")] == '-' ||
                     argv[i][2 + strlength("fs")] == '+' ) &&
                    !argv[i][2 + strlength("fs") + 1])
                {
                    free_spacing_mode = argv[i][2 + strlength("fs")] == '+';
                }
                else
                if (strncmp(&argv[i][2], "npcg", strlength("npcg"))==0 &&
                    (argv[i][2 + strlength("npcg")] == '-' ||
                     argv[i][2 + strlength("npcg")] == '+' ) &&
                    !argv[i][2 + strlength("npcg") + 1])
                {
                    emulate_ECMA_NPCGs = argv[i][2 + strlength("npcg")] == '+';
                }
                else
                if (strncmp(&argv[i][2], "ecc", strlength("ecc"))==0 &&
                    (argv[i][2 + strlength("ecc")] == '-' ||
                     argv[i][2 + strlength("ecc")] == '+' ) &&
                    !argv[i][2 + strlength("ecc") + 1])
                {
                    allow_empty_character_classes = argv[i][2 + strlength("ecc")] == '+';
                }
                else
                if (strcmp(&argv[i][2], "trace")==0)
                {
                    debugTrace++;
                }
                else
                {
                    fprintf(stderr, "Error: Unrecognized option \"%s\"\n", argv[i]);
                    printShortUsage(argv[0]);
                    return -1;
                }
            }
            else
            if (argv[i][1]=='x')
            {
                const char *arg = &argv[i][2];
                if (!*arg && ++i < argc)
                    arg = argv[i];
                if (!*arg)
                {
                    fprintf(stderr, "Error: \"-x\" requires arguments\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
                for (const char *s = arg;;)
                {
                    if (strncmp(s, "ml", strlength("ml"))==0)
                    {
                        s += strlength("ml");
                        allow_molecular_lookahead = true;
                    }
                    else
                    if (strncmp(s, "ag", strlength("ag"))==0)
                    {
                        s += strlength("ag");
                        allow_atomic_groups = true;
                    }
                    else
                    if (strncmp(s, "rs", strlength("rs"))==0)
                    {
                        s += strlength("rs");
                        allow_reset_start = true;
                    }
                    else
                    if (strncmp(s, "all", strlength("all"))==0)
                    {
                        s += strlength("all");
                        allow_molecular_lookahead = true;
                        allow_atomic_groups = true;
                        allow_reset_start = true;
                    }
                    else
                    {
                        fprintf(stderr, "Error: Unrecognized argument after \"-x\"\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    if (!*s)
                        break;
                    if (*s == ',')
                        s++;
                    else
                    {
                        fprintf(stderr, "Error: Unrecognized argument after \"-x\"\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                }
            }
            else
            if (argv[i][1]=='f')
            {
                if (buf)
                {
                    errorMoreThanOnePattern(argv[0]);
                    return -1;
                }
                if (argv[i][2])
                {
                    if (int result = loadPatternFile(buf, &argv[i][2]))
                        return result;
                }
                else
                if (++i < argc)
                {
                    if (int result = loadPatternFile(buf, argv[i]))
                        return result;
                }
                else
                {
                    printShortUsage(argv[0]);
                    return -1;
                }
            }
            else
            if (argv[i][1]=='n')
            {
                if (mathMode)
                {
                    fprintf(stderr, "Error: \"-n\" or \"--num\" is specified more than once\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
                if (argv[i][2])
                {
                    if (argv[i][3])
                    {
                        fprintf(stderr, "Error: Argument after \"-n\" must be a single character\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    mathMode = argv[i][2];
                }
                else
                if (++i < argc)
                {
                    if (!argv[i][0] || argv[i][1])
                    {
                        fprintf(stderr, "Error: Argument after \"-n\" must be a single character\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    mathMode = argv[i][0];
                }
            }
            else
            if (argv[i][1]=='o' && !argv[i][2])
                showMatch = true;
            else
            if (argv[i][1]=='O')
            {
                try
                {
                    const char *optStr = &argv[i][2];
                    if (!*optStr)
                    {
                        if (++i >= argc)
                            throw ParsingError();
                        optStr = argv[i];
                    }
                    if (!inrange(*optStr, '0', '9'))
                        throw ParsingError();
                    optimizationLevel = readNumericConstant<Uint>(optStr);
                    if (!inrange(optimizationLevel, 0, 2))
                        throw ParsingError();
                }
                catch (ParsingError)
                {
                    fprintf(stderr, "Error: \"-O\" must be followed by a number from 0 to 2\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
            }
            else
            if (argv[i][1]=='t')
            {
                if (testNumInc)
                {
                    fprintf(stderr, "Error: In this version, only one test range may be specified\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
                try
                {
                    const char *rangeStr = &argv[i][2];
                    if (!*rangeStr)
                    {
                        if (++i >= argc)
                            throw ParsingError();
                        rangeStr = argv[i];
                    }
                    if (!inrange(*rangeStr, '0', '9'))
                        throw ParsingError();
                    testNum0 = readNumericConstant<Uint64>(rangeStr);
                    if (!*rangeStr)
                    {
                        testNum1 = testNum0;
                        testNum_digits = 1;
                        testNumInc = +1;
                    }
                    else
                    {
                        if (*rangeStr!='.' || *++rangeStr!='.' || (++rangeStr, !inrange(*rangeStr, '0', '9')))
                            throw ParsingError();
                        const char *prevPos = rangeStr;
                        testNum1 = readNumericConstant<Uint64>(rangeStr);
                        testNum_digits = (Uint)(rangeStr - prevPos);
                        if (*rangeStr)
                        {
                            fprintf(stderr, "Error: \"-t\" must be followed by a numerical range only\n");
                            printShortUsage(argv[0]);
                            return -1;
                        }
                        testNumInc = testNum0 <= testNum1 ? +1 : -1;
                    }
                }
                catch (ParsingError)
                {
                    fprintf(stderr, "Error: \"-t\" must be followed by a numerical range\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
            }
            else
            if (!argv[i][1])
                optionsDone = true;
            else
            {
                fprintf(stderr, "Error: Unrecognized option \"%s\"\n", argv[i]);
                printShortUsage(argv[0]);
                return -1;
            }
        }
        else
        {
            if (buf)
            {
                errorMoreThanOnePattern(argv[0]);
                return -1;
            }
            buf = argv[i];
        }
    }

    if (!buf)
    {
        fprintf(stderr, "Error: No pattern specified\n");
        printShortUsage(argv[0]);
        return -1;
    }

    try
    {
        Regex regex(buf);

        if (mathMode)
        {
#if defined(TEST_NUMBERS_FIBONACCI)
            Uint64 a=0, b=1;
            for(;;)
            {
                Uint64 returnMatch;
                if (regex.MatchNumber(a, mathMode, returnMatch))
                    printf("%llu -> %llu\n", a, returnMatch);
                else
                    printf("%llu -> no match (FALSE NEGATIVE)\n", a);
#   if defined(TEST_FOR_FALSE_POSITIVES)
                for (Uint64 i=a+1; i<b; i++)
                    if (regex.MatchNumber(i, mathMode, returnMatch))
                        printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
#   endif
                Uint64 c = a + b;
                a = b;
                b = c;
            }
#else
            if (testNumInc)
                for (Uint64 i=testNum0;; i+=testNumInc)
                {
                    Uint64 returnMatch;
                    if (regex.MatchNumber(i, mathMode, returnMatch))
                    {
                        printf("%*llu -> %*llu\n", testNum_digits, i, testNum_digits, returnMatch);
                        if (lineBuffered)
                            fflush(stdout);
                    }
                    if (i==testNum1)
                        break;
                }
            else
            {
                LineGetter lineGetter(1<<5);
                for (;;)
                {
                    const char *line = lineGetter.fgets(stdin);
                    if (!line)
                        break;
                    if (inrange(*line, '0', '9'))
                    {
                        Uint64 input = readNumericConstant<Uint64>(line);
                        Uint64 returnMatch;
                        bool matched = regex.MatchNumber(input, mathMode, returnMatch);
                        if (verbose)
                        {
                            if (matched)
                                printf("%llu -> %llu\n", input, returnMatch);
                            else
                                printf("%llu -> no match\n", input);
                        }
                        else
                        if (matched)
                            printf("%llu\n", returnMatch);
                    }
                    else
                        puts(line);
                }
            }
#endif
        }
        else
        {
            LineGetter lineGetter(1<<15);
            const char newline = '\n';
            for (;;)
            {
                char *line = lineGetter.fgets(stdin);
                if (!line)
                    break;
                const char *returnMatch;
                size_t returnMatchLength;
                if (regex.MatchString(line, returnMatch, returnMatchLength))
                {
                    if (showMatch)
                        printf("%.*s\n", returnMatchLength, returnMatch);
                    else
                        puts(line);
                    if (lineBuffered)
                        fflush(stdout);
                }
            }
        }

        return 0;
    }
    catch (RegexParsingError err)
    {
        fprintf(stderr, "Error parsing regex pattern at offset %u: %s\n", err.buf - buf, err.msg);
        return -1;
    }
}
