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

#include <stack>
#include <vector>
#include <queue>
#include <limits.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "tools.h"

extern bool debugTrace;
extern bool free_spacing_mode;
extern bool emulate_ECMA_NPCGs;
extern bool allow_empty_character_classes;
extern Uint optimizationLevel;

enum RegexSymbolType
{
    RegexSymbol_Group,
    RegexSymbol_Character,
    RegexSymbol_CharacterClass,
    RegexSymbol_String,
    RegexSymbol_Backref,
    RegexSymbol_AnchorStart,
    RegexSymbol_AnchorEnd,
    RegexSymbol_WordBoundaryNot,
    RegexSymbol_WordBoundary,
    RegexSymbol_DigitNot,
    RegexSymbol_Digit,
    RegexSymbol_SpaceNot,
    RegexSymbol_Space,
    RegexSymbol_WordCharacterNot,
    RegexSymbol_WordCharacter,

    RegexSymbol_IsPowerOf2,
};

enum RegexGroupType
{
    RegexGroup_NonCapturing,
    RegexGroup_Capturing,
    RegexGroup_Lookahead,
    RegexGroup_LookaheadMolecular,
    RegexGroup_NegativeLookahead,
};

class RegexPattern;

template<bool> class RegexMatcher;
template<bool> class MatchingStack_LookaheadCapture;
template<bool> class MatchingStack_SkipGroup;
template<bool> class MatchingStack_EnterGroup;
template<bool> class MatchingStack_LeaveGroup;
template<bool> class MatchingStack_LoopGroup;
template<bool> class MatchingStack_LeaveGroupLazily;
template<bool> class MatchingStack_LeaveMolecularLookahead;
template<bool> class MatchingStack_TryLazyAlternatives;
template<bool> class MatchingStack_TryMatch;

class RegexSymbol
{
    friend class Regex;
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class MatchingStack_SkipGroup<false>;
    friend class MatchingStack_SkipGroup<true>;
    friend class MatchingStack_EnterGroup<false>;
    friend class MatchingStack_EnterGroup<true>;
    friend class MatchingStack_LoopGroup<false>;
    friend class MatchingStack_LoopGroup<true>;
    friend class MatchingStack_TryMatch<false>;
    friend class MatchingStack_TryMatch<true>;
    const char *originalCode;
    RegexPattern **parentAlternative;
    RegexSymbol  **self;
    union
    {
        void *initMatchFunction;
        void (RegexMatcher<false>::*numberMatchFunction)(RegexSymbol *thisSymbol);
        void (RegexMatcher<true >::*stringMatchFunction)(RegexSymbol *thisSymbol);
    };
    union
    {
        struct
        {
            Uint minCount;
            Uint maxCount;
        };
        char *string;
        RegexSymbol *originalSymbol;
    };
    union
    {
        struct
        {
            bool lazy;
            bool characterAny;
            char character;
        };
        size_t strLength;
    };
    RegexSymbolType type;
public:
    RegexSymbol() {}
    RegexSymbol(RegexSymbolType type) : initMatchFunction(NULL), type(type), minCount(1), maxCount(1), lazy(false) {}
};

class RegexPattern
{
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class MatchingStack_LeaveGroupLazily<false>;
    friend class MatchingStack_LeaveGroupLazily<true>;
    friend class MatchingStack_TryLazyAlternatives<false>;
    friend class MatchingStack_TryLazyAlternatives<true>;
    friend class MatchingStack_LoopGroup<false>;
    friend class MatchingStack_LoopGroup<true>;
    RegexSymbol **symbols; // list terminated with NULL
};

class RegexGroup : public RegexSymbol
{
    friend class Regex;
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class MatchingStack_SkipGroup<false>;
    friend class MatchingStack_SkipGroup<true>;
    friend class MatchingStack_EnterGroup<false>;
    friend class MatchingStack_EnterGroup<true>;
    friend class MatchingStack_LeaveGroup<false>;
    friend class MatchingStack_LeaveGroup<true>;
    friend class MatchingStack_LeaveGroupLazily<false>;
    friend class MatchingStack_LeaveGroupLazily<true>;
    friend class MatchingStack_LeaveMolecularLookahead<false>;
    friend class MatchingStack_LeaveMolecularLookahead<true>;
    friend class MatchingStack_TryLazyAlternatives<false>;
    friend class MatchingStack_TryLazyAlternatives<true>;
    friend class MatchingStack_LoopGroup<false>;
    friend class MatchingStack_LoopGroup<true>;
    friend class MatchingStack_LookaheadCapture<false>;
    friend class MatchingStack_LookaheadCapture<true>;
    RegexPattern **alternatives; // list terminated with NULL
    RegexGroupType type;
public:
    RegexGroup(RegexGroupType type) : RegexSymbol(RegexSymbol_Group), type(type) {}
    bool isLookahead()
    {
        return type == RegexGroup_Lookahead || type == RegexGroup_LookaheadMolecular || type == RegexGroup_NegativeLookahead;
    }
};

class RegexGroupCapturing : public RegexGroup
{
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class MatchingStack_LeaveGroup<false>;
    friend class MatchingStack_LeaveGroup<true>;
    Uint backrefIndex; // zero-numbered; 0 corresponds to \1
public:
    RegexGroupCapturing(Uint backrefIndex) : RegexGroup(RegexGroup_Capturing), backrefIndex(backrefIndex) {}
};

class RegexBackref : public RegexSymbol
{
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    Uint index; // zero-numbered; 0 corresponds to \1
public:
    RegexBackref() : RegexSymbol(RegexSymbol_Backref) {}
};

class RegexCharacterClass : public RegexSymbol
{
    Uint8 allowedChars[256/8]; // one bit for every ASCII code
public:
    RegexCharacterClass(Uint8 _allowedChars[256/8]) : RegexSymbol(RegexSymbol_CharacterClass)
    {
        memcpy(allowedChars, _allowedChars, sizeof(allowedChars));
    }
    bool8 isInClass(char ch)
    {
        Uint8 c = ch;
        return allowedChars[c/8] & (1 << (c%8));
    }
};

class RegexParsingError
{
public:
    const char *buf;
    const char *msg;
    RegexParsingError(const char *buf, const char *msg) : buf(buf), msg(msg) {}
};
class RegexInternalError {};

#ifdef _MSC_VER
#define THROW_ENGINEBUG __debugbreak()
#else
#define THROW_ENGINEBUG throw RegexInternalError();
#endif
