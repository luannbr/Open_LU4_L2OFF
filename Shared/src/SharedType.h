#pragma once

#include "SSQ.h"
#include "Shared/src/TemplateHelper.h"

#include "Shared/src/Runtime/FixedSharedMemoryTransport.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

// PDB path /MatchingPoint/MATCHINGPOINT_TYPE. This is a source namespace in
// the shared server vocabulary, not a portable-only wrapper.
namespace MatchingPoint {

enum MATCHINGPOINT_TYPE : std::int32_t {
    MPT_BASE = 0,
    MPT_NONE = 0,
    MPT_PLEDGEGAME = 0,
    MPT_OLYMPIAD = 1,
    MPT_MAX = 2,
};

} // namespace MatchingPoint


// Shared/SharedType.h. PDB type 0x14270 confirms the complete target
// surface and its two-field 0x10 layout.
template <typename T, std::size_t Count>
class CSharedArray {
public:
    explicit CSharedArray(const wchar_t* name) {
#if defined(L2_BUILD_L2SERVER)
        m_currentIndex = 0;
        m_slotCount = 0;
        const portable::FixedSharedMemoryBinding binding = portable::CreateFixedSharedMemory(name, Count * sizeof(T));
#else
        const portable::FixedSharedMemoryBinding binding = portable::OpenFixedSharedMemory(name, Count * sizeof(T));
#endif
        m_hfm = binding.owner;
        m_index = static_cast<T*>(binding.records);
#if defined(L2_BUILD_L2SERVER)
        // Current L2Server.exe 0x00a51208 and 0x00a57144 create the
        // producer specialization: zero the published records, then seed
        // the local circular free-slot carrier in 1..Count-1,0 order.
        std::memset(m_index, 0, Count * sizeof(T));
        for (std::size_t index = 0; index < Count; ++index) {
            ++m_slotCount;
            m_freeSlots[static_cast<std::size_t>(m_slotCount) % Count] = static_cast<int>(index);
        }
#endif
    }

    ~CSharedArray() { portable::CloseFixedSharedMemory(m_hfm); }

    // These producer-side members are part of the PDB-confirmed template
    // surface but have no compiled consumer implementation in L2NPC.exe.
    int AllocSlot();
    void FreeSlot(int index);

    T& operator[](const int index) {
        int checkedIndex = index;
        if (checkedIndex < 0 || checkedIndex >= static_cast<int>(Count)) {
            portable::ReportFixedSharedArrayIndexError(checkedIndex, static_cast<int>(Count));
            checkedIndex = 0;
        }
        return m_index[checkedIndex];
    }

    const T& operator[](const int index) const { return const_cast<CSharedArray*>(this)->operator[](index); }

private:
    void* m_hfm;
    T* m_index;
#if defined(L2_BUILD_L2SERVER)
    int m_freeSlots[Count];
    int m_currentIndex;
    int m_slotCount;
#endif
};


// Shared/SharedType.h:160-240. The adjacent PDB fixes the complete public
// surface and every field offset. Current L2NPC.exe 0x0053447c-0x00535a74
// fixes the consumer implementation below. The producer-only CreateMMF
// member is declaration-only because L2NPC emits and calls only
// OpenMMF.
template <typename T, int RecordsPerSegment, int SegmentGrowth, int MaximumSegments>
class CSharedVector {
public:
    struct SharedVectorSharedInfo {
        int nSegmentCount;
        int nCount;
    };

    explicit CSharedVector(const wchar_t* sharedMemoryName) : m_psi(nullptr) {
#if defined(L2_BUILD_L2SERVER)
        m_nLocalSegmentCount = 0;
#endif
        m_pszSMName = sharedMemoryName;

        wchar_t sharedInfoName[104]{};
        std::swprintf(sharedInfoName, std::size(sharedInfoName), L"%ls_si", m_pszSMName);

        const portable::FixedSharedMemoryBinding binding =
#if defined(L2_BUILD_L2SERVER)
            portable::CreateFixedSharedMemory(
#else
            portable::OpenFixedSharedMemory(
#endif
                sharedInfoName, sizeof(SharedVectorSharedInfo)
            );
        m_si_hfm = binding.owner;
        m_nLocalSegmentCount = 0;
        m_psi = static_cast<SharedVectorSharedInfo*>(binding.records);
#if defined(L2_BUILD_L2SERVER)
        // Current producer constructors 0x00a58254..0x00a58e24 write the
        // two shared counters before publishing segment zero.
        m_psi->nSegmentCount = 0;
        m_psi->nCount = 0;
        Resize(0);
#else
        Resize(m_psi->nSegmentCount - 1);
#endif
    }

    ~CSharedVector() {
#if defined(L2_BUILD_L2SERVER)
        const int segmentCount = m_psi->nSegmentCount;
#else
        const int segmentCount = m_nLocalSegmentCount;
#endif
        for (int segment = 0; segment < segmentCount; ++segment) {
            if (segment < MaximumSegments) {
                portable::CloseFixedSharedMemory(m_hfm[segment]);
            }
        }
        portable::CloseFixedSharedMemory(m_si_hfm);
    }

    T& operator[](const int index) {
        const int segment = index / RecordsPerSegment;
        Resize(segment);
#if defined(L2_BUILD_L2SERVER)
        if (m_psi->nCount <= index) {
            {
                L2_GUARD(mLock);
                if (m_psi->nCount <= index) {
                    m_psi->nCount = index + 1;
                }
            }
        }
#endif
        return m_index[segment][index % RecordsPerSegment];
    }

    int Size() const { return m_psi->nCount; }

    int GetSegmentCount() const { return m_psi->nSegmentCount; }

    const wchar_t* GetName() { return m_pszSMName; }

private:
    void Resize(int segment) {
#if defined(L2_BUILD_L2SERVER)
        if (m_psi->nSegmentCount <= segment) {
            L2_GUARD(mLock);
            if (m_psi->nSegmentCount <= segment) {
                for (int current = m_psi->nSegmentCount; current <= segment; ++current) {
                    CreateMMF(current);
                }
                m_psi->nSegmentCount = segment + 1;
                m_nLocalSegmentCount = segment + 1;
            }
        }
#else
        if (m_nLocalSegmentCount <= segment) {
            L2_GUARD(mLock);
            if (m_nLocalSegmentCount <= segment) {
                for (int current = m_nLocalSegmentCount; current <= segment; ++current) {
                    OpenMMF(current);
                }
                m_nLocalSegmentCount = segment + 1;
            }
        }
#endif
    }

    void OpenMMF(int segment) {
        wchar_t segmentName[104]{};
        std::swprintf(segmentName, std::size(segmentName), L"%ls_%d", m_pszSMName, segment);

        const portable::FixedSharedMemoryBinding binding =
            portable::OpenFixedSharedMemory(segmentName, static_cast<std::size_t>(RecordsPerSegment) * sizeof(T));
        m_hfm[segment] = binding.owner;
        m_index[segment] = static_cast<T*>(binding.records);
    }

    void CreateMMF(int segment) {
        wchar_t segmentName[104]{};
        std::swprintf(segmentName, std::size(segmentName), L"%ls_%d", m_pszSMName, segment);

        constexpr std::size_t kSegmentBytes = static_cast<std::size_t>(RecordsPerSegment) * sizeof(T);
        const portable::FixedSharedMemoryBinding binding = portable::CreateFixedSharedMemory(segmentName, kSegmentBytes);
        m_hfm[segment] = binding.owner;
        m_index[segment] = static_cast<T*>(binding.records);
        // Every current L2Server CreateMMF instantiation at
        // 0x00a55b40..0x00a56104 clears the complete segment.
        std::memset(static_cast<void*>(m_index[segment]), 0, kSegmentBytes);
    }

    const wchar_t* m_pszSMName;
    void* m_hfm[MaximumSegments];
    T* m_index[MaximumSegments];
    int m_nLocalSegmentCount;
    SharedVectorSharedInfo* m_psi;
    void* m_si_hfm;
    CYieldLock mLock;
};


template <typename T>
class SharedType;

struct SharedEventInfo;
using SharedEventInfoVector = CSharedVector<SharedEventInfo, 1, 1, 1>;

struct PCCafeCoupon {
    int couponEventState = 0;
};

struct PCCafePointEvent {
    int m_PcCafePointEventState = 0;
};

struct GameEvent {
    bool eventDropTime = false; // +0x00
    int m_FirstDropTime = 0; // +0x04
    bool eventPCCafeBonusTime = false; // +0x08
    int m_PlayingUserAverage = 0; // +0x0c
    int m_DebugInfo[10]{}; // +0x10
    std::int32_t m_AgathionCount = 0; // +0x38, target long
    std::int32_t m_Goal = 0; // +0x3c, target long
};

// shared/shareinfo.cpp. Current L2NPC.exe obtains the sole record from
// CSharedVector<SharedEventInfo,1,1,1> at 0x00535e98.
struct SharedEventInfo {
    static SharedEventInfo* Inst();
    void Init();

    PCCafeCoupon m_PCCafeCoupon{}; // +0x00
    PCCafePointEvent m_PCCafePointEvent{}; // +0x04
    GameEvent m_gameEvent{}; // +0x08
};


struct SharedFishingInfo {
    static SharedFishingInfo* GetInfo();

    bool m_bState = false; // +0x00
    int m_RankingDBID[5]{}; // +0x04
};


struct SharedKamarokaInfo {
    static SharedKamarokaInfo* GetInfo();

    int m_nRound = 0; // +0x00
    int m_nState = 0; // +0x04
    bool m_bKamaCycle[7]{}; // +0x08
};


struct SharedCastleInfo;
using SharedCastleInfoVector = CSharedVector<SharedCastleInfo, 10, 1, 10>;

// shared/shareinfo.cpp. Current L2NPC.exe 0x00535f88 indexes
// CSharedVector<SharedCastleInfo,10,1,10> with a 0x10-byte stride.
struct SharedCastleInfo {
    static SharedCastleInfo* GetInfo(int residenceId);

    int m_nCurrentOwnerId = 0; // +0x00
    int m_nCurrentOwnerWorldId = 0; // +0x04
    int m_nPreviousOwnerId = 0; // +0x08
    int m_nPreviousOwnerWorldId = 0; // +0x0c
};


// shared/shareinfo.cpp. Current L2NPC.exe 0x00536660 maps one dword under
// the SharedRaidServerData name.
struct SharedRaidServerData {
    static SharedRaidServerData* GetInfo();

    int m_nRaidServerTimeDiff = 0;
};


struct SharedPlatform {
    int airportId = 0; // +0x00
    int platformId = 0; // +0x04
    bool occupied = false; // +0x08
};

// shared/shareinfo.cpp. Current L2NPC.exe 0x0053611c maps the exact
// L2SharedPlatformInfo record. The paired current L2Server creates the
// producer methods while L2NPC creates the consumer methods below.
struct SharedPlatformInfo {
    static constexpr std::uint32_t MAX_PLATFORM = 20;

    static SharedPlatformInfo* Inst();
    void Init();
    int AddPlatform(int airportId, int platformId);
    bool IsOccupied(int index);
    bool SetOccupied(int index, bool occupied);
    int GetPlatformIndex(int airportId, int platformId);

    SharedPlatform m_PlatformList[MAX_PLATFORM]{}; // +0x000
    std::uint32_t m_MaxIndex = 0; // +0x0f0
};


struct SharedDynamicQuestInfo;
using SharedDynamicQuestInfoVector = CSharedVector<SharedDynamicQuestInfo, 100, 1, 100>;

// shared/shareinfo.cpp. Current L2NPC.exe 0x00536020 indexes
// CSharedVector<SharedDynamicQuestInfo,100,1,100> with a 0x18-byte stride.
struct SharedDynamicQuestInfo {
    static SharedDynamicQuestInfo* GetInfo(int index);

    int m_DynamicQuestID = 0; // +0x00
    int m_CampaignID = 0; // +0x04
    double m_Progress = 0.0; // +0x08
    int m_Goal = 0; // +0x10
    int m_Point = 0; // +0x14
};


struct SharedCleftInfo;
using SharedCleftInfoVector = CSharedVector<SharedCleftInfo, 1, 1, 1>;

// shared/shareinfo.cpp. Current L2NPC.exe 0x005360c0 returns the sole record
// of CSharedVector<SharedCleftInfo,1,1,1>.
struct SharedCleftInfo {
    static SharedCleftInfo* Inst();
    void Init();

    int m_nCleftState = 0;
};


// shared/shareinfo.cpp. The target stores one 0x4000-byte record in
// CSharedVector<CSharedAgitDecoInfo,1,1,1>.
class CSharedAgitDecoInfo {
public:
    enum CSharedAgitDecoAgit_Enum {
        MAX_SHARED_AGIT_DECO_TYPE = 32,
        MAX_SHARED_AGIT_DECO_LEVEL = 32,
    };

    struct DayFee {
        int nDay = 0; // +0x00
        std::int64_t nFee = 0; // +0x08
    };

    CSharedAgitDecoInfo() = default;

    static CSharedAgitDecoInfo* Inst();
    void Init();

    // CNPC::Agit_GetDecoDay/Fee access this table directly in the
    // shipping image.
    std::array<std::array<DayFee, MAX_SHARED_AGIT_DECO_LEVEL>, MAX_SHARED_AGIT_DECO_TYPE> m_DayFee{};
};


// shared/shareinfo.cpp. The adjacent PDB contains the original first four
// fields (sizeof 0x10). The shipping executable's 0x1c GetInfo stride and
// GlobalObject access sites establish the three fields appended by the
// current revision.
struct SharedFieldCycleInfo {
    enum {
        ADD_POINT_FIELD_CYCLE = 0,
        SET_POINT_FIELD_CYCLE = 1,
        SET_STEP_FIELD_CYCLE = 2,
    };

    static SharedFieldCycleInfo* GetInfo(int cycleId);

    int cycleID = 0; // +0x00
    int nowPoint = 0; // +0x04
    int nowStep = 0; // +0x08
    std::int32_t stepChangedTime = 0; // +0x0c, target long
    std::int32_t tMissionStarted = 0; // +0x10, target long
    bool bIsMissionMode = false; // +0x14
    std::array<std::uint8_t, 3> missionModePadding{}; // +0x15
    int eMissionStatus = 0; // +0x18
};


enum ConditionInSeason : std::int32_t {
    CIS_ALIVE = 0,
    CIS_DIE_STEP_1 = 1,
    CIS_DIE_STEP_2 = 2,
    CIS_UNKNOWN = -1,
};

struct SharedPledgeRaidInfoData {
    SharedPledgeRaidInfoData() : nPledgeDbId_(0), szPledgeName_{}, nPoint_(0), nRank_(0), nTier_(0), bCanReward_(false) {}
    SharedPledgeRaidInfoData(
        int pledgeDatabaseId, const char16_t* pledgeName, std::int64_t point, int rank, int tier, bool canReward
    );

    int nPledgeDbId_; // +0x00
    char16_t szPledgeName_[25]; // +0x04
    std::int64_t nPoint_; // +0x38
    int nRank_; // +0x40
    int nTier_; // +0x44
    bool bCanReward_; // +0x48
};

// shared/shareinfo.cpp. Current L2NPC.exe 0x00536714 maps this exact
// SharedType record as L2SharedPledgeRaidInfo.
struct SharedPledgeRaidInfo {
    static constexpr int MAX_PREV_SEASON_PLEDGE_RAID = 100;

    static SharedPledgeRaidInfo* GetInfo();

    SharedPledgeRaidInfoData* GetPrevSeasonPledgeRaidInfo(int index);
    SharedPledgeRaidInfoData* GetPrevSeasonPledgeRaidInfoWithDbId(int pledgeDatabaseId);
    void AddPrevSeasonPledgeRaid(const SharedPledgeRaidInfoData& data);
    void Clear();

    int nPrevSeason_ = 0; // +0x00
    int nSeason_ = 0; // +0x04
    int nStep_ = 0; // +0x08
    std::int32_t tPrevSeasonStartTime_ = 0; // +0x0c
    std::int32_t tSeasonStartTime_ = 0; // +0x10
    std::int32_t tNextSeasonStartTime_ = 0; // +0x14
    ConditionInSeason eHuntNpcCondition_ = CIS_UNKNOWN; // +0x18
    int nTotalCount_ = 0; // +0x1c
    SharedPledgeRaidInfoData prevSeasonPledgeRaid_[MAX_PREV_SEASON_PLEDGE_RAID]{}; // +0x20
};


struct sRimRankInfo {
    int nRanking = 0; // +0x00
    int nCharId = 0; // +0x04
    int nType = 0; // +0x08
    int nLevel = 0; // +0x0c
    char16_t sName[25]{}; // +0x10
    int nPoint = 0; // +0x44
};

// The donor record contained 10 rankings. Current L2NPC.exe 0x0043a434
// accepts indices 0..24 and uses the same 0x48-byte row stride.
struct SharedRimPointInfo {
    static constexpr int RANKING_COUNT = 25;

    static SharedRimPointInfo* GetInfo();

    std::uint64_t nRankingCount = 0; // +0x000
    sRimRankInfo arrRankingList[RANKING_COUNT]{}; // +0x008
};


enum PGFieldStatus {
    PGFIELD_NONE = 0,
    PGFIELD_WAIT = 1,
    PGFIELD_MATCH = 2,
};

struct sPledgeGameFieldInfo {
    PGFieldStatus fieldStatus = PGFIELD_NONE; // +0x00
    int nInzoneTypeId = 0; // +0x04
    int nInzoneSId = 0; // +0x08
};

// shared/shareinfo.cpp. Current L2NPC.exe accesses the prize/name fields at
// +0x54..+0xc8. The matching VS2015 type record supplies the complete record,
// including the six field rows and the trailing season-player count.
struct SharedPledgeGameInfo {
    static SharedPledgeGameInfo* GetInfo();

    sPledgeGameFieldInfo arrFieldInfo[6]{}; // +0x00
    int m_EntryCount = 0; // +0x48
    int m_nSeason = 0; // +0x4c
    int m_nStep = 0; // +0x50
    int m_nTopRankerTotalPrize = 0; // +0x54
    int m_nPrevSeasonWinnerPledgeDbId = 0; // +0x58
    char16_t m_PrevSeasonWinnerPledgeName[25]{}; // +0x5c
    int m_nPrevSeasonWinnerCharacterDbId = 0; // +0x90
    char16_t m_PrevSeasonWinnerCharacterName[25]{}; // +0x94
    int m_nPrevSeasonWinnerGotItem = 0; // +0xc8
    int m_nSeasonPlayerCount = 0; // +0xcc
};


enum TimeAttackRecordFieldType {
    RF_ROUND_NUMBER = 0,
    RF_PART_TYPE = 1,
    RF_MEMBER_COUNT = 2,
    RF_MEMBER_NAME_LIST = 3,
    RF_POINT = 4,
    RF_RECORD_TIME = 5,
    RF_ELPASED_TIME = 6,
};

enum TimeAttackRecordType {
    TOP_OF_ROOM = 0,
    TOP_OF_TWILIGHT = 1,
    TOP_OF_DAWN = 2,
    TOP_OF_MAX = 3,
};

// shared/shareinfo.cpp. L2SharedTimeAttackInfo is a fixed
// CSharedArray<SharedTimeAttackInfo,15>: five rooms by three record types.
struct SharedTimeAttackInfo {
    static SharedTimeAttackInfo* GetInfo(int roomNo, int recordType);

    int GetRewardFlag(int characterDatabaseId) const;
    int SetRewardFlag(int characterDatabaseId, int rewardFlag);
    int IsMember(int characterDatabaseId) const;

    int mRoomNo = 0; // +0x000
    int mRecordType = 0; // +0x004
    int mSSQRound = 0; // +0x008
    int mSSQPart = 0; // +0x00c
    std::int64_t mPoint = 0; // +0x010
    int mRecordTime = 0; // +0x018
    int mElapsedTime = 0; // +0x01c
    int mMemberCount = 0; // +0x020
    std::int64_t mFee = 0; // +0x028
    char16_t mMemberNameList[9][25]{}; // +0x030
    int mMemberDBIDList[9]{}; // +0x1f4
    int mMemberRewardFlags[9]{}; // +0x218
};


struct SharedSSQInfo {
    static SharedSSQInfo* GetInfo();

    int mRoundNumber = 0; // +0x00
    SSQStatus::type mCurrentStatus = // +0x04
        SSQStatus::NONE;
    SSQPart::type mWinner = SSQPart::NONE; // +0x08
    SSQPart::type mPrevWinner = // +0x0c
        SSQPart::NONE;
    SSQPart::type mSealOwner[8]{}; // +0x10
    time_l2_t mEventStartTime = 0; // +0x30, target long
    time_l2_t mEventEndTime = 0; // +0x34
    time_l2_t mSealEffectTime = 0; // +0x38
    time_l2_t mSealEffectEndTime = 0; // +0x3c
    std::int64_t mTotalPoint[3]{}; // +0x40
    std::int64_t mCollectedPoint[3]{}; // +0x58
    std::int64_t mMainEventPoint[3]{}; // +0x70
    int mMemberCount[3]{}; // +0x88
    int mSealSelectionCount[3][8]{}; // +0x94
    int m_nCanDropGuard = 0; // +0xf4
};


struct SharedNoblessInfo;
using SharedNoblessInfoVector = CSharedVector<SharedNoblessInfo, 1024, 1, 1024>;

// shared/shareinfo.cpp. The current executable obtains these records from
// CSharedVector<SharedNoblessInfo,1024,1,1024> and addresses every field
// directly.
struct SharedNoblessInfo {
    static SharedNoblessInfoVector& Inst();
    static SharedNoblessInfo* GetInfo(int index);

    void Init();

    int nCharSid = 0; // +0x00
    int nCharDbId = 0; // +0x04
    int nNoblessType = 0; // +0x08
    // int nHeroType = 0,              // +0x0c
    int nHeroType = HERO_NONE; // +0x0c
    int nWinCount = 0; // +0x10
    int nPreviousPoint = 0; // +0x14
    int nOlympiadPoint = 0; // +0x18
    int nMatchCount = 0; // +0x1c
    int nOlympiadWinCount = 0; // +0x20
    int nOlympiadLoseCount = 0; // +0x24
    int nOlympiadTradePoint = 0; // +0x28
};


struct sOlympiadRankInfo {
    int nRanking = 0; // +0x00
    char16_t sName[25]{}; // +0x04
    int nPoint = 0; // +0x38
};

struct sOlympiadFieldInfo {
    OlympiadFieldStatus fieldStatus = FIELD_NONE; // +0x00
    int nPlayer1 = -1; // +0x04
    int nPlayer2 = -1; // +0x08
    GameRuleType gameRule = GRT_TEAM; // +0x0c
    int nInzoneTypeId = 0; // +0x10
    int nInzoneSId = 0; // +0x14
};

// shared/shareinfo.h/.cpp. The record offsets below are all read directly
// by the current L2NPC.exe. The matching VS2015 type record supplies the
// original field names and nested record types.
struct SharedOlympiadInfo {
    static constexpr int FIELD_COUNT = 850;
    static constexpr int HERO_CLASS_COUNT = 36;
    static constexpr int RANK_COUNT_PER_CLASS = 25;

    static SharedOlympiadInfo* GetInfo();

    std::uint64_t GetRankCount(ClassType classType) const;
    sOlympiadRankInfo* GetRankInfo(ClassType classType, int order);
    const sOlympiadRankInfo* GetRankInfo(ClassType classType, int order) const;
    int GetRankByName(ClassType classType, char16_t* name);
    bool ChangeRankName(ClassType classType, char16_t* oldName, char16_t* newName);
    void AddRankInfo(ClassType classType, int ranking, const SharedUtilsWideChar* name, int point);

    int m_EntryCount[3]{}; // +0x00000
    sOlympiadFieldInfo arrFieldInfo[FIELD_COUNT]{}; // +0x0000c
    std::uint64_t arrRankingCount[HERO_CLASS_COUNT]{}; // +0x04fc0
    sOlympiadRankInfo arrRankingList[HERO_CLASS_COUNT][RANK_COUNT_PER_CLASS]{}; // +0x050e0
    int m_nSeason = 0; // +0x123d0
    int m_nStep = 0; // +0x123d4
    int m_nSeasonPlayerCount = 0; // +0x123d8
    bool m_bHostConnected = false; // +0x123dc
    std::array<std::uint8_t, 3> m_bHostConnectedPadding{}; // +0x123dd
    int m_idLegendary = 0; // +0x123e0
};


namespace InstantAgitJobType {

enum Enum {
    APPLY = 0,
    ADD_DECO = 1,
    RETURN = 2,
    ENTER = 3,
    LEAVE = 4,
    EXPEL = 5,
    CHOOSE_OWNER = 6,
    LOAD = 7,
    LOAD_LOTTERY = 8,
    LOAD_AGIT = 9,
    LOAD_APPLICANT = 10,
    LOAD_DECO = 11,
    MANAGER_ENTER = 12,
    MAX = 13,
};

enum ApplyDate {
    DATE_YEAR = 0,
    DATE_MONTH = 1,
    DATE_DAY = 2,
    DATE_HOUR = 3,
    DATE_MINUTE = 4,
    DATE_COUNT = 5,
};

inline constexpr int RENTAL_PERIOD_BUFFER = 300;
inline constexpr int CHOICE_HOUR = 21;
inline constexpr int HOUR_TO_SECOND = 3600;
inline constexpr int DAY_TO_SECOND = 86400;
inline constexpr int INVALID_DATE = 0;

} // namespace InstantAgitJobType

// MSVC 2015's `tm` is nine consecutive 32-bit integers. The portable host's
// libc `tm` is not an ABI type, so shared mappings use this exact carrier.
struct SharedTm32 {
    int tm_sec = 0; // +0x00
    int tm_min = 0; // +0x04
    int tm_hour = 0; // +0x08
    int tm_mday = 0; // +0x0c
    int tm_mon = 0; // +0x10
    int tm_year = 0; // +0x14
    int tm_wday = 0; // +0x18
    int tm_yday = 0; // +0x1c
    int tm_isdst = 0; // +0x20
};


struct SharedInstantAgitIndex;
class SharedInstantAgitApplyData;
struct SharedInstantAgitDeco;

using SharedInstantAgitIndexInfo = SharedType<SharedInstantAgitIndex>;
using SharedInstantAgitApplyDataList = CSharedVector<SharedInstantAgitApplyData, 10, 1, 10>;
using SharedInstantAgitDecoDataList = CSharedVector<SharedInstantAgitDeco, 400, 1, 10>;

struct SharedInstantAgitIndex {
    volatile std::int32_t index; // +0x00, target volatile long
};


class SharedInstantAgitApplyData {
public:
    enum {
        MAX_COUNT = 100,
        COUNT_IN_SEGMENT = 10,
        MAX_SEGMENT_COUNT = 10,
        DEFAULT_SEGMENT = 1,
    };

    SharedInstantAgitApplyData() = default;

    static SharedInstantAgitApplyData* GetInfo(int instantAgitId);
    void Init(int instantAgitId, int maxInstantAgitCount, int cost);
    void SetLotteryDate(int firstApplyDate, int lastApplyDate);
    static std::int32_t MakeFirstApplyDate(std::int32_t lotteryDate, int applyDuration);
    static std::int32_t MakeLastApplyDate(std::int32_t lotteryDate);
    int GetFirstApplyDate(InstantAgitJobType::ApplyDate date);
    int GetLastApplyDate(InstantAgitJobType::ApplyDate date);

private:
    static SharedInstantAgitIndexInfo& GetIndexInfo();
    static SharedInstantAgitApplyDataList& Inst();

public:
    int instantAgitId = 0; // +0x00
    int applicantCount = 0; // +0x04
    int maxInstantAgitCount = 0; // +0x08
    std::uint32_t padding = 0; // +0x0c
    std::int64_t cost = 0; // +0x10
    SharedTm32 firstApplyDate{}; // +0x18, target tm
    SharedTm32 lastApplyDate{}; // +0x3c, target tm
};


struct SharedInstantAgitDeco {
    struct InstantAgitDecoSlot {
        InstantAgitDecoSlot() = default;
        void Reset();

        int agitDecoId = 0; // +0x00
        int agitDecoLevel = 0; // +0x04
        double agitDecoPower = 0; // +0x08
    };

    struct InstantAgitDecoDecoNpc {
        int nCID = 0; // +0x00
        int nNpcSID = 0; // +0x04
        std::int32_t tExpire = 0; // +0x08, target long
    };

    SharedInstantAgitDeco() = default;

    void Init(int pledgeDatabaseId);

    int pledgeDBId = 0; // +0x000
    std::uint32_t padding = 0; // +0x004
    std::array<InstantAgitDecoSlot, 32> decoInfo{}; // +0x008
    AgitDecoEffect decoEffect; // +0x208

    // The adjacent 2015 PDB ends at +0x220. Current L2NPC.exe accesses
    // SharedInstantAgitDeco with a 0x260 stride. The intervening revision
    // added the five exact 0x0c decoration-NPC records below.
    std::array<InstantAgitDecoDecoNpc, 5> aDecoNpc{}; // +0x220
};


// Original static-only façade (sizeof 1), not the shared-vector transport.
class SharedInstantAgitDecoData {
public:
    enum {
        MAX_COUNT = 4000,
        COUNT_IN_SEGMENT = 400,
        MAX_SEGMENT_COUNT = 10,
        DEFAULT_SEGMENT = 1,
    };

    static SharedInstantAgitIndexInfo& GetIndexInfo();
    static SharedInstantAgitDecoDataList& Inst();
    static SharedInstantAgitDeco* GetInfo(int pledgeDatabaseId);
    static SharedInstantAgitDeco* Create();

private:
    SharedInstantAgitDecoData() = default;
};


namespace Tm2ApplyDate {

int Conv(SharedTm32& value, InstantAgitJobType::ApplyDate date);
bool CanApplyNow(SharedTm32& first, SharedTm32& last);

} // namespace Tm2ApplyDate
