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

extern Uint debugTrace;
extern bool free_spacing_mode;
extern bool emulate_ECMA_NPCGs;
extern bool allow_empty_character_classes;
extern bool allow_quantifiers_on_assertions;
extern bool allow_molecular_lookahead;
extern bool allow_atomic_groups;
extern bool allow_branch_reset_groups;
extern bool allow_possessive_quantifiers;
extern bool allow_conditionals;
extern bool allow_lookaround_conditionals;
extern bool allow_reset_start;
extern bool enable_persistent_backrefs;
extern Uint optimizationLevel;

enum RegexSymbolType
{
    RegexSymbol_NoOp,
    RegexSymbol_Group,
    RegexSymbol_Verb,
    RegexSymbol_Character,
    RegexSymbol_CharacterClass,
    RegexSymbol_String,
    RegexSymbol_Backref,
    RegexSymbol_ResetStart,
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
    RegexGroup_Atomic,
    RegexGroup_BranchReset,
    RegexGroup_Lookahead,
    RegexGroup_LookaheadMolecular,
    RegexGroup_NegativeLookahead,
    RegexGroup_Conditional,
    RegexGroup_LookaroundConditional,
};

enum RegexVerb
{
    RegexVerb_None,
    RegexVerb_Accept,
    RegexVerb_Fail,
    RegexVerb_Commit,
    RegexVerb_Prune,
    RegexVerb_Skip,
    RegexVerb_Then,
};

class RegexPattern;

template<bool> class RegexMatcher;
template<bool, RegexVerb, const char *> class Backtrack_Verb;
template<bool> class Backtrack_Skip;
template<bool> class Backtrack_AtomicCapture;
template<bool> class Backtrack_SkipGroup;
template<bool> class Backtrack_EnterGroup;
template<bool> class Backtrack_BeginAtomicGroup;
template<bool> class Backtrack_SelfCapture;
template<bool> class Backtrack_LeaveGroup;
template<bool> class Backtrack_LeaveCaptureGroup;
template<bool> class Backtrack_LoopGroup;
template<bool, bool> class Backtrack_LeaveCaptureGroup_Base;
template<bool> class Backtrack_LeaveGroupLazily;
template<bool> class Backtrack_LeaveCaptureGroupLazily;
template<bool> class Backtrack_LeaveMolecularLookahead;
template<bool> class Backtrack_TryMatch;
template<bool> class Backtrack_ResetStart;

class RegexSymbol
{
    friend class Regex;
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class Backtrack_SkipGroup<false>;
    friend class Backtrack_SkipGroup<true>;
    friend class Backtrack_EnterGroup<false>;
    friend class Backtrack_EnterGroup<true>;
    friend class Backtrack_LeaveGroupLazily<false>;
    friend class Backtrack_LeaveGroupLazily<true>;
    friend class Backtrack_LoopGroup<false>;
    friend class Backtrack_LoopGroup<true>;
    friend class Backtrack_TryMatch<false>;
    friend class Backtrack_TryMatch<true>;
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
            bool possessive;
            bool characterAny;
            char character;
        };
        size_t strLength;
        RegexVerb verb;
    };
    RegexSymbolType type;
public:
    RegexSymbol() {}
    RegexSymbol(RegexSymbolType type) : initMatchFunction(NULL), type(type), minCount(1), maxCount(1), lazy(false), possessive(false) {}
};

class RegexPattern
{
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class Backtrack_EnterGroup<false>;
    friend class Backtrack_EnterGroup<true>;
    friend class Backtrack_LeaveGroupLazily<false>;
    friend class Backtrack_LeaveGroupLazily<true>;
    friend class Backtrack_LoopGroup<false>;
    friend class Backtrack_LoopGroup<true>;
    RegexSymbol **symbols; // list terminated with NULL
};

class RegexGroup : public RegexSymbol
{
    friend class Regex;
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class Backtrack_SkipGroup<false>;
    friend class Backtrack_SkipGroup<true>;
    friend class Backtrack_EnterGroup<false>;
    friend class Backtrack_EnterGroup<true>;
    friend class Backtrack_LeaveGroup<false>;
    friend class Backtrack_LeaveGroup<true>;
    friend class Backtrack_LeaveGroupLazily<false>;
    friend class Backtrack_LeaveGroupLazily<true>;
    friend class Backtrack_LeaveMolecularLookahead<false>;
    friend class Backtrack_LeaveMolecularLookahead<true>;
    friend class Backtrack_LoopGroup<false>;
    friend class Backtrack_LoopGroup<true>;
    friend class Backtrack_AtomicCapture<false>;
    friend class Backtrack_AtomicCapture<true>;
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
    friend class Backtrack_LeaveGroup<false>;
    friend class Backtrack_LeaveGroup<true>;
    friend class Backtrack_LeaveGroupLazily<false>;
    friend class Backtrack_LeaveGroupLazily<true>;
    friend class Backtrack_LeaveCaptureGroup_Base<true, false>;
    friend class Backtrack_LeaveCaptureGroup_Base<true, true >;
    friend class Backtrack_LoopGroup<false>;
    friend class Backtrack_LoopGroup<true>;
    Uint backrefIndex; // zero-numbered; 0 corresponds to \1
public:
    RegexGroupCapturing(Uint backrefIndex) : RegexGroup(RegexGroup_Capturing), backrefIndex(backrefIndex) {}
};

class RegexConditional : public RegexGroup
{
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    Uint backrefIndex; // zero-numbered; 0 corresponds to \1
public:
    RegexConditional(Uint backrefIndex) : RegexGroup(RegexGroup_Conditional), backrefIndex(backrefIndex) {}
};

class RegexLookaroundConditional : public RegexGroup
{
    friend class RegexParser;
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    RegexGroup *lookaround;
public:
    RegexLookaroundConditional(RegexGroup *lookaround) : RegexGroup(RegexGroup_LookaroundConditional), lookaround(lookaround) {}
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
