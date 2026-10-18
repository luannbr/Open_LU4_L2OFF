#pragma once

#include "ItemAnnounceLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>

struct ItemAnnounceItemInfo;
struct ItemAnnounceRandomBoxInfo;

class CParserForItemAnnounceInfo {
public:
    CParserForItemAnnounceInfo();
    ~CParserForItemAnnounceInfo();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    std::vector<char16_t*>* m_pStringVector; // +0x0f0
    std::u16string* m_pString; // +0x0f8
    ItemAnnounceItemInfo* m_pItemAnnounceItemInfo; // +0x100
    ItemAnnounceRandomBoxInfo* m_pItemAnnounceRandomBoxInfo; // +0x108

public:
    CLexerForItemAnnounceInfo m_lexer; // +0x110
};
