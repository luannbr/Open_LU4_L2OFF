#pragma once

#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(L2_BUILD_L2SERVER)
class CDBSocket;
using DxPacketSocket = CDBSocket;
#else
class CSocket;
using DxPacketSocket = CSocket;
#endif

using DxPacketCallback = bool (*)(DxPacketSocket*, const std::uint8_t*);
using DxPacketCallbackMap = FixedSizeCallbackMap<116, DxPacketCallback>;

class AtomicJob;
class CFortress;
class CInstantAgit;
class CInstantAgitData;
class CInstantAgitManager;
class PledgeRaidSystem;
class CPledge;
class PledgeMastery;
class WOlympiadService;
class User;

// Current Shared/DXPacket.cpp global. Both the current Cached.exe and the
// symbol-complete 2015 build identify the exact name, capacity and default
// callback.
extern DxPacketCallbackMap g_DxPacketTable;

bool DummyDXPacket(DxPacketSocket* socket, const std::uint8_t* packet);

template <class Packet, int Protocol>
class TPacketProcess {
public:
    static bool DoJob(DxPacketSocket* socket, const std::uint8_t* packet);
};


// Shared/DXPacket.h packets are copied byte-for-byte between L2Server and
// CacheD. The 2015 symbol-complete PDB supplies the source member names and
// packing. Current Cached.exe confirms all three sizes and
// field offsets through its packet callbacks and DoCacheDJob bodies.
class CStackOnly {
private:
    static void* operator new(std::size_t size);
    static void operator delete(void* allocation);
};

// Exact empty Shared/DXPacket.h helper in TotalChecker2015.pdb. It occupies
// one byte when embedded in the three legacy Olympiad packets below.
class OlympiadHelper {};

#pragma pack(push, 1)

// Exact 4-byte base recorded by TotalChecker2015.pdb. The three later
// World Olympiad request carriers below inherit it. Their current CacheD
// handlers do not check the job key.
class CDX_ATOMICJOB_BASE : public CStackOnly {
public:
    explicit CDX_ATOMICJOB_BASE(AtomicJob* job = nullptr);
    void DoServerJob();

protected:
    int m_nJobKey; // +0x00
};

// Newbie and residence-deco jobs. TotalChecker2015.pdb fixes every source
// identifier and packed offset below. Current Cached.exe confirms the same
// byte extents and field use in DX slots 0 through 4.
class CNewbieCheck : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int nServerId; // +0x00
    int nDbId; // +0x04
    int nAccountId; // +0x08
    int nLevel; // +0x0c
    int nIsNewbie; // +0x10
};

class CSetResidenceDeco : public CStackOnly {
public:
    enum {
        MAX_SET_DECO_NAME = 64,
    };

    CSetResidenceDeco(
        int npcIndex,
        int userIndex,
        int agitId,
        int decoType,
        int decoId,
        const char16_t* decoName,
        int level,
        int expire,
        int returnValue
    ) :
        nNpcIndex(npcIndex),
        nUserIndex(userIndex),
        nAgitId(agitId),
        nDecoType(decoType),
        nDecoId(decoId),
        nLevel(level),
        nExpire(expire),
        nReturn(returnValue) {
        std::size_t length = 0;
        while (decoName[length] != u'\0') {
            ++length;
        }
        if (length < MAX_SET_DECO_NAME) {
            std::memcpy(sDecoName, decoName, (length + 1U) * sizeof(char16_t));
        } else {
            std::memcpy(sDecoName, decoName, (MAX_SET_DECO_NAME - 1U) * sizeof(char16_t));
            sDecoName[MAX_SET_DECO_NAME - 1U] = u'\0';
        }
    }

    bool DoCacheDJob();
    void DoServerJob();

private:
    int nNpcIndex; // +0x00
    int nUserIndex; // +0x04
    int nAgitId; // +0x08
    int nDecoType; // +0x0c
    int nDecoId; // +0x10
    int nLevel; // +0x14
    int nExpire; // +0x18
    int nReturn; // +0x1c
    char16_t sDecoName[64]; // +0x20
};

class CResetResidenceDeco : public CStackOnly {
public:
    CResetResidenceDeco(int npcIndex, int userIndex, int agitId, int decoType, int returnValue) :
        nNpcIndex(npcIndex), nUserIndex(userIndex), nAgitId(agitId), nDecoType(decoType), nReturn(returnValue) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int nNpcIndex; // +0x00
    int nUserIndex; // +0x04
    int nAgitId; // +0x08
    int nDecoType; // +0x0c
    int nReturn; // +0x10
};

class CResetAllResidenceDeco : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class CCastle;
    friend class CFortress;
    friend class CWorldAgit;

    int nNpcIndex; // +0x00
    int nUserIndex; // +0x04
    int nAgitId; // +0x08
};

class CRenewResidenceDeco : public CStackOnly {
public:
    CRenewResidenceDeco(int pledgeId, int agitId, int decoCount, int* decoTypes, int* expires, std::int64_t* prices);

    bool DoCacheDJob();
    void DoServerJob();

private:
    // The PDB records this as CRenewResidenceDeco's nested data type.
    struct CRenewResidenceDecoData {
        short nType; // +0x00
        short nReturn; // +0x02
        int nExpire; // +0x04
        std::int64_t nPrice; // +0x08
    };

    int nAgitId; // +0x000
    int nDecoCount; // +0x004
    int nPledgeId; // +0x008
    CRenewResidenceDecoData aDecoData[32]; // +0x00c
};

// Legacy siege and first-generation Olympiad jobs. TotalChecker2015.pdb
// supplies the exact source member names and packing. The current Cached.exe
// confirms the same byte extents and every field used below.
class DxSaveSiegeKillDeath : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class CPledge;

    int mPledgeDbId; // +0x00
    int mKillNo; // +0x04
    int mDeathNo; // +0x08
    int mReturn; // +0x0c
};

class DxSaveNoblessType : public CStackOnly {
public:
    DxSaveNoblessType(int userDbId, int noblessType, int defaultOlympiadPoint, int npcSid, int userSid) :
        mUserDbId(userDbId),
        mNoblessType(noblessType),
        mDefaultOlympiadPoint(defaultOlympiadPoint),
        mNpcSid(npcSid),
        mUserSid(userSid),
        mReturn(0) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mNoblessType; // +0x04
    int mDefaultOlympiadPoint; // +0x08
    int mNpcSid; // +0x0c
    int mUserSid; // +0x10
    int mReturn; // +0x14
};

class DxSaveHeroType : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mHeroType; // +0x04
    int mNpcSid; // +0x08
    int mUserSid; // +0x0c
    int mReturn; // +0x10
};

class DxWriteNoblessAchievement : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class User;

    int mUserDbId; // +0x00
    int mType; // +0x04
    int mTarget; // +0x08
    int mTime; // +0x0c
    int mReturn; // +0x10
    bool mNeedReload; // +0x14
};

class DxReloadOlympiadPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserServerId; // +0x00
    int mUserDbId; // +0x04
    int mOlympiadPoint; // +0x08
};

class DxDeclareWar : public CStackOnly {
public:
    DxDeclareWar(
        int pledgeSid, int pledgeDbId, int enemyPledgeSid, int enemyPledgeDbId, int declareUserSid, int declareTime
    ) :
        mPledgeSid(pledgeSid),
        mPledgeDbId(pledgeDbId),
        mEnemyPledgeSid(enemyPledgeSid),
        mEnemyPledgeDbId(enemyPledgeDbId),
        mDeclareTime(declareTime),
        mResult(0),
        mDeclareUserSid(declareUserSid) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    bool DeclareWar();

    int mPledgeSid; // +0x00
    int mPledgeDbId; // +0x04
    int mEnemyPledgeSid; // +0x08
    int mEnemyPledgeDbId; // +0x0c
    int mDeclareTime; // +0x10
    int mResult; // +0x14
    int mDeclareUserSid; // +0x18
};

class DxCancelWar : public CStackOnly {
public:
    DxCancelWar(
        int pledgeSid,
        int pledgeDbId,
        int enemyPledgeSid,
        int enemyPledgeDbId,
        int cancelUserSid,
        int cancelTime,
        bool updateNameValue,
        int stopType
    ) :
        mPledgeSid(pledgeSid),
        mPledgeDbId(pledgeDbId),
        mEnemyPledgeSid(enemyPledgeSid),
        mEnemyPledgeDbId(enemyPledgeDbId),
        mResult(0),
        mCancelUserSid(cancelUserSid),
        mCancelTime(cancelTime),
        mStopType(stopType),
        mUpdateNameValue(updateNameValue) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    bool CancelWar();
    bool PostCancelWar();

    int mPledgeSid; // +0x00
    int mPledgeDbId; // +0x04
    int mEnemyPledgeSid; // +0x08
    int mEnemyPledgeDbId; // +0x0c
    int mResult; // +0x10
    int mCancelUserSid; // +0x14
    int mCancelTime; // +0x18
    int mStopType; // +0x1c
    bool mUpdateNameValue; // +0x20
};

class DxLoadOlympiad : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    std::int32_t mSeasonStart; // +0x08, target long
    int mStartSec; // +0x0c
    int mBonus1Sec; // +0x10
    int mBonus2Sec; // +0x14
    int mBonus3Sec; // +0x18
    int mBonus4Sec; // +0x1c
    int mNominateSec; // +0x20
};

class DxLoadPledgeGame : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    std::int32_t mSeasonStartTime; // +0x08, target long
    int mMaxPrizeCount; // +0x0c
    int mPrevSeasonWinnerPledgeDbId; // +0x10
    char16_t mPrevSeasonWinnerPledgeName[25]; // +0x14
    int mPrevSeasonWinnerCharacterDbId; // +0x46
    char16_t mPrevSeasonWinnerCharacterName[25]; // +0x4a
    int mPrevSeasonWinnerGotItem; // +0x7c
};

class DxSetClass : public CStackOnly {
public:
    DxSetClass(int userDbId, int subJobId, int classType) : mUserDbId(userDbId), mSubJobId(subJobId), mClass(classType) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mSubJobId; // +0x04
    int mClass; // +0x08
};

class DxWriteHeroWords : public CStackOnly {
public:
    enum {
        MAX_HERO_WORDS = 128,
    };

    DxWriteHeroWords(const int userDbId, const char16_t* const heroWords) : mUserDbId(userDbId) {
        std::size_t length = 0;
        if (heroWords != nullptr) {
            while (length < MAX_HERO_WORDS && heroWords[length] != u'\0') {
                ++length;
            }
        }
        if (heroWords == nullptr || length >= MAX_HERO_WORDS) {
            mHeroWords[0] = u'\0';
            errno = heroWords == nullptr ? EINVAL : ERANGE;
            runtime::process::HandleInvalidParameter();
            return;
        }
        std::memcpy(mHeroWords, heroWords, (length + 1U) * sizeof(char16_t));
    }

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x000
    char16_t mHeroWords[128]; // +0x004
};

class DxStartOlympiadSeason : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    std::int32_t mSeasonStart; // +0x08, target long
    int mReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class DxInitAllOlympiadPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    int mInitPoint; // +0x08
    int mReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class DxAddAllOlympiadBonusPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    int mBonusPoint; // +0x08
    int mReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class DxNominateHeroes : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mNowSeason; // +0x00
    int mNewSeason; // +0x04
    int mNewStep; // +0x08
    int mNewSeasonStart; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class DxSaveOlympiadRecord : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStep; // +0x04
    int mWinnerId; // +0x08
    int mLoserId; // +0x0c
    int mWinnerPoint; // +0x10
    int mLoserPoint; // +0x14
    int mTime; // +0x18
    int mResultType; // +0x1c
    int mElapsedTime; // +0x20
    int mGameRuleType; // +0x24
};

class DxReloadHeroRank : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mHeroCount; // +0x000
    int mHeroClassList[36]; // +0x004
    int mHeroIdList[36]; // +0x094
};

class DxReloadHeroById : public CStackOnly {
public:
    DxReloadHeroById(int charId) :
        mCharId(charId),
        mCharName{},
        mMainClass(0),
        mHeroType(0),
        mPledgeId(0),
        mPledgeName{},
        mPledgeCrestId(0),
        mAllianceId(0),
        mAllianceName{},
        mAllianceCrestId(0),
        mWinCount(0),
        mWords{},
        mHistoryOpen(0),
        mReturn(false) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mCharId; // +0x000
    char16_t mCharName[25]; // +0x004
    int mMainClass; // +0x036
    int mHeroType; // +0x03a
    int mPledgeId; // +0x03e
    char16_t mPledgeName[25]; // +0x042
    int mPledgeCrestId; // +0x074
    int mAllianceId; // +0x078
    char16_t mAllianceName[25]; // +0x07c
    int mAllianceCrestId; // +0x0ae
    int mWinCount; // +0x0b2
    char16_t mWords[129]; // +0x0b6
    int mHistoryOpen; // +0x1b8
    bool mReturn; // +0x1bc
};

class DxDeletePreviousOlympiadPoint : public CStackOnly {
public:
    DxDeletePreviousOlympiadPoint(int userDbId, int previousOlympiadPoint, int npcSid, int userSid) :
        mUserDbId(userDbId), mPreviousOlympiadPoint(previousOlympiadPoint), mNpcSid(npcSid), mUserSid(userSid), mReturn(0) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mPreviousOlympiadPoint; // +0x04
    int mNpcSid; // +0x08
    int mUserSid; // +0x0c
    int mReturn; // +0x10
};

class DxSaveSeasonStartTime : public CStackOnly {
public:
    DxSaveSeasonStartTime(int season, std::int32_t seasonStartTime);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    std::int32_t mSeasonStartTime; // +0x04, target long
};

class DxSaveOlympiadTerm : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x00
    int mStartSec; // +0x04
    int mBonus1Sec; // +0x08
    int mBonus2Sec; // +0x0c
    int mBonus3Sec; // +0x10
    int mBonus4Sec; // +0x14
    int mNominateSec; // +0x18
};

class DxSetHistoryOpen : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mCharId; // +0x00
    int mHistoryOpen; // +0x04
};

class DxDeleteOlympiadTradePoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mOlympiadTradePoint; // +0x04
    int mNpcSid; // +0x08
    int mUserSid; // +0x0c
    int mReturn; // +0x10
};

class DxAddOlympiadTradePoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDbId; // +0x00
    int mOlympiadTradePoint; // +0x04
    int mNpcSid; // +0x08
    int mUserSid; // +0x0c
    int mReturn; // +0x10
};

class CDxSetFortressFacility : public CStackOnly {
public:
    CDxSetFortressFacility(int npcId, int userId, int fortressId, int facilityType, int facilityLevel) :
        m_nNpcId(npcId),
        m_nUserId(userId),
        m_nFortressId(fortressId),
        m_nFacilityType(facilityType),
        m_nFacilityLevel(facilityLevel),
        m_nReturn(1) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nNpcId; // +0x00
    int m_nUserId; // +0x04
    int m_nFortressId; // +0x08
    int m_nFacilityType; // +0x0c
    int m_nFacilityLevel; // +0x10
    int m_nReturn; // +0x14
};

class CDxResetFortressFacility : public CStackOnly {
public:
    CDxResetFortressFacility(int npcId, int userId, int fortressId) :
        m_nNpcId(npcId), m_nUserId(userId), m_nFortressId(fortressId) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nNpcId; // +0x00
    int m_nUserId; // +0x04
    int m_nFortressId; // +0x08
};

class DxAddOlympiadPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mPoint; // +0x00
    int mUserId; // +0x04
};

// Legacy Instant Agit, fishing/matching-point and decoration-NPC jobs.
// TotalChecker2015.pdb supplies the exact source member names and packed
// layouts. Current Cached.exe fixes every field used below.
class CApplyInstantAgit : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class CInstantAgitData;

    int npcIndex; // +0x00
    int userIndex; // +0x04
    int instantAgitId; // +0x08
    int pledgeDBId; // +0x0c
    int rentalFee; // +0x10
    int inzoneTypeId; // +0x14
    int applyTime; // +0x18
    int nReturn; // +0x1c
    int nReason; // +0x20
};

class CAddInstantAgitDeco : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class CInstantAgit;

    int npcIndex; // +0x00
    int userIndex; // +0x04
    int instantAgitId; // +0x08
    int pledgeDBId; // +0x0c
    int agitDecoId; // +0x10
    int removeAgitDecoId; // +0x14
    int adenaAmount; // +0x18
    int nReturn; // +0x1c
};

class CReturnInstantAgit : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    friend class CInstantAgitData;
    friend class CInstantAgitManager;

    int npcIndex; // +0x00
    int userIndex; // +0x04
    int pledgeDBId; // +0x08
    int nReturn; // +0x0c
};

class DxResetOlympiadPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mPoint; // +0x00
    int mUserId; // +0x04
};

class DxFishingChampionShip : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mSeason; // +0x000
    int mUserDBID[5]; // +0x004
    int mFishLength[5]; // +0x018
    char16_t sCharName[5][25]; // +0x02c
    std::int32_t mSeasonStart; // +0x126, target long
};

class DxMatchingPoint : public CStackOnly {
public:
    DxMatchingPoint(
        bool isSave,
        MatchingPoint::MATCHINGPOINT_TYPE type,
        std::uint32_t userSID,
        std::uint32_t userDBID,
        std::int64_t matchingPointDiff
    ) :
        m_isSave(isSave),
        m_cType(type),
        m_nUserSID(userSID),
        m_nUserDBID(userDBID),
        m_nMatchingPointDiff(matchingPointDiff) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    bool m_isSave; // +0x00
    MatchingPoint::MATCHINGPOINT_TYPE m_cType; // +0x01
    std::uint32_t m_nUserSID; // +0x05
    std::uint32_t m_nUserDBID; // +0x09
    std::int64_t m_nMatchingPointDiff; // +0x0d
};

class CSetResidenceDecoNpc : public CStackOnly {
public:
    CSetResidenceDecoNpc(
        int userSID,
        int userDBID,
        int userAccountID,
        const FVector& position,
        int pledgeDBID,
        int agitCID,
        int agitSlot,
        int decoCID,
        int expireTime,
        int cost,
        int itemCID,
        std::int64_t itemCount,
        int returnValue
    );
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nUserSID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nUserAccountID; // +0x08
    int m_nPosX; // +0x0c
    int m_nPosY; // +0x10
    int m_nPosZ; // +0x14
    int m_nPledgeDBID; // +0x18
    int m_nAgitCID; // +0x1c
    int m_nAgitSlot; // +0x20
    int m_nDecoCID; // +0x24
    int m_nExpireTime; // +0x28
    int m_nCost; // +0x2c
    int m_nItemCID; // +0x30
    std::int64_t m_nItemCount; // +0x34
    int m_nReturn; // +0x3c
};

class CResetResidenceDecoNpc : public CStackOnly {
public:
    CResetResidenceDecoNpc(
        int userSID, int userDBID, int userAccountID, const FVector& position, int pledgeDBID, int agitCID, int agitSlot
    );
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nUserSID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nUserAccountID; // +0x08
    int m_nPosX; // +0x0c
    int m_nPosY; // +0x10
    int m_nPosZ; // +0x14
    int m_nPledgeDBID; // +0x18
    int m_nAgitCID; // +0x1c
    int m_nAgitSlot; // +0x20
    int m_nReturn; // +0x24
};

class CResetAllResidenceDecoNpc : public CStackOnly {
public:
    CResetAllResidenceDecoNpc(int pledgeDatabaseId, int agitId) noexcept;
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nPledgeDBID; // +0x00
    int m_nAgitCID; // +0x04
    int m_nReturn; // +0x08
};

class CExpireResidenceDecoNpc : public CStackOnly {
public:
    CExpireResidenceDecoNpc(int pledgeDatabaseId, int agitId, int count, const int* agitSlots) noexcept;

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nPledgeDBID; // +0x00
    int m_nAgitCID; // +0x04
    int m_nCount; // +0x08
    int m_aAgitSlots[5]; // +0x0c
    int m_aReturns[5]; // +0x20
};

class CSetInstantAgitDecoNpc : public CStackOnly {
public:
    CSetInstantAgitDecoNpc(
        int userSID,
        int userDBID,
        int userAccountID,
        const FVector& position,
        int pledgeDBID,
        int instantAgitCID,
        int agitSlot,
        int decoCID,
        int expireTime,
        int cost,
        int itemCID,
        std::int64_t itemCount,
        int returnValue
    );

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nUserSID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nUserAccountID; // +0x08
    int m_nPosX; // +0x0c
    int m_nPosY; // +0x10
    int m_nPosZ; // +0x14
    int m_nPledgeDBID; // +0x18
    int m_nInstantAgitCID; // +0x1c
    int m_nAgitSlot; // +0x20
    int m_nDecoCID; // +0x24
    int m_nExpireTime; // +0x28
    int m_nCost; // +0x2c
    int m_nItemCID; // +0x30
    std::int64_t m_nItemCount; // +0x34
    int m_nReturn; // +0x3c
};

class CResetInstantAgitDecoNpc : public CStackOnly {
public:
    CResetInstantAgitDecoNpc(
        int userSID,
        int userDBID,
        int userAccountID,
        const FVector& position,
        int instantAgitCID,
        int pledgeDBID,
        int agitSlot
    );

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nUserSID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nUserAccountID; // +0x08
    int m_nPosX; // +0x0c
    int m_nPosY; // +0x10
    int m_nPosZ; // +0x14
    int m_nInstantAgitCID; // +0x18
    int m_nPledgeDBID; // +0x1c
    int m_nAgitSlot; // +0x20
    int m_nReturn; // +0x24
};

class CExpireInstantAgitDecoNpc : public CStackOnly {
public:
    CExpireInstantAgitDecoNpc(int instantAgitCID, int pledgeDBID, int count, const int* agitSlots);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nInstantAgitCID; // +0x00
    int m_nPledgeDBID; // +0x04
    int m_nCount; // +0x08
    int m_aAgitSlots[5]; // +0x0c
    int m_aReturns[5]; // +0x20
};

// Current World Olympiad jobs in DX slots 66 through 93. The surviving
// TotalChecker2015 PDB supplies the exact source identifiers and packing.
// every result/input access and complete object size is independently
// confirmed by the matching current Cached.exe DoCacheDJob body.
class CDX_WOLYMPIAD_CHECK_PEER : public CStackOnly {
public:
    explicit CDX_WOLYMPIAD_CHECK_PEER(int worldId);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    bool m_bIsValid; // +0x04
};

class CDX_WOLYMPIAD_LOAD : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nStep; // +0x04
    std::int32_t m_tSeasonStart; // +0x08, target long
    int m_nStartSec; // +0x0c
    int m_nBonus1Sec; // +0x10
    int m_nBonus2Sec; // +0x14
    int m_nBonus3Sec; // +0x18
    int m_nBonus4Sec; // +0x1c
    int m_nNominateSec; // +0x20
    int m_idLegendary; // +0x24
};

class CDX_WOLYMPIAD_GET_TOP10 : public CStackOnly {
public:
    CDX_WOLYMPIAD_GET_TOP10(int heroClassType, int season);
    bool DoCacheDJob();
    void DoServerJob();

private:
    struct UserRank {
        int nWorldID; // +0x00
        int nUserDBID; // +0x04
        int nPoint; // +0x08
        int nMatchCount; // +0x0c
        int nWinCount; // +0x10
        char16_t sCharName[25]; // +0x14
    };

    int m_nHeroClassType; // +0x000
    int m_nSeason; // +0x004
    int m_nCount; // +0x008
    UserRank m_Ranks[10]; // +0x00c
};

class CDX_WOLYMPIAD_GET_PLAYER_COUNT : public CStackOnly {
public:
    explicit CDX_WOLYMPIAD_GET_PLAYER_COUNT(int season);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nCount; // +0x04
};

class CDX_WOLYMPIAD_ADD_USER : public CStackOnly {
public:
    enum NextAction : int {
        NA_Register = 0,
        NA_NoblessInfo = 1,
    };

    CDX_WOLYMPIAD_ADD_USER(
        int worldId,
        int userDbId,
        int classType,
        int heroClassType,
        const char16_t* charName,
        int pledgeDbId,
        const char16_t* pledgeName,
        int pledgeCrestId,
        int allianceDbId,
        const char16_t* allianceName,
        int allianceCrestId,
        int defaultOlympiadPoint,
        NextAction nextAction
    );

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nClass; // +0x08
    int m_nHeroClassType; // +0x0c
    char16_t m_szCharName[25]; // +0x10
    int m_nPledgeDBID; // +0x42
    char16_t m_szPledgeName[25]; // +0x46
    int m_nPledgeCrestID; // +0x78
    int m_nAllianceDBID; // +0x7c
    char16_t m_szAllianceName[25]; // +0x80
    int m_nAllianceCrestID; // +0xb2
    int m_nDefaultOlympiadPoint; // +0xb6
    int m_nReturn; // +0xba
    NextAction m_eNextAction; // +0xbe
};

class CDX_WOLYMPIAD_SAVE_TERM : public CStackOnly {
public:
    CDX_WOLYMPIAD_SAVE_TERM(
        int season, int startSec, int bonus1Sec, int bonus2Sec, int bonus3Sec, int bonus4Sec, int nominateSec
    );

    bool DoCacheDJob();
    void DoServerJob() {}

private:
    int m_nSeason; // +0x00
    int m_nStartSec; // +0x04
    int m_nBonus1Sec; // +0x08
    int m_nBonus2Sec; // +0x0c
    int m_nBonus3Sec; // +0x10
    int m_nBonus4Sec; // +0x14
    int m_nNominateSec; // +0x18
};

class CDX_WOLYMPIAD_NOMINATE_HEROES : public CStackOnly {
public:
    CDX_WOLYMPIAD_NOMINATE_HEROES(int nowSeason, int newStep, std::int32_t newSeasonStart, bool nominateHeroes);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nNowSeason; // +0x00
    int m_nNewSeason; // +0x04
    int m_nNewStep; // +0x08
    std::int32_t m_tNewSeasonStart; // +0x0c, target long
    bool m_bNominateHeroes; // +0x10
    int m_idLegendary; // +0x11
    OlympiadHelper nothing; // +0x15
};

class CDX_WOLYMPIAD_RELOAD_HERO_RANK : public CStackOnly {
public:
    CDX_WOLYMPIAD_RELOAD_HERO_RANK();
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nHeroCount; // +0x000
    int m_nHeroClassList[36]; // +0x004
    int m_nHeroWorldIdList[36]; // +0x094
    int m_nHeroIdList[36]; // +0x124
};

class CDX_WOLYMPIAD_SAVE_MATCH_COUNT : public CStackOnly {
public:
    CDX_WOLYMPIAD_SAVE_MATCH_COUNT(int worldId, int userDbId, int season, int step, int gameRule, int matchCount);
    bool DoCacheDJob();
    void DoServerJob() {}

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nSeason; // +0x08
    int m_nStep; // +0x0c
    int m_nGameRule; // +0x10
    int m_nMatchCount; // +0x14
};

class CDX_WOLYMPIAD_LOG_PENALTY : public CStackOnly {
public:
    CDX_WOLYMPIAD_LOG_PENALTY(
        int worldId, int userDbId, int point, int totalPoint, int fieldId, int gameRule, int penaltyType
    );
    bool DoCacheDJob();
    void DoServerJob() {}

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nPoint; // +0x08
    int m_nTotalPoint; // +0x0c
    int m_nFieldID; // +0x10
    int m_nGameRule; // +0x14
    int m_nPenaltyType; // +0x18
};

class CDX_WOLYMPIAD_RELOAD_USER : public CStackOnly {
public:
    CDX_WOLYMPIAD_RELOAD_USER(int worldId, int userDatabaseId) :
        m_nWorldID(worldId),
        m_nUserDBID(userDatabaseId),
        m_nHeroType(0),
        m_nWinCount(0),
        m_nPreviousPoint(0),
        m_nOlympiadPoint(0),
        m_nMatchCount(0),
        m_nOlympiadWinCount(0),
        m_nOlympiadLoseCount(0),
        m_nHistoryOpen(0),
        m_nOlympiadTradePoint(0) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nHeroType; // +0x08
    int m_nWinCount; // +0x0c
    int m_nPreviousPoint; // +0x10
    int m_nOlympiadPoint; // +0x14
    int m_nMatchCount; // +0x18
    int m_nOlympiadWinCount; // +0x1c
    int m_nOlympiadLoseCount; // +0x20
    int m_nHistoryOpen; // +0x24
    int m_nOlympiadTradePoint; // +0x28
};

class CDX_WOLYMPIAD_INIT_ALL_POINT : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nStep; // +0x04
    int m_nInitPoint; // +0x08
    int m_nReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class CDX_WOLYMPIAD_ADD_ALL_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_ADD_ALL_POINT(int season, int step, int bonusPoint);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nStep; // +0x04
    int m_nBonusPoint; // +0x08
    int m_nReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class CDX_WOLYMPIAD_SAVE_RECORD : public CStackOnly {
public:
    CDX_WOLYMPIAD_SAVE_RECORD(
        int season,
        int step,
        int winnerWorldId,
        int winnerDbId,
        int loserWorldId,
        int loserDbId,
        int winnerPoint,
        int winnerItemType,
        int winnerItemAmount,
        int loserPoint,
        int loserItemType,
        int loserItemAmount,
        int time,
        int resultType,
        int elapsedTime,
        int gameRuleType
    );
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nStep; // +0x04
    int m_nWinnerWorldID; // +0x08
    int m_nWinnerDBID; // +0x0c
    int m_nLoserWorldID; // +0x10
    int m_nLoserDBID; // +0x14
    int m_nWinnerPoint; // +0x18
    int m_nWinnerItemType; // +0x1c
    int m_nWinnerItemAmount; // +0x20
    int m_nLoserPoint; // +0x24
    int m_nLoserItemType; // +0x28
    int m_nLoserItemAmount; // +0x2c
    int m_nTime; // +0x30
    int m_nResultType; // +0x34
    int m_nElapsedTime; // +0x38
    int m_nGameRuleType; // +0x3c
};

class CDX_WOLYMPIAD_SAVE_SEASON_START_TIME : public CStackOnly {
public:
    CDX_WOLYMPIAD_SAVE_SEASON_START_TIME(int season, std::int32_t seasonStart);

    bool DoCacheDJob();
    void DoServerJob() {}

private:
    int m_nSeason; // +0x00
    std::int32_t m_tSeasonStartTime; // +0x04, target long
};

class CDX_WOLYMPIAD_RELOAD_HERO_BY_ID : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x000
    int m_nUserDBID; // +0x004
    char16_t m_szCharName[25]; // +0x008
    int m_nHeroClassType; // +0x03a
    int m_nHeroType; // +0x03e
    int m_nPledgeDBID; // +0x042
    char16_t m_szPledgeName[25]; // +0x046
    int m_nPledgeCrestID; // +0x078
    int m_nAllianceDBID; // +0x07c
    char16_t m_szAllianceName[25]; // +0x080
    int m_nAllianceCrestID; // +0x0b2
    int m_nWinCount; // +0x0b6
    char16_t m_szWords[129]; // +0x0ba
    int m_nHistoryOpen; // +0x1bc
    bool m_nReturn; // +0x1c0
};

class CDX_WOLYMPIAD_ADD_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_ADD_POINT(int worldId, int userDbId, int point);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nPoint; // +0x04
    int m_nUserDBID; // +0x08
};

class CDX_WOLYMPIAD_START_SEASON : public CStackOnly {
public:
    CDX_WOLYMPIAD_START_SEASON(int season, int step, std::int32_t seasonStart);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nSeason; // +0x00
    int m_nStep; // +0x04
    std::int32_t m_tSeasonStart; // +0x08, target long
    int m_nReturn; // +0x0c
    OlympiadHelper nothing; // +0x10
};

class CDX_WOLYMPIAD_SAVE_HERO_TYPE : public CStackOnly {
public:
    CDX_WOLYMPIAD_SAVE_HERO_TYPE(
        int worldId,
        int userDbId,
        int heroType,
        const char16_t* charName,
        int pledgeDbId,
        const char16_t* pledgeName,
        int pledgeCrestId,
        int allianceDbId,
        const char16_t* allianceName,
        int allianceCrestId,
        int npcSid,
        int userSid
    );
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nHeroType; // +0x08
    char16_t m_szCharName[25]; // +0x0c
    int m_nPledgeDBID; // +0x3e
    char16_t m_szPledgeName[25]; // +0x42
    int m_nPledgeCrestID; // +0x74
    int m_nAllianceDBID; // +0x78
    char16_t m_szAllianceName[25]; // +0x7c
    int m_nAllianceCrestID; // +0xae
    int m_nNpcSID; // +0xb2
    int m_nUserSID; // +0xb6
    int m_nReturn; // +0xba
};

class CDX_WOLYMPIAD_DELETE_TRADE_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_DELETE_TRADE_POINT(
        int worldId,
        int userDbId,
        const char16_t* charName,
        int accountId,
        const char16_t* accountName,
        int tradePoint,
        int npcSid
    );
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    char16_t m_szCharName[25]; // +0x08
    int m_nAccountID; // +0x3a
    char16_t m_szAccountName[15]; // +0x3e
    int m_nTradePoint; // +0x5c
    int m_nNpcSID; // +0x60
    int m_nReturn; // +0x64
};

class CDX_WOLYMPIAD_ADD_TRADE_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_ADD_TRADE_POINT(int worldId, int userDbId, int tradePoint);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nTradePoint; // +0x08
    int m_nReturn; // +0x0c
};

class CDX_WOLYMPIAD_RESET_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_RESET_POINT(int worldId, int userDbId, int point);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nWorldID; // +0x00
    int m_nUserDBID; // +0x04
    int m_nPoint; // +0x08
};

class CDX_WOLYMPIAD_MATCHING_POINT : public CStackOnly {
public:
    CDX_WOLYMPIAD_MATCHING_POINT(int worldId, int userDbId);
    bool DoCacheDJob();
    void DoServerJob();

private:
    int m_nUserDBID; // +0x00
    int m_nWorldID; // +0x04
    std::int64_t m_nPoint; // +0x08
    bool m_bSuccess; // +0x10
};

class CDX_WOLYMPIAD_MATCHING_POINT_SAVE : public CStackOnly {
public:
    CDX_WOLYMPIAD_MATCHING_POINT_SAVE();
    void AddRecord(int worldId, int userDbId, int matchId, std::int64_t newPoint, std::int64_t oldPoint);
    bool HasRecord() const;
    bool DoCacheDJob();
    void DoServerJob() {}

private:
    int m_nCount; // +0x00
    std::int64_t m_anNewPoints[3]; // +0x04
    std::int64_t m_anOldPoints[3]; // +0x1c
    int m_anUserDBIDs[3]; // +0x34
    int m_anWorldIDs[3]; // +0x40
    int m_anMatchIDs[3]; // +0x4c
    int m_anResults[3]; // +0x58
};

class CDX_WOLYMPIAD_MATCHING_POINT_RESET_ALL : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob() {}
};

class CDX_WOLYMPIAD_RECORD : public CDX_ATOMICJOB_BASE {
public:
    CDX_WOLYMPIAD_RECORD(int worldId, int userDbId, int season, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

    // The current free builder callback at 0x008f1a28 mutates these packet
    // fields directly before publishing the response. They are therefore an
    // original public packet boundary instead of private implementation.
    friend class WOlympiadUsers;
    int idWorld; // +0x04
    int idUser; // +0x08
    int nSeason; // +0x0c
    int nPoint; // +0x10
    int nWinCount; // +0x14
    int nLoseCount; // +0x18
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
};

class CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST : public CDX_ATOMICJOB_BASE {
public:
    CDX_WOLYMPIAD_GENERATE_RECORD_FOR_TEST(
        int maxOlympiadPoint, int maxOlympiadWinCount, int maxOlympiadMatchCount, AtomicJob* job = nullptr
    );
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nMaxOlympiadPoint; // +0x04
    int nMaxOlympiadWinCount; // +0x08
    int nMaxOlympiadMatchCount; // +0x0c
};

class CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10 : public CDX_ATOMICJOB_BASE {
public:
    CDX_WOLYMPIAD_GET_CLASS_RANK_BY_SERVER_TOP10(int worldId, int heroClassType, int season, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class WOlympiadService;

    struct UserRank {
        int nWorldID; // +0x00
        int nUserDBID; // +0x04
        int nPoint; // +0x08
        int nMatchCount; // +0x0c
        int nWinCount; // +0x10
        char16_t sCharName[25]; // +0x14
    };

    int m_idWorld; // +0x004
    int m_nHeroClassType; // +0x008
    int m_nSeason; // +0x00c
    int m_nCount; // +0x010
    UserRank m_Ranks[10]; // +0x014
};

// Current pledge mastery/contribution/raid/mission jobs in DX slots 94
// through 115. TotalChecker2015.pdb supplies the exact source identifiers
// and packing. The current Cached.exe confirms every complete
// object extent and every accessed field.
class CDX_PLEDGE_MASTERY_LOAD : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_MASTERY_LOAD(int pledgeDatabaseId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeMastery;

    int m_idPledge; // +0x04
    int m_anPoints[17]; // +0x08
    bool m_bSuccess; // +0x4c
};

class CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_CONTRIBUTION_MEMBER_ADD(int userDatabaseId, int pledgeDatabaseId, int point, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int m_dbidUser; // +0x04
    int m_dbidPledge; // +0x08
    int m_nPoint; // +0x0c
};

class CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_CONTRIBUTION_MEMBER_RESET(int userDatabaseId, int pledgeDatabaseId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int m_dbidUser; // +0x04
    int m_dbidPledge; // +0x08
};

class CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_CONTRIBUTION_MEMBER_TAKE_PERIOD_REWARD(int userDatabaseId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeContributionSystem;

    int m_idUser; // +0x04
    int m_idPledge; // +0x08
    int m_nResult; // +0x0c
    int m_nRank; // +0x10
    int m_nRatio; // +0x14
    int m_nActiveMember; // +0x18
};

class CDX_PLEDGE_CONTRIBUTION_RANK_LOAD : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_CONTRIBUTION_RANK_LOAD(AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeContributionSystem;
    int nSeason; // +0x04
    std::int32_t tStartTime; // +0x08, target long
};

class CDX_PLEDGE_CONTRIBUTION_RANK_SAVE : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_CONTRIBUTION_RANK_SAVE(int season, std::int32_t startTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nSeason; // +0x04
    std::int32_t tStartTime; // +0x08, target long
};

class CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_CONTRIBUTION_RANK_DECIDE(int season, std::int32_t startTime, int minimumPoint, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nSeason; // +0x04
    std::int32_t tStartTime; // +0x08, target long
    int nMinPoint; // +0x0c
};

class CDX_PLEDGE_RAID_LOAD : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_RAID_LOAD(AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    int nSeason_; // +0x04
    std::int32_t tStartTime_; // +0x08, target long
    int nPrevSeason_; // +0x0c
    std::int32_t tPrevStartTime_; // +0x10, target long
};

class CDX_PLEDGE_RAID_SAVE : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_SAVE(int season, std::int32_t startTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    int nSeason_; // +0x04
    std::int32_t tStartTime_; // +0x08, target long
};

class CDX_PLEDGE_RAID_NEXT_SEASON : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_RAID_NEXT_SEASON(std::int32_t startTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    std::int32_t tStartTime_; // +0x04, target long
};

class CDX_PLEDGE_RAID_RESET_SEASON : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_RAID_RESET_SEASON(std::int32_t resetTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    std::int32_t tResetTime_; // +0x04, target long
};

class CDX_PLEDGE_RAID_GET_INFO : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_GET_INFO(int season, int worldId, int pledgeDbId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;
    friend class PledgeRaidPacketHandlerAccess;

    int nSeason_; // +0x04
    int nWorldId_; // +0x08
    int nPledgeDbId_; // +0x0c
    char16_t szPledgeName_[25]; // +0x10
    int nRank_; // +0x42
    std::int64_t nPoint_; // +0x46
    int nRewardReason_; // +0x4e
    int nTotalCount_; // +0x52
};

class CDX_PLEDGE_RAID_GET_TOTAL_COUNT : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_GET_TOTAL_COUNT(int season, int isReward, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    int nSeason_; // +0x04
    int nIsReward_; // +0x08
    int nTotalCount_; // +0x0c
};

class CDX_PLEDGE_RAID_SET_INFO : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_SET_INFO(
        int season,
        int worldId,
        int pledgeDbId,
        const wchar_t* pledgeName,
        std::int64_t point,
        std::int32_t saveTime,
        AtomicJob* job = nullptr
    );
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    int nSeason_; // +0x04
    int nWorldId_; // +0x08
    int nPledgeDbId_; // +0x0c
    char16_t szPledgeName_[25]; // +0x10
    std::int32_t tSaveTime_; // +0x42, target long
    std::int64_t nPoint_; // +0x46
};

class CDX_PLEDGE_RAID_HUNT_NPC_INFO : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_HUNT_NPC_INFO(const wchar_t* npcDbName, std::int32_t deathTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    char16_t szNpcDbName_[25]; // +0x04
    std::int32_t tDeathTime_; // +0x36, target long
};

class CDX_PLEDGE_RAID_INZONE_SAVE : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_RAID_INZONE_SAVE(int pledgeDbId, std::int32_t restrictTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    friend class PledgeRaidSystem;

    int nPledgeDbId_; // +0x04
    std::int32_t tRestrictTime_; // +0x08, target long
};

class CDX_PLEDGE_RAID_INZONE_DELETE : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_RAID_INZONE_DELETE(int pledgeDbId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nPledgeDbId_; // +0x04
};

class CDX_PLEDGE_RAID_INZONE_RESET : public CDX_ATOMICJOB_BASE {
public:
    explicit CDX_PLEDGE_RAID_INZONE_RESET(AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }
};

class CDX_PLEDGE_MISSION_SET_COUNT : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_MISSION_SET_COUNT(int userDbId, int missionId, int count, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

    int nUserDbId_; // +0x04
    int nMissionId_; // +0x08
    int nCount_; // +0x0c
};

class CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_LOAD(int userDbId, std::int32_t checkWeekStartTime, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

    int nUserDbId_; // +0x04
    std::int32_t tCheckWeekStartTime_; // +0x08, target long
    std::int32_t tWeekStartTime_; // +0x0c, target long
    int nCount_; // +0x10
};

class CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_ADD : public CDX_ATOMICJOB_BASE {
public:
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nUserDbId_; // +0x04
    int nAddCount_; // +0x08
    std::int32_t tWeekStartTime_; // +0x0c, target long
};

class CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET : public CDX_ATOMICJOB_BASE {
public:
    CDX_PLEDGE_MISSION_COUNT_OF_ONE_WEEK_RESET(int userDatabaseId, AtomicJob* job = nullptr);
    bool DoCacheDJob();
    void DoServerJob() { CDX_ATOMICJOB_BASE::DoServerJob(); }

private:
    int nUserDbId_; // +0x04
};

// Seven Signs jobs. TotalChecker2015.pdb supplies these exact source member
// names and packed layouts. Every size and every field read/write below is
// present in the current Cached.exe DoCacheDJob body.
class DxChangeSSQPeriod : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mStatus; // +0x00
    int mStartTime; // +0x04
    int mEndTime; // +0x08
};

class DxStartSSQAccount : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mStatus; // +0x00
};

class DxStartSSQSealEffect : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mStatus; // +0x00
};

class DxChangeSSQStatus : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mStatus; // +0x00
};

class DxSetTicketBuyCount : public CStackOnly {
public:
    DxSetTicketBuyCount(int userDbId, int userSid, int count) : mUserDBID(userDbId), mUserSID(userSid), mCount(count) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mUserDBID; // +0x00
    int mUserSID; // +0x04
    int mCount; // +0x08
};

class DxSetSevenSignQuestResult : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mWinner; // +0x00
    int mSealOwner[8]; // +0x04
};

class DxCloseSevenSignQuest : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mStatus; // +0x00
};

class DxAddSevenSignQuestMember : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mUserSID; // +0x08
    int mSSQRound; // +0x0c
    int mSSQPart; // +0x10
    int mSealSelectionNo; // +0x14
    int mPosition; // +0x18
    std::int64_t mInitialPoint; // +0x1c
    int mJoinType; // +0x24
};

class DxDeleteDepositedSevenSignQuestItem : public CStackOnly {
public:
    DxDeleteDepositedSevenSignQuestItem(
        int userDatabaseId, int userServerId, SSQPart::type ssqPart, SSQItem::type itemType, std::int64_t count
    ) :
        mErrorCode(0),
        mUserDBID(userDatabaseId),
        mUserSID(userServerId),
        mSSQPart(ssqPart),
        mItemType(itemType),
        mCount(count) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mUserSID; // +0x08
    SSQPart::type mSSQPart; // +0x0c
    SSQItem::type mItemType; // +0x10
    std::int64_t mCount; // +0x14
};

class DxChangeSealSelectionCount : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    SSQPart::type mPart; // +0x04
    SSQSeal::type mSealNum; // +0x08
    int mCount; // +0x0c
};

class DxChangeSSQUserPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mUserSID; // +0x08
    SSQPart::type mPart; // +0x0c
    std::int64_t mPoint; // +0x10
};

class DxStartQuickCycle : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    SSQStatus::type mStatus; // +0x04
    int mEventStartTime; // +0x08
    int mEventEndTime; // +0x0c
    int mSealEffectTime; // +0x10
    int mSealEffectEndTime; // +0x14
};

class DxChangeMainEventPoint : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    SSQPart::type mPartType; // +0x04
    int mMainEventPoint; // +0x08
};

class DxChangeSealOwner : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    SSQSeal::type mSealNumber; // +0x04
    SSQPart::type mPartType; // +0x08
};

class DxChangeSSQWinner : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    SSQPart::type mPartType; // +0x04
};

class DxDismissSevenSignMember : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mUserSID; // +0x08
    SSQPart::type mFromPart; // +0x0c
    std::int64_t mPoint; // +0x10
    SSQSeal::type mSealSelectionNo; // +0x18
};

class DxChangeCycleMode : public CStackOnly {
public:
    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mCycleModeFlag; // +0x04
};

class DxDepositSevenSignQuestItemEx : public CStackOnly {
public:
    DxDepositSevenSignQuestItemEx(
        int userDatabaseId, int userServerId, SSQPart::type ssqPart, std::int64_t countList[], std::int64_t point
    ) :
        mErrorCode(0),
        mUserDBID(userDatabaseId),
        mUserSID(userServerId),
        mSSQPart(ssqPart),
        mCountList{countList[0], countList[1], countList[2]},
        mPoint(point) {}

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mUserSID; // +0x08
    SSQPart::type mSSQPart; // +0x0c
    std::int64_t mCountList[3]; // +0x10
    std::int64_t mPoint; // +0x28
};

class DxAddTimeAttackRecord : public CStackOnly {
public:
    DxAddTimeAttackRecord(int roomNo, SSQPart::type ssqPart, std::int64_t point, int recordTime, int elapsedTime);

    bool DoCacheDJob();
    void DoServerJob();
    bool AddMember(const char16_t* memberName, int memberDatabaseId);
    int GetMemberCount() const;

private:
    int mErrorCode; // +0x000
    int mRoomNo; // +0x004
    SSQPart::type mSSQPart; // +0x008
    std::int64_t mPoint; // +0x00c
    int mRecordTime; // +0x014
    int mElapsedTime; // +0x018
    int mMemberCount; // +0x01c
    char16_t mMemberNameList[9][25]; // +0x020
    int mMemberDBIDList[9]; // +0x1e2
    int mTopRecordUpdated; // +0x206
    int mPartRecordUpdated; // +0x20a
};

class DxAddTimeAttackFee : public CStackOnly {
public:
    DxAddTimeAttackFee(int roomNo, std::int64_t fee);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mRoomNO; // +0x04 -- exact PDB spelling
    std::int64_t mFee; // +0x08
};

class DxSetTimeAttackRewardFlag : public CStackOnly {
public:
    DxSetTimeAttackRewardFlag(int userDatabaseId, int roomNo, int partType, int flag);

    bool DoCacheDJob();
    void DoServerJob();

private:
    int mErrorCode; // +0x00
    int mUserDBID; // +0x04
    int mRoomNO; // +0x08 -- exact PDB spelling
    int mPartType; // +0x0c
    int mFlag; // +0x10
};

#pragma pack(pop)
