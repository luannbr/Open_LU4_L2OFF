#include "L2StatusWnd.h"

#include "Default.h"
#include "L2Time.h"
#include "NewInspector.h"
#include "Raid.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareInfo.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

// Current L2Server.exe 0x16658450/0x16658454/0x16658458. The matching
// TotalChecker map keeps all three symbols in their original L2StatusWnd.obj
// physical owner.
std::int32_t g_CPUUsage = 0;
std::int32_t g_L2ServerUsage = 0;
std::int32_t g_L2NPCUsage = 0;

CL2StatusWnd::CL2StatusWnd() {
    // Current L2Server.exe 0x0080aafc.
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    SYSTEM_INFO systemInformation;
    ::GetSystemInfo(&systemInformation);
    const unsigned processorCount = systemInformation.dwNumberOfProcessors;
#else
    const unsigned processorCount = std::thread::hardware_concurrency();
#endif

    char adminRevisionText[80];
    char npcRevisionText[80];
    char databaseRevisionText[80];
    int adminRevision;
    int npcRevision;
    int databaseRevision;
#if defined(_MSC_VER)
    ::sscanf_s(
        "$Revision: #56 $", "%s #%d", adminRevisionText, static_cast<unsigned>(sizeof(adminRevisionText)), &adminRevision
    );
    ::sscanf_s("$Revision: #272 $", "%s #%d", npcRevisionText, static_cast<unsigned>(sizeof(npcRevisionText)), &npcRevision);
    ::sscanf_s(
        "$Revision: #373 $", "%s #%d", databaseRevisionText, static_cast<unsigned>(sizeof(databaseRevisionText)),
        &databaseRevision
    );
#else
    std::sscanf("$Revision: #56 $", "%79s #%d", adminRevisionText, &adminRevision);
    std::sscanf("$Revision: #272 $", "%79s #%d", npcRevisionText, &npcRevision);
    std::sscanf("$Revision: #373 $", "%79s #%d", databaseRevisionText, &databaseRevision);
#endif
    const int l2ProtocolVersion = GetL2ProtocolVersion();
    SetMessageV(0, L"L2[%d] DB[%d] NPC[%d] Admin[%d]", l2ProtocolVersion, databaseRevision, npcRevision, adminRevision);

    int communicationRevision;
    int sharedFileRevision;
#if defined(_MSC_VER)
    ::sscanf_s(
        "$Revision: #5 $", "%s #%d", databaseRevisionText, static_cast<unsigned>(sizeof(databaseRevisionText)),
        &communicationRevision
    );
    ::sscanf_s(
        "$Revision: #289 $", "%s #%d", npcRevisionText, static_cast<unsigned>(sizeof(npcRevisionText)), &sharedFileRevision
    );
#else
    std::sscanf("$Revision: #5 $", "%79s #%d", databaseRevisionText, &communicationRevision);
    std::sscanf("$Revision: #289 $", "%79s #%d", npcRevisionText, &sharedFileRevision);
#endif
    const int sharedInfoVersion = GetSharedInfoVersion();
    SetMessageV(1, L"SF[%d] COMM[%d] SharedInfo[%d]", sharedFileRevision, communicationRevision, sharedInfoVersion);

#if defined(_WIN32)
    ULONG highestNumaNode;
    if (!::GetNumaHighestNodeNumber(&highestNumaNode)) {
        SetMessageV(2, L"%s ", g_GuardHistoryStr);
    } else {
        SetMessageV(2, L"NUMA[%s] %s", highestNumaNode == 0 ? L"OFF" : L"ON", g_GuardHistoryStr);
    }
#else
    // The current Windows failure branch is the exact no-NUMA-API boundary.
    SetMessageV(2, L"%s ", g_GuardHistoryStr);
#endif

    const int ioThreadCount = GetIOThreadCount();
    SetMessageV(3, L"CPU[%d] IOThread[%d]", static_cast<int>(processorCount), ioThreadCount);
    SetMessageV(4, L"BuildDate    : %s", CBuildDate::inst().BuildDateTime());
    DisplayCommon();
}

void CL2StatusWnd::DisplayCommon() {
    // Current L2Server.exe 0x0080a9e4 observes total before free/count.
    const int totalBufferCount = GetIOBufferPoolTotalCount();
    const int freeBufferCount = GetIOBufferPoolCount();
    SetMessageV(5, L"IOBufferPool : %d (count) / %d", freeBufferCount, totalBufferCount);

    const int userSocketCount = g_ServerContext.UserSocket.load(std::memory_order_relaxed);
    SetMessageV(6, L"UserSocket   : %d (current)", userSocketCount);

    // The current instructions read these volatile globals right-to-left.
    const int maximumUserCount = g_nMaxServerUserCount;
    const int playingUserCount = g_nPlayingUserCount;
    const int userCount = g_nServerUserCount;
    SetMessageV(7, L"User         : %d, Play: %d, MAX: %d", userCount, playingUserCount, maximumUserCount);

    const int pendingWrite = g_nSocketPendingWrite.load(std::memory_order_relaxed);
    SetMessageV(8, L"PendingWrite : [%d], /16k [%d]", pendingWrite, pendingWrite / 0x4000);
    SetMessageV(9, L"Server Up    : %s", GetServerUpTimeString());
    SetMessageV(10, L"Current      : %s", GetCurrentTimeString());
    wchar_t* const elapsed = GetElapsedTimeString();
    SetMessageV(11, L"Elapsed      : %s", elapsed);
    std::free(elapsed);
}

void CL2StatusWnd::OnRefresh() {
    // Current L2Server.exe 0x0080adcc.
    L2_GUARD_FUNCTION();

    DisplayCommon();

    // The current instructions observe NPC, server and total CPU in that
    // order before publishing the display row.
    const int npcUsage = g_L2NPCUsage;
    const int serverUsage = g_L2ServerUsage;
    const int totalUsage = g_CPUUsage;
    SetMessageV(12, L"CPU Usage    : Total[%d] Server[%d] NPC[%d]", totalUsage, serverUsage, npcUsage);

    SetMessageV(
        13, L"InspectorCRC : [%ld]",
        static_cast<long>(runtime::Int32FromBits(static_cast<std::uint32_t>(g_NewInspectorDB.GetCRC())))
    );

    const int inZoneNpcCount = g_nInZoneNPCCount;
    const int npcCount = g_nNPCCount;
    SetMessageV(14, L"NPCCount     : [%d] InZone[%d]", npcCount, inZoneNpcCount);

    const RaidUserCountLong outgoingRaidUsers = RaidClientManager::inst().GetCurrentRaidUserCount();
    const int incomingRaidUsers = RaidServerManager::inst().GetCurrentRaiduserCount();
    SetMessageV(
        15, L"Raid in/out  : [%ld] / [%ld]", static_cast<long>(incomingRaidUsers), static_cast<long>(outgoingRaidUsers)
    );
}
