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
#include <math.h>

#include "regex.h"
#include "parser.h"
#include "matcher.h"

class Regex
{
    RegexGroupRoot regex;
    Uint numCaptureGroups;
    Uint maxGroupDepth;
public:
    Regex(const char *buf);
    bool MatchNumber(Uint64 input, char basicChar, Uint returnMatch_backrefIndex, Uint64 &returnMatch, Uint64 *possibleMatchesCount_ptr);
    bool MatchString(const char *stringToMatchAgainst, Uint returnMatch_backrefIndex, const char *&returnMatch, size_t &returnMatchLength, Uint64 *possibleMatchesCount_ptr);
};

Regex::Regex(const char *buf)
{
    regex.type = RegexGroup_NonCapturing;
    regex.minCount = 1;
    regex.maxCount = 1;
    regex.lazy = 0;
    regex.possessive = 0;

    RegexParser parser(regex, buf);
    numCaptureGroups = parser.backrefIndex;
    maxGroupDepth    = parser.maxGroupDepth;
}

bool Regex::MatchNumber(Uint64 input, char basicChar, Uint returnMatch_backrefIndex, Uint64 &returnMatch, Uint64 *possibleMatchesCount_ptr=NULL)
{
    RegexMatcher<false> match;
    match.basicChar = basicChar;
    Uint64 returnMatchOffset;
    return match.Match(regex, numCaptureGroups, maxGroupDepth, input, returnMatch_backrefIndex, returnMatchOffset, returnMatch, possibleMatchesCount_ptr);
}

bool Regex::MatchString(const char *stringToMatchAgainst, Uint returnMatch_backrefIndex, const char *&returnMatch, size_t &returnMatchLength, Uint64 *possibleMatchesCount_ptr=NULL)
{
    RegexMatcher<true> match;
    bool result = match.Match(regex, numCaptureGroups, maxGroupDepth, (Uint64)stringToMatchAgainst, returnMatch_backrefIndex, (Uint64 &)returnMatch, (Uint64 &)returnMatchLength, possibleMatchesCount_ptr);
    (const char *&)returnMatch = stringToMatchAgainst + (size_t)(Uint64 &)returnMatch;
    return result;
}


enum StringModeTest
{
    StringModeTest_NONE,
    StringModeTest_TRIPLES,
    StringModeTest_MULTIPLICATION,
    StringModeTest_MULTIPLICATION_INCLUDING_ZERO,
    StringModeTest_BINARY_SUM,
    StringModeTest_DECIMAL_SUM,
    StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_ALLOWED,
    StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_PROHIBITED,
    StringModeTest_SMOOTH_NUMBERS,
    StringModeTest_TRIANGULAR_TABLE,
    StringModeTest_TRIANGULAR_TABLE2,
};
enum NumericalModeTest
{
    NumericalModeTest_NONE,
    NumericalModeTest_NUMBERS_FIBONACCI,
    NumericalModeTest_NUMBERS_POWER_OF_2,
    NumericalModeTest_NUMBERS_TRIANGULAR,
    NumericalModeTest_DIV_SQRT2,
    NumericalModeTest_DIV_SQRT2_up,
    NumericalModeTest_DIV_SQRT2_any,
};


// George Marsaglia's multiply with carry PRNG; very fast, but much more random than linear congruential
#define znew  ((z=36969*(z&65535)+(z>>16))<<16)
#define wnew  ((w=18000*(w&65535)+(w>>16))&65535)
#define IUNI  (znew+wnew)
static Uint32 z=362436069, w=521288629;

// todo: implement these as class members rather than global variables?
Uint debugTrace = 0;
bool free_spacing_mode = false;
bool emulate_ECMA_NPCGs = true;
bool allow_empty_character_classes = true;
bool no_empty_optional = true;
bool allow_quantifiers_on_assertions = true;
bool allow_molecular_lookaround = false;
bool allow_lookinto = false;
bool allow_atomic_groups = false;
bool allow_branch_reset_groups = false;
bool allow_possessive_quantifiers = false;
bool allow_conditionals = false;
bool allow_lookaround_conditionals = false;
bool allow_reset_start = false;
bool enable_persistent_backrefs = false;
bool enable_verbs = false;
Uint optimizationLevel = 2;

static void printShortUsage(const char *argv0)
{
    fprintf(stderr, "Use the \"--help\" option to see full information on command-line options.\n");
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
                      By default it is disabled.\n\
  --npcg{-|+}         Specifies the behavior of non-participating capture\n\
                      groups. \"-\" makes them match nothing (as in most regex\n\
                      engines), and \"+\" makes them match an empty string (as\n\
                      in ECMAScript). The default is \"+\".\n\
  --ecc{-|+}          Specifies whether an empty character class, i.e. \"[]\",\n\
                      or \"[^]\", is permitted. \"-\" makes it an error to attempt\n\
                      to use them (as in most regex engines), and \"+\" allows\n\
                      them (as in ECMAScript). The default is \"+\".\n\
  --neo{-|+}          Specifies what to do when an empty match occurs in a\n\
                      group that has a minimum and maximum quantifier different\n\
                      from each other (e.g. \"?\", \"*\", \"+\", \"{3,5}\") after the\n\
                      minimum has been satisfied. \"-\" makes the engine exit the\n\
                      group with a successful completed match and not attempt\n\
                      to fulfill the maximum. \"+\" makes it backtrack in an\n\
                      attempt to find a non-empty match. The default is \"+\"\n\
                      (standard ECMAScript behavior).\n\
                      Stands for \"no empty optional\".\n\
  --qa{-|+}           Specifies whether to allow quantifiers on assertions:\n\
                      lookaheads, anchors, and word boundaries/nonboundaries.\n\
                      \"-\" disallows them, and \"+\" allows them (the default).\n\
                      Note that this is the default because ECMAScript as\n\
                      implemented in most browsers allows it, even though the\n\
                      specification disallows it.\n\
  -x EXT,EXT,...      Enable extensions. Currently available extensions are:\n\
                      ml   Molecular (non-atomic) lookahead: (?*...)\n\
                      li   Lookinto: (?^=...), (?^!...), (?^N=...), (?^N!...)\n\
                           if \"ml\" is enabled also, (?^*...), (?^N*...)\n\
                      ag   Atomic Grouping: (?>...)\n\
                      brg  Branch Reset Groups: (?|(...)|(...)|...)\n\
                      pq   Possessive Quantifiers: p*+ p++ p?+ p{A,B}+\n\
                      cnd  Conditionals: (?(N)...|...) where N=backref number\n\
                      lcnd Lookaround Conditionals: (?(?=...)...|...) etc.\n\
                      rs   Reset Start: \\K\n\
                      pbr  Nested and forward backrefs\n\
                      v    Verbs: (*ACCEPT), (*FAIL), (*COMMIT),\n\
                                  (*PRUNE), (*SKIP), and (*THEN)\n\
                      all  Enable all of the above extensions\n\
  --pcre              Emulate PCRE as closely as currently possible. This\n\
                      is equivalent to:\n\
                      --npcg- --ecc- --neo- -x ag,pq,cnd,rs,pbr\n\
  -o                  Show only the part of the line that matched\n\
  -v, --invert-match  Show non-matching inputs instead of matching inputs\n\
  -q [NUM0[..NUM1]]   Show the NUM0th..NUM1th (zero-indexed) number(s) that are\n\
                      a match. Implies \"--num=x\" if \"--num\" was not specified.\n\
                      Can be combined with \"--invert-match\". If NUM0 is not\n\
                      specified, it will be read from standard input.\n\
  -Q [NUM]            Show the first NUM numbers that are a match. Implies\n\
                      \"--num=x\" if \"--num\" was not specified. Can be combined\n\
                      with \"--invert-match\". If NUM is not specified, it will be\n\
                      read from standard input.\n\
  -X                  Exhaustive mode; counts the number of possible matches,\n\
                      without reporting what the actual matches are.\n\
  -O NUMBER           Specifies the optimization level, from 0 to 2. This\n\
                      controls whether optimizations are enabled which skip\n\
                      unnecessary backtracking. The default is the maximum, 2.\n\
                      Currently, -O1 enables simple end-anchor and subtraction\n\
                      optimizations, and -O2 enables Power of 2 optimizations.\n\
  -t NUM0[..NUM1]     (In numerical mode only) Test the range of numbers from\n\
                      NUM0 to NUM1, inclusive. If NUM1 is not specified, only\n\
                      one number, NUM0, shall be tested.\n\
  --test=TEST         Execute one of the built-in tests aimed at specific\n\
                      challenges. Use --test alone to show a list of available\n\
					  tests.\n\
  --test-false+       Enable testing false positives for whichever built-in test\n\
                      is selected. Must be combined with \"--test=\".\n\
  --trace             Enable printout of debug trace. Use this parameter twice\n\
                      to include a dump of the backtracking stack at every step\n\
                      (which is extremely verbose).\n\
  --verbose           Print both matches and non-matches along with the input\n\
                      number. Currently works only in numerical mode when\n\
                      taking input from standard input.\n\
", argv0);
}

static void printTestList()
{
    fprintf(stderr, "\
String mode tests:\n\
  triples                  Match multiples of 3 in decimal notation.\n\
\n\
  multiplication           Match correct multiplication in unary. Example:\n\
     xxx*xxxx=xxxxxxxxxxxxxxxx\n\
\n\
  multiplication-0         Match correct multiplication in unary, where\n\
                           factors can be equal to zero. Examples:\n\
     *xxxx=\n\
     xxx*=\n\
     xxx*xxxx=xxxxxxxxxxxxxxxx\n\
\n\
  binary-sum               Match correct addition in 16-digit binary, where the\n\
                           carry bit is discarded. Examples:\n\
     1100100101100000 + 0000000011011100 = 1100101000111100\n\
     1111011011101110 + 0110001010000100 = 0101100101110010\n\
\n\
  decimal-sum              Match correct addition in 10-digit decimal, where the\n\
                           carry is discarded. Examples:\n\
     1293972669 + 6684886271 = 7978858940\n\
     2107255058 + 8170104067 = 0277359125\n\
\n\
  decimal-byte             Match decimal numbers in the range 0-255, with no\n\
                           leading zeroes allowed.\n\
\n\
  decimal-byte-0           Match decimal numbers in the range 0-255, with\n\
                           leading zeroes allowed.\n\
\n\
  smoothest-numbers        Test solutions to CCGC question #36384 - take two\n\
                           unary numbers as input, delimited by a comma, and\n\
                           return as a match the number within that inclusive\n\
                           range which has the smallest prime factor. If there\n\
                           are more than one with the same smallest prime\n\
                           factor, any one of them will be accepted.\n\
\n\
  triangular-table         Tests any regex taking two comma-delimited positive\n\
                           parameters in unary, where the second parameter must\n\
                           be less than or equal to the first. Prints the output\n\
                           in a triangular table. The row indicates the first\n\
                           parameter, and the column the second. Unlike all the\n\
                           other tests, this only displays the output, and\n\
                           doesn't verify its correctness. This can be combined\n\
                           with the -t NUM0[..NUM1] parameter.\n\
  triangular-table2        The above, but with the order of arguments reversed.\n\
\n\
Numerical mode (unary) tests:\n\
  Fibonacci                Match only Fibonacci numbers.\n\
  power-of-2               Match only powers of 2.\n\
  triangular               Match only triangular numbers.\n\
  div-sqrt2                Take any number as input, and output that number\n\
                           divided by the square root of 2, rounded down.\n\
  div-sqrt2-up             The above, but rounded up.\n\
  div-sqrt2-any            The above, but allowing the rounding to be either\n\
                           up or down.\n\
");
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

static Uint64 largestPrimeFactor(Uint64 n)
{
    for (Uint64 k=n/2; k>1;)
    {
        if (n % k == 0)
        {
            n = k;
            k = n/2;
        }
        else
            k--;
    }
    return n;
}

int main(int argc, char *argv[])
{
    // crudely implemented getopt-command-line interface; probably replace it with getopt later
    char *buf = NULL;
    char mathMode = '\0'; // if nonzero, enables math mode and specifies what character to use
    StringModeTest stringModeTest = StringModeTest_NONE;
    NumericalModeTest numericalModeTest = NumericalModeTest_NONE;
    bool testForFalsePositives = false;
    bool verbose = false;
    bool lineBuffered = false;
    bool showMatch = false;
    bool invertMatch = false;
    bool showSequenceNth = false;
    bool showSequenceUpTo = false;
    bool countPossibleMatches = false;
    bool optionsDone = false;
    Uint showMatch_backrefIndex = 0;
    Uint64 testNum0, testNum1; Uint testNum_digits; int64 testNumInc = 0;
    Uint64  seqNum0,  seqNum1; Uint  seqNum_digits; int64  seqNumInc = 0;
    auto setFullTestRange = [&]()
    {
        testNum0 = 0;
        testNum1 = ULLONG_MAX;
        testNumInc = 1;
        testNum_digits = 0;
    };

    for (int i=1; i<argc; i++)
    {
        auto parseRange = [&](Uint64 &num0, Uint64 &num1, Uint &num_digits, int64 &numInc, const char *optionName, const char *onlyOneErrorStr) -> int
        {
            if (numInc)
            {
                fprintf(stderr, onlyOneErrorStr);
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
                num0 = readNumericConstant<Uint64>(rangeStr);
                if (!*rangeStr)
                {
                    num1 = num0;
                    num_digits = 1;
                    numInc = +1;
                }
                else
                {
                    if (*rangeStr!='.' || *++rangeStr!='.' || (++rangeStr, !inrange(*rangeStr, '0', '9')))
                        throw ParsingError();
                    const char *prevPos = rangeStr;
                    num1 = readNumericConstant<Uint64>(rangeStr);
                    num_digits = (Uint)(rangeStr - prevPos);
                    if (*rangeStr)
                    {
                        fprintf(stderr, "Error: \"-%s\" must be followed by a numerical range only\n", optionName);
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    numInc = num0 <= num1 ? +1 : -1;
                }
            }
            catch (ParsingError)
            {
                fprintf(stderr, "Error: \"-%s\" must be followed by a numerical range\n", optionName);
                printShortUsage(argv[0]);
                return -1;
            }
            return 0;
        };

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
                if (strcmp(&argv[i][2], "invert-match")==0)
                    invertMatch = true;
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
                if (strncmp(&argv[i][2], "neo", strlength("neo"))==0 &&
                    (argv[i][2 + strlength("neo")] == '-' ||
                     argv[i][2 + strlength("neo")] == '+' ) &&
                    !argv[i][2 + strlength("neo") + 1])
                {
                    no_empty_optional = argv[i][2 + strlength("neo")] == '+';
                }
                else
                if (strncmp(&argv[i][2], "qa", strlength("qa"))==0 &&
                    (argv[i][2 + strlength("qa")] == '-' ||
                     argv[i][2 + strlength("qa")] == '+' ) &&
                    !argv[i][2 + strlength("qa") + 1])
                {
                    allow_quantifiers_on_assertions = argv[i][2 + strlength("qa")] == '+';
                }
                else
                if (strcmp(&argv[i][2], "pcre")==0)
                {
                    emulate_ECMA_NPCGs = false;
                    allow_empty_character_classes = false;
                    no_empty_optional = false;
                    allow_quantifiers_on_assertions = true;
                    allow_molecular_lookaround = false;
                    allow_lookinto = false;
                    allow_atomic_groups = true;
                    allow_branch_reset_groups = true;
                    allow_possessive_quantifiers = true;
                    allow_conditionals = true;
                    allow_lookaround_conditionals = true;
                    allow_reset_start = true;
                    enable_persistent_backrefs = true;
                    enable_verbs = true;
                }
                else
                if (strcmp(&argv[i][2], "trace")==0)
                {
                    debugTrace++;
                }
                else
                if (strcmp(&argv[i][2], "test")==0)
                {
                    printTestList();
                    return 0;
                }
                else
                if (strncmp(&argv[i][2], "test=", strlength("test="))==0)
                {
                    if (stringModeTest != StringModeTest_NONE || numericalModeTest != NumericalModeTest_NONE)
                    {
                        fprintf(stderr, "Error: Cannot do more than one test in a single run\n");
                        return -1;
                    }
                    {{}} if (strcmp(&argv[i][2+strlength("test=")], "triples"          )==0) stringModeTest = StringModeTest_TRIPLES;
                    else if (strcmp(&argv[i][2+strlength("test=")], "multiplication"   )==0) stringModeTest = StringModeTest_MULTIPLICATION;
                    else if (strcmp(&argv[i][2+strlength("test=")], "multiplication-0" )==0) stringModeTest = StringModeTest_MULTIPLICATION_INCLUDING_ZERO;
                    else if (strcmp(&argv[i][2+strlength("test=")], "binary-sum"       )==0) stringModeTest = StringModeTest_BINARY_SUM;
                    else if (strcmp(&argv[i][2+strlength("test=")], "decimal-sum"      )==0) stringModeTest = StringModeTest_DECIMAL_SUM;
                    else if (strcmp(&argv[i][2+strlength("test=")], "decimal-byte"     )==0) stringModeTest = StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_ALLOWED;
                    else if (strcmp(&argv[i][2+strlength("test=")], "decimal-byte-0"   )==0) stringModeTest = StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_PROHIBITED;
                    else if (strcmp(&argv[i][2+strlength("test=")], "smoothest-numbers")==0) stringModeTest = StringModeTest_SMOOTH_NUMBERS;
                    else if (strcmp(&argv[i][2+strlength("test=")], "triangular-table" )==0) stringModeTest = StringModeTest_TRIANGULAR_TABLE;
                    else if (strcmp(&argv[i][2+strlength("test=")], "triangular-table2")==0) stringModeTest = StringModeTest_TRIANGULAR_TABLE2;
                    else if (strcmp(&argv[i][2+strlength("test=")], "Fibonacci"        )==0) numericalModeTest = NumericalModeTest_NUMBERS_FIBONACCI;
                    else if (strcmp(&argv[i][2+strlength("test=")], "power-of-2"       )==0) numericalModeTest = NumericalModeTest_NUMBERS_POWER_OF_2;
                    else if (strcmp(&argv[i][2+strlength("test=")], "triangular"       )==0) numericalModeTest = NumericalModeTest_NUMBERS_TRIANGULAR;
                    else if (strcmp(&argv[i][2+strlength("test=")], "div-sqrt2"        )==0) numericalModeTest = NumericalModeTest_DIV_SQRT2;
                    else if (strcmp(&argv[i][2+strlength("test=")], "div-sqrt2-up"     )==0) numericalModeTest = NumericalModeTest_DIV_SQRT2_up;
                    else if (strcmp(&argv[i][2+strlength("test=")], "div-sqrt2-any"    )==0) numericalModeTest = NumericalModeTest_DIV_SQRT2_any;
                    else
                    {
                        fprintf(stderr, "Error: Unrecognized test \"%s\"\n", &argv[i][2+strlength("test=")]);
                        return -1;
                    }
                }
                else
                if (strcmp(&argv[i][2], "test-false+")==0)
                {
                    testForFalsePositives = true;
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
                        allow_molecular_lookaround = true;
                    }
                    else
                    if (strncmp(s, "li", strlength("li"))==0)
                    {
                        s += strlength("li");
                        allow_lookinto = true;
                    }
                    else
                    if (strncmp(s, "ag", strlength("ag"))==0)
                    {
                        s += strlength("ag");
                        allow_atomic_groups = true;
                    }
                    else
                    if (strncmp(s, "brg", strlength("brg"))==0)
                    {
                        s += strlength("brg");
                        allow_branch_reset_groups = true;
                    }
                    else
                    if (strncmp(s, "pq", strlength("pq"))==0)
                    {
                        s += strlength("pq");
                        allow_possessive_quantifiers = true;
                    }
                    else
                    if (strncmp(s, "cnd", strlength("cnd"))==0)
                    {
                        s += strlength("cnd");
                        allow_conditionals = true;
                    }
                    else
                    if (strncmp(s, "lcnd", strlength("lcnd"))==0)
                    {
                        s += strlength("lcnd");
                        allow_lookaround_conditionals = true;
                    }
                    else
                    if (strncmp(s, "rs", strlength("rs"))==0)
                    {
                        s += strlength("rs");
                        allow_reset_start = true;
                    }
                    else
                    if (strncmp(s, "pbr", strlength("pbr"))==0)
                    {
                        s += strlength("pbr");
                        enable_persistent_backrefs = true;
                    }
                    else
                    if (strncmp(s, "v", strlength("v"))==0)
                    {
                        s += strlength("v");
                        enable_verbs = true;
                    }
                    else
                    if (strncmp(s, "all", strlength("all"))==0)
                    {
                        s += strlength("all");
                        allow_molecular_lookaround = true;
                        allow_lookinto = true;
                        allow_atomic_groups = true;
                        allow_branch_reset_groups = true;
                        allow_possessive_quantifiers = true;
                        allow_conditionals = true;
                        allow_lookaround_conditionals = true;
                        allow_reset_start = true;
                        enable_persistent_backrefs = true;
                        enable_verbs = true;
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
            if (argv[i][1]=='o')
            {
                showMatch = true;
                const char *optStr = &argv[i][2];
                if (*optStr)
                {
                    try
                    {
                        if (!inrange(*optStr, '0', '9'))
                            throw ParsingError();
                        showMatch_backrefIndex = readNumericConstant<Uint>(optStr);
                        if (*optStr)
                            throw ParsingError();
                    }
                    catch (ParsingError)
                    {
                        fprintf(stderr, "Error: \"-o\" must be followed by a capture group number\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                }
                else
                    showMatch_backrefIndex = 0;
            }
            else
            if (argv[i][1]=='v')
                invertMatch = true;
            else
            if (argv[i][1]=='q')
            {
                if (!mathMode)
                    mathMode = 'x';
                const char *onlyOneErrorStr = "Error: In this version, only one sequence range may be specified\n";
                if (showSequenceNth)
                {
                    fprintf(stderr, onlyOneErrorStr);
                    printShortUsage(argv[0]);
                    return -1;
                }
                if (argv[i][2] || i+1 < argc && argv[i+1][0] != '-')
                {
                    if (int retval = parseRange(seqNum0, seqNum1, seqNum_digits, seqNumInc, "t", onlyOneErrorStr))
                        return retval;
                    if (seqNumInc != 1)
                    {
                        fprintf(stderr, "Error: In this version, only ascending sequence ranges are supported\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    setFullTestRange();
                }
                showSequenceNth = true;
            }
            else
            if (argv[i][1]=='Q')
            {
                if (!mathMode)
                    mathMode = 'x';
                if (showSequenceNth || showSequenceUpTo)
                {
                    fprintf(stderr, "Error: Only one sequence range may be specified\n");
                    printShortUsage(argv[0]);
                    return -1;
                }
                const char *optStr = &argv[i][2];
                if (*optStr || i+1 < argc && argv[i+1][0] != '-')
                {
                    try
                    {
                        if (!*optStr)
                        {
                            ++i;
                            optStr = argv[i];
                        }
                        if (!inrange(*optStr, '0', '9'))
                            throw ParsingError();
                        const char *prevPos = optStr;
                        seqNum1 = readNumericConstant<Uint64>(optStr);
                        seqNum_digits = (Uint)(optStr - prevPos);
                        if (*optStr)
                            throw ParsingError();
                    }
                    catch (ParsingError)
                    {
                        fprintf(stderr, "Error: \"-Q\" must be followed by a number\n");
                        printShortUsage(argv[0]);
                        return -1;
                    }
                    if (seqNum1 == 0)
                        seqNum0 = 1;
                    else
                    {
                        seqNum0 = 0;
                        seqNum1--;
                        seqNum_digits = intLength(seqNum1);
                    }
                    seqNumInc = 1;
                    setFullTestRange();
                    showSequenceNth = true;
                }
                else
                    showSequenceUpTo = true;
            }
            else
            if (argv[i][1]=='X')
                countPossibleMatches = true;
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
                    if (!inrange(optimizationLevel, 0, 2) || *optStr)
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
                if (int retval = parseRange(testNum0, testNum1, testNum_digits, testNumInc, "t", "Error: In this version, only one test range may be specified\n"))
                    return retval;
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

    if (invertMatch && (showMatch || countPossibleMatches || verbose))
    {
        fprintf(stderr, "Error: -v cannot be combined with -o, -X, or --verbose\n");
        printShortUsage(argv[0]);
        return -1;
    }
    if (showSequenceNth && (showSequenceUpTo || showMatch || countPossibleMatches))
    {
        fprintf(stderr, "Error: -q cannot be combined with -Q, -o, or -X\n");
        printShortUsage(argv[0]);
        return -1;
    }
    if (showSequenceUpTo && (showMatch || countPossibleMatches))
    {
        fprintf(stderr, "Error: -q cannot be combined with -o or -X\n");
        printShortUsage(argv[0]);
        return -1;
    }
    if (countPossibleMatches && showMatch)
    {
        fprintf(stderr, "Error: -X cannot currently be combined with -o\n");
        printShortUsage(argv[0]);
        return -1;
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
            if (stringModeTest != StringModeTest_NONE)
            {
                fprintf(stderr, "Error: String Mode test specified in Numerical Mode\n");
                return -1;
            }
            switch (numericalModeTest)
            {
                case NumericalModeTest_NUMBERS_FIBONACCI:
                {
                    Uint64 a=0, b=1;
                    for(;;)
                    {
                        Uint64 returnMatch;
                        if (regex.MatchNumber(a, mathMode, showMatch_backrefIndex, returnMatch))
                            printf("%llu -> %llu\n", a, returnMatch);
                        else
                            printf("%llu -> no match (FALSE NEGATIVE)\n", a);
                        if (a == 12200160415121876738uLL)
                        {
                            if (testForFalsePositives)
                                for (Uint64 i=a+1; i!=0; i++)
                                    if (regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch))
                                        printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
                            break;
                        }
                        if (testForFalsePositives)
                            for (Uint64 i=a+1; i<b; i++)
                                if (regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch))
                                    printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
                        Uint64 c = a + b;
                        a = b;
                        b = c;
                    }
                    break;
                }
                case NumericalModeTest_NUMBERS_POWER_OF_2:
                {
                    Uint64 z=0;
                    Uint64 a=1;
                    for(;;)
                    {
                        Uint64 returnMatch;
                        if (testForFalsePositives)
                        {
                            for (Uint64 i=z; i<a; i++)
                                if (regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch))
                                    printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
                            z = a+1;
                        }
                        if (regex.MatchNumber(a, mathMode, showMatch_backrefIndex, returnMatch))
                            printf("%llu -> %llu\n", a, returnMatch);
                        else
                            printf("%llu -> no match (FALSE NEGATIVE)\n", a);
                        Uint64 a2 = a + a;
                        if (a2 == 0)
                        {
                            if (testForFalsePositives)
                                for (Uint64 i=a+1; i!=0; i++)
                                    if (regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch))
                                        printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
                            break;
                        }
                        a = a2;
                    }
                    break;
                }
                case NumericalModeTest_NUMBERS_TRIANGULAR:
                {
                    Uint n=0, m=1, z=0;
                    for (;;)
                    {
                        Uint64 returnMatch;
                        if (testForFalsePositives)
                        {
                            for (Uint64 i=z; i<n; i++)
                                if (regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch))
                                    printf("%llu -> %llu (FALSE POSITIVE)\n", i, returnMatch);
                            z = n+1;
                        }
                        if (regex.MatchNumber(n, mathMode, showMatch_backrefIndex, returnMatch))
                            printf("%u -> %llu\n", n, returnMatch);
                        else
                            printf("%u -> no match (FALSE NEGATIVE)\n", n);
                        n += m;
                        m++;
                    }
                    break;
                }
                case NumericalModeTest_DIV_SQRT2:
                case NumericalModeTest_DIV_SQRT2_up:
                case NumericalModeTest_DIV_SQRT2_any:
                {
                    const double sqrt2 = sqrt(2.);
                    for (Uint64 i=0;; i++)
                    {
                        Uint64 answer = (Uint64)floor(i / sqrt2);
                        if (numericalModeTest == NumericalModeTest_DIV_SQRT2_up)
                            answer += 1;
                        Uint64 returnMatch;
                        bool matched = regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch);
                        if (!matched)
                        {
                            printf("%9llu -> NOT MATCHED!\n", i);
                            if (lineBuffered)
                                fflush(stdout);
                        }
                        else
                        if (returnMatch != answer && (numericalModeTest != NumericalModeTest_DIV_SQRT2_any || returnMatch != answer+1))
                        {
                            int64 error = returnMatch - answer;
                            if (numericalModeTest == NumericalModeTest_DIV_SQRT2_any && error > 0)
                                error -= 1;
                            printf("%9llu -> %9llu (off by %2lld, should be %9llu", i, returnMatch, error, answer);
                            if (numericalModeTest == NumericalModeTest_DIV_SQRT2_any)
                                printf(" or %9llu", answer+1);
                            fputs(")\n", stdout);
                            if (lineBuffered)
                                fflush(stdout);
                        }
                        else
                        if (i <= 10 || i % (1<<4) == 0 || i >= 940)
                        {
                            printf("%9llu -> %9llu\n", i, returnMatch);
                            if (lineBuffered)
                                fflush(stdout);
                        }
                    }
                    break;
                }
                default:
                {
                    Uint64 possibleMatchesCount;
                    Uint64 *possibleMatchesCount_ptr = countPossibleMatches ? &possibleMatchesCount : NULL;

                    auto showSequence = [&](bool showIndex)
                    {
                        Uint64 seqNum = 0;
                        for (Uint64 i=testNum0;; i+=testNumInc)
                        {
                            Uint64 returnMatch;
                            bool matched = regex.MatchNumber(i, mathMode, showMatch_backrefIndex, returnMatch, possibleMatchesCount_ptr);
                            if (invertMatch)
                            {
                                if (!matched)
                                {
                                    if (showIndex && seqNumInc && seqNum >= seqNum0)
                                        printf("%*llu: ", seqNum_digits, seqNum);
                                    if (!seqNumInc || seqNum >= seqNum0)
                                    {
                                        printf("%*llu\n", testNum_digits, i);
                                        if (lineBuffered)
                                            fflush(stdout);
                                    }
                                    if (seqNumInc && seqNum++ >= seqNum1)
                                        break;
                                }
                            }
                            else
                            if (matched || countPossibleMatches)
                            {
                                if (showIndex && seqNumInc && seqNum >= seqNum0)
                                    printf("%*llu: ", seqNum_digits, seqNum);
                                if (!seqNumInc || seqNum >= seqNum0)
                                {
                                    printf("%*llu", testNum_digits, i);
                                    if (countPossibleMatches)
                                        printf(" -> %llu", *possibleMatchesCount_ptr);
                                    else
                                    if (showMatch)
                                        printf(" -> %*llu", testNum_digits, returnMatch);
                                    putchar('\n');
                                    if (lineBuffered)
                                        fflush(stdout);
                                }
                                if (seqNumInc && seqNum++ >= seqNum1)
                                    break;
                            }
                            if (i==testNum1)
                                break;
                        }
                    };

                    if (testNumInc)
                        showSequence(true);
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
                                if (showSequenceNth)
                                {
                                    setFullTestRange();
                                    seqNum0 = seqNum1 = input;
                                    seqNumInc = 1;
                                    seqNum_digits = 0;
                                    showSequence(false);
                                }
                                else
                                if (showSequenceUpTo)
                                {
                                    setFullTestRange();
                                    seqNum0 = 0;
                                    seqNum1 = input - 1;
                                    seqNumInc = 1;
                                    seqNum_digits = intLength(seqNum1);
                                    showSequence(true);
                                }
                                else
                                {
                                    Uint64 returnMatch;
                                    bool matched = regex.MatchNumber(input, mathMode, showMatch_backrefIndex, returnMatch, possibleMatchesCount_ptr);
                                    if (invertMatch)
                                    {
                                        if (!matched)
                                            printf("%llu\n", input);
                                    }
                                    else
                                    if (verbose)
                                    {
                                        if (countPossibleMatches)
                                            printf("%llu -> %llu\n", input, *possibleMatchesCount_ptr);
                                        else
                                        if (matched)
                                            printf("%llu -> %llu\n", input, returnMatch);
                                        else
                                            printf("%llu -> no match\n", input);
                                    }
                                    else
                                    if (matched)
                                        printf("%llu\n", returnMatch);
                                }
                            }
                            else
                                puts(line);
                        }
                    }
                    break;
                }
            }
        }
        else
        {
            if (numericalModeTest != NumericalModeTest_NONE)
            {
                fprintf(stderr, "Error: String Mode test specified in Numerical Mode\n");
                return -1;
            }
            switch (stringModeTest)
            {
                case StringModeTest_TRIPLES:
                {
                    for (Uint64 n=0;;)
                    {
                        bool positive = n % 3 == 0;
                        if (testForFalsePositives || positive)
                        {
                            char str[strlength("18446744073709551615")+1];
                            sprintf(str, "%llu", n);

                            const char *returnMatch;
                            size_t returnMatchLength;
                            if (regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                            {
                                if (!positive)
                                    printf("%s - FALSE POSITIVE!\n", str);
                            }
                            else
                            {
                                if (positive)
                                    printf("%s - FALSE NEGATIVE!\n", str);
                            }
                            if (n % 0x10000 == 0)
                                printf("%s\n", str);
                        }
                        if (++n == 0)
                            break;
                    }
                    break;
                }
                case StringModeTest_MULTIPLICATION:
                case StringModeTest_MULTIPLICATION_INCLUDING_ZERO:
                {
                    Uint start = stringModeTest==StringModeTest_MULTIPLICATION_INCLUDING_ZERO ? 0 : 1;
                    const Uint range = 25;
                    char str[range + strlength("*") + range + strlength("=") + range*range + 1];
                    for (Uint a=start; a<=range; a++)
                    {
                        for (Uint i=0; i<a; i++)
                            str[i] = 'x';
                        str[a] = '*';
                        for (Uint b=start; b<=range; b++)
                        {
                            for (Uint i=0; i<b; i++)
                                str[a+1+i] = 'x';
                            str[a+1+b] = '=';
                            Uint c;
                            if (!testForFalsePositives)
                            {
                                c = a * b;
                                goto skip_for;
                            }
                            for (c=start; c<=range*range; c++)
                            {
                            skip_for:
                                for (Uint i=0; i<c; i++)
                                    str[a+1+b+1+i] = 'x';
                                str[a+1+b+1+c] = '\0';

                                bool positive = a * b == c;

                                const char *returnMatch;
                                size_t returnMatchLength;
                                if (regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                                {
                                    printf("%u * %u = %u", a, b, c);
                                    if (!positive)
                                        fputs(" - FALSE POSITIVE!\n", stdout);
                                    else
                                        fputc('\n', stdout);
                                }
                                else
                                {
                                    if (positive)
                                        printf("%u * %u = %u - FALSE NEGATIVE!\n", a, b, c);
                                }
                                if (!testForFalsePositives)
                                    break;
                            }
                        }
                    }
                    break;
                }
                case StringModeTest_BINARY_SUM:
                {
                    const Uint bits = 16;
                    const Uint size = 1 << bits;
                    char str[bits + strlength(" + ") + bits + strlength(" = ") + bits + 1];
                    str[bits    ] = ' ';
                    str[bits  +1] = '+';
                    str[bits  +2] = ' ';
                    str[bits*2+3] = ' ';
                    str[bits*2+4] = '=';
                    str[bits*2+5] = ' ';
                    str[bits*3+6] = '\0';
                    bool useNoZeroesInAllButFirstNumber = true;
                    for (;;)
                    {
                        Uint a = IUNI  & (size-1);
                        Uint b = IUNI  & (size-1);
                        Uint c = (a+b) & (size-1);

                        Uint corruptedBits;
                        if (testForFalsePositives)
                        {
                            corruptedBits = IUNI % 4;
                            for (Uint i=0; i<corruptedBits; i++)
                                c ^= 1 << (IUNI % bits);
                        }

                        if (useNoZeroesInAllButFirstNumber)
                        {
                            a = 0;
                            b = size-1;
                            c = a + b;
                            useNoZeroesInAllButFirstNumber = false;
                        }

                        bool wrong;
                        if (testForFalsePositives)
                            wrong = ((a + b) & (size-1)) != c;
                        else
                            wrong = false;

                        for (Uint i=0; i<bits; i++)
                            str[i] = (a & (1<<(bits-1-i))) ? '1' : '0';
                        for (Uint i=0; i<bits; i++)
                            str[bits + 3 + i] = (b & (1<<(bits-1-i))) ? '1' : '0';
                        for (Uint i=0; i<bits; i++)
                            str[bits*2 + 6 + i] = (c & (1<<(bits-1-i))) ? '1' : '0';

                        const char *returnMatch;
                        size_t returnMatchLength;
                        if (!regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                        {
                            if (!wrong)
                                printf("%04X+%04X=%04X: %s - FALSE NEGATIVE!\n", a, b, c, str);
                            /*else
                                printf("%04X+%04X=%04X: %s - correct\n", a, b, c, str);*/
                        }
                        else
                        {
                            if (wrong)
                                printf("%04X+%04X=%04X: %s - FALSE POSITIVE!\n", a, b, c, str);
                            /*else
                                printf("%04X+%04X=%04X: %s - incorrect\n", a, b, c, str);*/
                        }
                    }
                    break;
                }
                case StringModeTest_DECIMAL_SUM:
                {
                    const Uint digits = 10;
                    const Uint64 size = 10000000000;
                    char str[digits + strlength(" + ") + digits + strlength(" = ") + digits + 1];
                    bool useNoZeroesInAllButFirstNumber = true;
                    for (Uint64 ii=0;; ii++)
                    {
                        Uint64 a = IUNI  % size;
                        Uint64 b = IUNI  % size;
                        Uint64 c = (a+b) % size;

                        Uint corruptedDigits;
                        if (testForFalsePositives)
                        {
                            corruptedDigits = IUNI % 4;
                            for (Uint i=0; i<corruptedDigits; i++)
                            {
                                Uint digit = IUNI % digits;
                                int64 div = 1;
                                for (Uint j=0; j<digit; j++)
                                    div *= 10;
                                c -= (((int64)c / div) % 10 - (int64)(IUNI % 10)) * div;
                            }
                        }

                        sprintf(str, "%0*llu + %0*llu = %0*llu", digits, a, digits, b, digits, c);

                        bool wrong;
                        if (testForFalsePositives)
                            wrong = (a + b) % size != c;
                        else
                            wrong = false;

                        const char *returnMatch;
                        size_t returnMatchLength;
                        if (!regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                        {
                            if (!wrong)
                                printf("%s - FALSE NEGATIVE!\n", str);
                            /*else
                                printf("%s - correct\n", str);*/
                        }
                        else
                        {
                            if (wrong)
                                printf("%s - FALSE POSITIVE!\n", str);
                            /*else
                                printf("%s - incorrect\n", str);*/
                        }

                        if (ii%0x10000==0)
                            printf("%llu\n", ii);
                    }
                    break;
                }
                case StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_ALLOWED:
                case StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_PROHIBITED:
                {
                    const char *returnMatch;
                    size_t returnMatchLength;
                    Uint i;
                    const Uint maxZeroPadding = 4;
                    char str[maxZeroPadding + strlength("4294967295") + 1];
                    for (i=0; i<256; i++)
                        for (Uint j=0; j<=maxZeroPadding; j++)
                        {
                            memset(str, '0', maxZeroPadding);
                            sprintf(str+j, "%u", i);
                            const bool shouldMatch = stringModeTest==StringModeTest_DECIMAL_BYTE__LEADING_ZEROES_ALLOWED ? true : j == 0;
                            if (!testForFalsePositives && !shouldMatch)
                                continue;
                            if (regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength) != shouldMatch)
                                printf("%s - FALSE %s!\n", str, shouldMatch ? "NEGATIVE" : "POSITIVE");
                        }
                        if (testForFalsePositives)
                            for (;; i++)
                                for (Uint j=0; j<=maxZeroPadding; j++)
                                {
                                    memset(str, '0', maxZeroPadding);
                                    sprintf(str+j, "%u", i);
                                    if (regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                                        printf("%s - FALSE POSITIVE!\n", str);
                                }
                    break;
                }
                case StringModeTest_SMOOTH_NUMBERS:
                {
                    // see https://codegolf.stackexchange.com/questions/36384/find-the-smoothest-number/
                    const Uint64 maxsize = 256; // maximum length of string to test as input
                    const char numeral = '1';
                    char *str = new char [maxsize+1];
                    memset(str, numeral, 2+1+2);
                    str[2+1+2] = '\0';
                    for (Uint64 i=2+1+2; i < maxsize;)
                    {
                        for (Uint64 j=2; j<=(i-1)/2; j++)
                        {
                            const Uint64 k = i-1-j;
                            str[j] = ',';

                            const char *returnMatch;
                            size_t returnMatchLength;
                            if (!regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength))
                                printf("%llu, %llu - NON-MATCH!\n", j, k);
                            else
                            if (inrangex64(j, returnMatch-str, returnMatch-str + returnMatchLength))
                                printf("%llu, %llu -> %llu,%llu - INCORRECT! (delimiter included in match)\n", j, k, j - (returnMatch-str), returnMatch-str + returnMatchLength - (j+1));
                            else
                            if (!inrange64(returnMatchLength, j, k))
                                printf("%llu, %llu -> %" PRIsize_t " - INCORRECT! (outside range)\n", j, k, returnMatchLength);
                            else
                            {
                                Uint64 smallestLargestPrimeFactor = k;
                                for (Uint64 n=k; n>=j; n--)
                                {
                                    Uint64 p = largestPrimeFactor(n);
                                    if (smallestLargestPrimeFactor > p)
                                        smallestLargestPrimeFactor = p;
                                }
                                Uint64 returned_largestPrimeFactor = largestPrimeFactor(returnMatchLength);
                                if (returned_largestPrimeFactor != smallestLargestPrimeFactor)
                                    printf("%llu, %llu -> %" PRIsize_t " (largest prime factor %llu) - INCORRECT! (should have smallest largest prime factor %llu)\n", j, k, returnMatchLength, returned_largestPrimeFactor, smallestLargestPrimeFactor);
                            }

                            str[j] = numeral;
                        }
                        str[i++] = numeral;
                        str[i  ] = '\0';
                    }
                    delete [] str;
                    break;
                }
                case StringModeTest_TRIANGULAR_TABLE:
                case StringModeTest_TRIANGULAR_TABLE2:
                {
                    Uint64 possibleMatchesCount;
                    Uint64 *possibleMatchesCount_ptr = countPossibleMatches ? &possibleMatchesCount : NULL;

                    Uint64 maxSize = 64;
                    const char numeral = 'x';
                    char *str = (char*)malloc(maxSize*2+1+1);
                    for (Uint64 n = testNumInc ? testNum0 : 1;; n++)
                    {
                        if (n>1)
                        {
                            putchar('\n');
                            if (lineBuffered)
                                fflush(stdout);
                        }
                        if (testNumInc && n > testNum1)
                            break;
                        if (n > maxSize)
                        {
                            maxSize *= 2;
                            str = (char*)realloc(str, maxSize*2+1+1);
                        }
                        for (Uint64 k=1; k<=n; k++)
                        {
                            if (k>1)
                                putchar(' ');

                            Uint64 a=n, b=k;
                            if (stringModeTest == StringModeTest_TRIANGULAR_TABLE2) {a=k; b=n;}
                            memset(str    , numeral, a); str[a    ] = ',';
                            memset(str+a+1, numeral, b); str[a+1+b] = 0;

                            const char *returnMatch;
                            size_t returnMatchLength;
                            bool matched = regex.MatchString(str, showMatch_backrefIndex, returnMatch, returnMatchLength, possibleMatchesCount_ptr);
                            if (countPossibleMatches)
                                printf("%4llu", *possibleMatchesCount_ptr);
                            else if (matched)
                                printf("%4" PRIsize_t, returnMatchLength);
                        }
                    }
                    break;
                }
                default:
                {
                    Uint64 possibleMatchesCount;
                    Uint64 *possibleMatchesCount_ptr = countPossibleMatches ? &possibleMatchesCount : NULL;

                    LineGetter lineGetter(1<<15);
                    const char newline = '\n';
                    for (;;)
                    {
                        char *line = lineGetter.fgets(stdin);
                        if (!line)
                            break;
                        const char *returnMatch;
                        size_t returnMatchLength;
                        bool matched = regex.MatchString(line, showMatch_backrefIndex, returnMatch, returnMatchLength, possibleMatchesCount_ptr);
                        if (invertMatch)
                        {
                            if (!matched)
                            {
                                puts(line);
                                if (lineBuffered)
                                    fflush(stdout);
                            }
                        }
                        else
                        if (countPossibleMatches)
                            printf("%llu\n", *possibleMatchesCount_ptr);
                        else
                        if (matched)
                        {
                            if (showMatch)
                                printf("%.*s\n", returnMatchLength < INT_MAX ? (int)returnMatchLength : INT_MAX, returnMatch);
                            else
                                puts(line);
                            if (lineBuffered)
                                fflush(stdout);
                        }
                    }
                }
            }
        }

        return 0;
    }
    catch (RegexParsingError err)
    {
        fprintf(stderr, "Error parsing regex pattern at offset %" PRIptrdiff_t ": %s\n", err.buf - buf, err.msg);
        return -1;
    }
}
