#include "Shared/src/Runtime/ConfigDirectory.h"
#include "ServerList.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/TimerQueue.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Config.h"
#include "DBConn.h"
#include "IOBuffer.h"
#include "IOServer.h"
#include "Log.h"
#include "Reporter.h"
#include "Util.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <utility>
#include <vector>

void ReadServerListInfo(std::vector<WorldServer>& servers);

#if defined(_WIN32)
void __stdcall UserCountTimerRoutine(
#else
void UserCountTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);

CServerList g_ServerList;
CServerUserCountStatus g_UserCount;

namespace {

constexpr std::size_t kServerListFrameCapacity = 0x17f9;
constexpr std::ptrdiff_t kPacket6Capacity = 0x17bd;
constexpr std::uint32_t kRevivalServerBit = UINT32_C(0x80000000);
constexpr std::uint32_t kServerSlotMask = UINT32_C(0x7fffffff);
constexpr std::uint32_t kInvalidSocket = UINT32_C(0xffffffff);

// Diagnostic-only batch summary. All SQL and parameter binding stays below.
void ReportConcurrentUserStatisticsFailures(
    const std::array<bool, 128>& failedServers,
    unsigned int attempted) {
    unsigned int failed = 0;
    for (const bool value : failedServers) {
        if (value) ++failed;
    }
    if (failed == 0) return;

    // Every ID is 0..127, followed by at most a comma and a space.
    std::array<char, 128 * 5 + 1> ids{};
    std::size_t used = 0;
    bool incomplete = false;
    for (std::size_t index = 0; index < failedServers.size(); ++index) {
        if (!failedServers[index]) continue;
        const int written = std::snprintf(ids.data() + used, ids.size() - used,
            "%s%zu", used == 0 ? "" : ", ", index);
        if (written < 0 || static_cast<std::size_t>(written) >= ids.size() - used) {
            ids[used] = '\0';
            incomplete = true;
            break;
        }
        used += static_cast<std::size_t>(written);
    }
    // Log.AddLog(LOG_ERROR,
    //     "[metrics] Could not confirm %u of %u concurrent-user statistics updates. Server IDs: %s%s. See preceding database diagnostics.",
    //     failed, attempted, ids.data(), incomplete ? " (incomplete list)" : "")
    L2_LOG_ERROR(Log,
        "[metrics] Could not confirm %u of %u concurrent-user statistics updates. Server IDs: %s%s. See preceding database diagnostics.",
        failed, attempted, ids.data(), incomplete ? " (incomplete list)" : "");
}

char* AllocateServerListFrame() {
    auto* const frame = static_cast<char*>(
        ::operator new(kServerListFrameCapacity));
    std::memset(frame, 0, kServerListFrameCapacity);
    return frame;
}

void ReplaceServerListFrame(
    char*& destination,
    std::int32_t& destinationLength,
    char* replacement,
    int replacementLength) {
    char* previous = nullptr;
    if (destination != nullptr) {
        previous = destination;
    }
    destinationLength = replacementLength;
    destination = replacement;
    if (previous != nullptr) {
        ::operator delete(previous);
    }
}

std::int32_t EncodeRevivalSlot(int slot) noexcept {
    const std::uint32_t encoded =
        kRevivalServerBit | static_cast<std::uint32_t>(slot);
    std::int32_t result = 0;
    std::memcpy(&result, &encoded, sizeof(result));
    return result;
}

void WriteFrame2ServerName(
    char* destination,
    const char* source) {
    std::array<char, 20> truncated{};
    if (std::strlen(source) >= truncated.size()) {
        std::strncpy(
            truncated.data(),
            source,
            truncated.size() - 1);
        source = truncated.data();
    }

    // The selected Win32 image writes through swprintf(L"%S") into a
    // two-byte wchar_t field. Keep those exact bytes on LP64 hosts instead
    // of using the host's four-byte wchar_t representation.
    std::array<char16_t, 20> wideName{};
    portable::WindowsAnsiToWideChar(
        source,
        -1,
        wideName.data(),
        static_cast<int>(wideName.size()));
    std::memcpy(
        destination,
        wideName.data(),
        sizeof(wideName));
}

} // namespace

// L2Authd.exe 0x0047C3D0, Src/serverlist.cpp:37-55.
void CServerListPacket6::FreePacketPageList(
    PacketPageList& pages,
    PacketPageDynOffsets& offsets) {
    for (CIOBuffer*& page : pages) {
        page->Free();
        page = nullptr;
    }
    pages.clear();
    offsets.m_currentUser.clear();
    offsets.m_maxLimitUser.clear();
    offsets.m_serverStatus.clear();
}

// L2Authd.exe 0x0047C460, Src/serverlist.cpp:19-21.
CServerListPacket6::CServerListPacket6() = default;

// L2Authd.exe 0x0047C4D0, Src/serverlist.cpp:25-30.
CServerListPacket6::~CServerListPacket6() {
    FreePacketPageList(m_pages, m_infoOffsets);
}

// L2Authd.exe 0x0047C550, Src/serverlist.cpp:61-251.
bool CServerListPacket6::MakePacketFrame(
    const std::vector<WorldServer>& servers) {
    PacketPageList pages;
    PacketPageDynOffsets offsets;
    std::size_t serverIndex = 0;

    for (;;) {
        CIOBuffer* const page = CIOBuffer::Alloc();
        if (page == nullptr) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "MakeServerListFrame6 failed : alloc buffer")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=server_list event=frame_build_failed format=6 reason=buffer_allocation_failed completed_pages=%zu next_server_index=%zu server_records=%zu action=retain_previous_frame",
            //     pages.size(), serverIndex, servers.size())
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[worlds] Could not allocate a format-6 server-list buffer after %zu pages (next server index %zu of %zu records). Keeping the previous frame.",
            //     pages.size(), serverIndex, servers.size())
            // L2_LOG_ERROR(Log,
            //     "[worlds] Could not allocate a format-6 server-list buffer after %zu pages (next server index %zu of %zu records). Keeping the previous frame.",
            //     pages.size(), serverIndex, servers.size())
            char message[384]{};
            std::snprintf(message, sizeof(message),
                "[worlds] Could not allocate a format-6 server-list buffer after %zu pages (next server index %zu of %zu records). Keeping the previous frame.",
                pages.size(), serverIndex, servers.size());
            runtime::logging::Write("authd", runtime::logging::Level::Error, message, __FILE__);
            FreePacketPageList(pages, offsets);
            return false;
        }

        char* cursor = page->m_buffer;
        char* const end = page->m_buffer + kPacket6Capacity;
        BufCat(&cursor, static_cast<int>(end - cursor), '\0');
        BufCat(
            &cursor,
            static_cast<int>(end - cursor),
            static_cast<char>(pages.size()));
        BufCat(
            &cursor,
            static_cast<int>(end - cursor),
            static_cast<char>(servers.size()));
        char* const pageServerCount = cursor;
        BufCat(&cursor, static_cast<int>(end - cursor), '\0');

        int serversOnPage = 0;
        while (serverIndex < servers.size()) {
            const WorldServer& world = servers[serverIndex];
            char* const recordStart = cursor;

            bool appended = BufCat(
                &cursor,
                static_cast<int>(end - cursor),
                world.serverid);

            in_addr externalAddress{};
            externalAddress.s_addr = ::inet_addr(world.ip);
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    externalAddress);
            }
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    world.port);
            }
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    world.domainName.c_str(),
                    static_cast<int>(world.domainName.size() + 1));
            }
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    world.ageLimit);
            }
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    static_cast<char>(world.pkflag));
            }

            const int currentUserOffset = static_cast<int>(
                cursor - page->m_buffer);
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    static_cast<short>(0));
            }
            offsets.m_currentUser[world.serverid] = PageOffsetType(
                static_cast<int>(pages.size()),
                currentUserOffset);

            const int maximumUserOffset = static_cast<int>(
                cursor - page->m_buffer);
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    static_cast<short>(0));
            }
            offsets.m_maxLimitUser[world.serverid] = PageOffsetType(
                static_cast<int>(pages.size()),
                maximumUserOffset);

            const int serverStatusOffset = static_cast<int>(
                cursor - page->m_buffer);
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    '\0');
            }
            offsets.m_serverStatus[world.serverid] = PageOffsetType(
                static_cast<int>(pages.size()),
                serverStatusOffset);

            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    world.serverKind);
            }
            if (appended) {
                appended = BufCat(
                    &cursor,
                    static_cast<int>(end - cursor),
                    world.region);
            }

            if (!appended) {
                *pageServerCount = static_cast<char>(serversOnPage);
                page->m_size = static_cast<std::uint32_t>(
                    recordStart - page->m_buffer);
                pages.push_back(page);
                break;
            }

            ++serversOnPage;
            ++serverIndex;
        }

        if (serverIndex == servers.size()) {
            *pageServerCount = static_cast<char>(serversOnPage);
            page->m_size = static_cast<std::uint32_t>(
                cursor - page->m_buffer);
            pages.push_back(page);

            for (CIOBuffer* packetPage : pages) {
                packetPage->m_buffer[0] =
                    static_cast<char>(pages.size());
            }

            {
                L2_WRITE_GUARD(m_lock);
                FreePacketPageList(m_pages, m_infoOffsets);
                m_pages = pages;
                m_infoOffsets.m_currentUser =
                    offsets.m_currentUser;
                m_infoOffsets.m_maxLimitUser =
                    offsets.m_maxLimitUser;
                m_infoOffsets.m_serverStatus =
                    offsets.m_serverStatus;
            }

            // Log.AddLog(
            //     LOG_NORMAL,
            //     "ServerListPacket(6) has %d pages.",
            //     static_cast<int>(m_pages.size()))
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=server_list event=frame_built format=6 pages=%zu server_records=%zu",
            //     pages.size(), servers.size())
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[worlds] Built format-6 server list: %zu pages, %zu server records.",
            //     pages.size(), servers.size())
            L2_LOG_INFO(Log,
                "[worlds] Built format-6 server list: %zu pages, %zu server records.",
                pages.size(), servers.size());
            return true;
        }
    }
}

// L2Authd.exe 0x0047CBE0, Src/serverlist.cpp:258-274.
void CServerListPacket6::SetServerStatus(
    const char serverId,
    const char status) {
    {
        L2_WRITE_GUARD(m_lock);
        const auto found = m_infoOffsets.m_serverStatus.find(serverId);
        if (found != m_infoOffsets.m_serverStatus.end()) {
            const PageOffsetType& location = found->second;
            CIOBuffer* const page = m_pages.at(location.m_pageNo);
            if (page != nullptr) {
                page->m_buffer[location.m_offset] = status;
            }
        }
    }
}

// L2Authd.exe 0x0047CC70, Src/serverlist.cpp:282-310.
void CServerListPacket6::SetUserNum(
    const char serverId,
    const short currentUser,
    const short maximumUser) {
    {
        L2_WRITE_GUARD(m_lock);

        const auto current = m_infoOffsets.m_currentUser.find(serverId);
        if (current != m_infoOffsets.m_currentUser.end()) {
            const PageOffsetType& location = current->second;
            CIOBuffer* const page = m_pages.at(location.m_pageNo);
            if (page != nullptr) {
                std::memcpy(
                    page->m_buffer + location.m_offset,
                    &currentUser,
                    sizeof(currentUser));
            }
        }

        const auto maximum = m_infoOffsets.m_maxLimitUser.find(serverId);
        if (maximum != m_infoOffsets.m_maxLimitUser.end()) {
            const PageOffsetType& location = maximum->second;
            CIOBuffer* const page = m_pages.at(location.m_pageNo);
            if (page != nullptr) {
                std::memcpy(
                    page->m_buffer + location.m_offset,
                    &maximumUser,
                    sizeof(maximumUser));
            }
        }

    }
}

// L2Authd.exe 0x0047CD70, Src/serverlist.cpp:319-333.
bool CServerListPacket6::GetPacketFrame(
    const int pageNumber,
    char*& packet,
    int& packetLength,
    bool& isMorePages) {
    const std::size_t pageCount = m_pages.size();
    const bool validPage =
        static_cast<unsigned int>(pageNumber) < pageCount;
    if (validPage) {
        {
            L2_READ_GUARD(m_lock);
            CIOBuffer* const page = m_pages[pageNumber];
            std::memcpy(packet, page->m_buffer, page->m_size);
            packetLength = static_cast<int>(page->m_size);
            isMorePages =
                static_cast<unsigned int>(pageNumber + 1) <
                m_pages.size();
        }
    }
    return validPage;
}

// L2Authd.exe 0x0047CE30, Src/serverlist.cpp:808-953.
void CServerList::MakeServerListFrame1() {
    char* const normalFrame = AllocateServerListFrame();
    normalFrame[0] = static_cast<char>(normalservernum);
    normalFrame[1] = '\0';
    char* normalCursor = normalFrame + 2;

    char* const revivalFrame = AllocateServerListFrame();
    revivalFrame[0] = static_cast<char>(revivalservernum);
    revivalFrame[1] = '\0';
    char* revivalCursor = revivalFrame + 2;

    for (int index = 1; index <= servernum; ++index) {
        const WorldServer current = serverlist.at(index - 1);
        const bool revival =
            (static_cast<std::uint32_t>(current.serverKind) & 0x80U) != 0;
        char* record = revival ? revivalCursor : normalCursor;
        record[0] = current.serverid;
        std::memcpy(record + 1, &current.outer_addr, 4);
        std::memcpy(record + 5, &current.port, 4);
        record[9] = current.ageLimit;
        record[10] = static_cast<char>(current.pkflag);
        if (revival) {
            revivalCursor = record + 0x10;
        } else {
            normalCursor = record + 0x10;
        }
    }

    ReplaceServerListFrame(
        ServerListPacketFrame[1],
        ServerListLen[1],
        normalFrame,
        normalservernum * 0x10 + 2);
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[1],
        RevivalServerListLen[1],
        revivalFrame,
        revivalservernum * 0x10 + 2);
}

// L2Authd.exe 0x0047D250, Src/serverlist.cpp:956-1129.
void CServerList::MakeServerListFrame2() {
    char* const normalFrame = AllocateServerListFrame();
    normalFrame[0] = static_cast<char>(normalservernum);
    normalFrame[1] = '\0';
    char* normalCursor = normalFrame + 2;

    char* const revivalFrame = AllocateServerListFrame();
    revivalFrame[0] = static_cast<char>(revivalservernum);
    revivalFrame[1] = '\0';
    char* revivalCursor = revivalFrame + 2;

    for (int index = 1; index <= servernum; ++index) {
        const WorldServer current = serverlist.at(index - 1);
        const bool revival =
            (static_cast<std::uint32_t>(current.serverKind) & 0x80U) != 0;
        char* record = revival ? revivalCursor : normalCursor;
        record[0] = current.serverid;
        WriteFrame2ServerName(record + 1, current.name);
        std::memcpy(record + 0x29, &current.outer_addr, 4);
        std::memcpy(record + 0x2d, &current.port, 4);
        record[0x31] = current.ageLimit;
        record[0x32] = static_cast<char>(current.pkflag);
        if (revival) {
            revivalCursor = record + 0x38;
        } else {
            normalCursor = record + 0x38;
        }
    }

    ReplaceServerListFrame(
        ServerListPacketFrame[2],
        ServerListLen[2],
        normalFrame,
        normalservernum * 0x38 + 2);
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[2],
        RevivalServerListLen[2],
        revivalFrame,
        revivalservernum * 0x38 + 2);
}

// L2Authd.exe 0x0047D6F0, Src/serverlist.cpp:1132-1283.
void CServerList::MakeServerListFrame3() {
    char* const normalFrame = AllocateServerListFrame();
    normalFrame[0] = static_cast<char>(normalservernum);
    normalFrame[1] = '\0';
    char* normalCursor = normalFrame + 2;

    char* const revivalFrame = AllocateServerListFrame();

    // This is the selected image's shipping behavior at 0x0047D7D2: both
    // the revival header/cursor and the normal header/cursor use normalFrame.
    // Revival records can therefore overwrite normal records, while the
    // separately published revivalFrame remains zero-filled.
    normalFrame[0] = static_cast<char>(revivalservernum);
    normalFrame[1] = '\0';
    char* revivalCursor = normalFrame + 2;

    for (int index = 1; index <= servernum; ++index) {
        const WorldServer current = serverlist.at(index - 1);
        const bool revival =
            (static_cast<std::uint32_t>(current.serverKind) & 0x80U) != 0;
        char* record = revival ? revivalCursor : normalCursor;
        record[0] = current.serverid;
        std::memcpy(record + 1, &current.outer_addr, 4);
        std::memcpy(record + 5, &current.port, 4);
        record[9] = current.ageLimit;
        record[10] = static_cast<char>(current.pkflag);
        std::memcpy(record + 0x10, &current.serverKind, 4);
        if (revival) {
            revivalCursor = record + 0x14;
        } else {
            normalCursor = record + 0x14;
        }
    }

    ReplaceServerListFrame(
        ServerListPacketFrame[3],
        ServerListLen[3],
        normalFrame,
        normalservernum * 0x14 + 2);
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[3],
        RevivalServerListLen[3],
        revivalFrame,
        revivalservernum * 0x14 + 2);
}

// L2Authd.exe 0x0047DB30, Src/serverlist.cpp:1286-1441.
void CServerList::MakeServerListFrame4() {
    char* const normalFrame = AllocateServerListFrame();
    normalFrame[0] = static_cast<char>(normalservernum);
    normalFrame[1] = '\0';
    char* normalCursor = normalFrame + 2;

    char* const revivalFrame = AllocateServerListFrame();
    revivalFrame[0] = static_cast<char>(revivalservernum);
    revivalFrame[1] = '\0';
    char* revivalCursor = revivalFrame + 2;

    for (int index = 1; index <= servernum; ++index) {
        const WorldServer current = serverlist.at(index - 1);
        const bool revival =
            (static_cast<std::uint32_t>(current.serverKind) & 0x80U) != 0;
        char* record = revival ? revivalCursor : normalCursor;
        record[0] = current.serverid;
        std::memcpy(record + 1, &current.outer_addr, 4);
        std::memcpy(record + 5, &current.port, 4);
        record[9] = current.ageLimit;
        record[10] = static_cast<char>(current.pkflag);
        std::memcpy(record + 0x10, &current.serverKind, 4);
        record[0x14] = current.region;
        if (revival) {
            revivalCursor = record + 0x15;
        } else {
            normalCursor = record + 0x15;
        }
    }

    ReplaceServerListFrame(
        ServerListPacketFrame[4],
        ServerListLen[4],
        normalFrame,
        normalservernum * 0x15 + 2);
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[4],
        RevivalServerListLen[4],
        revivalFrame,
        revivalservernum * 0x15 + 2);
}

// L2Authd.exe 0x0047DF90, Src/serverlist.cpp:1445-1491.
void CServerList::MakeServerListFrame5() {
    char* const normalFrame = AllocateServerListFrame();
    if (ServerListPacketFrame[4] != nullptr) {
        std::memcpy(
            normalFrame,
            ServerListPacketFrame[4],
            kServerListFrameCapacity);
    }
    ReplaceServerListFrame(
        ServerListPacketFrame[5],
        ServerListLen[5],
        normalFrame,
        ServerListLen[4]);

    char* const revivalFrame = AllocateServerListFrame();
    // The selected EXE tests the normal frame-4 pointer before copying the
    // revival frame, instead of testing RevivalServerListPacketFrame[4].
    if (ServerListPacketFrame[4] != nullptr) {
        std::memcpy(
            revivalFrame,
            RevivalServerListPacketFrame[4],
            kServerListFrameCapacity);
    }
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[5],
        RevivalServerListLen[5],
        revivalFrame,
        RevivalServerListLen[4]);
}

// L2Authd.exe 0x0047E1F0, Src/serverlist.cpp:1494-1496.
void CServerList::MakeServerListFrame6() {
    m_listPacket6.MakePacketFrame(serverlist);
}

// L2Authd.exe 0x0047E210, Src/serverlist.cpp:1499-1547.
void CServerList::MakeServerListFrame7() {
    char* const normalFrame = AllocateServerListFrame();
    if (ServerListPacketFrame[5] != nullptr) {
        std::memcpy(
            normalFrame,
            ServerListPacketFrame[5],
            kServerListFrameCapacity);
    }
    ReplaceServerListFrame(
        ServerListPacketFrame[7],
        ServerListLen[7],
        normalFrame,
        ServerListLen[5]);

    char* const revivalFrame = AllocateServerListFrame();
    // Frame 7 repeats frame 5's normal-pointer guard bug unchanged.
    if (ServerListPacketFrame[5] != nullptr) {
        std::memcpy(
            revivalFrame,
            RevivalServerListPacketFrame[5],
            kServerListFrameCapacity);
    }
    ReplaceServerListFrame(
        RevivalServerListPacketFrame[7],
        RevivalServerListLen[7],
        revivalFrame,
        RevivalServerListLen[5]);
}

// L2Authd.exe 0x0047E480, Src/serverlist.cpp:1550-1583.
void CServerList::SetServerStatus(
    char serverId,
    char status) {
    {
        L2_WRITE_GUARD(mylock);
        m_lu4Status[static_cast<int>(serverId)] = status;
        const auto found = servermap.find(static_cast<int>(serverId));
        if (found != servermap.end()) {
            const std::uint32_t encoded =
                static_cast<std::uint32_t>(found->second);
            const bool normal =
                (encoded & kRevivalServerBit) == 0;
            const std::uint32_t slot = encoded & kServerSlotMask;
            if (slot != 0 && static_cast<int>(slot) <= servernum) {
                if (normal) {
                    ServerListPacketFrame[1][slot * 0x10 + 1] = status;
                    ServerListPacketFrame[3][slot * 0x14 - 3] = status;
                    ServerListPacketFrame[4][slot * 0x15 - 4] = status;
                    ServerListPacketFrame[5][slot * 0x15 - 4] = status;
                    m_listPacket6.SetServerStatus(serverId, status);
                    ServerListPacketFrame[7][slot * 0x15 - 4] = status;
                } else {
                    RevivalServerListPacketFrame[1][slot * 0x10 + 1] = status;
                    RevivalServerListPacketFrame[3][slot * 0x14 - 3] = status;
                    RevivalServerListPacketFrame[4][slot * 0x15 - 4] = status;
                    RevivalServerListPacketFrame[5][slot * 0x15 - 4] = status;
                    RevivalServerListPacketFrame[7][slot * 0x15 - 4] = status;
                }
            }
        }
    }
}

// L2Authd.exe 0x0047E690, Src/serverlist.cpp:620-626.
int CServerList::GetAgeLimit(int serverId) {
    const WorldServer serverInfo = GetAt(serverId);
    if (serverInfo.serverid == '\0') {
        return 0;
    }
    return static_cast<int>(serverInfo.ageLimit);
}

// L2Authd.exe 0x0047E700, Src/serverlist.cpp:570-590.
void CServerList::MapForbiddenServers(
    int age,
    char* forbiddenServers,
    unsigned int capacity) {
    const std::vector<WorldServer> servers = [this] {
        L2_READ_GUARD(mylock);
        return serverlist;
    }();

    for (const WorldServer& serverInfo : servers) {
        if (age < serverInfo.ageLimit) {
            const std::div_t location = std::div(
                static_cast<int>(serverInfo.serverid) - 1,
                8);
            const unsigned int byteIndex =
                static_cast<unsigned int>(location.quot);
            const unsigned char bit = static_cast<unsigned char>(
                1U << (static_cast<unsigned int>(location.rem) & 0x1fU));

            // The <= comparison is present in the selected image, including
            // its one-byte-outside-the-declared-capacity edge case.
            if (byteIndex <= capacity) {
                forbiddenServers[byteIndex] = static_cast<char>(
                    static_cast<unsigned char>(forbiddenServers[byteIndex]) |
                    bit);
            }
        }
    }
}

// L2Authd.exe 0x0047E810, Src/serverlist.cpp:596-617.
void CServerList::MapForbiddenServers(
    unsigned char* availableServers,
    int availableServerCount,
    char* forbiddenServers,
    unsigned int capacity) {
    if (availableServerCount == 0 || availableServers == nullptr) {
        return;
    }

    std::memset(forbiddenServers, 0, capacity);
    for (unsigned int index = 0; index < capacity; ++index) {
        forbiddenServers[index] = static_cast<char>(
            static_cast<unsigned char>(forbiddenServers[index]) ^ 0xffU);
    }

    for (int index = 0; index < availableServerCount; ++index) {
        const std::div_t location = std::div(
            static_cast<int>(availableServers[index]) - 1,
            8);
        const unsigned int byteIndex =
            static_cast<unsigned int>(location.quot);
        if (byteIndex <= capacity) {
            forbiddenServers[byteIndex] = static_cast<char>(
                static_cast<unsigned char>(forbiddenServers[byteIndex]) ^
                static_cast<unsigned char>(
                    1U <<
                    (static_cast<unsigned int>(location.rem) & 0x1fU)));
        }
    }
}

// L2Authd.exe 0x0047E8F0, Src/serverlist.cpp:630-637.
bool CServerList::IsRevivalServer(int serverId) {
    const WorldServer serverInfo = GetAt(serverId);
    return
        (static_cast<std::uint32_t>(serverInfo.serverKind) & 0x80U) != 0;
}

// L2Authd.exe 0x0047E960, Src/serverlist.cpp:345-369.
CServerList::CServerList()
    : ageLimit(100),
      packetlen(0),
      revivalpacketlen(0),
      servernum(0),
      normalservernum(0),
      revivalservernum(0) {
    std::memset(listpacket, 0, sizeof(listpacket));
    std::memset(revivallistpacket, 0, sizeof(revivallistpacket));
    for (int index = 0; index < 10; ++index) {
        ServerListPacketFrame[index] = nullptr;
        ServerListLen[index] = 0;
        RevivalServerListPacketFrame[index] = nullptr;
        RevivalServerListLen[index] = 0;
    }
}

// L2Authd.exe 0x0047EAF0, Src/serverlist.cpp:406-567.
void CServerList::ReloadServer() {
    std::optional<const std::vector<WorldServer>> previousServersStorage;
    std::optional<const std::map<int, int>> previousServerMapStorage;
    std::optional<std::vector<WorldServer>> loadedServersStorage;
    {
        L2_WRITE_GUARD(mylock);
        packetlen = 0;
        revivalpacketlen = 0;
        servernum = 0;
        normalservernum = 0;
        revivalservernum = 0;

        const auto& previousServers = previousServersStorage.emplace(serverlist);
        const auto& previousServerMap = previousServerMapStorage.emplace(totalservermap);
        serverlist.clear();
        servermap.clear();
        totalservermap.clear();

        int minimumAgeLimit = ageLimit;
        auto& loadedServers = loadedServersStorage.emplace();
        ReadServerListInfo(loadedServers);

        int allServerIndex = 0;
        for (const WorldServer& loadedServer : loadedServers) {
            WorldServer current = loadedServer;
            if (current.ageLimit < minimumAgeLimit) {
                minimumAgeLimit = static_cast<int>(current.ageLimit);
            }

            const int serverId = static_cast<int>(current.serverid);
            const auto previous = previousServerMap.find(serverId);
            if (previous != previousServerMap.end()) {
                const WorldServer& previousServer =
                    previousServers.at(previous->second - 1);
                if (std::memcmp(
                        &current.inner_addr,
                        &previousServer.inner_addr,
                        sizeof(current.inner_addr)) == 0) {
                    current.s = previousServer.s;
                    current.UserNum = previousServer.UserNum;
                }
            }

            serverlist.push_back(current);
            ++allServerIndex;
            const bool revival =
                (static_cast<std::uint32_t>(current.serverKind) & 0x80U) != 0;
            if (!revival) {
                char* const record =
                    listpacket + normalservernum * 9 + 1;
                std::memcpy(record, &current.serverid, 1);
                std::memcpy(record + 1, &current.outer_addr, 4);
                std::memcpy(record + 5, &current.port, 4);
                ++normalservernum;
                servermap.insert(std::make_pair(
                    serverId,
                    normalservernum));
            } else {
                char* const record =
                    revivallistpacket + revivalservernum * 9 + 1;
                std::memcpy(record, &current.serverid, 1);
                std::memcpy(record + 1, &current.outer_addr, 4);
                std::memcpy(record + 5, &current.port, 4);
                ++revivalservernum;
                servermap.insert(std::make_pair(
                    serverId,
                    EncodeRevivalSlot(revivalservernum)));
            }
            totalservermap.insert(std::make_pair(
                serverId,
                allServerIndex));

            // Log.AddLog(
            //     LOG_NORMAL,
            //     "Server %d loaded, innerip:%s,outerip:%s,port:%d,agelimit:%d,pk:%d,kind:%d,name:[%s],domain:[%s]",
            //     static_cast<int>(current.serverid),
            //     current.inner_ip,
            //     current.ip,
            //     current.port,
            //     static_cast<int>(current.ageLimit),
            //     static_cast<int>(current.pkflag),
            //     current.serverKind,
            //     current.name,
            //     current.domainName.c_str())
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=server_list event=world_configured server_id=%d inner_address=%.15s outer_address=%.15s port=%d age_limit=%d pk=%u kind=%d name=\"%.25s\" domain=\"%.253s\"",
            //     static_cast<int>(current.serverid), current.inner_ip, current.ip, current.port,
            //     static_cast<int>(current.ageLimit), static_cast<unsigned int>(current.pkflag),
            //     current.serverKind, current.name, current.domainName.c_str())
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[worlds] Server %d: internal address %.15s, client address %.15s:%d. Minimum age %d, PK flag %u, kind %d, name \"%.25s\", domain \"%.253s\".",
            //     static_cast<int>(current.serverid), current.inner_ip, current.ip, current.port,
            //     static_cast<int>(current.ageLimit), static_cast<unsigned int>(current.pkflag),
            //     current.serverKind, current.name, current.domainName.c_str())
            L2_LOG_INFO(Log,
                "[worlds] Server %d: internal address %.15s, client address %.15s:%d; minimum age %d, PK flag %u, kind %d, name \"%.25s\", domain \"%.253s\".",
                static_cast<int>(current.serverid), current.inner_ip, current.ip, current.port,
                static_cast<int>(current.ageLimit), static_cast<unsigned int>(current.pkflag),
                current.serverKind, current.name, current.domainName.c_str());
        }

        std::memcpy(listpacket, &normalservernum, 1);
        std::memcpy(revivallistpacket, &revivalservernum, 1);
        servernum = static_cast<int>(totalservermap.size());

        packetlen = normalservernum * 9 + 1;
        ServerListPacketFrame[0] = listpacket;
        ServerListLen[0] = packetlen;
        revivalpacketlen = revivalservernum * 9 + 1;
        RevivalServerListPacketFrame[0] = revivallistpacket;
        RevivalServerListLen[0] = revivalpacketlen;

        if (servermap.empty()) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Server load fail, please check server table or DB connection string")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=server_list event=empty_server_list source=%s previous_records=%zu action=publish_empty_list check=server_list_source_and_load_errors",
            //     config.ReadLocalServerList ? "local_file" : "database", previousServers.size())
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[worlds] The %s supplied no servers. Publishing an empty list (previously %zu records). Check the source and preceding load errors.",
            //     config.ReadLocalServerList ? "local file" : "database", previousServers.size())
            L2_LOG_ERROR(Log,
                "[worlds] The %s supplied no servers; publishing an empty list (previously %zu records). Check the source and preceding load errors.",
                config.ReadLocalServerList ? "local file" : "database", previousServers.size());
        }

        MakeServerListFrame1();
        MakeServerListFrame2();
        MakeServerListFrame3();
        MakeServerListFrame4();
        MakeServerListFrame5();
        MakeServerListFrame6();
        MakeServerListFrame7();
        if (!servermap.empty()) {
            // Log.AddLog(LOG_NORMAL,
            //     "component=server_list event=list_reloaded source=%s loaded_records=%zu unique_server_ids=%d normal_records=%d revival_records=%d previous_records=%zu availability=pending_status_check",
            //     config.ReadLocalServerList ? "local_file" : "database", loadedServers.size(),
            //     servernum, normalservernum, revivalservernum, previousServers.size())
            // Log.AddLog(LOG_NORMAL,
            //     "[worlds] Reloaded from %s: %zu records, %d unique server IDs, %d normal and %d revival records (previously %zu records). Availability checks are pending.",
            //     config.ReadLocalServerList ? "local file" : "database", loadedServers.size(),
            //     servernum, normalservernum, revivalservernum, previousServers.size())
            // L2_LOG_INFO(Log,
            //     "[worlds] Reloaded from %s: %zu records, %d unique server IDs, %d normal and %d revival records (previously %zu records). Availability checks are pending.",
            //     config.ReadLocalServerList ? "local file" : "database", loadedServers.size(),
            //     servernum, normalservernum, revivalservernum, previousServers.size())
            L2_LOG_INFO(Log,
                "[worlds] Applied %zu records returned by the %s loader: %d unique server IDs, %d normal and %d revival records (previously %zu records). Availability checks are pending.",
                loadedServers.size(), config.ReadLocalServerList ? "local file" : "database",
                servernum, normalservernum, revivalservernum, previousServers.size());
        }
        ageLimit = minimumAgeLimit;
    }

    CheckAllServerStatus();
}

// L2Authd.exe 0x0047F1C0, Src/serverlist.cpp:372-403.
CServerList::~CServerList() {
    for (int index = 1; index < 10; ++index) {
        if (ServerListPacketFrame[index] != nullptr) {
            ::operator delete(ServerListPacketFrame[index]);
        }
        if (RevivalServerListPacketFrame[index] != nullptr) {
            ::operator delete(RevivalServerListPacketFrame[index]);
        }
    }
}

// L2Authd.exe 0x0047F2E0, Src/serverlist.cpp:640-669.
std::vector<std::pair<WorldServer, char>> CServerList::Lu4Servers() {
    std::vector<std::pair<WorldServer, char>> servers;
    L2_READ_GUARD(mylock);
    for (int index = 1; index <= servernum; ++index) {
        const WorldServer& server = serverlist.at(index - 1);
        if ((static_cast<std::uint32_t>(server.serverKind) & 0x80U) != 0) {
            continue;
        }
        const auto status = m_lu4Status.find(static_cast<int>(server.serverid));
        servers.emplace_back(server, status == m_lu4Status.end() ? 0 : status->second);
    }
    return servers;
}

WorldServer CServerList::GetAt(int serverId) {
    // keep the copied WorldServer alive after unlock.
    // L2Authd.exe: copy 0x0047f3be, unlock 0x0047f3d3, return copy
    // 0x0047f3e2, then snapshot destruction 0x0047f403.
    std::optional<const WorldServer> foundServer;
    {
        L2_READ_GUARD(mylock);
        const auto found = totalservermap.find(serverId);
        if (found != totalservermap.end()) {
            const int index = found->second;
            if (index <= servernum && index > 0) {
                foundServer.emplace(serverlist.at(index - 1));
            }
        }
    }
    if (foundServer) {
        return *foundServer;
    }

    // The original default constructor initializes only std::string here
    // the miss path then writes exactly these two scalar fields.
    WorldServer result;
    result.serverid = '\0';
    result.inner_addr.s_addr = 0;
    return result;
}

// L2Authd.exe 0x0047F490, Src/serverlist.cpp:688-696.
bool CServerList::ExistId(int serverId) {
    bool exists = false;
    {
        L2_READ_GUARD(mylock);
        exists = servermap.find(serverId) != servermap.end();
    }
    return exists;
}

// L2Authd.exe 0x0047F500, Src/serverlist.cpp:720-753.
int CServerList::SetServerSocket(
    in_addr address,
    unsigned int socket) {
    std::int8_t result = 0;
    {
        L2_WRITE_GUARD(mylock);
        for (WorldServer& current : serverlist) {
            if (std::memcmp(
                    &current.inner_addr,
                    &address,
                    sizeof(address)) != 0) {
                continue;
            }

            if (current.s == kInvalidSocket) {
                current.s = socket;
                result = static_cast<std::int8_t>(current.serverid);
            } else if (current.s == socket) {
                current.s = kInvalidSocket;
                result = static_cast<std::int8_t>(current.serverid);
            } else {
                result = -1;
            }
            break;
        }
    }
    return static_cast<int>(result);
}

// L2Authd.exe 0x0047F610, Src/serverlist.cpp:756-805.
int CServerList::SetServerUserNum(
    [[maybe_unused]] in_addr address,
    int userCount,
    char serverId) {
    const unsigned char* packet =
        reinterpret_cast<const unsigned char*>(&userCount);
    std::uint16_t currentUser = static_cast<std::uint16_t>(
        GetShortFromPacket(&packet));
    std::uint16_t maximumUser = static_cast<std::uint16_t>(
        GetShortFromPacket(&packet));

    if (config.changeServerListUsersInPercentage && maximumUser != 0) {
        currentUser = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(currentUser) * 100U /
            static_cast<std::uint32_t>(maximumUser));
        maximumUser = 100;
        userCount =
            static_cast<int>(currentUser) |
            0x00640000;
    }

    {
        L2_READ_GUARD(mylock);
        const auto found = servermap.find(static_cast<int>(serverId));
        if (found != servermap.end()) {
            const std::uint32_t encoded =
                static_cast<std::uint32_t>(found->second);
            const bool normal =
                (encoded & kRevivalServerBit) == 0;
            const std::uint32_t slot = encoded & kServerSlotMask;
            if (slot != 0 && static_cast<int>(slot) <= servernum) {
                if (normal) {
                    std::memcpy(
                        ServerListPacketFrame[1] + slot * 0x10 - 3,
                        &userCount,
                        4);
                    std::memcpy(
                        ServerListPacketFrame[3] + slot * 0x14 - 7,
                        &userCount,
                        4);
                    std::memcpy(
                        ServerListPacketFrame[4] + slot * 0x15 - 8,
                        &userCount,
                        4);
                    std::memcpy(
                        ServerListPacketFrame[5] + slot * 0x15 - 8,
                        &userCount,
                        4);
                    m_listPacket6.SetUserNum(
                        serverId,
                        static_cast<short>(currentUser),
                        static_cast<short>(maximumUser));
                    std::memcpy(
                        ServerListPacketFrame[7] + slot * 0x15 - 8,
                        &userCount,
                        4);
                } else {
                    std::memcpy(
                        RevivalServerListPacketFrame[1] + slot * 0x10 - 3,
                        &userCount,
                        4);
                    std::memcpy(
                        RevivalServerListPacketFrame[3] + slot * 0x14 - 7,
                        &userCount,
                        4);
                    std::memcpy(
                        RevivalServerListPacketFrame[4] + slot * 0x15 - 8,
                        &userCount,
                        4);
                    std::memcpy(
                        RevivalServerListPacketFrame[5] + slot * 0x15 - 8,
                        &userCount,
                        4);
                    std::memcpy(
                        RevivalServerListPacketFrame[7] + slot * 0x15 - 8,
                        &userCount,
                        4);
                }
            }
        }
    }
    return static_cast<int>(serverId);
}

// L2Authd.exe 0x0047F910, Src/serverlist.cpp:1586-1596.
void CServerList::CheckAllServerStatus() {
    for (int index = 1; index <= servernum; ++index) {
        const WorldServer current = [this, index] {
            L2_READ_GUARD(mylock);
            return serverlist.at(index - 1);
        }();
        if (server.GetServerStatus(current.inner_addr)) {
            SetServerStatus(current.serverid, '\x01');
        }
    }
}

// L2Authd.exe 0x0047FA20, Src/serverlist.cpp:1599-1633.
bool CServerList::GetServerStatus(char serverId) {
    char status = '\0';
    {
        L2_READ_GUARD(mylock);
        const auto found = servermap.find(static_cast<int>(serverId));
        if (found != servermap.end()) {
            const std::uint32_t encoded =
                static_cast<std::uint32_t>(found->second);
            const bool normal =
                (encoded & kRevivalServerBit) == 0;
            const std::uint32_t slot = encoded & kServerSlotMask;
            if (slot != 0 && static_cast<int>(slot) <= servernum) {
                if (normal) {
                    status = ServerListPacketFrame[1][slot * 0x10 + 1];
                } else {
                    status =
                        RevivalServerListPacketFrame[1][slot * 0x10 + 1];
                }
            }
        }
    }
    return status == '\x01';
}

// L2Authd.exe 0x0047FB30, Src/serverlist.cpp:703-712.
bool CServerList::CheckBeginnerWorld(
    [[maybe_unused]] int serverId) {
    {
        L2_READ_GUARD(mylock);
    }
    return false;
}

// L2Authd.exe 0x0047FB60, Src/serverlist.cpp:1644-1659.
CServerUserCountStatus::CServerUserCountStatus() {
    authd::platform::InitializeCriticalSection(count_section);
    CountID = static_cast<std::int32_t>(
        runtime::TickCount32());
    {
        L2_GUARD(count_section);
        for (int index = 0; index < 128; ++index) {
            wun[index].limituser = 0;
            wun[index].playuser = 0;
            wun[index].serverid = static_cast<char>(index);
        }
    }

    // g_UserCount is zero-initialized before this constructor in the target.
    // keep that failure value explicit at the portable timer boundary.
    m_timer = nullptr;
    // runtime::timer_queue::CreateTimer(
    //     &m_timer,
    //     nullptr,
    //     UserCountTimerRoutine,
    //     this,
    //     300000,
    //     300000)
    const bool timerCreated = runtime::timer_queue::CreateTimer(
        &m_timer,
        nullptr,
        UserCountTimerRoutine,
        this,
        300000,
        300000);
    if (!timerCreated) {
        // This owner is constructed before main. Global CLog may not exist yet.
        // runtime::logging::Write("authd", runtime::logging::Level::Error,
        //     "[metrics] Could not start the five-minute player-count timer, automatic statistics updates will not run.")
        runtime::logging::Write("authd", runtime::logging::Level::Error,
            "[metrics] Could not start the five-minute player-count timer; automatic statistics updates will not run.", __FILE__);
    }
}

// L2Authd.exe 0x0047FC20, Src/serverlist.cpp:1662-1665.
CServerUserCountStatus::~CServerUserCountStatus() {
    // The selected image destroys these in this order, even though deleting
    // the critical section before its timer is a legacy shutdown race.
    authd::platform::DeleteCriticalSection(count_section);
    // runtime::timer_queue::DeleteTimer(nullptr, m_timer)
    const bool timerRemoved = runtime::timer_queue::DeleteTimer(nullptr, m_timer);
    if (m_timer != nullptr && !timerRemoved) {
        // CRT shutdown must not depend on the lifetime of global CLog.
        // runtime::logging::Write("authd", runtime::logging::Level::Warning,
        //     "[metrics] Player-count timer removal was not confirmed during shutdown.")
        runtime::logging::Write("authd", runtime::logging::Level::Warning,
            "[metrics] Player-count timer removal was not confirmed during shutdown.", __FILE__);
    }
}

// L2Authd.exe 0x0047FC50, Src/serverlist.cpp:1668-1677.
void CServerUserCountStatus::UpdateUserNum(
    unsigned char serverId,
    short playUser,
    short limitUser) {
    if (serverId < 128) {
        {
            L2_GUARD(count_section);
            wun[serverId].limituser = static_cast<int>(limitUser);
            wun[serverId].playuser = static_cast<int>(playUser);
        }
    }
}

// L2Authd.exe 0x0047FCC0, Src/serverlist.cpp:1691-1720.
void CServerUserCountStatus::UpdateAllServerNumToDB() {
    CDBConn connection(g_linDB);
    std::array<bool, 128> failedServers{};
    unsigned int attempted = 0;
    SetCounter();
    for (int serverIndex = 0; serverIndex < 128; ++serverIndex) {
        bool shouldExecute = false;
        char statement[512];

        {
            L2_GUARD(count_section);
            if (wun[serverIndex].limituser > 0) {
                shouldExecute = true;
                connection.BindParam(&serverIndex, 1);
                connection.BindParam(&wun[serverIndex].playuser, 1);
                connection.BindParam(&wun[serverIndex].limituser, 1);
                connection.BindParam(&wun[0].playuser, 1);
                connection.BindParam(&reporter.m_SocketCount, 1);
                std::snprintf(
                    statement,
                    sizeof(statement),
                    "{call ap_SetConcurrentUserStatistics (?, ?, ?, ?, ?)}");
            }
        }

        if (shouldExecute) {
            ++attempted;
            // connection.Execute(statement)
            const bool confirmed = connection.Execute(statement);
            if (!confirmed) failedServers[static_cast<std::size_t>(serverIndex)] = true;
            connection.ResetHtmt();
        }
    }
    // Noise: Summarize the failed batch once. CDBConn already reports each SQL error.
    ReportConcurrentUserStatisticsFailures(failedServers, attempted);
}

// L2Authd.exe 0x0047FE90, Src/serverlist.cpp:1682-1689.
void CServerUserCountStatus::SetCounter() {
    ++CountID;
    {
        L2_GUARD(count_section);
        wun[0].limituser = config.SocketLimit;
        wun[0].playuser = reporter.m_UserCount;
    }
}

// L2Authd.exe 0x0047FF00, Src/serverlist.cpp:1724-1729.
int SetServerSocket(
    in_addr address,
    std::uint32_t socket) {
    return g_ServerList.SetServerSocket(address, socket);
}

// L2Authd.exe 0x0047FF30, Src/serverlist.cpp:1732-1734.
void SetServerStatus(
    char serverId,
    char status) {
    g_ServerList.SetServerStatus(serverId, status);
}

// L2Authd.exe 0x0047FF50, Src/serverlist.cpp:1737-1743.
bool GetServerStatus(char serverId) {
    return g_ServerList.GetServerStatus(serverId);
}

// L2Authd.exe 0x0047FF80, Src/serverlist.cpp:1746-1751.
WorldServer GetWorldServer(char serverId) {
    WorldServer result;
    result = g_ServerList.GetAt(static_cast<int>(serverId));
    return result;
}

// L2Authd.exe 0x00480060, Src/serverlist.cpp:1754-1756.
void SetServerUserNum(
    in_addr address,
    int userCount,
    char serverId) {
    g_ServerList.SetServerUserNum(
        address,
        userCount,
        serverId);
}

// L2Authd.exe 0x00480080, Src/serverlist.cpp:1759-1852.
void ReadServerListInfo(std::vector<WorldServer>& servers) {
    WorldServer serverInfo;
    if (!config.ReadLocalServerList) {
        CDBConn connection(g_linDB);
        connection.Bind(&serverInfo.serverid);
        connection.Bind(serverInfo.name, 0x1a);
        connection.Bind(serverInfo.ip, 0x10);
        connection.Bind(serverInfo.inner_ip, 0x10);
        connection.Bind(&serverInfo.ageLimit);
        connection.Bind(&serverInfo.pkflag);
        connection.Bind(&serverInfo.serverKind);
        connection.Bind(&serverInfo.port);
        connection.Bind(&serverInfo.region);

        std::array<char, 0xfe> domainName{};
        connection.Bind(
            domainName.data(),
            static_cast<int>(domainName.size()));

        if (connection.Execute("{call ap_GetServers}")) {
            bool noMoreRows = false;
            while (connection.Fetch(noMoreRows)) {
                serverInfo.inner_addr.s_addr =
                    ::inet_addr(serverInfo.inner_ip);
                serverInfo.outer_addr.s_addr =
                    ::inet_addr(serverInfo.ip);
                serverInfo.s = kInvalidSocket;
                serverInfo.name[0x19] = '\0';
                serverInfo.UserNum = 0;
                serverInfo.domainName = domainName.data();
                std::memset(
                    domainName.data(),
                    0,
                    domainName.size());
                servers.push_back(serverInfo);
            }
            if (!noMoreRows) {
                // Log.AddLog(LOG_ERROR,
                //     "component=server_list event=load_failed source=database operation=ap_GetServers stage=fetch loaded_records=%zu action=return_rows_read_so_far",
                //     servers.size())
                // Log.AddLog(LOG_ERROR,
                //     "[worlds] Could not fetch ap_GetServers results. Keeping the %zu rows read so far.",
                //     servers.size())
                L2_LOG_ERROR(Log,
                    "[worlds] Could not fetch ap_GetServers results. Keeping the %zu rows read so far.",
                    servers.size());
            }
        } else {
            // Log.AddLog(LOG_ERROR,
            //     "component=server_list event=load_failed source=database operation=ap_GetServers stage=execute loaded_records=%zu action=return_rows_read_so_far",
            //     servers.size())
            // Log.AddLog(LOG_ERROR,
            //     "[worlds] Could not execute ap_GetServers. Returning with %zu rows loaded.",
            //     servers.size())
            L2_LOG_ERROR(Log,
                "[worlds] Could not execute ap_GetServers. Returning with %zu rows loaded.",
                servers.size());
        }
        return;
    }

#if defined(_WIN32)
    constexpr const char* serverListPath = "etc\\serverlist";
#else
    // fopen is the only host-filesystem boundary in this source unit.
    constexpr const char* serverListPath = "etc/serverlist";
#endif
    // std::FILE* const file = std::fopen(
    //     runtime::ConfigFile(serverListPath, "serverlist").string().c_str(), "rt")
    const std::string serverListFile =
        runtime::ConfigFile(serverListPath, "serverlist").string();
    std::FILE* const file = std::fopen(serverListFile.c_str(), "rt");
    if (file == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=server_list event=load_failed source=local_file path=\"%s\" stage=open error_domain=errno code=%d action=return_without_loading",
        //     serverListFile.c_str(), errno)
        // Log.AddLog(LOG_ERROR,
        //     "[worlds] Could not open server-list file \"%s\" (errno %d). No records loaded.",
        //     serverListFile.c_str(), errno)
        L2_LOG_ERROR(Log,
            "[worlds] Could not open server-list file \"%s\" (errno %d). No records loaded.",
            serverListFile.c_str(), errno);
        return;
    }

    std::array<char, 1024> lineBuffer{};
    char* line = std::fgets(
        lineBuffer.data(),
        static_cast<int>(lineBuffer.size()),
        file);
    while (line != nullptr || std::feof(file) == 0) {
        if (std::strlen(lineBuffer.data()) < 11) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "can't load serverlist file")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=server_list event=load_failed source=local_file path=\"%s\" stage=read_record reason=%s record_index=%zu loaded_records=%zu action=return_rows_read_so_far",
            //     serverListFile.c_str(), std::ferror(file) != 0 ? "read_error" : "record_too_short",
            //     servers.size() + 1, servers.size())
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[worlds] Could not read \"%s\": %s at record %zu. Keeping the %zu rows read so far.",
            //     serverListFile.c_str(), std::ferror(file) != 0 ? "read error" : "record too short",
            //     servers.size() + 1, servers.size())
            L2_LOG_ERROR(Log,
                "[worlds] Could not read \"%s\": %s at record %zu. Keeping the %zu rows read so far.",
                serverListFile.c_str(), std::ferror(file) != 0 ? "read error" : "record too short",
                servers.size() + 1, servers.size());

            // The selected function returns without fclose on this malformed
            // line path. Preserve that observable legacy resource leak.
            return;
        }

        char* token = std::strtok(lineBuffer.data(), ",");
        serverInfo.serverid = static_cast<char>(std::atoi(token));

        token = std::strtok(nullptr, ",");
        std::strcpy(serverInfo.name, token);

        token = std::strtok(nullptr, ",");
        serverInfo.inner_addr.s_addr = ::inet_addr(token);
        std::strcpy(serverInfo.inner_ip, token);

        token = std::strtok(nullptr, ",");
        std::strcpy(serverInfo.ip, token);

        token = std::strtok(nullptr, ",");
        serverInfo.ageLimit = static_cast<char>(std::atoi(token));

        token = std::strtok(nullptr, ",");
        serverInfo.serverKind = std::atoi(token);

        token = std::strtok(nullptr, ",");
        serverInfo.pkflag = static_cast<unsigned char>(
            std::atoi(token));

        token = std::strtok(nullptr, ",");
        serverInfo.port = std::atoi(token);

        token = std::strtok(nullptr, ",");
        serverInfo.region = static_cast<char>(std::atoi(token));

        serverInfo.outer_addr.s_addr =
            ::inet_addr(serverInfo.ip);
        serverInfo.s = kInvalidSocket;
        serverInfo.name[0x19] = '\0';
        serverInfo.UserNum = 0;
        serverInfo.domainName = "";
        servers.push_back(serverInfo);

        std::memset(lineBuffer.data(), 0, lineBuffer.size());
        line = std::fgets(
            lineBuffer.data(),
            static_cast<int>(lineBuffer.size()),
            file);
    }
    // std::fclose(file)
    const int closeResult = std::fclose(file);
    if (closeResult != 0) {
        // Log.AddLog(LOG_WARN,
        //     "[worlds] Could not close server-list file \"%s\" after reading %zu records (errno %d). The loaded records are retained.",
        //     serverListFile.c_str(), servers.size(), errno)
        L2_LOG_WARN(Log,
            "[worlds] Could not close server-list file \"%s\" after reading %zu records (errno %d). The loaded records are retained.",
            serverListFile.c_str(), servers.size(), errno);
    }
}

// L2Authd.exe 0x004805D0, Src/serverlist.cpp:1636-1641.
#if defined(_WIN32)
void __stdcall UserCountTimerRoutine(
#else
void UserCountTimerRoutine(
#endif
    void* parameter,
    [[maybe_unused]] std::uint8_t timerOrWaitFired) {
    const std::uint32_t webAttempts = static_cast<std::uint32_t>(
        runtime::raw_atomic::LoadI32Relaxed(&reporter.m_WebLoginTry));
    const std::uint32_t clientAttempts = static_cast<std::uint32_t>(
        runtime::raw_atomic::LoadI32Relaxed(&reporter.m_ClientLoginTry));
    // Noise: Skip empty login-attempt samples. The database statistics update still runs.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "WebLoginTry : %u,ClientLoginTry : %u",
    //     reporter.m_WebLoginTry,
    //     reporter.m_ClientLoginTry)
    if (webAttempts != 0 || clientAttempts != 0) {
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=metrics event=login_attempts web_counter=%u client_counter=%u counter_scope=process_lifetime counter_bits=32 sample_interval_ms=300000",
        //     webAttempts, clientAttempts)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[metrics] Login attempts since process start: web %u, client %u (32-bit counters, sampled every five minutes).",
        //     webAttempts, clientAttempts)
        L2_LOG_INFO(Log,
            "[metrics] Login attempts since process start: web %u, client %u (32-bit counters, sampled every five minutes).",
            webAttempts, clientAttempts);
    }
    static_cast<CServerUserCountStatus*>(parameter)->
        UpdateAllServerNumToDB();
}
