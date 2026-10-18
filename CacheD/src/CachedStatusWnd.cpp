#include "CachedStatusWnd.h"

#include "Cached.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOC.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

namespace {

std::array<int, 4> InetAddressOctets(const std::string& text) {
    // Current InitConfig stores inet_addr's network-order dword and the
    // CachedStatusWnd constructor reads its four bytes in memory order.
    const std::uint32_t networkAddress = static_cast<std::uint32_t>(::inet_addr(text.c_str()));
    std::array<unsigned char, 4> bytes{};
    std::memcpy(bytes.data(), &networkAddress, sizeof(networkAddress));
    return {static_cast<int>(bytes[0]), static_cast<int>(bytes[1]), static_cast<int>(bytes[2]), static_cast<int>(bytes[3])};
}

int TargetSignedPort(std::uint16_t port) {
    // Both current status lines use movsx from the 16-bit process global.
    return static_cast<int>(static_cast<std::int16_t>(port));
}

} // namespace

CachedStatusWnd::CachedStatusWnd() {
    // Current Cached.exe 0x0045aae4.
    L2_GUARD_FUNCTION();


    char revisionText[80];
    int databaseRevision;
    int npcDatabaseRevision;
    int adminRevision;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #56 $", "%s #%d", revisionText, static_cast<unsigned>(sizeof(revisionText)), &databaseRevision);
    ::sscanf_s(
        "$Revision: #14 $", "%s #%d", revisionText, static_cast<unsigned>(sizeof(revisionText)), &npcDatabaseRevision
    );
    ::sscanf_s("$Revision: #373 $", "%s #%d", revisionText, static_cast<unsigned>(sizeof(revisionText)), &adminRevision);
#else
    std::sscanf("$Revision: #56 $", "%79s #%d", revisionText, &databaseRevision);
    std::sscanf("$Revision: #14 $", "%79s #%d", revisionText, &npcDatabaseRevision);
    std::sscanf("$Revision: #373 $", "%79s #%d", revisionText, &adminRevision);
#endif

    // The current instructions pass #373 in the DB slot and #56 in the
    // Admin slot. Preserve that shipping display bug instead of silently
    // correcting the labels.
    SetMessageV(
        0, L"L2[%d], DB[%d], NPCDB[%d], Admin[%d]", GetL2ProtocolVersion(), adminRevision, npcDatabaseRevision,
        databaseRevision
    );

    const CacheDConfiguration& config = GetCacheDConfiguration();
    SetMessageV(1, L"WorldId[%d], BinaryType[%s]", config.worldId, L"RELEASE");

    const std::array<int, 4> gipAddress = InetAddressOctets(config.gipAddress);
    SetMessageV(
        2, L"GIP[%s, %d.%d.%d.%d:%d] Tick[%d]", config.gipEnabled ? L"On" : L"Off", gipAddress[0], gipAddress[1],
        gipAddress[2], gipAddress[3], TargetSignedPort(config.gipPort),
        static_cast<std::int32_t>(config.gipHeartbeatMilliseconds)
    );

    const std::array<int, 4> logDAddress = InetAddressOctets(config.logDAddress);
    SetMessageV(
        3, L"LogD: [%d.%d.%d.%d:%d]", logDAddress[0], logDAddress[1], logDAddress[2], logDAddress[3],
        TargetSignedPort(config.logDPort)
    );

    SetMessageV(4, L"Build Date : %s", CBuildDate::inst().BuildDateTime());
    SetMessageV(5, L"Server Up Time : %s", GetServerUpTimeString());
    SetMessageV(6, L"Current   Time : %s", GetCurrentTimeString());
    wchar_t* const elapsed = GetElapsedTimeString();
    SetMessageV(7, L"Elapsed   time : %s", elapsed);
    std::free(elapsed);
}

CachedStatusWnd::~CachedStatusWnd() {
    // Current Cached.exe 0x0045adec.
    L2_GUARD_FUNCTION();
}

void CachedStatusWnd::OnRefresh() {
    // Current Cached.exe 0x0045ae88.
    L2_GUARD_FUNCTION();


    SetMessageV(6, L"Current   Time : %s", GetCurrentTimeString());
    wchar_t* const elapsed = GetElapsedTimeString();
    SetMessageV(7, L"Elapsed   time : %s", elapsed);
    std::free(elapsed);
    SetMessageV(8, L"IOBufferPool : %d (count) / %d", GetIOBufferPoolCount(), GetIOBufferPoolTotalCount());

    // The Windows target displays its ODBC FILEDSN global here. PostgreSQL
    // has no FILEDSN, so the transport-neutral carrier remains empty rather
    // than exposing or relabelling a PostgreSQL credential string.
    SetMessageV(9, L"File DSN     : %s", L"");
}

CachedStatusWnd& CachedStatusWnd::Init() {
    // Current Cached.exe 0x0045afd8.
    L2_GUARD_FUNCTION();

    static CachedStatusWnd instance;
    return instance;
}
