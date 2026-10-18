#pragma once

#include "Shared/src/LexerSpawn.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TerritoryInfo.h"

#include <array>
#include <cstddef>
#include <cstdint>

class AIClassInfo;
class CTerritory;

// Shared/parser_spawn.h, L2Server specialization. The grammar is
// ParserSpawn.y. Its productions match the L2NPC grammar, but the actions are
// the checker used by LocalizationSystem and TotalChecker in the L2Server
// product. Bison generates yyparse as a member, so the actions use these
// fields. The offset comments give the layout in the original image.
class CParserForNPCSpawn {
public:
    using AIParameterMap = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;
    using TerritoryDBForChecker = std::map<std::u16string, CTerritory*, runtime::CStringLess<std::u16string>>;

    struct CNpcMakerInfoForChecker {
        void Clear();
        bool Verify();

        TerritoryInfo m_TerritoryInfo; // +0x00
        bool m_bAnyWhere; // +0x98
        bool m_bFlying; // +0x99

    private:
        [[maybe_unused]] std::array<std::byte, 6> m_targetPadding; // +0x9a
    };

    CParserForNPCSpawn();
    ~CParserForNPCSpawn();

    int Create(char16_t* text, int codeUnitCount);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    bool CheckAddTerritoryToTerritoryInfo(const char16_t* name);
    bool CheckAddBannedTerritoryToTerritoryInfo(const char16_t* name);
    bool CheckSpawnPoint(const FVector* position);

private:
    friend class CNpcPosChecker;

    // The current constructor leaves these two generated
    // grammar flags untouched until their first semantic action.
    bool m_bSpawnTime; // +0x0f0
    bool m_bMaxNpc; // +0x0f1
    [[maybe_unused]] std::array<std::byte, 6> m_flagPadding; // +0x0f2
    std::u16string mMakerName; // +0x0f8
    AIClassInfo* mCurrentClassInfo; // +0x120
    FVector* pSpawnPos; // +0x128
    std::u16string m_TerritoryName; // +0x130
    bool mCheckAI; // +0x158
    [[maybe_unused]] std::array<std::byte, 7> m_checkAiPadding; // +0x159
    CLexerForNPCSpawn m_lexer; // +0x160
    CTerritory* pTempTerritory; // +0x230
    char16_t* m_dbname; // +0x238
    std::int32_t m_dbsaving; // +0x240
    [[maybe_unused]] std::array<std::byte, 4> m_dbPadding; // +0x244
    AIParameterMap m_AIParameters; // +0x248
    TerritoryDBForChecker m_TerritoryDBForChecker; // +0x268
    CNpcMakerInfoForChecker m_CurrentNpcMaker; // +0x288
    std::set<std::u16string> eventNameSet; // +0x328
    bool m_bOnlyMakerEventList; // +0x348
    [[maybe_unused]] std::array<std::byte, 7> m_onlyMakerEventPadding; // +0x349
    std::set<std::u16string> mMakerNameSet; // +0x350
};
