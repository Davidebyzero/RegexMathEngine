#include "math-optimization.h"

template <bool USE_STRINGS>
void RegexMatcher<USE_STRINGS>::matchSymbol_IsPrime(RegexSymbol *thisSymbol)
{
    Uint64 spaceLeft = input - position;
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
    Uint64 spaceLeft = input - position;
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
    if (optimizationLevel >= 2)
    {
        RegexGroup *const group = (RegexGroup*)(*thisSymbol);
        if (!USE_STRINGS && group->type == RegexGroup_NegativeLookahead && group->minCount)
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

                        *thisSymbol = new RegexSymbol(RegexSymbol_IsPrime);
                        (*thisSymbol)->lazy              = matchZero ? 0 : 1;
                        (*thisSymbol)->parentAlternative = parentAlternative;
                        (*thisSymbol)->self              = thisSymbol;
                        (*thisSymbol)->originalCode      = originalCode;
                        (*thisSymbol)->originalSymbol    = originalSymbol;
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

                                        *thisSymbol = new RegexSymbol(RegexSymbol_IsPowerOf2);
                                        (*thisSymbol)->lazy              = matchZero;
                                        (*thisSymbol)->parentAlternative = parentAlternative;
                                        (*thisSymbol)->self              = thisSymbol;
                                        (*thisSymbol)->originalCode      = originalCode;
                                        (*thisSymbol)->originalSymbol    = originalSymbol;
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

                            *thisSymbol = new RegexSymbol(RegexSymbol_IsPowerOf2);
                            (*thisSymbol)->lazy              = (bool&)innerSymbol1[0]->minCount; // can be cast by reinterpretation since the value has already been narrowed down to being 0 or 1
                            (*thisSymbol)->parentAlternative = parentAlternative;
                            (*thisSymbol)->self              = thisSymbol;
                            (*thisSymbol)->originalCode      = originalCode;
                            (*thisSymbol)->originalSymbol    = originalSymbol;
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
ALWAYS_INLINE bool RegexMatcher<USE_STRINGS>::runtimeOptimize_matchSymbol_Character_or_Backref(RegexSymbol *const thisSymbol, Uint64 const multiple, MATCH_TYPE const repetend)
// return true if this optimizer function handled the match and the caller should do nothing further
{
    if (optimizationLevel && !thisSymbol->possessive)
    {
        if (symbol[+1] && symbol[+1]->type==RegexSymbol_AnchorEnd)
        {
            Uint64 spaceLeft = input - position;
            currentMatch = spaceLeft / multiple;
            if (!inrange64(currentMatch, thisSymbol->minCount, MAX_EXTEND(thisSymbol->maxCount)))
            {
                nonMatch();
                return true;
            }
            if (!doesRepetendMatch(repetend, multiple, currentMatch))
            {
                nonMatch();
                return true;
            }
            position     = input - spaceLeft % multiple;
            currentMatch = ULLONG_MAX;
            symbol++;
            return true;
        }
        RegexSymbol *nextSymbol = symbol[+1];
        RegexGroup *thisGroup = groupStackTop->group;
        bool afterEndOfGroup = false;
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
                        return true;
                    }
                    currentMatch -= alreadyCaptured;
                    currentMatch /= multiple;
                    if (currentMatch < thisSymbol->minCount)
                    {
                        nonMatch();
                        return true;
                    }
                    if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                        currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                    if (nextSymbolAfter && nextSymbolAfter->type == RegexSymbol_AnchorEnd)
                    {
                        if (!doesRepetendMatch(repetend, multiple, currentMatch))
                        {
                            nonMatch();
                            return true;
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
                                return true;
                            }
                        }
                        pushStack();
                    }
                    position += currentMatch * multiple;
                    currentMatch = ULLONG_MAX;
                    symbol++;
                    return true;
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
                                            return true;
                                        }
                                        Uint64 target = input - totalLength;
                                        if (position > target)
                                        {
                                            nonMatch();
                                            return true;
                                        }
                                        Uint64 spaceLeft = target - groupStackTop->position;
                                        currentMatch = spaceLeft / (multiple * (1 + currentSymbol->minCount));
                                        if (currentMatch < position - groupStackTop->position)
                                        {
                                            nonMatch();
                                            return true;
                                        }
                                        currentMatch -= position - groupStackTop->position;
                                        if (currentMatch < thisSymbol->minCount)
                                        {
                                            nonMatch();
                                            return true;
                                        }
                                        if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                                            currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                                        if (lookaheadSymbol[+1]) // anchored?
                                        {
                                            if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                            {
                                                nonMatch();
                                                return true;
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
                                                    return true;
                                                }
                                            }
                                            pushStack();
                                        }
                                        position += currentMatch * multiple;
                                        currentMatch = ULLONG_MAX;
                                        symbol++;
                                        return true;
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
                                return true;
                            }
                            Uint64 target = input - totalLength;
                            if (position > target)
                            {
                                nonMatch();
                                return true;
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
                                return true;
                            }
                            if (currentMatch > MAX_EXTEND(thisSymbol->maxCount))
                                currentMatch = MAX_EXTEND(thisSymbol->maxCount);
                            if (lookaheadSymbol && *lookaheadSymbol) // anchored?
                            {
                                if (!doesRepetendMatch(repetend, multiple, currentMatch))
                                {
                                    nonMatch();
                                    return true;
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
                                        return true;
                                    }
                                }
                                if (currentMatch != (thisSymbol->lazy ? MAX_EXTEND(thisSymbol->maxCount) : thisSymbol->minCount))
                                    pushStack();
                            }
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
                                    return true;
                                }
                                enterGroup(multiplicationGroup);
                                symbol = multiplicationAnchor;
                                position = input;
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}
