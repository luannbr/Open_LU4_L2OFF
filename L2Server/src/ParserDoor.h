#pragma once

#include "LexerDoor.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CChair;
class CControlTower;
class CDoor;
class CDoorDB;
class CSignBoard;
class CTerritory;
class CWorldTrapInfo;

class CParserForDoor {
public:
    CParserForDoor();
    ~CParserForDoor();

    int Create(CDoorDB* doorDb, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    friend class CDoorDB;

public:
    CLexerForDoor m_lexer; // +0x0f0

private:
    std::int32_t m_nEmitterID; // +0x1d0
    std::int32_t m_nCollision; // +0x1d4
    std::set<std::int32_t> m_EditIdSet; // +0x1d8
    bool m_bIsInstantZone; // +0x1f8
    bool m_bFirstStep; // +0x1f9
    [[maybe_unused]] std::byte m_alignment1faTo200[6];
    CDoorDB* m_pDoorDB; // +0x200
    CDoor* m_pDoor; // +0x208
    CSignBoard* m_pSignBoard; // +0x210
    CChair* m_pChair; // +0x218
    CControlTower* m_pControlTower; // +0x220
    FVector m_TempVector; // +0x228
    std::u16string m_Name; // +0x240
    std::vector<std::u16string> m_TempStrings; // +0x268
    CWorldTrapInfo* m_pTrapInfo; // +0x288
    CTerritory* m_pTempTerritory; // +0x290
};
