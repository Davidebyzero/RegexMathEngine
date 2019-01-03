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
    // it makes no difference whether a lookahead is repeated once or an infinite number of times, so limit them to 1 iteration
    if (symbol->type == RegexSymbol_Group && ((RegexGroup*)symbol)->isLookahead())
    {
        symbol->minCount = symbol->minCount ? 1 : 0;
        symbol->maxCount = symbol->maxCount ? 1 : 0;
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

RegexParser::RegexParser(RegexGroup &regex, const char *buf)
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
        default:
            symbol = new RegexSymbol(RegexSymbol_Character);
            symbol->characterAny = false;
            symbol->character = *buf;
            addSymbol(buf++, symbol);
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
                                Uint8 backup2 = allowedChars[(Uchar)' '/8] & (1 << ((Uchar)' '%8));
                                Uint8 backup3 = allowedChars[(Uchar)' '/8] & (1 << ((Uchar)' '%8)); // non-breaking space; WARNING: may not be portable
                                memset(allowedChars, 0xFF, sizeof(allowedChars));
                                allowedChars[1] |= backup1;
                                allowedChars[(Uchar)' '/8] |= backup2;
                                allowedChars[(Uchar)' '/8] |= backup3;
                                inRange = -1;
                                buf++;
                                break;
                            }
                        case 's':
                            if (inRange == 2)
                                throw RegexParsingError(buf, "Invalid range in character class");
                            allowedChars[1] |= (1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5); // '\t','\n','\v','\f','\r'
                            allowedChars[(Uchar)' '/8] |= 1 << ((Uchar)' '%8);
                            allowedChars[(Uchar)' '/8] |= 1 << ((Uchar)' '%8); // non-breaking space; WARNING: may not be portable
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
                    if (*buf == '-')
                    {
                        if (buf[+1] != ']')
                        {
                            if (inRange == 1)
                            {
                                inRange = 2;
                                firstCharInRange = ch;
                                buf++;
                            }
                            else
                            if (inRange < 0)
                                throw RegexParsingError(buf, "Invalid range in character class");
                        }
                        else
                            goto process_char_for_charClass;
                    }
                    else
                    {
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
                switch (*buf)
                {
                case '?':
                    switch (buf[1])
                    {
                    case ':':                                                                                                   buf+=2; group = new RegexGroup(RegexGroup_NonCapturing);       break;
                    case '>': if (!allow_atomic_groups      ) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; group = new RegexGroup(RegexGroup_Atomic);             break;
                    case '=':                                                                                                   buf+=2; group = new RegexGroup(RegexGroup_Lookahead);          break;
                    case '*': if (!allow_molecular_lookahead) throw RegexParsingError(buf, "Unrecognized character after (?");  buf+=2; group = new RegexGroup(RegexGroup_LookaheadMolecular); break;
                    case '!':                                                                                                   buf+=2; group = new RegexGroup(RegexGroup_NegativeLookahead);  break;
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
                default:
                    group = new RegexGroupCapturing(backrefIndex++);
                    break;
                }
                addSymbol(bufGroup, group);

                curGroupDepth++;
                if (maxGroupDepth < curGroupDepth)
                    maxGroupDepth = curGroupDepth;

                group->parentAlternative = NULL;
                group->self = NULL;
                
                {
                    ParsingStack *stackDown = stack;
                    stack = new ParsingStack;
                    stack->below = stackDown;
                }

                stack->alternatives.push(new RegexPattern);
                stack->group = group;
                symbol = NULL;
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

                curGroupDepth--;

                ParsingStack *stackDown = stack->below;
                delete stack;
                stack = stackDown;
                if (!stack)
                    goto finished_parsing;

                symbol = group;
                symbolCountSpecified = false;
                symbolLazinessSpecified = false;
                break;
            }
        case '|':
            buf++;
            closeAlternative(stack->alternatives.back()->symbols, stack->symbols);
            stack->alternatives.push(new RegexPattern);
            break;
        case '\\':
            {
                const char *buf0 = buf;
                buf++;
                if (inrange(*buf, '1', '9'))
                {
                    RegexBackref *backref = new RegexBackref;
                    addSymbol(buf0, symbol = backref);
                    backref->index = readNumericConstant<Uint>(buf) - 1;
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
                symbolCountSpecified = true;
            }
            break;
        case '{':
            if (!symbol || symbolCountSpecified || symbolLazinessSpecified)
                throw RegexParsingError(buf, "Nothing to repeat");
            buf++;
            if (!inrange(*buf, '0', '9'))
                throw RegexParsingError(buf, "Non-numeric character after {");
            symbol->minCount = readNumericConstant<Uint>(buf);
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
                    symbol->maxCount = readNumericConstant<Uint>(buf);
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
    for (;;)
    {
        if (*thisSymbol)
        {
            switch ((*thisSymbol)->type)
            {
            case RegexSymbol_Backref:
                if (((RegexBackref*)(*thisSymbol))->index >= backrefIndex)
                    throw RegexParsingError((*thisSymbol)->originalCode, "reference to non-existent capture group");
                // fall through
            default:
                thisSymbol++;
                break;
            case RegexSymbol_Group:
                RegexGroup *group = (RegexGroup*)(*thisSymbol);
                *groupStackTop++ = group;
                thisAlternative = group->alternatives;
                thisSymbol      = group->alternatives[0]->symbols;
                break;
            }
        }
        else
        {
            thisAlternative++;
            if (*thisAlternative)
                thisSymbol = (*thisAlternative)->symbols;
            else
            {
                RegexGroup *group = *--groupStackTop;
                if (groupStackTop == groupStackBase)
                    break;
                thisAlternative = group->parentAlternative;
                thisSymbol      = group->self + 1;
            }
        }
    }
    delete [] groupStackBase;
}
