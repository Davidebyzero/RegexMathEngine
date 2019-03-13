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

class ParsingStack
{
    friend class RegexParser;
    ParsingStack *below;
    std::queue<RegexPattern*> alternatives;
    std::queue<RegexSymbol*> symbols;
    RegexGroup *group;
    union
    {
        struct
        {
            Uint backrefIndexFirst;
            Uint backrefIndexNext; // the next backrefIndex that a capturing group will get outside of this group
        } BranchResetGroup;
    };
};

class RegexParser
{
    friend class Regex;

    ParsingStack *stack;
    RegexSymbol *symbol;
    Uint backrefIndex; // zero-numbered; 0 corresponds to \1
    Uint maxGroupDepth; // minimum is 1 (meaning, root group only)
    Uint curGroupDepth;
    bool symbolCountSpecified;
    bool symbolLazinessSpecified;

    inline RegexSymbolType symbolWithLowercaseOpposite(RegexSymbolType neg, RegexSymbolType pos, char ch, char chNeg);

    void addSymbol(const char *buf, RegexSymbol *symbol);
    void fixLookaheadQuantifier();
    static void skipWhitespace(const char *&buf);
    void closeAlternative(RegexSymbol **&symbols, std::queue<RegexSymbol*> &symbolQueue);
    void closeGroup(RegexPattern **&alternatives, std::queue<RegexPattern*> &patternQueue);
public:
    RegexParser(RegexGroupRoot &regex, const char *buf);
};
