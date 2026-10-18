#pragma once

#include "BaseSpawnDefine.h"
#include "LexerSpawn.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TerritoryInfo.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CTerritory;
class NpcSpawnDefineEx;

namespace l2npc {

class CNpcMakerDB;
class CNpcMaker;
class CNpcSpawnDefine;
class NpcMakerEx;

// L2NPC/parser_spawn.h. The grammar is ParserSpawn.y. Bison generates yyparse
// as a member, so the grammar actions use these fields. The offset comments
// give the layout in the original image.
class CParserForNPCSpawn {
public:
    using TerritoryDBForChecker = std::map<std::u16string, CTerritory*, runtime::CStringLess<std::u16string>>;

    struct CNpcMakerInfoForChecker {
        CNpcMakerInfoForChecker();

        void Clear();
        bool Verify();

        TerritoryInfo m_TerritoryInfo;
        bool m_bAnyWhere;
        bool m_bFlying;

    private:
        [[maybe_unused]] std::array<std::byte, 6> m_targetPadding;
    };

    CParserForNPCSpawn();
    virtual ~CParserForNPCSpawn();

    int Create(CNpcMakerDB* npcMakerDatabase, std::uint16_t* text, int codeUnitCount);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void CheckAI(bool check);


    CNpcMakerDB* m_pNpcMakerDB; // +0x0f0
    CNpcMaker* pTempNpcMaker; // +0x0f8
    CNpcSpawnDefine* pTempNpcSpawnDefine; // +0x100
    NpcMakerEx* pNpcMakerEx; // +0x108
    NpcSpawnDefineEx* pNpcSpawnDefineEx; // +0x110
    std::vector<FVector> m_WayPoints; // +0x118
    std::vector<int> m_WayPointDelays; // +0x138
    FVector m_Point; // +0x158
    std::vector<std::u16string> stringlist; // +0x170
    std::u16string m_TerritoryName; // +0x190
    bool mCheckAI; // +0x1b8

private:
    [[maybe_unused]] std::array<std::byte, 7> m_checkAiPadding; // +0x1b9

public:
    CLexerForNPCSpawn m_lexer; // +0x1c0
    CTerritory* pTempTerritory; // +0x290
    char16_t* m_dbname; // +0x298
    int m_dbsaving; // +0x2a0

private:
    [[maybe_unused]] std::array<std::byte, 4> m_dbSavingPadding; // +0x2a4

public:
    BaseSpawnDefine::AiParameterMap m_AIParameters; // +0x2a8
    TerritoryDBForChecker m_TerritoryDBForChecker; // +0x2c8
    CNpcMakerInfoForChecker m_CurrentNpcMaker; // +0x2e8
    std::set<std::u16string> eventNameSet; // +0x388
    bool m_bOnlyMakerEventList; // +0x3a8

private:
    [[maybe_unused]] std::array<std::byte, 7> m_onlyMakerEventPadding; // +0x3a9
    std::set<std::u16string> mMakerNameSet; // +0x3b0
};


} // namespace l2npc
