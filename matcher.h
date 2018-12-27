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

template <bool> class MatchingStackNode;
template <bool> class RegexMatcher;
class GroupStackNode;

template <bool USE_STRINGS>
class MatchingStack
{
#ifdef _DEBUG
    MatchingStack<USE_STRINGS> &stack;
    Uint64 stackDepth;
#endif
    // todo: fix the problem that a constant chunk size limits the maximum number of capture groups (to something very very large, but still)
    enum { CHUNK_SIZE = 256*1024 };
    Uint8 *firstChunk;
    Uint8 *chunkBase;
    Uint8 *pendingChunkDeletion;
    MatchingStackNode<USE_STRINGS> *nextToBePopped;

    struct ChunkInfo
    {
        Uint8 *baseOfPreviousChunk;
        MatchingStackNode<USE_STRINGS> *previousNode;
    };

public:
    MatchingStack()
        : pendingChunkDeletion(NULL)
#ifdef _DEBUG
        , stack(*this), stackDepth(0)
#endif
    {
        firstChunk = (Uint8*)malloc(CHUNK_SIZE);
        chunkBase = firstChunk;
        nextToBePopped = (MatchingStackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
    }
    ~MatchingStack()
    {
        free(chunkBase); // assume that flush() has already been called
    }
    void flush();
    bool empty()
    {
        return nextToBePopped == (MatchingStackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
    }
    template <class NODE_TYPE> NODE_TYPE *push(size_t size);
    template <class NODE_TYPE> NODE_TYPE *push() { return push<NODE_TYPE>(sizeof(NODE_TYPE)); }
    void pop(RegexMatcher<USE_STRINGS> &matcher, bool delayChunkDeletion = false);
    void deletePendingChunk()
    {
        if (pendingChunkDeletion)
        {
            free(pendingChunkDeletion);
            pendingChunkDeletion = NULL;
        }
    }
    MatchingStackNode<USE_STRINGS> &operator*()
    {
        return *nextToBePopped;
    }
    MatchingStackNode<USE_STRINGS> *operator->()
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
};

template <bool USE_STRINGS>
class RegexMatcher : public RegexMatcherBase<USE_STRINGS>
{
    friend class MatchingStack<USE_STRINGS>;
    friend class MatchingStackNode<USE_STRINGS>;
    friend class MatchingStack_LookaheadCapture<USE_STRINGS>;
    friend class MatchingStack_SkipGroup<USE_STRINGS>;
    friend class MatchingStack_EnterGroup<USE_STRINGS>;
    friend class MatchingStack_LeaveGroup<USE_STRINGS>;
    friend class MatchingStack_LeaveGroupLazily<USE_STRINGS>;
    friend class MatchingStack_LeaveMolecularLookahead<USE_STRINGS>;
    friend class MatchingStack_TryLazyAlternatives<USE_STRINGS>;
    friend class MatchingStack_LoopGroup<USE_STRINGS>;
    friend class MatchingStack_LoopGroupGreedily<USE_STRINGS>;
    friend class MatchingStack_TryMatch<USE_STRINGS>;

#ifdef _DEBUG
    RegexMatcher<USE_STRINGS> &matcher;
#endif

    Uint64 input;
    Uint64 *captures;

    MatchingStack<USE_STRINGS> stack;
    Uint *captureStackBase;
    Uint *captureStackTop;
    GroupStackNode *groupStackBase;
    GroupStackNode *groupStackTop;

    Uint64 position;
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
    void leaveGroup(MatchingStack_LeaveGroup<USE_STRINGS> *pushStack, Uint64 pushPosition);
    void leaveLazyGroup();
    void leaveMaxedOutGroup();
    void *loopGroup(MatchingStack_LoopGroup<USE_STRINGS> *pushLoop, size_t privateSpace, Uint64 pushPosition);

    inline void initInput(Uint64 _input, Uint numCaptureGroups);
    inline void  readCapture(Uint index, Uint64 &multiple, const char *&pBackref);
    inline void writeCapture(Uint index, Uint64  multiple, const char * pBackref);
    inline void writeCaptureRelative(Uint index, Uint64 start, Uint64 end);

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
    void matchSymbol_Character_or_Backref (RegexSymbol *thisSymbol, Uint64 multiple, MATCH_TYPE pBackref);

    inline void (RegexMatcher<USE_STRINGS>::*&matchFunction(RegexSymbol *thisSymbol))(RegexSymbol *thisSymbol);
    inline bool characterCanMatch(RegexSymbol *thisSymbol);
    inline bool8 characterClassCanMatch(RegexCharacterClass *thisSymbol);
    inline void (RegexMatcher<USE_STRINGS>::*chooseBuiltinCharacterClassFunction(bool (*characterMatchFunction)(Uchar ch), void (RegexMatcher<USE_STRINGS>::*matchFunction)(RegexSymbol *thisSymbol)))(RegexSymbol *thisSymbol);
    inline void virtualizeSymbols(RegexGroup *rootGroup);

    inline void fprintCaptures(FILE *f);

public:
    inline RegexMatcher();
    inline ~RegexMatcher();
    bool Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
};

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

template<> inline void RegexMatcher<false>::writeCaptureRelative(Uint index, Uint64 start, Uint64 end)
{
    captures[index] = end - start;
}
template<> inline void RegexMatcher<true>::writeCaptureRelative(Uint index, Uint64 start, Uint64 end)
{
    captures      [index] = end - start;
    captureOffsets[index] = stringToMatchAgainst + start;
}

class GroupStackNode
{
    friend class RegexMatcher<false>;
    friend class RegexMatcher<true>;
    friend class MatchingStackNode<false>;
    friend class MatchingStackNode<true>;
    friend class MatchingStack_LookaheadCapture<false>;
    friend class MatchingStack_LookaheadCapture<true>;
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
    friend class MatchingStack_LoopGroupGreedily<false>;
    friend class MatchingStack_LoopGroupGreedily<true>;
    friend class MatchingStack_TryMatch<false>;
    friend class MatchingStack_TryMatch<true>;

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

template <bool> class MatchingStack_ConnectingChunk;

template <bool USE_STRINGS>
class MatchingStackNode
{
    friend class RegexMatcher<USE_STRINGS>;
    friend class MatchingStack<USE_STRINGS>;
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)=0;
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)=0; // returns true if the popping can finish with this one
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)=0;
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)=0; // returns the numCaptured delta
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)=0;
};

template <bool USE_STRINGS>
void MatchingStack<USE_STRINGS>::flush()
{
    while (chunkBase != firstChunk)
    {
        ChunkInfo *node = (ChunkInfo*)(chunkBase + CHUNK_SIZE - sizeof(ChunkInfo));
        Uint8 *oldChunk = chunkBase;
        chunkBase = node->baseOfPreviousChunk;
        free(oldChunk);
    }
    nextToBePopped = (MatchingStackNode<USE_STRINGS>*)(chunkBase + CHUNK_SIZE);
}

template <bool USE_STRINGS>
template <class NODE_TYPE> NODE_TYPE *MatchingStack<USE_STRINGS>::push(size_t size)
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
    return new(nextToBePopped = (MatchingStackNode<USE_STRINGS>*)newNode) NODE_TYPE();
}

template <bool USE_STRINGS>
void MatchingStack<USE_STRINGS>::pop(RegexMatcher<USE_STRINGS> &matcher, bool delayChunkDeletion/* = false*/)
{
#ifdef _DEBUG
    stackDepth++;
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
    nextToBePopped = (MatchingStackNode<USE_STRINGS>*)next;
}

template <bool USE_STRINGS>
class MatchingStack_LookaheadCapture : public MatchingStackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    Uint numCaptured;
    RegexPattern **parentAlternative;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        for (Uint i=0; i<numCaptured; i++)
            matcher.captures[*--matcher.captureStackTop] = NON_PARTICIPATING_CAPTURE_GROUP;
        matcher.groupStackTop->numCaptured -= numCaptured;
        matcher.alternative = parentAlternative;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        for (Uint i=0; i<numCaptured; i++)
            matcher.captures[*--matcher.captureStackTop] = NON_PARTICIPATING_CAPTURE_GROUP;
    }
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        return (int)numCaptured;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
};

template <bool USE_STRINGS>
class MatchingStack_SkipGroup : public MatchingStackNode<USE_STRINGS>
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
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
};

template <bool USE_STRINGS>
class MatchingStack_EnterGroup : public MatchingStackNode<USE_STRINGS>
{
    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        RegexGroup *group = matcher.groupStackTop->group;
#ifdef _DEBUG
        if (matcher.groupStackTop->numCaptured)
            THROW_ENGINEBUG;
#endif
        matcher.groupStackTop--;
        matcher.alternative = group->parentAlternative;
        matcher.position = matcher.groupStackTop[+1].position;
        if (group->type == RegexGroup_NegativeLookahead)
        {
            // if we've reached here, it means no match was found inside the negative lookahead, which makes it a match outside
            matcher.symbol = group->self + 1;
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
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop--;
        return 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return true;
    }
};

template <bool USE_STRINGS>
class MatchingStack_LeaveMolecularLookahead : public MatchingStackNode<USE_STRINGS>
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
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        return 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
};

template <bool USE_STRINGS>
class MatchingStack_LeaveGroup : public MatchingStackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
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
        matcher.groupStackTop[-1].numCaptured -= numCaptured;
        popCaptureGroup(matcher);
        matcher.alternative = group->alternatives + alternative;
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        popCaptureGroup(matcher);
    }
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        matcher.groupStackTop++;
        matcher.groupStackTop->group = group;
        return group->type == RegexGroup_Capturing ? 1 : 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
};

template <bool USE_STRINGS>
class MatchingStack_LeaveGroupLazily : public MatchingStack_LeaveGroup<USE_STRINGS>
{
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        MatchingStack_LeaveGroup<USE_STRINGS>::popTo(matcher);
        matcher.groupStackTop->loopCount++;
        matcher.position = matcher.groupStackTop->position;
        matcher.symbol = (*matcher.alternative)->symbols;
        matcher.currentMatch = ULLONG_MAX;
        return true;
    }
};

template <bool USE_STRINGS>
class MatchingStack_TryLazyAlternatives : public MatchingStackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    Uint64 position;
    Uint alternative;

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return sizeof(*this);
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        Uint numCaptured = matcher.groupStackTop->numCaptured;
        for (Uint i=0; i<numCaptured; i++)
        {
            Uint index = matcher.captureStackTop[(int)i - (int)numCaptured];
            matcher.captures[index] = NON_PARTICIPATING_CAPTURE_GROUP;
        }
        matcher.captureStackTop -= numCaptured;

        matcher.groupStackTop->position = position;
        matcher.groupStackTop->loopCount--;
        matcher.position = position;
        matcher.alternative = matcher.groupStackTop->group->alternatives + alternative + 1;
        if (*matcher.alternative)
        {
            matcher.symbol = (*matcher.alternative)->symbols;
            matcher.currentMatch = ULLONG_MAX;
            return true;
        }
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
    }
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
    }
};

template <bool USE_STRINGS>
class MatchingStack_LoopGroup : public MatchingStackNode<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;

protected:
    Uint64 position;
    Uint numCaptured;
    Uint8 buffer[1]; // variable number of elements

    static size_t get_size(Uint numCaptured, size_t privateSpace)
    {
        return (size_t)&((MatchingStack_LoopGroup*)0)->buffer + privateSpace + (sizeof(Uint64) + (USE_STRINGS ? sizeof(const char*) : 0) + sizeof(Uint))*numCaptured;
    }

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        return get_size(numCaptured, 0);
    }
    void popTo(RegexMatcher<USE_STRINGS> &matcher, size_t privateSpace)
    {
        Uint64 *values = (Uint64*)(buffer + privateSpace);
        const char **offsets;
        Uint *indexes;
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
        matcher.groupStackTop->position    = position;
        matcher.groupStackTop->numCaptured = numCaptured;
        matcher.groupStackTop->loopCount--;

        for (Uint i=0; i<numCaptured; i++)
        {
            *matcher.captureStackTop++ = indexes[i];
            matcher.writeCapture(indexes[i], values[i], USE_STRINGS ? offsets[i] : NULL);
        }
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher, size_t privateSpace)
    {
        Uint64 *values = (Uint64*)(buffer + privateSpace);
        const char **offsets;
        Uint *indexes;
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

    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        popTo(matcher, 0);
        return false;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        popForNegativeLookahead(matcher, 0);
    }
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        return -(int)numCaptured;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return true;
    }
};

template <bool USE_STRINGS>
class MatchingStack_LoopGroupGreedily : public MatchingStack_LoopGroup<USE_STRINGS>
{
    friend class RegexMatcher<USE_STRINGS>;
    // this class must have no member variables

    virtual size_t getSize(RegexMatcher<USE_STRINGS> &matcher)
    {
        RegexGroup *group = matcher.groupStackTop->group;
        return this->get_size(this->numCaptured, sizeof(Uint64) + sizeof(Uint64));
    }
    virtual bool popTo(RegexMatcher<USE_STRINGS> &matcher)
    {
        MatchingStack_LoopGroup<USE_STRINGS>::popTo(matcher, sizeof(Uint64) + sizeof(Uint64)); // first term has sizeof(Uint64) instead of sizeof(Uint) for alignment
        matcher.alternative = matcher.groupStackTop->group->alternatives + *(Uint*)this->buffer;
        matcher.groupStackTop->position = ((Uint64*)this->buffer)[1];
        matcher.position = this->position;
        matcher.leaveGroup(matcher.stack.template push< MatchingStack_LeaveGroup<USE_STRINGS> >(), matcher.groupStackTop->position);
        return true;
    }
    virtual void popForNegativeLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        MatchingStack_LoopGroup<USE_STRINGS>::popForNegativeLookahead(matcher, sizeof(Uint64) + sizeof(Uint64)); // first term has sizeof(Uint64) instead of sizeof(Uint) for alignment
    }
};

template <bool USE_STRINGS>
class MatchingStack_TryMatch : public MatchingStackNode<USE_STRINGS>
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
    virtual int popForLookahead(RegexMatcher<USE_STRINGS> &matcher)
    {
        return 0;
    }
    virtual bool okayToTryAlternatives(RegexMatcher<USE_STRINGS> &matcher)
    {
        return false;
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
}

template<> inline RegexMatcher<false>::~RegexMatcher()
{
    delete [] groupStackBase;
    delete [] captureStackBase;
    delete [] captures;
}
template<> inline RegexMatcher<true>::~RegexMatcher()
{
    delete [] groupStackBase;
    delete [] captureStackBase;
    delete [] captures;
    delete [] captureOffsets;
}

#pragma warning(pop)
