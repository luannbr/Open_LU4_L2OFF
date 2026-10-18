#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/proto/Stream.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace olympiad {

// The current L2Server.exe RTTI and virtual stream bodies fix
// these protocol/olympiadpacket.h carriers. TotalChecker2015 contributes the
// original member spellings only. It is not current layout authority.

struct OSQ_PREPARE final : network::IPacket {
    ~OSQ_PREPARE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCA_PREPARE final : network::IPacket {
    ~OCA_PREPARE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nGameValidCondition);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nGameValidCondition);
    }

    int nUserDBID; // +0x08
    int nGameValidCondition; // +0x0c
};


struct OSQ_RETURN final : network::IPacket {
    ~OSQ_RETURN() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&nUserSID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&nUserSID); }

    int nUserDBID; // +0x08
    int nUserSID; // +0x0c
};


struct OCA_RETURN final : network::IPacket {
    ~OCA_RETURN() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserSID) && stream->Encode(&bSuccess); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserSID) && stream->Decode(&bSuccess); }

    int nUserSID; // +0x08
    bool bSuccess; // +0x0c
};


struct OSQ_RESTART final : network::IPacket {
    ~OSQ_RESTART() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&nUserSID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&nUserSID); }

    int nUserDBID; // +0x08
    int nUserSID; // +0x0c
};


struct OCA_RESTART final : network::IPacket {
    ~OCA_RESTART() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserSID) && stream->Encode(&bSuccess); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserSID) && stream->Decode(&bSuccess); }

    int nUserSID; // +0x08
    bool bSuccess; // +0x0c
};


struct OCQ_VERSION final : network::IPacket {
    ~OCQ_VERSION() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nWorldId); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nWorldId); }

    int nWorldId; // +0x08
};


struct OSA_VERSION final : network::IPacket {
    ~OSA_VERSION() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nWorldId) && stream->Encode(&bSuccess); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nWorldId) && stream->Decode(&bSuccess); }

    int nWorldId; // +0x08
    bool bSuccess; // +0x0c
};


struct OSA_REGISTER final : network::IPacket {
    ~OSA_REGISTER() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&bSuccess); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&bSuccess); }

    int nUserDBID; // +0x08
    bool bSuccess; // +0x0c
};


struct OCQ_STOP final : network::IPacket {
    ~OCQ_STOP() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&bByDisconnect);
    }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&bByDisconnect); }

    int nUserDBID; // +0x08
    bool bByDisconnect; // +0x0c
};


struct OSA_STOP final : network::IPacket {
    ~OSA_STOP() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&cResult); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&cResult); }

    int nUserDBID; // +0x08
    char cResult; // +0x0c
};


struct OSA_OBSERVE final : network::IPacket {
    ~OSA_OBSERVE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&cResult); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&cResult); }

    int nUserDBID; // +0x08
    char cResult; // +0x0c
};


struct OSN_CONNECT final : network::IPacket {
    ~OSN_CONNECT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OSN_CONFIRMED final : network::IPacket {
    ~OSN_CONFIRMED() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OSN_DISCONNECTED final : network::IPacket {
    ~OSN_DISCONNECTED() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCQ_MATCH_LIST final : network::IPacket {
    ~OCQ_MATCH_LIST() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCQ_HERO_LIST final : network::IPacket {
    ~OCQ_HERO_LIST() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OSN_PLAYER_COUNT final : network::IPacket {
    ~OSN_PLAYER_COUNT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nCount); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nCount); }

    int nCount; // +0x08
};


struct OSN_START_SEASON final : network::IPacket {
    ~OSN_START_SEASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nSeason); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nSeason); }

    int nSeason; // +0x08
};


struct OSN_STOP_SEASON final : network::IPacket {
    ~OSN_STOP_SEASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nSeason); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nSeason); }

    int nSeason; // +0x08
};


struct OCN_NOBLESS_INFO final : network::IPacket {
    ~OCN_NOBLESS_INFO() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCQ_DEBUG_STRING final : network::IPacket {
    ~OCQ_DEBUG_STRING() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCN_LAST_MATCH_RESULT final : network::IPacket {
    ~OCN_LAST_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCN_UPDATE_PLEDGE_INFO final : network::IPacket {
    ~OCN_UPDATE_PLEDGE_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nPledgeDBID) && stream->Encode(&nCrestDBID);
    }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nPledgeDBID) && stream->Decode(&nCrestDBID); }

    int nPledgeDBID; // +0x08
    int nCrestDBID; // +0x0c
};


struct OCQ_RESET_OLYMPIAD_POINT final : network::IPacket {
    ~OCQ_RESET_OLYMPIAD_POINT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&nPoint); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&nPoint); }

    int nUserDBID; // +0x08
    int nPoint; // +0x0c
};


struct OCQ_OLYMPIAD_RECORD final : network::IPacket {
    ~OCQ_OLYMPIAD_RECORD() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID); }

    int nUserDBID; // +0x08
};


struct OCQ_HERO_MATCH_RESULT final : network::IPacket {
    ~OCQ_HERO_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nClassType) && stream->Encode(&nPage);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nClassType) && stream->Decode(&nPage);
    }

    int nUserDBID; // +0x08
    int nClassType; // +0x0c
    int nPage; // +0x10
};


struct OCQ_MATCH_COUNT final : network::IPacket {
    ~OCQ_MATCH_COUNT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nTargetWorldID) && stream->Encode(&nTargetUserDBID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nTargetWorldID) && stream->Decode(&nTargetUserDBID);
    }

    int nUserDBID; // +0x08
    int nTargetWorldID; // +0x0c
    int nTargetUserDBID; // +0x10
};


struct OCQ_OBSERVE final : network::IPacket {
    ~OCQ_OBSERVE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nFieldID) && stream->Encode(&nAccountID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nFieldID) && stream->Decode(&nAccountID);
    }

    int nUserDBID; // +0x08
    int nFieldID; // +0x0c
    int nAccountID; // +0x10
};


struct OSA_MATCH_COUNT final : network::IPacket {
    ~OSA_MATCH_COUNT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nGameRule) && stream->Encode(&nMatchCount);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nGameRule) && stream->Decode(&nMatchCount);
    }

    int nUserDBID; // +0x08
    int nGameRule; // +0x0c
    int nMatchCount; // +0x10
};


struct OSN_ENTRY_COUNT final : network::IPacket {
    ~OSN_ENTRY_COUNT() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nGameRule) && stream->Encode(&nCount); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nGameRule) && stream->Decode(&nCount); }

    int nGameRule; // +0x08
    int nCount; // +0x0c
};


struct OSN_CANCEL_MATCH final : network::IPacket {
    ~OSN_CANCEL_MATCH() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nGameValidCondition) && stream->Encode(&nItemID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nGameValidCondition) && stream->Decode(&nItemID);
    }

    int nUserDBID; // +0x08
    int nGameValidCondition; // +0x0c
    int nItemID; // +0x10
};


struct OSN_LAST_MATCH_RESULT final : network::IPacket {
    ~OSN_LAST_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nMatchResult) && stream->Encode(&nItemCount);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nMatchResult) && stream->Decode(&nItemCount);
    }

    int nUserDBID; // +0x08
    int nMatchResult; // +0x0c
    int nItemCount; // +0x10
};


struct OCQ_CLASS_RANK_BY_SERVER final : network::IPacket {
    ~OCQ_CLASS_RANK_BY_SERVER() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nReserved); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nReserved); }

    char nReserved; // +0x08
};


struct OSN_OLYMPIAD_MATCHING_INFO final : network::IPacket {
    ~OSN_OLYMPIAD_MATCHING_INFO() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&bMatchOpen); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&bMatchOpen); }

    bool bMatchOpen; // +0x08
};


struct OCQ_PLEDGE_RAID_GET_SEASON final : network::IPacket {
    ~OCQ_PLEDGE_RAID_GET_SEASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nSeason_); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nSeason_); }

    int nSeason_; // +0x08
};


struct OSA_PLEDGE_RAID_GET_SEASON final : network::IPacket {
    ~OSA_PLEDGE_RAID_GET_SEASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nSeason_) && stream->Encode(&tStartTime_); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nSeason_) && stream->Decode(&tStartTime_); }

    int nSeason_; // +0x08
    int tStartTime_; // +0x0c
};


struct OCN_PLEDGE_RAID_RESET_SEASON final : network::IPacket {
    ~OCN_PLEDGE_RAID_RESET_SEASON() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&tResetTime_); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&tResetTime_); }

    int tResetTime_; // +0x08
};


struct OCN_PLEDGE_RAID_DECIDE final : network::IPacket {
    ~OCN_PLEDGE_RAID_DECIDE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&tDelay_); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&tDelay_); }

    int tDelay_; // +0x08
};


struct OSA_PLEDGE_RAID_SET_INFO final : network::IPacket {
    ~OSA_PLEDGE_RAID_SET_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason_) && stream->Encode(&nPledgeDbId_) && stream->Encode(&nResult_);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason_) && stream->Decode(&nPledgeDbId_) && stream->Decode(&nResult_);
    }

    int nSeason_; // +0x08
    int nPledgeDbId_; // +0x0c
    int nResult_; // +0x10
};


struct OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST final : network::IPacket {
    ~OCQ_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nSeason_); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nSeason_); }

    int nSeason_; // +0x08
};


struct OSA_BCMD final : network::IPacket {
    ~OSA_BCMD() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&sMessage); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&sMessage); }

    int nUserDBID; // +0x08
    std::u16string sMessage; // +0x10
};


struct OSA_DEBUG_STRING final : network::IPacket {
    ~OSA_DEBUG_STRING() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&sString); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&sString); }

    int nUserDBID; // +0x08
    std::u16string sString; // +0x10
};


struct OSN_SYSTEM_MESSAGE final : network::IPacket {
    ~OSN_SYSTEM_MESSAGE() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&nUserDBID) && stream->Encode(&sMessage); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&nUserDBID) && stream->Decode(&sMessage); }

    int nUserDBID; // +0x08
    std::u16string sMessage; // +0x10
};


struct OSA_CONNECT final : network::IPacket {
    ~OSA_CONNECT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&bSuccess) && stream->Encode(&bAsPlayer);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&bSuccess) && stream->Decode(&bAsPlayer);
    }

    int nUserDBID; // +0x08
    bool bSuccess; // +0x0c
    bool bAsPlayer; // +0x0d
};


struct OSA_START final : network::IPacket {
    ~OSA_START() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&cGameRule) && stream->Encode(&nUserDBID) && stream->Encode(&cResult) &&
            stream->Encode(&nMessageNo) && stream->Encode(&nEntryCount);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&cGameRule) && stream->Decode(&nUserDBID) && stream->Decode(&cResult) &&
            stream->Decode(&nMessageNo) && stream->Decode(&nEntryCount);
    }

    char cGameRule; // +0x08
    int nUserDBID; // +0x0c
    char cResult; // +0x10
    int nMessageNo; // +0x14
    int nEntryCount; // +0x18
};


struct OSN_NOTIFY_SEASON final : network::IPacket {
    ~OSN_NOTIFY_SEASON() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason) && stream->Encode(&nStep) && stream->Encode(&nStartTime) &&
            stream->Encode(&nNominateSecond) && stream->Encode(&nLegendaryWorldID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason) && stream->Decode(&nStep) && stream->Decode(&nStartTime) &&
            stream->Decode(&nNominateSecond) && stream->Decode(&nLegendaryWorldID);
    }

    int nSeason; // +0x08
    int nStep; // +0x0c
    int nStartTime; // +0x10
    int nNominateSecond; // +0x14
    int nLegendaryWorldID; // +0x18
};


struct OSN_SYSTEM_MESSAGE_NO final : network::IPacket {
    ~OSN_SYSTEM_MESSAGE_NO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&bBroadcast) && stream->Encode(&nUserDBID) && stream->Encode(&nMessageNo);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&bBroadcast) && stream->Decode(&nUserDBID) && stream->Decode(&nMessageNo);
    }

    bool bBroadcast; // +0x08
    int nUserDBID; // +0x0c
    int nMessageNo; // +0x10
};


struct OSN_OLYMPIAD_POINT final : network::IPacket {
    ~OSN_OLYMPIAD_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&bBroadcast) && stream->Encode(&nUserDBID) && stream->Encode(&nPoint);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&bBroadcast) && stream->Decode(&nUserDBID) && stream->Decode(&nPoint);
    }

    bool bBroadcast; // +0x08
    int nUserDBID; // +0x0c
    int nPoint; // +0x10
};


struct OSN_STOP final : network::IPacket {
    ~OSN_STOP() override = default;

    bool Encode(network::Stream* stream) const override { return stream->Encode(&bBroadcast) && stream->Encode(&nUserDBID); }

    bool Decode(network::Stream* stream) override { return stream->Decode(&bBroadcast) && stream->Decode(&nUserDBID); }

    bool bBroadcast; // +0x08
    int nUserDBID; // +0x0c
};


struct OSA_DELETE_TRADE_POINT final : network::IPacket {
    ~OSA_DELETE_TRADE_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nTradePoint) && stream->Encode(&nNPCSID) &&
            stream->Encode(&bSuccess);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nTradePoint) && stream->Decode(&nNPCSID) &&
            stream->Decode(&bSuccess);
    }

    int nUserDBID; // +0x08
    int nTradePoint; // +0x0c
    int nNPCSID; // +0x10
    bool bSuccess; // +0x14
};


struct OSA_SET_HERO_TYPE final : network::IPacket {
    ~OSA_SET_HERO_TYPE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nHeroType) && stream->Encode(&nNPCSID) &&
            stream->Encode(&nWinCount) && stream->Encode(&cResult);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nHeroType) && stream->Decode(&nNPCSID) &&
            stream->Decode(&nWinCount) && stream->Decode(&cResult);
    }

    int nUserDBID; // +0x08
    int nHeroType; // +0x0c
    int nNPCSID; // +0x10
    int nWinCount; // +0x14
    char cResult; // +0x18
};


struct OSN_NOBLESS_INFO final : network::IPacket {
    ~OSN_NOBLESS_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nHeroType) && stream->Encode(&nWinCount) &&
            stream->Encode(&nPreviousPoint) && stream->Encode(&nOlympiadPoint) && stream->Encode(&nMatchCount) &&
            stream->Encode(&nOlympiadWinCount) && stream->Encode(&nOlympiadLoseCount) &&
            stream->Encode(&nOlympiadTradePoint) && stream->Encode(&bNewSeason);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nHeroType) && stream->Decode(&nWinCount) &&
            stream->Decode(&nPreviousPoint) && stream->Decode(&nOlympiadPoint) && stream->Decode(&nMatchCount) &&
            stream->Decode(&nOlympiadWinCount) && stream->Decode(&nOlympiadLoseCount) &&
            stream->Decode(&nOlympiadTradePoint) && stream->Decode(&bNewSeason);
    }

    int nUserDBID; // +0x08
    int nHeroType; // +0x0c
    int nWinCount; // +0x10
    int nPreviousPoint; // +0x14
    int nOlympiadPoint; // +0x18
    int nMatchCount; // +0x1c
    int nOlympiadWinCount; // +0x20
    int nOlympiadLoseCount; // +0x24
    int nOlympiadTradePoint; // +0x28
    bool bNewSeason; // +0x2c
};


struct OSA_OLYMPIAD_RECORD final : network::IPacket {
    ~OSA_OLYMPIAD_RECORD() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nPoint) && stream->Encode(&nWinCount) &&
            stream->Encode(&nLoseCount) && stream->Encode(&nMatchCount) && stream->Encode(&nPrevClassType) &&
            stream->Encode(&nPrevRank) && stream->Encode(&nPrevRankCount) && stream->Encode(&nPrevClassRank) &&
            stream->Encode(&nPrevClassRankCount) && stream->Encode(&nPrevClassRankByServer) &&
            stream->Encode(&nPrevClassRankByServerCount) && stream->Encode(&nPrevPoint) && stream->Encode(&nPrevWinCount) &&
            stream->Encode(&nPrevLoseCount) && stream->Encode(&nPrevGrade);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nPoint) && stream->Decode(&nWinCount) &&
            stream->Decode(&nLoseCount) && stream->Decode(&nMatchCount) && stream->Decode(&nPrevClassType) &&
            stream->Decode(&nPrevRank) && stream->Decode(&nPrevRankCount) && stream->Decode(&nPrevClassRank) &&
            stream->Decode(&nPrevClassRankCount) && stream->Decode(&nPrevClassRankByServer) &&
            stream->Decode(&nPrevClassRankByServerCount) && stream->Decode(&nPrevPoint) && stream->Decode(&nPrevWinCount) &&
            stream->Decode(&nPrevLoseCount) && stream->Decode(&nPrevGrade);
    }

    int nUserDBID; // +0x08
    int nPoint; // +0x0c
    int nWinCount; // +0x10
    int nLoseCount; // +0x14
    int nMatchCount; // +0x18
    int nPrevClassType; // +0x1c
    int nPrevRank; // +0x20
    int nPrevRankCount; // +0x24
    int nPrevClassRank; // +0x28
    int nPrevClassRankCount; // +0x2c
    int nPrevClassRankByServer; // +0x30
    int nPrevClassRankByServerCount; // +0x34
    int nPrevPoint; // +0x38
    int nPrevWinCount; // +0x3c
    int nPrevLoseCount; // +0x40
    int nPrevGrade; // +0x44
};


struct OCQ_PLEDGE_RAID_SET_INFO final : network::IPacket {
    ~OCQ_PLEDGE_RAID_SET_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason_) && stream->Encode(&nPledgeDbId_) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(szPledgeName_), sizeof(szPledgeName_)) &&
            stream->Encode(&tSaveTime_) && stream->Encode(&nPoint_);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason_) && stream->Decode(&nPledgeDbId_) &&
            stream->Decode(reinterpret_cast<unsigned char*>(szPledgeName_), sizeof(szPledgeName_)) &&
            stream->Decode(&tSaveTime_) && stream->Decode(&nPoint_);
    }

    int nSeason_; // +0x08
    int nPledgeDbId_; // +0x0c
    char16_t szPledgeName_[25]; // +0x10
    int tSaveTime_; // +0x44
    std::int64_t nPoint_; // +0x48
};


struct OCQ_PLEDGE_RAID_GET_INFO final : network::IPacket {
    ~OCQ_PLEDGE_RAID_GET_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason_) && stream->Encode(&nPledgeDbId_) && stream->Encode(&bIsCurrent_) &&
            stream->Encode(&nUserDbId_) && stream->Encode(&tStartTime_) && stream->Encode(&tEndTime_);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason_) && stream->Decode(&nPledgeDbId_) && stream->Decode(&bIsCurrent_) &&
            stream->Decode(&nUserDbId_) && stream->Decode(&tStartTime_) && stream->Decode(&tEndTime_);
    }

    int nSeason_; // +0x08
    int nPledgeDbId_; // +0x0c
    bool bIsCurrent_; // +0x10
    int nUserDbId_; // +0x14
    int tStartTime_; // +0x18
    int tEndTime_; // +0x1c
};


struct OCQ_PLEDGE_RAID_GET_INFO_LIST final : network::IPacket {
    ~OCQ_PLEDGE_RAID_GET_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason_) && stream->Encode(&bIsCurrent_) && stream->Encode(&nUserDbId_);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason_) && stream->Decode(&bIsCurrent_) && stream->Decode(&nUserDbId_);
    }

    int nSeason_; // +0x08
    bool bIsCurrent_; // +0x0c
    int nUserDbId_; // +0x10
};


struct OSA_PLEDGE_RAID_GET_INFO final : network::IPacket {
    ~OSA_PLEDGE_RAID_GET_INFO() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nSeason_) && stream->Encode(&nPledgeDbId_) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(szPledgeName_), sizeof(szPledgeName_)) &&
            stream->Encode(&nRank_) && stream->Encode(&nPoint_) && stream->Encode(&nRewardReason_) &&
            stream->Encode(&nTotalCount_) && stream->Encode(&bIsCurrent_) && stream->Encode(&nUserDbId_) &&
            stream->Encode(&tStartTime_) && stream->Encode(&tEndTime_);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nSeason_) && stream->Decode(&nPledgeDbId_) &&
            stream->Decode(reinterpret_cast<unsigned char*>(szPledgeName_), sizeof(szPledgeName_)) &&
            stream->Decode(&nRank_) && stream->Decode(&nPoint_) && stream->Decode(&nRewardReason_) &&
            stream->Decode(&nTotalCount_) && stream->Decode(&bIsCurrent_) && stream->Decode(&nUserDbId_) &&
            stream->Decode(&tStartTime_) && stream->Decode(&tEndTime_);
    }

    int nSeason_; // +0x08
    int nPledgeDbId_; // +0x0c
    char16_t szPledgeName_[25]; // +0x10
    int nRank_; // +0x44
    std::int64_t nPoint_; // +0x48
    int nRewardReason_; // +0x50
    int nTotalCount_; // +0x54
    bool bIsCurrent_; // +0x58
    int nUserDbId_; // +0x5c
    int tStartTime_; // +0x60
    int tEndTime_; // +0x64
};


struct PkOlympiadAuthInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nTimeLeft) && stream->Encode(&nUserStat) && stream->Encode(&nWarnFlag) &&
            stream->Encode(&nLoginFlag) && stream->Encode(&nAge) && stream->Encode(&cChargeMethod) &&
            stream->Encode(&nReservations) && stream->Encode(&nRestTime_china) && stream->Encode(&nPlayTime_china) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(sPCCafeCode), sizeof(sPCCafeCode)) &&
            stream->Encode(&nClientAddress) && stream->Encode(&cCountryCode) && stream->Encode(&nAccountID) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(sAccountName), sizeof(sAccountName)) &&
            stream->Encode(&nPremiumLevel) &&
            stream->Encode(reinterpret_cast<const unsigned char*>(sCharName), sizeof(sCharName)) && stream->Encode(&nX) &&
            stream->Encode(&nY) && stream->Encode(&nZ) && stream->Encode(&nDormantType) &&
            stream->Encode(&tDormantMarkingTime);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nTimeLeft) && stream->Decode(&nUserStat) && stream->Decode(&nWarnFlag) &&
            stream->Decode(&nLoginFlag) && stream->Decode(&nAge) && stream->Decode(&cChargeMethod) &&
            stream->Decode(&nReservations) && stream->Decode(&nRestTime_china) && stream->Decode(&nPlayTime_china) &&
            stream->Decode(reinterpret_cast<unsigned char*>(sPCCafeCode), sizeof(sPCCafeCode)) &&
            stream->Decode(&nClientAddress) && stream->Decode(&cCountryCode) && stream->Decode(&nAccountID) &&
            stream->Decode(reinterpret_cast<unsigned char*>(sAccountName), sizeof(sAccountName)) &&
            stream->Decode(&nPremiumLevel) &&
            stream->Decode(reinterpret_cast<unsigned char*>(sCharName), sizeof(sCharName)) && stream->Decode(&nX) &&
            stream->Decode(&nY) && stream->Decode(&nZ) && stream->Decode(&nDormantType) &&
            stream->Decode(&tDormantMarkingTime);
    }

    int nTimeLeft; // +0x00
    int nUserStat; // +0x04
    int nWarnFlag; // +0x08
    int nLoginFlag; // +0x0c
    int nAge; // +0x10
    char cChargeMethod; // +0x14
    int nReservations; // +0x18
    int nRestTime_china; // +0x1c
    int nPlayTime_china; // +0x20
    char sPCCafeCode[9]; // +0x24
    int nClientAddress; // +0x30
    char cCountryCode; // +0x34
    int nAccountID; // +0x38
    char16_t sAccountName[15]; // +0x3c
    int nPremiumLevel; // +0x5c
    char16_t sCharName[25]; // +0x60
    int nX; // +0x94
    int nY; // +0x98
    int nZ; // +0x9c
    int nDormantType; // +0xa0
    int tDormantMarkingTime; // +0xa4
};


struct OCQ_CONNECT final : network::IPacket {
    ~OCQ_CONNECT() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream->Encode(&nServerID) || !stream->Encode(&nUserDBID)) {
            return false;
        }

        unsigned char* const frameBegin = stream->GetCursor();
        std::int16_t frameSize = 0;
        if (!stream->Encode(&frameSize) || !authInfo.Encode(stream)) {
            return false;
        }
        frameSize = static_cast<std::int16_t>(stream->GetCursor() - frameBegin);
        std::memcpy(frameBegin, &frameSize, sizeof(frameSize));
        return true;
    }

    bool Decode(network::Stream* stream) override {
        if (!stream->Decode(&nServerID) || !stream->Decode(&nUserDBID)) {
            return false;
        }

        unsigned char* const frameBegin = stream->GetCursor();
        std::int16_t frameSize = 0;
        if (!stream->Decode(&frameSize) || !authInfo.Decode(stream)) {
            return false;
        }

        const std::ptrdiff_t consumed = stream->GetCursor() - frameBegin;
        if (consumed > frameSize) {
            return false;
        }

        const std::uintptr_t bufferBegin = reinterpret_cast<std::uintptr_t>(stream->GetBuffer());
        const std::uintptr_t frameAddress = reinterpret_cast<std::uintptr_t>(frameBegin);
        const std::uintptr_t frameEnd = frameAddress + static_cast<std::uintptr_t>(frameSize);
        const std::uintptr_t bufferEnd = bufferBegin + static_cast<std::uintptr_t>(stream->GetSize());
        if (frameEnd < bufferBegin || frameEnd > bufferEnd) {
            return false;
        }
        stream->SetCursor(reinterpret_cast<unsigned char*>(frameEnd));
        return true;
    }

    int nServerID; // +0x08
    int nUserDBID; // +0x0c
    PkOlympiadAuthInfo authInfo; // +0x10
};


struct PkApplication {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nRace) && stream->Encode(&nSex) && stream->Encode(&cGameRule) &&
            stream->Encode(&nClassType) && stream->Encode(&nLevel) && stream->Encode(&sCharName) &&
            stream->Encode(&nAccountID) && stream->Encode(&sAccountName);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nRace) && stream->Decode(&nSex) && stream->Decode(&cGameRule) &&
            stream->Decode(&nClassType) && stream->Decode(&nLevel) && stream->Decode(&sCharName) &&
            stream->Decode(&nAccountID) && stream->Decode(&sAccountName);
    }

    int nRace; // +0x00
    int nSex; // +0x04
    char cGameRule; // +0x08
    int nClassType; // +0x0c
    int nLevel; // +0x10
    std::u16string sCharName; // +0x18
    int nAccountID; // +0x40
    std::u16string sAccountName; // +0x48
};


struct OCQ_START final : network::IPacket {
    ~OCQ_START() override = default;

    bool Encode(network::Stream* stream) const override {
        if (!stream->Encode(&nUserDBID)) {
            return false;
        }

        unsigned char* const frameBegin = stream->GetCursor();
        std::int16_t frameSize = 0;
        if (!stream->Encode(&frameSize) || !Application.Encode(stream)) {
            return false;
        }
        frameSize = static_cast<std::int16_t>(stream->GetCursor() - frameBegin);
        std::memcpy(frameBegin, &frameSize, sizeof(frameSize));
        return true;
    }

    bool Decode(network::Stream* stream) override {
        if (!stream->Decode(&nUserDBID)) {
            return false;
        }

        unsigned char* const frameBegin = stream->GetCursor();
        std::int16_t frameSize = 0;
        if (!stream->Decode(&frameSize) || !Application.Decode(stream)) {
            return false;
        }

        const std::ptrdiff_t consumed = stream->GetCursor() - frameBegin;
        if (consumed > frameSize) {
            return false;
        }

        const std::uintptr_t bufferBegin = reinterpret_cast<std::uintptr_t>(stream->GetBuffer());
        const std::uintptr_t frameAddress = reinterpret_cast<std::uintptr_t>(frameBegin);
        const std::uintptr_t frameEnd = frameAddress + static_cast<std::uintptr_t>(frameSize);
        const std::uintptr_t bufferEnd = bufferBegin + static_cast<std::uintptr_t>(stream->GetSize());
        if (frameEnd < bufferBegin || frameEnd > bufferEnd) {
            return false;
        }
        stream->SetCursor(reinterpret_cast<unsigned char*>(frameEnd));
        return true;
    }

    int nUserDBID; // +0x08
    PkApplication Application; // +0x10
};


struct OCQ_REGISTER final : network::IPacket {
    ~OCQ_REGISTER() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nClass) && stream->Encode(&sCharName) &&
            stream->Encode(&nPledgeDBID) && stream->Encode(&sPledgeName) && stream->Encode(&nPledgeCrestID) &&
            stream->Encode(&nAllianceDBID) && stream->Encode(&sAllianceName) && stream->Encode(&nAllianceCrestID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nClass) && stream->Decode(&sCharName) &&
            stream->Decode(&nPledgeDBID) && stream->Decode(&sPledgeName) && stream->Decode(&nPledgeCrestID) &&
            stream->Decode(&nAllianceDBID) && stream->Decode(&sAllianceName) && stream->Decode(&nAllianceCrestID);
    }

    int nUserDBID; // +0x08
    int nClass; // +0x0c
    std::u16string sCharName; // +0x10
    int nPledgeDBID; // +0x38
    std::u16string sPledgeName; // +0x40
    int nPledgeCrestID; // +0x68
    int nAllianceDBID; // +0x6c
    std::u16string sAllianceName; // +0x70
    int nAllianceCrestID; // +0x98
};


struct OCQ_SET_HERO_TYPE final : network::IPacket {
    ~OCQ_SET_HERO_TYPE() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&nHeroType) && stream->Encode(&nNPCSID) &&
            stream->Encode(&sCharName) && stream->Encode(&nPledgeDBID) && stream->Encode(&sPledgeName) &&
            stream->Encode(&nPledgeCrestID) && stream->Encode(&nAllianceDBID) && stream->Encode(&sAllianceName) &&
            stream->Encode(&nAllianceCrestID) && stream->Encode(&bByBuilder);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&nHeroType) && stream->Decode(&nNPCSID) &&
            stream->Decode(&sCharName) && stream->Decode(&nPledgeDBID) && stream->Decode(&sPledgeName) &&
            stream->Decode(&nPledgeCrestID) && stream->Decode(&nAllianceDBID) && stream->Decode(&sAllianceName) &&
            stream->Decode(&nAllianceCrestID) && stream->Decode(&bByBuilder);
    }

    int nUserDBID; // +0x08
    int nHeroType; // +0x0c
    int nNPCSID; // +0x10
    std::u16string sCharName; // +0x18
    int nPledgeDBID; // +0x40
    std::u16string sPledgeName; // +0x48
    int nPledgeCrestID; // +0x70
    int nAllianceDBID; // +0x74
    std::u16string sAllianceName; // +0x78
    int nAllianceCrestID; // +0xa0
    bool bByBuilder; // +0xa4
};


struct OCQ_DELETE_TRADE_POINT final : network::IPacket {
    ~OCQ_DELETE_TRADE_POINT() override = default;

    bool Encode(network::Stream* stream) const override {
        return stream->Encode(&nUserDBID) && stream->Encode(&sCharName) && stream->Encode(&nAccountID) &&
            stream->Encode(&sAccountName) && stream->Encode(&nTradePoint) && stream->Encode(&nNPCSID);
    }

    bool Decode(network::Stream* stream) override {
        return stream->Decode(&nUserDBID) && stream->Decode(&sCharName) && stream->Decode(&nAccountID) &&
            stream->Decode(&sAccountName) && stream->Decode(&nTradePoint) && stream->Decode(&nNPCSID);
    }

    int nUserDBID; // +0x08
    std::u16string sCharName; // +0x10
    int nAccountID; // +0x38
    std::u16string sAccountName; // +0x40
    int nTradePoint; // +0x68
    int nNPCSID; // +0x6c
};


struct OCQ_BCMD final : network::IPacket {
    ~OCQ_BCMD() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vParams.size());
        if (!stream->Encode(&nUserDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (std::u16string value : vParams) {
            if (!stream->Encode(&value)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nUserDBID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            std::u16string value;
            if (!stream->Decode(&value)) {
                return false;
            }
            vParams.push_back(value);
        }
        return true;
    }

    int nUserDBID; // +0x08
    std::list<std::u16string, std::allocator<std::u16string>> vParams; // target +0x10
};


struct PkMatchInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nFieldID) && stream->Encode(&nGameRule) && stream->Encode(&nStatus) &&
            stream->Encode(&sTeam1Name) && stream->Encode(&sTeam2Name);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nFieldID) && stream->Decode(&nGameRule) && stream->Decode(&nStatus) &&
            stream->Decode(&sTeam1Name) && stream->Decode(&sTeam2Name);
    }

    int nFieldID; // +0x00
    int nGameRule; // +0x04
    int nStatus; // +0x08
    std::u16string sTeam1Name; // +0x10
    std::u16string sTeam2Name; // +0x38
};


struct OSA_MATCH_LIST final : network::IPacket {
    ~OSA_MATCH_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vList.size());
        if (!stream->Encode(&nUserDBID) || !stream->Encode(&bRemain) || !stream->Encode(&count)) {
            return false;
        }
        for (PkMatchInfo value : vList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nUserDBID) || !stream->Decode(&bRemain) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkMatchInfo value;
            if (!value.Decode(stream)) {
                return false;
            }
            vList.push_back(value);
        }
        return true;
    }

    int nUserDBID; // +0x08
    bool bRemain; // +0x0c
    std::list<PkMatchInfo, std::allocator<PkMatchInfo>> vList; // target +0x10
};


struct PkHeroInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nWorldID) && stream->Encode(&sName) && stream->Encode(&nClassType) &&
            stream->Encode(&sPledgeName) && stream->Encode(&nPledgeCrestId) && stream->Encode(&sAllianceName) &&
            stream->Encode(&nAllianceCrestId) && stream->Encode(&nWinCount) && stream->Encode(&nHeroType);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nWorldID) && stream->Decode(&sName) && stream->Decode(&nClassType) &&
            stream->Decode(&sPledgeName) && stream->Decode(&nPledgeCrestId) && stream->Decode(&sAllianceName) &&
            stream->Decode(&nAllianceCrestId) && stream->Decode(&nWinCount) && stream->Decode(&nHeroType);
    }

    int nWorldID; // +0x00
    std::u16string sName; // +0x08
    int nClassType; // +0x30
    std::u16string sPledgeName; // +0x38
    int nPledgeCrestId; // +0x60
    std::u16string sAllianceName; // +0x68
    int nAllianceCrestId; // +0x90
    int nWinCount; // +0x94
    int nHeroType; // +0x98
};


struct OSA_HERO_LIST final : network::IPacket {
    ~OSA_HERO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vList.size());
        if (!stream->Encode(&nUserDBID) || !stream->Encode(&count)) {
            return false;
        }
        for (PkHeroInfo value : vList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nUserDBID) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkHeroInfo value;
            if (!value.Decode(stream)) {
                return false;
            }
            vList.push_back(value);
        }
        return true;
    }

    int nUserDBID; // +0x08
    std::list<PkHeroInfo, std::allocator<PkHeroInfo>> vList; // target +0x10
};


struct PkHeroMatchResult {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nMatchTime) && stream->Encode(&nIsWinner) && stream->Encode(&nGameRule) &&
            stream->Encode(&sRivalName) && stream->Encode(&nRivalClassID) && stream->Encode(&nElapsedTime) &&
            stream->Encode(&nTempleteNumber) && stream->Encode(&nWinCount) && stream->Encode(&nDrawCount) &&
            stream->Encode(&nLoseCount);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nMatchTime) && stream->Decode(&nIsWinner) && stream->Decode(&nGameRule) &&
            stream->Decode(&sRivalName) && stream->Decode(&nRivalClassID) && stream->Decode(&nElapsedTime) &&
            stream->Decode(&nTempleteNumber) && stream->Decode(&nWinCount) && stream->Decode(&nDrawCount) &&
            stream->Decode(&nLoseCount);
    }

    int nMatchTime; // +0x00
    int nIsWinner; // +0x04
    int nGameRule; // +0x08
    std::u16string sRivalName; // +0x10
    int nRivalClassID; // +0x38
    int nElapsedTime; // +0x3c
    int nTempleteNumber; // +0x40
    int nWinCount; // +0x44
    int nDrawCount; // +0x48
    int nLoseCount; // +0x4c
};


struct OSA_HERO_MATCH_RESULT final : network::IPacket {
    ~OSA_HERO_MATCH_RESULT() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vList.size());
        if (!stream->Encode(&nUserDBID) || !stream->Encode(&nPage) || !stream->Encode(&nClassType) ||
            !stream->Encode(&nWinCount) || !stream->Encode(&nDrawCount) || !stream->Encode(&nLoseCount) ||
            !stream->Encode(&nTotalMatchCount) || !stream->Encode(&bHasNextPage) || !stream->Encode(&count)) {
            return false;
        }
        for (PkHeroMatchResult value : vList) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nUserDBID) || !stream->Decode(&nPage) || !stream->Decode(&nClassType) ||
            !stream->Decode(&nWinCount) || !stream->Decode(&nDrawCount) || !stream->Decode(&nLoseCount) ||
            !stream->Decode(&nTotalMatchCount) || !stream->Decode(&bHasNextPage) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkHeroMatchResult value;
            if (!value.Decode(stream)) {
                return false;
            }
            vList.push_back(value);
        }
        return true;
    }

    int nUserDBID; // +0x08
    int nPage; // +0x0c
    int nClassType; // +0x10
    int nWinCount; // +0x14
    int nDrawCount; // +0x18
    int nLoseCount; // +0x1c
    int nTotalMatchCount; // +0x20
    bool bHasNextPage; // +0x24
    std::list<PkHeroMatchResult, std::allocator<PkHeroMatchResult>> vList; // target +0x28
};


struct PkClassRankByServerInfo {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(&nRanking) && stream->Encode(&sName) && stream->Encode(&nPoint);
    }

    bool Decode(network::Stream* stream) {
        return stream->Decode(&nRanking) && stream->Decode(&sName) && stream->Decode(&nPoint);
    }

    int nRanking; // +0x00
    std::u16string sName; // +0x08
    int nPoint; // +0x30
};


struct OSA_CLASS_RANK_BY_SERVER final : network::IPacket {
    ~OSA_CLASS_RANK_BY_SERVER() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vRanks.size());
        if (!stream->Encode(&nHeroClass) || !stream->Encode(&count)) {
            return false;
        }
        for (PkClassRankByServerInfo value : vRanks) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nHeroClass) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkClassRankByServerInfo value;
            if (!value.Decode(stream)) {
                return false;
            }
            vRanks.push_back(value);
        }
        return true;
    }

    int nHeroClass; // +0x08
    std::list<PkClassRankByServerInfo,
        std::allocator<PkClassRankByServerInfo>> vRanks; // target +0x10
};


struct PkOlympiadPledgeRaidInfoData {
    bool Encode(network::Stream* stream) const {
        return stream->Encode(reinterpret_cast<const unsigned char*>(this), sizeof(*this));
    }

    bool Decode(network::Stream* stream) { return stream->Decode(reinterpret_cast<unsigned char*>(this), sizeof(*this)); }

    int nWorldId_; // +0x00
    int nPledgeDbId_; // +0x04
    char16_t szPledgeName_[25]; // +0x08
    int nRank_; // +0x3c
    std::int64_t nPoint_; // +0x40
};


struct OSA_PLEDGE_RAID_GET_INFO_LIST final : network::IPacket {
    ~OSA_PLEDGE_RAID_GET_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nTotalCount_) || !stream->Encode(&count)) {
            return false;
        }
        for (PkOlympiadPledgeRaidInfoData value : vList_) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return stream->Encode(&bIsCurrent_) && stream->Encode(&nUserDbId_);
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nTotalCount_) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkOlympiadPledgeRaidInfoData value;
            if (!value.Decode(stream)) {
                return false;
            }
            vList_.push_back(value);
        }
        return stream->Decode(&bIsCurrent_) && stream->Decode(&nUserDbId_);
    }

    int nTotalCount_; // +0x08
    std::list<PkOlympiadPledgeRaidInfoData> vList_; // target +0x10
    bool bIsCurrent_; // target +0x28
    int nUserDbId_; // target +0x2c
};


struct OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST final : network::IPacket {
    ~OSA_PLEDGE_RAID_GET_PREV_SEASON_INFO_LIST() override = default;

    bool Encode(network::Stream* stream) const override {
        const int count = static_cast<int>(vList_.size());
        if (!stream->Encode(&nSeason_) || !stream->Encode(&nTotalCount_) || !stream->Encode(&count)) {
            return false;
        }
        for (PkOlympiadPledgeRaidInfoData value : vList_) {
            if (!value.Encode(stream)) {
                return false;
            }
        }
        return true;
    }

    bool Decode(network::Stream* stream) override {
        int count = 0;
        if (!stream->Decode(&nSeason_) || !stream->Decode(&nTotalCount_) || !stream->Decode(&count)) {
            return false;
        }
        for (int index = 0; index < count; ++index) {
            PkOlympiadPledgeRaidInfoData value;
            if (!value.Decode(stream)) {
                return false;
            }
            vList_.push_back(value);
        }
        return true;
    }

    int nSeason_; // +0x08
    int nTotalCount_; // +0x0c
    std::list<PkOlympiadPledgeRaidInfoData> vList_; // target +0x10
};


} // namespace olympiad
