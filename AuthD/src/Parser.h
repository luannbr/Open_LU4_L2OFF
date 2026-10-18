#pragma once

#include <istream>

// Original owner: Src/parser.h. L2Authd.pdb type 0x72F0.
enum Token : int {
    eWord = 0,
    ePeriod = 1,
    eComma = 2,
    eMinus = 3,
    eColon = 4,
    eSemiColon = 5,
    eEqual = 6,
    eSlash = 7,
    eAmpersand = 8,
    eLeftParen = 9,
    eRightParen = 10,
    eNewLine = 11,
    eNumber = 12,
    eSingleQuot = 13,
    eEOF = 14,
    eError = 15,
};

bool IsDigit(char c) noexcept;
bool IsWhiteSpace(char c) noexcept;
bool IsAlpha(char c) noexcept;

Token GetToken(
    std::istream& stream,
    char* value,
    bool strOnly);

