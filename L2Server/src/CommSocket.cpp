#include "CommSocket.h"
#include "CConfig.h"
#include "ClanBbs.h"
#include "CommCli.h"
#include "Community.h"
#include "DB.h"
#include "Memo.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif

namespace {

void StoreLittleEndian16(std::uint8_t* const destination, const std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

struct ResolvedCommUser {
    CSPointer<User> hold;
    User* user = nullptr;
    CUserSocket* socket = nullptr;
};

ResolvedCommUser ResolveCommUser(const int userServerId, const int) {
    ResolvedCommUser result;
    result.hold = g_UserMap.FindObject(userServerId);
    if (result.hold && result.hold->IsPC()) {
        // Current callbacks call CastUser twice.
        result.user = result.hold->CastUser();
        User* const socketOwner = result.hold->CastUser();
        result.socket = socketOwner->GetSocket();
        return result;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid user[%x]",
    //     L"commsocket.cpp",
    //     sourceLine,
    //     userServerId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid user[%x]",
    //     L"commsocket.cpp",
    //     sourceLine,
    //     userServerId)
    L2_LOG_WARN(
        Log, "Community reply cannot find player object 0x%08x; no client socket is resolved.",
        static_cast<unsigned>(userServerId)
    );
    return result;
}

SharedUtilsWideChar g_emptyTargetWide[1]{};

} // namespace

bool CommCheckVersion(CCommSocket*, const std::uint8_t*);
bool CommBBSMakeBoardPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSGetBoardListPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSModBoardPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSDelBoardPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSWriteArticlePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSReadArticlePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSGetArticleListPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSReadArticleThreadPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSGetCommentPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSReplyFormPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSWriteCommentPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSWriteFormPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSSearchPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSModifyFormpacket(CCommSocket*, const std::uint8_t*);
bool CommBBSModifyPacket(CCommSocket*, const std::uint8_t*);
bool CommBBSDeletePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSGetFavoritePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSAddFavoritePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSDelFavoritePacket(CCommSocket*, const std::uint8_t*);
bool CommBBSGetAnnouncePacket(CCommSocket*, const std::uint8_t*);
bool CommManMemoPacket(CCommSocket*, const std::uint8_t*);
bool CommManPledgeBBSPacket(CCommSocket*, const std::uint8_t*);
bool CommSysinfoPacket(CCommSocket*, const std::uint8_t*);
bool CommDelClanBoardPacket(CCommSocket*, const std::uint8_t*);
bool CommPledgeMasterTransferPacket(CCommSocket*, const std::uint8_t*);
bool CommDummyPacket(CCommSocket*, const std::uint8_t*);

CommCallback g_CommPacketTable[27] = {
    CommCheckVersion,
    CommBBSMakeBoardPacket,
    CommBBSGetBoardListPacket,
    CommBBSModBoardPacket,
    CommBBSDelBoardPacket,
    CommBBSWriteArticlePacket,
    CommBBSReadArticlePacket,
    CommBBSGetArticleListPacket,
    CommBBSReadArticleThreadPacket,
    CommBBSGetCommentPacket,
    CommBBSReplyFormPacket,
    CommBBSWriteCommentPacket,
    CommBBSWriteFormPacket,
    CommBBSSearchPacket,
    CommBBSModifyFormpacket,
    CommBBSModifyPacket,
    CommBBSDeletePacket,
    CommBBSGetFavoritePacket,
    CommBBSAddFavoritePacket,
    CommBBSDelFavoritePacket,
    CommBBSGetAnnouncePacket,
    CommManMemoPacket,
    CommManPledgeBBSPacket,
    CommSysinfoPacket,
    CommDelClanBoardPacket,
    CommPledgeMasterTransferPacket,
    CommDummyPacket,
};

CCommSocket::CCommSocket(const std::uint64_t socket, CommCallback* const packetTable) :
    CIOSocket(socket, 600),
    m_socket(socket),
    m_threadCount(0),
    m_alignmentBC(0),
    m_packetTable(packetTable),
    m_mode(SM_READ_LEN),
    m_packetLength(0) {
    // Current L2Server 0x005bd768.
    L2_GUARD_FUNCTION();
}

CCommSocket::~CCommSocket() { m_packetTable = nullptr; }

void CCommSocket::Send(const char* const format, ...) {
    // Current L2Server 0x005bac84.
    L2_GUARD_FUNCTION();
    std::va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CCommSocket::SendV(const char* const format, std::va_list arguments) {
    // Current L2Server 0x005bdc48.
    L2_GUARD_FUNCTION();
    if (m_mode == SM_CLOSE) {
        return;
    }
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const std::uint16_t bodySize = static_cast<std::uint16_t>(
        ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments)
    );
    std::uint16_t frameSize = bodySize;
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        // L2_LOG_ERROR(Log,
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        L2_LOG_ERROR(
            Log,
            "Community socket 0x%llx could not assemble the outgoing packet (format %s); the zero-length buffer is still "
            "passed to Write.",
            static_cast<unsigned long long>(m_socket), format != nullptr ? format : "<null>"
        );
    } else {
        frameSize = static_cast<std::uint16_t>(bodySize + 2);
        if (m_packetTable == g_CommPacketTable) {
            frameSize = static_cast<std::uint16_t>(bodySize + 4);
            StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
            StoreLittleEndian16(buffer->m_Buffer.data() + frameSize - 2, frameSize);
        } else {
            StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
        }
    }
    buffer->m_dwSize = frameSize;
    Write(buffer);
}

void CCommSocket::OnClose() {
    // Current L2Server 0x005baba0.
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_WARN,
    //     L"CCommSocket Close %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // L2_LOG_WARN(Log,
    //     L"CCommSocket Close %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    L2_LOG_WARN(
        Log, "Community socket 0x%llx closed; marking the client disconnected and clearing this socket handle.",
        static_cast<unsigned long long>(m_socket)
    );
    m_mode = SM_CLOSE;
    CCommCli::Inst()->SetCliStatus(4);
    runtime::raw_atomic::ExchangeU32(reinterpret_cast<volatile std::uint32_t*>(&m_socket), 0);
}

void CCommSocket::OnRead() {
    // Current L2Server 0x005bd9c0.
    L2_GUARD_FUNCTION();
    if (m_mode == SM_CLOSE) {
        Close();
        return;
    }
    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_mode == SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                             ) -
                2;
            if (m_packetLength < 1 || m_packetLength > 0x4ffe) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"dbsocket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     m_packetLength)
                // L2_LOG_ERROR(Log,
                //     L"dbsocket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     m_packetLength)
                L2_LOG_ERROR(
                    Log,
                    "Community socket 0x%llx received invalid body length %d; expected 1 through 20478 bytes. Closing the "
                    "connection.",
                    static_cast<unsigned long long>(m_socket), m_packetLength
                );
                m_mode = SM_CLOSE;
                Close();
                return;
            }
            m_mode = SM_READ_BODY;
            packetOffset += 2;
        } else if (m_mode != SM_READ_BODY) {
            Close();
            return;
        }
        if (readSize < packetOffset + m_packetLength) {
            Read(readSize - packetOffset);
            return;
        }
        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (m_packetTable == g_CommPacketTable && opcode > 0x19) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[commsocket]unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     L"[commsocket]unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "Community socket 0x%llx received unsupported opcode 0x%02x; closing the connection.",
                static_cast<unsigned long long>(m_socket), static_cast<unsigned>(opcode)
            );
            m_mode = SM_CLOSE;
            Close();
            return;
        }
        CCommPacket* const packet = new CCommPacket;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);
        packetOffset += m_packetLength;
        m_mode = SM_READ_LEN;
    }
    Read(0);
}

bool CommCheckVersion(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bde30.
    L2_GUARD_FUNCTION();
    int requestedVersion = 0;
    int serverVersion = 0;
    int serverValue = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &requestedVersion, &serverVersion, &serverValue);
    if (requestedVersion == serverVersion) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"l2comm server connected(%d, %d, %d)",
        //     requestedVersion,
        //     serverVersion,
        //     serverValue)
        // L2_LOG_INFO(Log,
        //     L"l2comm server connected(%d, %d, %d)",
        //     requestedVersion,
        //     serverVersion,
        //     serverValue)
        L2_LOG_INFO(
            Log,
            "Community versions match: requested %d, peer %d, peer value %d. Loading world names before marking the client "
            "sendable.",
            requestedVersion, serverVersion, serverValue
        );
        int worldCount = 0;
        cursor = ::Disassemble(cursor, "d", &worldCount);
        std::array<SharedUtilsWideChar, 0x66> worldName{};
        while (worldCount > 0) {
            --worldCount;
            int worldId = 0;
            cursor = ::Disassemble(cursor, "dS", &worldId, static_cast<int>(worldName.size()), worldName.data());
            g_mapWorldName[worldId] = std::u16string(worldName.data());
        }
        --worldCount;
        CCommCli::Inst()->SetCliStatus(3);
    }
    return false;
}

bool CommBBSMakeBoardPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bad2c.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &ignored, &pledgeId);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x55);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSInit(resolved.user, 0, 0, pledgeId, 0, 1);
    }
    return false;
}

bool CommBBSGetBoardListPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bae9c.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int ignored2 = 0;
    int ignored3 = 0;
    int ignored4 = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddddd", &userServerId, &ignored1, &ignored2, &ignored3, &ignored4);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x73);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetBoardList(cursor));
    }
    return false;
}

bool CommBBSModBoardPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb038.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int pledgeId = 0;
    int value = 0;
    ::Disassemble(packet, "dddd", &userServerId, &ignored, &pledgeId, &value);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x8b);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSInit(resolved.user, 0, 0, pledgeId, value, 1);
    }
    return false;
}

bool CommBBSDelBoardPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb1b8.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &ignored, &pledgeId);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0xa1);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSInit(resolved.user, 0, 0, pledgeId, 0, 1);
    }
    return false;
}

bool CommBBSWriteArticlePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb328.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int boardId = 0;
    int flags = 0;
    int ignored2 = 0;
    ::Disassemble(packet, "ddddd", &userServerId, &ignored1, &boardId, &flags, &ignored2);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0xba);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSList(resolved.user, boardId, 1, flags);
    }
    return false;
}

bool CommBBSReadArticlePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb4a4.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int boardType = 0;
    std::array<SharedUtilsWideChar, 100> search{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddS", &userServerId, &ignored, &boardId, &boardType, static_cast<int>(search.size()), search.data()
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0xf0);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.ReadArticle(boardId, boardType, search.data(), cursor));
    }
    return false;
}

bool CommBBSGetArticleListPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb678.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &userServerId, &ignored, &boardId);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x11b);
    if (resolved.user != nullptr && boardId != -1) {
        int boardType = 0;
        int listMode = 0;
        int listPage = 0;
        int searchType = 0;
        int searchStart = 0;
        std::array<SharedUtilsWideChar, 100> search{};
        cursor = ::Disassemble(
            cursor, "dSdddd", &boardType, static_cast<int>(search.size()), search.data(), &listMode, &listPage, &searchType,
            &searchStart
        );
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetArticleList(
            boardId, boardType, search.data(), listMode, listPage, searchType, cursor, searchStart, -1, g_emptyTargetWide, 1,
            1
        ));
    }
    return false;
}

bool CommBBSReadArticleThreadPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bb8e0.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int articleId = 0;
    int boardType = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddddd", &userServerId, &ignored, &boardId, &articleId, &boardType);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x145);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetArticleThread(boardId, articleId, boardType, cursor));
    }
    return false;
}

bool CommBBSGetCommentPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bba8c.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int articleId = 0;
    int boardType = 0;
    int listMode = 0;
    int listPage = 0;
    int searchType = 0;
    int searchStart = 0;
    int searchEnd = 1;
    int commentPage = 0;
    int commentCount = 0;
    int mode = 0;
    std::array<SharedUtilsWideChar, 0x2a> search{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddddddSddddd", &userServerId, &ignored, &boardId, &articleId, &boardType, &listMode, &listPage,
        &searchType, static_cast<int>(search.size()), search.data(), &searchStart, &searchEnd, &commentPage, &commentCount,
        &mode
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x174);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetArticleComment(
            boardId, articleId, boardType, listMode, listPage, searchType, search.data(), searchStart, searchEnd,
            commentPage, commentCount, cursor, mode
        ));
    }
    return false;
}

bool CommBBSReplyFormPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bbd1c.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int boardType = 0;
    std::array<SharedUtilsWideChar, 100> search{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddS", &userServerId, &ignored, &boardId, &boardType, static_cast<int>(search.size()), search.data()
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x1a4);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.WriteReplyForm(boardId, boardType, search.data(), cursor));
    }
    return false;
}

bool CommBBSWriteCommentPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bbef0.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int boardId = 0;
    int searchType = 0;
    int articleId = 0;
    int ignored2 = 0;
    ::Disassemble(packet, "dddddd", &userServerId, &ignored1, &boardId, &searchType, &articleId, &ignored2);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x1c1);
    if (resolved.user != nullptr) {
        CCommCli* const client = CCommCli::Inst();
        client->RequestBBSRead(resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1);
        client->RequestBBSReadThread(resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1);
        client->RequestBBSReadComment(resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1);
    }
    return false;
}

bool CommBBSWriteFormPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bc124.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int boardType = 0;
    std::array<SharedUtilsWideChar, 100> search{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddS", &userServerId, &ignored, &boardId, &boardType, static_cast<int>(search.size()), search.data()
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x1e2);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.WriteForm(boardId, boardType, search.data(), cursor));
    }
    return false;
}

bool CommBBSSearchPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bc2f8.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int boardType = 0;
    int listMode = 0;
    int listPage = 0;
    int searchStart = 0;
    int searchEnd = 0;
    int searchType = 0;
    std::array<SharedUtilsWideChar, 0x40> search{};
    std::array<SharedUtilsWideChar, 100> extraSearch{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dddddddddSS", &userServerId, &ignored, &boardId, &boardType, &listMode, &listPage, &searchStart, &searchEnd,
        &searchType, static_cast<int>(search.size()), search.data(), static_cast<int>(extraSearch.size()), extraSearch.data()
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x20f);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetSearch(
            boardId, boardType, searchType, search.data(), extraSearch.data(), listMode, listPage, searchStart, cursor,
            searchEnd
        ));
    }
    return false;
}

bool CommBBSModifyFormpacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bc564.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int boardType = 0;
    std::array<SharedUtilsWideChar, 100> search{};
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddddS", &userServerId, &ignored, &boardId, &boardType, static_cast<int>(search.size()), search.data()
    );
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x241);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.WriteModifyForm(boardId, boardType, search.data(), cursor));
    }
    return false;
}

bool CommBBSModifyPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bc738.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int boardId = 0;
    int flags = 0;
    int ignored2 = 0;
    ::Disassemble(packet, "ddddd", &userServerId, &ignored1, &boardId, &flags, &ignored2);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x25b);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSList(resolved.user, boardId, 1, flags);
    }
    return false;
}

bool CommBBSDeletePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bc8b4.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int boardId = 0;
    int searchType = 0;
    int articleId = 0;
    int mode = 0;
    ::Disassemble(packet, "dddddd", &userServerId, &ignored, &boardId, &searchType, &articleId, &mode);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x276);
    if (resolved.user != nullptr) {
        CCommCli* const client = CCommCli::Inst();
        if (mode > 0) {
            if (mode < 3) {
                client->RequestBBSList(resolved.user, boardId, 1, searchType);
            } else if (mode == 3) {
                client->RequestBBSRead(resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1);
                client->RequestBBSReadThread(
                    resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1
                );
                client->RequestBBSReadComment(
                    resolved.user, boardId, articleId, 0, 1, 1, searchType, g_emptyTargetWide, 1, 1
                );
            }
        }
    }
    return false;
}

bool CommBBSGetFavoritePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bcb14.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    int value = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &userServerId, &ignored, &value);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x29d);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetFavorite(value, cursor));
    }
    return false;
}

bool CommBBSAddFavoritePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bcc8c.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int ignored2 = 0;
    ::Disassemble(packet, "ddd", &userServerId, &ignored1, &ignored2);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x2b3);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSGetFavorite(resolved.user);
    }
    return false;
}

bool CommBBSDelFavoritePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bcdd8.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored1 = 0;
    int ignored2 = 0;
    ::Disassemble(packet, "ddd", &userServerId, &ignored1, &ignored2);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x2c9);
    if (resolved.user != nullptr) {
        CCommCli::Inst()->RequestBBSGetFavorite(resolved.user);
    }
    return false;
}

bool CommBBSGetAnnouncePacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bcf24.
    L2_GUARD_FUNCTION();
    int userServerId = 0;
    int ignored = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &userServerId, &ignored);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x2e5);
    if (resolved.user != nullptr) {
        CCommunity community(resolved.socket, 1);
        static_cast<void>(community.GetAnnounce(cursor));
    }
    return false;
}

bool CommManMemoPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bd094.
    L2_GUARD_FUNCTION();
    int ignored = 0;
    int userServerId = 0;
    int mode = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &ignored, &userServerId, &mode);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x2ff);
    if (resolved.user == nullptr) {
        return false;
    }
    if (mode == 1) {
        CMemoHTML memo(resolved.socket);
        memo.MemoList(cursor);
    } else if (mode == 2) {
        CMemoHTML memo(resolved.socket);
        memo.MemoRead(cursor);
    } else if (mode == 3) {
        int memoId = 0;
        ::Disassemble(cursor, "d", &memoId);
        if (memoId == -1) {
            resolved.socket->SendSystemMessage(SystemMessageId::MEMO_LIMIT_EXCEEDED);
        }
    } else if (mode == 4 || mode == 6) {
        CCommCli::Inst()->RequestGetMemoList(resolved.user, 1, g_emptyTargetWide);
    } else if (mode == 5) {
        CMemoHTML memo(resolved.socket);
        memo.MemoModifyForm(6, cursor);
    }
    return false;
}

bool CommManPledgeBBSPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bd2d0.
    L2_GUARD_FUNCTION();
    int ignored = 0;
    int userServerId = 0;
    int mode = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &ignored, &userServerId, &mode);
    CObject* const object = CObject::GetObjectW(userServerId);
    if (object == nullptr || !object->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"commsocket.cpp",
        //     0x345,
        //     userServerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid user[%x]",
        //     L"commsocket.cpp",
        //     0x345,
        //     userServerId)
        L2_LOG_WARN(
            Log,
            "Cannot deliver the clan-board reply to player object 0x%08x: the player is unavailable; the reply is ignored.",
            static_cast<unsigned>(userServerId)
        );
        return false;
    }
    User* const user = object->CastUser();
    User* const socketOwner = object->CastUser();
    CUserSocket* const socket = socketOwner->GetSocket();
    if (mode == 1) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSList(cursor);
    } else if (mode == 2) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSView(cursor);
    } else if (mode == 3) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSView2(cursor);
    } else if (mode == 4) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSAdmin(cursor);
    } else if (mode == 6) {
        const int pledgeId = user->m_data->m_nPledgeID;
        if (pledgeId != 0) {
            CCommCli::Inst()->RequestClanBBSView(user, pledgeId);
        }
    } else if (mode == 7) {
        CPledge* const candidate = user->GetPledge();
        if (candidate != nullptr) {
            CPledge* const masterOwner = user->GetPledge();
            const std::uint32_t masterDbId = masterOwner->GetMasterDbId();
            if (user->GetDBID() == masterDbId) {
                CPledge* const idOwner = user->GetPledge();
                CCommCli::Inst()->RequestClanBBSAdmin(user, static_cast<int>(idOwner->GetDbId()));
            }
        }
    }
    return false;
}

bool CommSysinfoPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005bd568.
    L2_GUARD_FUNCTION();
    int ignored = 0;
    int userServerId = 0;
    int received = 0;
    int processed = 0;
    int processTime = 0;
    ::Disassemble(packet, "ddddd", &ignored, &userServerId, &received, &processed, &processTime);
    ResolvedCommUser resolved = ResolveCommUser(userServerId, 0x38b);
    if (resolved.user != nullptr) {
        std::array<char16_t, 128> message{};
        FormatUtf16To(
            message.data(), 0x7f,
            u"lin2comm server: packet recved[%d], processed[%d], return time[%d ms], process time[%d ms]", received,
            processed, runtime::TickCount32() - resolved.user->m_dwL2CommTick, processTime
        );
        resolved.socket->SendSystemMessage(u"SYS", message.data());
    }
    return false;
}

bool CommDelClanBoardPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005ba990.
    L2_GUARD_FUNCTION();
    int result = 0;
    int pledgeId = 0;
    ::Disassemble(packet, "dd", &result, &pledgeId);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "fail to delete clan board but clan[%d] is deleted",
        //     pledgeId)
        // L2_LOG_ERROR(Log,
        //     "fail to delete clan board but clan[%d] is deleted",
        //     pledgeId)
        L2_LOG_ERROR(
            Log, "Community server reported clan-board deletion failure for clan %d (result %d).", pledgeId, result
        );
    } else {
        // Log.Add(
        //     LOG_NORMAL,
        //     "delete clan board because clan[%d] is deleted",
        //     pledgeId)
        // L2_LOG_INFO(Log,
        //     "delete clan board because clan[%d] is deleted",
        //     pledgeId)
        L2_LOG_INFO(Log, "Community server reported clan-board deletion for clan %d (result %d).", pledgeId, result);
    }
    return false;
}

bool CommPledgeMasterTransferPacket(CCommSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x005baa74.
    L2_GUARD_FUNCTION();
    db.RequestUpdateCommPledgeMasterResult(packet);
    return false;
}

bool CommDummyPacket(CCommSocket*, const std::uint8_t*) {
    // Current L2Server 0x005bab20.
    L2_GUARD_FUNCTION();
    return false;
}
