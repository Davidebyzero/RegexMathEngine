#include "math-optimization.h"

template <bool USE_STRINGS> // currently implemented only for !USE_STRINGS
Uint64 RegexMatcher<USE_STRINGS>::matchSymbol_ConstGroup(RegexSymbol *thisSymbol, bool capturing)
{
    RegexGroup *const group = ((RegexConstGroup*)thisSymbol)->originalGroup;
    RegexPattern **insideAlternative = group->alternatives;
    Uint64 multiple = 0;
    for (RegexSymbol **insideSymbol = insideAlternative[0]->symbols; *insideSymbol; insideSymbol++)
    {
        switch ((*insideSymbol)->type)
        {
        case RegexSymbol_NoOp:
            break;
        case RegexSymbol_Character:
            multiple += (*insideSymbol)->minCount;
            break;
        case RegexSymbol_Backref:
            {
                Uint64 thisMultiple;
                const char *pBackref;
                readCapture(((RegexBackref*)*insideSymbol)->index, thisMultiple, pBackref);
                if (thisMultiple == NON_PARTICIPATING_CAPTURE_GROUP)
                {
                    if (!emulate_ECMA_NPCGs && (*insideSymbol)->minCount != 0)
                    {
                        nonMatch();
                        return NON_PARTICIPATING_CAPTURE_GROUP;
                    }
                    thisMultiple = 0;
                }
                multiple += thisMultiple * (*insideSymbol)->minCount;
                break;
            }
        }
    }
    if (multiple == 0 && !capturing) // don't backtrack when it will make no difference to do so
    {
        symbol++;
        return 0;
    }
    if (matchSymbol_Character_or_Backref(thisSymbol, multiple, (const char *)NULL))
        return multiple;
    return NON_PARTICIPATING_CAPTURE_GROUP;
}
template <bool USE_STRINGS> // currently implemented only for !USE_STRINGS
void RegexMatcher<USE_STRINGS>::matchSymbol_ConstGroupNonCapturing(RegexSymbol *thisSymbol)
{
    matchSymbol_ConstGroup(thisSymbol, false);
}
template <bool USE_STRINGS> // currently implemented only for !USE_STRINGS
void RegexMatcher<USE_STRINGS>::matchSymbol_ConstGroupCapturing(RegexSymbol *thisSymbol)
{
    Uint64 position0 = position;
    Uint64 multiple = matchSymbol_ConstGroup(thisSymbol, true);
    if (multiple != NON_PARTICIPATING_CAPTURE_GROUP)
    {
        Uint backrefIndex = ((RegexConstGroupCapturing*)thisSymbol)->backrefIndex;

        Backtrack_LeaveConstGroupCapturing<USE_STRINGS> *pushStack = stack.template push< Backtrack_LeaveConstGroupCapturing<USE_STRINGS> >(Backtrack_LeaveConstGroupCapturing<USE_STRINGS>::get_size());
        pushStack->backrefIndex = backrefIndex;
        if (enable_persistent_backrefs)
        {
            const char *&dummy = (const char *&)pushStack->buffer;
            if (!USE_STRINGS)
                readCapture(backrefIndex, *(Uint64*)(pushStack->buffer                      ), dummy);
            else
                readCapture(backrefIndex, *(Uint64*)(pushStack->buffer + sizeof(const char*)), *(const char**)pushStack->buffer);
        }

        Uint64 prevValue = captures[backrefIndex];
        writeCapture(backrefIndex, multiple, (const char *)NULL);
        if (!enable_persistent_backrefs || prevValue == NON_PARTICIPATING_CAPTURE_GROUP)
        {
            *captureStackTop++ = backrefIndex;
            groupStackTop->numCaptured++;
        }
    }
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_IsPrime(RegexSymbol *thisSymbol)
{
    Uint64 spaceLeft;
    if (!thisSymbol->possessive)
        spaceLeft = input - position;
    else
    {
        const char *ptr;
        if (!getLookintoEntrace(((RegexBackref*)thisSymbol)->index, spaceLeft, ptr))
            return;
    }
    if (inrange64(spaceLeft, thisSymbol->lazy, 1) || isPrime(spaceLeft))
    {
        symbol++;
        return;
    }
    nonMatch();
}

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_IsPowerOf2(RegexSymbol *thisSymbol)
{
    Uint64 spaceLeft;
    if (!thisSymbol->possessive)
        spaceLeft = input - position;
    else
    {
        const char *ptr;
        if (!getLookintoEntrace(((RegexBackref*)thisSymbol)->index, spaceLeft, ptr))
            return;
    }
    if ((spaceLeft != 0 || thisSymbol->lazy) && !(spaceLeft & (spaceLeft - 1)))
    {
        symbol++;
        return;
    }
    nonMatch();
}

template <bool USE_STRINGS>
ALWAYS_INLINE bool RegexMatcher<USE_STRINGS>::staticallyOptimizeGroup(RegexSymbol **thisSymbol)
// return true if the group has been rewritten into a specialized symbol
{
    if (optimizationLevel && !USE_STRINGS)
    {
        RegexGroup *const group = (RegexGroup*)(*thisSymbol);
        if ((group->type == RegexGroup_NonCapturing || group->type == RegexGroup_Capturing) && group->maxCount)
        {
            RegexPattern **insideAlternative = group->alternatives;
            if (!insideAlternative[+1])
            {
                Uint backrefIndex = group->type == RegexGroup_Capturing ? ((RegexGroupCapturing*)group)->backrefIndex : UINT_MAX;
                for (RegexSymbol **insideSymbol = insideAlternative[0]->symbols; *insideSymbol;)
                {
                    if ((*insideSymbol)->type != RegexSymbol_Character && (*insideSymbol)->type != RegexSymbol_Backref && (*insideSymbol)->type != RegexSymbol_NoOp || (*insideSymbol)->minCount != (*insideSymbol)->maxCount)
                        break;
                    if ((*insideSymbol)->type == RegexSymbol_Backref && ((RegexBackref*)*insideSymbol)->index == backrefIndex)
                        break;
                    insideSymbol++;
                    if (!*insideSymbol)
                    {
                        const char    *originalCode      = (*thisSymbol)->originalCode;
                        RegexPattern **parentAlternative = (*thisSymbol)->parentAlternative;

                        *thisSymbol = group->type == RegexGroup_NonCapturing ? new RegexConstGroup(group) : new RegexConstGroupCapturing(group, backrefIndex);
                        (*thisSymbol)->minCount          = group->minCount;
                        (*thisSymbol)->maxCount          = group->maxCount;
                        (*thisSymbol)->lazy              = group->lazy;
                        (*thisSymbol)->possessive        = group->possessive;
                        (*thisSymbol)->parentAlternative = parentAlternative;
                        (*thisSymbol)->self              = thisSymbol;
                        (*thisSymbol)->originalCode      = originalCode;
                        return true;
                    }
                }
            }
        }
        else
        if (optimizationLevel >= 2 && group->isNegativeLookaround() && group->minCount)
        {
            RegexPattern **insideAlternative = group->alternatives;
            RegexSymbol **insideSymbol = insideAlternative[0]->symbols;

            // (?!(xx+|)\1+$)
            if (!insideAlternative[+1] &&
                insideSymbol[0] && insideSymbol[0]->type==RegexSymbol_Group &&
                insideSymbol[1] && insideSymbol[1]->type==RegexSymbol_Backref && insideSymbol[1]->minCount==1 && insideSymbol[1]->maxCount==UINT_MAX &&
                insideSymbol[2] && insideSymbol[2]->type==RegexSymbol_AnchorEnd && insideSymbol[2]->minCount && !insideSymbol[3])
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
                        innerSymbol[1] && innerSymbol[1]->type==RegexSymbol_Character && innerSymbol[1]->minCount==1 && innerSymbol[1]->maxCount==UINT_MAX && characterCanMatch(innerSymbol[1]) && !innerSymbol[2])
                    {
                        RegexSymbol   *originalSymbol    = (*thisSymbol);
                        const char    *originalCode      = (*thisSymbol)->originalCode;
                        RegexPattern **parentAlternative = (*thisSymbol)->parentAlternative;

                        bool isLookinto = group->type == RegexGroup_NegativeLookinto;
                        *thisSymbol = isLookinto ? new RegexBackref(RegexSymbol_IsPrime) : new RegexSymbol(RegexSymbol_IsPrime);
                        (*thisSymbol)->lazy              = matchZero ? 0 : 1;
                        (*thisSymbol)->possessive        = isLookinto;
                        (*thisSymbol)->parentAlternative = parentAlternative;
                        (*thisSymbol)->self              = thisSymbol;
                        (*thisSymbol)->originalCode      = originalCode;
                        (*thisSymbol)->originalSymbol    = originalSymbol;
                        if (isLookinto)
                            ((RegexBackref*)*thisSymbol)->index = ((RegexGroupLookinto*)group)->backrefIndex;
                        matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_IsPrime;
                        thisSymbol++;
                        init_isPrime();
                        return true;
                    }
                }
            }

            // (?!(x(xx)+|)\1*$)
            if (!insideAlternative[+1] &&
                insideSymbol[0] && insideSymbol[0]->type==RegexSymbol_Group &&
                insideSymbol[1] && insideSymbol[1]->type==RegexSymbol_Backref && insideSymbol[1]->minCount==0 && insideSymbol[1]->maxCount==UINT_MAX &&
                insideSymbol[2] && insideSymbol[2]->type==RegexSymbol_AnchorEnd && insideSymbol[2]->minCount && !insideSymbol[3])
            {
                RegexGroup *insideGroup = (RegexGroup*)insideSymbol[0];
                if (insideGroup->type == RegexGroup_Capturing && insideGroup->minCount==1 && insideGroup->maxCount==1 &&
                    ((RegexBackref*)insideSymbol[1])->index == ((RegexGroupCapturing*)insideGroup)->backrefIndex)
                {
                    bool matchZero = true;
                    RegexPattern **innerAlternative = insideGroup->alternatives;
                    RegexSymbol **innerSymbol;
                    if (!innerAlternative[1])
                        innerSymbol = innerAlternative[0]->symbols;
                    else
                    {
                        if (innerAlternative[2])
                            return false;
                        if (!innerAlternative[0]->symbols[0] && (innerSymbol = innerAlternative[1]->symbols)[0] ||
                            !innerAlternative[1]->symbols[0] && (innerSymbol = innerAlternative[0]->symbols)[0])
                            matchZero = false;
                    }

                    RegexSymbol *innerSymbol1, *innerSymbol2;
                    if ((innerSymbol1 = innerSymbol[0]) && (innerSymbol2 = innerSymbol[1]) && !innerSymbol[2])
                    {
                        if (innerSymbol[0]->type == RegexSymbol_Group)
                        {
                            innerSymbol1 = innerSymbol[1];
                            innerSymbol2 = innerSymbol[0];
                        }
                        if (innerSymbol1->type==RegexSymbol_Character && innerSymbol1->minCount==1 && innerSymbol1->maxCount==1        && characterCanMatch(innerSymbol1) &&
                            innerSymbol2->type==RegexSymbol_Group     && innerSymbol2->minCount==1 && innerSymbol2->maxCount==UINT_MAX && !innerSymbol2->possessive)
                        {
                            RegexGroup *innerGroup = (RegexGroup*)innerSymbol2;
                            if (innerGroup->type == RegexGroup_Capturing || innerGroup->type == RegexGroup_NonCapturing)
                            {
                                RegexPattern **innermostAlternative = innerGroup->alternatives;
                                RegexSymbol **innermostSymbol;
                                if (!innermostAlternative[1])
                                {
                                    innermostSymbol = innermostAlternative[0]->symbols;
                                    if (innermostSymbol[0] && innermostSymbol[0]->type==RegexSymbol_Character && innermostSymbol[0]->minCount==2 && innermostSymbol[0]->maxCount==2 && characterCanMatch(innermostSymbol[0]) && !innermostSymbol[1])
                                    {
                                        RegexSymbol   *originalSymbol    = (*thisSymbol);
                                        const char    *originalCode      = (*thisSymbol)->originalCode;
                                        RegexPattern **parentAlternative = (*thisSymbol)->parentAlternative;

                                        bool isLookinto = group->type == RegexGroup_NegativeLookinto;
                                        *thisSymbol = isLookinto ? new RegexBackref(RegexSymbol_IsPowerOf2) : new RegexSymbol(RegexSymbol_IsPowerOf2);
                                        (*thisSymbol)->lazy              = matchZero;
                                        (*thisSymbol)->possessive        = isLookinto;
                                        (*thisSymbol)->parentAlternative = parentAlternative;
                                        (*thisSymbol)->self              = thisSymbol;
                                        (*thisSymbol)->originalCode      = originalCode;
                                        (*thisSymbol)->originalSymbol    = originalSymbol;
                                        if (isLookinto)
                                            ((RegexBackref*)*thisSymbol)->index = ((RegexGroupLookinto*)group)->backrefIndex;
                                        matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_IsPowerOf2;
                                        thisSymbol++;
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // (?!(x*)(\1\1)+$)
            if (!insideAlternative[+1] &&
                insideSymbol[0] && insideSymbol[0]->type==RegexSymbol_Group &&
                insideSymbol[1] && insideSymbol[1]->type==RegexSymbol_Group &&
                insideSymbol[2] && insideSymbol[2]->type==RegexSymbol_AnchorEnd && insideSymbol[2]->minCount && !insideSymbol[3])
            {
                RegexGroup *insideGroup1 = (RegexGroup*)insideSymbol[0];
                RegexGroup *insideGroup2 = (RegexGroup*)insideSymbol[1];
                if (        insideGroup1->type == RegexGroup_Capturing                           && insideGroup1->minCount==1 && insideGroup1->maxCount==1 && !insideGroup1->possessive && !insideGroup1->alternatives[1] &&
                    inrange(insideGroup2->type, RegexGroup_NonCapturing, RegexGroup_BranchReset) && insideGroup2->minCount==1 && insideGroup2->maxCount==UINT_MAX                       && !insideGroup2->alternatives[1])
                {
                    RegexSymbol **innerSymbol1 = insideGroup1->alternatives[0]->symbols;
                    RegexSymbol **innerSymbol2 = insideGroup2->alternatives[0]->symbols;
                    if (innerSymbol1[0] && innerSymbol1[0]->type==RegexSymbol_Character && innerSymbol1[0]->minCount<=1 && innerSymbol1[0]->maxCount==UINT_MAX && characterCanMatch(innerSymbol1[0]) && !innerSymbol1[1] &&
                        innerSymbol2[0] && innerSymbol2[0]->type==RegexSymbol_Backref   && innerSymbol2[0]->minCount==1 && innerSymbol2[0]->maxCount==1 &&
                        innerSymbol2[1] && innerSymbol2[1]->type==RegexSymbol_Backref   && innerSymbol2[1]->minCount==1 && innerSymbol2[1]->maxCount==1 && !innerSymbol2[2])
                    {
                        Uint backrefIndex = ((RegexGroupCapturing*)insideGroup1)->backrefIndex;
                        if (((RegexBackref*)innerSymbol2[0])->index == backrefIndex &&
                            ((RegexBackref*)innerSymbol2[1])->index == backrefIndex)
                        {
                            RegexSymbol   *originalSymbol    = (*thisSymbol);
                            const char    *originalCode      = (*thisSymbol)->originalCode;
                            RegexPattern **parentAlternative = (*thisSymbol)->parentAlternative;

                            bool isLookinto = group->type == RegexGroup_NegativeLookinto;
                            *thisSymbol = isLookinto ? new RegexBackref(RegexSymbol_IsPowerOf2) : new RegexSymbol(RegexSymbol_IsPowerOf2);
                            (*thisSymbol)->lazy              = (bool&)innerSymbol1[0]->minCount; // can be cast by reinterpretation since the value has already been narrowed down to being 0 or 1
                            (*thisSymbol)->possessive        = isLookinto;
                            (*thisSymbol)->parentAlternative = parentAlternative;
                            (*thisSymbol)->self              = thisSymbol;
                            (*thisSymbol)->originalCode      = originalCode;
                            (*thisSymbol)->originalSymbol    = originalSymbol;
                            if (isLookinto)
                                ((RegexBackref*)*thisSymbol)->index = ((RegexGroupLookinto*)group)->backrefIndex;
                            matchFunction(*thisSymbol) = &RegexMatcher<USE_STRINGS>::matchSymbol_IsPowerOf2;
                            thisSymbol++;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

template <bool USE_STRINGS>
template <typename MATCH_TYPE>
ALWAYS_INLINE int8 RegexMatcher<USE_STRINGS>::runtimeOptimize_matchSymbol_Character_or_Backref(RegexSymbol *const thisSymbol, Uint64 const multiple, MATCH_TYPE const repetend)
// return nonzero if this optimizer function handled the match and the caller should do nothing further: +1 if repetend matched at least once, -1 if it matched zero times
{
    if (optimizationLevel && !thisSymbol->possessive)
    {
        RegexSymbol *nextSymbol;
        RegexSymbol **nextSymbolPtr = symbol + 1;
        while (*nextSymbolPtr && ((*nextSymbolPtr)->type == RegexSymbol_IsPrime || (*nextSymbolPtr)->type == RegexSymbol_IsPowerOf2 || (*nextSymbolPtr)->type == RegexSymbol_Group && ((RegexGroup*)*nextSymbolPtr)->isNegativeLookaround()))
            nextSymbolPtr++;
        nextSymbol = *nextSymbolPtr;
        if (nextSymbol && nextSymbol->type==RegexSymbol_AnchorEnd)
        {
            Uint64 spaceLeft = input - position;
            currentMatch = spaceLeft / multiple;
            if (!inrange64(currentMatch, thisSymbol->minCount, MAX_EXTEND(thisSymbol->maxCount)))
            {
                nonMatch();
                return -1;
            }
            if (!doesRepetendMatch(repetend, multiple, currentMatch))
            {
                nonMatch();
                return -1;
            }
            int8 matched = currentMatch != 0 ? +1 : -1;
            position     = input - spaceLeft % multiple;
            currentMatch = ULLONG_MAX;
            symbol++;
            return matched;
        }
        RegexGroup *thisGroup = groupStackTop->group;
        bool afterEndOfGroup = false;
        if (nextSymbol && nextSymbol->type==RegexSymbol_Backref && nextSymbolPtr[+1] && nextSymbolPtr[+1]->type==RegexSymbol_AnchorEnd && nextSymbol->minCount==1 && nextSymbol->maxCount==1)
        {
            Uint64 subtract = captures[((RegexBackref*)nextSymbol)->index];
            if (subtract == NON_PARTICIPATING_CAPTURE_GROUP)
            {
                if (!emulate_ECMA_NPCGs)
                {
                    nonMatch();
                    return -1;
                }
                subtract = 0;
            }
            Uint64 spaceLeft = input - position;
            if (subtract > spaceLeft || (spaceLeft - subtract) % multiple != 0)
            {
                nonMatch();
                return -1;
            }
        }
        if (nextSymbol && nextSymbol->type==RegexSymbol_Group ||
            !nextSymbol && !alternative[+1] && groupStackTop > groupStackBase
                        && (thisGroup->type==RegexGroup_Capturing || thisGroup->type==RegexGroup_NonCapturing)
                        && groupStackTop->loopCount==MAX_EXTEND(thisGroup->maxCount) && (nextSymbol = thisGroup->self[+1])
                        && (afterEndOfGroup=true))
        {
            if (afterEndOfGroup && nextSymbol->type!=RegexSymbol_Group)
            {
                if (nextSymbol->type==RegexSymbol_Backref && thisGroup->type==RegexGroup_Capturing && ((RegexGroupCapturing*)thisGroup)->backrefIndex == ((RegexBackref*)nextSymbol)->index &&
                    optimizationLevel >= 2 && !thisSymbol->lazy && nextSymbol->minCount==nextSymbol->maxCount)
                {
                    Uint64 divisor = 1 + nextSymbol->minCount;
                    RegexSymbol *nextSymbolAfter = thisGroup->self[+2];
                    if (nextSymbolAfter && nextSymbolAfter->type == RegexSymbol_Group)
                    {
                        RegexGroup *group = (RegexGroup*)nextSymbolAfter;
                        if (group->type != RegexGroup_NegativeLookahead && !group->alternatives[1] && group->minCount==1 && group->maxCount==1)
                        {
                            RegexSymbol **lookaheadSymbol = group->alternatives[0]->symbols;
                            if (*lookaheadSymbol && (*lookaheadSymbol)->type==RegexSymbol_Backref && ((RegexGroupCapturing*)thisGroup)->backrefIndex == ((RegexBackref*)*lookaheadSymbol)->index &&
                                (*lookaheadSymbol)->minCount==(*lookaheadSymbol)->maxCount)
                            {
                                divisor += (*lookaheadSymbol)->minCount;
                                nextSymbolAfter = lookaheadSymbol[+1];
                            }
                        }
                    }

                    Uint64 alreadyCaptured = position - groupStackTop->position;
                    Uint64 spaceLeft = input - position;
                    currentMatch = (alreadyCaptured + spaceLeft) / divisor;
                    if (currentMatch < alreadyCaptured)
                    {
                        nonMatch();
                        return -1;
                    }
                    currentMatch -= alreadyCaptured;
                    currentMatch /= multiple;
                    if (currentMatch < thisSymbol->minCount)
                    {
                        nonMatch();
                        return -1;
                    }
                    if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                        currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                    if (nextSymbolAfter && nextSymbolAfter->type == RegexSymbol_AnchorEnd)
                    {
                        if (!doesRepetendMatch(repetend, multiple, currentMatch))
                        {
                            nonMatch();
                            return -1;
                        }
                    }
                    else
                    {
                        if (USE_STRINGS && repetend)
                        {
                            countRepetendMatches(repetend, multiple);
                            if (currentMatch < thisSymbol->minCount)
                            {
                                nonMatch();
                                return -1;
                            }
                        }
                        pushStack();
                    }
                    int8 matched = currentMatch != 0 ? +1 : -1;
                    position += currentMatch * multiple;
                    currentMatch = ULLONG_MAX;
                    symbol++;
                    return matched;
                }
            }
            else
            {
                RegexGroup *group = (RegexGroup*)nextSymbol;
                RegexSymbol **lookaheadSymbol;
                if (group->type==RegexGroup_Lookahead && !group->alternatives[1] && group->minCount && *(lookaheadSymbol = group->alternatives[0]->symbols))
                {
                    Uint64 totalLength = 0;
                    bool cannotMatch = false;
                    Uint64 multiplication = 0;
                    for (;;)
                    {
                        RegexSymbol *currentSymbol = *lookaheadSymbol;
                        if (currentSymbol->type == RegexSymbol_Backref)
                        {
                            if (afterEndOfGroup && thisGroup->type==RegexGroup_Capturing && ((RegexGroupCapturing*)thisGroup)->backrefIndex == ((RegexBackref*)currentSymbol)->index)
                            {
                                if (currentSymbol->minCount != currentSymbol->maxCount)
                                    break;
                                if (lookaheadSymbol == group->alternatives[0]->symbols && optimizationLevel >= 2)
                                {
                                    if (lookaheadSymbol[+1] ? (lookaheadSymbol[+1]->type == RegexSymbol_AnchorEnd) : !thisSymbol->lazy)
                                    {
                                        if (totalLength > input || cannotMatch)
                                        {
                                            nonMatch();
                                            return -1;
                                        }
                                        Uint64 target = input - totalLength;
                                        if (position > target)
                                        {
                                            nonMatch();
                                            return -1;
                                        }
                                        Uint64 spaceLeft = target - groupStackTop->position;
                                        currentMatch = spaceLeft / (1 + currentSymbol->minCount);
                                        if (currentMatch < position - groupStackTop->position)
                                        {
                                            nonMatch();
                                            return -1;
                                        }
                                        currentMatch -= position - groupStackTop->position;
                                        currentMatch /= multiple;
                                        if (currentMatch < thisSymbol->minCount)
                                        {
                                            nonMatch();
                                            return -1;
                                        }
                                        if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                                            currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                                        if (lookaheadSymbol[+1]) // anchored?
                                        {
                                            if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                            {
                                                nonMatch();
                                                return -1;
                                            }
                                        }
                                        else
                                        {
                                            if (USE_STRINGS && repetend)
                                            {
                                                countRepetendMatches(repetend, multiple);
                                                if (currentMatch < thisSymbol->minCount)
                                                {
                                                    nonMatch();
                                                    return -1;
                                                }
                                            }
                                            pushStack();
                                        }
                                        int8 matched = currentMatch != 0 ? +1 : -1;
                                        position += currentMatch * multiple;
                                        currentMatch = ULLONG_MAX;
                                        symbol++;
                                        return matched;
                                    }
                                }
                                break;
                            }
                            Uint64 thisCapture = captures[((RegexBackref*)currentSymbol)->index];
                            if (thisCapture != NON_PARTICIPATING_CAPTURE_GROUP)
                            {
                                totalLength += thisCapture * currentSymbol->minCount;
                                if (currentSymbol->minCount != currentSymbol->maxCount)
                                {
                                    if (currentSymbol->maxCount == UINT_MAX && lookaheadSymbol[+1] && lookaheadSymbol[+1]->type==RegexSymbol_AnchorEnd && optimizationLevel >= 2)
                                    {
                                        multiplication = thisCapture;
                                        goto do_optimization;
                                    }
                                    break;
                                }
                            }
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
                        if (!*lookaheadSymbol)
                        {
                            if (thisSymbol->lazy)
                                break;
                            goto do_optimization;
                        }
                        if ((*lookaheadSymbol)->type==RegexSymbol_AnchorEnd)
                        {
                        do_optimization:
                            if (totalLength > input || cannotMatch)
                            {
                                nonMatch();
                                return -1;
                            }
                            Uint64 target = input - totalLength;
                            if (position > target)
                            {
                                nonMatch();
                                return -1;
                            }
                            Uint64 spaceLeft = target - position;
                            RegexGroup *multiplicationGroup = NULL;
                            RegexSymbol **multiplicationAnchor;
                            Uint64 totalLengthSmallerFactor;
                            tellCompilerVariableIsntUninitialized(multiplicationAnchor);
                            tellCompilerVariableIsntUninitialized(totalLengthSmallerFactor);
                            if (multiplication)
                            {
                                RegexSymbol *afterLookahead = group->self[+1];
                                bool lazinessDoesntMatter = afterLookahead && afterLookahead->type==RegexSymbol_Backref &&
                                                            afterLookahead->minCount == 0 && afterLookahead->maxCount == UINT_MAX && !afterLookahead->lazy &&
                                                            ((RegexBackref*)afterLookahead)->index == ((RegexBackref*)currentSymbol)->index;
                                if (!USE_STRINGS && (lazinessDoesntMatter || !thisSymbol->lazy && thisSymbol->maxCount == UINT_MAX))
                                {
                                    if (lazinessDoesntMatter)
                                        afterLookahead = group->self[+2];
                                    if (afterLookahead && afterLookahead->type==RegexSymbol_Group && afterLookahead->minCount==1 && afterLookahead->maxCount==1)
                                    {
                                        RegexGroup *outsideGroup = afterEndOfGroup ? groupStackTop[-1].group : thisGroup;
                                        if (outsideGroup->type==RegexGroup_Lookahead && !outsideGroup->alternatives[1])
                                        {
                                            RegexGroup *afterGroup = (RegexGroup*)afterLookahead;
                                            RegexSymbol **afterSymbol = afterGroup->alternatives[0]->symbols;
                                            totalLengthSmallerFactor = 0;
                                            for (; *afterSymbol; afterSymbol++)
                                            {
                                                if ((*afterSymbol)->type == RegexSymbol_Backref)
                                                {
                                                    Uint64 afterCapture = captures[((RegexBackref*)(*afterSymbol))->index];
                                                    if ((*afterSymbol)->minCount == (*afterSymbol)->maxCount)
                                                    {
                                                        if (afterCapture != NON_PARTICIPATING_CAPTURE_GROUP)
                                                            totalLengthSmallerFactor += afterCapture * (*afterSymbol)->minCount;
                                                        else
                                                        {
                                                            if ((*afterSymbol)->minCount && !emulate_ECMA_NPCGs)
                                                                break;
                                                        }
                                                    }
                                                    else
                                                    if ((*afterSymbol)->minCount==1 && (*afterSymbol)->maxCount==UINT_MAX && afterSymbol[+1]->type==RegexSymbol_AnchorEnd &&
                                                        totalLengthSmallerFactor <= multiplication && afterCapture+1 == multiplication)
                                                    {
                                                        lazinessDoesntMatter = true;
                                                        multiplicationGroup = afterGroup;
                                                        multiplicationAnchor = &afterSymbol[+1];
                                                        break;
                                                    }
                                                    else
                                                        break;
                                                }
                                                else
                                                if ((*afterSymbol)->type == RegexSymbol_Character && (*afterSymbol)->minCount == (*afterSymbol)->maxCount)
                                                    totalLengthSmallerFactor += (*afterSymbol)->minCount;
                                            }
                                        }
                                    }
                                }
                                if (lazinessDoesntMatter || thisSymbol->lazy)
                                {
                                    Uint64 minMatch = thisSymbol->minCount * multiple;
                                    spaceLeft = (spaceLeft - minMatch) % multiplication + minMatch;
                                }
                                if (!lazinessDoesntMatter)
                                    lookaheadSymbol = NULL;
                            }
                            currentMatch     = spaceLeft / multiple;
                            Uint64 remainder = spaceLeft % multiple;
                            if (currentMatch < thisSymbol->minCount)
                            {
                                nonMatch();
                                return -1;
                            }
                            if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                                currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                            if (lookaheadSymbol && *lookaheadSymbol) // anchored?
                            {
                                if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                {
                                    nonMatch();
                                    return -1;
                                }
                            }
                            else
                            {
                                if (USE_STRINGS && repetend)
                                {
                                    countRepetendMatches(repetend, multiple);
                                    if (currentMatch < thisSymbol->minCount)
                                    {
                                        nonMatch();
                                        return -1;
                                    }
                                }
                                if (currentMatch != (thisSymbol->lazy ? MAX_EXTEND(thisSymbol->maxCount) : thisSymbol->minCount))
                                    pushStack();
                            }
                            int8 matched = currentMatch != 0 ? +1 : -1;
                            //position = target - spaceLeft % multiple;
                            position += currentMatch * multiple;
                            currentMatch = ULLONG_MAX;
                            symbol++;
                            if (multiplicationGroup && remainder == 0)
                            {
                                spaceLeft = input - position;
                                // todo: check for overflow
                                Uint64 product = (totalLengthSmallerFactor ? totalLengthSmallerFactor : multiplication-1) * multiplication;
                                position = input - product;
                                if (afterEndOfGroup)
                                    thisGroup->lazy ? leaveLazyGroup() : leaveMaxedOutGroup();
                                if (spaceLeft < product)
                                {
                                    nonMatch();
                                    return -1;
                                }
                                enterGroup(multiplicationGroup);
                                symbol = multiplicationAnchor;
                                position = input;
                            }
                            return matched;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
