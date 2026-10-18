#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedType.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <limits>

namespace {

std::tm ToHostTm(const SharedTm32& value) {
    std::tm result{};
    result.tm_sec = value.tm_sec;
    result.tm_min = value.tm_min;
    result.tm_hour = value.tm_hour;
    result.tm_mday = value.tm_mday;
    result.tm_mon = value.tm_mon;
    result.tm_year = value.tm_year;
    result.tm_wday = value.tm_wday;
    result.tm_yday = value.tm_yday;
    result.tm_isdst = value.tm_isdst;
    return result;
}

void FromHostTm(const std::tm& source, SharedTm32& destination) {
    destination.tm_sec = source.tm_sec;
    destination.tm_min = source.tm_min;
    destination.tm_hour = source.tm_hour;
    destination.tm_mday = source.tm_mday;
    destination.tm_mon = source.tm_mon;
    destination.tm_year = source.tm_year;
    destination.tm_wday = source.tm_wday;
    destination.tm_yday = source.tm_yday;
    destination.tm_isdst = source.tm_isdst;
}

bool CheckedTime32Add(std::int32_t left, std::int32_t right, std::int32_t& result) {
    const std::int64_t wide = static_cast<std::int64_t>(left) + static_cast<std::int64_t>(right);
    if (wide < std::numeric_limits<std::int32_t>::min() || wide > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    result = static_cast<std::int32_t>(wide);
    return true;
}

bool CheckedTime32Multiply(std::int32_t value, std::int32_t factor, std::int32_t& result) {
    const std::int64_t wide = static_cast<std::int64_t>(value) * static_cast<std::int64_t>(factor);
    if (wide < std::numeric_limits<std::int32_t>::min() || wide > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    result = static_cast<std::int32_t>(wide);
    return true;
}

bool ValidateTime32Arithmetic(const SharedTm32& value, std::int32_t normalizedYear, std::int32_t normalizedMonth) {
    // Current L2NPC.exe CRT table at 0x006bc218. These values are one less
    // than the cumulative day count because tm_mday is one-based.
    static constexpr std::int32_t kMonthOffset[12] = {-1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333};

    std::int32_t day = normalizedYear * 365 - 0x63df + ((normalizedYear - 1) >> 2) + kMonthOffset[normalizedMonth];
    if ((normalizedYear & 3) == 0 && normalizedMonth > 1) {
        ++day;
    }
    if (!CheckedTime32Add(day, value.tm_mday, day) || !CheckedTime32Multiply(day, 24, day) ||
        !CheckedTime32Add(day, value.tm_hour, day) || !CheckedTime32Multiply(day, 60, day) ||
        !CheckedTime32Add(day, value.tm_min, day) || !CheckedTime32Multiply(day, 60, day) ||
        !CheckedTime32Add(day, value.tm_sec, day)) {
        return false;
    }
    return true;
}

std::int32_t Mktime32(SharedTm32& value) {
    std::tm host = ToHostTm(value);
#if defined(_MSC_VER)
    const __time32_t result = ::_mktime32(&host);
    FromHostTm(host, value);
    return static_cast<std::int32_t>(result);
#else
    // Current L2NPC.exe 0x00556c64/0x00556f24 is the VS2010 32-bit CRT
    // implementation. Preserve its validation and the one mutation it can
    // make on a failed call: an out-of-range month is normalized in-place
    // before the adjusted year is checked again.
    std::int32_t normalizedYear = value.tm_year;
    if (static_cast<std::uint32_t>(normalizedYear) - 69u >= 0x409u) {
        return -1;
    }

    std::int32_t normalizedMonth = value.tm_mon;
    if (normalizedMonth < 0 || normalizedMonth > 11) {
        normalizedYear += normalizedMonth / 12;
        normalizedMonth %= 12;
        value.tm_mon = normalizedMonth;
        if (normalizedMonth < 0) {
            --normalizedYear;
            normalizedMonth += 12;
            value.tm_mon = normalizedMonth;
        }
        if (static_cast<std::uint32_t>(normalizedYear) - 69u >= 0x409u) {
            return -1;
        }
    }

    if (!ValidateTime32Arithmetic(value, normalizedYear, normalizedMonth)) {
        return -1;
    }

    host.tm_year = normalizedYear;
    host.tm_mon = normalizedMonth;
    const std::time_t result = std::mktime(&host);
    if (result < static_cast<std::time_t>(0) ||
        result > static_cast<std::time_t>(std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    FromHostTm(host, value);
    return static_cast<std::int32_t>(result);
#endif
}

char16_t FoldUtf16Case(char16_t value) {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

int CompareUtf16CaseInsensitive(const char16_t* left, const char16_t* right) {
    if (left == nullptr || right == nullptr) {
        errno = EINVAL;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return std::numeric_limits<int>::max();
    }
    for (;;) {
        const char16_t foldedLeft = FoldUtf16Case(*left++);
        const char16_t foldedRight = FoldUtf16Case(*right++);
        if (foldedLeft != foldedRight || foldedLeft == u'\0') {
            return static_cast<int>(foldedLeft) - static_cast<int>(foldedRight);
        }
    }
}

} // namespace

NpcSpawnInfo* NpcSpawnInfo::Inst() {
    // Current L2NPC.exe 0x00535b14.
    static NpcSpawnInfoVector info(L"NpcSpawnInfo");
    return &info[0];
}

void NpcSpawnInfo::Init() {
    // Current L2NPC.exe 0x00534404 only announces the producer-owned
    // counters. Current L2Server.exe 0x00a55724 is the producer and clears
    // every counter except Server_EnterWorldQueueSize (+0x10), then restores
    // the -1 preset sentinel. Keep that product split literal.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[NpcSpawnInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
#if defined(L2_BUILD_L2SERVER)
    Server_Create = 0;
    Server_Delete = 0;
    Server_EnterWorld = 0;
    Server_LeaveWorld = 0;
    Server_CreatePrivate = 0;
    Server_DeletePrivate = 0;
    Npc_Create = 0;
    Npc_Delete = 0;
    Npc_EnterWorld = 0;
    NpcMaker_RequestCreate = 0;
    NpcMaker_OnCreate = 0;
    NpcMaker_OnDelete = 0;
    NpcMakerWorld_RequestCreate = 0;
    NpcMakerWorld_OnCreate = 0;
    NpcMakerWorld_OnDelete = 0;
    Npc_CreatePrivate = 0;
    Npc_OnPrivateDie = 0;
    Npc_RandPresetValue = -1;
#endif
}

SharedEventInfo* SharedEventInfo::Inst() {
    // Current L2NPC.exe 0x00535e98.
    static SharedEventInfoVector info(L"SharedEventInfo");
    return &info[0];
}

void SharedEventInfo::Init() {
    // Current L2NPC.exe 0x0053444c.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedEventInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
}

SharedFishingInfo* SharedFishingInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005364f8.
    static SharedType<SharedFishingInfo> info(L"L2SharedFishingInfo");
    return info;
}

SharedKamarokaInfo* SharedKamarokaInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005365ac.
    static SharedType<SharedKamarokaInfo> info(L"L2SharedKamarokaInfo");
    return info;
}

SharedCastleInfo* SharedCastleInfo::GetInfo(int residenceId) {
    // Current L2NPC.exe 0x00535f88.
    static SharedCastleInfoVector info(L"SharedCastleInfo");
    return &info[residenceId];
}

SharedRaidServerData* SharedRaidServerData::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00536660.
    static SharedType<SharedRaidServerData> info(L"SharedRaidServerData");
    return info;
}

SharedPlatformInfo* SharedPlatformInfo::Inst() {
    // Current L2NPC.exe 0x0053611c.
    static SharedType<SharedPlatformInfo> info(L"L2SharedPlatformInfo");
    return info;
}

void SharedPlatformInfo::Init() {
    // Current L2NPC.exe 0x00534464.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedPlatformInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
}

bool SharedPlatformInfo::IsOccupied(int index) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00534c50.
    const std::uint32_t maximum = m_MaxIndex;
    if (maximum >= MAX_PLATFORM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what the hell. [%d][%d]",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x1a3,
        //     static_cast<int>(maximum),
        //     static_cast<int>(MAX_PLATFORM))
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Cannot check occupancy at index %d: shared maximum %u reaches or exceeds capacity %u.
        //     returning false.", index, static_cast<unsigned>(maximum), static_cast<unsigned>(MAX_PLATFORM))
        L2_LOG_ERROR(
            Log,
            "[airship platforms] Cannot check occupancy at index %d: shared maximum %u reaches or exceeds capacity %u; "
            "returning false.",
            index, static_cast<unsigned>(maximum), static_cast<unsigned>(MAX_PLATFORM)
        );
        return false;
    }
    if (index < 0 || maximum < static_cast<std::uint32_t>(index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid index[%d] of shared platform.",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x1ae,
        //     index)
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Occupancy index %d is outside the accepted range 0..%u. Treating it as occupied.",
        //     index, static_cast<unsigned>(maximum))
        L2_LOG_ERROR(
            Log, "[airship platforms] Occupancy index %d is outside the accepted range 0..%u; treating it as occupied.",
            index, static_cast<unsigned>(maximum)
        );
        return true;
    }
    return m_PlatformList[index].occupied;
}

int SharedPlatformInfo::GetPlatformIndex(int airportId, int platformId) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00534d74.
    if (m_MaxIndex >= MAX_PLATFORM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what the hell. [%d][%d]",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x1bd,
        //     static_cast<int>(m_MaxIndex),
        //     static_cast<int>(MAX_PLATFORM))
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Cannot find airport %d, platform %d: shared maximum %u reaches or exceeds capacity %u.
        //     returning -1.", airportId, platformId, static_cast<unsigned>(m_MaxIndex),
        //     static_cast<unsigned>(MAX_PLATFORM))
        L2_LOG_ERROR(
            Log,
            "[airship platforms] Cannot find airport %d, platform %d: shared maximum %u reaches or exceeds capacity %u; "
            "returning -1.",
            airportId, platformId, static_cast<unsigned>(m_MaxIndex), static_cast<unsigned>(MAX_PLATFORM)
        );
        return -1;
    }
    for (std::uint32_t index = 0; index < m_MaxIndex; ++index) {
        const SharedPlatform& platform = m_PlatformList[index];
        if (platform.airportId == airportId && platform.platformId == platformId) {
            return static_cast<int>(index);
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid airportId and platformId[%d, %d]",
    //     L"..\\Shared\\ShareInfo.cpp",
    //     0x1cc,
    //     airportId,
    //     platformId)
    // Log.Add(LOG_ERROR,
    //     "[airship platforms] Airport %d, platform %d has no registered entry. Returning -1.",
    //     airportId, platformId)
    L2_LOG_ERROR(
        Log, "[airship platforms] Airport %d, platform %d has no registered entry; returning -1.", airportId, platformId
    );
    return -1;
}

SharedDynamicQuestInfo* SharedDynamicQuestInfo::GetInfo(int index) {
    // Current L2NPC.exe 0x00536020.
    static SharedDynamicQuestInfoVector info(L"SharedDynamicQuestInfo");
    return &info[index];
}

SharedCleftInfo* SharedCleftInfo::Inst() {
    // Current L2NPC.exe 0x005360c0.
    static SharedCleftInfoVector info(L"SharedCleftInfo");
    return &info[0];
}

void SharedCleftInfo::Init() {
    // Current L2NPC.exe 0x00534434.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedCleftInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
}

SharedPledgeRaidInfo* SharedPledgeRaidInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00536714.
    static SharedType<SharedPledgeRaidInfo> info(L"L2SharedPledgeRaidInfo");
    return info;
}

SharedPledgeRaidInfoData* SharedPledgeRaidInfo::GetPrevSeasonPledgeRaidInfo(int index) {
    // This method is not compiled by current L2NPC.exe. The matching shared
    // source body and guard are kept by nasc64_2015 at 0x007c3de0.
    L2_GUARD_FUNCTION();
    if (index < 0 || index >= nTotalCount_) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Out of Range : nIndex[%d]",
        //     index)
        // Log.Add(LOG_ERROR,
        //     "[clan raids] Previous-season entry index %d is outside the published count. Returning no entry.",
        //     index)
        L2_LOG_ERROR(
            Log, "[clan raids] Previous-season entry index %d is outside the published count; returning no entry.", index
        );
        return nullptr;
    }
    return &prevSeasonPledgeRaid_[index];
}

SharedPledgeRaidInfoData* SharedPledgeRaidInfo::GetPrevSeasonPledgeRaidInfoWithDbId(int pledgeDatabaseId) {
    // Current L2NPC.exe 0x005351bc performs a first-match linear scan.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < runtime::raw_atomic::LoadI32Relaxed(&nTotalCount_); ++index) {
        SharedPledgeRaidInfoData& entry = prevSeasonPledgeRaid_[index];
        if (entry.nPledgeDbId_ == pledgeDatabaseId) {
            return &entry;
        }
    }
    return nullptr;
}

SharedRimPointInfo* SharedRimPointInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00535c80.
    static SharedType<SharedRimPointInfo> info(L"L2SharedRimPointInfo");
    return info;
}

CSharedAgitDecoInfo* CSharedAgitDecoInfo::Inst() {
    // Current L2NPC.exe 0x00535c24.
    static CSharedVector<CSharedAgitDecoInfo, 1, 1, 1> g_SharedAgitDecoInfo(L"SharedAgitDecoInfo");
    return &g_SharedAgitDecoInfo[0];
}

SharedFieldCycleInfo* SharedFieldCycleInfo::GetInfo(int cycleId) {
    // Current L2NPC.exe 0x00535ef4:
    // CSharedVector<SharedFieldCycleInfo,16,1,16>::operator[].
    static CSharedVector<SharedFieldCycleInfo, 16, 1, 16> info(L"SharedFieldCycleInfo");
    return &info[cycleId];
}

SharedPledgeGameInfo* SharedPledgeGameInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00536444: the one-record shared-memory singleton
    // named L2SharedPledgeGameInfo.
    static SharedType<SharedPledgeGameInfo> info(L"L2SharedPledgeGameInfo");
    return info;
}

SharedTimeAttackInfo* SharedTimeAttackInfo::GetInfo(int roomNo, int recordType) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x005356a8.
    static CSharedArray<SharedTimeAttackInfo, 15> information(L"L2SharedTimeAttackInfo");
    if (roomNo < 1 || roomNo > 5 || recordType < 0 || recordType > 2) {
        return nullptr;
    }
    return &information[(roomNo - 1) * 3 + recordType];
}

SharedSSQInfo* SharedSSQInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00535b70.
    static SharedType<SharedSSQInfo> info(L"L2SharedSSQINFO");
    return info;
}

SharedNoblessInfoVector& SharedNoblessInfo::Inst() {
    // Current L2NPC.exe 0x00535d34.
    static SharedNoblessInfoVector info(L"SharedNoblessInfo");
    return info;
}

SharedNoblessInfo* SharedNoblessInfo::GetInfo(int index) {
    // Current L2NPC.exe 0x00535d84.
    return &Inst()[index];
}

void SharedNoblessInfo::Init() {
    // Current L2NPC startup logs the vector name directly. The original
    // record method remains visible in nasc64_2015 at 0x007c4660.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedNoblessInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
}

SharedOlympiadInfo* SharedOlympiadInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00535de4.
    static SharedType<SharedOlympiadInfo> info(L"L2SharedOlympiadInfo");
    return info;
}

std::uint64_t SharedOlympiadInfo::GetRankCount(ClassType classType) const {
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    return classIndex == -1 ? 0 : arrRankingCount[classIndex];
}

sOlympiadRankInfo* SharedOlympiadInfo::GetRankInfo(ClassType classType, int order) {
    return const_cast<sOlympiadRankInfo*>(static_cast<const SharedOlympiadInfo*>(this)->GetRankInfo(classType, order));
}

const sOlympiadRankInfo* SharedOlympiadInfo::GetRankInfo(ClassType classType, int order) const {
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (classIndex == -1 || static_cast<std::uint64_t>(static_cast<std::int64_t>(order)) >= arrRankingCount[classIndex] ||
        order < 0 || order >= RANK_COUNT_PER_CLASS) {
        return nullptr;
    }
    return &arrRankingList[classIndex][order];
}

int SharedOlympiadInfo::GetRankByName(ClassType classType, char16_t* name) {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00534ae4.
    const int listClassIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (listClassIndex == -1) {
        return 0;
    }
    const int countClassIndex = OlympiadDefine::GetHeroClassIndex(classType);
    const std::uint64_t count = countClassIndex == -1 ? 0 : arrRankingCount[countClassIndex];
    const sOlympiadRankInfo* const rankings = &arrRankingList[listClassIndex][0];
    for (int order = 0; static_cast<std::uint64_t>(static_cast<std::int64_t>(order)) < count; ++order) {
        sOlympiadRankInfo rank;
        std::memcpy(&rank, &rankings[order], sizeof(rank));
        if (CompareUtf16CaseInsensitive(rank.sName, name) == 0) {
            return rank.nRanking;
        }
    }
    return 0;
}

int SharedTimeAttackInfo::GetRewardFlag(int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00534960.
    const volatile int& observedCount = mMemberCount;
    for (int index = 0; index < observedCount; ++index) {
        if (mMemberDBIDList[index] == characterDatabaseId) {
            return mMemberRewardFlags[index];
        }
    }
    return 0;
}

int SharedTimeAttackInfo::IsMember(int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00534a24.
    const volatile int& observedCount = mMemberCount;
    for (int index = 0; index < observedCount; ++index) {
        if (mMemberDBIDList[index] == characterDatabaseId) {
            return 1;
        }
    }
    return 0;
}

void CSharedAgitDecoInfo::Init() {
    // Current target 0x0053441c: Init has no data mutation.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[CSharedAgitDecoInfo] Init")
    // Noise: this consumer Init hook neither resets shared data nor validates producer readiness.
}

SharedInstantAgitIndexInfo& SharedInstantAgitApplyData::GetIndexInfo() {
    // Current target 0x00536160.
    static SharedInstantAgitIndexInfo info(L"SharedInstantAgitApplyDataIndexInfo");
    return info;
}

SharedInstantAgitApplyDataList& SharedInstantAgitApplyData::Inst() {
    // Current target 0x005361a4.
    static SharedInstantAgitApplyDataList i(L"SharedInstantAgitApplyData");
    return i;
}

SharedInstantAgitApplyData* SharedInstantAgitApplyData::GetInfo(int instantAgitId) {
    // Current target 0x005361f4. IndexInfo::index is an exclusive count.
    SharedInstantAgitApplyDataList& records = Inst();
    for (int current = 0;; ++current) {
        SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
        if (indexInfo->index <= current) {
            return nullptr;
        }
        if (records[current].instantAgitId == instantAgitId) {
            // The target calls CSharedVector::operator[] again when
            // producing the successful return value.
            return &records[current];
        }
    }
}

int SharedInstantAgitApplyData::GetFirstApplyDate(InstantAgitJobType::ApplyDate date) {
    L2_GUARD_FUNCTION();
    return Tm2ApplyDate::CanApplyNow(firstApplyDate, lastApplyDate) ? Tm2ApplyDate::Conv(firstApplyDate, date)
                                                                    : InstantAgitJobType::INVALID_DATE;
}

int SharedInstantAgitApplyData::GetLastApplyDate(InstantAgitJobType::ApplyDate date) {
    L2_GUARD_FUNCTION();
    return Tm2ApplyDate::CanApplyNow(firstApplyDate, lastApplyDate) ? Tm2ApplyDate::Conv(lastApplyDate, date)
                                                                    : InstantAgitJobType::INVALID_DATE;
}

void SharedInstantAgitDeco::InstantAgitDecoSlot::Reset() {
    agitDecoId = 0;
    agitDecoLevel = 0;
    agitDecoPower = 0.0;
}

void SharedInstantAgitDeco::Init(const int pledgeDatabaseId) {
    // Current L2Server target 0x00a55838. The record is reset in place.
    // shared-vector storage owns its lifetime.
    pledgeDBId = pledgeDatabaseId;
    for (InstantAgitDecoSlot& slot : decoInfo) {
        slot.Reset();
    }
    decoEffect.Init();
    for (InstantAgitDecoDecoNpc& npc : aDecoNpc) {
        npc.nCID = 0;
        npc.nNpcSID = 0;
        npc.tExpire = 0;
    }
}

SharedInstantAgitIndexInfo& SharedInstantAgitDecoData::GetIndexInfo() {
    // Current target 0x005362d0.
    static SharedInstantAgitIndexInfo info(L"SharedInstantAgitDecoDataIndexInfo");
    return info;
}

SharedInstantAgitDecoDataList& SharedInstantAgitDecoData::Inst() {
    // Current target 0x00536314.
    static SharedInstantAgitDecoDataList i(L"SharedInstantAgitDecoDataList");
    return i;
}

SharedInstantAgitDeco* SharedInstantAgitDecoData::GetInfo(int pledgeDatabaseId) {
    // Current target 0x00536364. IndexInfo::index is an inclusive maximum.
    SharedInstantAgitDecoDataList& records = Inst();
    for (int current = 0;; ++current) {
        SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
        if (indexInfo->index < current) {
            return nullptr;
        }
        if (records[current].pledgeDBId == pledgeDatabaseId) {
            // The successful target path indexes the vector a second time.
            return &records[current];
        }
    }
}

SharedInstantAgitDeco* SharedInstantAgitDecoData::Create() {
    // Current L2Server target 0x00a59a18. The shared index is an inclusive
    // maximum for this table. Index zero remains the sentinel record.
    SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
    const std::int32_t previous = runtime::raw_atomic::AddI32(&indexInfo->index, 1) - 1;
    SharedInstantAgitDecoDataList& records = Inst();
    return &records[previous + 1];
}

int Tm2ApplyDate::Conv(SharedTm32& value, InstantAgitJobType::ApplyDate date) {
    L2_GUARD_FUNCTION();
    switch (date) {
        case InstantAgitJobType::DATE_YEAR:
            return value.tm_year + 1900;
        case InstantAgitJobType::DATE_MONTH:
            return value.tm_mon + 1;
        case InstantAgitJobType::DATE_DAY:
            return value.tm_mday;
        case InstantAgitJobType::DATE_HOUR:
            return value.tm_hour;
        case InstantAgitJobType::DATE_MINUTE:
            return value.tm_min;
        default:
            return -1;
    }
}

bool Tm2ApplyDate::CanApplyNow(SharedTm32& first, SharedTm32& last) {
    L2_GUARD_FUNCTION();
    // Current target 0x00534f84 calls _time32 first, then _mktime32 on both
    // references. mktime normalization is therefore observable in the
    // live shared records and is kept here.
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t firstTime = Mktime32(first);
    const std::int32_t lastTime = Mktime32(last);
    return firstTime <= now && now <= lastTime;
}
