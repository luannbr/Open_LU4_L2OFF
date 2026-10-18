#pragma once

#include "Auction.h"
#include "LexerAuctionData.h"

#include <cstddef>
#include <cstdint>

class CParserForAuction {
public:
    CParserForAuction();
    ~CParserForAuction();

    int create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

private:
    CAuctionNPC* m_pAuctionNPC; // +0xf0
    std::int32_t m_nItemType; // +0xf8
    [[maybe_unused]] std::byte m_paddingFcTo100[4];
    std::int64_t m_nItemAmount; // +0x100
    std::int64_t m_nItemPrice; // +0x108
    std::int32_t m_nItemCount; // +0x110
    [[maybe_unused]] std::byte m_padding114To118[4];

public:
    CLexerForAuction m_lexer; // +0x118
};
