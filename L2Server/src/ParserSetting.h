#pragma once

#include "LexerSetting.h"
#include "PVPMatch.h"
#include "Setting.h"

#include "Protocol/src/L2Constants.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CBlockUpset;
class CBlockUpsetManager;
class CCleft;
class CSkillInfo;

// The grammar is ParserSetting.y. Bison generates yyparse as a member, so the
// grammar actions use these fields. The offset comments give the layout in the
// original image.
class CParserForSetting final {
public:
    struct CleftData {
        std::int32_t m_WinnerSpBonus = 0;
        std::int32_t m_LoserSpBonus = 0;
        std::int32_t m_KillSpBonus = 0;
        std::int32_t m_CatPeriod = 0;
    };

    struct BlockUpsetData {
        std::int32_t m_Stage = 0;
        std::uint32_t m_alignment004 = 0;
        CSkillInfo* m_pBlueStartSkill = nullptr;
        CSkillInfo* m_pRedStartSkill = nullptr;
        CSkillInfo* m_pWaitSkill = nullptr;

        void Init() {
            m_Stage = 0;
            m_pBlueStartSkill = nullptr;
            m_pRedStartSkill = nullptr;
            m_pWaitSkill = nullptr;
        }
    };

    struct RewardData {
        std::int32_t nLev = 0;
        std::int32_t nItemId = 0;
        std::int32_t nItemCount = 0;
        void Init() {
            nLev = 0;
            nItemId = 0;
            nItemCount = 0;
        }
    };

    CParserForSetting();
    ~CParserForSetting();

    int Create(CSetting* setting, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForSetting m_lexer; // +0x0f0
    ClassType tempClass; // +0x1c8
    GameRuleType tempRule; // +0x1cc
    std::vector<std::u16string> stringList; // +0x1d0
    RaceType tempRace; // +0x1f0
    [[maybe_unused]] std::uint32_t m_alignment1f4;
    CSetting::ClassifiedPosition tempPosition; // +0x1f8
    std::int64_t tempClassBit = 0; // +0x220
    FVector tempPoint; // +0x228
    std::vector<FVector> tempPointList; // +0x240
    std::vector<FVector> tempChaoPointList; // +0x260
    std::vector<std::int32_t> tempItemList; // +0x280
    std::vector<std::pair<std::int32_t, std::int64_t>> tempInitItemList; // +0x2a0
    std::vector<std::int64_t> tempSkillList; // +0x2c0
    CSetting::StatValueType* tempStatArray; // +0x2e0
    CSetting::AbnormalResistType* tempResistType; // +0x2e8
    CSetting::RestartPointGroup* pTempRestartPointGroup; // +0x2f0
    CSetting::RestartTerritory* pTempRestartTerritory; // +0x2f8
    CTerritory* m_pTerritory; // +0x300
    CPVPMatchSystem::CPVPPointTimeBlock* pTimeBlock; // +0x308
    std::int32_t tempId; // +0x310
    [[maybe_unused]] std::uint32_t m_alignment314;
    FVector tempPoint1[3]; // +0x318
    FVector tempPoint2[3]; // +0x360
    FVector tempArenaPoint[3]; // +0x3a8
    std::int32_t m_pointIndex; // +0x3f0

private:
    [[maybe_unused]] std::uint32_t m_alignment3f4;
    CSetting* m_pSetting; // +0x3f8
    CCleft* m_pCleftMatch; // +0x400
    CBlockUpsetManager* m_pBlockUpsetManager; // +0x408
    CBlockUpset* m_pBlockUpset; // +0x410
    CleftData m_CleftData; // +0x418
    BlockUpsetData m_BlockUpsetData; // +0x428
    RewardData m_RewardData; // +0x448
    [[maybe_unused]] std::uint32_t m_alignment454;
    std::vector<RewardData> tempRewardList; // +0x458
};
