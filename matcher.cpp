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
#include "matcher.h"
#include "matcher-optimization.h"

RegexPattern *nullAlternative = NULL;
RegexSymbol  *nullSymbol      = NULL;

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::nonMatch(bool negativeLookahead)
{
    if (debugTrace)
    {
        if (negativeLookahead)
            fputs("Match found inside negative lookahead, resulting in a non-match outside it\n\n", stderr);
        else
            fputs(": non-match", stderr);
    }

    position = groupStackTop->position;

    // if any changes are made here, they may need to be duplicated in Backtrack_Commit<USE_STRINGS>::popTo()
    for (;;)
    {
        if (verb != RegexVerb_None && verb != RegexVerb_Then)
        {
            if (groupStackTop->group->type == RegexGroup_NegativeLookahead && stack->okayToTryAlternatives(*this))
                verb = RegexVerb_None;
        }
        else
        if (*alternative && (stack.empty() || stack->okayToTryAlternatives(*this)) && !inrange(groupStackTop->group->type, RegexGroup_Conditional, RegexGroup_LookaroundConditional))
        {
            alternative++;
            if (*alternative)
            {
                verb = RegexVerb_None;
                position = groupStackTop->position;
                symbol = (*alternative)->symbols;
                currentMatch = ULLONG_MAX;
                return;
            }
        }

        if (stack.empty())
        {
            match = verb == RegexVerb_Commit ? -2 : -1;
            return;
        }

        BacktrackNode<USE_STRINGS> &formerTop = *stack;
        stack.pop(*this, true);
        bool stopHere = formerTop.popTo(*this);
        stack.deletePendingChunk();
        
        if (stopHere && verb == RegexVerb_None)
            break;
    }
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::pushStack()
{
    if  ((*symbol)->possessive)
        return;
    Backtrack_TryMatch<USE_STRINGS> *pushStack = stack.template push< Backtrack_TryMatch<USE_STRINGS> >();
    pushStack->position     = position;
    pushStack->currentMatch = currentMatch;
    pushStack->symbol       = *symbol;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::enterGroup(RegexGroup *group)
{
    RegexPattern **alternativeTmp = group->alternatives;

    if (group->type == RegexGroup_Conditional)
    {
        Uint64 multiple;
        const char *pBackref;
        readCapture(((RegexConditional*)group)->backrefIndex, multiple, pBackref);
        if (multiple == NON_PARTICIPATING_CAPTURE_GROUP)
        {
            alternativeTmp++;
            if (*alternativeTmp == NULL)
            {
                symbol++;
                return;
            }
        }
    }

    alternative = alternativeTmp;
    symbol = (*alternative)->symbols;

    groupStackTop++;
    groupStackTop->position    = position;
    groupStackTop->loopCount   = 1;
    groupStackTop->group       = group;
    groupStackTop->numCaptured = 0;

    if (group->possessive)
        stack.template push< Backtrack_BeginAtomicGroup<USE_STRINGS> >();

    stack.template push< Backtrack_EnterGroup<USE_STRINGS> >();

    if (group->type == RegexGroup_Atomic)
        stack.template push< Backtrack_BeginAtomicGroup<USE_STRINGS> >();
    else
    if (group->type == RegexGroup_LookaroundConditional)
        enterGroup(((RegexLookaroundConditional*)group)->lookaround);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::leaveGroup(Backtrack_LeaveGroup<USE_STRINGS> *pushStack, Uint64 pushPosition)
{
    RegexGroup *const group = groupStackTop->group;

    pushStack->position    = pushPosition;
    pushStack->loopCount   = groupStackTop->loopCount;
    pushStack->group       = group;
    pushStack->numCaptured = groupStackTop->numCaptured;
    pushStack->alternative = (Uint)(alternative - group->alternatives);

    if (group->type == RegexGroup_Capturing)
    {
        Uint backrefIndex = ((RegexGroupCapturing*)group)->backrefIndex;
        Uint64 prevValue = captures[backrefIndex];
        writeCaptureRelative(backrefIndex, groupStackTop->position, position);
        if (!enable_persistent_backrefs || prevValue == NON_PARTICIPATING_CAPTURE_GROUP)
        {
            *captureStackTop++ = backrefIndex;
            groupStackTop->numCaptured++;
        }
    }

    alternative = group->parentAlternative;
    symbol      = group->self + 1;
    groupStackTop[-1].numCaptured += groupStackTop->numCaptured;
    groupStackTop--;
    currentMatch = ULLONG_MAX;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::leaveLazyGroup()
{
    Backtrack_LeaveGroupLazily<USE_STRINGS> *pushStack;
    if (enable_persistent_backrefs && groupStackTop->group->type == RegexGroup_Capturing)
    {
        Backtrack_LeaveCaptureGroupLazily<USE_STRINGS> *pushStackCapture = stack.template push< Backtrack_LeaveCaptureGroupLazily<USE_STRINGS> >();
        pushStackCapture->setCapture(*this);
    }
    else
        pushStack = stack.template push< Backtrack_LeaveGroupLazily<USE_STRINGS> >();
    pushStack->positionDiff = position - groupStackTop->position;
    leaveGroup(pushStack, position);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::leaveMaxedOutGroup()
{
    Backtrack_LeaveGroup<USE_STRINGS> *pushStack;
    RegexGroup *const group = groupStackTop->group;
    bool possessive = group->possessive;
    if (enable_persistent_backrefs && group->type == RegexGroup_Capturing)
    {
        Backtrack_LeaveCaptureGroup<USE_STRINGS> *pushStackCapture = stack.template push< Backtrack_LeaveCaptureGroup<USE_STRINGS> >();
        pushStackCapture->setCapture(*this);
        pushStack = pushStackCapture;
    }
    else
        pushStack = stack.template push< Backtrack_LeaveGroup<USE_STRINGS> >();
    leaveGroup(pushStack, groupStackTop->position);
    if (possessive)
        popAtomicGroup(group);
}

template <bool USE_STRINGS>
Backtrack_LoopGroup<USE_STRINGS> *RegexMatcher<USE_STRINGS>::pushStack_LoopGroup()
{
    Uint64 size;
    int set_numCaptured = -1;
    if (!enable_persistent_backrefs)
        size = Backtrack_LoopGroup<USE_STRINGS>::get_size(groupStackTop->numCaptured);
    else
    {
        Uint backrefIndex = ((RegexGroupCapturing*)groupStackTop->group)->backrefIndex;
        set_numCaptured = (groupStackTop->group->type == RegexGroup_Capturing && captures[backrefIndex] != NON_PARTICIPATING_CAPTURE_GROUP) ? 1 : 0;
        size = Backtrack_LoopGroup<USE_STRINGS>::get_size(set_numCaptured);
    }
    Backtrack_LoopGroup<USE_STRINGS> *pushStack = stack.template push< Backtrack_LoopGroup<USE_STRINGS> >(size);
    if (set_numCaptured >= 0)
        pushStack->numCaptured = set_numCaptured;
    return pushStack;
}

template <bool USE_STRINGS>
void *RegexMatcher<USE_STRINGS>::loopGroup(Backtrack_LoopGroup<USE_STRINGS> *pushLoop, Uint64 pushPosition, Uint64 oldPosition, Uint alternativeNum)
{
    groupStackTop->loopCount++;

    pushLoop->position = pushPosition;

    const RegexGroup *group = groupStackTop->group;

    if (!enable_persistent_backrefs)
    {
        const Uint numCaptured = groupStackTop->numCaptured;
        pushLoop->numCaptured = numCaptured;
        groupStackTop->numCaptured = 0;

        const char *&dummy = (const char *&)pushLoop->buffer;
        Uint64 *values = (Uint64*)pushLoop->buffer;
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
        {
            indexes[i] = captureStackTop[(int)i - (int)numCaptured];
            readCapture(indexes[i], values[i], USE_STRINGS ? offsets[i] : dummy);
            if (!enable_persistent_backrefs)
                captures[indexes[i]] = NON_PARTICIPATING_CAPTURE_GROUP;
        }
        captureStackTop -= numCaptured;
    }
    else
    if (group->type == RegexGroup_Capturing)
    {
        Uint backrefIndex = ((RegexGroupCapturing*)group)->backrefIndex;
        if (pushLoop->numCaptured = captures[backrefIndex] != NON_PARTICIPATING_CAPTURE_GROUP)
        {
            const char *&dummy = (const char *&)pushLoop->buffer;
            if (!USE_STRINGS)
                readCapture(backrefIndex, *(Uint64*)(pushLoop->buffer                      ), dummy);
            else
                readCapture(backrefIndex, *(Uint64*)(pushLoop->buffer + sizeof(const char*)), *(const char**)pushLoop->buffer);
        }
        if (writeCaptureRelative(backrefIndex, groupStackTop->position, position) && pushLoop->numCaptured == 0)
        {
            *captureStackTop++ = backrefIndex;
            groupStackTop->numCaptured++;
        }
    }

    alternative = group->alternatives;
    symbol      = group->alternatives[0]->symbols;
    groupStackTop->position = position;
    currentMatch = ULLONG_MAX;

    pushLoop->oldPosition = oldPosition;
    pushLoop->alternative = alternativeNum;

    if (group->type == RegexGroup_Atomic)
        stack.template push< Backtrack_BeginAtomicGroup<USE_STRINGS> >();
    else
    if (group->type == RegexGroup_LookaroundConditional)
        enterGroup(((RegexLookaroundConditional*)group)->lookaround);

    return (void*)pushLoop->buffer;
}

bool matchWordCharacter(Uchar ch);

template<> void RegexMatcher<false>::initInput(Uint64 _input, Uint numCaptureGroups)
{
    input = _input;
    basicCharIsWordCharacter = matchWordCharacter(basicChar);
}
template<> void RegexMatcher<true>::initInput(Uint64 _input, Uint numCaptureGroups)
{
    stringToMatchAgainst = (const char *)_input;
    input = strlen(stringToMatchAgainst);
    delete [] captureOffsets;
    captureOffsets = new const char * [numCaptureGroups];
    for (Uint i=0; i<numCaptureGroups; i++)
        captureOffsets[i] = stringToMatchAgainst;
    if (enable_persistent_backrefs)
    {
        delete [] captureOffsetsAtomicTmp;
        captureOffsetsAtomicTmp = new const char * [numCaptureGroups];
    }
}

template<> bool RegexMatcher<false>::doesRepetendMatch(const char *pBackref, Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<false>::doesRepetendMatch(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<false>::doesRepetendMatch(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<true>::doesRepetendMatch(const char *pBackref, Uint64 multiple, Uint64 count)
{
    if (pBackref)
    {
        const char *s          = stringToMatchAgainst + position;
        const char *upperBound = stringToMatchAgainst + input - multiple;
        for (Uint64 i=0; i < count && s <= upperBound; i++, s+=multiple)
            if (memcmp(s, pBackref, (size_t)multiple)!=0)
                return false;
    }
    return true;
}
template<> bool RegexMatcher<true>::doesRepetendMatch(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count)
{
    const char *s          = stringToMatchAgainst + position;
    const char *upperBound = stringToMatchAgainst + input - 1;
    for (Uint64 i=0; i < count && s <= upperBound; i++, s+=1)
        if (!matchFunction(*s))
            return false;
    return true;
}
template<> bool RegexMatcher<true>::doesRepetendMatch(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count)
{
    const char *s          = stringToMatchAgainst + position;
    const char *upperBound = stringToMatchAgainst + input - 1;
    for (Uint64 i=0; i < count && s <= upperBound; i++, s+=1)
        if (!charClass->isInClass(*s))
            return false;
    return true;
}

template<> bool RegexMatcher<false>::doesRepetendMatchOnce(const char *pBackref, Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<false>::doesRepetendMatchOnce(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<false>::doesRepetendMatchOnce(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count)
{
    return true;
}
template<> bool RegexMatcher<true>::doesRepetendMatchOnce(const char *pBackref, Uint64 multiple, Uint64 count)
{
    if (pBackref)
    {
        const char *s = stringToMatchAgainst + position + count * multiple;
        return memcmp(s, pBackref, (size_t)multiple)==0;
    }
    return true;
}
template<> bool RegexMatcher<true>::doesRepetendMatchOnce(bool (*matchFunction)(Uchar ch), Uint64 multiple, Uint64 count)
{
    return matchFunction(stringToMatchAgainst[position + count]);
}
template<> bool RegexMatcher<true>::doesRepetendMatchOnce(RegexCharacterClass *charClass, Uint64 multiple, Uint64 count)
{
    return !!charClass->isInClass(stringToMatchAgainst[position + count]); // todo: give these functions bool8 return values so that the overhead of the conversion from bool8 to bool can be eliminated
}

template<> void RegexMatcher<false>::countRepetendMatches(const char *pBackref, Uint64 multiple)
{
}
template<> void RegexMatcher<false>::countRepetendMatches(bool (*matchFunction)(Uchar ch), Uint64 multiple)
{
}
template<> void RegexMatcher<false>::countRepetendMatches(RegexCharacterClass *charClass, Uint64 multiple)
{
}
template<> void RegexMatcher<true>::countRepetendMatches(const char *pBackref, Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=multiple)
        if (memcmp(s, pBackref, (size_t)multiple)!=0)
            break;
    currentMatch = count;
}
template<> void RegexMatcher<true>::countRepetendMatches(bool (*matchFunction)(Uchar ch), Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=1)
        if (!matchFunction(*s))
            break;
    currentMatch = count;
}
template<> void RegexMatcher<true>::countRepetendMatches(RegexCharacterClass *charClass, Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=1)
        if (!charClass->isInClass(*s))
            break;
    currentMatch = count;
}

template<> inline bool RegexMatcher<false>::doesStringMatch(RegexSymbol *stringSymbol)
{
    // it can't be a match in the case of !USE_STRINGS, because RegexSymbol_String is only created by the parser if there's more than one kind of character in it
    return false;
}
template<> inline bool RegexMatcher<true>::doesStringMatch(RegexSymbol *stringSymbol)
{
    return position + stringSymbol->strLength <= input && memcmp(stringToMatchAgainst + position, stringSymbol->string, stringSymbol->strLength)==0;
}

template<> bool RegexMatcher<false>::matchWordBoundary()
{
    return basicCharIsWordCharacter && (position==0 || position==input) && input!=0;
}

template<> bool RegexMatcher<true>::matchWordBoundary()
{
    bool lfWord = position==0     ? false : matchWordCharacter(stringToMatchAgainst[position-1]);
    bool rhWord = position==input ? false : matchWordCharacter(stringToMatchAgainst[position  ]);
    return lfWord != rhWord;
}

template<> void (RegexMatcher<false>::*&RegexMatcher<false>::matchFunction(RegexSymbol *thisSymbol))(RegexSymbol *thisSymbol)
{
    return thisSymbol->numberMatchFunction;
}
template<> void (RegexMatcher<true >::*&RegexMatcher<true >::matchFunction(RegexSymbol *thisSymbol))(RegexSymbol *thisSymbol)
{
    return thisSymbol->stringMatchFunction;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch(RegexSymbol *thisSymbol)
{
    symbol++;
}
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_NeverMatch(RegexSymbol *thisSymbol)
{
    nonMatch();
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_String(RegexSymbol *thisSymbol)
{
    if (!doesStringMatch(thisSymbol))
    {
        nonMatch();
        return;
    }
    position += thisSymbol->strLength;
    symbol++;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Character(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, thisSymbol->characterAny ? (const char *)NULL : &thisSymbol->character);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_CharacterClass(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, (RegexCharacterClass*)thisSymbol);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Backref(RegexSymbol *thisSymbol)
{
    Uint64 multiple;
    const char *pBackref;
    readCapture(((RegexBackref*)thisSymbol)->index, multiple, pBackref);
    if (multiple == NON_PARTICIPATING_CAPTURE_GROUP)
    {
        if (!emulate_ECMA_NPCGs && thisSymbol->minCount != 0)
        {
            nonMatch();
            return;
        }
        multiple = 0;
    }
    if (multiple == 0) // don't backtrack when it will make no difference to do so
    {
        symbol++;
        return;
    }
    matchSymbol_Character_or_Backref(thisSymbol, multiple, pBackref);
}

template <bool USE_STRINGS>
template <typename MATCH_TYPE>
void RegexMatcher<USE_STRINGS>::matchSymbol_Character_or_Backref(RegexSymbol *thisSymbol, Uint64 multiple, MATCH_TYPE repetend)
{
    if (currentMatch == ULLONG_MAX)
    {
        if (runtimeOptimize_matchSymbol_Character_or_Backref(thisSymbol, multiple, repetend))
            return;
        if (thisSymbol->lazy)
        {
            currentMatch = thisSymbol->minCount;
            if (!doesRepetendMatch(repetend, multiple, currentMatch))
            {
                nonMatch();
                return;
            }
        }
        else
        {
            if (thisSymbol->maxCount == UINT_MAX)
            {
                Uint64 spaceLeft = input - position;
                currentMatch = spaceLeft / multiple;
                if (currentMatch < thisSymbol->minCount)
                {
                    nonMatch();
                    return;
                }
                if (USE_STRINGS && repetend)
                {
                    countRepetendMatches(repetend, multiple);
                    if (currentMatch < thisSymbol->minCount)
                    {
                        nonMatch();
                        return;
                    }
                }
                if (currentMatch > thisSymbol->minCount)
                    pushStack();
                if (USE_STRINGS)
                    position += currentMatch * multiple;
                else
                    position = input - spaceLeft % multiple;
                currentMatch = ULLONG_MAX;
                symbol++;
                return;
            }
            else
            {
                currentMatch = thisSymbol->maxCount;
                if (USE_STRINGS && repetend)
                {
                    countRepetendMatches(repetend, multiple);
                    if (currentMatch < thisSymbol->minCount)
                    {
                        nonMatch();
                        return;
                    }
                }
            }
        }
    }
    else
        goto try_next_match;
    for (;;)
    {
        {
            Uint64 neededMatch = position + currentMatch * multiple;
            if (input >= neededMatch)
            {
                if (USE_STRINGS && thisSymbol->lazy && currentMatch && !doesRepetendMatchOnce(repetend, multiple, currentMatch-1))
                {
                    nonMatch();
                    return;
                }
                if (currentMatch != (thisSymbol->lazy ? MAX_EXTEND(thisSymbol->maxCount) : thisSymbol->minCount))
                    pushStack();
                position     = neededMatch;
                currentMatch = ULLONG_MAX;
                symbol++;
                return;
            }
        }
        if (thisSymbol->lazy)
        {
            nonMatch();
            return;
        }
    try_next_match:
        if (thisSymbol->lazy)
        {
            if (currentMatch == MAX_EXTEND(thisSymbol->maxCount))
            {
                nonMatch();
                return;
            }
            currentMatch++;
        }
        else
        {
            if (currentMatch == thisSymbol->minCount)
            {
                nonMatch();
                return;
            }
            currentMatch--;
        }
    }
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Group(RegexSymbol *thisSymbol)
{
    RegexGroup *group = (RegexGroup*)thisSymbol;
    if (group->maxCount == 0)
    {
        symbol++;
        return;
    }
    if (group->lazy && group->minCount == 0)
    {
        Backtrack_SkipGroup<USE_STRINGS> *pushStack = stack.template push< Backtrack_SkipGroup<USE_STRINGS> >();
        pushStack->position = position;
        pushStack->group    = group;
        symbol++;
        return;
    }
    enterGroup(group);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Accept(RegexSymbol *thisSymbol)
{
    symbol++;
    verb = RegexVerb_Accept;
    symbol = &nullSymbol;
}

const char Backtrack_VerbName_Commit[] = "Backtrack_Commit";
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Commit(RegexSymbol *thisSymbol)
{
    stack.template push< Backtrack_Commit<USE_STRINGS> >();
    symbol++;
}

const char Backtrack_VerbName_Prune[] = "Backtrack_Prune";
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Prune(RegexSymbol *thisSymbol)
{
    stack.template push< Backtrack_Prune<USE_STRINGS> >();
    symbol++;
}

const char Backtrack_VerbName_Skip[] = ""; // dummy string, not actually used; just here to make g++ happy
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Skip(RegexSymbol *thisSymbol)
{
    Backtrack_Skip<USE_STRINGS> *pushStack = stack.template push< Backtrack_Skip<USE_STRINGS> >();
    pushStack->skipPosition = position;
    symbol++;
}

const char Backtrack_VerbName_Then[] = "Backtrack_Then";
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Then(RegexSymbol *thisSymbol)
{
    stack.template push< Backtrack_Then<USE_STRINGS> >();
    symbol++;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_ResetStart(RegexSymbol *thisSymbol)
{
    if (startPosition < position)
    {
        Backtrack_ResetStart<USE_STRINGS> *pushStack = stack.template push< Backtrack_ResetStart<USE_STRINGS> >();
        pushStack->startPosition = startPosition;
        startPosition = position;
    }
    symbol++;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_AnchorStart(RegexSymbol *thisSymbol)
{
    if (position == 0)
    {
        symbol++;
        return;
    }
    nonMatch();
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_AnchorEnd(RegexSymbol *thisSymbol)
{
    if (position == input)
    {
        symbol++;
        return;
    }
    nonMatch();
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundaryNot(RegexSymbol *thisSymbol)
{
    if (!matchWordBoundary())
    {
        symbol++;
        return;
    }
    nonMatch();
    
}
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundary(RegexSymbol *thisSymbol)
{
    if (matchWordBoundary())
    {
        symbol++;
        return;
    }
    nonMatch();
}

bool matchDigitNot        (Uchar ch) {return !(inrange(ch,'0','9')                                                         );}
bool matchDigit           (Uchar ch) {return  (inrange(ch,'0','9')                                                         );}
bool matchSpaceNot        (Uchar ch) {return !(inrange(ch,0x9,0xD) || ch==' ' || ch==(Uchar)' '                            );}
bool matchSpace           (Uchar ch) {return  (inrange(ch,0x9,0xD) || ch==' ' || ch==(Uchar)' '                            );}
bool matchWordCharacterNot(Uchar ch) {return !(inrange(ch,'0','9') || inrange(ch,'A','Z') || inrange(ch,'a','z') || ch=='_');}
bool matchWordCharacter   (Uchar ch) {return  (inrange(ch,'0','9') || inrange(ch,'A','Z') || inrange(ch,'a','z') || ch=='_');}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_DigitNot(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchDigitNot);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Digit(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchDigit);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_SpaceNot(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchSpaceNot);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_Space(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchSpace);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacterNot(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchWordCharacterNot);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}
template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacter(RegexSymbol *thisSymbol)
{
    if (USE_STRINGS)
        matchSymbol_Character_or_Backref(thisSymbol, 1, &matchWordCharacter);
    else
        matchSymbol_Character_or_Backref(thisSymbol, 1, (const char *)NULL);
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_IsPowerOf2(RegexSymbol *thisSymbol)
{
    Uint64 spaceLeft = input - position;
    if ((spaceLeft != 0 || thisSymbol->lazy) && !(spaceLeft & (spaceLeft - 1)))
    {
        symbol++;
        return;
    }
    nonMatch();
}

template<> bool RegexMatcher<false>::characterCanMatch(RegexSymbol *thisSymbol)
{
    if (thisSymbol->characterAny)
        return true;
    return thisSymbol->character == basicChar;
}
template<> bool RegexMatcher<true>::characterCanMatch(RegexSymbol *thisSymbol)
{
    return true;
}

template<> bool8 RegexMatcher<false>::characterClassCanMatch(RegexCharacterClass *thisSymbol)
{
    return thisSymbol->isInClass(basicChar);
}
template<> bool8 RegexMatcher<true>::characterClassCanMatch(RegexCharacterClass *thisSymbol)
{
    return true;
}

template<> void (RegexMatcher<false>::*RegexMatcher<false>::chooseBuiltinCharacterClassFunction(bool (*characterMatchFunction)(Uchar ch), void (RegexMatcher<false>::*matchFunction)(RegexSymbol *thisSymbol)))(RegexSymbol *thisSymbol)
{
    if (characterMatchFunction(basicChar))
        return &RegexMatcher<false>::matchSymbol_Character;
    else
        return &RegexMatcher<false>::matchSymbol_NeverMatch;
}
template<> void (RegexMatcher<true>::*RegexMatcher<true>::chooseBuiltinCharacterClassFunction(bool (*characterMatchFunction)(Uchar ch), void (RegexMatcher<true>::*matchFunction)(RegexSymbol *thisSymbol)))(RegexSymbol *thisSymbol)
{
    return matchFunction;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::virtualizeSymbols(RegexGroup *rootGroup)
{
    // Note that this group iterator code is redundant with that at the end of RegexParser::RegexParser(); todo: Factor it out into a separate function
    RegexPattern **thisAlternative;
    RegexSymbol  **thisSymbol = &(RegexSymbol*&)rootGroup;
    for (;;)
    {
        if (*thisSymbol)
        {
            switch ((*thisSymbol)->type)
            {
            case RegexSymbol_NoOp:
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                break;
            case RegexSymbol_Character:
                if (characterCanMatch(*thisSymbol))
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Character;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_NeverMatch;
                break;
            case RegexSymbol_CharacterClass:
                if (characterClassCanMatch((RegexCharacterClass*)*thisSymbol))
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_CharacterClass;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_NeverMatch;
                break;
            case RegexSymbol_String:
                if (USE_STRINGS)
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_String;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_NeverMatch;
                break;
            case RegexSymbol_Backref:
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Backref;
                break;
            case RegexSymbol_ResetStart:
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_ResetStart;
                break;
            case RegexSymbol_AnchorStart:
                if ((*thisSymbol)->minCount == 0)
                {
                    (*thisSymbol)->type = RegexSymbol_NoOp;
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                }
                else
                {
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AnchorStart;
                    groupStackTop[-1].currentAlternativeAnchored = true;
                }
                break;
            case RegexSymbol_AnchorEnd:
                if ((*thisSymbol)->minCount == 0)
                {
                    (*thisSymbol)->type = RegexSymbol_NoOp;
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                }
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AnchorEnd;
                break;
            case RegexSymbol_WordBoundaryNot:
                if ((*thisSymbol)->minCount == 0)
                {
                    (*thisSymbol)->type = RegexSymbol_NoOp;
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                }
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundaryNot;
                break;
            case RegexSymbol_WordBoundary:
                if ((*thisSymbol)->minCount == 0)
                {
                    (*thisSymbol)->type = RegexSymbol_NoOp;
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                }
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundary;
                break;
            case RegexSymbol_DigitNot:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchDigitNot,         &RegexMatcher<USE_STRINGS>::matchSymbol_DigitNot);
                break;
            case RegexSymbol_Digit:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchDigit,            &RegexMatcher<USE_STRINGS>::matchSymbol_Digit);
                break;
            case RegexSymbol_SpaceNot:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchSpaceNot,         &RegexMatcher<USE_STRINGS>::matchSymbol_SpaceNot);
                break;
            case RegexSymbol_Space:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchSpace,            &RegexMatcher<USE_STRINGS>::matchSymbol_Space);
                break;
            case RegexSymbol_WordCharacterNot:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchWordCharacterNot, &RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacterNot);
                break;
            case RegexSymbol_WordCharacter:
                matchFunction(*thisSymbol++) = chooseBuiltinCharacterClassFunction(matchWordCharacter,    &RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacter);
                break;
            case RegexSymbol_IsPowerOf2:
                if (USE_STRINGS)
                {
                    RegexSymbol *originalSymbol = (*thisSymbol)->originalSymbol;
                    delete *thisSymbol;
                    *thisSymbol = originalSymbol;
                }
                break;
            case RegexSymbol_Verb:
                switch ((*thisSymbol)->verb)
                {
                case RegexVerb_Accept: matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Accept; break;
                case RegexVerb_Fail:   matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_NeverMatch ; break;
                case RegexVerb_Commit: matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Commit; break;
                case RegexVerb_Prune:  matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Prune ; break;
                case RegexVerb_Skip:   matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Skip  ; break;
                case RegexVerb_Then:   matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Verb_Then  ; break;
                default:
                    UNREACHABLE_CODE;
                }
                break;
            case RegexSymbol_Group:
                if (staticallyOptimizeGroup(thisSymbol))
                    break;
                matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_Group;
                RegexGroup *group = (RegexGroup*)(*thisSymbol);
                groupStackTop->numAnchoredAlternatives = 0;
                groupStackTop->currentAlternativeAnchored = false;
                (*groupStackTop++).group = group;
                thisAlternative = group->alternatives;
                thisSymbol      = group->alternatives[0]->symbols;
                if (group->type == RegexGroup_LookaroundConditional)
                {
                    group = ((RegexLookaroundConditional*)group)->lookaround;
                    (*groupStackTop++).group = group;
                    thisAlternative = group->alternatives;
                    thisSymbol      = group->alternatives[0]->symbols;
                }
                break;
            }
        }
        else
        {
            groupStackTop[-1].numAnchoredAlternatives += groupStackTop[-1].currentAlternativeAnchored;
            groupStackTop[-1].currentAlternativeAnchored = false;
            thisAlternative++;
            if (*thisAlternative)
                thisSymbol = (*thisAlternative)->symbols;
            else
            {
                RegexGroup *group = (*--groupStackTop).group;
                anchored = groupStackTop->numAnchoredAlternatives == thisAlternative - group->alternatives;
                if (groupStackTop == groupStackBase)
                    return;
                if (group->minCount && group->type != RegexGroup_NegativeLookahead)
                    groupStackTop[-1].currentAlternativeAnchored |= anchored;
                thisAlternative = group->parentAlternative;
                thisSymbol      = group->self ? group->self + 1 : (*thisAlternative)->symbols; // group->self will be NULL if this is the lookaround in a conditional
            }
        }
    }
}

template<>
void RegexMatcher<false>::writeCaptureAtomicTmp(captureTuple capture)
{
    if (!captureIndexUsedAtomicTmp[capture.index])
    {
        captureIndexUsedAtomicTmp[capture.index] = true;
        captureIndexesAtomicTmp[captureIndexNumUsedAtomicTmp++] = capture.index;
    }
    capturesAtomicTmp[capture.index] = capture.length;
}

template<>
void RegexMatcher<true>::writeCaptureAtomicTmp(captureTuple capture)
{
    if (!captureIndexUsedAtomicTmp[capture.index])
    {
        captureIndexUsedAtomicTmp[capture.index] = true;
        captureIndexesAtomicTmp[captureIndexNumUsedAtomicTmp++] = capture.index;
    }
    capturesAtomicTmp[capture.index] = capture.length;
    captureOffsetsAtomicTmp[capture.index] = capture.offset;
}

template<>
void RegexMatcher<false>::readCaptureAtomicTmp(Uint i, Uint &index, Uint64 &length, const char *&offset)
{
    Uint backrefIndex = captureIndexesAtomicTmp[i];
    index = backrefIndex;
    length = capturesAtomicTmp[backrefIndex];
    captureIndexUsedAtomicTmp[backrefIndex] = false; // erase this capture from the temporary list (but leave it to the caller to do the final step)
}

template<>
void RegexMatcher<true>::readCaptureAtomicTmp(Uint i, Uint &index, Uint64 &length, const char *&offset)
{
    Uint backrefIndex = captureIndexesAtomicTmp[i];
    index = backrefIndex;
    length = capturesAtomicTmp[backrefIndex];
    offset = captureOffsetsAtomicTmp[backrefIndex];
    captureIndexUsedAtomicTmp[backrefIndex] = false; // erase this capture from the temporary list (but leave it to the caller to do the final step)
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::popAtomicGroup(RegexGroup *const group)
{
    int numCapturedDelta = 0;

    for (GroupStackNode *groupStackOldTop = groupStackTop;;)
    {
        bool done = groupStackTop == groupStackOldTop && stack->isAtomicGroup();
        int numCaptured = stack->popForAtomicCapture(*this);
        numCapturedDelta += numCaptured;
        if (enable_persistent_backrefs)
            for (int i=0; i<numCaptured; i++)
                writeCaptureAtomicTmp(stack->popForAtomicForwardCapture(*this, i));
        stack.pop(*this);
        if (done)
            break;
    }

    // the following is needed for atomic lookahead but not for atomic groups... todo: reacquaint myself with the reason for this, and make the two more similar if it makes sense to do so
    //groupStackTop->numCaptured += enable_persistent_backrefs ? captureIndexNumUsedAtomicTmp : numCapturedDelta;

    if (numCapturedDelta)
    {
        Backtrack_AtomicCapture<USE_STRINGS> *pushStack = stack.template push< Backtrack_AtomicCapture<USE_STRINGS> >(Backtrack_AtomicCapture<USE_STRINGS>::get_size(enable_persistent_backrefs ? captureIndexNumUsedAtomicTmp : numCapturedDelta));
        pushStack->numCaptured       = numCapturedDelta; // will be overridding by "transfer" call below if we're in enable_persistent_backrefs mode
        pushStack->parentAlternative = group->parentAlternative;
        pushStack->transfer(*this);
    }
}

template <bool USE_STRINGS>
bool RegexMatcher<USE_STRINGS>::Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength)
{
    delete [] groupStackBase;
    groupStackBase = new GroupStackNode [maxGroupDepth];
    groupStackTop = groupStackBase;
    if (matchFunction(&regex) != &RegexMatcher<USE_STRINGS>::matchSymbol_Group)
        virtualizeSymbols(&regex);

    delete [] captures;
    captures = new Uint64 [numCaptureGroups];

    if (enable_persistent_backrefs)
    {
        delete [] captureIndexUsedAtomicTmp;
        captureIndexUsedAtomicTmp = new bool [numCaptureGroups];
        delete [] captureIndexesAtomicTmp;
        captureIndexesAtomicTmp = new Uint [numCaptureGroups];
        delete [] capturesAtomicTmp;
        captureIndexNumUsedAtomicTmp = 0;
        capturesAtomicTmp = new Uint64 [numCaptureGroups];
        memset(captureIndexUsedAtomicTmp, false, numCaptureGroups * sizeof(bool));
    }

    delete [] captureStackBase;
    captureStackBase = new Uint[numCaptureGroups];

    verb = RegexVerb_None;

    initInput(_input, numCaptureGroups);

    Uint64 curPosition=0;
    for (; curPosition<=input; curPosition++)
    {
        numSteps = 0;
        alternative   = regex.alternatives;
        symbol        = regex.alternatives[0]->symbols;
        position      = curPosition;
        startPosition = curPosition;
        currentMatch  = ULLONG_MAX;

        groupStackTop->position    = curPosition;
        groupStackTop->loopCount   = 1;
        groupStackTop->group       = &regex;
        groupStackTop->numCaptured = 0;

        memset(captures, (Uint8)NON_PARTICIPATING_CAPTURE_GROUP, numCaptureGroups * sizeof(Uint64));

        captureStackTop = captureStackBase;
#ifdef _DEBUG
        memset(captureStackBase, -1, numCaptureGroups*sizeof(Uint));
#endif

        match = 0;

        do
        {
            RegexSymbol *thisSymbol = *symbol;
            if (!thisSymbol) // exiting a group?
            {
                if (groupStackTop == groupStackBase)
                {
                    match = +1;
                    break;
                }

                RegexGroup *const group = groupStackTop->group;

                if (group->type == RegexGroup_Atomic)
                    popAtomicGroup(group);
                else
                if (group->type == RegexGroup_Lookahead)
                {
                    if (verb == RegexVerb_Accept)
                        verb = RegexVerb_None;

                    position = groupStackTop->position;

                    int numCapturedDelta = 0;

                    GroupStackNode *groupStackOldTop = groupStackTop;
                    do
                    {
                        int numCaptured = stack->popForAtomicCapture(*this);
                        numCapturedDelta += numCaptured;
                        if (enable_persistent_backrefs)
                            for (int i=0; i<numCaptured; i++)
                                writeCaptureAtomicTmp(stack->popForAtomicForwardCapture(*this, i));
                        stack.pop(*this);
                    }
                    while (groupStackTop >= groupStackOldTop);

                    groupStackTop->numCaptured += enable_persistent_backrefs ? captureIndexNumUsedAtomicTmp : numCapturedDelta;

                    if (numCapturedDelta)
                    {
                        Backtrack_AtomicCapture<USE_STRINGS> *pushStack = stack.template push< Backtrack_AtomicCapture<USE_STRINGS> >(Backtrack_AtomicCapture<USE_STRINGS>::get_size(enable_persistent_backrefs ? captureIndexNumUsedAtomicTmp : numCapturedDelta));
                        pushStack->numCaptured       = numCapturedDelta; // will be overridding by "transfer" call below if we're in enable_persistent_backrefs mode
                        pushStack->parentAlternative = group->parentAlternative;
                        pushStack->transfer(*this);
                    }

                    alternative = group->parentAlternative;
                    if (!group->self) // group->self will be NULL if this is the lookaround in a conditional
                    {
                        if (debugTrace)
                            fputs("Match found inside lookaround conditional; jumping to \"yes\" alternative\n\n", stderr);
                        symbol = (*alternative)->symbols;
                    }
                    else
                        symbol = group->self + 1;
                    continue;
                }
                else
                if (group->type == RegexGroup_LookaheadMolecular)
                {
                    if (verb == RegexVerb_Accept)
                        verb = RegexVerb_None;

                    Backtrack_LeaveMolecularLookahead<USE_STRINGS> *pushStack = stack.template push< Backtrack_LeaveMolecularLookahead<USE_STRINGS> >();

                    pushStack->position    = groupStackTop->position;
                    pushStack->group       = group;
                    pushStack->numCaptured = groupStackTop->numCaptured;
                    pushStack->alternative = (Uint)(alternative - groupStackTop->group->alternatives);

                    position    = groupStackTop->position;
                    alternative = group->parentAlternative;
                    if (!group->self) // group->self will be NULL if this is the lookaround in a conditional
                    {
                        if (debugTrace)
                            fputs("Match found inside lookaround conditional; jumping to \"yes\" alternative\n\n", stderr);
                        symbol = (*alternative)->symbols;
                    }
                    else
                        symbol = group->self + 1;
                    groupStackTop[-1].numCaptured += groupStackTop->numCaptured;
                    groupStackTop--;
                    currentMatch = ULLONG_MAX;
                    continue;
                }
                else
                if (group->type == RegexGroup_NegativeLookahead)
                {
                    if (verb == RegexVerb_Accept)
                        verb = RegexVerb_None;

                    position = groupStackTop->position;
                    alternative = group->parentAlternative;

                    GroupStackNode *groupStackOldTop = groupStackTop;
                    do
                    {
                        stack->popForNegativeLookahead(*this);
                        stack.pop(*this);
                    }
                    while (groupStackTop >= groupStackOldTop);

                    // if we've reached here, it means a match was found inside the negative lookahead, which makes it a non-match outside
                    if (!group->self) // group->self will be NULL if this is the lookaround in a conditional
                    {
                        if (debugTrace)
                            fputs("Match found inside negative lookahead conditional, resulting in a non-match outside it; jumping to \"no\" alternative\n\n", stderr);
                        alternative++;
                        symbol = *alternative ? (*alternative)->symbols : &nullSymbol;
                    }
                    else
                        nonMatch(true);
                    continue;
                }

#ifdef _DEBUG
                if (groupStackTop->loopCount > MAX_EXTEND(group->maxCount))
                    THROW_ENGINEBUG;
#endif
                if (group->lazy && groupStackTop->loopCount >= group->minCount)
                    leaveLazyGroup();
                else
                if (groupStackTop->loopCount == MAX_EXTEND(group->maxCount) || group->maxCount == UINT_MAX && groupStackTop->loopCount >= group->minCount && position == groupStackTop->position)
                    leaveMaxedOutGroup();
                else
                    loopGroup(pushStack_LoopGroup(), position, groupStackTop->position, (Uint)(alternative - groupStackTop->group->alternatives)); // todo: optimize this for possessive groups by not pushing unnecessarily onto the stack

                if (verb == RegexVerb_Accept)
                    symbol = &nullSymbol;

                continue;
            }
            if (debugTrace)
            {
                const char *source = thisSymbol->originalCode;
                const char *delim = strpbrk(source, "\r\n");
                char *copy;
                if (delim)
                {
                    size_t size = delim - source;
                    copy = new char [size + 1];
                    memcpy(copy, source, size);
                    copy[size] = '\0';
                }
                else
                {
                    size_t size = strlen(source) + 1;
                    copy = new char [size];
                    memcpy(copy, source, size);
                }
                fprintf(stderr, "%s\n", copy);
                delete [] copy;

#ifdef _DEBUG
                fprintf(stderr, "Step %llu: {%llu|%llu} <%llu> ", numSteps, position, input - position, stack.getStackDepth());
#else
                fprintf(stderr, "Step %llu: {%llu|%llu} ", numSteps, position, input - position);
#endif
                for (Uint *i=captureStackBase; i<captureStackTop; i++)
                {
                    fprintCapture(stderr, *i);
                    if (i<captureStackTop-1)
                        fputs(", ", stderr);
                }
                fputc('\n', stderr);

                if (debugTrace > 1)
                    stack.fprint(*this, stderr);

                for (GroupStackNode *i=groupStackBase; i<=groupStackTop; i++)
                {
                    const char *openSymbol;
                    switch (i->group->type)
                    {
                    case RegexGroup_NonCapturing:       openSymbol=" (?:"; break;
                    case RegexGroup_Capturing:          openSymbol=" (";   break;
                    case RegexGroup_Atomic:             openSymbol=" (?>"; break;
                    case RegexGroup_BranchReset:        openSymbol=" (?|"; break;
                    case RegexGroup_Lookahead:          openSymbol=" (?="; break;
                    case RegexGroup_LookaheadMolecular: openSymbol=" (?*"; break;
                    case RegexGroup_NegativeLookahead:  openSymbol=" (?!"; break;
                    case RegexGroup_Conditional:
                        {
                            char conditionalStr[strlength(" (?(4294967296)")+1];
                            sprintf(conditionalStr, " (?(%u)", ((RegexConditional*)i->group)->backrefIndex + 1);
                            openSymbol = conditionalStr;
                            break;
                        }
                    case RegexGroup_LookaroundConditional:
                        switch (((RegexLookaroundConditional*)i->group)->lookaround->type)
                        {
                        case RegexGroup_Lookahead:          openSymbol=" (?(?=)"; break;
                        case RegexGroup_LookaheadMolecular: openSymbol=" (?(?*)"; break;
                        case RegexGroup_NegativeLookahead:  openSymbol=" (?(?!)"; break;
                        default: UNREACHABLE_CODE;
                        }
                        break;
                    default:
                        UNREACHABLE_CODE;
                    }
                    if (i > groupStackBase)
                    {
                        fprintf(stderr, "%s{%llu", openSymbol, i->position);
                        if (i==groupStackTop)
                            fprintf(stderr, "..%llu", position);
                        fprintf(stderr, "} #%llu ", i->loopCount);
                    }
                    fprintf(stderr, "[%u]", i->numCaptured);
                }
                for (GroupStackNode *i=groupStackTop; i>groupStackBase; i--)
                    fputc(')', stderr);

                numSteps++;
            }

            (this->*matchFunction(thisSymbol))(thisSymbol); // in debugTrace mode, nonMatch() will print that there was a non-match

            if (debugTrace)
                fputc('\n', stderr);

            if (match)
                break;

            if (debugTrace)
                fputc('\n', stderr);
        }
        while (!match); // this check is redundant with the one directly above, unless a "continue" was used inside the loop

        if (verb == RegexVerb_Skip)
        {
            if (skipPosition == curPosition)
                match = -2;
            else
                curPosition = skipPosition-1;
        }
        verb = RegexVerb_None;

        stack.flush();

        if (match > 0)
        {
            if (debugTrace)
                fprintf(stderr, "Match found at {%llu}\n\n", curPosition);
            break;
        }
        if (match < -1)
        {
            // a non-match backtracked through (*COMMIT)
            if (debugTrace)
                fprintf(stderr, "\n""Halting matching process due to backtracking verb\n\n");
            break;
        }
        if (debugTrace)
        {
            fputs("No match found", stderr);
            if (curPosition+1 <= input && !anchored)
                fprintf(stderr, "; trying at {%llu}", curPosition+1);
            fputs("\n\n", stderr);
        }
        if (anchored)
            break;
    }

    if (startPosition > position) // if \K comes after the end of the match due to use of lookahead, swap the two positions
    {
        curPosition = position;
        position = startPosition;
        startPosition = curPosition;
    }

    returnMatchOffset = startPosition;
    returnMatchLength = (size_t)(position - startPosition);
    
    return match > 0;
}

template <>
void RegexMatcher<false>::fprintCapture(FILE *f, Uint64 length, const char *offset)
{
    if (length == NON_PARTICIPATING_CAPTURE_GROUP)
        fputs("NPCG", f);
    else
        fprintf(f, "%llu", length);
}
template <>
void RegexMatcher<false>::fprintCapture(FILE *f, Uint i)
{
    fprintf(f, "\\%u=", i+1);
    if (captures[i] == NON_PARTICIPATING_CAPTURE_GROUP)
        fputs("NPCG", f);
    else
        fprintf(f, "%llu", captures[i]);
}

template <>
void RegexMatcher<true>::fprintCapture(FILE *f, Uint64 length, const char *offset)
{
    if (length == NON_PARTICIPATING_CAPTURE_GROUP)
    {
        fputs("NPCG", f);
        return;
    }
    fputc('\"', f);
    const char *s = offset;
    for (Uint64 len=length; len!=0; len--)
    {
        switch (*s)
        {
        case '\\': fputs("\\\\", f); break;
        case '"':  fputs("\\\"", f); break;
        default:
            fputc(*s, f);
            break;
        }
        s++;
    }
    fprintf(f, "\" (%llu:%llu)", offset - stringToMatchAgainst, length);
}
template <>
void RegexMatcher<true>::fprintCapture(FILE *f, Uint i)
{
    fprintf(f, "\\%u=", i+1);
    fprintCapture(f, captures[i], captureOffsets[i]);
}

template bool RegexMatcher<false>::Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
template bool RegexMatcher<true >::Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
