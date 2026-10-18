#include "Shared/src/SharedType.h"

#include "Shared/src/ShareInfo.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>

namespace {

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

void CopyLocalTime32(std::int32_t epochSeconds, SharedTm32& destination) {
    const std::tm* const local = runtime::crt::LocalTime32(epochSeconds);
    if (local == nullptr) {
        // The current body passes the null result straight to memcpy. Use the
        // main portable access-violation boundary so a host
        // optimizer cannot turn that target fault into another timestamp.
        CrashImpl();
        std::abort();
    }
    SharedTm32 carrier;
    FromHostTm(*local, carrier);
    std::memcpy(&destination, &carrier, sizeof(destination));
}

void CopyUtf16Secure25(char16_t (&destination)[25], const char16_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        errno = EINVAL;
        myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        return;
    }
    for (std::size_t index = 0; index < 25; ++index) {
        const char16_t value = source[index];
        destination[index] = value;
        if (value == u'\0') {
            return;
        }
    }

    // The current VS2010 calls are wcsncpy_s(..., 25, ..., 25) and
    // wcscpy_s(..., 25, ...). At this exact boundary both clear the first
    // code unit, set ERANGE and reach _invalid_parameter_noinfo.
    destination[0] = u'\0';
    errno = ERANGE;
    myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
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

int GetSharedInfoVersion() {
    // Current L2Server.exe 0x00a55798. Both locals are left
    // uninitialized before the guaranteed-success fixed-literal parse.
    char temporary[80];
    int version;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #75 $", "%s #%d", temporary, static_cast<unsigned>(sizeof(temporary)), &version);
#else
    std::sscanf("$Revision: #75 $", "%79s #%d", temporary, &version);
#endif
    return version;
}

SharedNoblessInfoVector& SharedNoblessInfo::Inst() {
    // Current L2Server.exe 0x00a59578.
    static SharedNoblessInfoVector info(L"SharedNoblessInfo");
    return info;
}

SharedNoblessInfo* SharedNoblessInfo::GetInfo(int index) {
    // Current L2Server.exe 0x00a595c8.
    return &Inst()[index];
}

NpcSpawnInfo* NpcSpawnInfo::Inst() {
    // Current L2Server.exe 0x00a58fa4.
    static NpcSpawnInfoVector info(L"NpcSpawnInfo");
    return &info[0];
}

void NpcSpawnInfo::Init() {
    // Current L2Server.exe 0x00a55724. The queue-size field at +0x10 is
    // kept while every producer counter is reset.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[NpcSpawnInfo] Init")
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Resetting shared spawn counters, the enter-world queue count is retained.")
    L2_LOG_INFO(Log, "[NPC statistics] Resetting shared spawn counters; the enter-world queue count is retained.");
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
}

void ReportShareInfo() {
    // Current L2Server.exe 0x00a58ff8, reached from WndProc command 0x8011.
    // Keep the repeated Inst calls and load order: the counters reside in
    // shared memory and the current body observes them independently.
    L2_GUARD_FUNCTION();

    // Log.Add(LOG_NORMAL, L"----- Create -----")
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Reading shared spawn counters independently, cross-process differences may be temporary.")
    L2_LOG_INFO(
        Log, "[NPC statistics] Reading shared spawn counters independently; cross-process differences may be temporary."
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Server Create(g_ObjectDB.CreateNPC)    %d",
    //     NpcSpawnInfo::Inst()->Server_Create)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC creations in L2Server: %d.",
    //     NpcSpawnInfo::Inst()->Server_Create)
    L2_LOG_INFO(Log, "[NPC statistics] NPC creations in L2Server: %d.", NpcSpawnInfo::Inst()->Server_Create);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer Create(new CNPC)             %d",
    //     NpcSpawnInfo::Inst()->Npc_Create)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC creations in L2NPC: %d.",
    //     NpcSpawnInfo::Inst()->Npc_Create)
    L2_LOG_INFO(Log, "[NPC statistics] NPC creations in L2NPC: %d.", NpcSpawnInfo::Inst()->Npc_Create);

    // Log.Add(LOG_NORMAL, L"----- EnterWorld -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Server EnterWorld                      %d",
    //     NpcSpawnInfo::Inst()->Server_EnterWorld)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC world entries in L2Server: %d.",
    //     NpcSpawnInfo::Inst()->Server_EnterWorld)
    L2_LOG_INFO(Log, "[NPC statistics] NPC world entries in L2Server: %d.", NpcSpawnInfo::Inst()->Server_EnterWorld);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer EnterWorld(CNPC::EnterWorld) %d",
    //     NpcSpawnInfo::Inst()->Npc_EnterWorld)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC world entries in L2NPC: %d.",
    //     NpcSpawnInfo::Inst()->Npc_EnterWorld)
    L2_LOG_INFO(Log, "[NPC statistics] NPC world entries in L2NPC: %d.", NpcSpawnInfo::Inst()->Npc_EnterWorld);

    // Log.Add(LOG_NORMAL, L"----- delete -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Server Delete(CNPC::Delete)            %d",
    //     NpcSpawnInfo::Inst()->Server_Delete)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC deletions in L2Server: %d.",
    //     NpcSpawnInfo::Inst()->Server_Delete)
    L2_LOG_INFO(Log, "[NPC statistics] NPC deletions in L2Server: %d.", NpcSpawnInfo::Inst()->Server_Delete);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Delete(CNPC::Delete)                   %d",
    //     NpcSpawnInfo::Inst()->Npc_Delete)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPC deletions in L2NPC: %d.",
    //     NpcSpawnInfo::Inst()->Npc_Delete)
    L2_LOG_INFO(Log, "[NPC statistics] NPC deletions in L2NPC: %d.", NpcSpawnInfo::Inst()->Npc_Delete);

    // Log.Add(LOG_NORMAL, L"----- create - delete -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    NpcSpawnInfo* const serverCounters = NpcSpawnInfo::Inst();
    const std::int32_t serverDelete = serverCounters->Server_Delete;
    const std::int32_t serverCreate = serverCounters->Server_Create;
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Server Create(1) - Delete(2)           %d",
    //     runtime::SubtractInt32Wrapping(
    //         serverCreate,
    //         serverDelete))
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] L2Server creations minus deletions: %d.",
    //     runtime::SubtractInt32Wrapping(
    //     serverCreate,
    //     serverDelete))
    L2_LOG_INFO(
        Log, "[NPC statistics] L2Server creations minus deletions: %d.",
        runtime::SubtractInt32Wrapping(serverCreate, serverDelete)
    );
    NpcSpawnInfo* const npcCounters = NpcSpawnInfo::Inst();
    const std::int32_t npcDelete = npcCounters->Npc_Delete;
    const std::int32_t npcCreate = npcCounters->Npc_Create;
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer Create(1) - Delete(2)        %d",
    //     runtime::SubtractInt32Wrapping(
    //         npcCreate,
    //         npcDelete))
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] L2NPC creations minus deletions: %d.",
    //     runtime::SubtractInt32Wrapping(
    //     npcCreate,
    //     npcDelete))
    L2_LOG_INFO(
        Log, "[NPC statistics] L2NPC creations minus deletions: %d.", runtime::SubtractInt32Wrapping(npcCreate, npcDelete)
    );

    // Log.Add(LOG_NORMAL, L"----- maker -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer Maker Request Create(1)      %d",
    //     NpcSpawnInfo::Inst()->NpcMaker_RequestCreate)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Maker creation requests: %d.",
    //     NpcSpawnInfo::Inst()->NpcMaker_RequestCreate)
    L2_LOG_INFO(Log, "[NPC statistics] Maker creation requests: %d.", NpcSpawnInfo::Inst()->NpcMaker_RequestCreate);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer CreatePrivate(2)             %d",
    //     NpcSpawnInfo::Inst()->Npc_CreatePrivate)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Private NPC creations in L2NPC: %d.",
    //     NpcSpawnInfo::Inst()->Npc_CreatePrivate)
    L2_LOG_INFO(Log, "[NPC statistics] Private NPC creations in L2NPC: %d.", NpcSpawnInfo::Inst()->Npc_CreatePrivate);

    const std::int32_t npcCreatePrivate = NpcSpawnInfo::Inst()->Npc_CreatePrivate;
    const std::int32_t serverCreatePrivate = NpcSpawnInfo::Inst()->Server_CreatePrivate;
    if (npcCreatePrivate == serverCreatePrivate) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Server CreatePrivate(packet)(=2)       %d",
        //     NpcSpawnInfo::Inst()->Server_CreatePrivate)
        // Log.Add(LOG_NORMAL,
        //     "[NPC statistics] Private NPC creations in L2Server: %d. The preceding comparison matched L2NPC.",
        //     NpcSpawnInfo::Inst()->Server_CreatePrivate)
        L2_LOG_INFO(
            Log, "[NPC statistics] Private NPC creations in L2Server: %d; the preceding comparison matched L2NPC.",
            NpcSpawnInfo::Inst()->Server_CreatePrivate
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Server CreatePrivate(packet)(=2)       %d",
        //     NpcSpawnInfo::Inst()->Server_CreatePrivate)
        // Log.Add(LOG_WARN,
        //     "[NPC statistics] Private creation counters differed: compared L2Server %d with L2NPC %d. Latest L2Server
        //     value %d. Counters are read independently.", serverCreatePrivate, npcCreatePrivate,
        //     NpcSpawnInfo::Inst()->Server_CreatePrivate)
        L2_LOG_WARN(
            Log,
            "[NPC statistics] Private creation counters differed: compared L2Server %d with L2NPC %d; latest L2Server value "
            "%d. Counters are read independently.",
            serverCreatePrivate, npcCreatePrivate, NpcSpawnInfo::Inst()->Server_CreatePrivate
        );
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"MakerWorld Request Create(3)           %d",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_RequestCreate)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] World-maker creation requests: %d.",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_RequestCreate)
    L2_LOG_INFO(
        Log, "[NPC statistics] World-maker creation requests: %d.", NpcSpawnInfo::Inst()->NpcMakerWorld_RequestCreate
    );
    const std::int32_t privateRequest = NpcSpawnInfo::Inst()->Npc_CreatePrivate;
    const std::int32_t makerWorldRequest = NpcSpawnInfo::Inst()->NpcMakerWorld_RequestCreate;
    const std::int32_t makerRequest = NpcSpawnInfo::Inst()->NpcMaker_RequestCreate;
    const std::int32_t totalRequest =
        runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(privateRequest, makerWorldRequest), makerRequest);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"total request create(1+2+3)            %d",
    //     totalRequest)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Total creation requests from makers, world makers and private NPCs: %d.",
    //     totalRequest)
    L2_LOG_INFO(
        Log, "[NPC statistics] Total creation requests from makers, world makers and private NPCs: %d.", totalRequest
    );

    const std::int32_t makerOnCreate = NpcSpawnInfo::Inst()->NpcMaker_OnCreate;
    if (totalRequest == makerOnCreate) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Maker On Create(=1+2+3 =EnterWorld)    %d",
        //     NpcSpawnInfo::Inst()->NpcMaker_OnCreate)
        // Log.Add(LOG_NORMAL,
        //     "[NPC statistics] Maker creation callbacks: %d. The preceding comparison matched total creation requests.",
        //     NpcSpawnInfo::Inst()->NpcMaker_OnCreate)
        L2_LOG_INFO(
            Log, "[NPC statistics] Maker creation callbacks: %d; the preceding comparison matched total creation requests.",
            NpcSpawnInfo::Inst()->NpcMaker_OnCreate
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Maker On Create(=1+2+3 =EnterWorld)     %d",
        //     NpcSpawnInfo::Inst()->NpcMaker_OnCreate)
        // Log.Add(LOG_WARN,
        //     "[NPC statistics] Maker creation counters differed: compared %d requests with %d callbacks. Latest callback
        //     count %d. Counters are read independently.", totalRequest, makerOnCreate,
        //     NpcSpawnInfo::Inst()->NpcMaker_OnCreate)
        L2_LOG_WARN(
            Log,
            "[NPC statistics] Maker creation counters differed: compared %d requests with %d callbacks; latest callback "
            "count %d. Counters are read independently.",
            totalRequest, makerOnCreate, NpcSpawnInfo::Inst()->NpcMaker_OnCreate
        );
    }

    // Log.Add(LOG_NORMAL, L"----- maker delete -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer Maker On Delete              %d",
    //     NpcSpawnInfo::Inst()->NpcMaker_OnDelete)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Maker deletion callbacks: %d.",
    //     NpcSpawnInfo::Inst()->NpcMaker_OnDelete)
    L2_LOG_INFO(Log, "[NPC statistics] Maker deletion callbacks: %d.", NpcSpawnInfo::Inst()->NpcMaker_OnDelete);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Server DeletePrivate(CNPC::Delete)     %d",
    //     NpcSpawnInfo::Inst()->Server_DeletePrivate)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Private NPC deletions in L2Server: %d.",
    //     NpcSpawnInfo::Inst()->Server_DeletePrivate)
    L2_LOG_INFO(Log, "[NPC statistics] Private NPC deletions in L2Server: %d.", NpcSpawnInfo::Inst()->Server_DeletePrivate);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"NpcServer Private Die                  %d",
    //     NpcSpawnInfo::Inst()->Npc_OnPrivateDie)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] Private NPC deaths reported in L2NPC: %d.",
    //     NpcSpawnInfo::Inst()->Npc_OnPrivateDie)
    L2_LOG_INFO(Log, "[NPC statistics] Private NPC deaths reported in L2NPC: %d.", NpcSpawnInfo::Inst()->Npc_OnPrivateDie);

    // Log.Add(LOG_NORMAL, L"----- etc -----")
    // Noise: the following rows already name their counters. The separator adds no information.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"EnterWorld(Queue)                      %d",
    //     NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] NPCs queued to enter the world: %d.",
    //     NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize)
    L2_LOG_INFO(
        Log, "[NPC statistics] NPCs queued to enter the world: %d.", NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"MakerWorld On Create                   %d",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_OnCreate)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] World-maker creation callbacks: %d.",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_OnCreate)
    L2_LOG_INFO(Log, "[NPC statistics] World-maker creation callbacks: %d.", NpcSpawnInfo::Inst()->NpcMakerWorld_OnCreate);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"MakerWorld On Delete                   %d",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_OnDelete)
    // Log.Add(LOG_NORMAL,
    //     "[NPC statistics] World-maker deletion callbacks: %d.",
    //     NpcSpawnInfo::Inst()->NpcMakerWorld_OnDelete)
    L2_LOG_INFO(Log, "[NPC statistics] World-maker deletion callbacks: %d.", NpcSpawnInfo::Inst()->NpcMakerWorld_OnDelete);
}

SharedEventInfo* SharedEventInfo::Inst() {
    // The L2Server and L2NPC products attach the same one-element shared
    // vector by its producer-owned mapping name.
    static SharedEventInfoVector info(L"SharedEventInfo");
    return &info[0];
}

SharedCleftInfo* SharedCleftInfo::Inst() {
    // Current L2Server.exe 0x00a59808. The producer-owned mapping is shared
    // with L2NPC, but each product emits its own template envelope.
    static SharedCleftInfoVector info(L"SharedCleftInfo");
    return &info[0];
}

void SharedCleftInfo::Init() {
    // Current L2Server.exe 0x00a557f0.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedCleftInfo] Init")
    // Noise: this Init hook does not initialize or validate the shared record.
}

CSharedAgitDecoInfo* CSharedAgitDecoInfo::Inst() {
    // Current L2Server.exe 0x00a5945c constructs the one-record shared
    // vector named SharedAgitDecoInfo and returns element zero.
    static CSharedVector<CSharedAgitDecoInfo, 1, 1, 1> info(L"SharedAgitDecoInfo");
    return &info[0];
}

void CSharedAgitDecoInfo::Init() {
    // Current L2Server.exe 0x00a557d8.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[CSharedAgitDecoInfo] Init")
    // Noise: this Init hook does not initialize or validate the shared record.
}

SharedFieldCycleInfo* SharedFieldCycleInfo::GetInfo(int cycleId) {
    // Current L2Server.exe 0x00a59700 attaches the same sixteen-record
    // producer-owned shared vector as L2NPC and returns the indexed record.
    static CSharedVector<SharedFieldCycleInfo, 16, 1, 16> info(L"SharedFieldCycleInfo");
    return &info[cycleId];
}

SharedCastleInfo* SharedCastleInfo::GetInfo(const int residenceId) {
    // Current L2Server.exe 0x00a59758.
    static SharedCastleInfoVector info(L"SharedCastleInfo");
    return &info[residenceId];
}

void SharedEventInfo::Init() {
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedEventInfo] Init")
    // Noise: this Init hook does not initialize or validate the shared record.
}

int SharedTimeAttackInfo::GetRewardFlag(int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a561a8 rereads the producer-owned count.
    const volatile int& observedCount = mMemberCount;
    for (int index = 0; index < observedCount; ++index) {
        if (mMemberDBIDList[index] == characterDatabaseId) {
            return mMemberRewardFlags[index];
        }
    }
    return 0;
}

int SharedTimeAttackInfo::SetRewardFlag(int characterDatabaseId, int rewardFlag) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a5626c rereads the producer-owned count.
    const volatile int& observedCount = mMemberCount;
    for (int index = 0; index < observedCount; ++index) {
        if (mMemberDBIDList[index] == characterDatabaseId) {
            mMemberRewardFlags[index] = rewardFlag;
            return 1;
        }
    }
    return 0;
}

int SharedTimeAttackInfo::IsMember(int characterDatabaseId) const {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56338 rereads the producer-owned count.
    const volatile int& observedCount = mMemberCount;
    for (int index = 0; index < observedCount; ++index) {
        if (mMemberDBIDList[index] == characterDatabaseId) {
            return 1;
        }
    }
    return 0;
}

int SharedOlympiadInfo::GetRankByName(ClassType classType, char16_t* name) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a563f8 computes the class index twice and
    // snapshots each complete 0x3c-byte row before comparing its name.
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

bool SharedOlympiadInfo::ChangeRankName(ClassType classType, char16_t* oldName, char16_t* newName) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56564 has no added null or rank-count cap.
    const int listClassIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (listClassIndex == -1) {
        return false;
    }
    const int countClassIndex = OlympiadDefine::GetHeroClassIndex(classType);
    const std::uint64_t count = countClassIndex == -1 ? 0 : arrRankingCount[countClassIndex];
    sOlympiadRankInfo* const rankings = &arrRankingList[listClassIndex][0];
    for (int order = 0; static_cast<std::uint64_t>(static_cast<std::int64_t>(order)) < count; ++order) {
        sOlympiadRankInfo rank;
        std::memcpy(&rank, &rankings[order], sizeof(rank));
        if (CompareUtf16CaseInsensitive(rank.sName, oldName) != 0) {
            continue;
        }
        CopyUtf16Secure25(rankings[order].sName, newName);
        return true;
    }
    return false;
}

void SharedOlympiadInfo::AddRankInfo(ClassType classType, int ranking, const SharedUtilsWideChar* name, int point) {
    // Current L2Server.exe 0x00673124 (the imported shareinfo.h body).
    const int classIndex = OlympiadDefine::GetHeroClassIndex(classType);
    if (classIndex == -1) {
        return;
    }

    std::uint64_t& count = arrRankingCount[classIndex];
    if (count >= RANK_COUNT_PER_CLASS) {
        return;
    }

    sOlympiadRankInfo& destination = arrRankingList[classIndex][count];
    destination.nRanking = ranking;
    static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
    CopyUtf16Secure25(destination.sName, reinterpret_cast<const char16_t*>(name));
    destination.nPoint = point;
    ++count;
}

SharedPlatformInfo* SharedPlatformInfo::Inst() {
    L2_GUARD_FUNCTION();
    // Current L2Server counterpart of the shared platform producer. This
    // process owns a distinct mapped instance from the L2NPC consumer TU.
    static SharedType<SharedPlatformInfo> info(L"L2SharedPlatformInfo");
    return info;
}

void SharedPlatformInfo::Init() {
    // Current L2Server.exe 0x00a55820.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[SharedPlatformInfo] Init")
    // Noise: this Init hook does not initialize or validate the shared record.
}

int SharedPlatformInfo::AddPlatform(int airportId, int platformId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a566fc.
    const volatile std::uint32_t& observedMaximum = m_MaxIndex;
    const std::uint32_t oldMaximum = observedMaximum;
    if (oldMaximum >= MAX_PLATFORM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] shared airport info is full.",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x163,
        //     airportId,
        //     platformId)
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Cannot register airport %d, platform %d: shared maximum %u reaches or exceeds capacity
        //     %u, returning 0 without adding an entry.", airportId, platformId, static_cast<unsigned>(oldMaximum),
        //     static_cast<unsigned>(MAX_PLATFORM))
        L2_LOG_ERROR(
            Log,
            "[airship platforms] Cannot register airport %d, platform %d: shared maximum %u reaches or exceeds capacity %u; "
            "returning 0 without adding an entry.",
            airportId, platformId, static_cast<unsigned>(oldMaximum), static_cast<unsigned>(MAX_PLATFORM)
        );
        return 0;
    }

    for (std::uint32_t index = 0; index < observedMaximum; ++index) {
        const SharedPlatform& platform = m_PlatformList[index];
        if (platform.airportId == airportId && platform.platformId == platformId) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] duplicated airportId and platformId[%d, %d]",
            //     L"..\\Shared\\ShareInfo.cpp",
            //     0x16e,
            //     airportId,
            //     platformId)
            // Log.Add(LOG_WARN,
            //     "[airship platforms] Airport %d, platform %d is already registered. Existing entry retained, returning
            //     -1.", airportId, platformId)
            L2_LOG_WARN(
                Log,
                "[airship platforms] Airport %d, platform %d is already registered; existing entry retained, returning -1.",
                airportId, platformId
            );
            return -1;
        }
    }

    m_MaxIndex = oldMaximum + 1U;
    SharedPlatform& platform = m_PlatformList[oldMaximum];
    platform.airportId = airportId;
    platform.platformId = platformId;
    platform.occupied = false;
    return static_cast<int>(oldMaximum);
}

bool SharedPlatformInfo::SetOccupied(int index, bool occupied) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56864. Equality with m_MaxIndex is accepted.
    const std::uint32_t maximum = m_MaxIndex;
    if (maximum >= MAX_PLATFORM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what the hell. [%d][%d]",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x188,
        //     static_cast<int>(maximum),
        //     static_cast<int>(MAX_PLATFORM))
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Cannot set occupancy at index %d to %s: shared maximum %u reaches or exceeds capacity %u.
        //     state unchanged.", index, occupied ? "occupied" : "free", static_cast<unsigned>(maximum),
        //     static_cast<unsigned>(MAX_PLATFORM))
        L2_LOG_ERROR(
            Log,
            "[airship platforms] Cannot set occupancy at index %d to %s: shared maximum %u reaches or exceeds capacity %u; "
            "state unchanged.",
            index, occupied ? "occupied" : "free", static_cast<unsigned>(maximum), static_cast<unsigned>(MAX_PLATFORM)
        );
        return false;
    }
    if (index < 0 || maximum < static_cast<std::uint32_t>(index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid index[%d] of shared platform.",
        //     L"..\\Shared\\ShareInfo.cpp",
        //     0x194,
        //     index)
        // Log.Add(LOG_ERROR,
        //     "[airship platforms] Cannot set occupancy at index %d to %s: index is outside the accepted range 0..%u. State
        //     unchanged.", index, occupied ? "occupied" : "free", static_cast<unsigned>(maximum))
        L2_LOG_ERROR(
            Log,
            "[airship platforms] Cannot set occupancy at index %d to %s: index is outside the accepted range 0..%u; state "
            "unchanged.",
            index, occupied ? "occupied" : "free", static_cast<unsigned>(maximum)
        );
        return false;
    }
    m_PlatformList[index].occupied = occupied;
    return true;
}

bool SharedPlatformInfo::IsOccupied(int index) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56998 captures m_MaxIndex once. Equality
    // with that value is accepted. An invalid index returns true.
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

void SharedInstantAgitApplyData::Init(int newInstantAgitId, int newMaxInstantAgitCount, int newCost) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56abc writes only these three fields.
    instantAgitId = newInstantAgitId;
    maxInstantAgitCount = newMaxInstantAgitCount;
    cost = static_cast<std::int64_t>(newCost);
}

std::int32_t SharedInstantAgitApplyData::MakeFirstApplyDate(std::int32_t lotteryDate, int applyDuration) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56b70 uses one wrapping 32-bit subtraction.
    return runtime::SubtractInt32Wrapping(lotteryDate, static_cast<std::int32_t>(applyDuration));
}

std::int32_t SharedInstantAgitApplyData::MakeLastApplyDate(std::int32_t lotteryDate) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56c0c uses one wrapping 32-bit subtraction.
    return runtime::SubtractInt32Wrapping(lotteryDate, InstantAgitJobType::RENTAL_PERIOD_BUFFER);
}

void SharedInstantAgitApplyData::SetLotteryDate(int newFirstApplyDate, int newLastApplyDate) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a5817c resets the count before both local-time
    // conversions and applies the five-minute buffer only to the second one.
    applicantCount = 0;
    CopyLocalTime32(static_cast<std::int32_t>(newFirstApplyDate), firstApplyDate);
    CopyLocalTime32(MakeLastApplyDate(static_cast<std::int32_t>(newLastApplyDate)), lastApplyDate);
}

SharedInstantAgitIndexInfo& SharedInstantAgitApplyData::GetIndexInfo() {
    static SharedInstantAgitIndexInfo info(L"SharedInstantAgitApplyDataIndexInfo");
    return info;
}

SharedInstantAgitApplyDataList& SharedInstantAgitApplyData::Inst() {
    static SharedInstantAgitApplyDataList data(L"SharedInstantAgitApplyData");
    return data;
}

SharedInstantAgitApplyData* SharedInstantAgitApplyData::GetInfo(int instantAgitId) {
    SharedInstantAgitApplyDataList& records = Inst();
    for (int index = 0;; ++index) {
        SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
        if (indexInfo->index <= index) {
            return nullptr;
        }
        if (records[index].instantAgitId == instantAgitId) {
            return &records[index];
        }
    }
}

void SharedInstantAgitDeco::InstantAgitDecoSlot::Reset() {
    agitDecoId = 0;
    agitDecoLevel = 0;
    agitDecoPower = 0.0;
}

void SharedInstantAgitDeco::Init(int pledgeDatabaseId) {
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
    static SharedInstantAgitIndexInfo info(L"SharedInstantAgitDecoDataIndexInfo");
    return info;
}

SharedInstantAgitDecoDataList& SharedInstantAgitDecoData::Inst() {
    static SharedInstantAgitDecoDataList data(L"SharedInstantAgitDecoDataList");
    return data;
}

SharedInstantAgitDeco* SharedInstantAgitDecoData::GetInfo(int pledgeDatabaseId) {
    SharedInstantAgitDecoDataList& records = Inst();
    for (int index = 0;; ++index) {
        SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
        if (indexInfo->index < index) {
            return nullptr;
        }
        if (records[index].pledgeDBId == pledgeDatabaseId) {
            return &records[index];
        }
    }
}

SharedInstantAgitDeco* SharedInstantAgitDecoData::Create() {
    SharedInstantAgitIndexInfo& indexInfo = GetIndexInfo();
    const std::int32_t previous = runtime::raw_atomic::AddI32(&indexInfo->index, 1) - 1;
    SharedInstantAgitDecoDataList& records = Inst();
    return &records[previous + 1];
}

SharedPledgeRaidInfoData::SharedPledgeRaidInfoData(
    int pledgeDatabaseId, const char16_t* pledgeName, std::int64_t point, int rank, int tier, bool canReward
) {
    // Current L2Server.exe 0x00a56c9c writes exactly these six members.
    nPledgeDbId_ = pledgeDatabaseId;
    CopyUtf16Secure25(szPledgeName_, pledgeName);
    nPoint_ = point;
    nRank_ = rank;
    nTier_ = tier;
    bCanReward_ = canReward;
}

SharedPledgeRaidInfoData* SharedPledgeRaidInfo::GetPrevSeasonPledgeRaidInfoWithDbId(int pledgeDatabaseId) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56ce8 performs an uncapped first-match scan
    // and rereads the producer-owned count at every loop condition.
    const volatile int& observedCount = nTotalCount_;
    for (int index = 0; index < observedCount; ++index) {
        SharedPledgeRaidInfoData& entry = prevSeasonPledgeRaid_[index];
        if (entry.nPledgeDbId_ == pledgeDatabaseId) {
            return &entry;
        }
    }
    return nullptr;
}

SharedPledgeRaidInfoData* SharedPledgeRaidInfo::GetPrevSeasonPledgeRaidInfo(int index) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56db0 has no array-capacity clamp.
    if (index < 0 || nTotalCount_ <= index) {
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

void SharedPledgeRaidInfo::AddPrevSeasonPledgeRaid(const SharedPledgeRaidInfoData& data) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56e84 has no capacity check.
    const int index = nTotalCount_;
    nTotalCount_ = index + 1;
    SharedPledgeRaidInfoData& destination = prevSeasonPledgeRaid_[index];
    destination.nPledgeDbId_ = data.nPledgeDbId_;
    CopyUtf16Secure25(destination.szPledgeName_, data.szPledgeName_);
    destination.nPoint_ = data.nPoint_;
    destination.nRank_ = data.nRank_;
    destination.nTier_ = data.nTier_;
    destination.bCanReward_ = data.bCanReward_;
}

void SharedPledgeRaidInfo::Clear() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a56f68 keeps this write order.
    *static_cast<volatile ConditionInSeason*>(&eHuntNpcCondition_) = CIS_UNKNOWN;
    *static_cast<volatile int*>(&nSeason_) = 0;
    *static_cast<volatile int*>(&nStep_) = 0;
    *static_cast<volatile std::int32_t*>(&tSeasonStartTime_) = 0;
    *static_cast<volatile std::int32_t*>(&tNextSeasonStartTime_) = 0;
    *static_cast<volatile int*>(&nPrevSeason_) = 0;
    *static_cast<volatile std::int32_t*>(&tPrevSeasonStartTime_) = 0;
    *static_cast<volatile int*>(&nTotalCount_) = 0;
    std::memset(prevSeasonPledgeRaid_, 0, 8000);
}

SharedTimeAttackInfo* SharedTimeAttackInfo::GetInfo(int roomNo, int recordType) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a58044 owns five rooms by three record types.
    static CSharedArray<SharedTimeAttackInfo, 15> information(L"L2SharedTimeAttackInfo");
    if (static_cast<std::uint32_t>(roomNo) - 1U >= 5U || static_cast<std::uint32_t>(recordType) >= 3U) {
        return nullptr;
    }
    return &information[(roomNo - 1) * 3 + recordType];
}

SharedSSQInfo* SharedSSQInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a59394.
    static SharedType<SharedSSQInfo> info(L"L2SharedSSQINFO");
    return info;
}

SharedRimPointInfo* SharedRimPointInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a594b0.
    static SharedType<SharedRimPointInfo> info(L"L2SharedRimPointInfo");
    return info;
}

SharedOlympiadInfo* SharedOlympiadInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a595e4.
    static SharedType<SharedOlympiadInfo> info(L"L2SharedOlympiadInfo");
    return info;
}

SharedDynamicQuestInfo* SharedDynamicQuestInfo::GetInfo(int index) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a597b0. This product owns a distinct
    // process-local mapped-vector instance. Do not reuse the L2NPC TU.
    static SharedDynamicQuestInfoVector info(L"SharedDynamicQuestInfo");
    return &info[index];
}

SharedPledgeGameInfo* SharedPledgeGameInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a59a48.
    static SharedType<SharedPledgeGameInfo> info(L"L2SharedPledgeGameInfo");
    return info;
}

SharedKamarokaInfo* SharedKamarokaInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a59b10.
    static SharedType<SharedKamarokaInfo> info(L"L2SharedKamarokaInfo");
    return info;
}

SharedRaidServerData* SharedRaidServerData::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a59bd8.
    static SharedType<SharedRaidServerData> info(L"SharedRaidServerData");
    return info;
}

SharedPledgeRaidInfo* SharedPledgeRaidInfo::GetInfo() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00a59ca0.
    static SharedType<SharedPledgeRaidInfo> info(L"L2SharedPledgeRaidInfo");
    return info;
}
