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

#define NON_PARTICIPATING_CAPTURE_GROUP ULLONG_MAX

#pragma warning(push)
#pragma warning(disable : 4355)

extern RegexPattern *nullAlternative;

template <bool> class BacktrackNode;
template <bool> class RegexMatcher;
class GroupStackNode;

template <bool USE_STRINGS>
class Backtrack
{
#ifdef _DEBUG
    Backtrack<USE_STRINGS> &stack;
    Uint64 stackDepth;
#endif
    // todo: fix the problem that a constant chunk size limits the maximum number of capture groups (to something very very large, but still)
    enum { CHUNK_SIZE = 256*1024 };
    Uint8 *firstChunk;
    Uint8 *chunkBase;
    Uint8 *pendingChunkDeletion;
    BacktrackNode<USE_STRINGS> *nextToBePopped;

    struct ChunkInfo
    {
        Uint8 *baseOfPreviousChunk;
        BacktrackNode<USE_STRINGS> *previousNode;
    };

public:
    Backtrack()
        : pendingChunkDeletion(NULL)
#ifdef _DEBUG
        , stack(*this), stackDepth(0)
#endif
    {
        firstChunk = (Uint8*)malloc(CHUNK_SIZE);
        chunkBase = firstChunk;
        nextToBePopped = (BacktrackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
    }
    ~Backtrack()
    {
        free(chunkBase); // assume that flush() has already been called
    }
    void flush();
    bool empty()
    {
        return nextToBePopped == (BacktrackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
    }
    template <class NODE_TYPE> NODE_TYPE *push(size_t size);
    template <class NODE_TYPE> NODE_TYPE *push() { return push<NODE_TYPE>(sizeof(NODE_TYPE)); }
    void pop(RegexMatcher<USE_STRINGS> &matcher, bool delayChunkDeletion = false);
    void fprint(RegexMatcher<USE_STRINGS> &matcher, FILE *f);
    void deletePendingChunk()
    {
        if (pendingChunkDeletion)
        {
            free(pendingChunkDeletion);
            pendingChunkDeletion = NULL;
        }
    }
    BacktrackNode<USE_STRINGS> &operator*()
    {
        return *nextToBePopped;
    }
    BacktrackNode<USE_STRINGS> *operator->()
    {
        return nextToBePopped;
    }
#ifdef _DEBUG
    Uint64 getStackDepth()
    {
        return stackDepth;
    }
#endif
};

extern const char Backtrack_VerbName_Commit[];
extern const char Backtrack_VerbName_Prune [];
extern const char Backtrack_VerbName_Skip  [];
extern const char Backtrack_VerbName_Then  [];

struct captureTuple
{
    Uint64 length;
    const char *offset;
    Uint index;
    captureTuple(Uint64 length, const char *offset, Uint index) : length(length), offset(offset), index(index) {}
};

template <bool>
struct RegexMatcherBase
{
};
template <>
struct RegexMatcherBase<false>
{
    friend class Regex;
protected:
    char basicChar;
    bool basicCharIsWordCharacter;
};
template <>
struct RegexMatcherBase<true>
{
    const char *stringToMatchAgainst;
    const char **captureOffsets;
    const char **captureOffsetsAtomicTmp; // only used with enable_persistent_backrefs
};

template <bool USE_STRINGS>
class RegexMatcher : public RegexMatcherBase<USE_STRINGS>
{
    friend class Backtrack<USE_STRINGS>;
    friend class BacktrackNode<USE_STRINGS>;
    friend class Backtrack_Verb<USE_STRINGS, RegexVerb_Commit, Backtrack_VerbName_Commit>;
    friend class Backtrack_Verb<USE_STRINGS, RegexVerb_Prune , Backtrack_VerbName_Prune >;
    friend class Backtrack_Verb<USE_STRINGS, RegexVerb_Skip  , Backtrack_VerbName_Skip  >;
    friend class Backtrack_Verb<USE_STRINGS, RegexVerb_Then  , Backtrack_VerbName_Then  >;
    friend class Backtrack_Skip<USE_STRINGS>;
    friend class Backtrack_AtomicCapture<USE_STRINGS>;
    friend class Backtrack_SkipGroup<USE_STRINGS>;
    friend class Backtrack_EnterGroup<USE_STRINGS>;
    friend class Backtrack_LeaveGroup<USE_STRINGS>;
    friend class Backtrack_LeaveGroupLazily<USE_STRINGS>;
    friend class Backtrack_LeaveCaptureGroup_Base<false, USE_STRINGS>;
    friend class Backtrack_LeaveCaptureGroup_Base<true , USE_STRINGS>;
    friend class Backtrack_LeaveMolecularLookahead<USE_STRINGS>;
    friend class Backtrack_LoopGroup<USE_STRINGS>;
    friend class Backtrack_TryMatch<USE_STRINGS>;
    friend class Backtrack_ResetStart<USE_STRINGS>;

#ifdef _DEBUG
    RegexMatcher<USE_STRINGS> &matcher;
#endif

    Uint64 input;
    Uint64 *captures;
    Uint captureIndexNumUsedAtomicTmp; // only used with enable_persistent_backrefs
    bool *captureIndexUsedAtomicTmp; // only used with enable_persistent_backrefs
    Uint *captureIndexesAtomicTmp; // only used with enable_persistent_backrefs
    Uint64 *capturesAtomicTmp; // only used with enable_persistent_backrefs

    RegexVerb verb; // can only be RegexVerb_None, RegexVerb_Commit, RegexVerb_Prune, RegexVerb_Skip, or RegexVerb_Then
    Uint64 skipPosition; // for RegexVerb_Skip
    Backtrack<USE_STRINGS> stack;
    Uint *captureStackBase;
    Uint *captureStackTop;
    GroupStackNode *groupStackBase;
    GroupStackNode *groupStackTop;

    Uint64 position, startPosition;
    Uint64 currentMatch; // ULLONG_MAX means no match has been tried yet
    RegexPattern **alternative;
    RegexSymbol  **symbol;

    Uint64 numSteps;

    char match; // zero = looking for match, negative = match failed, positive = match found
    bool anchored; // indicates whether we can optimize the search by only trying a match at the start

    void nonMatch(bool negativeLookahead = false);
    void yesMatch(Uint64 newPosition, bool haveChoice);
    void pushStack();
    void enterGroup(RegexGroup *group);
    void leaveGroup(Backtrack_LeaveGroup<USE_STRINGS> *pushStack, Uint64 pushPosition);
    void leaveLazyGroup();
    void leaveMaxedOutGroup();
    Backtrack_LoopGroup<USE_STRINGS> *pushStack_LoopGroup();
    void *loopGroup(Backtrack_LoopGroup<USE_STRINGS> *pushLoop, Uint64 pushPosition, Uint64 oldPosition, Uint alternativeNum);
    void popAtomicGroup(RegexGroup *const group);

    inline void initInput(Uint64 _input, Uint numCaptureGroups);
    inline void  readCapture(Uint index, Uint64 &multiple, const char *&pBackref);
    inline void writeCapture(Uint index, Uint64  multiple, const char * pBackref);
    inline bool writeCaptureRelative(Uint index, Uint64 start, Uint64 end); // returns true iff this changes the capture's value

    void writeCaptureAtomicTmp(captureTuple capture);
    void  readCaptureAtomicTmp(Uint i, Uint &index, Uint64 &length, const char *&offset);

    inline bool doesRepetendMatchOnce(const char *pBackref, Uint64 multiple, Uint64 count);
    inline bool doesRepetendMatchOnce(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count);
    inline bool doesRepetendMatchOnce(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count);

    inline bool doesRepetendMatch(const char *pBackref, Uint64 multiple, Uint64 count);
    inline bool doesRepetendMatch(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count);
    inline bool doesRepetendMatch(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count);

    inline void countRepetendMatches(const char *pBackref, Uint64 multiple);
    inline void countRepetendMatches(bool (*matchFunction)(Uchar ch), Uint64 multiple);
    inline void countRepetendMatches(RegexCharacterClass *charClass, Uint64 multiple);

    inline bool doesStringMatch(RegexSymbol *stringSymbol);
    inline bool matchWordBoundary();

    void matchSymbol_AlwaysMatch          (RegexSymbol *thisSymbol);
    void matchSymbol_NeverMatch           (RegexSymbol *thisSymbol);
    void matchSymbol_String               (RegexSymbol *thisSymbol);
    void matchSymbol_Character            (RegexSymbol *thisSymbol);
    void matchSymbol_CharacterClass       (RegexSymbol *thisSymbol);
    void matchSymbol_Backref              (RegexSymbol *thisSymbol);
    void matchSymbol_Group                (RegexSymbol *thisSymbol);
    void matchSymbol_Verb_Accept          (RegexSymbol *thisSymbol);
    void matchSymbol_Verb_Commit          (RegexSymbol *thisSymbol);
    void matchSymbol_Verb_Prune           (RegexSymbol *thisSymbol);
    void matchSymbol_Verb_Skip            (RegexSymbol *thisSymbol);
    void matchSymbol_Verb_Then            (RegexSymbol *thisSymbol);
    void matchSymbol_ResetStart           (RegexSymbol *thisSymbol);
    void matchSymbol_AnchorStart          (RegexSymbol *thisSymbol);
    void matchSymbol_AnchorEnd            (RegexSymbol *thisSymbol);
    void matchSymbol_WordBoundaryNot      (RegexSymbol *thisSymbol);
    void matchSymbol_WordBoundary         (RegexSymbol *thisSymbol);
    void matchSymbol_DigitNot             (RegexSymbol *thisSymbol);
    void matchSymbol_Digit                (RegexSymbol *thisSymbol);
    void matchSymbol_SpaceNot             (RegexSymbol *thisSymbol);
    void matchSymbol_Space                (RegexSymbol *thisSymbol);
    void matchSymbol_WordCharacterNot     (RegexSymbol *thisSymbol);
    void matchSymbol_WordCharacter        (RegexSymbol *thisSymbol);
    void matchSymbol_IsPowerOf2           (RegexSymbol *thisSymbol);

    template <typename MATCH_TYPE>
    inline bool runtimeOptimize_matchSymbol_Character_or_Backref(RegexSymbol *const thisSymbol, Uint64 const multiple, MATCH_TYPE const repetend);
    template <typename MATCH_TYPE>
    void matchSymbol_Character_or_Backref (RegexSymbol *thisSymbol, Uint64 multiple, MATCH_TYPE pBackref);

    inline void (RegexMatcher<USE_STRINGS>::*&matchFunction(RegexSymbol *thisSymbol))(RegexSymbol *thisSymbol);
    inline bool characterCanMatch(RegexSymbol *thisSymbol);
    inline bool8 characterClassCanMatch(RegexCharacterClass *thisSymbol);
    inline void (RegexMatcher<USE_STRINGS>::*chooseBuiltinCharacterClassFunction(bool (*characterMatchFunction)(Uchar ch), void (RegexMatcher<USE_STRINGS>::*matchFunction)(RegexSymbol *thisSymbol)))(RegexSymbol *thisSymbol);
    inline bool staticallyOptimizeGroup(RegexSymbol **thisSymbol);
    inline void virtualizeSymbols(RegexGroup *rootGroup);

    inline void fprintCapture(FILE *f, Uint i);
    inline void fprintCapture(FILE *f, Uint64 length, const char *offset);

public:
    inline RegexMatcher();
    inline ~RegexMatcher();
    bool Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
};

template <> void RegexMatcher<false>::fprintCapture(FILE *f, Uint64 length, const char *offset);
template <> void RegexMatcher<false>::fprintCapture(FILE *f, Uint i);
template <> void RegexMatcher<true>::fprintCapture(FILE *f, Uint64 length, const char *offset);
template <> void RegexMatcher<true>::fprintCapture(FILE *f, Uint i);

template<> inline void RegexMatcher<false>::readCapture(Uint index, Uint64 &multiple, const char *&pBackref)
{
    multiple = captures[index];
}
template<> inline void RegexMatcher<true>::readCapture(Uint index, Uint64 &multiple, const char *&pBackref)
{
    multiple = captures      [index];
    pBackref = captureOffsets[index];
}

template<> inline void RegexMatcher<false>::writeCapture(Uint index, Uint64 multiple, const char *pBackref)
{
    captures[index] = multiple;
}
template<> inline void RegexMatcher<true>::writeCapture(Uint index, Uint64 multiple, const char *pBackref)
{
    captures      [index] = multiple;
    captureOffsets[index] = pBackref;
}

template<> inline bool RegexMatcher<false>::writeCaptureRelative(Uint index, Uint64 start, Uint64 end)
{
    Uint64 prevLength = captures[index];
    captures[index] = end - start;
    return captures[index] != prevLength;
}
template<> inline bool RegexMatcher<true>::writeCaptureRelative(Uint index, Uint64 start, Uint64 end)
{
    Uint64      prevLength = captures      [index];
    const char *prevOffset = captureOffsets[index];
    captures      [index] = end - start;
    captureOffsets[index] = stringToMatchAgainst + start;
    return captures[index] != prevLength || captureOffsets[index] != prevOffset;
}

class GroupStackNode
{
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class BacktrackNode<false>;
    friend class BacktrackNode<true>;
    friend class Backtrack_AtomicCapture<false>;
    friend class Backtrack_AtomicCapture<true>;
    friend class Backtrack_SkipGroup<false>;
    friend class Backtrack_SkipGroup<true>;
    friend class Backtrack_EnterGroup<false>;
    friend class Backtrack_EnterGroup<true>;
    friend class Backtrack_LeaveGroup<false>;
    friend class Backtrack_LeaveGroup<true>;
    friend class Backtrack_LeaveGroupLazily<false>;
    friend class Backtrack_LeaveGroupLazily<true>;
    friend class Backtrack_LeaveCaptureGroup_Base<true, false>;
    friend class Backtrack_LeaveCaptureGroup_Base<true, true >;
    friend class Backtrack_LeaveMolecularLookahead<false>;
    friend class Backtrack_LeaveMolecularLookahead<true>;
    friend class Backtrack_LoopGroup<false>;
    friend class Backtrack_LoopGroup<true>;
    friend class Backtrack_TryMatch<false>;
    friend class Backtrack_TryMatch<true>;

    union
    {
        struct // for the matcher parsing stage
        {
            size_t numAnchoredAlternatives;
            bool currentAlternativeAnchored;
        };
        struct // for the matching stage
        {
            Uint64 position;
            Uint64 loopCount; // how many times "group" has been matched
        };
    };
    RegexGroup *group;
    Uint numCaptured; // how many capture groups inside this group (including nested groups) have been pushed onto the capture stack
};

template <bool USE_STRINGS>
class BacktrackNode
{
    friend class RegexMatcher<USE_STRINGS>;
    friend class Backtrack<USE_STRINGS>;
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)=0;
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)=0; // returns true if the popping can finish with this one
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)=0;
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)=0; // returns the numCaptured delta
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)=0;
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)=0;
    virtual bool isAtomicGroup()
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)=0;
};

template <bool USE_STRINGS>
void Backtrack<USE_STRINGS>::flush()
{
    while (chunkBase != firstChunk)
    {
        ChunkInfo *node = (ChunkInfo*)(chunkBase + CHUNK_SIZE - sizeof(ChunkInfo));
        Uint8 *oldChunk = chunkBase;
        chunkBase = node->baseOfPreviousChunk;
        free(oldChunk);
    }
    nextToBePopped = (BacktrackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
}

template <bool USE_STRINGS>
template <class NODE_TYPE> NODE_TYPE *Backtrack<USE_STRINGS>::push(size_t size)
{
#ifdef _DEBUG
    stackDepth++;
#endif
    Uint8 *newNode = (Uint8*)nextToBePopped - size;
    if (newNode < chunkBase)
    {
        Uint8 *newChunk = (Uint8*)malloc(CHUNK_SIZE);
        ChunkInfo *node = (ChunkInfo*)(newChunk + CHUNK_SIZE - sizeof(ChunkInfo));
        node->baseOfPreviousChunk = chunkBase;
        node->previousNode = nextToBePopped;
        chunkBase = newChunk;
        newNode = (Uint8*)node - size;
    }
    return new(nextToBePopped = (BacktrackNode<USE_STRINGS>*)newNode) NODE_TYPE();
}

template <bool USE_STRINGS>
void Backtrack<USE_STRINGS>::pop(RegexMatcher<USE_STRINGS> &matcher, bool delayChunkDeletion/* = false*/)
{
#ifdef _DEBUG
    stackDepth--;
#endif
    Uint8 *next = (Uint8*)nextToBePopped + nextToBePopped->getSize(matcher);
    if (next == chunkBase + CHUNK_SIZE - sizeof(ChunkInfo) && chunkBase != firstChunk)
    {
        ChunkInfo *node = (ChunkInfo*)next;
        Uint8 *oldChunk = chunkBase;
        chunkBase = node->baseOfPreviousChunk;
        nextToBePopped = node->previousNode;
        if (delayChunkDeletion)
            pendingChunkDeletion = oldChunk;
        else
            free(oldChunk);
        return;
    }
    nextToBePopped = (BacktrackNode<USE_STRINGS>*)next;
}

template <bool USE_STRINGS>
void Backtrack<USE_STRINGS>::fprint(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
{
    BacktrackNode<USE_STRINGS> *nextPop = nextToBePopped;
    Uint8 *base = chunkBase;
    while (nextPop != (BacktrackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE))
    {
        nextPop->fprintDebug(matcher, f);

        Uint8 *next = (Uint8*)nextPop + nextPop->getSize(matcher);
        if (next == base + CHUNK_SIZE - sizeof(ChunkInfo) && base != firstChunk)
        {
            ChunkInfo *node = (ChunkInfo*)next;
            Uint8 *oldChunk = base;
            base = node->baseOfPreviousChunk;
            nextPop = node->previousNode;
        }
        else
            nextPop = (BacktrackNode<USE_STRINGS>*)next;
    }
}

template <bool USE_STRINGS, RegexVerb verb, const char *name>
class Backtrack_Verb : public BacktrackNode<USE_STRINGS>
{
protected:
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (matcher.verb == RegexVerb_None)
            matcher.verb = verb;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fputs(name, f);
        fputc('\n', f);
    }
};

template <bool USE_STRINGS>
class Backtrack_Commit : public Backtrack_Verb<USE_STRINGS, RegexVerb_Commit, Backtrack_VerbName_Commit> {};

template <bool USE_STRINGS>
class Backtrack_Prune : public Backtrack_Verb<USE_STRINGS, RegexVerb_Prune, Backtrack_VerbName_Prune> {};

template <bool USE_STRINGS>
class Backtrack_Skip : public Backtrack_Verb<USE_STRINGS, RegexVerb_Skip, Backtrack_VerbName_Skip>
{
    friend class RegexMatcher<USE_STRINGS>;
    Uint64 skipPosition;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (matcher.verb == RegexVerb_None)
        {
            matcher.verb = RegexVerb_Skip;
            matcher.skipPosition = skipPosition;
        }
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_Skip: position=%llu\n", skipPosition);
    }
};

template <bool USE_STRINGS>
class Backtrack_Then : public Backtrack_Verb<USE_STRINGS, RegexVerb_Then, Backtrack_VerbName_Then> {};

#pragma warning(push)
#pragma warning(disable : 4700) // for passing "offsets" to fprintCapture() with USE_STRINGS=false

template <bool USE_STRINGS>
class Backtrack_AtomicCapture : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    RegexPattern **parentAlternative;
    Uint numCaptured;
    Uint8 buffer[FLEXIBLE_SIZE_ARRAY];

    static size_t get_size(Uint numCaptured)
    {
        return (size_t)&((Backtrack_AtomicCapture*)0)->buffer + (enable_persistent_backrefs ? (sizeof(Uint64) + (USE_STRINGS ? sizeof(const char*) : 0) + sizeof(Uint))*numCaptured : 0);
    }

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return get_size(numCaptured);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (enable_persistent_backrefs)
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

            for (Uint i=0; i<numCaptured; i++)
            {
                matcher.writeCapture(indexes[i], values[i], USE_STRINGS ? offsets[i] : NULL);
                if (values[i] == NON_PARTICIPATING_CAPTURE_GROUP)
                {
#ifdef _DEBUG
                    if (matcher.captureStackTop[-1] != indexes[i])
                        THROW_ENGINEBUG;
#endif
                    matcher.captureStackTop--;
                    matcher.groupStackTop->numCaptured--;
                }
            }
        }
        else
        {
            for (Uint i=0; i<numCaptured; i++)
                matcher.captures[*--matcher.captureStackTop] = NON_PARTICIPATING_CAPTURE_GROUP;
            matcher.groupStackTop->numCaptured -= numCaptured;
        }
        matcher.alternative = parentAlternative;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (enable_persistent_backrefs)
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

            for (Uint i=0; i<numCaptured; i++)
            {
                if (values[i] == NON_PARTICIPATING_CAPTURE_GROUP)
                {
#ifdef _DEBUG
                    if (matcher.captureStackTop[-1] != indexes[i])
                        THROW_ENGINEBUG;
#endif
                    matcher.captureStackTop--;
                    matcher.groupStackTop->numCaptured--;
                }
            }
        }
        else
        {
            for (Uint i=0; i<numCaptured; i++)
                matcher.captures[*--matcher.captureStackTop] = NON_PARTICIPATING_CAPTURE_GROUP;
        }
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return (int)numCaptured;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        const Uint64 *values = (Uint64*)buffer;
        const char **offsets;
        const Uint *indexes;
        if (!USE_STRINGS)
            indexes = (Uint*)(values + numCaptured);
        else
        {
            offsets = (const char **)(values + numCaptured);
            indexes = (Uint*)(offsets + numCaptured);
        }

        return USE_STRINGS ? captureTuple(values[captureNum], offsets[captureNum], indexes[captureNum])
                           : captureTuple(values[captureNum], NULL               , indexes[captureNum]);
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }

    void transfer(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (!enable_persistent_backrefs)
            return;

        numCaptured = matcher.captureIndexNumUsedAtomicTmp;

        const char *&dummy = (const char *&)buffer;
        Uint64 *values = (Uint64*)buffer;
        const char **offsets;
        Uint *indexes;
        if (!USE_STRINGS)
            indexes = (Uint*)(values + numCaptured);
        else
        {
            offsets = (const char **)(values + numCaptured);
            indexes = (Uint*)(offsets + numCaptured);
        }

        for (Uint i=0; i<matcher.captureIndexNumUsedAtomicTmp; i++)
            matcher.readCaptureAtomicTmp(i, indexes[i], values[i], USE_STRINGS ? offsets[i] : dummy);
        matcher.captureIndexNumUsedAtomicTmp = 0;
    }

    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fputs("Backtrack_AtomicCapture: ", f);
        if (!enable_persistent_backrefs)
            fprintf(f, "numCaptured=%u\n", numCaptured);
        else
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

            for (Uint i=0; i<numCaptured; i++)
            {
                if (i != 0)
                    fputs(", ", f);
                fprintf(f, "\\%u=", indexes[i]+1);
                matcher.fprintCapture(f, values[i], USE_STRINGS ? offsets[i] : NULL);
            }
            fputc('\n', f);
        }
    }
};

#pragma warning(pop)

template <bool USE_STRINGS>
class Backtrack_SkipGroup : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    Uint64 position;
    RegexGroup *group;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.position = position;
        matcher.enterGroup(group);
        matcher.currentMatch = ULLONG_MAX;
        return true;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_SkipGroup: position=%llu\n", position);
    }
};

template <bool USE_STRINGS>
class Backtrack_EnterGroup : public BacktrackNode<USE_STRINGS>
{
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        RegexGroup *const group = matcher.groupStackTop->group;
#ifdef _DEBUG
        if (enable_persistent_backrefs ? matcher.groupStackTop->numCaptured != (matcher.groupStackTop->loopCount > 1) : matcher.groupStackTop->numCaptured)
            THROW_ENGINEBUG;
#endif
        matcher.groupStackTop--;
        matcher.alternative = group->parentAlternative;
        matcher.position = matcher.groupStackTop[+1].position;
        if (group->type == RegexGroup_NegativeLookahead)
        {
            // if we've reached here, it means no match was found inside the negative lookahead, which makes it a match outside
            if (!group->self) // group->self will be NULL if this is the lookaround in a conditional
            {
                if (debugTrace)
                    fputs("\n\n""Non-match found inside negative lookahead conditional, resulting in a match outside it; jumping to \"yes\" alternative", stderr);
                matcher.symbol = (*matcher.alternative)->symbols;
            }
            else
            {
                if (debugTrace)
                    fputs("\n\n""Non-match found inside negative lookahead, resulting in a match outside it", stderr);
                matcher.symbol = group->self + 1;
            }
            matcher.currentMatch = ULLONG_MAX;
            return true;
        }
        if (matcher.groupStackTop->group->type == RegexGroup_LookaroundConditional && group == ((RegexLookaroundConditional*)matcher.groupStackTop->group)->lookaround)
        {
            if (debugTrace)
                fputs("\n\n""Non-match found inside lookaround conditional; jumping to \"no\" alternative", stderr);
            matcher.alternative++;
            matcher.symbol = *matcher.alternative ? (*matcher.alternative)->symbols : &nullSymbol;
            matcher.currentMatch = ULLONG_MAX;
            return true;
        }
        if (matcher.groupStackTop[+1].loopCount > group->minCount && group->minCount != group->maxCount)
        {
            matcher.symbol = group->self + 1;
            matcher.currentMatch = ULLONG_MAX;
            return true;
        }
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop--;
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop--;
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return true;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_EnterGroup\n");
    }
};

template <bool USE_STRINGS>
class Backtrack_BeginAtomicGroup : public BacktrackNode<USE_STRINGS>
{
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return true;
    }
    virtual bool isAtomicGroup()
    {
        return true;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_BeginAtomicGroup\n");
    }
};

template <bool USE_STRINGS>
class Backtrack_LeaveMolecularLookahead : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    Uint64 position;
    Uint numCaptured;
    Uint alternative;
    RegexGroup *group;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->position    = position;
        matcher.groupStackTop->group       = group;
        matcher.groupStackTop->numCaptured = numCaptured;
        matcher.groupStackTop[-1].numCaptured -= numCaptured;
        matcher.alternative = group->alternatives + alternative;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_LeaveMolecularLookahead: position=%llu, numCaptured=%u, alternative=%u\n", position, numCaptured, alternative);
    }
};

template <bool USE_STRINGS>
class Backtrack_LeaveGroup : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    friend class Backtrack_LeaveCaptureGroup<false>;
    friend class Backtrack_LeaveCaptureGroup<true>;
    friend class Backtrack_LeaveCaptureGroupLazily<false>;
    friend class Backtrack_LeaveCaptureGroupLazily<true>;
    Uint64 position;
    Uint64 loopCount;
    Uint numCaptured;
    Uint alternative;

    void popCaptureGroup(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (group->type == RegexGroup_Capturing)
        {
            Uint backrefIndex = ((RegexGroupCapturing*)group)->backrefIndex;
            matcher.captures[backrefIndex] = NON_PARTICIPATING_CAPTURE_GROUP;
            matcher.captureStackTop--;
            matcher.groupStackTop[-1].numCaptured--;
#ifdef _DEBUG
            if (*matcher.captureStackTop != backrefIndex)
                THROW_ENGINEBUG;
#endif
        }
    }
protected:
    RegexGroup *group;

    virtual void popCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
    }

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->position    = position;
        matcher.groupStackTop->loopCount   = loopCount;
        matcher.groupStackTop->group       = group;
        matcher.groupStackTop->numCaptured = numCaptured;
        if (enable_persistent_backrefs)
            popCapture(matcher);
        else
        {
            matcher.groupStackTop[-1].numCaptured -= numCaptured;
            popCaptureGroup(matcher);
        }
        if (group->type == RegexGroup_Atomic)
        {
            matcher.alternative = &nullAlternative;
            return false;
        }
        matcher.alternative = group->alternatives + alternative;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        if (enable_persistent_backrefs)
            popCapture(matcher);
        else
            popCaptureGroup(matcher);
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        return group->type == RegexGroup_Capturing ? 1 : 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        UNREACHABLE_CODE;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebugBase(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, ": position=%llu, loopCount=%llu, numCaptured=%u, alternative=%u", position, loopCount, numCaptured, alternative);
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveGroup", f);
        fprintDebugBase(matcher, f);
        fputc('\n', f);
    }
};

template <bool USE_STRINGS>
class Backtrack_LeaveGroupLazily : public Backtrack_LeaveGroup<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;

protected:
    Uint64 positionDiff; // could be a boolean, but would that mess with alignment?

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        Backtrack_LeaveGroup<USE_STRINGS>::popTo(matcher);

        if (matcher.groupStackTop->loopCount == MAX_EXTEND(this->group->maxCount) ||
            positionDiff == 0 && matcher.groupStackTop->group->maxCount == UINT_MAX && matcher.groupStackTop->loopCount >= matcher.groupStackTop->group->minCount)
        {
            matcher.position = matcher.groupStackTop->position -= positionDiff;
            matcher.alternative = &nullAlternative;
            this->popCapture(matcher);
            return false;
        }

        matcher.position = matcher.groupStackTop->position;
        matcher.loopGroup(matcher.pushStack_LoopGroup(), matcher.position, matcher.position - positionDiff, (Uint)(matcher.alternative - matcher.groupStackTop->group->alternatives));
        return true;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveGroupLazily", f);
        Backtrack_LeaveGroup<USE_STRINGS>::fprintDebugBase(matcher, f);
        fprintf(f, ", positionDiff=%llu\n", positionDiff);
    }
};

template <bool FORWARD_CAPTURE, bool USE_STRINGS> class Backtrack_LeaveCaptureGroup_Base;
template <> class Backtrack_LeaveCaptureGroup_Base<false, false> {};
template <> class Backtrack_LeaveCaptureGroup_Base<false, true > {};
template <> class Backtrack_LeaveCaptureGroup_Base<true, false> // only used in enable_persistent_backrefs mode
{
    friend class RegexMatcher                     <false>;
    friend class Backtrack_LeaveCaptureGroup      <false>;
    friend class Backtrack_LeaveCaptureGroupLazily<false>;
    Uint64 length;

    void setCapture(RegexMatcher<false> &matcher)
    {
        length = matcher.captures[((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex];
    }
    void popCapture(RegexMatcher<false> &matcher)
    {
        Uint backrefIndex = ((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex;
        matcher.captures[backrefIndex] = length;
        if (length == NON_PARTICIPATING_CAPTURE_GROUP)
        {
#ifdef _DEBUG
            if (matcher.captureStackTop[-1] != backrefIndex)
                THROW_ENGINEBUG;
#endif
            matcher.captureStackTop--;
            matcher.groupStackTop[-1].numCaptured--;
        }
    }
    captureTuple popForAtomicForwardCapture(RegexMatcher<false> &matcher, Uint captureNum)
    {
        return captureTuple(length, NULL, ((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex);
    }
    void fprintCapture(RegexMatcher<false> &matcher, FILE *f)
    {
        fputs(", precapture=", f);
        matcher.fprintCapture(f, length, NULL);
        fputc('\n', f);
    }
};
template <> class Backtrack_LeaveCaptureGroup_Base<true, true> // only used in enable_persistent_backrefs mode
{
    friend class RegexMatcher                     <true>;
    friend class Backtrack_LeaveCaptureGroup      <true>;
    friend class Backtrack_LeaveCaptureGroupLazily<true>;
    Uint64 length;
    const char *offset;

    void setCapture(RegexMatcher<true> &matcher)
    {
        offset = matcher.captureOffsets[((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex];
        length = matcher.captures      [((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex];
    }
    void popCapture(RegexMatcher<true> &matcher)
    {
        Uint backrefIndex = ((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex;
        matcher.captures[backrefIndex] = length;
        if (length == NON_PARTICIPATING_CAPTURE_GROUP)
        {
#ifdef _DEBUG
            if (matcher.captureStackTop[-1] != backrefIndex)
                THROW_ENGINEBUG;
#endif
            matcher.captureStackTop--;
            matcher.groupStackTop[-1].numCaptured--;
        }
        else
            matcher.captureOffsets[backrefIndex] = offset;
    }
    captureTuple popForAtomicForwardCapture(RegexMatcher<true> &matcher, Uint captureNum)
    {
        return captureTuple(length, offset, ((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex);
    }
    void fprintCapture(RegexMatcher<true> &matcher, FILE *f)
    {
        fputs(", precapture=", f);
        matcher.fprintCapture(f, length, offset);
        fputc('\n', f);
    }
};

template <>
class Backtrack_LeaveCaptureGroup<false> : public Backtrack_LeaveGroup<false>, public Backtrack_LeaveCaptureGroup_Base<true, false> // only used in enable_persistent_backrefs mode
{
    virtual void popCapture(RegexMatcher<false> &matcher)
    {
        return Backtrack_LeaveCaptureGroup_Base::popCapture(matcher);
    }
    virtual size_t getSize(RegexMatcher<false> &matcher)
    {
        return sizeof(*this);
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<false> &matcher, Uint captureNum)
    {
        return Backtrack_LeaveCaptureGroup_Base::popForAtomicForwardCapture(matcher, captureNum);
    }
    virtual void fprintDebug(RegexMatcher<false> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveCaptureGroup", f);
        Backtrack_LeaveGroup<false>::fprintDebugBase(matcher, f);
        fprintCapture(matcher, f);
    }
};

template <>
class Backtrack_LeaveCaptureGroupLazily<false> : public Backtrack_LeaveGroupLazily<false>, public Backtrack_LeaveCaptureGroup_Base<true, false> // only used in enable_persistent_backrefs mode
{
    virtual void popCapture(RegexMatcher<false> &matcher)
    {
        return Backtrack_LeaveCaptureGroup_Base::popCapture(matcher);
    }
    virtual size_t getSize(RegexMatcher<false> &matcher)
    {
        return sizeof(*this);
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<false> &matcher, Uint captureNum)
    {
        return Backtrack_LeaveCaptureGroup_Base::popForAtomicForwardCapture(matcher, captureNum);
    }
    virtual void fprintDebug(RegexMatcher<false> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveCaptureGroupLazily", f);
        Backtrack_LeaveGroup<false>::fprintDebugBase(matcher, f);
        fprintf(f, ", positionDiff=%llu", positionDiff);
        fprintCapture(matcher, f);
    }
};

template <>
class Backtrack_LeaveCaptureGroup<true> : public Backtrack_LeaveGroup<true>, public Backtrack_LeaveCaptureGroup_Base<true, true> // only used in enable_persistent_backrefs mode
{
    virtual void popCapture(RegexMatcher<true> &matcher)
    {
        return Backtrack_LeaveCaptureGroup_Base::popCapture(matcher);
    }
    virtual size_t getSize(RegexMatcher<true> &matcher)
    {
        return sizeof(*this);
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<true> &matcher, Uint captureNum)
    {
        return Backtrack_LeaveCaptureGroup_Base::popForAtomicForwardCapture(matcher, captureNum);
    }
    virtual void fprintDebug(RegexMatcher<true> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveCaptureGroup", f);
        Backtrack_LeaveGroup<true>::fprintDebugBase(matcher, f);
        fprintCapture(matcher, f);
    }
};

template <>
class Backtrack_LeaveCaptureGroupLazily<true> : public Backtrack_LeaveGroupLazily<true>, public Backtrack_LeaveCaptureGroup_Base<true, true> // only used in enable_persistent_backrefs mode
{
    virtual void popCapture(RegexMatcher<true> &matcher)
    {
        return Backtrack_LeaveCaptureGroup_Base::popCapture(matcher);
    }
    virtual size_t getSize(RegexMatcher<true> &matcher)
    {
        return sizeof(*this);
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<true> &matcher, Uint captureNum)
    {
        return Backtrack_LeaveCaptureGroup_Base::popForAtomicForwardCapture(matcher, captureNum);
    }
    virtual void fprintDebug(RegexMatcher<true> &matcher, FILE *f)
    {
        fputs("Backtrack_LeaveCaptureGroupLazily", f);
        Backtrack_LeaveGroup<true>::fprintDebugBase(matcher, f);
        fprintf(f, ", positionDiff=%llu\n", positionDiff);
        fprintCapture(matcher, f);
    }
};

#pragma warning(push)
#pragma warning(disable : 4700) // for passing "offsets" to fprintCapture() with USE_STRINGS=false

template <bool USE_STRINGS>
class Backtrack_LoopGroup : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    friend class Backtrack_LeaveGroupLazily<USE_STRINGS>;

protected:
    Uint64 position;
    Uint numCaptured;
    Uint alternative;
    Uint64 oldPosition;
    Uint8 buffer[FLEXIBLE_SIZE_ARRAY];

    static size_t get_size(Uint numCaptured)
    {
        return (size_t)&((Backtrack_LoopGroup*)0)->buffer +
            (enable_persistent_backrefs ? (sizeof(Uint64) + (USE_STRINGS ? sizeof(const char*) : 0)               )*numCaptured
                                        : (sizeof(Uint64) + (USE_STRINGS ? sizeof(const char*) : 0) + sizeof(Uint))*numCaptured);
    }

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return get_size(numCaptured);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        const RegexGroup *group = matcher.groupStackTop->group;
        if (!enable_persistent_backrefs)
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

#ifdef _DEBUG
            if (matcher.groupStackTop->numCaptured)
                THROW_ENGINEBUG;
#endif
            matcher.groupStackTop->numCaptured = numCaptured;

            for (Uint i=0; i<numCaptured; i++)
            {
                *matcher.captureStackTop++ = indexes[i];
                matcher.writeCapture(indexes[i], values[i], USE_STRINGS ? offsets[i] : NULL);
            }
        }
        else
        if (group->type == RegexGroup_Capturing)
        {
            Uint backrefIndex = ((RegexGroupCapturing*)group)->backrefIndex;
            if (numCaptured != 0)
            {
                const char *&dummy = (const char *&)buffer;
                if (!USE_STRINGS)
                    matcher.writeCapture(backrefIndex, *(Uint64*)(buffer                      ), dummy);
                else
                    matcher.writeCapture(backrefIndex, *(Uint64*)(buffer + sizeof(const char*)), *(const char**)buffer);
            }
            else
            {
#ifdef _DEBUG
                if (matcher.captureStackTop[-1] != backrefIndex)
                    THROW_ENGINEBUG;
#endif
                matcher.captures[backrefIndex] = NON_PARTICIPATING_CAPTURE_GROUP;
                matcher.captureStackTop--;
                matcher.groupStackTop->numCaptured--;
            }
        }
        matcher.groupStackTop->position = position;
        matcher.groupStackTop->loopCount--;

        matcher.alternative = matcher.groupStackTop->group->alternatives + alternative;
        matcher.groupStackTop->position = oldPosition;
        matcher.position = position;
        if (matcher.groupStackTop->group->lazy || matcher.groupStackTop->loopCount < matcher.groupStackTop->group->minCount)
            return false;
        matcher.leaveMaxedOutGroup();
        return true;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        if (!enable_persistent_backrefs)
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

            for (Uint i=0; i<numCaptured; i++)
                *matcher.captureStackTop++ = indexes[i];
        }
        else
        if (matcher.groupStackTop->group->type == RegexGroup_Capturing)
        {
            if (numCaptured == 0)
            {
                Uint backrefIndex = ((RegexGroupCapturing*)matcher.groupStackTop->group)->backrefIndex;
                matcher.captures[backrefIndex] = NON_PARTICIPATING_CAPTURE_GROUP;
                matcher.captureStackTop--;
                matcher.groupStackTop->numCaptured--;
            }
        }
    }

    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return -(int)numCaptured;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        return captureTuple(0, NULL, 0);
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return true;
    }
    void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_LoopGroup: position=%llu, numCaptured=%u, alternative=%u, oldPosition=%llu", position, numCaptured, alternative, oldPosition);

        if (!enable_persistent_backrefs)
        {
            const Uint64 *values = (Uint64*)buffer;
            const char **offsets;
            const Uint *indexes;
            if (!USE_STRINGS)
                indexes = (Uint*)(values + numCaptured);
            else
            {
                offsets = (const char **)(values + numCaptured);
                indexes = (Uint*)(offsets + numCaptured);
            }

            for (Uint i=0; i<numCaptured; i++)
            {
                fprintf(f, ", \\%u=", indexes[i]+1);
                matcher.fprintCapture(f, values[i], offsets[i]);
            }
        }
        else
        if (numCaptured != 0)
        {
            fputs(", capture=", f);
            if (!USE_STRINGS)
                matcher.fprintCapture(f, *(Uint64*)(buffer                      ), NULL);
            else
                matcher.fprintCapture(f, *(Uint64*)(buffer + sizeof(const char*)), *(const char**)buffer);
        }
        fputc('\n', f);
    }
};

#pragma warning(pop)

template <bool USE_STRINGS>
class Backtrack_TryMatch : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;

    Uint64 position;
    Uint64 currentMatch; // ULLONG_MAX means no match has been tried yet
    RegexSymbol *symbol;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.position     = position;
        matcher.currentMatch = currentMatch;
        matcher.alternative  = symbol->parentAlternative;
        matcher.symbol       = symbol->self;
        return true;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        return captureTuple(0, NULL, 0);
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_TryMatch: position=%llu, currentMatch=%llu\n", position, currentMatch);
    }
};

template <bool USE_STRINGS>
class Backtrack_ResetStart : public BacktrackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;

    Uint64 startPosition;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.startPosition = startPosition;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForAtomicCapture(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual captureTuple popForAtomicForwardCapture(RegexMatcher<USE_STRINGS> &matcher, Uint captureNum)
    {
        return captureTuple(0, NULL, 0);
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
    virtual void fprintDebug(RegexMatcher<USE_STRINGS> &matcher, FILE *f)
    {
        fprintf(f, "Backtrack_ResetStart: startPosition=%llu\n", startPosition);
    }
};

template<> inline RegexMatcher<false>::RegexMatcher() :
    groupStackBase(NULL),
    captureStackBase(NULL)
#ifdef _DEBUG
    ,matcher(*this)
#endif
{
    captures = NULL;
    if (enable_persistent_backrefs)
    {
        captureIndexUsedAtomicTmp = NULL;
        captureIndexesAtomicTmp = NULL;
        capturesAtomicTmp = NULL;
    }
}
template<> inline RegexMatcher<true>::RegexMatcher() :
    groupStackBase(NULL),
    captureStackBase(NULL)
#ifdef _DEBUG
    ,matcher(*this)
#endif
{
    captures = NULL;
    captureOffsets = NULL;
    if (enable_persistent_backrefs)
    {
        captureIndexUsedAtomicTmp = NULL;
        captureIndexesAtomicTmp = NULL;
        capturesAtomicTmp = NULL;
        captureOffsetsAtomicTmp = NULL;
    }
}

template<> inline RegexMatcher<false>::~RegexMatcher()
{
    delete [] groupStackBase;
    delete [] captureStackBase;
    delete [] captures;
    if (enable_persistent_backrefs)
    {
        delete [] captureIndexUsedAtomicTmp;
        delete [] captureIndexesAtomicTmp;
        delete [] capturesAtomicTmp;
    }
}
template<> inline RegexMatcher<true>::~RegexMatcher()
{
    delete [] groupStackBase;
    delete [] captureStackBase;
    delete [] captures;
    delete [] captureOffsets;
    if (enable_persistent_backrefs)
    {
        delete [] captureIndexUsedAtomicTmp;
        delete [] captureIndexesAtomicTmp;
        delete [] capturesAtomicTmp;
        delete [] captureOffsetsAtomicTmp;
    }
}

#pragma warning(pop)
