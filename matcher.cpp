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

#ifdef _DEBUG
#define POPSTACK  stackDepth--;
#define PUSHSTACK stackDepth++;
#else
#define POPSTACK  do{}while (0)
#define PUSHSTACK do{}while (0)
#endif

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::nonMatch()
{
    position = groupStackTop->position;

    for (;;)
    {
        if (*alternative && (!stack || stack->okayToTryAlternatives(*this)))
        {
            alternative++;
            if (*alternative)
            {
                position = groupStackTop->position;
                symbol = (*alternative)->symbols;
                currentMatch = ULLONG_MAX;
                return;
            }
        }

        if (!stack)
        {
            match = -1;
            return;
        }

        MatchingStackNode<USE_STRINGS> *formerTop = stack;
        stack = stack->below;
        POPSTACK;

        bool stopHere = formerTop->popTo(*this);
        
        delete formerTop;

        if (stopHere)
            break;
    }
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::pushStack()
{
    MatchingStack_TryMatch<USE_STRINGS> *pushStack = new MatchingStack_TryMatch<USE_STRINGS>;
    pushStack->below = stack;
    stack = pushStack;
    PUSHSTACK;

    pushStack->position     = position;
    pushStack->currentMatch = currentMatch;
    pushStack->symbol       = *symbol;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::enterGroup(RegexGroup *group)
{
    groupStackTop++;
    groupStackTop->position    = position;
    groupStackTop->loopCount   = 1;
    groupStackTop->group       = group;
    groupStackTop->numCaptured = 0;

    alternative = group->alternatives;
    symbol      = group->alternatives[0]->symbols;

    MatchingStack_EnterGroup<USE_STRINGS> *pushStack = new MatchingStack_EnterGroup<USE_STRINGS>;
    pushStack->below = stack;
    stack = pushStack;
    PUSHSTACK;
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::leaveGroup(MatchingStack_LeaveGroup<USE_STRINGS> *pushStack, Uint64 pushPosition)
{
    pushStack->below = stack;
    stack = pushStack;
    PUSHSTACK;

    pushStack->position    = pushPosition;
    pushStack->loopCount   = groupStackTop->loopCount;
    pushStack->group       = groupStackTop->group;
    pushStack->numCaptured = groupStackTop->numCaptured;
    pushStack->alternative = (Uint)(alternative - groupStackTop->group->alternatives);

    RegexGroup *group = groupStackTop->group;
    if (group->type == RegexGroup_Capturing)
    {
        Uint backrefIndex = ((RegexGroupCapturing*)group)->backrefIndex;
        writeCaptureRelative(backrefIndex, groupStackTop->position, position);
        *captureStackTop++ = backrefIndex;
        groupStackTop->numCaptured++;
    }

    alternative = groupStackTop->group->parentAlternative;
    symbol      = groupStackTop->group->self + 1;
    groupStackTop[-1].numCaptured += groupStackTop->numCaptured;
    groupStackTop--;
    currentMatch = ULLONG_MAX;
}

template <bool USE_STRINGS>
void *RegexMatcher<USE_STRINGS>::loopGroup(MatchingStack_LoopGroup<USE_STRINGS> *allocateFunction(Uint numCaptured, size_t privateSpace), size_t privateSpace, Uint64 pushPosition)
{
    groupStackTop->loopCount++;

    Uint numCaptured = groupStackTop->numCaptured;
    groupStackTop->numCaptured = 0;

    MatchingStack_LoopGroup<USE_STRINGS> *pushLoop = allocateFunction(numCaptured, privateSpace);
    pushLoop->below       = stack;
    pushLoop->position    = pushPosition;
    pushLoop->numCaptured = numCaptured;
    stack = pushLoop;
    PUSHSTACK;

    Uint64 *values = (Uint64*)(pushLoop->buffer + privateSpace);
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
        readCapture(indexes[i], values[i], offsets[i]);
        captures[indexes[i]] = NON_PARTICIPATING_CAPTURE_GROUP;
    }
    captureStackTop -= numCaptured;

    alternative = groupStackTop->group->alternatives;
    symbol      = groupStackTop->group->alternatives[0]->symbols;
    groupStackTop->position = position;
    currentMatch = ULLONG_MAX;

    return (void*)pushLoop->buffer;
}

template<> void RegexMatcher<false>::initInput(Uint64 _input, Uint numCaptureGroups)
{
    input = _input;
}
template<> void RegexMatcher<true>::initInput(Uint64 _input, Uint numCaptureGroups)
{
    stringToMatchAgainst = (const char *)_input;
    input = strlen(stringToMatchAgainst);
    delete [] captureOffsets;
    captureOffsets = new const char * [numCaptureGroups];
    for (Uint i=0; i<numCaptureGroups; i++)
        captureOffsets[i] = stringToMatchAgainst;
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

template<> Uint64 RegexMatcher<false>::countRepetendMatches(const char *pBackref, Uint64 multiple)
{
    return 0;
}
template<> Uint64 RegexMatcher<false>::countRepetendMatches(bool (*matchFunction)(Uchar ch), Uint64 multiple)
{
    return 0;
}
template<> Uint64 RegexMatcher<false>::countRepetendMatches(RegexCharacterClass *charClass, Uint64 multiple)
{
    return 0;
}
template<> Uint64 RegexMatcher<true>::countRepetendMatches(const char *pBackref, Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=multiple)
        if (memcmp(s, pBackref, (size_t)multiple)!=0)
            break;
    return count;
}
template<> Uint64 RegexMatcher<true>::countRepetendMatches(bool (*matchFunction)(Uchar ch), Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=1)
        if (!matchFunction(*s))
            break;
    return count;
}
template<> Uint64 RegexMatcher<true>::countRepetendMatches(RegexCharacterClass *charClass, Uint64 multiple)
{
    const char *s = stringToMatchAgainst + position;
    Uint64 count;
    for (count = 0; count < currentMatch; count++, s+=1)
        if (!charClass->isInClass(*s))
            break;
    return count;
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
    return position==0 || position==input;
}

template<> bool RegexMatcher<true>::matchWordBoundary()
{
    if (position==0 || position==input)
        return true;

    char lfChar = stringToMatchAgainst[position-1];
    char rhChar = stringToMatchAgainst[position];
    bool lfWord = inrange(lfChar,'A','Z') || inrange(lfChar,'a','z') || inrange(lfChar,'0','9') || lfChar=='_';
    bool rhWord = inrange(rhChar,'A','Z') || inrange(rhChar,'a','z') || inrange(rhChar,'0','9') || rhChar=='_';
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
    if (multiple == NON_PARTICIPATING_CAPTURE_GROUP && emulate_ECMA_NPCGs)
        multiple = 0;
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
        if (optimizationLevel)
        {
            if (symbol[+1] && symbol[+1]->type==RegexSymbol_AnchorEnd)
            {
                Uint64 spaceLeft = input - position;
                currentMatch = spaceLeft / multiple;
                if (!inrange(currentMatch, thisSymbol->minCount, thisSymbol->maxCount))
                {
                    nonMatch();
                    return;
                }
                if (!doesRepetendMatch(repetend, multiple, currentMatch))
                {
                    nonMatch();
                    return;
                }
                position     = input - spaceLeft % multiple;
                currentMatch = ULLONG_MAX;
                symbol++;
                return;
            }
            RegexSymbol *nextSymbol = symbol[+1];
            RegexGroup *thisGroup = groupStackTop->group;
            bool afterEndOfGroup = false;
            if (nextSymbol && nextSymbol->type==RegexSymbol_Group ||
                !nextSymbol && !alternative[+1] && groupStackTop > groupStackBase
                            && (thisGroup->type==RegexGroup_Capturing || thisGroup->type==RegexGroup_NonCapturing)
                            && groupStackTop->loopCount==thisGroup->maxCount && (nextSymbol = thisGroup->self[+1])
                            && nextSymbol->type==RegexSymbol_Group && (afterEndOfGroup=true))
            {
                RegexGroup *group = (RegexGroup*)nextSymbol;
                if (group->type==RegexGroup_Lookahead && !group->alternatives[1] && group->minCount)
                {
                    RegexSymbol **lookaheadSymbol = group->alternatives[0]->symbols;
                    Uint64 totalLength = 0;
                    bool cannotMatch = false;
                    for (;;)
                    {
                        RegexSymbol *currentSymbol = *lookaheadSymbol;
                        if (currentSymbol->type == RegexSymbol_Backref && currentSymbol->minCount == currentSymbol->maxCount)
                        {
                            if (afterEndOfGroup && thisGroup->type==RegexGroup_Capturing && ((RegexGroupCapturing*)thisGroup)->backrefIndex == ((RegexBackref*)currentSymbol)->index)
                            {
                                if (lookaheadSymbol == group->alternatives[0]->symbols && optimizationLevel >= 2)
                                {
                                    if (!lookaheadSymbol[+1] && !thisSymbol->lazy || lookaheadSymbol[+1]->type == RegexSymbol_AnchorEnd)
                                    {
                                        if (totalLength > input || cannotMatch)
                                        {
                                            nonMatch();
                                            return;
                                        }
                                        Uint64 target = input - totalLength;
                                        if (position > target)
                                        {
                                            nonMatch();
                                            return;
                                        }
                                        Uint64 spaceLeft = target - position;
                                        currentMatch = spaceLeft / (multiple * (1 + currentSymbol->minCount));
                                        if (currentMatch < thisSymbol->minCount)
                                        {
                                            nonMatch();
                                            return;
                                        }
                                        if (currentMatch > thisSymbol->maxCount)
                                            currentMatch = thisSymbol->maxCount;
                                        if (lookaheadSymbol[+1]) // anchored?
                                        {
                                            if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                            {
                                                nonMatch();
                                                return;
                                            }
                                        }
                                        else
                                        {
                                            if (USE_STRINGS)
                                                currentMatch = countRepetendMatches(repetend, multiple);
                                            pushStack();
                                        }
                                        position += currentMatch * multiple;
                                        currentMatch = ULLONG_MAX;
                                        symbol++;
                                        return;
                                    }
                                }
                                break;
                            }
                            Uint64 thisCapture = captures[((RegexBackref*)currentSymbol)->index];
                            if (thisCapture != NON_PARTICIPATING_CAPTURE_GROUP)
                                totalLength += thisCapture * currentSymbol->minCount;
                            else
                            {
                                if (currentSymbol->minCount && !emulate_ECMA_NPCGs)
                                    cannotMatch = true;
                            }
                        }
                        else
                        if (currentSymbol->type == RegexSymbol_Character && currentSymbol->minCount == currentSymbol->maxCount)
                            totalLength += currentSymbol->minCount;
                        else
                            break;
                        lookaheadSymbol++;
                        if (!*lookaheadSymbol && !thisSymbol->lazy || (*lookaheadSymbol)->type==RegexSymbol_AnchorEnd)
                        {
                            if (totalLength > input || cannotMatch)
                            {
                                nonMatch();
                                return;
                            }
                            Uint64 target = input - totalLength;
                            if (position > target)
                            {
                                nonMatch();
                                return;
                            }
                            Uint64 spaceLeft = target - position;
                            currentMatch = spaceLeft / multiple;
                            if (currentMatch < thisSymbol->minCount)
                            {
                                nonMatch();
                                return;
                            }
                            if (currentMatch > thisSymbol->maxCount)
                                currentMatch = thisSymbol->maxCount;
                            if (*lookaheadSymbol) // anchored?
                            {
                                if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                {
                                    nonMatch();
                                    return;
                                }
                            }
                            else
                            {
                                if (USE_STRINGS)
                                    currentMatch = countRepetendMatches(repetend, multiple);
                                pushStack();
                            }
                            //position = target - spaceLeft % multiple;
                            position += currentMatch * multiple;
                            currentMatch = ULLONG_MAX;
                            symbol++;
                            return;
                        }
                    }
                }
            }
        }
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
                    currentMatch = countRepetendMatches(repetend, multiple);
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
                    currentMatch = countRepetendMatches(repetend, multiple);
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
                if (USE_STRINGS && thisSymbol->lazy && !doesRepetendMatchOnce(repetend, multiple, currentMatch-1))
                {
                    nonMatch();
                    return;
                }
                if (currentMatch != (thisSymbol->lazy ? thisSymbol->maxCount : thisSymbol->minCount))
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
            if (currentMatch == thisSymbol->maxCount)
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
        if (group->type == RegexGroup_Capturing)
            captures[((RegexGroupCapturing*)group)->backrefIndex] = NON_PARTICIPATING_CAPTURE_GROUP;
        return;
    }
    if (group->lazy && group->minCount == 0)
    {
        MatchingStack_SkipGroup<USE_STRINGS> *pushStack = new MatchingStack_SkipGroup<USE_STRINGS>;
        pushStack->below = stack;
        stack = pushStack;
        PUSHSTACK;
        pushStack->position = position;
        pushStack->group    = group;
        symbol++;
        return;
    }
    enterGroup(group);
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

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::virtualizeSymbols(RegexGroup *rootGroup)
{
    RegexPattern **thisAlternative;
    RegexSymbol  **thisSymbol = &(RegexSymbol*&)rootGroup;
    for (;;)
    {
        if (*thisSymbol)
        {
            switch ((*thisSymbol)->type)
            {
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
            case RegexSymbol_AnchorStart:
                if ((*thisSymbol)->minCount == 0)
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                else
                {
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AnchorStart;
                    groupStackTop[-1].currentAlternativeAnchored = true;
                }
                break;
            case RegexSymbol_AnchorEnd:
                if ((*thisSymbol)->minCount == 0)
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AnchorEnd;
                break;
            case RegexSymbol_WordBoundaryNot:
                if ((*thisSymbol)->minCount == 0)
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundaryNot;
                break;
            case RegexSymbol_WordBoundary:
                if ((*thisSymbol)->minCount == 0)
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_AlwaysMatch;
                else
                    matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordBoundary;
                break;
            case RegexSymbol_DigitNot:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_DigitNot;
                break;
            case RegexSymbol_Digit:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Digit;
                break;
            case RegexSymbol_SpaceNot:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_SpaceNot;
                break;
            case RegexSymbol_Space:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_Space;
                break;
            case RegexSymbol_WordCharacterNot:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacterNot;
                break;
            case RegexSymbol_WordCharacter:
                // todo: check character uniformity
                matchFunction(*thisSymbol++) = &RegexMatcher<USE_STRINGS>::matchSymbol_WordCharacter;
                break;
            case RegexSymbol_Group:
                matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_Group;
                RegexGroup *group = (RegexGroup*)(*thisSymbol);
                if (optimizationLevel >= 2)
                {
                    // (?!(x(xx)+|)\1*$)
                    if (!USE_STRINGS && group->type == RegexGroup_NegativeLookahead && group->minCount)
                    {
                        RegexPattern **insideAlternative = group->alternatives;
                        RegexSymbol **insideSymbol = insideAlternative[0]->symbols;
                        if (!insideAlternative[+1] &&
                            insideSymbol[0] && insideSymbol[0]->type==RegexSymbol_Group &&
                            insideSymbol[1] && insideSymbol[1]->type==RegexSymbol_Backref && insideSymbol[1]->minCount==0 && insideSymbol[1]->maxCount==UINT_MAX &&
                            insideSymbol[2] && insideSymbol[2]->type==RegexSymbol_AnchorEnd && insideSymbol[2]->maxCount && !insideSymbol[3])
                        {
                            RegexGroup *insideGroup = (RegexGroup*)insideSymbol[0];
                            if (insideGroup->type == RegexGroup_Capturing && insideGroup->minCount==1 && insideGroup->maxCount==1 &&
                                ((RegexBackref*)insideSymbol[1])->index == ((RegexGroupCapturing*)insideGroup)->backrefIndex)
                            {
                                bool matchZero = true;
                                RegexPattern **innerAlternative = insideGroup->alternatives;
                                RegexSymbol **innerSymbol;
                                if (innerAlternative[1] && !innerAlternative[2] &&
                                    (!innerAlternative[0]->symbols[0] && (innerSymbol = innerAlternative[1]->symbols)[0] ||
                                        !innerAlternative[1]->symbols[0] && (innerSymbol = innerAlternative[0]->symbols)[0]))
                                {
                                    matchZero = false;
                                }
                                else
                                    innerSymbol = innerAlternative[0]->symbols;
                                
                                if (innerSymbol[0] && innerSymbol[0]->type==RegexSymbol_Character && innerSymbol[0]->minCount==1 && innerSymbol[0]->maxCount==1        && characterCanMatch(innerSymbol[0]) &&
                                    innerSymbol[1] && innerSymbol[1]->type==RegexSymbol_Group     && innerSymbol[1]->minCount==1 && innerSymbol[1]->maxCount==UINT_MAX && !innerSymbol[2])
                                {
                                    RegexGroup *innerGroup = (RegexGroup*)innerSymbol[1];
                                    if (innerGroup->type == RegexGroup_Capturing || innerGroup->type == RegexGroup_NonCapturing)
                                    {
                                        RegexPattern **innermostAlternative = innerGroup->alternatives;
                                        RegexSymbol **innermostSymbol;
                                        if (!innermostAlternative[1])
                                        {
                                            innermostSymbol = innermostAlternative[0]->symbols;
                                            if (innermostSymbol[0] && innermostSymbol[0]->type==RegexSymbol_Character && innermostSymbol[0]->minCount==2 && innermostSymbol[0]->maxCount==2 && characterCanMatch(innermostSymbol[0]) && !innermostSymbol[1])
                                            {
                                                const char    *originalCode      = (*thisSymbol)->originalCode;
                                                RegexPattern **parentAlternative = (*thisSymbol)->parentAlternative;

                                                // todo: delete the old negative lookahead and its children

                                                *thisSymbol = new RegexSymbol(RegexSymbol_IsPowerOf2);
                                                (*thisSymbol)->lazy              = matchZero;
                                                (*thisSymbol)->parentAlternative = parentAlternative;
                                                (*thisSymbol)->self              = thisSymbol;
                                                (*thisSymbol)->originalCode      = originalCode;
                                                (*thisSymbol)->minCount          = 1;
                                                (*thisSymbol)->maxCount          = 1;
                                                matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_IsPowerOf2;
                                                thisSymbol++;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                groupStackTop->numAnchoredAlternatives = 0;
                groupStackTop->currentAlternativeAnchored = false;
                (*groupStackTop++).group = group;
                thisAlternative = group->alternatives;
                thisSymbol      = group->alternatives[0]->symbols;
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
                thisSymbol      = group->self + 1;
            }
        }
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

    delete [] captureStackBase;
    captureStackBase = new Uint[numCaptureGroups];

    initInput(_input, numCaptureGroups);

    Uint64 curPosition=0;
    for (; curPosition<=input; curPosition++)
    {
        numSteps = 0;
#ifdef _DEBUG
        stackDepth = 0;
#endif
        stack = NULL;
        alternative   = regex.alternatives;
        symbol        = regex.alternatives[0]->symbols;
        position      = curPosition;
        currentMatch  = ULLONG_MAX;

        groupStackTop->position    = 0;
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
                    while (stack)
                    {
                        MatchingStackNode<USE_STRINGS> *formerTop = stack;
                        stack = stack->below;
                        delete formerTop;
                    }
                    break;
                }

                RegexGroup *group = groupStackTop->group;

                if (group->type == RegexGroup_Lookahead)
                {
                    position = groupStackTop->position;

                    int numCapturedDelta = 0;

                    GroupStackNode *groupStackOldTop = groupStackTop;
                    do
                    {
                        numCapturedDelta += stack->popForLookahead(*this);
                    
                        MatchingStackNode<USE_STRINGS> *stackDown = stack->below;
                        delete stack;
                        stack = stackDown;
                        POPSTACK;
                    }
                    while (groupStackTop >= groupStackOldTop);

                    if (numCapturedDelta)
                    {
                        MatchingStack_LookaheadCapture<USE_STRINGS> *pushStack = new MatchingStack_LookaheadCapture<USE_STRINGS>;
                        pushStack->below = stack;
                        stack = pushStack;
                        PUSHSTACK;
                        pushStack->numCaptured       = numCapturedDelta;
                        pushStack->parentAlternative = group->parentAlternative;
                    }

                    groupStackTop->numCaptured += numCapturedDelta;

                    alternative = group->parentAlternative;
                    symbol      = group->self + 1;
                    continue;
                }
                else
                if (group->type == RegexGroup_NegativeLookahead)
                {
                    position = groupStackTop->position;
                    alternative = group->parentAlternative;

                    GroupStackNode *groupStackOldTop = groupStackTop;
                    do
                    {
                        stack->popForNegativeLookahead(*this);
                    
                        MatchingStackNode<USE_STRINGS> *stackDown = stack->below;
                        delete stack;
                        stack = stackDown;
                        POPSTACK;
                    }
                    while (groupStackTop >= groupStackOldTop);

                    // if we've reached here, it means a match was found inside the negative lookahead, which makes it a non-match outside
                    nonMatch();
                    continue;
                }

                if (group->lazy && groupStackTop->loopCount >= group->minCount)
                {
                    MatchingStack_TryLazyAlternatives<USE_STRINGS> *pushStack = new MatchingStack_TryLazyAlternatives<USE_STRINGS>;
                    pushStack->below = stack;
                    stack = pushStack;
                    PUSHSTACK;
                    pushStack->position    = groupStackTop->position;
                    pushStack->alternative = (Uint)(alternative - groupStackTop->group->alternatives);

                    leaveGroup(new MatchingStack_LeaveGroupLazily<USE_STRINGS>, position);
                }
                else
                if (groupStackTop->loopCount == group->maxCount || position == groupStackTop->position)
                    leaveGroup(new MatchingStack_LeaveGroup<USE_STRINGS>, groupStackTop->position);
                else
                if (!group->lazy && inrangex(groupStackTop->loopCount, group->minCount, group->maxCount))
                {
                    bool selfCapture = group->type == RegexGroup_Capturing;
                    Uint64 oldPosition = groupStackTop->position;
                    Uint alternativeNum = (Uint)(alternative - groupStackTop->group->alternatives);
                    void *buffer = loopGroup(
                        MatchingStack_LoopGroupGreedily<USE_STRINGS>::allocate,
                        sizeof(Uint64) + (selfCapture ? sizeof(Uint64) : 0), // first term has sizeof(Uint64) instead of sizeof(Uint) for alignment
                        position);
                    *(Uint*)buffer = alternativeNum;
                    if (selfCapture)
                        ((Uint64*)buffer)[1] = oldPosition;
                }
                else
                    loopGroup(MatchingStack_LoopGroup<USE_STRINGS>::allocate, 0, position);
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
                fprintf(stderr, "Step %llu: {%llu} <%llu> ", numSteps, position, stackDepth);
#else
                fprintf(stderr, "Step %llu: {%llu} ", numSteps, position);
#endif
                for (Uint *i=captureStackBase; i<captureStackTop; i++)
                    fprintf(stderr, "\\%u=%llu%s", *i+1, captures[*i], i<captureStackTop-1 ? ", " : "");
                fputc('\n', stderr);

                for (GroupStackNode *i=groupStackBase; i<=groupStackTop; i++)
                {
                    const char *openSymbol;
                    switch (i->group->type)
                    {
                    default:
                    case RegexGroup_NonCapturing:      openSymbol=" (?:"; break;
                    case RegexGroup_Capturing:         openSymbol=" (";   break;
                    case RegexGroup_Lookahead:         openSymbol=" (?="; break;
                    case RegexGroup_NegativeLookahead: openSymbol=" (?!"; break;
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
                fputc('\n', stderr);

                fputc('\n', stderr);
                numSteps++;
            }
            (this->*matchFunction(thisSymbol))(thisSymbol);
        }
        while (!match);

        if (match > 0)
            break;

        if (anchored)
            break;
    }

    returnMatchOffset = curPosition;
    returnMatchLength = (size_t)(position - curPosition);
    
    return match > 0;
}

template bool RegexMatcher<false>::Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
template bool RegexMatcher<true >::Match(RegexGroup &regex, Uint numCaptureGroups, Uint maxGroupDepth, Uint64 _input, Uint64 &returnMatchOffset, Uint64 &returnMatchLength);
