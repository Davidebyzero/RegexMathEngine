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
#include "parser.h"
#include <stack>

struct anchorStackNode
{
    bool allAlternativesAreAnchored;
    bool currentAlternativeAnchored;
    anchorStackNode(int) : allAlternativesAreAnchored(true), currentAlternativeAnchored(false) {}
};

RegexSymbolType RegexParser::symbolWithLowercaseOpposite(RegexSymbolType neg, RegexSymbolType pos, char ch, char chNeg)
{
    if (pos-neg != 1)
        THROW_ENGINEBUG;
    return (RegexSymbolType)(neg + ((ch - chNeg) >> 5));
}

void RegexParser::addSymbol(const char *buf, RegexSymbol *newSymbol)
{
    newSymbol->originalCode = buf;
    stack->symbols.push(newSymbol);
    symbolCountSpecified = false;
    symbolLazinessSpecified = false;
}

void RegexParser::fixLookaheadQuantifier()
{
    // It makes no difference whether a lookahead is repeated once or an infinite number of times, so limit them to 1 iteration.
    // But in persistent backrefs mode this isn't true (unless it's a negative lookaround), as captures made in the lookaround can be influenced by their previous values.
    if (symbol->type == RegexSymbol_Group && ((RegexGroup*)symbol)->isLookaround())
    {
        if (!enable_persistent_backrefs || ((RegexGroup*)symbol)->isNegativeLookaround())
        {
            symbol->minCount = symbol->minCount ? 1 : 0;
            symbol->maxCount = symbol->maxCount ? 1 : 0;
        }
        // In no-empty-optional mode, the maxCount might as well be set equal to the minCount for a lookaround, because the match will always be empty.
        if (no_empty_optional)
            symbol->maxCount = symbol->minCount;
    }
}

void RegexParser::skipWhitespace(const char *&buf)
{
    while (*buf==' ' || *buf=='\t' || *buf=='\v' || *buf=='\r' || *buf=='\n')
        buf++;
}

void RegexParser::closeAlternative(RegexSymbol **&symbols, std::queue<RegexSymbol*> &symbolQueue)
{
    RegexSymbol **potentialString = NULL;
    Uint potentialStringLength = 0;
    int firstChar;
    bool moreThanOneTypeOfChar;
    bool wildcardUsed = false;

    symbols = (RegexSymbol**)malloc((symbolQueue.size() + 1) * sizeof(RegexSymbol*));
    size_t i;
    for (i=0;; i++)
    {
        if (symbolQueue.empty())
            symbols[i] = NULL;
        else
        {
            symbols[i] = symbolQueue.front();
            symbolQueue.pop();
            symbols[i]->self = &symbols[i];
        }

        if (symbols[i] && symbols[i]->type == RegexSymbol_Character && symbols[i]->minCount && symbols[i]->minCount == symbols[i]->maxCount)
        {
            int nextChar = (wildcardUsed |= symbols[i]->characterAny) ? -1 : symbols[i]->character;
            if (!potentialString)
            {
                potentialString = &symbols[i];
                firstChar = nextChar;
                moreThanOneTypeOfChar = false;
                wildcardUsed = false;
            }
            else
            if (nextChar != firstChar)
                moreThanOneTypeOfChar = true;
            potentialStringLength += symbols[i]->minCount;
        }
        else
        {
            size_t symbolCount = &symbols[i] - potentialString;
            if (potentialString && symbolCount >= 2)
            {
                if (!moreThanOneTypeOfChar)
                {
                    RegexSymbol *symbolAfter = symbols[i];

                    RegexSymbol *charactersSymbol = new RegexSymbol(RegexSymbol_Character);
                    charactersSymbol->originalCode = (*potentialString)->originalCode;
                    charactersSymbol->self = potentialString;
                    for (RegexSymbol **sym = potentialString; sym < &symbols[i]; sym++)
                        delete *sym;
                    charactersSymbol->minCount = potentialStringLength;
                    charactersSymbol->maxCount = potentialStringLength;
                    charactersSymbol->lazy = false;
                    charactersSymbol->possessive = false;
                    charactersSymbol->characterAny = firstChar < 0;
                    charactersSymbol->character = firstChar;

                    i = potentialString - symbols;
                    symbols[i] = charactersSymbol;
                    i++;

                    symbols[i] = symbolAfter;
                    if (symbolAfter)
                        symbols[i]->self = &symbols[i];
                }
                else
                if (potentialStringLength - symbolCount <= symbolCount && firstChar >= 0 && !wildcardUsed)
                {
                    RegexSymbol *symbolAfter = symbols[i];

                    RegexSymbol *stringSymbol = new RegexSymbol(RegexSymbol_String);
                    stringSymbol->originalCode = (*potentialString)->originalCode;
                    stringSymbol->self = potentialString;
                    stringSymbol->string = new char [potentialStringLength + 1];
                    size_t len = 0;
                    for (RegexSymbol **sym = potentialString; sym < &symbols[i]; sym++)
                    {
                        memset(stringSymbol->string + len, (*sym)->character, (*sym)->minCount);
                        len += (*sym)->minCount;
                        delete *sym;
                    }
                    stringSymbol->string[len] = '\0';
                    stringSymbol->strLength = len;

                    i = potentialString - symbols;
                    symbols[i] = stringSymbol;
                    i++;

                    symbols[i] = symbolAfter;
                    if (symbolAfter)
                        symbols[i]->self = &symbols[i];
                }
            }
            if (!symbols[i])
                break;
            potentialString = NULL;
            potentialStringLength = 0;
        }
    }
}

void RegexParser::closeGroup(RegexPattern **&alternatives, std::queue<RegexPattern*> &patternQueue)
{
    alternatives = new RegexPattern* [patternQueue.size() + 1];
    Uint i;
    for (i=0; !patternQueue.empty(); i++)
    {
        alternatives[i] = patternQueue.front();
        patternQueue.pop();
        for (RegexSymbol **symbol = alternatives[i]->symbols; *symbol; symbol++)
            (*symbol)->parentAlternative = &alternatives[i];
    }
    alternatives[i] = NULL;
}

RegexGroup *RegexParser::parseLookinto(const char *&buf)
{
    if (!allow_lookinto)
        throw RegexParsingError(buf, "Unrecognized character after (?");
    RegexGroup *group;
    Uint backrefIndex = UINT_MAX;
    if (inrange(*buf, '0', '9'))
    {
        try
        {
            backrefIndex = readNumericConstant<Uint>(buf);
        }
        catch (ParsingError)
        {
            throw RegexParsingError(buf, "Group number is too big");
        }
    }
    switch (*buf)
    {
    case '=':                                                                                                     group = new RegexGroupLookinto(RegexGroup_Lookinto         , backrefIndex); break;
    case '*': if (!allow_molecular_lookaround) throw RegexParsingError(buf, "Unrecognized character after (?^");  group = new RegexGroupLookinto(RegexGroup_LookintoMolecular, backrefIndex); break;
    case '!':                                                                                                     group = new RegexGroupLookinto(RegexGroup_NegativeLookinto , backrefIndex); break;
    default:                                   throw RegexParsingError(buf, "Unrecognized character after (?^");
    }
    buf++;
    return group;
}

RegexParser::RegexParser(RegexGroupRoot &regex, const char *buf)
{
    regex.originalCode = buf;
    regex.parentAlternative = NULL;

    stack = new ParsingStack;
    stack->below = NULL;
    stack->group = &regex;
    stack->alternatives.push(new RegexPattern);
    symbol = NULL;
    backrefIndex = 0;
    maxGroupDepth = 1;
    curGroupDepth = 1;
    maxLookintoDepth = 0;
    curLookintoDepth = 0;

    for (;;)
    {
        switch (*buf)
        {
        case ' ':
        case '\t':
        case '\v':
        case '\r':
        case '\n':
            if (free_spacing_mode)
            {
                do
                    buf++;
                while (*buf==' ' || *buf=='\t' || *buf=='\v' || *buf=='\r' || *buf=='\n');
                break;
            }
            // else fall through
        literal_char:
        default:
            symbol = new RegexSymbol(RegexSymbol_Character);
            symbol->characterAny = false;
            symbol->character = *buf;
            addSymbol(buf++, symbol);
            break;
        case '#':
            if (!free_spacing_mode)
                goto literal_char;
            for (;;)
            {
                buf++;
                if (*buf == '\0')
                    break;
                if (*buf == '\n')
                {
                    buf++;
                    break;
                }
            }
            break;
        case '^':
            addSymbol(buf++, symbol = new RegexSymbol(RegexSymbol_AnchorStart));
            if (!allow_quantifiers_on_assertions) symbol = NULL;
            break;
        case '$':
            addSymbol(buf++, symbol = new RegexSymbol(RegexSymbol_AnchorEnd));
            if (!allow_quantifiers_on_assertions) symbol = NULL;
            break;
        case '.':
            addSymbol(buf++, symbol = new RegexSymbol(RegexSymbol_Character));
            symbol->characterAny = true;
            break;
        case '[':
            {
                const char *buf0 = buf;
                buf++;
                bool inverted = *buf == '^';
                if (inverted)
                    buf++;
                Uint8 allowedChars[256/8];
                memset(allowedChars, 0, sizeof(allowedChars));
                int inRange = 0; /* -1 = the last char was part of an escape code that cannot be part of a range
                                     0 = a hyphen at this point will be treated as a literal
                                     1 = a hyphen at this point will denote a range unless it's the last character between the brackets
                                     2 = the upcoming character or escape code will denote the end of a range */
                Uint8 ch, firstCharInRange;
                for (bool firstCharInCharacterClass = true;; firstCharInCharacterClass=false)
                {
                    if (!*buf)
                        throw RegexParsingError(buf, "Missing terminating ] for character class");
                    if (*buf == ']' && (!firstCharInCharacterClass || allow_empty_character_classes))
                    {
                        buf++;
                        break;
                    }
                    if (*buf == '\\')
                    {
                        switch (*++buf)
                        {
                        case '0': ch = '\0'; goto process_char_for_charClass;
                        case 'b': ch = '\b'; goto process_char_for_charClass;
                        case 't': ch = '\t'; goto process_char_for_charClass;
                        case 'n': ch = '\n'; goto process_char_for_charClass;
                        case 'v': ch = '\v'; goto process_char_for_charClass;
                        case 'f': ch = '\f'; goto process_char_for_charClass;
                        case 'r': ch = '\r'; goto process_char_for_charClass;
                        default:  ch = *buf; goto process_char_for_charClass;
                        case 'D':
                            {
                                if (inRange == 2)
                                    throw RegexParsingError(buf, "Invalid range in character class");
                                Uint16 backup = *(Uint16*)(allowedChars + '0'/8) & (((1 << ('9'-'0'+1)) - 1) << ('0'%8));
                                memset(allowedChars, 0xFF, sizeof(allowedChars));
                                *(Uint16*)(allowedChars + '0'/8) |= backup;
                                inRange = -1;
                                buf++;
                                break;
                            }
                        case 'd':
                            if (inRange == 2)
                                throw RegexParsingError(buf, "Invalid range in character class");
                            *(Uint16*)(allowedChars + '0'/8) |= ((1 << ('9'-'0'+1)) - 1) << ('0'%8);
                            inRange = -1;
                            buf++;
                            break;
                        case 'S':
                            {
                                if (inRange == 2)
                                    throw RegexParsingError(buf, "Invalid range in character class");
                                Uint8 backup1 = allowedChars[1] & ((1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)); // '\t','\n','\v','\f','\r'
                                Uint8 backup2 = allowedChars[(Uchar)' ' /8] & (1 << ((Uchar)' ' %8));
                                Uint8 backup3 = allowedChars[(Uchar)0xA0/8] & (1 << ((Uchar)0xA0%8)); // non-breaking space; WARNING: may not be portable
                                memset(allowedChars, 0xFF, sizeof(allowedChars));
                                allowedChars[1] |= backup1;
                                allowedChars[(Uchar)' ' /8] |= backup2;
                                allowedChars[(Uchar)0xA0/8] |= backup3; // non-breaking space; WARNING: may not be portable
                                inRange = -1;
                                buf++;
                                break;
                            }
                        case 's':
                            if (inRange == 2)
                                throw RegexParsingError(buf, "Invalid range in character class");
                            allowedChars[1] |= (1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5); // '\t','\n','\v','\f','\r'
                            allowedChars[(Uchar)' ' /8] |= 1 << ((Uchar)' ' %8);
                            allowedChars[(Uchar)0xA0/8] |= 1 << ((Uchar)0xA0%8); // non-breaking space; WARNING: may not be portable
                            inRange = -1;
                            buf++;
                            break;
                        case 'W':
                            {
                                if (inRange == 2)
                                    throw RegexParsingError(buf, "Invalid range in character class");
                                Uint16 backup1 = *(Uint16*)(allowedChars + '0'/8) & (((1 << ('9'-'0'+1)) - 1) << ('0'%8));
                                Uint32 backup2 = *(Uint32*)(allowedChars + 'A'/8) & (((1 << ('Z'-'A'+1)) - 1) << ('A'%8));
                                Uint32 backup3 = *(Uint32*)(allowedChars + 'a'/8) & (((1 << ('z'-'a'+1)) - 1) << ('a'%8));
                                Uint8  backup4 = allowedChars['_'/8] & (1 << ('_'%8));
                                memset(allowedChars, 0xFF, sizeof(allowedChars));
                                *(Uint16*)(allowedChars + '0'/8) |= backup1;
                                *(Uint32*)(allowedChars + 'A'/8) |= backup2;
                                *(Uint32*)(allowedChars + 'a'/8) |= backup3;
                                allowedChars['_'/8] |= backup4;
                                inRange = -1;
                                buf++;
                                break;
                            }
                        case 'w':
                            if (inRange == 2)
                                throw RegexParsingError(buf, "Invalid range in character class");
                            *(Uint16*)(allowedChars + '0'/8) |= ((1 << ('9'-'0'+1)) - 1) << ('0'%8);
                            *(Uint32*)(allowedChars + 'A'/8) |= ((1 << ('Z'-'A'+1)) - 1) << ('A'%8);
                            *(Uint32*)(allowedChars + 'a'/8) |= ((1 << ('z'-'a'+1)) - 1) << ('a'%8);
                            allowedChars['_'/8] |= 1 << ('_'%8);
                            inRange = -1;
                            buf++;
                            break;
                        }
                    }
                    else
                    {
                        if (*buf == '-' && buf[+1] != ']')
                        {
                            if (inRange == 1)
                            {
                                inRange = 2;
                                firstCharInRange = ch;
                                buf++;
                                continue;
                            }
                            else
                            if (inRange < 0)
                                throw RegexParsingError(buf, "Invalid range in character class");
                        }
                        ch = *buf;
                    process_char_for_charClass:
                        if (inRange == 2)
                        {
                            if (firstCharInRange > ch)
                                throw RegexParsingError(buf, "Range out of order in character class");
                            Uint byte0 = firstCharInRange / 8;
                            Uint  bit0 = firstCharInRange % 8;
                            Uint byte1 = (ch + 1) / 8;
                            Uint  bit1 = (ch + 1) % 8;
                            if (byte0 == byte1)
                                allowedChars[byte0] |= (1 << bit1) - (1 << bit0);
                            else
                            {
                                allowedChars[byte0] |= -(1 << bit0);
                                allowedChars[byte1] |=  (1 << bit1) - 1;
                                memset(allowedChars + byte0 + 1, 0xFF, ((Uint)ch + 1)/8 - byte0 - 1);
                            }
                            inRange = 0;
                        }
                        else
                        {
                            allowedChars[ch / 8] |= 1 << (ch % 8);
                            inRange = 1;
                        }
                        buf++;
                    }
                }
                if (inverted)
                {
                    ((Uint64*)allowedChars)[0] = ~((Uint64*)allowedChars)[0];
                    ((Uint64*)allowedChars)[1] = ~((Uint64*)allowedChars)[1];
                    ((Uint64*)allowedChars)[2] = ~((Uint64*)allowedChars)[2];
                    ((Uint64*)allowedChars)[3] = ~((Uint64*)allowedChars)[3];
                }
                addSymbol(buf0, symbol = new RegexCharacterClass(allowedChars));
                break;
            }
        case '(':
            {
                const char *bufGroup = buf;
                buf++;
                RegexGroup *group;
                RegexGroup *lookaroundCondition = NULL;
                switch (*buf)
                {
                case '?':
                    switch (buf[1])
                    {
                    case ':':                                                                                                    buf+=2; group = new RegexGroup(RegexGroup_NonCapturing);       break;
                    case '>': if (!allow_atomic_groups       ) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; group = new RegexGroup(RegexGroup_Atomic);             break;
                    case '|': if (!allow_branch_reset_groups ) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; group = new RegexGroup(RegexGroup_BranchReset);        break;
                    case '=':                                                                                                    buf+=2; group = new RegexGroup(RegexGroup_Lookahead);          break;
                    case '*': if (!allow_molecular_lookaround) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; group = new RegexGroup(RegexGroup_LookaheadMolecular); break;
                    case '!':                                                                                                    buf+=2; group = new RegexGroup(RegexGroup_NegativeLookahead);  break;
                    case '^':                                                                                                    buf+=2; group = parseLookinto(buf);                            break;
                    case '(':
                        if (!allow_conditionals && !allow_lookaround_conditionals)
                            throw RegexParsingError(buf, "Unrecognized character after (?");
                        buf+=2;
                        if (allow_conditionals && inrange(*buf, '0', '9'))
                        {
                            try
                            {
                                group = new RegexConditional(readNumericConstant<Uint>(buf) - 1);
                            }
                            catch (ParsingError)
                            {
                                throw RegexParsingError(buf, "Group number is too big");
                            }
                            if (*buf != ')')
                                throw RegexParsingError(buf, "Missing closing parenthesis for condition");
                            buf++;
                        }
                        else
                        if (allow_lookaround_conditionals && *buf == '?')
                        {
                            const char *bufOrig = buf-1;
                            switch (buf[1])
                            {
                            case '=':                                                                                                    buf+=2; lookaroundCondition = new RegexGroup(RegexGroup_Lookahead);          break;
                            case '*': if (!allow_molecular_lookaround) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; lookaroundCondition = new RegexGroup(RegexGroup_LookaheadMolecular); break;
                            case '!':                                                                                                    buf+=2; lookaroundCondition = new RegexGroup(RegexGroup_NegativeLookahead);  break;
                            case '^':                                                                                                    buf+=2; lookaroundCondition = parseLookinto(buf);                            break;
                            default:
                                goto condition_not_found;
                            }
                            group = new RegexLookaroundConditional(lookaroundCondition);
                            lookaroundCondition->originalCode = bufOrig;
                        }
                        else condition_not_found:
                            throw RegexParsingError(buf,
                                allow_conditionals ? allow_lookaround_conditionals ? "Backreference number or lookaround expected in condition"
                                                                                   : "Backreference number expected in condition"
                                                                                   : "Lookaround expected in condition");
                        break;
                    case '#':
                        buf+=2;
                        for (;;)
                        {
                            buf++;
                            if (*buf == ')')
                                break;
                            if (!*buf)
                                throw RegexParsingError(buf, "Missing ) after comment");
                        }
                        buf++;
                        goto not_a_group;
                    default:
                        throw RegexParsingError(buf, "Unrecognized character after (?");
                    }
                    break;
                case '*':
                    symbol = new RegexSymbol(RegexSymbol_Verb);
                    addSymbol(buf-1, symbol);
                    buf++;
                    {{}} if (strncmp(buf, "ACCEPT)", strlength("ACCEPT)"))==0 ) { buf += strlength("ACCEPT)"); symbol->verb = RegexVerb_Accept; }
                    else if (strncmp(buf, "FAIL)"  , strlength("FAIL)"  ))==0 ) { buf += strlength("FAIL)"  ); symbol->verb = RegexVerb_Fail  ; }
                    else if (strncmp(buf, "F)"     , strlength("F)"     ))==0 ) { buf += strlength("F)"     ); symbol->verb = RegexVerb_Fail  ; }
                    else if (strncmp(buf, "COMMIT)", strlength("COMMIT)"))==0 ) { buf += strlength("COMMIT)"); symbol->verb = RegexVerb_Commit; }
                    else if (strncmp(buf, "PRUNE)" , strlength("PRUNE)" ))==0 ) { buf += strlength("PRUNE)" ); symbol->verb = RegexVerb_Prune ; }
                    else if (strncmp(buf, "SKIP)"  , strlength("SKIP)"  ))==0 ) { buf += strlength("SKIP)"  ); symbol->verb = RegexVerb_Skip  ; }
                    else if (strncmp(buf, "THEN)"  , strlength("THEN)"  ))==0 ) { buf += strlength("THEN)"  ); symbol->verb = RegexVerb_Then  ; }
                    else
                        throw RegexParsingError(buf, "(*VERB) not recognized or malformed");
                    symbol = NULL; // don't allow a quantifer on a verb
                    goto not_a_group;
                    break;
                default:
                    group = new RegexGroupCapturing(backrefIndex++);
                    break;
                }
                addSymbol(bufGroup, group);

            add_nested_group:
                curGroupDepth++;
                // Neither the current lookaround nor atomic group matching code can handle quantifiers, so leave room in case we will be wrapping it in a non-capturing group (can't know at this point if it has a quantifier or not)
                if (group->isLookaround() || group->type == RegexGroup_Atomic)
                    curGroupDepth++;
                if (maxGroupDepth < curGroupDepth)
                    maxGroupDepth = curGroupDepth;
                switch (group->type)
                {
                case RegexGroup_Lookinto:
                case RegexGroup_LookintoMolecular:
                case RegexGroup_NegativeLookinto:
                    curLookintoDepth++;
                    if (maxLookintoDepth < curLookintoDepth)
                        maxLookintoDepth = curLookintoDepth;
                    break;
                }

                group->parentAlternative = NULL;
                group->self = NULL;
                
                {
                    ParsingStack *stackDown = stack;
                    stack = new ParsingStack;
                    stack->below = stackDown;
                }

                stack->alternatives.push(new RegexPattern);
                stack->group = group;
                if (group->type == RegexGroup_BranchReset)
                {
                    stack->BranchResetGroup.backrefIndexFirst = backrefIndex;
                    stack->BranchResetGroup.backrefIndexNext  = backrefIndex;
                }
                symbol = NULL;

                if (lookaroundCondition)
                {
                    group = lookaroundCondition;
                    lookaroundCondition = NULL;
                    goto add_nested_group;
                }

            not_a_group:
                break;
            }
        case '\0':
            if (stack->below)
                throw RegexParsingError(buf, "Missing closing parentheses");
            goto close_group;
        case ')':
            if (!stack->below)
                throw RegexParsingError(buf, "Unmatched closing parenthesis");
            buf++;
        close_group:
            {
                RegexGroup *group = stack->group;
                closeAlternative(stack->alternatives.back()->symbols, stack->symbols);
                closeGroup(group->alternatives, stack->alternatives);

                if (group->type == RegexGroup_BranchReset)
                {
                    if (stack->BranchResetGroup.backrefIndexNext < backrefIndex)
                        stack->BranchResetGroup.backrefIndexNext = backrefIndex;
                    backrefIndex = stack->BranchResetGroup.backrefIndexNext;
                }
                else
                if (group->type == RegexGroup_LookaroundConditional)
                    ((RegexLookaroundConditional*)group)->lookaround->parentAlternative = group->alternatives;

                curGroupDepth--;
                // Neither the current lookaround nor atomic group matching code can handle quantifiers, so leave room in case we will be wrapping it in a non-capturing group (can't know at this point if it has a quantifier or not)
                if (group->isLookaround() || group->type == RegexGroup_Atomic)
                {
                    curGroupDepth--;
                    switch (group->type)
                    {
                    case RegexGroup_Lookinto:
                    case RegexGroup_LookintoMolecular:
                    case RegexGroup_NegativeLookinto:
                        curLookintoDepth--;
                        break;
                    }
                }

                ParsingStack *stackDown = stack->below;
                delete stack;
                stack = stackDown;
                if (!stack)
                    goto finished_parsing;

                if (stack->group->type == RegexGroup_LookaroundConditional)
                    symbol = NULL; // don't allow a quantifier on a lookaround conditional's lookaround
                else
                    symbol = group;
                symbolCountSpecified = false;
                symbolLazinessSpecified = false;
                break;
            }
        case '|':
            {
                RegexGroup *group = stack->group;
                if (inrange(group->type, RegexGroup_Conditional, RegexGroup_LookaroundConditional) && stack->alternatives.size() == 2)
                    throw RegexParsingError(buf, "Conditional group contains more than two branches");
                buf++;
                closeAlternative(stack->alternatives.back()->symbols, stack->symbols);
                stack->alternatives.push(new RegexPattern);
                if (group->type == RegexGroup_BranchReset)
                {
                    if (stack->BranchResetGroup.backrefIndexNext < backrefIndex)
                        stack->BranchResetGroup.backrefIndexNext = backrefIndex;
                    backrefIndex = stack->BranchResetGroup.backrefIndexFirst;
                }
                break;
            }
        case '\\':
            {
                const char *buf0 = buf;
                buf++;
                if (inrange(*buf, '1', '9'))
                {
                    RegexBackref *backref = new RegexBackref;
                    addSymbol(buf0, symbol = backref);
                    try
                    {
                        backref->index = readNumericConstant<Uint>(buf) - 1;
                    }
                    catch (ParsingError)
                    {
                        throw RegexParsingError(buf, "Group number is too big");
                    }
                }
                else
                {
                    if (!*buf)
                        throw RegexParsingError(buf, "\\ at end of pattern");
                    char ch;
                    switch (*buf)
                    {
                    case '0': ch = '\0'; goto process_char;
                    case 't': ch = '\t'; goto process_char;
                    case 'n': ch = '\n'; goto process_char;
                    case 'v': ch = '\v'; goto process_char;
                    case 'f': ch = '\f'; goto process_char;
                    case 'r': ch = '\r'; goto process_char;
                    default:
                    process_literal_char:
                        ch = *buf;
                    process_char:
                        symbol = new RegexSymbol(RegexSymbol_Character);
                        symbol->characterAny = false;
                        symbol->character = ch;
                        addSymbol(buf0, symbol);
                        buf++;
                        break;
                    case 'K':
                        if (!allow_reset_start)
                            goto process_literal_char;
                        addSymbol(buf0, new RegexSymbol(RegexSymbol_ResetStart));
                        symbol = NULL; // don't allow this symbol to be quantified
                        buf++;
                        break;
                    case 'B':
                    case 'b':
                        addSymbol(buf0, symbol = new RegexSymbol(symbolWithLowercaseOpposite(RegexSymbol_WordBoundaryNot, RegexSymbol_WordBoundary, *buf, 'B')));
                        if (!allow_quantifiers_on_assertions) symbol = NULL;
                        buf++;
                        break;
                    case 'D':
                    case 'd':
                        addSymbol(buf0, symbol = new RegexSymbol(symbolWithLowercaseOpposite(RegexSymbol_DigitNot, RegexSymbol_Digit, *buf, 'D')));
                        buf++;
                        break;
                    case 'S':
                    case 's':
                        addSymbol(buf0, symbol = new RegexSymbol(symbolWithLowercaseOpposite(RegexSymbol_SpaceNot, RegexSymbol_Space, *buf, 'S')));
                        buf++;
                        break;
                    case 'W':
                    case 'w':
                        addSymbol(buf0, symbol = new RegexSymbol(symbolWithLowercaseOpposite(RegexSymbol_WordCharacterNot, RegexSymbol_WordCharacter, *buf, 'W')));
                        buf++;
                        break;
                    }
                }
                break;
            }
        case '+':
            if (symbol && symbolCountSpecified && !symbolLazinessSpecified && allow_possessive_quantifiers)
            {
                buf++;
                symbol->possessive = true;
                fixLookaheadQuantifier();
                symbolLazinessSpecified = true;
                break;
            }
            if (!symbol || symbolCountSpecified || symbolLazinessSpecified)
                throw RegexParsingError(buf, "Nothing to repeat");
            buf++;
            symbol->maxCount = UINT_MAX;
            fixLookaheadQuantifier();
            symbolCountSpecified = true;
            break;
        case '*':
            if (!symbol || symbolCountSpecified || symbolLazinessSpecified)
                throw RegexParsingError(buf, "Nothing to repeat");
            buf++;
            symbol->minCount = 0;
            symbol->maxCount = UINT_MAX;
            fixLookaheadQuantifier();
            symbolCountSpecified = true;
            break;
        case '?':
            if (!symbol || symbolLazinessSpecified)
                throw RegexParsingError(buf, "Nothing to repeat");
            buf++;
            if (symbolCountSpecified)
            {
                symbol->lazy = true;
                symbolLazinessSpecified = true;
            }
            else
            {
                symbol->minCount = 0;
                fixLookaheadQuantifier();
                symbolCountSpecified = true;
            }
            break;
        case '{':
            if (!symbol || symbolCountSpecified || symbolLazinessSpecified)
                throw RegexParsingError(buf, "Nothing to repeat");
            buf++;
            if (!inrange(*buf, '0', '9'))
                throw RegexParsingError(buf, "Non-numeric character after {");
            try
            {
                symbol->minCount = readNumericConstant<Uint>(buf);
                if (symbol->minCount == UINT_MAX) // this value is reserved to mean "unlimited"
                    throw ParsingError();
            }
            catch (ParsingError)
            {
                throw RegexParsingError(buf, "Number too big in {} quantifier");
            }
            if (*buf == '}')
            {
                buf++;
                symbol->maxCount = symbol->minCount;
            }
            else
            if (*buf == ',')
            {
                buf++;
                if (*buf == '}')
                {
                    symbol->maxCount = UINT_MAX;
                    buf++;
                }
                else
                {
                    try
                    {
                        symbol->maxCount = readNumericConstant<Uint>(buf);
                        if (symbol->maxCount == UINT_MAX) // this value is reserved to mean "unlimited"
                            throw ParsingError();
                    }
                    catch (ParsingError)
                    {
                        throw RegexParsingError(buf, "Number too big in {} quantifier");
                    }
                    if (symbol->maxCount < symbol->minCount)
                        throw RegexParsingError(buf, "Numbers out of order in {} quantifier");
                    if (*buf != '}')
                        throw RegexParsingError(buf, "Missing closing } in quantifier");
                    buf++;
                }
            }
            else
                throw RegexParsingError(buf, "{ at end of pattern");
            fixLookaheadQuantifier();
            symbolCountSpecified = true;
            break;
        }
    }
finished_parsing:

    // Note that this group iterator code is redundant with that in RegexMatcher::virtualizeSymbols(); todo: Factor it out into a separate function
    RegexGroup **groupStackBase = new RegexGroup *[maxGroupDepth];
    RegexGroup **groupStackTop = groupStackBase;
    RegexGroup *rootGroup = &regex;
    RegexPattern **thisAlternative;
    RegexSymbol  **thisSymbol = &(RegexSymbol*&)rootGroup;
    std::stack<anchorStackNode> anchorStack;
    for (;;)
    {
        if (*thisSymbol)
        {
            switch ((*thisSymbol)->type)
            {
            case RegexSymbol_AnchorStart:
                anchorStack.top().currentAlternativeAnchored = true;
                thisSymbol++;
                break;
            case RegexSymbol_Backref:
                if (((RegexBackref*)(*thisSymbol))->index >= backrefIndex)
                    throw RegexParsingError((*thisSymbol)->originalCode, "reference to non-existent capture group");
                // fall through
            default:
                thisSymbol++;
                break;
            case RegexSymbol_Group:
                RegexGroup *group = (RegexGroup*)(*thisSymbol);
                switch (group->type)
                {
                case RegexGroup_Lookinto:
                case RegexGroup_LookintoMolecular:
                case RegexGroup_NegativeLookinto:
                    if (((RegexGroupLookinto*)group)->backrefIndex+1 > backrefIndex+1)
                        throw RegexParsingError((*thisSymbol)->originalCode, "reference to non-existent capture group");
                    break;
                case RegexGroup_Conditional:
                    if (((RegexConditional*)group)->backrefIndex >= backrefIndex)
                        throw RegexParsingError((*thisSymbol)->originalCode, "reference to non-existent capture group");
                    break;
                case RegexGroup_LookaroundConditional:
                    {
                        RegexGroup *lookaround = ((RegexLookaroundConditional*)group)->lookaround;
                        switch (lookaround->type)
                        {
                        case RegexGroup_Lookinto:
                        case RegexGroup_LookintoMolecular:
                        case RegexGroup_NegativeLookinto:
                            if (((RegexGroupLookinto*)lookaround)->backrefIndex+1 > backrefIndex+1)
                                throw RegexParsingError((*thisSymbol)->originalCode, "reference to non-existent capture group");
                            break;
                        }
                        break;
                    }
                }
                // The current lookaround matching code can't handle quantifiers, so wrap it in a non-capturing group with a quantifier if it needs one.
                if (group->isLookaround()            && (group->maxCount != group->minCount || group->maxCount > 1) ||
                    group->type == RegexGroup_Atomic &&                                        group->maxCount > 1)
                {
                    RegexGroup *wrapper = new RegexGroup(RegexGroup_NonCapturing);
                    wrapper->originalCode = group->originalCode;
                    wrapper->minCount     = group->minCount;
                    wrapper->maxCount     = group->maxCount;
                    wrapper->lazy         = group->lazy;
                    wrapper->possessive   = group->possessive;
                    group->minCount   = 1;
                    group->maxCount   = 1;
                    group->lazy       = false;
                    group->possessive = false;
                    wrapper->alternatives    = new RegexPattern* [1 + 1];
                    wrapper->alternatives[0] = new RegexPattern;
                    wrapper->alternatives[0]->symbols = (RegexSymbol**)malloc((1 + 1) * sizeof(RegexSymbol*));
                    wrapper->alternatives[0]->symbols[0] = group;
                    wrapper->alternatives[0]->symbols[1] = NULL;
                    wrapper->alternatives[1] = NULL;
                    wrapper->parentAlternative = thisAlternative;
                    wrapper->self              = thisSymbol;
                    group  ->parentAlternative = &wrapper->alternatives[0];
                    group  ->self              = &wrapper->alternatives[0]->symbols[0];
                    *groupStackTop++ = wrapper;
                    *thisSymbol      = wrapper;
                    thisAlternative = &wrapper->alternatives[0];
                    thisSymbol      = &wrapper->alternatives[0]->symbols[0];
                    anchorStack.push(0);
                }
                *groupStackTop++ = group;
                thisAlternative = group->alternatives;
                thisSymbol      = group->alternatives[0]->symbols;
                anchorStack.push(0);
                if (group->type == RegexGroup_LookaroundConditional)
                {
                    group = ((RegexLookaroundConditional*)group)->lookaround;
                    *groupStackTop++ = group;
                    thisAlternative = group->alternatives;
                    thisSymbol      = group->alternatives[0]->symbols;
                    anchorStack.push(0);
                }
                break;
            }
        }
        else
        {
            anchorStack.top().allAlternativesAreAnchored &= anchorStack.top().currentAlternativeAnchored;
            anchorStack.top().currentAlternativeAnchored = false;
            thisAlternative++;
            if (*thisAlternative)
                thisSymbol = (*thisAlternative)->symbols;
            else
            {
                RegexGroup *group = *--groupStackTop;
                if (groupStackTop == groupStackBase)
                    break;

                bool anchored = anchorStack.top().allAlternativesAreAnchored;
                // conditionals with only 1 alternative have an implied empty second alternative (which is not anchored)
                if ((group->type==RegexGroup_Conditional || group->type==RegexGroup_LookaroundConditional) && (thisAlternative - group->alternatives)==1)
                    anchored = false;

                thisAlternative = group->parentAlternative;
                thisSymbol      = group->self ? group->self + 1 : (*thisAlternative)->symbols; // group->self will be NULL if this is the lookaround in a conditional

                anchorStack.pop();
                if (group->self && group->minCount && !group->isNegativeLookaround() )
                    anchorStack.top().currentAlternativeAnchored |= anchored;
            }
        }
    }
    delete [] groupStackBase;

#ifdef _DEBUG
    if (anchorStack.size() != 1)
        throw "Anchor stack error";
#endif
    regex.anchored = anchorStack.top().allAlternativesAreAnchored;
}
