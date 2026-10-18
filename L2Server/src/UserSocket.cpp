#include "UserSocket.h"
#include "AgentGameGuard.h"
#include "AgitHtml.h"
#include "Alliance.h"
#include "Area.h"
#include "Auction.h"
#include "Auth.h"
#include "BotChecker.h"
#include "BuilderCmdHandler.h"
#include "BuySell.h"
#include "BuySellListManager.h"
#include "CConfig.h"
#include "CWorld.h"
#include "Castle.h"
#include "CommCli.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "Dominion.h"
#include "Door.h"
#include "EventMaker.h"
#include "EventMatch.h"
#include "FieldCycleStuff.h"
#include "Friends.h"
#include "GameEventSystem.h"
#include "HTMLCache.h"
#include "HtmlActionParamParser.h"
#include "HtmlCommand.h"
#include "IOCPMessageServer.h"
#include "IPBlock.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "L2Packet.h"
#include "L2StatusWnd.h"
#include "L2Time.h"
#include "MainEvent.h"
#include "Manor.h"
#include "MultiPartyCommandChannel.h"
#include "MultiPartyCommandChannelManager.h"
#include "MultiSell.h"
#include "NPC.h"
#include "NPSystem.h"
#include "NewInspector.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadStuff.h"
#include "PCParam.h"
#include "PVPMatch.h"
#include "Packet.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "PartyMatchingWaitingRoom.h"
#include "PartyRoomInfo.h"
#include "PartyRoomListFilter.h"
#include "Pet.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeMemberService.h"
#include "Portable/ClientProtocolRuntime.h"
#include "Portable/Lu4Protocol.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/PacketShuffler.h"
#include "Protocol/src/SystemMessageIds.h"
#include "QueueTicket.h"
#include "Raid.h"
#include "Recipe.h"
#include "Relay.h"
#include "ReplyChecker.h"
#include "Residence.h"
#include "SSQ.h"
#include "Server.h"
#include "Setting.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeCrest.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/WarehouseValidation.h"
#include "Shared/src/platform/Runtime.h"
#include "SharedContainer.h"
#include "SiegeAgit.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "User.h"
#include "UserBasicAction.h"
#include "UserPacketEx.h"
#include "Vehicle.h"
#include "WareHouse.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

bool PledgeInfo(CSocket* socket, const std::uint8_t* packet);

void CheckResurrectable(CCreature* creature, bool answer, int requestId);
bool ConfirmDlg_OpenDoor(CUserSocket* socket, User* user, bool answer, int requestId);
bool ConfirmDlg_CloseDoor(CUserSocket* socket, User* user, bool answer, int requestId);
bool ConfirmDlg_Resurretion(CUserSocket* socket, User* user, bool answer, int requestId);
bool ConfirmDlg_CallPC(CUserSocket* socket, User* user, bool answer, int requestId);
bool ConfirmDlg_WithdrawAuctionBid(CUserSocket* socket, User* user, bool answer, int requestId);

using ConfirmDialogCallback = bool (*)(CUserSocket*, User*, bool, int);

struct ConfirmDialogFuncTable {
    std::int32_t MessageNum;
    std::array<std::byte, 4> m_alignment0004To0008;
    ConfirmDialogCallback Func;
};


ConfirmDialogFuncTable g_ConfirmDialogTable[6]{
    {0x474, {}, &ConfirmDlg_OpenDoor},
    {0x475, {}, &ConfirmDlg_CloseDoor},
    {0x5e6, {}, &ConfirmDlg_Resurretion},
    {0x732, {}, &ConfirmDlg_CallPC},
    {0x82d, {}, &ConfirmDlg_WithdrawAuctionBid},
    {0x902, {}, &ConfirmDlg_Resurretion},
};

extern thread_local std::uint32_t g_TalkChannelError;

namespace {

// Current UserSocket.obj .rdata 0x01266228/0x01266260. These are the
// original thirteen character-creation rows, not a derived class/race map.
constexpr std::array<ClassType, 13> __InitClassType{static_cast<ClassType>(0), static_cast<ClassType>(10),
    static_cast<ClassType>(18), static_cast<ClassType>(25), static_cast<ClassType>(31), static_cast<ClassType>(38),
    static_cast<ClassType>(44), static_cast<ClassType>(49), static_cast<ClassType>(53), static_cast<ClassType>(123),
    static_cast<ClassType>(124), static_cast<ClassType>(182), static_cast<ClassType>(183)};

constexpr std::array<RaceType, 13> __InitRaceType{static_cast<RaceType>(0), static_cast<RaceType>(0),
    static_cast<RaceType>(1), static_cast<RaceType>(1), static_cast<RaceType>(2), static_cast<RaceType>(2),
    static_cast<RaceType>(3), static_cast<RaceType>(3), static_cast<RaceType>(4), static_cast<RaceType>(5),
    static_cast<RaceType>(5), static_cast<RaceType>(6), static_cast<RaceType>(6)};


// _wcsicmp over UTF-16 code units.
int CompareWideCaseInsensitive(const char16_t* left, const char16_t* right) noexcept {
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(static_cast<wint_t>(*left));
        const wint_t foldedRight = std::towlower(static_cast<wint_t>(*right));
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
}

// Noise: imported function address did not identify the offending connection.
// int LegacySocketSymbolForLog() noexcept {
//     // Current 0x00c1b239 loads the imported `socket` function address for
//     // the diagnostic's first %d, exactly like the sibling socket owners.
//     return static_cast<int>(
//         reinterpret_cast<std::intptr_t>(&::socket))
// }


const char* SocketCloseReasonForLog(SOCKET_CLOSE_REASON reason) noexcept {
    switch (reason) {
        case SCR_NONE:
            return "unset";
        case SCR_NORMAL:
            return "normal";
        case SCR_QUIT:
            return "quit";
        case SCR_RESTART:
            return "restart";
        case SCR_REMAIN:
            return "remain";
        case SCR_PENDING:
            return "pending write";
        case SCR_ILLEGAL_BEHAVIOR:
            return "illegal-behavior close";
        case SCR_RAID_QUIT:
            return "raid quit";
        case SCR_RETURN_TO_ORIGIN:
            return "return to origin";
        case SCR_AUTH_KICK:
            return "auth kick";
        default:
            return "unknown";
    }
}

std::size_t TargetWideLength(const SharedUtilsWideChar* value) noexcept {
    return std::char_traits<SharedUtilsWideChar>::length(value);
}

bool TargetWideStartsWithAscii(const SharedUtilsWideChar* value, std::string_view prefix) noexcept {
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (value[index] == SharedUtilsWideChar{} ||
            value[index] != static_cast<SharedUtilsWideChar>(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

bool TargetWideEqualsAscii(const SharedUtilsWideChar* value, std::string_view expected) noexcept {
    return TargetWideStartsWithAscii(value, expected) && value[expected.size()] == SharedUtilsWideChar{};
}

int TargetWideToInt(const SharedUtilsWideChar* value) noexcept {
    const std::wstring hostValue = Utf16ToWide(value, Utf16WidePolicy::PreserveInvalidOnWindows);
    return WindowsWtol(std::wstring_view(hostValue));
}

void CopyWriteHtmlContents(SharedUtilsWideChar* destination, const SharedUtilsWideChar* source) noexcept {
    const std::size_t sourceLength = TargetWideLength(source);
    const std::size_t copiedLength = sourceLength < 3001U ? sourceLength : 3000U;
    std::copy_n(source, copiedLength, destination);
    destination[copiedLength] = SharedUtilsWideChar{};
}

std::int32_t CurrentTime64Low32() noexcept {
    const std::int64_t current = static_cast<std::int64_t>(std::time(nullptr));
    return runtime::Int32FromBits(static_cast<std::uint32_t>(static_cast<std::uint64_t>(current)));
}

bool CanCreateCharacter(CUserSocket& socket, S_PROTOCOL failureProtocol, bool& punish) {
    // Current anonymous UserSocket.cpp owner 0x00c09488.
    L2_GUARD_FUNCTION();
    punish = false;

    if (!socket.m_bForceLogin && g_bProhibitAllCharCreation) {
        socket.Send("cd", failureProtocol, 5);
        return false;
    }

    if (g_nCountryCode == 1 && socket.m_bEconomicRestriction && socket.GetCharacterAmount() > 2U) {
        socket.Send("cd", failureProtocol, 1);
        return false;
    }

    const std::uint32_t f2pCharacterAmount = socket.GetCharacterAmount();
    const int usableCharacterSlotCount = F2PConfig::GetUsableCharacterSlotCount(&socket);
    if (f2pCharacterAmount >= static_cast<std::uint32_t>(usableCharacterSlotCount)) {
        socket.Send("cd", failureProtocol, 8);
        return false;
    }

    // Current 0x00c095bb takes a third snapshot and reuses only this one
    // for the remaining three gates.
    const std::uint32_t characterAmount = socket.GetCharacterAmount();

    const auto rejectBot = [&](int line) {
        const std::int32_t accountId = runtime::Int32FromBits(socket.GetAccountID());
        const std::wstring accountName = Utf16ToWide(socket.GetAccountName(), Utf16WidePolicy::PreserveInvalidOnWindows);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Non-Client BOT? account[%d, %s]",
        //     kUserSocketSource,
        //     line,
        //     accountId,
        //     accountName.c_str())
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[character create] Request rejected on socket %u, account %s (%u): %S (observed existing characters: %u). A
        //     failure reply and socket closure will be requested.", socket.m_nId, accountName.c_str(),
        //     static_cast<std::uint32_t>(accountId), line == 0x397 ? "existing character count exceeds 6"
        //         : line == 0x39e ? "configured character-count limit reached"
        //         : "character-count check failed",
        //     characterAmount)
        L2_LOG_INFO(
            Log,
            u"[character create] Request rejected on socket %u, account %s (%u): %S (observed existing characters: %u); a "
            u"failure reply and socket closure will be requested.",
            socket.m_nId, accountName.c_str(), static_cast<std::uint32_t>(accountId),
            line == 0x397       ? "existing character count exceeds 6"
                : line == 0x39e ? "configured character-count limit reached"
                                : "character-count check failed",
            characterAmount
        );
        socket.Send("cd", failureProtocol, 1);
        punish = true;
    };

    if (characterAmount > 6U) {
        rejectBot(0x397);
        return false;
    }

    const bool forceLogin = socket.m_bForceLogin;
    if (!forceLogin && characterAmount >= static_cast<std::uint32_t>(g_nProhibitCharCreationCount)) {
        rejectBot(0x39e);
        return false;
    }

    if (!forceLogin && g_bProhibitFirstCharCreation && characterAmount == 0U) {
        socket.Send("cd", failureProtocol, 6);
        return false;
    }
    return true;
}

std::uint64_t ExchangeSocketHandle(std::uint64_t* handle, std::uint64_t desired) noexcept {
#if defined(_MSC_VER)
    return static_cast<std::uint64_t>(
        _InterlockedExchange64(reinterpret_cast<volatile long long*>(handle), static_cast<long long>(desired))
    );
#else
    return __atomic_exchange_n(handle, desired, __ATOMIC_SEQ_CST);
#endif
}

} // namespace

class CUserSocketReleaseVerifier final : public CIOObject {
public:
    CUserSocketReleaseVerifier(CUserSocket* socket, const wchar_t* guardString)
#if !defined(_WIN32)
        try
#endif
        :
        m_pUserSocket(socket), m_guardStr(guardString) {
        L2_GUARD_FUNCTION();
        L2_ADD_REF(m_pUserSocket, ObjectRefType::VerifierRetain);

        if (m_pUserSocket->m_nCloseReason == SCR_NORMAL) {
            User* const user = m_pUserSocket->GetUser();
            if (user) {
                const bool inCombat = user->m_data->m_bIsInCombatMode;
                CLogFormatter formatter;
                formatter.WriteLog2_1(
                    0x34d, user, nullptr, inCombat, 0, m_pUserSocket->m_nPendingWrite.load(std::memory_order_relaxed), 0, 0,
                    0, 0
                );
                if (inCombat && !user->m_bBeingCheckedIfBot) {
                    m_pUserSocket->m_nCloseReason = SCR_REMAIN;
                    AddTimer(15000, 3);
                    return;
                }
            }
        } else if (m_pUserSocket->m_nCloseReason == SCR_PENDING) {
            User* const user = m_pUserSocket->GetUser();
            if (user) {
                CLogFormatter formatter;
                formatter.WriteLog2_1(
                    0x34d, user, nullptr, user->m_data->m_bIsInCombatMode, 1,
                    m_pUserSocket->m_nPendingWrite.load(std::memory_order_relaxed), 0, 0, 0, 0
                );
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"pending usersocket close socket force!")
            // Log.Add(
            //     LOG_WARN,
            //     "[socket] Deferring account cleanup for socket %u, account %u, after a pending-write close. Scheduling a
            //     cleanup timer for 5 seconds.", m_pUserSocket->m_nId, m_pUserSocket->GetAccountID())
            L2_LOG_WARN(
                Log,
                "[socket] Deferring account cleanup for socket %u, account %u, after a pending-write close; scheduling a "
                "cleanup timer for 5 seconds.",
                m_pUserSocket->m_nId, m_pUserSocket->GetAccountID()
            );
            AddTimer(5000, 4);
            return;
        }

        m_pUserSocket->ProcessQuit();
        AddTimer(5000, 1);
    }
#if !defined(_WIN32)
    catch (const std::exception& error) {
        // Failed construction calls MemoryObject's forbidden delete before
        // an OnClose-level handler can run. Preserve the original exception
        // diagnostic before that cleanup masks it with a forced crash.
        // std::fprintf(
        //     stderr,
        //     "[portable] CUserSocketReleaseVerifier construction threw %s: %s\n",
        //     typeid(error).name(),
        //     error.what())
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Error,
        //     "[socket] Could not construct the socket-close verifier. Rethrowing exception %s: %s",
        //     typeid(error).name(),
        //     error.what())
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[socket] Could not construct the socket-close verifier; rethrowing exception %s: %s", typeid(error).name(),
            error.what()
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
        throw;
    } catch (...) {
        // std::fprintf(
        //     stderr,
        //     "[portable] CUserSocketReleaseVerifier construction threw a non-standard exception\n")
        // runtime::logging::WriteDirect(
        //     stderr, "l2server", runtime::logging::Level::Error,
        //     "[socket] Could not construct the socket-close verifier, rethrowing an exception of unknown type.")
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[socket] Could not construct the socket-close verifier; rethrowing an exception of unknown type."
        );
        // std::fflush(stderr), // WriteDirect flushes the complete record.
        throw;
    }
#endif

    ~CUserSocketReleaseVerifier() override = default;

    void TimerExpired(int id) override {
        L2_GUARD_FUNCTION();
        if (id == 1) {
            g_UserSocketMap.DeleteObject(static_cast<int>(m_pUserSocket->m_nId), "UserSocket.cpp", 0x3596);
            AddTimer(25000, 2);
            return;
        }
        if (id == 3 || id == 4) {
            m_pUserSocket->ProcessQuit();
            AddTimer(5000, 1);
            return;
        }

        L2_ADD_REF(m_pUserSocket, ObjectRefType::VerifierRetain);
        const std::int32_t remaining = L2_RELEASE(m_pUserSocket, ObjectRefType::VerifierRetain);
        if (remaining > 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ORT - UserSocket Release failed. "
            //     L"AccountId [%d] refcount[%d] Caller is [%s]",
            //     kUserSocketSource,
            //     0x35ad,
            //     m_pUserSocket->GetAccountID(),
            //     m_pUserSocket->RefCount(),
            //     m_guardStr)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[socket] Release verification for socket %u, account %u, observed %d remaining references after
            //     releasing the temporary verifier reference (expected 1). Close caller: %s.", m_pUserSocket->m_nId,
            //     m_pUserSocket->GetAccountID(), remaining, m_guardStr ? m_guardStr : L"unknown")
            L2_LOG_ERROR(
                Log,
                u"[socket] Release verification for socket %u, account %u, observed %d remaining references after releasing "
                u"the temporary verifier reference (expected 1); close caller: %s.",
                m_pUserSocket->m_nId, m_pUserSocket->GetAccountID(), remaining, m_guardStr ? m_guardStr : L"unknown"
            );
            m_pUserSocket->ReportOrt("UserSocket.cpp", 0x35b6);
            g_ServerContext.NotReleasedUserSocket.fetch_add(1, std::memory_order_relaxed);
        }
        L2_RELEASE(m_pUserSocket, ObjectRefType::VerifierRetain);
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    }

private:
    CUserSocket* m_pUserSocket;
    const wchar_t* m_guardStr;
};


namespace {

// Current L2Server .data lists at 0x015ea770..0x015ec2e4. The target loops
// over each list's complete fixed extent, including its zero-filled tail.
constexpr std::array<std::int32_t, 209> kCannotControlPacketList{0x0f, 0x1f, 0x01, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
    0x1c, 0x34, 0x35, 0x36, 0x37, 0x40, 0x42, 0x43, 0x44, 0x45, 0x46, 0x39, 0x3d, 0x3f, 0x47, 0x52, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x59, 0x5b, 0x5c, 0x02, 0x5f, 0x60, 0x61, 0x75, 0x76, 0x73, 0x7c, 0x7f, 0x80, 0x81, 0x2f, 0x31, 0x32, 0x96, 0x97,
    0x83, 0x93, 0x94, 0x95, 0x2c, 0x98, 0x9a, 0x9b, 0x9c, 0x9d, 0x9f, 0xb0, 0xb3, 0xb5, 0xb6, 0xb7, 0xb8, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xc0, 0xc2, 0xc3, 0xc4, 0x6f, 0x70, 0x71, 0x72, 0xc5, 0x6a};

constexpr std::array<std::int32_t, 209> kRaidAllowedPacketList{0xb1, 0xa6, 0x65, 0x2e, 0x50, 0x14, 0x38, 0x0f, 0x59, 0x1f,
    0x39, 0x3a, 0x19, 0x49, 0x22, 0x23, 0x74, 0x56, 0x5b, 0x5c, 0x6c, 0x11, 0x57, 0x00, 0x7d, 0x01, 0x48, 0xaa, 0xab, 0xac,
    0xad, 0xae, 0xaf, 0x6e, 0x3b, 0x3c, 0x67, 0xc6, 0x3d, 0x3f, 0x42, 0x43, 0x44, 0x45, 0x46, 0xcb, 0x92, 0x7e, 0xd0, 0x16,
    -1};

constexpr std::array<std::int32_t, 352> kRaidAllowedExPacketList{0x01, 0x3a, 0x24, 0x0d, 0x70, 0x71, 0x53, 0x0c, 0x60, 0x48,
    0x41, 0xc8, 0x21, 0x1a, 0x2a, 0x2b, 0x3c, 0x55, 0x5e, 0x10, 0xed, 0xfe, -1};

constexpr std::array<std::int32_t, 255> kRaidBlockedPacketList{0xa7};

constexpr std::array<std::int32_t, 514> kRaidBlockedExPacketList{
    0xaa, 0x21, 0x86, 0x10c, 0xfd, 0xe9, 0xfe, 0x11f, 0x13b, 0x14b, 0x147, 0x151};

constexpr std::array<std::int32_t, 209> kActionFailPacketList{0x0f, 0x1f, 0x01};

} // namespace

CSocketLimiter g_SocketLimiter;
UserPacketCallbackMap gPacketTableFromClient(&DummyUserPacket, u"C_PROTOCOL");

namespace {

// Current UserSocket.cpp static registrations at 0x00dce458..0x00dd0b54.
// All 209 current rows are created below. Forty-five bind
// DummyUserPacket, but still publish their packet name and registration flag.
// the startup TestRegistration scan observes that distinction from an
// unregistered default slot.
L2_BIND_PACKET(kBindCVersion, gPacketTableFromClient, 0x00e, u"C_VERSION", &CUserSocket::VersionPacket);
L2_BIND_PACKET(kBindCLogin, gPacketTableFromClient, 0x02b, u"C_LOGIN", &CUserSocket::LoginPacket);
L2_BIND_PACKET(
    kBindCCharacterCreate, gPacketTableFromClient, 0x00c, u"C_CHARACTER_CREATE", &CUserSocket::CharacterCreatePacket
);
L2_BIND_PACKET(
    kBindCCharacterDelete, gPacketTableFromClient, 0x00d, u"C_CHARACTER_DELETE", &CUserSocket::CharacterDeletePacket
);
L2_BIND_PACKET(
    kBindCCharacterSelect, gPacketTableFromClient, 0x012, u"C_CHARACTER_SELECT", &CUserSocket::CharacterSelectPacket
);
L2_BIND_PACKET(kBindCNewCharacter, gPacketTableFromClient, 0x013, u"C_NEW_CHARACTER", &CUserSocket::NewCharacterPacket);
L2_BIND_PACKET(kBindCRestoreCharacter, gPacketTableFromClient, 0x07b, u"C_RESTORE_CHARACTER", &RestoreCharacter);
L2_BIND_PACKET(kBindCReload, gPacketTableFromClient, 0x06e, u"C_RELOAD", &ReloadPacket);
L2_BIND_PACKET(kBindCEx, gPacketTableFromClient, 0x0d0, u"C_EX", &UserExPacket);
L2_BIND_PACKET(kBindCItemList, gPacketTableFromClient, 0x014, u"C_ITEMLIST", &ItemListPacket);
L2_BIND_PACKET(kBindCJoinPledge, gPacketTableFromClient, 0x026, u"C_JOIN_PLEDGE", &CUserSocket::JoinPledgePacket);
L2_BIND_PACKET(
    kBindCAnswerJoinPledge, gPacketTableFromClient, 0x027, u"C_ANSWER_JOIN_PLEDGE", &CUserSocket::AnswerJoinPledgePacket
);
L2_BIND_PACKET(kBindCWithdrawalPledge, gPacketTableFromClient, 0x028, u"C_WITHDRAWAL_PLEDGE", &WithdrawPledgePacket);
L2_BIND_PACKET(kBindCOustPledgeMember, gPacketTableFromClient, 0x029, u"C_OUST_PLEDGE_MEMBER", &OustPledgeMemberPacket);
L2_BIND_PACKET(kBindCSkillList, gPacketTableFromClient, 0x050, u"C_SKILL_LIST", &SkillListPacket);
L2_BIND_PACKET(
    kBindCRequestEnchantItem,
    gPacketTableFromClient,
    0x05f,
    u"C_REQUEST_ENCHANT_ITEM",
    &CUserSocket::RequestEnchantItemPacket
);
L2_BIND_PACKET(kBindCDestroyItem, gPacketTableFromClient, 0x060, u"C_DESTROY_ITEM", &CUserSocket::DestroyItem);
L2_BIND_PACKET(kBindCPledgeInfo, gPacketTableFromClient, 0x065, u"C_PLEDGE_INFO", &PledgeInfo);
L2_BIND_PACKET(kBindCPledgeCrest, gPacketTableFromClient, 0x067, u"C_PLEDGE_CREST", &PledgeCrest);
L2_BIND_PACKET(kBindCAllianceCrest, gPacketTableFromClient, 0x092, u"C_ALLIANCE_CREST", &AllianceCrestPacket);
L2_BIND_PACKET(kBindCHennaEquip, gPacketTableFromClient, 0x06f, u"C_HENNA_EQUIP", &RequestHennaEquip);
L2_BIND_PACKET(kBindCHennaUnequipList, gPacketTableFromClient, 0x070, u"C_HENNA_UNEQUIP_LIST", &RequestHennaUnequipList);
L2_BIND_PACKET(kBindCHennaUnequipInfo, gPacketTableFromClient, 0x071, u"C_HENNA_UNEQUIP_INFO", &RequestHennaUnequipInfo);
L2_BIND_PACKET(kBindCHennaUnequip, gPacketTableFromClient, 0x072, u"C_HENNA_UNEQUIP", &RequestHennaUnequip);
L2_BIND_PACKET(kBindCAcquireSkillInfo, gPacketTableFromClient, 0x073, u"C_ACQUIRE_SKILL_INFO", &AcquireSkillInfo);
L2_BIND_PACKET(kBindCRequestAcquireSkill, gPacketTableFromClient, 0x07c, u"C_REQ_ACQUIRE_SKILL", &RequestAcquireSkill);
L2_BIND_PACKET(kBindCHennaItemList, gPacketTableFromClient, 0x0c3, u"C_HENNA_ITEM_LIST", &RequestHennaItemList);
L2_BIND_PACKET(kBindCHennaItemInfo, gPacketTableFromClient, 0x0c4, u"C_HENNA_ITEM_INFO", &RequestHennaItemInfo);
L2_BIND_PACKET(kBindCBuySeed, gPacketTableFromClient, 0x0c5, u"C_BUY_SEED", &RequestBuySeed);
L2_BIND_PACKET(kBindCBuyPreviewList, gPacketTableFromClient, 0x0c7, u"C_BUY_PREVIEW_LIST", &BuyPreviewPacket);
L2_BIND_PACKET(kBindCQuestList, gPacketTableFromClient, 0x062, u"C_QUESTLIST", &QuestList);
L2_BIND_PACKET(kBindCFriendList, gPacketTableFromClient, 0x079, u"C_FRIEND_LIST", &FriendList);
L2_BIND_PACKET(kBindCBlockPacket, gPacketTableFromClient, 0x0a9, u"C_BLOCK_PACKET", &RequestBlockPacket);
L2_BIND_PACKET(
    kBindCRequestSkillCoolTime, gPacketTableFromClient, 0x0a6, u"C_REQUEST_SKILL_COOL_TIME", &RemainCoolTimePacket
);
L2_BIND_PACKET(kBindCLinkHtml, gPacketTableFromClient, 0x022, u"C_LINK_HTML", &LinkHtmlPacket);
L2_BIND_PACKET(kBindCPassCmdToServer, gPacketTableFromClient, 0x023, u"C_PASS_CMD_TO_SERVER", &CUserSocket::HtmlCMDPacket);
L2_BIND_PACKET(kBindCWriteBbs, gPacketTableFromClient, 0x024, u"C_WRITE_BBS", &WriteHtmlPacket);
L2_BIND_PACKET(kBindCShowBoard, gPacketTableFromClient, 0x05e, u"C_SHOW_BOARD", &ShowBBSBoard);
L2_BIND_PACKET(kBindCTutorialLinkHtml, gPacketTableFromClient, 0x085, u"C_TUTORIAL_LINK_HTML", &TutorialLinkHTML);
L2_BIND_PACKET(
    kBindCTutorialPassCmdToServer, gPacketTableFromClient, 0x086, u"C_TUTORIAL_PASS_CMD_TO_SERVER", &TutorialPassCmdToServer
);
L2_BIND_PACKET(kBindCTutorialMarkPressed, gPacketTableFromClient, 0x087, u"C_TUTORIAL_MARK_PRESSED", &TutorialMarkPressed);
L2_BIND_PACKET(kBindCTutorialClientEvent, gPacketTableFromClient, 0x088, u"C_TUTORIAL_CLIENT_EVENT", &TutorialClientEvent);
L2_BIND_PACKET(kBindCUserCmdBypass, gPacketTableFromClient, 0x0b3, u"C_USER_CMD_BYPASS", &CUserSocket::RequestUserCommand);
L2_BIND_PACKET(
    kBindCCastleSiegeInfo,
    gPacketTableFromClient,
    0x0aa,
    u"C_CASTLE_SIEGE_INFO",
    static_cast<bool (*)(CSocket*, const std::uint8_t*)>(&RequestCastleSiegeInfoPacket)
);
L2_BIND_PACKET(
    kBindCCastleSiegeAttackerList,
    gPacketTableFromClient,
    0x0ab,
    u"C_CASTLE_SIEGE_ATTACKER_LIST",
    &RequestCastleSiegeAttackerListPacket
);
L2_BIND_PACKET(
    kBindCCastleSiegeDefenderList,
    gPacketTableFromClient,
    0x0ac,
    u"C_CASTLE_SIEGE_DEFENDER_LIST",
    &RequestCastleSiegeDefenderListPacket
);
L2_BIND_PACKET(kBindCJoinCastleSiege, gPacketTableFromClient, 0x0ad, u"C_JOIN_CASTLE_SIEGE", &RequestJoinCastleSiegePacket);
L2_BIND_PACKET(
    kBindCConfirmCastleSiegeWaitingList,
    gPacketTableFromClient,
    0x0ae,
    u"C_CONFIRM_CASTLE_SIEGE_WAITING_LIST",
    &RequestConfirmCastleSiegeWaitingListPacket
);
L2_BIND_PACKET(
    kBindCSetCastleSiegeTime, gPacketTableFromClient, 0x0af, u"C_SET_CASTLE_SIEGE_TIME", &RequestSetCastleSiegeTimePacket
);
L2_BIND_PACKET(kBindCMultiSellChoose, gPacketTableFromClient, 0x0b0, u"C_MULTI_SELL_CHOOSE", &RequestMultiSellChoose);

// Current C12 initializers 0x00dd0224, 0x00dd0254, 0x00dd0884,
// 0x00dd06d4 and 0x00dd0704.
L2_BIND_PACKET(kBindCNetPing, gPacketTableFromClient, 0x0b1, u"C_NET_PING", &CUserSocket::RequestNetPing);
L2_BIND_PACKET(kBindCRemainTime, gPacketTableFromClient, 0x0b2, u"C_REMAIN_TIME", &RequestRemainTime);
L2_BIND_PACKET(kBindCGameGuardReply, gPacketTableFromClient, 0x0cb, u"C_GAMEGUARD_REPLY", &ReplyGameGuardInfo);
L2_BIND_PACKET(kBindCMakeMacro, gPacketTableFromClient, 0x0cd, u"C_MAKE_MACRO", &RequestMakeMacro);
L2_BIND_PACKET(kBindCDeleteMacro, gPacketTableFromClient, 0x0ce, u"C_DELETE_MACRO", &RequestDeleteMacro);

// Current C09 initializers 0x00dce4e8, 0x00dce574, 0x00dced24,
// 0x00dcef04, 0x00dcf384, 0x00dcf3e4 and 0x00dcf414.
L2_BIND_PACKET(kBindCAction, gPacketTableFromClient, 0x01f, u"C_ACTION", &ActionPacket);
L2_BIND_PACKET(kBindCAttack, gPacketTableFromClient, 0x001, u"C_ATTACK", &AttackPacket);
L2_BIND_PACKET(kBindCTargetUnselected, gPacketTableFromClient, 0x048, u"C_TARGET_UNSELECTED", &TargetUnselectedPacket);
L2_BIND_PACKET(kBindCGetOnVehicle, gPacketTableFromClient, 0x053, u"C_GETON_VEHICLE", &GetOnVehiclePacket);
L2_BIND_PACKET(kBindCTargetUserFromMenu, gPacketTableFromClient, 0x061, u"C_TARGET_USER_FROM_MENU", &TargetUserFromMenu);
L2_BIND_PACKET(
    kBindCMoveToLocationInVehicle,
    gPacketTableFromClient,
    0x075,
    u"C_MOVE_TO_LOCATION_IN_VEHICLE",
    &MoveToLocationInVehiclePacket
);
L2_BIND_PACKET(
    kBindCCannotMoveAnymoreInVehicle,
    gPacketTableFromClient,
    0x076,
    u"C_CAN_NOT_MOVE_ANYMORE_IN_VEHICLE",
    &CannotMoveAnymoreInVehiclePacket
);

// Current C10 initializers 0x00dcf564, 0x00dd0914, 0x00dcf3b4,
// 0x00dcf744, 0x00dd0074 and 0x00dd07f4.
L2_BIND_PACKET(kBindCDestroyQuest, gPacketTableFromClient, 0x063, u"C_DESTROY_QUEST", &DestroyQuest);
L2_BIND_PACKET(kBindCOpenMinimap, gPacketTableFromClient, 0x06c, u"C_OPEN_MINIMAP", &OpenMinimap);
L2_BIND_PACKET(kBindCSysCmd2, gPacketTableFromClient, 0x074, u"C_SYS_CMD_2", &SysCMD2Packet);
L2_BIND_PACKET(kBindCGmCommandType, gPacketTableFromClient, 0x07e, u"C_GM_COMMAND_TYPE", &GMCommand);
L2_BIND_PACKET(kBindCRequestPackageSend, gPacketTableFromClient, 0x0a8, u"C_REQUEST_PACKAGE_SEND", &RequestPackageSend);
L2_BIND_PACKET(kBindCSSQStatus, gPacketTableFromClient, 0x0c8, u"C_SSQ_STATUS", &RequestSSQStatus);

L2_BIND_PACKET(kBindCLogout, gPacketTableFromClient, 0x000, u"C_LOGOUT", &LogoutPacket);
L2_BIND_PACKET(
    kBindCMoveBackwardToLocation, gPacketTableFromClient, 0x002, u"C_MOVE_BACKWARD_TO_LOCATION", &MoveToLocationBackPacket
);
L2_BIND_PACKET(kBindCStartPledgeWar, gPacketTableFromClient, 0x003, u"C_START_PLEDGE_WAR", &StartPledgeWarPacket);
L2_BIND_PACKET(kBindCStopPledgeWar, gPacketTableFromClient, 0x005, u"C_STOP_PLEDGE_WAR", &StopPledgeWarPacket);
L2_BIND_PACKET(
    kBindCSetPledgeCrest,
    gPacketTableFromClient,
    0x009,
    u"C_SET_PLEDGE_CREST",
    static_cast<bool (*)(CSocket*, const std::uint8_t*)>(&SetPledgeCrestPacket)
);
L2_BIND_PACKET(kBindCChangeMoveType, gPacketTableFromClient, 0x035, u"C_CHANGE_MOVE_TYPE", &ChangeMoveTypePacket);
L2_BIND_PACKET(kBindCChangeWaitType, gPacketTableFromClient, 0x036, u"C_CHANGE_WAIT_TYPE", &ChangeStopTypePacket);
L2_BIND_PACKET(kBindCJoinParty, gPacketTableFromClient, 0x042, u"C_JOIN_PARTY", &CUserSocket::JoinPartyPacket);
L2_BIND_PACKET(
    kBindCAnswerJoinParty, gPacketTableFromClient, 0x043, u"C_ANSWER_JOIN_PARTY", &CUserSocket::AnswerJoinPartyPacket
);
L2_BIND_PACKET(kBindCWithdrawalParty, gPacketTableFromClient, 0x044, u"C_WITHDRAWAL_PARTY", &WithdrawPartyPacket);
L2_BIND_PACKET(kBindCOustPartyMember, gPacketTableFromClient, 0x045, u"C_OUST_PARTY_MEMBER", &OustPartyMemberPacket);
L2_BIND_PACKET(kBindCDismissParty, gPacketTableFromClient, 0x046, u"C_DISMISS_PARTY", &DismissPartyPacket);
L2_BIND_PACKET(kBindCMagicSkillList, gPacketTableFromClient, 0x038, u"C_MAGIC_SKILL_LIST", &MagicSkillListPacket);
L2_BIND_PACKET(kBindCMagicSkillUse, gPacketTableFromClient, 0x039, u"C_MAGIC_SKILL_USE", &MagicSkillUsePacket);
L2_BIND_PACKET(
    kBindCShowPledgeMemberListOpen,
    gPacketTableFromClient,
    0x04d,
    u"C_PLEDGE_REQ_SHOW_MEMBER_LIST_OPEN",
    &ShowPledgeMemberListOpenPacket
);
L2_BIND_PACKET(kBindCShortCutRegister, gPacketTableFromClient, 0x03d, u"C_SHORTCUT_REG", &ShortCutRegisterPacket);
L2_BIND_PACKET(kBindCShortCutDelete, gPacketTableFromClient, 0x03f, u"C_DEL_SHORTCUT", &ShortCutDeletePacket);
L2_BIND_PACKET(kBindCCannotMoveAnymore, gPacketTableFromClient, 0x047, u"C_CAN_NOT_MOVE_ANYMORE", &CannotMoveAnymorePacket);
L2_BIND_PACKET(kBindCMoveWithDelta, gPacketTableFromClient, 0x052, u"C_MOVE_WITH_DELTA", &MoveWithDeltaPacket);
L2_BIND_PACKET(kBindCGetOffVehicle, gPacketTableFromClient, 0x054, u"C_GETOFF_VEHICLE", &GetOffVehiclePacket);
L2_BIND_PACKET(kBindCIconAction, gPacketTableFromClient, 0x056, u"C_ICON_ACTION", &IconAction);
L2_BIND_PACKET(
    kBindCMoveToLocation, gPacketTableFromClient, 0x00f, u"C_MOVE_TO_LOCATION", &CUserSocket::MoveToLocationPacket
);
L2_BIND_PACKET(kBindCEnterWorld, gPacketTableFromClient, 0x011, u"C_ENTER_WORLD", &EnterWorldPacket);
L2_BIND_PACKET(kBindCAppearing, gPacketTableFromClient, 0x03a, u"C_APPEARING", &AppearingPacket);
L2_BIND_PACKET(kBindCRestart, gPacketTableFromClient, 0x057, u"C_RESTART", &RestartPacket);
L2_BIND_PACKET(
    kBindCValidatePosition, gPacketTableFromClient, 0x059, u"C_VALIDATE_POSITION", &CUserSocket::ValidateLocationPacket
);
L2_BIND_PACKET(kBindCSekCostume, gPacketTableFromClient, 0x05a, u"C_SEK_COSTUME", &SekCostume);
L2_BIND_PACKET(kBindCStartRotating, gPacketTableFromClient, 0x05b, u"C_START_ROTATING", &StartRotatingPacket);
L2_BIND_PACKET(kBindCFinishRotating, gPacketTableFromClient, 0x05c, u"C_FINISH_ROTATING", &FinishRotatingPacket);
L2_BIND_PACKET(kBindCRestartPoint, gPacketTableFromClient, 0x07d, u"C_RESTART_POINT", &RestartPointPacket);
L2_BIND_PACKET(kBindCListPartyWaiting, gPacketTableFromClient, 0x07f, u"C_LIST_PARTY_WAITING", &ReplyListPartyWaitingPacket);
L2_BIND_PACKET(kBindCManagePartyRoom, gPacketTableFromClient, 0x080, u"C_MANAGE_PARTY_ROOM", &ReplyManagePartyRoom);
L2_BIND_PACKET(kBindCJoinPartyRoom, gPacketTableFromClient, 0x081, u"C_JOIN_PARTY_ROOM", &ReplyJoinPartyRoom);
L2_BIND_PACKET(kBindCPrivateStoreListSet, gPacketTableFromClient, 0x031, u"C_PRIVATE_STORE_LIST_SET", &PrivateStoreListSet);
L2_BIND_PACKET(
    kBindCPrivateStoreManageCancel,
    gPacketTableFromClient,
    0x032,
    u"C_PRIVATE_STORE_MANAGE_CANCEL",
    &PrivateStoreManageCancel
);
L2_BIND_PACKET(
    kBindCPrivateStoreBuyListSend, gPacketTableFromClient, 0x083, u"C_PRIVATE_STORE_BUY_LIST_SEND", &PrivateStoreBuyListSend
);
L2_BIND_PACKET(kBindCPrivateStoreQuit, gPacketTableFromClient, 0x096, u"C_PRIVATE_STORE_QUIT", &PrivateStoreQuit);
L2_BIND_PACKET(kBindCPrivateStoreSetMsg, gPacketTableFromClient, 0x097, u"C_PRIVATE_STORE_SET_MSG", &PrivateStoreSetMsg);
L2_BIND_PACKET(
    kBindCPrivateStoreBuyListSet, gPacketTableFromClient, 0x09a, u"C_PRIVATE_STORE_BUY_LIST_SET", &PrivateStoreBuyListSet
);
L2_BIND_PACKET(
    kBindCPrivateStoreBuyManageCancel,
    gPacketTableFromClient,
    0x09b,
    u"C_PRIVATE_STORE_BUY_MANAGE_CANCEL",
    &PrivateStoreBuyManageCancel
);
L2_BIND_PACKET(kBindCPrivateStoreBuyQuit, gPacketTableFromClient, 0x09c, u"C_PRIVATE_STORE_BUY_QUIT", &PrivateStoreBuyQuit);
L2_BIND_PACKET(
    kBindCPrivateStoreBuySetMsg, gPacketTableFromClient, 0x09d, u"C_PRIVATE_STORE_BUY_SET_MSG", &PrivateStoreBuySetMSG
);
L2_BIND_PACKET(
    kBindCPrivateStoreBuyBuyListSend,
    gPacketTableFromClient,
    0x09f,
    u"C_PRIVATE_STORE_BUY_BUY_LIST_SEND",
    &PrivateStoreBuyBuyListSend
);
L2_BIND_PACKET(kBindCUnequipItem, gPacketTableFromClient, 0x016, u"C_UNEQUIP_ITEM", &UnequipItemPacket);
L2_BIND_PACKET(kBindCDropItem, gPacketTableFromClient, 0x017, u"C_DROP_ITEM", &CUserSocket::DropItemPacket);
L2_BIND_PACKET(kBindCUseItem, gPacketTableFromClient, 0x019, u"C_USE_ITEM", &UseItemPacket);
L2_BIND_PACKET(kBindCTradeRequest, gPacketTableFromClient, 0x01a, u"C_TRADE_REQUEST", &CUserSocket::TradeRequestPacket);
L2_BIND_PACKET(kBindCTradeAdd, gPacketTableFromClient, 0x01b, u"C_TRADE_ADD", &CUserSocket::TradeAddItemsPacket);
L2_BIND_PACKET(kBindCTradeDone, gPacketTableFromClient, 0x01c, u"C_TRADE_DONE", &CUserSocket::TradeDonePacket);
L2_BIND_PACKET(
    kBindCCrystallizeItem, gPacketTableFromClient, 0x02f, u"C_CRYSTALLIZE_ITEM", &CUserSocket::ItemCrystallizePacket
);
L2_BIND_PACKET(kBindCSellList, gPacketTableFromClient, 0x037, u"C_SELL_LIST", &SellPacket);
L2_BIND_PACKET(kBindCBuyList, gPacketTableFromClient, 0x040, u"C_BUY_LIST", &BuyPacket);
L2_BIND_PACKET(kBindCTradeStart, gPacketTableFromClient, 0x055, u"C_TRADE_START", &CUserSocket::TradeStartPacket);
L2_BIND_PACKET(kBindCRecipeBookOpen, gPacketTableFromClient, 0x0b5, u"C_RECIPE_BOOK_OPEN", &RequestRecipeBookOpen);
L2_BIND_PACKET(kBindCRecipeItemDelete, gPacketTableFromClient, 0x0b6, u"C_RECIPE_ITEM_DELETE", &RequestRecipeBookItemDelete);
L2_BIND_PACKET(
    kBindCRecipeItemMakeInfo, gPacketTableFromClient, 0x0b7, u"C_RECIPE_ITEM_MAKE_INFO", &RequestRecipeBookItemInfo
);
L2_BIND_PACKET(
    kBindCRecipeItemMakeSelf, gPacketTableFromClient, 0x0b8, u"C_RECIPE_ITEM_MAKE_SELF", &RequestRecipeBookItemMake
);
L2_BIND_PACKET(
    kBindCRecipeShopMessageSet, gPacketTableFromClient, 0x0ba, u"C_RECIPE_SHOP_MESSAGE_SET", &RequestRecipeShopSetMsg
);
L2_BIND_PACKET(kBindCRecipeShopListSet, gPacketTableFromClient, 0x0bb, u"C_RECIPE_SHOP_LIST_SET", &RequestRecipeShopSetList);
L2_BIND_PACKET(
    kBindCRecipeShopManageQuit, gPacketTableFromClient, 0x0bc, u"C_RECIPE_SHOP_MANAGE_QUIT", &RequestRecipeShopQuit
);
L2_BIND_PACKET(
    kBindCRecipeShopManageCancel, gPacketTableFromClient, 0x0bd, u"C_RECIPE_SHOP_MANAGE_CANCEL", &RequestRecipeShopCancel
);
L2_BIND_PACKET(
    kBindCRecipeShopMakeInfo, gPacketTableFromClient, 0x0be, u"C_RECIPE_SHOP_MAKE_INFO", &RequestRecipeShopItemInfo
);
L2_BIND_PACKET(kBindCRecipeShopMakeDo, gPacketTableFromClient, 0x0bf, u"C_RECIPE_SHOP_MAKE_DO", &RequestRecipeShopItemMake);
L2_BIND_PACKET(
    kBindCRecipeShopSellList, gPacketTableFromClient, 0x0c0, u"C_RECIPE_SHOP_SELL_LIST", &RequestRecipeShopSellList
);

// Current pet/petition registrations: initializers 0x00dcfce4,
// 0x00dcfa74, 0x00dcfaa4, 0x00dcfc54, 0x00dcfc84,
// 0x00dcfcb4, 0x00dcfd74 and 0x00dd0824.
L2_BIND_PACKET(kBindCGetItemFromPet, gPacketTableFromClient, 0x02c, u"C_GET_ITEM_FROM_PET", &GetItemFromPetPacket);
L2_BIND_PACKET(kBindCPetition, gPacketTableFromClient, 0x089, u"C_PETITION", &PetitionPacket);
L2_BIND_PACKET(kBindCPetitionCancel, gPacketTableFromClient, 0x08a, u"C_PETITION_CANCEL", &PetitionCancelPacket);
L2_BIND_PACKET(kBindCChangePetName, gPacketTableFromClient, 0x093, u"C_CHANGE_PET_NAME", &ChangePetNamePacket);
L2_BIND_PACKET(kBindCPetUseItem, gPacketTableFromClient, 0x094, u"C_PET_USE_ITEM", &PetUseItemPacket);
L2_BIND_PACKET(kBindCGiveItemToPet, gPacketTableFromClient, 0x095, u"C_GIVE_ITEM_TO_PET", &GiveItemToPetPacket);
L2_BIND_PACKET(kBindCPetGetItem, gPacketTableFromClient, 0x098, u"C_PET_GET_ITEM", &PetGetItem);
L2_BIND_PACKET(kBindCPetitionVote, gPacketTableFromClient, 0x0c9, u"C_PETITION_VOTE", &RequestPetitionVote);

// Current chat/friend registrations: initializers 0x00dcf294,
// 0x00dced54, 0x00dd08b4, 0x00dd08e4, 0x00dd0944,
// 0x00dcf444, 0x00dcf474 and 0x00dcf4d4.
L2_BIND_PACKET(kBindCGiveNickname, gPacketTableFromClient, 0x00b, u"C_GIVE_NICKNAME", &GiveNicknamePacket);
L2_BIND_PACKET(kBindCSay2, gPacketTableFromClient, 0x049, u"C_SAY2", &SayPacket2);
L2_BIND_PACKET(kBindCL2FriendList, gPacketTableFromClient, 0x06a, u"C_L2_FRIEND_LIST", &ReplyL2FriendList);
L2_BIND_PACKET(kBindCL2FriendSay, gPacketTableFromClient, 0x06b, u"C_L2_FRIEND_SAY", &L2FriendSayPacket);
L2_BIND_PACKET(kBindCMsnChatLog, gPacketTableFromClient, 0x06d, u"C_MSN_CHAT_LOG", &MSNChatLogPacket);
L2_BIND_PACKET(kBindCFriendAddRequest, gPacketTableFromClient, 0x077, u"C_FRIEND_ADD_REQUEST", &FriendAddRequest);
L2_BIND_PACKET(kBindCFriendAddReply, gPacketTableFromClient, 0x078, u"C_FRIEND_ADD_REPLY", &FriendAddReply);
L2_BIND_PACKET(
    kBindCFriendRemove, gPacketTableFromClient, 0x07a, u"C_FRIEND_REMOVE", static_cast<PacketFunc>(&FriendRemove)
);

// Current social/misc registrations: initializers 0x00dcfad4,
// 0x00dd02b4, 0x00dd0524 and 0x00dd0554.
L2_BIND_PACKET(kBindCGMList, gPacketTableFromClient, 0x08b, u"C_GMLIST", &GMListPacket);
L2_BIND_PACKET(kBindCSnoopQuit, gPacketTableFromClient, 0x0b4, u"C_SNOOP_QUIT", &SnoopQuit);
L2_BIND_PACKET(kBindCObserverEnd, gPacketTableFromClient, 0x0c1, u"C_OBSERVER_END", &ObserverEndPacket);
L2_BIND_PACKET(kBindCVoteSociality, gPacketTableFromClient, 0x0c2, u"C_VOTE_SOCIALITY", &RequestVoteSociality);
L2_BIND_PACKET(kBindCConfirmDialog, gPacketTableFromClient, 0x0c6, u"C_CONFIRM_DLG", &CUserSocket::ConfirmDialog);

// Current alliance registrations: initializers 0x00dcfd44 and
// 0x00dcfb04..0x00dcfbf4, guards 0x2129719a and 0x2129718e..0x21297193.
L2_BIND_PACKET(kBindCAllianceInfo, gPacketTableFromClient, 0x02e, u"C_ALLIANCE_INFO", &AllianceInfoPacket);
L2_BIND_PACKET(kBindCJoinAlliance, gPacketTableFromClient, 0x08c, u"C_JOIN_ALLIANCE", &JoinAlliancePacket);
L2_BIND_PACKET(
    kBindCAnswerJoinAlliance, gPacketTableFromClient, 0x08d, u"C_ANSWER_JOIN_ALLIANCE", &AnswerJoinAlliancePacket
);
L2_BIND_PACKET(kBindCWithdrawAlliance, gPacketTableFromClient, 0x08e, u"C_WITHDRAW_ALLIANCE", &WithdrawAlliancePacket);
L2_BIND_PACKET(
    kBindCOustAllianceMemberPledge,
    gPacketTableFromClient,
    0x08f,
    u"C_OUST_ALLIANCE_MEMBER_PLEDGE",
    &OustAllianceMemberPacket
);
L2_BIND_PACKET(kBindCDismissAlliance, gPacketTableFromClient, 0x090, u"C_DISMISS_ALLIANCE", &DismissAlliancePacket);
L2_BIND_PACKET(kBindCSetAllianceCrest, gPacketTableFromClient, 0x091, u"C_SET_ALLIANCE_CREST", &SetAllianceCrestPacket);
L2_BIND_PACKET(kBindCManagePledgePower, gPacketTableFromClient, 0x0cc, u"C_MANAGE_PLEDGE_POWER", &RequestManagePledgePower);

// Current warehouse/package registrations: initializers 0x00dcec04,
// 0x00dcec34 and 0x00dd0044.
L2_BIND_PACKET(
    kBindCWarehouseDepositList, gPacketTableFromClient, 0x03b, u"C_WAREHOUSE_DEPOSIT_LIST", &WarehouseDepositPacket
);
L2_BIND_PACKET(
    kBindCWarehouseWithdrawList, gPacketTableFromClient, 0x03c, u"C_WAREHOUSE_WITHDRAW_LIST", &WarehouseWithdrawPacket
);
L2_BIND_PACKET(
    kBindCRequestPackageSendableItemList,
    gPacketTableFromClient,
    0x0a7,
    u"C_REQUEST_PACKAGE_SENDABLE_ITEM_LIST",
    &RequestPackageSendableItemList
);

// Current intentional DummyUserPacket registrations. These are not omitted
// defaults: every row stores its exact current name and marks the slot
// registered before TestPacketTableRegistration scans C_PROTOCOL.
L2_BIND_PACKET(kBindCDummyNotUse1, gPacketTableFromClient, 0x015, u"C_NOT_USE_1", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse2, gPacketTableFromClient, 0x041, u"C_NOT_USE_2", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse3, gPacketTableFromClient, 0x03e, u"C_NOT_USE_3", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse4, gPacketTableFromClient, 0x04b, u"C_NOT_USE_4", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse5, gPacketTableFromClient, 0x04c, u"C_NOT_USE_5", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse6, gPacketTableFromClient, 0x04e, u"C_NOT_USE_6", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse7, gPacketTableFromClient, 0x04f, u"C_NOT_USE_7", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse8, gPacketTableFromClient, 0x051, u"C_NOT_USE_8", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse9, gPacketTableFromClient, 0x058, u"C_NOT_USE_9", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse10, gPacketTableFromClient, 0x004, u"C_NOT_USE_10", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse11, gPacketTableFromClient, 0x006, u"C_NOT_USE_11", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse12, gPacketTableFromClient, 0x007, u"C_NOT_USE_12", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse13, gPacketTableFromClient, 0x008, u"C_NOT_USE_13", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse14, gPacketTableFromClient, 0x00a, u"C_NOT_USE_14", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse15, gPacketTableFromClient, 0x05d, u"C_NOT_USE_15", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse16, gPacketTableFromClient, 0x064, u"C_NOT_USE_16", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse17, gPacketTableFromClient, 0x068, u"C_NOT_USE_17", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse18, gPacketTableFromClient, 0x069, u"C_NOT_USE_18", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse19, gPacketTableFromClient, 0x030, u"C_NOT_USE_19", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse20, gPacketTableFromClient, 0x082, u"C_NOT_USE_20", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse21, gPacketTableFromClient, 0x084, u"C_NOT_USE_21", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse22, gPacketTableFromClient, 0x02d, u"C_NOT_USE_22", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse23, gPacketTableFromClient, 0x099, u"C_NOT_USE_23", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse24, gPacketTableFromClient, 0x09e, u"C_NOT_USE_24", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse25, gPacketTableFromClient, 0x0a0, u"C_NOT_USE_25", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse26, gPacketTableFromClient, 0x0a1, u"C_NOT_USE_26", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse27, gPacketTableFromClient, 0x0a2, u"C_NOT_USE_27", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse28, gPacketTableFromClient, 0x0a3, u"C_NOT_USE_28", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse29, gPacketTableFromClient, 0x0a4, u"C_NOT_USE_29", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse30, gPacketTableFromClient, 0x0a5, u"C_NOT_USE_30", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse31, gPacketTableFromClient, 0x0b9, u"C_NOT_USE_31", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse32, gPacketTableFromClient, 0x0cf, u"C_NOT_USE_32", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse33, gPacketTableFromClient, 0x0ca, u"C_NOT_USE_33", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse34, gPacketTableFromClient, 0x010, u"C_NOT_USE_34", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse35, gPacketTableFromClient, 0x01d, u"C_NOT_USE_35", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse36, gPacketTableFromClient, 0x01e, u"C_NOT_USE_36", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse37, gPacketTableFromClient, 0x020, u"C_NOT_USE_37", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse38, gPacketTableFromClient, 0x021, u"C_NOT_USE_38", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse39, gPacketTableFromClient, 0x025, u"C_NOT_USE_39", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse40, gPacketTableFromClient, 0x02a, u"C_NOT_USE_40", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse41, gPacketTableFromClient, 0x033, u"C_NOT_USE_41", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyNotUse42, gPacketTableFromClient, 0x04a, u"C_NOT_USE_42", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyGetItem, gPacketTableFromClient, 0x018, u"C_GET_ITEM", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummySocialAction, gPacketTableFromClient, 0x034, u"C_SOCIAL_ACTION", &DummyUserPacket);
L2_BIND_PACKET(kBindCDummyPledgeExtendedInfo, gPacketTableFromClient, 0x066, u"C_PLEDGE_EXTENDED_INFO", &DummyUserPacket);

} // namespace

UserSocketIdMap g_UserSocketMap;
UserSocketPlayingMap g_UserSocketMapPlaying;
UserSocketEnterCharMap g_UserSocketMapEnterChar;
AccountLoginStepMap g_accountLoginStep;
AccountToNpGuidMap g_account2NPGuidMap;
NpGuidToAccountMap g_npGuid2AccountMap;
UserSocketAccountMap accountMap;
CYieldLock accountMapSect;
ReadyAccountMap g_ReadyAccountMap;
CYieldLock g_ReadyAccountMapSect;
volatile std::int64_t g_nSumOfAvgPacketReadCount = 0;
volatile std::int32_t g_nCountOfAvgPacketReadSum = 0;
volatile std::int64_t g_nSumOfMaxPacketReadCount = 0;
volatile std::int32_t g_nCountOfMaxPacketReadSum = 0;
volatile std::int32_t g_nMaxPacketReadCountPerSec[16]{};
bool aPacketActionFailTable[209]{};
bool aPacketExBlockTable_ForRaidUser[514]{};
bool aPacketBlockTable_ForRaidUser[255]{};
bool aPacketExAllowTable_ForRaidUser[352]{};
bool aPacketAllowTable_ForRaidUser[209]{};
bool aPacketBlockTable_CannotControl[209]{};
std::uint64_t g_nCheckInterval = 0;
volatile std::int32_t g_nTotalIncomingPacket = 0;
std::int32_t g_nMostTimeConsumingJob1 = 0;
std::uint32_t g_nMostTimeConsumingJob1Time = 0;
std::int32_t g_nMostTimeConsumingJob2 = 0;
std::uint32_t g_nMostTimeConsumingJob2Time = 0;
std::int32_t g_nMostTimeConsumingJob3 = 0;
std::uint32_t g_nMostTimeConsumingJob3Time = 0;
// Current UserSocket.obj counters at 0x210775fc/0x21077600. SayPacket2 and
// L2FriendSayPacket increment them. CPerfMon::ReportSayCount resets them.
volatile std::int32_t g_nSayCount = 0;
volatile std::int32_t g_nSaySize = 0;
int g_nClientFileDecryptionKeyLen = 0;
char g_nClientFileDecryptionKey[1024]{};
unsigned int CUserSocket::s_nBaseId = 0;

void InitClientDecryptionKey() {
    // Current UserSocket.obj 0x00c09174. The real listener startup calls
    // this once immediately before CPerfMon::Init. Do not initialize it
    // lazily from VersionPacket.
    constexpr char encryptedKey[] = "97df398472ddf737ef0a0cd17e8d172f"
                                    "0fef1661a38a8ae1d6e829bc1c6e4c3c"
                                    "fc19292dda9ef90175e46e7394a18850"
                                    "b6417d03be6eea274d3ed1dde5b5d7bd"
                                    "e72cc0a0b71d03608655633881793a02"
                                    "c9a67d9ef2b45eb7c08d4be329083ce"
                                    "450e68f7867b6749314d40511d09bc57"
                                    "44551baa86a89dc38123dc1668fd72d83";
    constexpr std::array<char, 8> xorKey{'0', '0', '0', '0', '0', '0', '3', '5'};

    g_nClientFileDecryptionKeyLen = 0x100;
    std::size_t xorIndex = 0;
    for (std::size_t index = 0; index < 0x100; ++index) {
        g_nClientFileDecryptionKey[index] = static_cast<char>(
            static_cast<unsigned char>(encryptedKey[index]) ^ static_cast<unsigned char>(xorKey[xorIndex])
        );
        ++xorIndex;
        if (xorIndex == xorKey.size()) {
            xorIndex = 0;
        }
    }
}

bool CheckIPIsAdmin(char16_t* address) {
    // Current UserSocket.obj 0x00c11b68: exact, ordered, case-sensitive
    // equality. Compare code units directly so the portable wchar_t width
    // does not introduce allocation or normalization.
    for (const std::u16string& configuredAddress : g_AdminIP) {
        const std::u16string_view configured = runtime::StringView(configuredAddress);
        std::size_t index = 0;
        while (index < configured.size() && address[index] != L'\0' &&
            static_cast<std::uint32_t>(configured[index]) == static_cast<std::uint32_t>(address[index])) {
            ++index;
        }
        if (index == configured.size() && address[index] == L'\0') {
            return true;
        }
    }
    return false;
}

CUserSocket::CUserSocket() : CSocket(std::numeric_limits<std::uint64_t>::max(), 1) {
    L2_GUARD_FUNCTION();
    m_BuilderSetting = {false, false};
    m_RaidInfo = {0, URS_NONE, 0, 0};
    kHtmlPacketQueue.pushpos = 0;
    kHtmlPacketQueue.dwReceiveTime.fill(std::numeric_limits<std::uint32_t>::max());
    SetAtConstructor(false, 0);
}

CUserSocket::CUserSocket(std::uint64_t socket) : CSocket(socket, 0x200) {
    L2_GUARD_FUNCTION();
    m_BuilderSetting = {false, false};
    m_RaidInfo = {0, URS_NONE, 0, 0};
    kHtmlPacketQueue.pushpos = 0;
    kHtmlPacketQueue.dwReceiveTime.fill(std::numeric_limits<std::uint32_t>::max());
    const auto id = static_cast<unsigned int>(
        runtime::raw_atomic::IncrementI32(reinterpret_cast<volatile std::int32_t*>(&s_nBaseId))
    );
    SetAtConstructor(true, id);
}

CUserSocket::~CUserSocket() {
    L2_GUARD_FUNCTION();
    runtime::client_protocol::ForgetSocket(this);
    g_ServerContext.UserSocket.fetch_sub(1, std::memory_order_relaxed);
    m_nQueueTicketAccountID = 0;
    m_bPassQueueTicket = false;
    m_pUser = nullptr;
}

CSocket* CUserSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    g_ServerContext.UserSocket.fetch_add(1, std::memory_order_relaxed);
    return new CUserSocket(socket);
}

void CUserSocket::SetAtConstructor(bool active, unsigned int id) {
    L2_GUARD_FUNCTION();

    runtime::client_protocol::ForgetSocket(this);

    HackInfo.SendListTick = 0;
    runtime::raw_atomic::StoreI32Relaxed(&HackInfo.EnterWorldHackCheckCount, 0);
    m_sAccountName[0] = SharedUtilsWideChar{};
    m_sAccountNameChar[0] = '\0';
    m_pPacketTable = gPacketTableFromClient.GetData();
    m_nSitTime = 0;
    m_nLastSayTime = 0;
    runtime::raw_atomic::StoreI32Relaxed(&m_nLastUsePacketId, 0);
    m_OldTime = static_cast<std::int32_t>(runtime::GetTickCount());
    m_nId = id;
    if (id != 0) {
        g_UserSocketMap.Insert(static_cast<int>(id), this, "UserSocket.cpp", 0x2c52);
    }

    m_nUseTimeSec = 0;
    m_nSocketUseTimeStamp = 0;
    m_nLastActionStamp = 0;
    m_pUser = nullptr;
    m_nAccountID = 0;
    m_bLoggedIn = false;
    m_bActive = active;
    m_bAuthCharge = true;
    m_nLeftTime = 0;
    m_nPayKind = 5;
    m_nUserStat = 0;
    m_nWarnFlag = 0;
    m_nReservations = 0;
    m_nAge = 0;
    m_nCloseReason = SCR_NONE;
    m_nLastTimeCheck = 0;
    runtime::raw_atomic::StoreI32Relaxed(&m_nRecvPingCount, 0);
    m_nSitTimeStamp = 0;
    m_bForceLogin = false;
    m_b22to6DisableGameForceLogin = false;
    m_nWeekPlayTime = 0;
    m_nSocketCharacterTimeStamp = 0;
    std::memset(&m_tmLastSaveWeekPlayTime, 0, sizeof(m_tmLastSaveWeekPlayTime));
    m_bIsNew = true;
    m_nRelaxCheckInterval = 0;
    m_nRestTime_china = 0;
    m_nPlayTime_china = 0;
    m_nStartPlayTimeStamp_china = 0;
    m_bApplyChinaFatigueSystem = false;
    m_bEconomicRestriction = false;
    m_iPremiumLevel = 0;
    InitCheckPacketAccept();
    InitShuffledPacket();
    m_nTimeForChangingMergedName = 0;
    m_nPacketList.fill(0);
    m_nPacketTime.fill(0);
    m_nPacketConunt = 0;
    m_nLastPacketLogTick = 0;
    m_nMacAddress = 0;
    m_BuilderSetting = {false, false};
    m_sPCCafeCode.fill('\0');
    m_UseSendGMListCount = 0;
    m_LastUseSendGMListTime = 0;
    m_wsidReceivedTick = 0;
    m_eCharacterDeactivateReasons.fill(CharacterDeactivateReason::CDR_DEACTIVATE_DEFAULT);
    m_nDormantMarkingTime = 0;
    m_nDormantType = 0;
    m_nMaxCharId = 0;
    m_nMaxLevel = 0;
    m_ChargeStart = 0;
    m_bReenterWorld = false;
    m_szNPUserGUIDA[0] = '\0';
    for (std::size_t index = 0; index < m_szClientINI.size(); ++index) {
        m_szClientINI[index].fill('\0');
        m_bNeedUpdateClientINI[index] = false;
    }
    m_bUserCountProcessed = false;
    m_nQueueTicketAccountID = 0;
    m_bPassQueueTicket = false;
}

void CUserSocket::OnRead() {
    // Current L2Server 0x00c1b154.
    L2_GUARD_FUNCTION();

    if (m_Mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    DefaultIOBuffer* const readBuffer = m_pReadBuf;
    const int readSize = static_cast<int>(readBuffer->m_dwSize);
    int packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_Mode == SocketMode::SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }

            const std::uint8_t* const header = readBuffer->m_Buffer.data() + packetOffset;
            const std::uint32_t packetLengthBits =
                static_cast<std::uint32_t>(header[0]) + (static_cast<std::uint32_t>(header[1]) << 8U) - 2U;
            std::memcpy(&m_nPacketLen, &packetLengthBits, sizeof(m_nPacketLen));
            if (packetLengthBits - 1U > 0x4ffdU) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"%d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     m_nPacketLen)
                // Log.Add(
                //     LOG_WARN,
                //     "[socket] Closing socket %u: declared packet body length %d is outside 1..20478 bytes (excluding the
                //     2-byte header).", m_nId, m_nPacketLen)
                L2_LOG_WARN(
                    Log,
                    "[socket] Closing socket %u: declared packet body length %d is outside 1..20478 bytes (excluding the "
                    "2-byte header).",
                    m_nId, m_nPacketLen
                );
                m_Mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }

            m_Mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        } else if (m_Mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + m_nPacketLen) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        m_nPacketCount = runtime::AddInt32Wrapping(m_nPacketCount, 1);
        if ((g_nCheckInterval & static_cast<std::uint32_t>(m_nPacketCount)) == 0) {
            m_OldTime = static_cast<std::int32_t>(runtime::GetTickCount());
        }

        std::uint8_t* packet = readBuffer->m_Buffer.data() + packetOffset;
        if (m_nInKey.IsEnabled()) {
            Decrypt(packet, m_nInKey, m_nPacketLen);
        }
        // Portable: LU4 client packets become this server's (Portable/Lu4Protocol.h).
        if ((!m_nInKey.IsEnabled() && lu4::IsVersionPacket(packet, m_nPacketLen)) ||
            runtime::client_protocol::UsesLu4Protocol(this)) {
            thread_local std::vector<std::uint8_t> lu4Packet;
            if (!lu4::TranslateIncoming(this, packet, m_nPacketLen, lu4Packet)) {
                packetOffset += m_nPacketLen;
                m_Mode = SocketMode::SM_READ_LEN;
                continue;
            }
            packet = lu4Packet.data();
        }

        g_Timer.BeginJob(JOB_USERSOCKET__TOTAL);
        runtime::raw_atomic::AddI32(&g_nTotalIncomingPacket, 1);
        const std::uint32_t packetStart = runtime::GetTickCount();
        const int opcode = g_bEnablePacketShuffle ? GetOriginalPacket(packet[0]) : static_cast<int>(packet[0]);
        if (opcode > 0xd0) {
            const std::u16string address = GetIPAddressString();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[usersocket]unknown protocol %d account_id[%d] ip[%s]",
            //     opcode,
            //     m_nAccountID,
            //     address.c_str())
            // Log.Add(
            //     LOG_WARN,
            //     L"[socket] Closing socket %u, account %u, peer %s: basic opcode 0x%02x is outside the supported range
            //     0x00..0xd0.", m_nId, m_nAccountID, address.c_str(), static_cast<unsigned>(opcode))
            L2_LOG_WARN(
                Log,
                u"[socket] Closing socket %u, account %u, peer %s: basic opcode 0x%02x is outside the supported range "
                u"0x00..0xd0.",
                m_nId, m_nAccountID, address.c_str(), static_cast<unsigned>(opcode)
            );
            m_Mode = SocketMode::SM_CLOSE;
            g_Timer.EndJob(JOB_USERSOCKET__TOTAL);
            Close();
            return;
        }

        if (g_bUserToServerPacketStatMonitor) {
            int statisticOpcode = opcode;
            int statisticSize = m_nPacketLen;
            if (opcode >= 0xd0) {
                statisticOpcode += static_cast<int>(packet[1]) + (static_cast<int>(packet[2]) << 8);
                statisticSize -= 2;
            }
            CPacketStat<560, 208>::Inst().Accumulate(statisticOpcode, statisticSize);
        }

        User* const user = GetUser();
        const PacketFunc callback =
            runtime::client_protocol::ResolveBasicIncomingCallback(this, opcode, m_pPacketTable[opcode]);
        const int policyOpcode = opcode;
        if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
            const bool closeRequested = callback(this, packet + 1);
            SetLastPacketNo(opcode);
            if (closeRequested) {
                if (m_nCloseReason != SCR_RETURN_TO_ORIGIN) {
                    m_Mode = SocketMode::SM_CLOSE;
                    g_Timer.EndJob(JOB_USERSOCKET__TOTAL);
                    Close();
                    return;
                }
                SetCloseTimer();
                // Noise: Routine return-to-origin timer setup. It does not indicate an error or a completed close.
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]"
                //     L"SetCloseTimer done: %p",
                //     L"UserSocket.cpp",
                //     0x2e95,
                //     this)
            }
        } else {
            SetLastPacketNo(opcode);
            if (!CheckMaxPacketAccept(policyOpcode) || !user->PacketAcceptable(policyOpcode)) {
                if (opcode == 0x57) {
                    Send("cd", 0x71, 0);
                }
                if (aPacketActionFailTable[policyOpcode]) {
                    Send("cd", 0x1f, 0);
                }
            } else {
                const bool closeRequested = callback(this, packet + 1);
                if (closeRequested) {
                    m_Mode = SocketMode::SM_CLOSE;
                    g_Timer.EndJob(JOB_USERSOCKET__TOTAL);
                    Close();
                    return;
                }
            }
        }

        const std::uint32_t packetEnd = runtime::GetTickCount();
        const std::uint32_t elapsed = packetEnd - packetStart;
        if (opcode != g_nMostTimeConsumingJob1 && opcode != g_nMostTimeConsumingJob2 && opcode != g_nMostTimeConsumingJob3 &&
            opcode != 0x34) {
            const std::uint64_t comparisonElapsed =
                static_cast<std::uint64_t>(packetEnd) - static_cast<std::uint64_t>(packetStart);
            if (g_nMostTimeConsumingJob1Time < comparisonElapsed) {
                g_nMostTimeConsumingJob1Time = elapsed;
                g_nMostTimeConsumingJob1 = opcode;
            } else if (g_nMostTimeConsumingJob2Time < comparisonElapsed) {
                g_nMostTimeConsumingJob2Time = elapsed;
                g_nMostTimeConsumingJob2 = opcode;
            } else if (g_nMostTimeConsumingJob3Time < comparisonElapsed) {
                g_nMostTimeConsumingJob3Time = elapsed;
                g_nMostTimeConsumingJob3 = opcode;
            }
        }
        IncreaseClientPacketTime(opcode, static_cast<std::int32_t>(elapsed));

        packetOffset += m_nPacketLen;
        m_Mode = SocketMode::SM_READ_LEN;
        g_Timer.EndJob(JOB_USERSOCKET__TOTAL);
    }

    Read(0);
}

void CUserSocket::TimerExpired(int id) {
    // Current L2Server 0x00c25bbc.
    L2_GUARD_FUNCTION();

    constexpr std::uint32_t raidPingMask =
        static_cast<std::uint32_t>(URS_ACCEPTED_REQUEST) | static_cast<std::uint32_t>(URS_ON_RAID);
    const auto initialRaidStatus = static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus);
    if ((initialRaidStatus & raidPingMask) != 0U && !g_bPingOnRaid) {
        return;
    }

    if (!CIOObject::IsInShortTime(m_nLastTimeCheck, 30000U)) {
        const std::uint32_t tick = runtime::GetTickCount();
        if (!g_bPingCountOff && !m_BuilderSetting.m_bPingCountOff) {
            const std::int32_t pingCount = runtime::raw_atomic::IncrementI32(&m_nRecvPingCount);
            if (pingCount > 4) {
                User* const user = GetUser();
                if (user != nullptr && !user->IsBuilder()) {
                    const FVector position = user->GetPos();
                    const std::wstring accountName =
                        Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
                    // Log.Add(
                    //     LOG_BOTH,
                    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d"
                    //     L",,,,,,,,,,,,,,%s,%s,,,",
                    //     static_cast<std::int64_t>(0x59c),
                    //     static_cast<std::int64_t>(user->GetDBID()),
                    //     static_cast<std::int64_t>(user->GetAccountID()),
                    //     static_cast<std::int64_t>(
                    //         runtime::Win64CvttDoubleToInt32(position.x)),
                    //     static_cast<std::int64_t>(
                    //         runtime::Win64CvttDoubleToInt32(position.y)),
                    //     static_cast<std::int64_t>(
                    //         runtime::Win64CvttDoubleToInt32(position.z)),
                    //     user->GetName(),
                    //     accountName.c_str())
                    Log.Add(
                        LOG_BOTH,
                        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d"
                        u",,,,,,,,,,,,,,%s,%s,,,",
                        static_cast<std::int64_t>(0x59c), static_cast<std::int64_t>(user->GetDBID()),
                        static_cast<std::int64_t>(user->GetAccountID()),
                        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), user->GetName(),
                        accountName.c_str()
                    );
                }
                Close();
                return;
            }
        }

        CIOObject::SetShortTimeStamp(&m_nLastTimeCheck, 0);
        Send("cd", 0xd9, tick);

        if (g_bBuilderLogAdd) {
            User* const user = GetUser();
            if (user != nullptr && user->IsBuilder()) {
                Agent* const agent = user->m_AgentMap[GAME_GUARD];
                if (agent != nullptr) {
                    const auto* const gameGuardAgent = static_cast<const GameGuardAgent*>(agent);
                    CLogFormatter formatter;
                    formatter.WriteLog4(
                        0x5a2, user, nullptr, nullptr, nullptr, nullptr, static_cast<std::int64_t>(m_nLastTimeCheck),
                        static_cast<std::int64_t>(m_nRecvPingCount),
                        static_cast<std::int64_t>(gameGuardAgent->m_lastRequestTick),
                        static_cast<std::int64_t>(gameGuardAgent->m_lastResponseTick), 0, 0, 0, 0, 0, 0, 0
                    );
                }
            }
        }
    }

    const auto currentRaidStatus = static_cast<std::uint32_t>(m_RaidInfo.m_eRaidStatus);
    if ((currentRaidStatus & raidPingMask) == 0U) {
        const std::int32_t now = runtime::crt::Time32Now();
        if (runtime::raw_atomic::LoadI32Relaxed(&m_nLastUsePacketId) == 0x2b &&
            g_nAutoLogoutTimeLimit < runtime::SubtractInt32Wrapping(now, m_nLastActionStamp)) {
            Send("cd", 0xb0, 0);
            Close();
            return;
        }

        if (m_nSocketCloseTimer < 1) {
            constexpr std::array<int, 8> warningMinutes{1, 2, 3, 4, 5, 10, 20, 30};

            if (m_bAuthCharge && (::g_nCountryCode == 4 || !g_bFreeOpenServer) &&
                (m_nPayKind == 1 || m_nPayKind == 3 || (m_nPayKind == 4 && m_nReservations == 0))) {
                const int leftTime = GetCurrentLeftTime();
                if (leftTime < 1) {
                    if (m_nPayKind != 4) {
                        SendSystemMessage(SystemMessageId::PREPAID_TIME_EXPIRED);
                        if (leftTime < -10) {
                            SetCloseTimer();
                        }
                    }
                } else {
                    for (const int minutes : warningMinutes) {
                        const int upper = runtime::MultiplyInt32Wrapping(minutes, 60);
                        if (runtime::AddInt32Wrapping(upper, -3) < leftTime && leftTime < upper) {
                            const unsigned int messageId = m_nPayKind == 4
                                ? static_cast<unsigned int>(SystemMessageId::PAID_USAGE_MINUTES_REMAINING)
                                : (m_nPayKind == 1
                                          ? static_cast<unsigned int>(SystemMessageId::PERSONAL_ACCOUNT_MINUTES_REMAINING)
                                          : static_cast<unsigned int>(SystemMessageId::PC_CAFE_PREPAID_MINUTES_REMAINING));
                            SendSystemMessage_d(messageId, minutes);
                        }
                    }
                }
            }

            if (g_nPlayTimeLimit > 0 && !m_bForceLogin) {
                if (m_nRelaxCheckInterval < 120) {
                    ++m_nRelaxCheckInterval;
                } else {
                    const runtime::SystemTime hostTime = runtime::GetLocalTimeNow();
                    _SYSTEMTIME localTime{hostTime.wYear, hostTime.wMonth, hostTime.wDayOfWeek, hostTime.wDay,
                        hostTime.wHour, hostTime.wMinute, hostTime.wSecond, hostTime.wMilliseconds};
                    if (CheckWeekChanged(&localTime)) {
                        ResetRelaxTime(&localTime);
                    } else {
                        const int playTime = GetSocketPlayTime();
                        if (playTime > 0 && !m_bIsNew) {
                            User* const user = GetUser();
                            int worldId = g_nWorldId;
                            if (user != nullptr) {
                                worldId = user->m_data->m_nWorldId;
                            }
                            db.RequestSaveTimeLimitUsed(
                                u"", playTime, worldId, static_cast<int>(m_nAccountID), &localTime
                            );
                        }
                    }
                    m_nRelaxCheckInterval = 1;
                }

                const int remaining = runtime::SubtractInt32Wrapping(g_nPlayTimeLimit, GetSocketPlayTime());
                if (::g_nCountryCode == 4 || !g_bFreeOpenServer) {
                    if (remaining < 1) {
                        SendSystemMessage(SystemMessageId::WEEKLY_USAGE_TIME_EXPIRED);
                        if (remaining < -10) {
                            SetCloseTimer();
                        }
                    } else {
                        for (const int minutes : warningMinutes) {
                            const int upper = runtime::MultiplyInt32Wrapping(minutes, 60);
                            if (runtime::AddInt32Wrapping(upper, -2) < remaining && remaining < upper) {
                                SendSystemMessage_d(SystemMessageId::WEEKLY_USAGE_MINUTES_REMAINING, minutes);
                            }
                        }
                    }
                }
            }

            if (m_bAuthCharge && (::g_nCountryCode == 4 || !g_bFreeOpenServer) && g_b22to6DisableGame &&
                !m_b22to6DisableGameForceLogin) {
                const runtime::SystemTime localTime = runtime::GetLocalTimeNow();
                if (localTime.wHour == 21) {
                    if (localTime.wMinute > 29) {
                        constexpr std::array<int, 8> shutdownMinutes{30, 20, 10, 5, 4, 3, 2, 1};
                        for (const int minutes : shutdownMinutes) {
                            if (localTime.wMinute == 60 - minutes && (localTime.wSecond == 1 || localTime.wSecond == 2)) {
                                SendSystemMessage_d(SystemMessageId::LOGOUT_MINUTES_REMAINING, minutes);
                            }
                        }
                    }
                } else if (localTime.wHour > 21 || localTime.wHour < 6) {
                    SendSystemMessage(SystemMessageId::LOGGING_OUT_FOR_NIGHTTIME_AGE_VERIFICATION_RESTRICTION);
                    if (localTime.wMinute > 5) {
                        SetCloseTimer();
                    }
                }
            }

            if (m_bApplyChinaFatigueSystem && g_nChinaFatigueSystemType > 1) {
                const int playTime = GetPlayTime_china();
                const int minute = (playTime % 3600) / 60;
                const int second = playTime % 60;
                bool fullFatigue = false;
                if (m_bApplyChinaFatigueSystem) {
                    fullFatigue = runtime::MultiplyInt32Wrapping(g_ChinaFullFatigueTime, 3600) < GetPlayTime_china();
                }
                if (fullFatigue) {
                    if ((minute == 0 || minute == 15 || minute == 30 || minute == 45) && second < 2) {
                        ShowFatigueStatus_china();
                        SendSystemMessage(SystemMessageId::UNHEALTHY_PLAYTIME_REWARDS_DISABLED_UNTIL_RESTED);
                    }
                } else if (!m_bApplyChinaFatigueSystem ||
                    runtime::MultiplyInt32Wrapping(g_ChinaFullFatigueTime, 3600) < GetPlayTime_china() ||
                    GetPlayTime_china() <= runtime::MultiplyInt32Wrapping(g_ChinaHalfFatigueTime, 3600)) {
                    const int day = playTime / 3600;
                    if (day > 0 && minute == 0 && second < 2) {
                        ShowFatigueStatus_china();
                        SendSystemMessage_d(SystemMessageId::ACCUMULATED_PLAYTIME_HOURS, day);
                    }
                } else if ((minute == 0 || minute == 30) && second < 2) {
                    ShowFatigueStatus_china();
                    SendSystemMessage(SystemMessageId::FATIGUE_PLAYTIME_REWARDS_HALVED);
                }
            }
        } else {
            const std::int32_t closeCheckTime = runtime::crt::Time32Now();
            if (runtime::SubtractInt32Wrapping(closeCheckTime, m_nSocketCloseTimer) > 1) {
                Close();
                return;
            }
        }

        if (id == 4 && m_bActive) {
            OnLoginTimeout(this);
        }
    }

    if (m_Mode != SocketMode::SM_CLOSE) {
        AddTimer(1000, 0);
    }
}

void CUserSocket::OnClose() {
    L2_GUARD_FUNCTION();

    runtime::client_protocol::ForgetSocket(this);

    m_Mode = SocketMode::SM_CLOSE;
    const std::uint64_t socket = ExchangeSocketHandle(&m_Socket, 0);
    g_server.Remove(socket);
    g_SocketLimiter.DecUserSocketPerIP(GetIPAddressUINT());
    g_SocketLimiter.DecUserSocketPerMAC(m_nMacAddress);

    const wchar_t* caller = runtime::process::CurrentGuardCaller();
    if (!caller) {
        caller = L"callStackError";
    }
    new CUserSocketReleaseVerifier(this, caller);
}

void CUserSocket::ProcessQuit() {
    // Current L2Server 0x00c26474.
    L2_GUARD_FUNCTION();
    if (!m_bActive) {
        return;
    }

    User* const user = GetUser();
    int maximumCharacterId = 0;
    int maximumLevel = 0;
    if (bWriteConnLog) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"User [%s](%d) Disconnected[%d]",
        //     user ? user->GetName() : L"",
        //     user ? static_cast<int>(user->GetServerId()) : 0,
        //     RefCount())
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[socket] Disconnect cleanup started for socket %u, account %u, character %s (user object %u). Recorded close
        //     reason: %S (%d), references: %d.", m_nId, m_nAccountID, user ? Utf16ToWide(user->GetName()).c_str() : L"none",
        //     user ? user->GetServerId() : 0U,
        //     SocketCloseReasonForLog(m_nCloseReason),
        //     static_cast<int>(m_nCloseReason), RefCount())
        // L2_LOG_INFO(Log,
        //     L"[socket] Disconnect cleanup started for socket %u, account %u, character %s (user object %u). Recorded close
        //     reason: %S (%d), references: %d.", m_nId, m_nAccountID, user ? user->GetName() : L"none", user ?
        //     user->GetServerId() : 0U, SocketCloseReasonForLog(m_nCloseReason), static_cast<int>(m_nCloseReason),
        //     RefCount())
        L2_LOG_INFO(
            Log,
            u"[socket] Disconnect cleanup started for socket %u, account %u, character %s (user object %u); recorded close "
            u"reason: %S (%d), references: %d.",
            m_nId, m_nAccountID, user ? user->GetName() : u"none", user ? user->GetServerId() : 0U,
            SocketCloseReasonForLog(m_nCloseReason), static_cast<int>(m_nCloseReason), RefCount()
        );
    }
    if (user) {
        if (g_bDualBillingSystem) {
            maximumCharacterId = static_cast<int>(user->GetDBID());
            maximumLevel = user->m_data->m_nLevel;
        }
        user->SendLogout();
    }

    const std::uint32_t accountId = m_nAccountID;
    if (g_bQueueTicketEnabled && m_nQueueTicketAccountID != 0) {
        QueueTicket::Inst()->CancleWaiter(static_cast<int>(m_nQueueTicketAccountID));
        if (static_cast<std::int32_t>(accountId) < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][USERSOCKETRELEASEVERIFIER_] 3 insert "
            //     L"TimerExpired failed, m_pUserSocket(%x)",
            //     kUserSocketSource,
            //     0x3333,
            //     this)
            // Log.Add(
            //     LOG_NORMAL,
            //     "[socket] Queue-wait cancellation requested for ticket account %u on socket %u before a positive signed
            //     account ID was available (account %u). Disconnect cleanup continues.", m_nQueueTicketAccountID, m_nId,
            //     accountId)
            L2_LOG_INFO(
                Log,
                "[socket] Queue-wait cancellation requested for ticket account %u on socket %u before a positive signed "
                "account ID was available (account %u); disconnect cleanup continues.",
                m_nQueueTicketAccountID, m_nId, accountId
            );
        }
    }

    if (accountId == 0) {
        if (m_bLoggedIn) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d], socket disconnect but cannot find account_",
            //     kUserSocketSource,
            //     0x33c0)
            // Log.Add(
            //     LOG_ERROR,
            //     "[socket] Logged-in socket %u has account ID zero during disconnect. Account logout cannot be requested,
            //     remaining cleanup continues.", m_nId)
            L2_LOG_ERROR(
                Log,
                "[socket] Logged-in socket %u has account ID zero during disconnect; account logout cannot be requested, "
                "remaining cleanup continues.",
                m_nId
            );
            runtime::process::ReportCallStack(2, false);
        } else if (m_nSocketUseTimeStamp > 0) {
            const ::in_addr address = GetAddress();
            const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
            // Log.Add(
            //     LOG_ERROR,
            //     L"file[%s], line[%d], socket disconnect without login"
            //     L"(ip address %d.%d.%d.%d)",
            //     kUserSocketSource,
            //     0x33c7,
            //     static_cast<int>(bytes[0]),
            //     static_cast<int>(bytes[1]),
            //     static_cast<int>(bytes[2]),
            //     static_cast<int>(bytes[3]))
            // Log.Add(
            //     LOG_NORMAL,
            //     "[socket] Socket %u from %d.%d.%d.%d disconnected before login.",
            //     m_nId, static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
            //     static_cast<int>(bytes[2]), static_cast<int>(bytes[3]))
            L2_LOG_INFO(
                Log, "[socket] Socket %u from %d.%d.%d.%d disconnected before login.", m_nId, static_cast<int>(bytes[0]),
                static_cast<int>(bytes[1]), static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
            );
        }
    } else {
        bool removedFromAccountMap = false;
        const bool queueTicketBranch = [&] {
            L2_GUARD(accountMapSect);
            if (!g_bQueueTicketEnabled) {
                L2_RELEASE(this, ObjectRefType::AccountMap);
                accountMap.erase(accountId);
                NPSystem::CNPD::LoginStep_Logout(accountId);
                return false;
            }
            const auto account = accountMap.find(accountId);
            if (account != accountMap.end()) {
                L2_RELEASE(this, ObjectRefType::AccountMap);
                accountMap.erase(account);
                NPSystem::CNPD::LoginStep_Logout(accountId);
                removedFromAccountMap = true;
            }
            return true;
        }();
        if (!queueTicketBranch) {
            const std::uint32_t now = runtime::GetTickCount();
            if (m_wsidReceivedTick != 0 && now - m_wsidReceivedTick < 0x5265c01U) {
                authd.RequestCloseWebSessionID(static_cast<int>(accountId), m_WebSessionId.data());
            }
            g_UserSocketMapEnterChar.DeleteObject(static_cast<int>(accountId), const_cast<char*>("UserSocket.cpp"), 0x3369);
        } else {
            if (removedFromAccountMap) {
                g_UserSocketMapEnterChar.DeleteObject(
                    static_cast<int>(accountId), const_cast<char*>("UserSocket.cpp"), 0x3353
                );
            }
        }

        if ((g_bQueueTicketEnabled && !removedFromAccountMap) || static_cast<std::int32_t>(accountId) < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d], cannot remove socket from "
            //     L"g_UserSocketMapEnterChar because nAccountId is [%d]",
            //     kUserSocketSource,
            //     g_bQueueTicketEnabled ? 0x3358 : 0x336e,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[socket] Disconnect cleanup for socket %u, account %u: %s. Remaining cleanup continues.",
            //     m_nId, accountId,
            //     g_bQueueTicketEnabled && !removedFromAccountMap
            //         ? "account-map entry was absent. Enter-character map removal was skipped"
            //         : "account ID failed the signed positive-ID check after map cleanup was requested")
            L2_LOG_WARN(
                Log, "[socket] Disconnect cleanup for socket %u, account %u: %s; remaining cleanup continues.", m_nId,
                accountId,
                g_bQueueTicketEnabled && !removedFromAccountMap
                    ? "account-map entry was absent; enter-character map removal was skipped"
                    : "account ID failed the signed positive-ID check after map cleanup was requested"
            );
        }

        if (g_bDualBillingSystem) {
            if (m_nMaxCharId == maximumCharacterId && maximumLevel >= 1) {
                m_nMaxLevel = maximumLevel;
            } else if (m_nMaxLevel < maximumLevel) {
                m_nMaxCharId = maximumCharacterId;
                m_nMaxLevel = maximumLevel;
            }
        }

        if (m_nCloseReason != SCR_RETURN_TO_ORIGIN) {
            if (g_bDualBillingSystem && runtime::raw_atomic::ExchangeI32(&m_ChargeStart, 0) == 1) {
                authd.AuthChargeStop(static_cast<int>(m_nAccountID));
                if (user) {
                    CLogFormatter formatter;
                    formatter.WriteLog1(0x1fb1, user, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
                }
            }

            if (!g_bUseAuthUserData) {
                authd.RequestLogout(
                    static_cast<int>(accountId), GetSocketUseTime(), GetPlayTime_china(), m_nMaxCharId, m_nMaxLevel,
                    static_cast<int>(m_nCloseReason)
                );
            } else {
                authd.SaveUserDataAndLogout(
                    static_cast<int>(accountId), GetSocketUseTime(), GetPlayTime_china(), m_nMaxCharId, m_nMaxLevel,
                    static_cast<int>(m_nCloseReason)
                );
            }
        }

        if (g_bProcessClientINI) {
            SaveINIToDB();
        }
        if (!g_bQueueTicketEnabled || removedFromAccountMap) {
            DecUserCount(true);
        }
    }

    {
        L2_GUARD(mLock);
        UnBindUser();
        m_bActive = false;
    }
}

int GetProtocolVersion() {
    char revision[80];
    int version;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #162 $", "%s #%d", revision, static_cast<unsigned>(sizeof(revision)), &version);
#else
    std::sscanf("$Revision: #162 $", "%79s #%d", revision, &version);
#endif
    return version;
}

bool DummyUserPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    if (!baseSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DummyUserPacket called in UserSocket")
        // Log.Add(
        //     LOG_ERROR,
        //     "[client request] Default basic handler received a null socket, returning a close request.")
        L2_LOG_ERROR(Log, "[client request] Default basic handler received a null socket; returning a close request.");
        return true;
    }

    CUserSocket* const socket = baseSocket->CastUserSocket();
    const int packetId = socket ? socket->GetLastPacketNo() : 0;
    const int extendedPacketId = socket ? socket->GetLastEXPacketNo() : 0;
    User* const user = socket ? socket->GetUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "DummyUserPacket called in UserSocket C[%d] EX[%d]",
        //     packetId,
        //     extendedPacketId)
        // Log.Add(
        //     LOG_WARN,
        //     "[client request] Default basic handler rejected a request: %s (socket %u). Last recorded basic opcode %d,
        //     extended opcode %d. Requesting socket closure.", socket ? "no user is bound" : "socket is not a user socket",
        //     socket ? socket->m_nId : 0U, packetId, extendedPacketId)
        L2_LOG_WARN(
            Log,
            "[client request] Default basic handler rejected a request: %s (socket %u); last recorded basic opcode %d, "
            "extended opcode %d. Requesting socket closure.",
            socket ? "no user is bound" : "socket is not a user socket", socket ? socket->m_nId : 0U, packetId,
            extendedPacketId
        );
        return true;
    }

    const std::wstring accountName = Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    // Log.Add(
    //     LOG_ERROR,
    //     L"DummyUserPacket called in UserSocket C[%d] EX[%d] "
    //     L"Account[%ls:%d] User[%ls:%d]",
    //     packetId,
    //     extendedPacketId,
    //     accountName.c_str(),
    //     user->GetAccountID(),
    //     user->GetName(),
    //     user->GetDBID())
    // Log.Add(
    //     LOG_WARN,
    //     L"[client request] Default basic handler rejected a request on socket %u, account %s (%u), character %s (DB ID
    //     %u). Last recorded basic opcode %d, extended opcode %d. Requesting socket closure.", socket->m_nId,
    //     accountName.c_str(), user->GetAccountID(), user->GetName(), user->GetDBID(), packetId, extendedPacketId)
    // L2_LOG_WARN(Log,
    //     L"[client request] Default basic handler rejected a request on socket %u, account %s (%u), character %s (DB ID
    //     %u). Last recorded basic opcode %d, extended opcode %d. Requesting socket closure.", socket->m_nId,
    //     accountName.c_str(), user->GetAccountID(), user->GetName(), user->GetDBID(), packetId, extendedPacketId)
    L2_LOG_WARN(
        Log,
        u"[client request] Default basic handler rejected a request on socket %u, account %s (%u), character %s (DB ID %u); "
        u"last recorded basic opcode %d, extended opcode %d. Requesting socket closure.",
        socket->m_nId, accountName.c_str(), user->GetAccountID(), user->GetName(), user->GetDBID(), packetId,
        extendedPacketId
    );
    return true;
}

bool UserExPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c19fd4.
    L2_GUARD_FUNCTION();

    std::int16_t extendedPacketNo = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "h", &extendedPacketNo);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    int packetNo = static_cast<int>(extendedPacketNo);
    if (g_bEnablePacketShuffle) {
        packetNo = socket->GetOriginalExPacket(packetNo);
    }

    socket->SetLastEXPacketNo(static_cast<std::int16_t>(packetNo));

    UserPacketExCallback callback = &DummyUserPacketEx;
    if (static_cast<unsigned int>(packetNo) < 352U) {
        callback = g_UserPacketEx.GetCallback(packetNo);
    }
    // The exact NA P152 client assigns both RequestShowResetShopList (empty)
    // and RequestResetBeauty (three DWORDs) to C_EX 0x0cd. The two-byte frame
    // length immediately precedes the D0/opcode body passed to this callback,
    // so keep the actual wire boundary instead of reading receive-buffer
    // padding to guess which request was sent.
    const int frameSize = static_cast<int>(packet[-3]) | (static_cast<int>(packet[-2]) << 8);
    const int payloadSize = frameSize >= 5 ? frameSize - 5 : 0;
    // Portable P152 compatibility extension: resolve the shifted
    // 0x15e..0x162 tail and the audited dummy-vs-functional slots without
    // mutating the exact current 352-entry C_EX table above.
    callback = runtime::client_protocol::ResolveExtendedIncomingCallback(socket, packetNo, payloadSize, callback);
    const int policyPacketNo = runtime::client_protocol::ExtendedPolicyOpcode(socket, packetNo);

    User* const user = socket->GetUser();
    if (user == nullptr || user->PacketExAcceptable(policyPacketNo)) {
        const bool closeRequested = callback(baseSocket, payload);
        return closeRequested;
    }
    return false;
}

void CUserSocket::ItemDebug(User* user, int itemServerId) {
    // Current L2Server 0x00c09994, original owner UserSocket.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CItem* const item = user->m_inv.GetByServerID(itemServerId);
    CUserSocket* const socket = user->GetSocket();
    if (item == nullptr || socket == nullptr) {
        return;
    }

    char16_t message[512]{};
    FormatUtf16To(
        message, std::size(message),
        u"Item debug ServerID = %d, Class [%ls] (%d), DB_ID = %d, is_variation = %ls, Option[%d, %d], is_drop = %ls",
        static_cast<int>(item->m_nId), g_ObjectDB.GetName(item->m_nClassId), item->m_nClassId, item->m_data->m_nDBID,
        item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0
            ? L"true"
            : L"false",
        item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2,
        item->IsDrop() ? L"true" : L"false"
    );
    socket->SendSystemMessage(u"SYS", message);
}

bool ItemListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c097f8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendItemList(false, true, nullptr);
    }
    return user == nullptr;
}

bool SkillListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b058.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendSkillList(nullptr, false, 0);
    }
    return user == nullptr;
}

bool CUserSocket::RequestEnchantItemPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c13c.
    L2_GUARD_FUNCTION();

    int targetItemId = 0;
    ::Disassemble(packet, "d", &targetItemId);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnRequestEnchantItemPacket(targetItemId);
}

bool CUserSocket::DestroyItem(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c21c.
    L2_GUARD_FUNCTION();

    int itemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "dQ", &itemServerId, &amount);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnDestroyItemPacket(itemServerId, amount);
}

bool RequestAcquireSkill(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0cbc4.
    L2_GUARD_FUNCTION();

    int skillId = 0;
    int packedSkillLevel = 0;
    std::uint32_t acquireTypeBits = 0;
    const std::uint8_t* const payload = ::Disassemble(packet, "ddd", &skillId, &packedSkillLevel, &acquireTypeBits);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    // Portable opt-in diagnostic. Absent from the shipping handler and
    // inactive unless L2_MAGIC_TRACE is present in the environment.
    if (user == nullptr) {
        return true;
    }
    if (acquireTypeBits > 0x8dU) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] TW Skill Acquire Error!! "
        //     L"(User : %s, type : %d)",
        //     kUserSocketSource,
        //     0x16d3,
        //     user->GetName(),
        //     runtime::Int32FromBits(acquireTypeBits))
        // Log.Add(
        //     LOG_WARN,
        //     L"[skills] Acquisition request ignored for character %s on socket %u: type %u is outside 0..141 (skill %d,
        //     packed level 0x%08x).", user->GetName(), socket->m_nId, acquireTypeBits, skillId,
        //     static_cast<unsigned>(packedSkillLevel))
        // L2_LOG_WARN(Log,
        //     L"[skills] Acquisition request ignored for character %s on socket %u: type %u is outside 0..141 (skill %d,
        //     packed level 0x%08x).", user->GetName(), socket->m_nId, acquireTypeBits, skillId,
        //     static_cast<unsigned>(packedSkillLevel))
        L2_LOG_WARN(
            Log,
            u"[skills] Acquisition request ignored for character %s on socket %u: type %u is outside 0..141 (skill %d, "
            u"packed level 0x%08x).",
            user->GetName(), socket->m_nId, acquireTypeBits, skillId, static_cast<unsigned>(packedSkillLevel)
        );
        return false;
    }

    if (runtime::raw_atomic::CompareExchangeI32(&user->m_nChangeSubjobFlag, 1, 0) != 0) {
        return false;
    }

    bool result = false;
    if (acquireTypeBits == 2U || acquireTypeBits == 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
        //     kUserSocketSource,
        //     0x16e3)
        // Log.Add(
        //     LOG_WARN,
        //     "[skills] Acquisition request ignored on socket %u, account %u: legacy clan acquisition type %u is no longer
        //     handled here (skill %d, packed level 0x%08x).", socket->m_nId, socket->GetAccountID(), acquireTypeBits,
        //     skillId, static_cast<unsigned>(packedSkillLevel))
        L2_LOG_WARN(
            Log,
            "[skills] Acquisition request ignored on socket %u, account %u: legacy clan acquisition type %u is no longer "
            "handled here (skill %d, packed level 0x%08x).",
            socket->m_nId, socket->GetAccountID(), acquireTypeBits, skillId, static_cast<unsigned>(packedSkillLevel)
        );
    } else {
        result = user->OnRequestAcquireSkillPacket(
            skillId, packedSkillLevel, static_cast<EtcSkillAcquireType>(acquireTypeBits), payload
        );
    }

    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    return result;
}

bool RequestHennaItemList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e6fc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendInvenHennaList();
    }
    return user == nullptr;
}

bool RequestHennaUnequipList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e7b8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendEquipedHennaListForUnequip();
    }
    return user == nullptr;
}

bool AcquireSkillInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2aed8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int skillId = 0;
    std::uint32_t packedSkillLevel = 0;
    EtcSkillAcquireType acquireType = ESAT_NORMAL;
    ::Disassemble(packet, "ddd", &skillId, &packedSkillLevel, &acquireType);

    const int skillLevel = static_cast<int>(packedSkillLevel & 0xffffU);
    const int skillSubLevel = runtime::Int16FromBits(packedSkillLevel >> 16U);

    if (acquireType == ESAT_NORMAL) {
        SkillAcquireInfo* const acquireInfo =
            g_SkillAcquireDB.GetSkillAcquireInfo(user->m_data->m_nClass, skillId, skillLevel);
        if (acquireInfo == nullptr || user->GetAcquiredSkillSubLevel(skillId) != skillSubLevel) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] hacking? user[%s] skill[%d:%d:%d] "
            //     L"type[%d] skill_name_id is invalid",
            //     kUserSocketSource,
            //     0x166d,
            //     user->GetName(),
            //     skillId,
            //     skillLevel,
            //     skillSubLevel,
            //     static_cast<int>(acquireType))
            // Log.Add(
            //     LOG_WARN,
            //     L"[skills] Acquisition information not sent for character %s on socket %u, skill %d level %d sublevel %d:
            //     %S.", user->GetName(), socket->m_nId, skillId, skillLevel, skillSubLevel, acquireInfo == nullptr
            //         ? "no acquisition entry exists for the character class"
            //         : "requested sublevel differs from the acquired sublevel")
            // L2_LOG_WARN(Log,
            //     L"[skills] Acquisition information not sent for character %s on socket %u, skill %d level %d sublevel %d:
            //     %S.", user->GetName(), socket->m_nId, skillId, skillLevel, skillSubLevel, acquireInfo == nullptr
            //         ? "no acquisition entry exists for the character class"
            //         : "requested sublevel differs from the acquired sublevel")
            L2_LOG_WARN(
                Log,
                u"[skills] Acquisition information not sent for character %s on socket %u, skill %d level %d sublevel %d: "
                u"%S.",
                user->GetName(), socket->m_nId, skillId, skillLevel, skillSubLevel,
                acquireInfo == nullptr ? "no acquisition entry exists for the character class"
                                       : "requested sublevel differs from the acquired sublevel"
            );
            return false;
        }

        std::array<char, 0x4f9c> payload;
        int payloadLength = ::Assemble(
            payload.data(), 0x4f9c, "ddQhhd", skillId, runtime::Int32FromBits(packedSkillLevel), acquireInfo->nLevelUP_SP,
            runtime::Int16FromBits(static_cast<std::uint32_t>(acquireInfo->nGetLevel)),
            runtime::Int16FromBits(static_cast<std::uint32_t>(acquireInfo->m_GetMainDualClassLevel)),
            static_cast<int>(acquireInfo->vNeededItem.size())
        );

        for (std::size_t index = 0; index < acquireInfo->vNeededItem.size(); ++index) {
            payloadLength += ::Assemble(
                payload.data() + payloadLength, 0x4f9c - payloadLength, "dQ",
                runtime::CheckedVectorAt(acquireInfo->vNeededItem, index),
                runtime::CheckedVectorAt(acquireInfo->vNeededItemCount, index)
            );
        }

        int replaceableSkillCount = 0;
        for (const CSkillInfo* const replaceableSkill : acquireInfo->m_ReplaceableSkills) {
            if (user->GetSkillLevel(replaceableSkill->m_nSkillId) != -1) {
                ++replaceableSkillCount;
            }
        }
        payloadLength += ::Assemble(payload.data() + payloadLength, 0x4f9c - payloadLength, "d", replaceableSkillCount);

        for (const CSkillInfo* const replaceableSkill : acquireInfo->m_ReplaceableSkills) {
            const int acquiredSubLevel = user->GetAcquiredSkillSubLevel(replaceableSkill->m_nSkillId);
            if (user->GetSkillLevel(replaceableSkill->m_nSkillId) != -1) {
                const std::uint32_t packedReplaceableLevel = (static_cast<std::uint32_t>(acquiredSubLevel) << 16U) |
                    static_cast<std::uint16_t>(replaceableSkill->m_nLevel);
                payloadLength += ::Assemble(
                    payload.data() + payloadLength, 0x4f9c - payloadLength, "dd", replaceableSkill->m_nSkillId,
                    runtime::Int32FromBits(packedReplaceableLevel)
                );
            }
        }

        user->GetSocket()->Send("chb", 0xfe, 0xfc, payloadLength, payload.data());
        return false;
    }

    if (acquireType == ESAT_PLEDGE || acquireType == ESAT_SUB_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]DEPRECATED!!!!!",
        //     kUserSocketSource,
        //     0x1675)
        // Log.Add(
        //     LOG_WARN,
        //     "[skills] Acquisition information request ignored on socket %u, account %u: legacy clan type %d is no longer
        //     handled here (skill %d, level %d).", socket->m_nId, socket->GetAccountID(), static_cast<int>(acquireType),
        //     skillId, skillLevel)
        L2_LOG_WARN(
            Log,
            "[skills] Acquisition information request ignored on socket %u, account %u: legacy clan type %d is no longer "
            "handled here (skill %d, level %d).",
            socket->m_nId, socket->GetAccountID(), static_cast<int>(acquireType), skillId, skillLevel
        );
        return false;
    }

    if (!TalkChannelCache::inst().mChannels[0].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        return false;
    }

    CNPC* const npc = [&] {
        L2_GUARD(*user->m_lock);
        return user->m_spTalkTarget.GetNPC();
    }();
    if (npc == nullptr || skillId < 1 || npc->m_data == nullptr) {
        return false;
    }

    SkillAcquireInfo* acquireInfo = nullptr;
    if (acquireType == ESAT_NORMAL) {
        acquireInfo =
            g_SkillAcquireDB.GetSkillAcquireInfo(user->m_data->m_nClass, skillId, skillLevel);
    } else {
        acquireInfo = g_SkillAcquireDB.GetEtcSkillAcquireInfo(acquireType, skillId, skillLevel);
    }
    if (acquireInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hacking? user[%s] skill[%d:%d] "
        //     L"skill_name_id is invalid",
        //     kUserSocketSource,
        //     0x16b2,
        //     user->GetName(),
        //     skillId,
        //     skillLevel)
        // Log.Add(
        //     LOG_WARN,
        //     L"[skills] Acquisition information not sent for character %s on socket %u: no acquisition entry exists for
        //     skill %d, level %d, type %d.", user->GetName(), socket->m_nId, skillId, skillLevel,
        //     static_cast<int>(acquireType))
        // L2_LOG_WARN(Log,
        //     L"[skills] Acquisition information not sent for character %s on socket %u: no acquisition entry exists for
        //     skill %d, level %d, type %d.", user->GetName(), socket->m_nId, skillId, skillLevel,
        //     static_cast<int>(acquireType))
        L2_LOG_WARN(
            Log,
            u"[skills] Acquisition information not sent for character %s on socket %u: no acquisition entry exists for "
            u"skill %d, level %d, type %d.",
            user->GetName(), socket->m_nId, skillId, skillLevel, static_cast<int>(acquireType)
        );
        return false;
    }

    const std::uint64_t skillNameId = (static_cast<std::uint64_t>(static_cast<std::int64_t>(skillId)) << 32U) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(skillLevel)) << 16U);
    g_npcserver.Send(
        "cdddQdd", 0x12, static_cast<int>(user->Id()), user->m_data->index, npc->m_data->index, skillNameId,
        acquireInfo->bNeedQuest ? 1 : 0, static_cast<int>(acquireType)
    );
    return false;
}

bool RequestHennaItemInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2b6c0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!TalkChannelCache::inst().mChannels[2].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request henna item info "
        //     L"with invalid talk channel.",
        //     kUserSocketSource,
        //     0x24cc,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[henna] Item-information request ignored for character %s on socket %u: the NPC talk channel is invalid. No
        //     information reply was requested.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[henna] Item-information request ignored for character %s on socket %u: the NPC talk channel is invalid. No
        //     information reply was requested.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[henna] Item-information request ignored for character %s on socket %u: the NPC talk channel is invalid; no "
            u"information reply was requested.",
            user->GetName(), socket->m_nId
        );
        return false;
    }

    int hennaId = 0;
    ::Disassemble(packet, "d", &hennaId);
    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] henna id is invalid [%d][%d]",
        //     "UserSocket.cpp",
        //     0x24d9,
        //     static_cast<int>(user->GetDBID()),
        //     hennaId)
        // Log.Add(
        //     LOG_WARN,
        //     "[henna] Item-information request ignored on socket %u for character DB ID %u: henna ID %d must be positive.
        //     no information reply was requested.", socket->m_nId, user->GetDBID(), hennaId)
        L2_LOG_WARN(
            Log,
            "[henna] Item-information request ignored on socket %u for character DB ID %u: henna ID %d must be positive; no "
            "information reply was requested.",
            socket->m_nId, user->GetDBID(), hennaId
        );
    } else {
        user->SendHennaItemInfo(hennaId);
    }
    return false;
}

bool RequestHennaEquip(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2b85c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!TalkChannelCache::inst().mChannels[2].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request henna equip "
        //     L"with invalid talk channel.",
        //     kUserSocketSource,
        //     0x24eb,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[henna] Equip request ignored for character %s on socket %u: the NPC talk channel is invalid. No equip
        //     operation was requested.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[henna] Equip request ignored for character %s on socket %u: the NPC talk channel is invalid. No equip
        //     operation was requested.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[henna] Equip request ignored for character %s on socket %u: the NPC talk channel is invalid; no equip "
            u"operation was requested.",
            user->GetName(), socket->m_nId
        );
        return false;
    }

    int hennaId = 0;
    ::Disassemble(packet, "d", &hennaId);
    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] henna id is invalid "
        //     L"[dbid:%d][hennaId:%d]",
        //     "UserSocket.cpp",
        //     0x24f8,
        //     static_cast<int>(user->GetDBID()),
        //     hennaId)
        // Log.Add(
        //     LOG_WARN,
        //     "[henna] Equip request ignored on socket %u for character DB ID %u: henna ID %d must be positive. No equip
        //     operation was requested.", socket->m_nId, user->GetDBID(), hennaId)
        L2_LOG_WARN(
            Log,
            "[henna] Equip request ignored on socket %u for character DB ID %u: henna ID %d must be positive; no equip "
            "operation was requested.",
            socket->m_nId, user->GetDBID(), hennaId
        );
    } else {
        user->EquipHenna(hennaId);
    }
    return false;
}

bool RequestHennaUnequipInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2b9f8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!TalkChannelCache::inst().mChannels[2].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        user->GetSocket()->Send(
            "cddQQdQdhdhdhdhdhdhdhdhd", 0xe7, -1, 0, std::int64_t{0}, std::int64_t{0}, 0, std::int64_t{0}, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request henna unequip info "
        //     L"with invalid talk channel.",
        //     kUserSocketSource,
        //     0x2511,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[henna] Removal-information request ignored for character %s on socket %u: the NPC talk channel is invalid.
        //     a fallback reply was requested.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[henna] Removal-information request ignored for character %s on socket %u: the NPC talk channel is invalid.
        //     a fallback reply was requested.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[henna] Removal-information request ignored for character %s on socket %u: the NPC talk channel is invalid; a "
            u"fallback reply was requested.",
            user->GetName(), socket->m_nId
        );
        return false;
    }

    int hennaId = 0;
    ::Disassemble(packet, "d", &hennaId);
    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] henna id is invalid [%d][%d]",
        //     "UserSocket.cpp",
        //     0x251e,
        //     static_cast<int>(user->GetDBID()),
        //     hennaId)
        // Log.Add(
        //     LOG_WARN,
        //     "[henna] Removal-information request ignored on socket %u for character DB ID %u: henna ID %d must be
        //     positive, no information reply was requested.", socket->m_nId, user->GetDBID(), hennaId)
        L2_LOG_WARN(
            Log,
            "[henna] Removal-information request ignored on socket %u for character DB ID %u: henna ID %d must be positive; "
            "no information reply was requested.",
            socket->m_nId, user->GetDBID(), hennaId
        );
    } else {
        user->SendHennaUnequipInfo(hennaId);
    }
    return false;
}

bool RequestHennaUnequip(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2bc58.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!TalkChannelCache::inst().mChannels[2].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request henna unequip "
        //     L"with invalid talk channel.",
        //     kUserSocketSource,
        //     0x2530,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[henna] Removal request ignored for character %s on socket %u: the NPC talk channel is invalid. No removal
        //     operation was requested.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[henna] Removal request ignored for character %s on socket %u: the NPC talk channel is invalid. No removal
        //     operation was requested.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[henna] Removal request ignored for character %s on socket %u: the NPC talk channel is invalid; no removal "
            u"operation was requested.",
            user->GetName(), socket->m_nId
        );
        return false;
    }

    int hennaId = 0;
    ::Disassemble(packet, "d", &hennaId);
    if (hennaId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] henna id is invalid [%d][%d]",
        //     "UserSocket.cpp",
        //     0x253d,
        //     static_cast<int>(user->GetDBID()),
        //     hennaId)
        // Log.Add(
        //     LOG_WARN,
        //     "[henna] Removal request ignored on socket %u for character DB ID %u: henna ID %d must be positive. No removal
        //     operation was requested.", socket->m_nId, user->GetDBID(), hennaId)
        L2_LOG_WARN(
            Log,
            "[henna] Removal request ignored on socket %u for character DB ID %u: henna ID %d must be positive; no removal "
            "operation was requested.",
            socket->m_nId, user->GetDBID(), hennaId
        );
    } else {
        user->UnequipHenna(hennaId);
    }
    return false;
}

bool QuestList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c830 always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendQuestList(nullptr);
    }
    return false;
}

bool FriendList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c774.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->OnRequestL2FriendList();
    }
    return user == nullptr;
}

bool RequestBlockPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0ddb4, original owner UserSocket.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        int requestType = -1;
        const unsigned char* const payload = ::Disassemble(packet, "d", &requestType);
        user->OnRequestBlock(requestType, payload);
    }
    return false;
}

bool CUserSocket::CharacterCreatePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c11edc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const std::int32_t previousPacket = socket->SetLastUsePacket(0x0c, false);
    if (previousPacket != 0x13 && previousPacket != 0x0c && previousPacket != 0x2b) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? last use packet [%d]",
        //     "UserSocket.cpp",
        //     0x3bb,
        //     previousPacket)
        // Log.Add(
        //     LOG_WARN,
        //     "[characters] Creation request rejected on socket %u, account %u: previous selection-flow opcode %d is not
        //     0x13, 0x0c or 0x2b, requesting socket closure.", socket->m_nId, socket->m_nAccountID, previousPacket)
        L2_LOG_WARN(
            Log,
            "[characters] Creation request rejected on socket %u, account %u: previous selection-flow opcode %d is not "
            "0x13, 0x0c or 0x2b; requesting socket closure.",
            socket->m_nId, socket->m_nAccountID, previousPacket
        );
        return true;
    }

    bool punish = false;
    if (!CanCreateCharacter(*socket, S_CHARACTER_CREATE_FAIL, punish)) {
        return punish;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    std::memset(targetName.data(), 0, 0x30);
    // Current 0x00c11ff8 passes every numeric lane uninitialized. A short
    // decode therefore keeps the stack residue. Only the first 0x30 bytes
    // of the 56-byte name buffer above are cleared.
    std::uint32_t race;
    std::int32_t sex;
    ClassType classType;
    std::int32_t intelligence;
    std::int32_t strength;
    std::int32_t constitution;
    std::int32_t mentality;
    std::int32_t dexterity;
    std::int32_t wit;
    std::int32_t hairStyle;
    std::int32_t hairColor;
    std::int32_t face;
    ::Disassemble(
        packet, "Sdddddddddddd", 0x30, targetName.data(), &race, &sex, &classType, &intelligence, &strength, &constitution,
        &mentality, &dexterity, &wit, &hairStyle, &hairColor, &face
    );

    const auto rejectMalformed = [&](int line) {
        const std::int32_t accountId = runtime::Int32FromBits(socket->m_nAccountID);
        const std::wstring accountName =
            Utf16ToWide(socket->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Non-Client BOT? account[%d, %s]",
        //     kUserSocketSource,
        //     line,
        //     accountId,
        //     accountName.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     L"[character create] Request rejected on socket %u, account %s (%u): %S. A failure reply and socket closure
        //     will be requested.", socket->m_nId, accountName.c_str(), static_cast<std::uint32_t>(accountId), line == 0x3da
        //     ? "sex value is neither 0 nor 1"
        //         : line == 0x3e1 ? "class and sex combination is not allowed"
        //         : line == 0x3e8 ? "race value exceeds 29"
        //         : "creation field validation failed")
        L2_LOG_WARN(
            Log,
            u"[character create] Request rejected on socket %u, account %s (%u): %S; a failure reply and socket closure "
            u"will be requested.",
            socket->m_nId, accountName.c_str(), static_cast<std::uint32_t>(accountId),
            line == 0x3da       ? "sex value is neither 0 nor 1"
                : line == 0x3e1 ? "class and sex combination is not allowed"
                : line == 0x3e8 ? "race value exceeds 29"
                                : "creation field validation failed"
        );
        socket->Send("cd", S_CHARACTER_CREATE_FAIL, 0);
    };

    if (sex != 0 && sex != 1) {
        rejectMalformed(0x3da);
        return true;
    }
    const int classValue = static_cast<int>(classType);
    if ((sex == 0 && classValue == 124) || (sex == 1 && classValue == 123)) {
        rejectMalformed(0x3e1);
        return true;
    }
    if (race > 29U) {
        rejectMalformed(0x3e8);
        return true;
    }

    if (!User::CheckCharacterName(targetName.data(), g_nCountryCode, false)) {
        socket->Send("cd", S_CHARACTER_CREATE_FAIL, 4);
        return false;
    }
    if (g_nCountryCode == 8 && !User::CheckCharacterNameDual(targetName.data(), 8, false)) {
        socket->Send("cd", S_CHARACTER_CREATE_FAIL, 7);
        return false;
    }

    const bool newClass = (classValue >= 139 && classValue <= 146) || classValue == 190 || classValue == 191;
    const bool useStandardCreation = !g_bNewCharacterCreate ||
        (socket->m_iPremiumLevel < 1 && std::strncmp(socket->GetPCCafeCode(), "0000000", 8) == 0) || !newClass;

    int statIndex = -1;
    int createLevel = 1;
    if (useStandardCreation) {
        bool initialClass = false;
        for (const ClassType candidate : __InitClassType) {
            if (classType == candidate) {
                initialClass = true;
                break;
            }
        }
        if (!initialClass) {
            socket->Send("cd", S_CHARACTER_CREATE_FAIL, 0);
            return false;
        }
        statIndex = classValue;
    } else {
        switch (race) {
            case 0:
                if (classValue >= 139 && classValue <= 142) {
                    statIndex = 0;
                } else if (classValue >= 143 && classValue <= 146) {
                    statIndex = 10;
                }
                break;
            case 1:
                if (classValue == 139 || classValue == 141 || classValue == 142) {
                    statIndex = 18;
                } else if (classValue >= 143 && classValue <= 146) {
                    statIndex = 25;
                }
                break;
            case 2:
                if (classValue == 139 || classValue == 141 || classValue == 142) {
                    statIndex = 31;
                } else if (classValue >= 143 && classValue <= 146) {
                    statIndex = 38;
                }
                break;
            case 3:
                if (classValue == 140) {
                    statIndex = 44;
                } else if (classValue == 144) {
                    statIndex = 49;
                }
                break;
            case 4:
                if (classValue == 140 || classValue == 141) {
                    statIndex = 53;
                }
                break;
            case 5:
                if (classValue == 140 && sex == 0) {
                    statIndex = 123;
                } else if (classValue == 142 && sex == 1) {
                    statIndex = 124;
                } else if (classValue == 143) {
                    statIndex = sex == 0 ? 124 : 123;
                }
                break;
            case 6:
                if (classValue == 190 && sex == 1) {
                    statIndex = 182;
                } else if (classValue == 191 && sex == 1) {
                    statIndex = 183;
                }
                break;
            default:
                break;
        }
        createLevel = g_nCharacterCreateLevel;
    }

    // The current new-creation branch indexes -1 when its
    // race/class matrix has no row. Do not normalize that target oddity.
    const CSetting::StatValueType maximum = Singleton<CSetting>::inst().m_maximum_stat[statIndex];
    const CSetting::StatValueType minimum = Singleton<CSetting>::inst().m_minimum_stat[statIndex];
    const CSetting::StatValueType recommended = Singleton<CSetting>::inst().m_recommended_stat[statIndex];

    intelligence = recommended.Int;
    strength = recommended.Str;
    constitution = recommended.Con;
    mentality = recommended.Men;
    dexterity = recommended.Dex;
    wit = recommended.Wit;

    const auto statOutOfRange = [](int value, int upper, int lower) { return value > upper || value < lower; };
    if (statOutOfRange(intelligence, maximum.Int, minimum.Int) || statOutOfRange(strength, maximum.Str, minimum.Str) ||
        statOutOfRange(constitution, maximum.Con, minimum.Con) || statOutOfRange(mentality, maximum.Men, minimum.Men) ||
        statOutOfRange(dexterity, maximum.Dex, minimum.Dex) || statOutOfRange(wit, maximum.Wit, minimum.Wit)) {
        socket->Send("cd", S_CHARACTER_CREATE_FAIL, 0);
        return false;
    }

    std::int32_t firstSum = constitution;
    firstSum = runtime::AddInt32Wrapping(firstSum, dexterity);
    firstSum = runtime::AddInt32Wrapping(firstSum, mentality);
    firstSum = runtime::AddInt32Wrapping(firstSum, intelligence);
    firstSum = runtime::AddInt32Wrapping(firstSum, wit);
    firstSum = runtime::AddInt32Wrapping(firstSum, strength);
    std::int32_t secondSum = constitution;
    secondSum = runtime::AddInt32Wrapping(secondSum, dexterity);
    secondSum = runtime::AddInt32Wrapping(secondSum, intelligence);
    secondSum = runtime::AddInt32Wrapping(secondSum, strength);
    secondSum = runtime::AddInt32Wrapping(secondSum, mentality);
    secondSum = runtime::AddInt32Wrapping(secondSum, wit);
    if (firstSum != secondSum) {
        socket->Send("cd", S_CHARACTER_CREATE_FAIL, 0);
        return false;
    }

    if (hairStyle < 0 || hairStyle > 100) {
        hairStyle = 0;
    }
    if (hairColor < 0 || hairColor > 100) {
        hairColor = 0;
    }
    if (face < 0 || face > 2) {
        face = 0;
    }

    db.RequestCreateChar(
        targetName.data(), static_cast<int>(race), sex, classValue, strength, dexterity, constitution, intelligence, wit,
        mentality, hairStyle, hairColor, face, socket, createLevel
    );
    return false;
}

bool CUserSocket::CharacterDeletePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c12990.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (!socket->m_bForceLogin && g_bProhibitAllCharDeletion) {
        socket->Send("cd", S_CHARACTER_DELETE_FAIL, 4);
        return false;
    }

    const std::int32_t previousPacket = socket->SetLastUsePacket(0x0d, false);
    if (previousPacket != 0x13 && previousPacket != 0x0c && previousPacket != 0x2b) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? last use packet [%d]",
        //     "UserSocket.cpp",
        //     0x546,
        //     previousPacket)
        // Log.Add(
        //     LOG_WARN,
        //     "[characters] Deletion request rejected on socket %u, account %u: previous selection-flow opcode %d is not
        //     0x13, 0x0c or 0x2b, requesting socket closure.", socket->m_nId, socket->m_nAccountID, previousPacket)
        L2_LOG_WARN(
            Log,
            "[characters] Deletion request rejected on socket %u, account %u: previous selection-flow opcode %d is not "
            "0x13, 0x0c or 0x2b; requesting socket closure.",
            socket->m_nId, socket->m_nAccountID, previousPacket
        );
        return true;
    }

    std::uint32_t index = 0;
    ::Disassemble(packet, "d", &index);
    if (index >= 7U) {
        return false;
    }

    const std::int32_t characterId = socket->m_nCharacterDBIDs[index];
    if (characterId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] CharacterDeletePacket char_index[%d] account[%S]",
        //     "UserSocket.cpp",
        //     0x554,
        //     index,
        //     socket->m_sAccountName.data())
        // Log.Add(
        //     LOG_WARN,
        //     "[characters] Deletion request ignored on socket %u, account %u: slot %u has character DB ID zero. No database
        //     deletion was requested.", socket->m_nId, socket->m_nAccountID, index)
        L2_LOG_WARN(
            Log,
            "[characters] Deletion request ignored on socket %u, account %u: slot %u has character DB ID zero; no database "
            "deletion was requested.",
            socket->m_nId, socket->m_nAccountID, index
        );
    } else {
        db.RequestDeleteChar(socket->m_nAccountID, static_cast<unsigned int>(characterId), socket);
    }
    return false;
}

bool CUserSocket::NewCharacterPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c12b34.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    const std::int32_t previousPacket = socket->SetLastUsePacket(0x13, false);
    if (previousPacket != 0x13 && previousPacket != 0x0c && previousPacket != 0x2b) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? last use packet [%d]",
        //     "UserSocket.cpp",
        //     0x56a,
        //     previousPacket)
        // Log.Add(
        //     LOG_WARN,
        //     "[characters] Creation-options request rejected on socket %u, account %u: previous selection-flow opcode %d is
        //     not 0x13, 0x0c or 0x2b, requesting socket closure.", socket->m_nId, socket->m_nAccountID, previousPacket)
        L2_LOG_WARN(
            Log,
            "[characters] Creation-options request rejected on socket %u, account %u: previous selection-flow opcode %d is "
            "not 0x13, 0x0c or 0x2b; requesting socket closure.",
            socket->m_nId, socket->m_nAccountID, previousPacket
        );
        return true;
    }

    bool punish = false;
    if (!CanCreateCharacter(*socket, S_NEW_CHARACTER_FAIL, punish)) {
        return punish;
    }

    CSetting& maximumSetting = Singleton<CSetting>::inst();
    CSetting& minimumSetting = Singleton<CSetting>::inst();
    CSetting& recommendedSetting = Singleton<CSetting>::inst();
    std::array<char, 0x5000> payload;
    std::int32_t payloadLength = 0;
    for (std::size_t index = 0; index < __InitClassType.size(); ++index) {
        const std::size_t statIndex = static_cast<std::size_t>(static_cast<int>(__InitClassType[index]));
        const CSetting::StatValueType& maximum = maximumSetting.m_maximum_stat[statIndex];
        const CSetting::StatValueType& minimum = minimumSetting.m_minimum_stat[statIndex];
        const CSetting::StatValueType& recommended = recommendedSetting.m_recommended_stat[statIndex];
        const int assembled = ::Assemble(
            payload.data() + payloadLength, runtime::SubtractInt32Wrapping(0x5000, payloadLength), "dddddddddddddddddddd",
            static_cast<int>(__InitRaceType[index]), static_cast<int>(__InitClassType[index]), maximum.Str, recommended.Str,
            minimum.Str, maximum.Dex, recommended.Dex, minimum.Dex, maximum.Con, recommended.Con, minimum.Con, maximum.Int,
            recommended.Int, minimum.Int, maximum.Wit, recommended.Wit, minimum.Wit, maximum.Men, recommended.Men,
            minimum.Men
        );
        if (assembled > 0) {
            payloadLength = runtime::AddInt32Wrapping(payloadLength, assembled);
        }
    }

    socket->Send("cdb", S_NEW_CHARACTER_SUCCESS, static_cast<int>(__InitClassType.size()), payloadLength, payload.data());
    return false;
}

bool RestoreCharacter(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c13d60.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    std::uint32_t index = 0;
    ::Disassemble(packet, "d", &index);

    User* const user = socket->GetUser();
    if (user != nullptr) {
        const char16_t* const name = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BOT User Try Restore Character In The World!!! (CharacterName = %s)",
        //     kUserSocketSource,
        //     0x15b6,
        //     name)
        // Log.Add(
        //     LOG_WARN,
        //     L"[characters] Restoration request rejected on socket %u, account %u: character %s is already bound to this
        //     socket, requesting forced closure.", socket->m_nId, socket->GetAccountID(), name)
        // L2_LOG_WARN(Log,
        //     L"[characters] Restoration request rejected on socket %u, account %u: character %s is already bound to this
        //     socket, requesting forced closure.", socket->m_nId, socket->GetAccountID(), name)
        L2_LOG_WARN(
            Log,
            u"[characters] Restoration request rejected on socket %u, account %u: character %s is already bound to this "
            u"socket; requesting forced closure.",
            socket->m_nId, socket->GetAccountID(), name
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    if (index <= 7U) {
        const std::uint32_t accountId = socket->GetAccountID();
        const std::int32_t characterId = socket->m_nCharacterDBIDs[index];
        db.RestoreCharacter(characterId, accountId, socket);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RestoreCharacter error. char index[%d]",
        //     index)
        // Log.Add(
        //     LOG_WARN,
        //     "[characters] Restoration request ignored on socket %u, account %u: slot %u exceeds this handler's upper bound
        //     of 7, no database restoration was requested.", socket->m_nId, socket->GetAccountID(), index)
        L2_LOG_WARN(
            Log,
            "[characters] Restoration request ignored on socket %u, account %u: slot %u exceeds this handler's upper bound "
            "of 7; no database restoration was requested.",
            socket->m_nId, socket->GetAccountID(), index
        );
    }
    return false;
}

bool RemainCoolTimePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0dc24 always returns false.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->SendCoolTime();
    }
    return false;
}

bool ReloadPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c19620.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    user->ParamChanged(std::int64_t{PCB_USER});
    const FVector position = user->GetPos();

    CContainer* container = nullptr;
    if (user->IsInRealWorld()) {
        const std::int32_t worldZ = runtime::Win64CvttDoubleToInt32(position.z);
        const std::int32_t worldY = runtime::Win64CvttDoubleToInt32(position.y);
        const std::int32_t worldX = runtime::Win64CvttDoubleToInt32(position.x);
        container = CContainerDB::Inst().GetWorldContainer(worldX, worldY, worldZ);
    } else {
        CInstantZone* const instantZone = user->GetInstantZone();
        container = instantZone != nullptr ? instantZone->GetContainer() : nullptr;
    }

    if (container == nullptr) {
        const std::int32_t errorY = runtime::Win64CvttDoubleToInt32(position.y);
        const std::int32_t errorX = runtime::Win64CvttDoubleToInt32(position.x);
        // Log.Add(
        //     LOG_ERROR,
        //     "maplock NULL pos(%d, %d), at file[%s], line[%d]",
        //     errorX,
        //     errorY,
        //     "UserSocket.cpp",
        //     0x28e7)
        // Log.Add(
        //     LOG_ERROR,
        //     "[world view] Reload stopped for socket %u, account %u: no container is available at observed position (%d,
        //     %d), nearby objects were not republished.", socket->m_nId, socket->GetAccountID(), errorX, errorY)
        L2_LOG_ERROR(
            Log,
            "[world view] Reload stopped for socket %u, account %u: no container is available at observed position (%d, "
            "%d); nearby objects were not republished.",
            socket->m_nId, socket->GetAccountID(), errorX, errorY
        );
        return false;
    }

    CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(container);

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(User* user) : user_(user) {}

        bool CanDo(CObject* object) override { return object != user_; }

    private:
        User* user_;
    } filter(user);

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(User* user) : user_(user) {}

        void Do(CObject* object) override {
            if (user_->IsDeleting() || user_->m_Deleted != 0) {
                return;
            }
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            user_->InSight(object, false);
        }

    private:
        User* user_;
    } function(user);

    FOR_ALL_OBJECT_IN_CONTAINER_INDEX(neighbors, filter, function);

    // The target performs three fresh GetPos calls in z/y/x evaluation
    // order before reading name/account identifiers.
    const FVector logZ = user->GetPos();
    const std::int64_t z = static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logZ.z));
    const FVector logY = user->GetPos();
    const std::int64_t y = static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logY.y));
    const FVector logX = user->GetPos();
    const std::int64_t x = static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(logX.x));
    const char16_t* const name = user->GetName();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t databaseId = user->GetDBID();
    const std::wstring accountName = Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);

    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,%s,%s,,,",
    //     std::int64_t{0x366},
    //     static_cast<std::int64_t>(databaseId),
    //     static_cast<std::int64_t>(accountId),
    //     x,
    //     y,
    //     z,
    //     name,
    //     accountName.c_str())
    Log.Add(
        LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,%s,%s,,,", std::int64_t{0x366},
        static_cast<std::int64_t>(databaseId), static_cast<std::int64_t>(accountId), x, y, z, name, accountName.c_str()
    );
    return false;
}

bool LogoutPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c12f04.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!user->IsNowTrade() || !CIOObject::IsInTime(static_cast<std::int32_t>(user->m_nTradeTimeStamp), 10)) {
        if (user->m_data->m_bIsInCombatMode == 0 || user->m_data->m_TrainingState == 1 ||
            !CIOObject::IsInTime(user->m_nCombatModeTimeStamp, 15)) {
            socket->Send("c", 0x84);
            socket->m_nCloseReason = SCR_QUIT;
        } else {
            socket->SendSystemMessage(SystemMessageId::CANNOT_LOG_OUT_DURING_COMBAT);
        }
    }
    return false;
}

bool MoveToLocationBackPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c13958.
    L2_GUARD_FUNCTION();

    int targetX = 0;
    int targetY = 0;
    int targetZ = 0;
    int originX = 0;
    int originY = 0;
    int originZ = 0;
    ::Disassemble(packet, "dddddd", &targetX, &targetY, &targetZ, &originX, &originY, &originZ);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
        (runtime::AddInt32Wrapping(targetX, 0x50000) >> 15) + 10,
        (runtime::AddInt32Wrapping(targetY, 0x40000) >> 15) + 10
    );
    if (zone == nullptr) {
        user->MoveToLocation(originX, originY, originZ, CCreature::UsePathFind);
    } else {
        user->MoveToLocation(targetX, targetY, targetZ, CCreature::UsePathFind);
    }
    return false;
}

bool ChangeMoveTypePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c13040.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    socket->SetLastUsePacket(0x35, true);

    std::uint32_t moveType = 0;
    ::Disassemble(packet, "d", &moveType);

    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (moveType < static_cast<std::uint32_t>(MT_END)) {
        user->ChangeMoveType(static_cast<MoveType>(moveType), 0);
    } else {
        User* const logUser = socket->GetUser();
        // Log.Add(
        //     LOG_ERROR,
        //     L"user invalid MoveType!!! char[%s][%d]",
        //     logUser->GetName(),
        //     moveType)
        // Log.Add(
        //     LOG_WARN,
        //     L"[movement] Mode change ignored for character %s on socket %u: requested mode %u is outside 0..%d.",
        //     logUser->GetName(), socket->m_nId, moveType, static_cast<int>(MT_END) - 1)
        // L2_LOG_WARN(Log,
        //     L"[movement] Mode change ignored for character %s on socket %u: requested mode %u is outside 0..%d.",
        //     logUser->GetName(), socket->m_nId, moveType, static_cast<int>(MT_END) - 1)
        L2_LOG_WARN(
            Log, u"[movement] Mode change ignored for character %s on socket %u: requested mode %u is outside 0..%d.",
            logUser->GetName(), socket->m_nId, moveType, static_cast<int>(MT_END) - 1
        );
    }
    return false;
}

bool ChangeStopTypePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c168ac.
    L2_GUARD_FUNCTION();

    int stopType = 0;
    ::Disassemble(packet, "d", &stopType);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (stopType == 0) {
        CObject* const target = user->m_spTarget.GetObjectW();
        CCreatureController* const controller = user->GetController();
        controller->Sit(0x9f6, target);
    } else if (stopType == 1) {
        user->GetController()->Stand(0x9f6);
    }
    return false;
}

bool MagicSkillListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c17a88.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t sourceId;
    std::int32_t serverId;
    std::int32_t targetId;
    ::Disassemble(packet, "ddd", &sourceId, &serverId, &targetId);

    if (static_cast<std::uint32_t>(serverId) != user->m_nId || static_cast<std::uint32_t>(sourceId) != user->GetDBID() ||
        static_cast<std::uint32_t>(targetId) == 0x000b2586U) {
        return false;
    }

    CCreature* const sourceCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sourceId));
    User* const sourceUser = sourceCreature != nullptr && sourceCreature->IsPC() ? sourceCreature->CastUser() : nullptr;

    CCreature* const targetCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetId));
    User* const targetUser = targetCreature != nullptr && targetCreature->IsPC() ? targetCreature->CastUser() : nullptr;
    if (targetUser == nullptr) {
        return false;
    }

    std::array<char16_t, 256> message{};
    if (sourceUser != nullptr) {
        FormatUtf16To(message.data(), message.size(), u"%s's answer is right.", sourceUser->GetName());
    } else {
        FormatUtf16To(message.data(), message.size(), u"[%d]'s answer is right.", sourceId);
    }
    targetUser->SendSystemMessage(u"SYS", message.data());
    return false;
}

bool MagicSkillUsePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b120.
    L2_GUARD_FUNCTION();

    std::int32_t skillId;
    std::int32_t controlPressed;
    char shiftPressed;
    ::Disassemble(packet, "ddc", &skillId, &controlPressed, &shiftPressed);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    user->OnMagicSkillUsePacket(skillId, controlPressed == 1, shiftPressed == 1);
    return false;
}

bool StartPledgeWarPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0be60.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 0x32> pledgeName;
    ::Disassemble(packet, "S", 0x32, pledgeName.data());
    const std::wstring hostPledgeName = Utf16ToWide(pledgeName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    return user->OnStartPledgeWarPacket(hostPledgeName.c_str());
}

bool StopPledgeWarPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0bf44.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> pledgeName;
    ::Disassemble(packet, "S", 0x32, pledgeName.data());
    const std::wstring hostPledgeNameStorage = Utf16ToWide(pledgeName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    const wchar_t* const hostPledgeName = hostPledgeNameStorage.c_str();
    if (user->CanStopPledgeWar(hostPledgeName)) {
        user->OnStopPledgeWarPacket(hostPledgeName);
    }
    return false;
}

bool SetPledgeCrestPacket(CSocket* baseSocket, const int size, const std::uint8_t* data) {
    // Current L2Server 0x00c13b08.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->HavePledgePower(PP_SET_CREST)) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::CREST)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_TO_REGISTER_CREST);
        return false;
    }
    if (pledge->IsWaitingToDismiss()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_CHANGE_CLAN_CREST_DURING_DISSOLUTION_GRACE_PERIOD);
        return false;
    }
    if (static_cast<std::uint32_t>(size) >= 0xc01U) {
        socket->SendSystemMessage(SystemMessageId::INVALID_IMAGE_DIMENSIONS);
        return false;
    }

    db.RequestSetPledgeCrest(user, pledge, data, size);
    socket->SendSystemMessage(size == 0 ? SystemMessageId::CREST_DELETED : SystemMessageId::CREST_REGISTERED);
    return false;
}

bool SetPledgeCrestPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c13c9c.
    L2_GUARD_FUNCTION();

    int size = 0;
    const std::uint8_t* const data = ::Disassemble(packet, "d", &size);
    return SetPledgeCrestPacket(baseSocket, size, data);
}

bool PledgeCrest(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0caf0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::uint32_t crestId;
    std::uint32_t pledgeSid;
    ::Disassemble(packet, "dd", &crestId, &pledgeSid);
    return user->OnPledgeCrest(crestId, pledgeSid);
}

bool PledgeInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0ca0c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (socket->GetUser() == nullptr) {
        return true;
    }

    std::uint32_t pledgeServerId;
    ::Disassemble(packet, "d", &pledgeServerId);
    if (CPledge* const pledge = CPledge::GetObjectW(pledgeServerId)) {
        pledge->SendPledgeInfo(socket);
    }
    return false;
}

bool RequestCastleSiegeInfoPacket(CSocket* baseSocket, const unsigned int residenceId) {
    // Current L2Server 0x00c0de90.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(residenceId));
    CCastle* castle = nullptr;
    CSiegeAgit* siegeAgit = nullptr;
    if (residence != nullptr) {
        castle = residence->CastCastle();
        if (residence->IsWorldAgit()) {
            siegeAgit = residence->CastWorldAgit()->CastSiegeAgit();
        }
    }

    if (castle != nullptr) {
        castle->SendCastleSiegeInfo(socket, user);
    } else if (siegeAgit != nullptr) {
        siegeAgit->SendCastleSiegeInfo(socket, user);
    }
    return false;
}

bool RequestCastleSiegeInfoPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0dfdc.
    L2_GUARD_FUNCTION();

    unsigned int residenceId;
    ::Disassemble(packet, "d", &residenceId);
    return RequestCastleSiegeInfoPacket(baseSocket, residenceId);
}

bool RequestCastleSiegeAttackerListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e098.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int residenceId;
    ::Disassemble(packet, "d", &residenceId);
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CCastle* castle = nullptr;
    CSiegeAgit* siegeAgit = nullptr;
    if (residence != nullptr) {
        castle = residence->CastCastle();
        if (residence->IsWorldAgit()) {
            siegeAgit = residence->CastWorldAgit()->CastSiegeAgit();
        }
    }

    if (castle != nullptr) {
        castle->SendCastleSiegeAttackerList(socket, user);
    } else if (siegeAgit != nullptr) {
        siegeAgit->SendSiegeAttackerList(socket, user);
    }
    return false;
}

bool RequestCastleSiegeDefenderListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e1e8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int residenceId;
    ::Disassemble(packet, "d", &residenceId);
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CCastle* castle = nullptr;
    CSiegeAgit* siegeAgit = nullptr;
    if (residence != nullptr) {
        castle = residence->CastCastle();
        if (residence->IsWorldAgit()) {
            siegeAgit = residence->CastWorldAgit()->CastSiegeAgit();
        }
    }

    if (castle != nullptr) {
        castle->SendCastleSiegeDefenderList(socket, user);
    } else if (siegeAgit != nullptr) {
        siegeAgit->SendSiegeDefenderList(socket, user);
    }
    return false;
}

bool RequestJoinCastleSiegePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c14e0c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int residenceId;
    int siegeRole;
    int registration;
    ::Disassemble(packet, "ddd", &residenceId, &siegeRole, &registration);

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || (!user->m_data->m_bPledgeMaster && !user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (pledge->IsWaitingToDismiss()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_SIEGE_DURING_CLAN_DISSOLUTION_GRACE_PERIOD);
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !g_ResidenceDB.CheckTalkableDistanceToMessenger(user, residenceId)) {
        return false;
    }

    CCastle* const castle = residence->CastCastle();
    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    CSiegeAgit* const siegeAgit = worldAgit != nullptr ? worldAgit->CastSiegeAgit() : nullptr;
    if (castle == nullptr) {
        if (siegeAgit == nullptr || siegeRole == 0) {
            return false;
        }
        if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::AGIT_SIEGE)) {
            socket->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_FOR_DEVASTATED_CASTLE_OR_FORTRESS_OF_DEAD_BATTLE);
            return false;
        }
        if (registration == 0) {
            db.RequestUnregisterSiegeAgitPledge(user, siegeAgit, pledge);
            return false;
        }
        if (siegeAgit->GetResidenceState() != RS_REGISTRATION) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_CLAN_HALL_SIEGE_DEADLINE_PASSED);
            return false;
        }
        if (siegeAgit->GetAttackerCount() >= 0x100) {
            socket->SendSystemMessage(SystemMessageId::NO_MORE_CLAN_HALL_SIEGE_REGISTRATIONS);
            return false;
        }
        if (pledge->GetWorldAgitDbId() != 0) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_JOIN_CLAN_HALL_SIEGE_ALREADY_OWNS_HALL);
            return false;
        }

        const int instantLookupPledgeId = static_cast<int>(pledge->GetDbId());
        if (CInstantAgitManager::inst().GetInstantAgit(instantLookupPledgeId) != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_JOIN_CLAN_HALL_SIEGE_ALREADY_OWNS_HALL);
            return false;
        }
        const int appliedLookupPledgeId = static_cast<int>(pledge->GetDbId());
        if (CInstantAgitManager::inst().IsAlreadyAppliedPledge(appliedLookupPledgeId)) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_REQUEST_MULTIPLE_CLAN_HALL_AUCTIONS_OR_DURING_SIEGE);
            return false;
        }
        db.RequestRegisterSiegeAgitPledge(user, siegeAgit, pledge);
        return false;
    }

    if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::CASTLE_SIEGE)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_FOR_CASTLE_SIEGE);
        return false;
    }
    if (CCastle* const ownedCastle = pledge->GetCastle()) {
        socket->SendSystemMessage(
            ownedCastle->GetResidenceID() == residenceId
                ? SystemMessageId::CASTLE_OWNER_CLAN_AUTOMATICALLY_REGISTERED_AS_DEFENDER
                : SystemMessageId::CASTLE_OWNER_CLAN_CANNOT_JOIN_ANOTHER_SIEGE
        );
        return false;
    }
    const ResidenceState residenceState = castle->GetResidenceState();
    if (residenceState != RS_REGISTRATION && residenceState != RS_TYPE_CHANGE) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_OR_CANCEL_OUTSIDE_SIEGE_REGISTRATION_PERIOD);
        return false;
    }

    const int reservedStatus = castle->GetReservedStatus(pledge->GetDbId(), pledge->GetWorldId());
    if (registration == 0) {
        if (reservedStatus != 0) {
            db.RequestQueryCastleSiegeForUnregister(user, castle);
        } else {
            socket->SendSystemMessage(SystemMessageId::NOT_REGISTERED_FOR_CASTLE_SIEGE);
        }
        return false;
    }
    if (reservedStatus != 0) {
        if (reservedStatus == 1) {
            socket->SendSystemMessage(SystemMessageId::CANCEL_ATTACKER_REGISTRATION_BEFORE_APPLYING_AS_DEFENDER);
        } else if (reservedStatus == 2) {
            socket->SendSystemMessage(SystemMessageId::CANCEL_DEFENDER_APPLICATION_BEFORE_REGISTERING_AS_ATTACKER);
        }
        return false;
    }

    if (siegeRole == 0) {
        if (castle->GetReservedDefenderCount() >= 0xff) {
            socket->SendSystemMessage(SystemMessageId::CASTLE_DEFENDER_APPLICATION_CLOSED);
            return false;
        }
    } else {
        bool canRegister = castle->GetReservedAttackerCount() < 0x100;
        if (!canRegister) {
            socket->SendSystemMessage(SystemMessageId::CASTLE_ATTACKER_REGISTRATION_CLOSED);
        }
        CFortress* const fortress = pledge->GetFortress();
        if (fortress != nullptr && castle->IsChildFortress(fortress)) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_SIEGE_WHILE_CONTRACTED_TO_CASTLE);
            canRegister = false;
        }
        if (!canRegister) {
            return false;
        }
    }

    db.RequestQueryCastleSiegeForRegister(user, castle, 2 - static_cast<int>(siegeRole != 0));
    return false;
}

bool RequestConfirmCastleSiegeWaitingListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c152d4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int residenceId;
    int pledgeSid;
    int rawOperation;
    ::Disassemble(packet, "ddd", &residenceId, &pledgeSid, &rawOperation);

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || (!user->m_data->m_bPledgeMaster && !user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (!g_ResidenceDB.CheckTalkableDistanceToMessenger(user, residenceId)) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsCastle()) {
        return false;
    }
    CCastle* const castle = residence->CastCastle();
    const ResidenceState residenceState = castle->GetResidenceState();
    if (residenceState != RS_REGISTRATION && residenceState != RS_TYPE_CHANGE) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_APPROVE_OR_REJECT_OUTSIDE_SIEGE_REGISTRATION_PERIOD);
        return false;
    }
    if (castle->GetResidenceOwnerID() != static_cast<int>(pledge->GetDbId()) ||
        castle->GetResidenceOwnerWorldID() != pledge->GetWorldId()) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_CHANGE_CASTLE_DEFENDER_LIST);
        return false;
    }

    CPledge* const targetPledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeSid));
    if (targetPledge == nullptr) {
        return false;
    }
    const int reservedStatus = castle->GetReservedStatus(targetPledge->GetDbId(), targetPledge->GetWorldId());
    if (reservedStatus == 2) {
        db.RequestConfirmCastleDefence(
            user, castle, targetPledge->GetDbId(), static_cast<std::uint32_t>(rawOperation), targetPledge->GetWorldId()
        );
    } else {
        castle->SendCastleSiegeDefenderList(socket, user);
    }
    return false;
}

bool RequestSetCastleSiegeTimePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c15558.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int residenceId;
    std::int32_t siegeTime;
    ::Disassemble(packet, "dd", &residenceId, &siegeTime);
    if (siegeTime == 0) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || (!user->m_data->m_bPledgeMaster && !user->HavePledgePower(PP_REGISTER_CASTLE_WAR))) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        return false;
    }
    if (!g_ResidenceDB.CheckTalkableDistanceToMessenger(user, residenceId)) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsCastle()) {
        return false;
    }
    CCastle* const castle = residence->CastCastle();
    if (castle->GetResidenceOwnerID() != static_cast<int>(pledge->GetDbId()) ||
        !CCastle::ReserveSiegeTime(castle->GetResidenceID(), siegeTime)) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_SET_SIEGE_TIME);
        return false;
    }

    db.RequestSetNextCastleSiege(user, castle, siegeTime);
    static_cast<void>(castle->SetSiegeDate(siegeTime));
    castle->ChangeResidenceState(RS_REGISTRATION);
    castle->BroadcastSetSiegeTime();
    castle->SendCastleSiegeInfo(socket, user);
    return false;
}

bool WriteHtmlPacket(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c16a14, original owner UserSocket.cpp.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 100> command{};
    std::array<SharedUtilsWideChar, 20> value1{};
    std::array<SharedUtilsWideChar, 20> value2{};
    std::array<SharedUtilsWideChar, 20> value3{};
    std::array<SharedUtilsWideChar, 256> title{};
    std::array<SharedUtilsWideChar, 3001> contents{};
    std::array<SharedUtilsWideChar, 20480> rawContents{};
    std::array<SharedUtilsWideChar, 20> sort{};

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (g_nCountryCode == 3 && socket->m_bEconomicRestriction) {
        socket->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }

    const std::uint8_t* const remainingPacket = ::Disassemble(packet, "S", 0xc8, command.data());

    if (TargetWideStartsWithAscii(command.data(), "_mail")) {
        CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(baseSocket, command.data());
        htmlCommand->SetPacket(remainingPacket);
        htmlCommand->Execute();
        return false;
    }
    if (TargetWideStartsWithAscii(command.data(), "_friend")) {
        CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(baseSocket, command.data());
        htmlCommand->SetPacket(remainingPacket);
        htmlCommand->Execute();
        return false;
    }
    if (TargetWideStartsWithAscii(command.data(), "_announce")) {
        CHtmlCommandSP htmlCommand = CHtmlCommandFactory::GetHtmlCommand(baseSocket, command.data());
        htmlCommand->SetPacket(remainingPacket);
        htmlCommand->Execute();
        return false;
    }

    const int mode = TargetWideToInt(command.data());
    if (mode == 6) {
        std::array<SharedUtilsWideChar, 256> saleDescription;
        saleDescription[0] = SharedUtilsWideChar{};
        std::memset(saleDescription.data() + 1, 0, 0x1fc);
        ::Disassemble(
            remainingPacket, "SSSSS", 0x28, value1.data(), 0x28, value2.data(), 0x28, value3.data(), 0xa000,
            rawContents.data(), 0x50, saleDescription.data()
        );
        CopyWriteHtmlContents(contents.data(), rawContents.data());
        std::int64_t minimumBid = GetInt64FromString(value3.data());
        if (minimumBid > 99900000000LL) {
            minimumBid = 99900000000LL;
        }
        const int bidDay = GetIntFromString(contents.data());
        std::wstring hostDescription = Utf16ToWide(saleDescription.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        CAgitHTML agitHtml(socket);
        agitHtml.AgitSale3(minimumBid, bidDay, hostDescription.data());
        return false;
    }

    if (mode == 7) {
        ::Disassemble(
            remainingPacket, "SSSSS", 0x28, value1.data(), 0x28, value2.data(), 0x28, value3.data(), 0xa000,
            rawContents.data(), 0x28, sort.data()
        );
        const int residenceId = TargetWideToInt(value1.data());
        const int minimumPrice = TargetWideToInt(value2.data());
        std::int64_t price = GetInt64FromString(value3.data());
        if (price > 99900000000LL) {
            price = 99900000000LL;
        }
        CAgitHTML agitHtml(socket);
        agitHtml.AgitBid2(residenceId, price, minimumPrice);
        return false;
    }

    ::Disassemble(
        remainingPacket, "SSSSS", 0x28, value1.data(), 0x28, value2.data(), 0x200, title.data(), 0xa000, rawContents.data(),
        0x28, sort.data()
    );
    CopyWriteHtmlContents(contents.data(), rawContents.data());

    SharedUtilsWideChar* separator = nullptr;
    for (SharedUtilsWideChar* cursor = value1.data(); *cursor != SharedUtilsWideChar{}; ++cursor) {
        if (*cursor == static_cast<SharedUtilsWideChar>('_')) {
            separator = cursor;
            break;
        }
    }
    int boardId = 0;
    int secondaryId = 0;
    if (separator == nullptr) {
        boardId = TargetWideToInt(value1.data());
    } else {
        *separator = SharedUtilsWideChar{};
        boardId = TargetWideToInt(value1.data());
        secondaryId = TargetWideToInt(separator + 1);
    }
    const int numericValue = TargetWideToInt(value2.data());

    for (int index = 0; static_cast<std::size_t>(index) < TargetWideLength(title.data()); ++index) {
        if (title[static_cast<std::size_t>(index)] == static_cast<SharedUtilsWideChar>('\'')) {
            title[static_cast<std::size_t>(index)] = static_cast<SharedUtilsWideChar>(0x7f);
        }
    }
    for (int index = 0; static_cast<std::size_t>(index) < TargetWideLength(contents.data()); ++index) {
        if (contents[static_cast<std::size_t>(index)] == static_cast<SharedUtilsWideChar>('\'')) {
            contents[static_cast<std::size_t>(index)] = static_cast<SharedUtilsWideChar>(0x7f);
        }
    }

    if (mode == 1) {
        if (TargetWideLength(title.data()) >= 129U) {
            return false;
        }
        if (boardId == -1) {
            commcli.RequestCreateMemo(user, title.data(), contents.data());
            return false;
        }
    } else if (mode == 2) {
        if (TargetWideLength(title.data()) >= 129U) {
            return false;
        }
    } else if (mode == 3) {
        if (TargetWideLength(title.data()) >= 129U) {
            return false;
        }
        if (boardId == -1) {
            commcli.RequestUpdateMemo(user, numericValue, title.data(), contents.data());
            return false;
        }
        if (!socket->IsWriteBBSBlocked()) {
            commcli.RequestBBSModify(user, boardId, secondaryId, numericValue, title.data(), contents.data(), sort.data());
            return false;
        }
        socket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
        commcli.RequestBBSInit(user, 0, 0, 0, 0, 1);
        return false;
    } else if (mode == 4) {
        if (TargetWideLength(title.data()) >= 128U) {
            return false;
        }
        if (boardId == -2) {
            commcli.RequestClanSetIntro(user, title.data());
            return false;
        }
        if (!socket->IsWriteBBSBlocked()) {
            commcli.RequestBBSWriteComment(user, boardId, secondaryId, numericValue, title.data());
            return false;
        }
        socket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
        commcli.RequestBBSInit(user, 0, 0, 0, 0, 1);
        return false;
    } else if (mode == 5) {
        if (TargetWideLength(title.data()) >= 32U) {
            return false;
        }
        for (int index = 0; static_cast<std::size_t>(index) < TargetWideLength(title.data()); ++index) {
            if (title[static_cast<std::size_t>(index)] == static_cast<SharedUtilsWideChar>('_')) {
                title[static_cast<std::size_t>(index)] = static_cast<SharedUtilsWideChar>(' ');
            }
        }
        if (boardId == -2) {
            commcli.RequestGetMemoList(user, 1, title.data());
            return false;
        }
        if (boardId == -1) {
            int sortType = 0;
            if (!TargetWideEqualsAscii(sort.data(), "Name") && TargetWideEqualsAscii(sort.data(), "Ruler")) {
                sortType = 1;
            }
            commcli.RequestClanBBSList(user, 1, sortType, title.data());
            return false;
        }
        int searchType = 0;
        if (!TargetWideEqualsAscii(sort.data(), "Title") && TargetWideEqualsAscii(sort.data(), "Writer")) {
            searchType = 1;
        }
        commcli.RequestBBSSearch(user, boardId, secondaryId, 1, searchType, title.data());
        return false;
    } else {
        return false;
    }

    if (!socket->IsWriteBBSBlocked()) {
        commcli.RequestBBSWrite(user, boardId, secondaryId, numericValue, title.data(), contents.data(), sort.data());
        return false;
    }
    socket->SendSystemMessage(SystemMessageId::NO_WRITE_PERMISSION);
    commcli.RequestBBSInit(user, 0, 0, 0, 0, 1);
    return false;
}

bool ShowBBSBoard(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0c03c.
    L2_GUARD_FUNCTION();

    int unused = 0;
    ::Disassemble(packet, "d", &unused);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    commcli.RequestBBSInit(user, 0, 0, 0, 0, 1);
    return false;
}

bool TutorialLinkHTML(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c17ca4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    int pageType = 1;
    std::array<SharedUtilsWideChar, 1024> decoded{};
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    ::Disassemble(packet, "dS", &pageType, 0x800, decoded.data());
    const std::u16string_view commandUtf16 = FixedUtf16View(decoded);
    const std::u16string command(commandUtf16);
    if (command.compare(0, 0x0f, u"tutorial_close_") == 0) {
        // wcstod parses native wide text.
        const double rawTutorialId = std::wcstod(Utf16ToWide(commandUtf16.substr(0x0f)).c_str(), nullptr);
        const int tutorialId = runtime::Win64CvttDoubleToInt32(rawTutorialId);
        socket->Send("c", 0xa9);
        if (tutorialId != 0) {
            g_npcserver.Send("cdd", 0x34, user->m_data->index, runtime::SubtractInt32Wrapping(0, tutorialId));
        }
        return false;
    }

    std::u16string html;
    if (g_HTMLCache.GetHTMLFile(SanitizeUtf16(commandUtf16).c_str(), html, user->m_data->m_CountryCode)) {
        socket->Send("cdS", 0xa6, pageType, html.c_str());
        if (user->IsBuilder()) {
            user->SendSystemMessage(u"Tutorial HTML", command.c_str());
        }
    }
    return false;
}

bool TutorialPassCmdToServer(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c2c668.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 256> decoded{};
    ::Disassemble(packet, "S", 0x200, decoded.data());
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    const std::u16string command = Utf16Text(decoded.data()).String();
    if (target_utf16::CompareN(command.c_str(), u"menu_select", 0x0b) == 0) {
        const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command.c_str() + 0x0b);
        const int reply = parameters.GetIntValue(u"reply");
        const int ask = parameters.GetIntValue(u"ask");
        g_npcserver.Send("cddd", 0x32, user->m_data->index, ask, reply);
    }
    return false;
}

bool TutorialMarkPressed(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0d3d8.
    L2_GUARD_FUNCTION();

    std::uint8_t type = 0;
    int questionId;
    ::Disassemble(packet, "cd", &type, &questionId);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    if (type < 2U) {
        g_npcserver.Send("cdcd", 0x33, user->m_data->index, runtime::Int8FromBits(type), questionId);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid question mark type[%d]",
        //     runtime::Int8FromBits(type))
        // Log.Add(
        //     LOG_WARN,
        //     "[tutorial] Mark click ignored on socket %u: type %u is outside 0..1. No NPC event was sent.",
        //     socket->m_nId, static_cast<unsigned>(type))
        L2_LOG_WARN(
            Log, "[tutorial] Mark click ignored on socket %u: type %u is outside 0..1; no NPC event was sent.",
            socket->m_nId, static_cast<unsigned>(type)
        );
    }
    return false;
}

bool TutorialClientEvent(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0d500.
    L2_GUARD_FUNCTION();

    int event;
    ::Disassemble(packet, "d", &event);
    if ((event & 0x221f) != 0) {
        auto* const socket = static_cast<CUserSocket*>(baseSocket);
        User* const user = socket->GetUser();
        if (user != nullptr) {
            g_npcserver.Send("cdd", 0x34, user->m_data->index, event);
        }
    }
    return false;
}

bool CUserSocket::RequestUserCommand(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c15764.
    L2_GUARD_FUNCTION();

    if (baseSocket == nullptr) {
        return false;
    }
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    int command = 0;
    ::Disassemble(packet, "d", &command);
    // Original-behavior bug: 0x00c1581f..0x00c15857 sends a transfer
    // restriction on every Olympiad-host user command, then executes it.
    // Command 0 (/loc) only reports coordinates and is not restricted.
    // Preserve the other command policies. Suppress this misleading warning.
    // if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
    if (command != 0 && Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        user->SendSystemMessage(
            command == 0x6d
                ? static_cast<unsigned int>(SystemMessageId::CANNOT_OPEN_OLYMPIAD_WINDOW_WHILE_PARTICIPATING_OR_OBSERVING)
                : static_cast<unsigned int>(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER)
        );
    }
    static_cast<void>(user->OnRequestUserCommand(command));
    return false;
}

bool ShowPledgeMemberListOpenPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b220.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge != nullptr) {
        pledge->OpenAllWindowPacket(user, false, nullptr);
    } else {
        socket->Send(
            "cddddSS", 0x5a, 0, runtime::Int32FromBits(user->m_nPledgeSid), user->m_data->m_nWorldId, 0, u"", u""
        );
    }
    return false;
}

bool AllianceCrestPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c20734.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t crestDatabaseId;
    std::int32_t allianceDatabaseId;
    std::int32_t allianceWorldId;
    ::Disassemble(packet, "ddd", &crestDatabaseId, &allianceDatabaseId, &allianceWorldId);

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(allianceWorldId, allianceDatabaseId));
    if (!alliance) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AllianceCrestPacket::Can not Find Alliance "
        //     L"AllianceDBID[%d] AllianceWorldID[%d]",
        //     allianceDatabaseId,
        //     allianceWorldId)
        // Log.Add(
        //     LOG_WARN,
        //     "[alliance crest] No crest reply requested on socket %u for alliance DB ID %d in world %d: the alliance was
        //     not found.", socket->m_nId, allianceDatabaseId, allianceWorldId)
        L2_LOG_WARN(
            Log,
            "[alliance crest] No crest reply requested on socket %u for alliance DB ID %d in world %d: the alliance was not "
            "found.",
            socket->m_nId, allianceDatabaseId, allianceWorldId
        );
        return false;
    }
    if (alliance->GetMasterPledge() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AllianceCrestPacket::Can not Find Pledge "
        //     L"AllianceDBID[%d] AllianceWorldID[%d]",
        //     allianceDatabaseId,
        //     allianceWorldId)
        // Log.Add(
        //     LOG_WARN,
        //     "[alliance crest] No crest reply requested on socket %u for alliance DB ID %d in world %d: the alliance leader
        //     clan was not found.", socket->m_nId, allianceDatabaseId, allianceWorldId)
        L2_LOG_WARN(
            Log,
            "[alliance crest] No crest reply requested on socket %u for alliance DB ID %d in world %d: the alliance leader "
            "clan was not found.",
            socket->m_nId, allianceDatabaseId, allianceWorldId
        );
        return false;
    }

    CPledgeCrestSP crest = g_PledgeCrestMap.FindObject(CPledgeCrestKey(allianceWorldId, crestDatabaseId));
    if (!crest) {
        db.RequestGetAllianceCrest(
            user, static_cast<std::uint32_t>(crestDatabaseId), static_cast<std::uint32_t>(allianceWorldId)
        );
    } else {
        const int bitmapSize = crest->bitmapSize();
        socket->SafeSend(0x80000, "cdddb", 0xaf, allianceWorldId, crestDatabaseId, bitmapSize, bitmapSize, crest->bitmap());
    }
    return false;
}

bool ShortCutRegisterPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a82c.
    L2_GUARD_FUNCTION();

    int type = 0;
    int slot = 0;
    int id = 0;
    int skillLevelSet = 0;
    int category = 0;
    int arg7 = 0;
    int arg8 = 0;
    ::Disassemble(packet, "ddddddd", &type, &slot, &id, &skillLevelSet, &category, &arg7, &arg8);
    category = runtime::AddInt32Wrapping(category, -1);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->m_ShortCut.AddShortCut(slot, type, id, category, true, skillLevelSet, arg7, arg8);
    }
    return user == nullptr;
}

bool ShortCutDeletePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a968.
    L2_GUARD_FUNCTION();

    int slot = 0;
    ::Disassemble(packet, "d", &slot);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->m_ShortCut.DeleteShortCut(slot);
    }
    return user == nullptr;
}

bool WithdrawPartyPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a3b4.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        socket->Send("cd", 0x3b, 0);
    } else if (party->m_data->m_OnlineMaster.id == user->m_nId) {
        party->Dismiss(false, true);
    } else if (!party->m_data->IsPVPMatching()) {
        party->Withdraw(user, false);
        socket->Send("cd", 0x3b, 1);
    } else {
        socket->Send("cd", 0x3b, 0);
    }
    return false;
}

bool OustPartyMemberPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a508.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> name;
    name[0] = 0;
    std::memset(name.data() + 1, 0, 0x30);
    ::Disassemble(packet, "S", 0x32, name.data());

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        socket->Send("cd", 0x3c, 0);
    } else {
        std::wstring hostName = Utf16ToWide(name.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        party->OnPacketOust(*user, hostName.data());
    }
    return false;
}

bool DismissPartyPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a630.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        socket->Send("cd", 0x3e, 0);
    } else if (party->m_data->m_OnlineMaster.id == user->m_nId) {
        party->Dismiss(false, false);
        socket->Send("cd", 0x3e, 1);
    } else {
        socket->Send("cd", 0x3e, 0);
    }
    return false;
}

bool ReplyListPartyWaitingPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c198c8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (!user->m_data->m_bCanJoinParty) {
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    int page = 0;
    int location = 0;
    int level = 0;
    ::Disassemble(packet, "ddd", &page, &location, &level);
    CPartyRoomListFilter filter(
        CSPointer<User>(user, ObjectRefType::FunctionChain), page, static_cast<LocationFilter>(location),
        static_cast<LevelFilter>(level), false
    );
    CPartyMatchingRequest::inst().ListRoom(filter);
    return false;
}

bool ReplyManagePartyRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c19ab4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->m_data->m_bCanJoinParty) {
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    int roomId = 0;
    int maxMember = 0;
    int lowerLevel = 0;
    int upperLevel = 0;
    int routing = 0;
    std::array<char16_t, 32> title;
    ::Disassemble(packet, "dddddS", &roomId, &maxMember, &lowerLevel, &upperLevel, &routing, 0x40, title.data());

    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (user->m_dwStopPartyMatch != 0) {
        const std::uint32_t now = runtime::GetTickCount();
        const std::uint32_t remainingMinutes = (user->m_dwStopPartyMatch - now) / 60000U;
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        user->GetSocket()->SendSystemMessage_d(
            SystemMessageId::PARTY_MATCHING_DISABLED_WITH_MINUTES_UNTIL_AVAILABLE,
            runtime::Int32FromBits(remainingMinutes)
        );
        return false;
    }
    if (!user->CheckCreatePartyMatchLevelLimit()) {
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::REGISTRATION_FAILED);
        return false;
    }

    CParty* const party = user->GetParty();
    if (party != nullptr) {
        routing = static_cast<int>(party->GetRoutingType());
    }
    CPartyRoomInfo roomInfo(
        roomId, CSPointer<User>(user, ObjectRefType::FunctionChain), lowerLevel, upperLevel, maxMember, routing, title.data()
    );
    CPartyMatchingRequest::inst().ManageRoom(roomInfo);
    return false;
}

bool ReplyJoinPartyRoom(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c19dac.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (!user->m_data->m_bCanJoinParty) {
        user->GetSocket()->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    int roomId = 0;
    int location = 0;
    int level = 0;
    int page = 0;
    ::Disassemble(packet, "dddd", &roomId, &location, &level, &page);
    CPartyRoomListFilter filter(
        CSPointer<User>(user, ObjectRefType::FunctionChain), page, static_cast<LocationFilter>(location),
        static_cast<LevelFilter>(level), false
    );
    CPartyMatchingRequest::inst().JoinRoom(CSPointer<User>(user, ObjectRefType::FunctionChain), roomId, filter);
    return false;
}

bool CannotMoveAnymorePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0aa44.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int x = 0;
    int y = 0;
    int z = 0;
    int unused = 0;
    ::Disassemble(packet, "dddd", &x, &y, &z, &unused);

    const FVector requestedPosition(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    if (!user->BeClosedToPassingPos(requestedPosition)) {
        const FVector currentPosition = user->GetPos();
        const double deltaX = currentPosition.x - requestedPosition.x;
        const double deltaY = currentPosition.y - requestedPosition.y;
        const double deltaZ = currentPosition.z - requestedPosition.z;
        if (deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ >= 10000.0) {
            user->StopMove();
            return false;
        }
    }
    user->MoveToLocation(x, y, z, CCreature::UsePathFind);
    return false;
}

bool MoveWithDeltaPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b348.
    L2_GUARD_FUNCTION();

    int deltaX = 0;
    int deltaY = 0;
    int deltaZ = 0;
    ::Disassemble(packet, "ddd", &deltaX, &deltaY, &deltaZ);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const FVector currentPosition = user->m_data->GetPos();
    user->MoveToLocation(
        runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(currentPosition.x), deltaX),
        runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(currentPosition.y), deltaY),
        runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(currentPosition.z), deltaZ), CCreature::UsePathFind
    );
    return false;
}

bool AttackPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c096f8.
    L2_GUARD_FUNCTION();

    std::uint32_t targetId;
    int x;
    int y;
    int z;
    char forceAttack;
    ::Disassemble(packet, "ddddc", &targetId, &x, &y, &z, &forceAttack);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnAttackPacket(targetId, forceAttack);
}

bool ActionPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a2a8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetId = 0;
    int x;
    int y;
    int z;
    char shiftPressed;
    ::Disassemble(packet, "ddddc", &targetId, &x, &y, &z, &shiftPressed);
    return user->OnActionPacket(targetId, shiftPressed != '\0');
}

bool TargetUnselectedPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0ac18.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int16_t request;
    ::Disassemble(packet, "h", &request);
    return user->OnTargetUnselectedPacket(request);
}

bool GetOnVehiclePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b488.
    L2_GUARD_FUNCTION();

    std::uint32_t vehicleId;
    int x;
    int y;
    int z;
    ::Disassemble(packet, "dddd", &vehicleId, &x, &y, &z);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    // Current FindObject keeps this reference, and the packet route
    // has no matching Release.
    CVehicle* const vehicle = CVehicle::FindObject(vehicleId);
    if (vehicle == nullptr || !vehicle->IsVehicle()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    user->StopMove();
    static_cast<void>(user->CanGetOnVehicle());
    if (user->HavePet() || user->HaveSummon()) {
        user->SendSystemMessage(SystemMessageId::UNSUMMON_PET_OR_SERVITOR_BEFORE_BOAT_DEPARTURE);
    }

    const FVector position(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    CVehicle* const castVehicle = vehicle->CastVehicle();
    CWorld& world = CWorld::Inst();
    static_cast<void>(world.GetOnVehicle(user, castVehicle, position));
    return false;
}

bool TargetUserFromMenu(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1ed90.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 25> targetName;
    std::memset(targetName.data(), 0, 0x32);
    ::Disassemble(packet, "S", 0x32, targetName.data());

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 25> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(targetName.data(), lowerName.data());
    const std::u16string hostLowerNameUtf16(FixedUtf16View(lowerName));
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);

    int selectedId = 0;
    if (target) {
        std::optional<FVector> userPositionStorage;
        {
            L2_GUARD(*user->m_lock);
            userPositionStorage.emplace(user->GetPos());
        }
        const FVector& userPosition = *userPositionStorage;

        std::optional<FVector> targetPositionStorage;
        {
            L2_GUARD(*target->m_lock);
            targetPositionStorage.emplace(target->GetPos());
            selectedId = runtime::Int32FromBits(target->Id());
        }
        const FVector& targetPosition = *targetPositionStorage;

        if (!(userPosition.DistCube(targetPosition) < 225.0)) {
            selectedId = 0;
        }
    }

    socket->Send("cSd", 0x7d, reinterpret_cast<const char16_t*>(targetName.data()), selectedId);
    return false;
}

bool MoveToLocationInVehiclePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c3dc.
    L2_GUARD_FUNCTION();

    std::uint32_t vehicleId;
    int targetX;
    int targetY;
    int targetZ;
    int originX;
    int originY;
    int originZ;
    ::Disassemble(packet, "ddddddd", &vehicleId, &targetX, &targetY, &targetZ, &originX, &originY, &originZ);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CObject* const object = CObject::GetObjectW(vehicleId);
    if (object == nullptr || !object->IsVehicle()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    user->MoveToLocationRelative(static_cast<CVehicle*>(object), targetX, targetY, targetZ);
    return false;
}

bool CannotMoveAnymoreInVehiclePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c530.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int vehicleId;
    int x;
    int y;
    int z;
    int heading;
    ::Disassemble(packet, "ddddd", &vehicleId, &x, &y, &z, &heading);
    if (user->IsInVehicle()) {
        const FVector relativePosition(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
        user->ValidateLocationInVehicle(relativePosition, heading, vehicleId);
    }
    user->StopMove();
    return false;
}

bool WarehouseDepositPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0acf4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        user->TradeCancel(false);
        return false;
    }
    if (user->GetPrivateStoreType() != PST_NONE) {
        user->SendSystemMessage(SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ONE) != nullptr || user->GetPendingUseETCItem(PUIT_TWO) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        return false;
    }
    if (!user->IsItemUsable(0)) {
        return false;
    }

    enum class DepositAction { Invalid, OutOfRange, NoPledge, Request };
    CWareHouseDeposit* warehouse = nullptr;
    const DepositAction depositAction = [&] {
        L2_GUARD(*user->m_lock);
        if (user->m_pEconomy == nullptr) {
            return DepositAction::Invalid;
        }
        if (typeid(*user->m_pEconomy) != typeid(CWareHouseDeposit)) {
            return DepositAction::Invalid;
        }
        warehouse = static_cast<CWareHouseDeposit*>(user->m_pEconomy);
        L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
        if (!warehouse->IsInRange()) {
            return DepositAction::OutOfRange;
        }
        if (warehouse->m_nWarehouseType == 2 && user->GetPledge() == nullptr) {
            user->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
            return DepositAction::NoPledge;
        }
        return DepositAction::Request;
    }();
    if (depositAction == DepositAction::Invalid) {
        return false;
    }
    if (depositAction == DepositAction::OutOfRange) {
        {
            L2_GUARD(*user->m_lock);
            socket->Send("cd", 0x43, -3);
            user->SendSystemMessage(SystemMessageId::WAREHOUSE_TOO_FAR);
            user->ReleaseEconomy();
        }
    } else if (depositAction == DepositAction::NoPledge) {
        L2_RELEASE(warehouse, ObjectRefType::Anonymous);
        return false;
    } else {
        warehouse->Request(packet);
    }

    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool WarehouseWithdrawPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2788c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        user->TradeCancel(false);
        return false;
    }
    if (user->GetPrivateStoreType() != PST_NONE) {
        user->SendSystemMessage(SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ONE) != nullptr || user->GetPendingUseETCItem(PUIT_TWO) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        return false;
    }
    if (!user->IsItemUsable(0)) {
        return false;
    }

    std::uint32_t itemCount = 0;
    const std::uint8_t* const itemList = ::Disassemble(packet, "d", &itemCount);
    if (itemCount == 0U || itemCount > warehouse::kMaxWithdrawalItems) {
        socket->Send("cd", 0x43, -3);
        return false;
    }

    CWareHouseWithdraw* warehouse = nullptr;
    {
        L2_GUARD(*user->m_lock);
        if (user->m_pEconomy == nullptr) {
            return false;
        }
        if (typeid(*user->m_pEconomy) != typeid(CWareHouseWithdraw)) {
            return false;
        }

        warehouse = static_cast<CWareHouseWithdraw*>(user->m_pEconomy);
        L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
    }

    ItemAmountDataStruct amounts;
    const std::uint8_t* cursor = itemList;
    for (std::uint32_t index = 0; index < itemCount; ++index) {
        int itemDatabaseId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dQ", &itemDatabaseId, &amount);
        // Inventory checks also accept removal deltas. Withdrawals never do.
        if (amount < 1 || amount > warehouse::kMaxStoredAmount) {
            socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
            L2_RELEASE(warehouse, ObjectRefType::Anonymous);
            return false;
        }
        const int itemClassId = warehouse->GetClassId(itemDatabaseId);
        if (CItemRestrict::Inst().CheckItemRestrict(itemClassId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]item restrict on. classid[%d], "
            //     L"dbid[%d], count[%I64d]",
            //     "UserSocket.cpp",
            //     0xf16,
            //     itemClassId,
            //     itemDatabaseId,
            //     amount)
            // Log.Add(
            //     LOG_WARN,
            //     "[warehouse] Withdrawal batch rejected on socket %u: item class %d is restricted (item DB ID %d, requested
            //     amount %lld), no withdrawal was requested.", socket->m_nId, itemClassId, itemDatabaseId, static_cast<long
            //     long>(amount))
            L2_LOG_WARN(
                Log,
                "[warehouse] Withdrawal batch rejected on socket %u: item class %d is restricted (item DB ID %d, requested "
                "amount %lld); no withdrawal was requested.",
                socket->m_nId, itemClassId, itemDatabaseId, static_cast<long long>(amount)
            );
            L2_RELEASE(warehouse, ObjectRefType::Anonymous);
            return false;
        }
        if (itemClassId < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Invalid withdraw item. "
            //     L"dbid[%d], count[%I64d]",
            //     "UserSocket.cpp",
            //     0xf21,
            //     itemDatabaseId,
            //     amount)
            // Log.Add(
            //     LOG_WARN,
            //     "[warehouse] Withdrawal batch rejected on socket %u: no valid class was found for item DB ID %d (observed
            //     class %d, requested amount %lld). No withdrawal was requested.", socket->m_nId, itemDatabaseId,
            //     itemClassId, static_cast<long long>(amount))
            L2_LOG_WARN(
                Log,
                "[warehouse] Withdrawal batch rejected on socket %u: no valid class was found for item DB ID %d (observed "
                "class %d, requested amount %lld); no withdrawal was requested.",
                socket->m_nId, itemDatabaseId, itemClassId, static_cast<long long>(amount)
            );
            L2_RELEASE(warehouse, ObjectRefType::Anonymous);
            return false;
        }
        amounts.Push(itemClassId, amount);
    }

    if (!user->m_inv.CheckAddableMulti(amounts, false)) {
        L2_RELEASE(warehouse, ObjectRefType::Anonymous);
        return false;
    }
    if (!warehouse->IsInRange()) {
        socket->Send("cd", 0x43, -3);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_TOO_FAR);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
    } else {
        // The current body passes the list start, not the
        // cursor advanced while validating the rows above.
        warehouse->Request(itemList, static_cast<int>(itemCount));
    }

    L2_RELEASE(warehouse, ObjectRefType::Anonymous);
    return false;
}

bool RequestPackageSendableItemList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0dcdc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        int requestValue = 0;
        ::Disassemble(packet, "d", &requestValue);
        user->SendPackageSendableList(requestValue);
    }
    return false;
}

bool SysCMD2Packet(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0c2fc.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsBuilder() || user->IsLowLevelBuilder()) {
        return BuilderCmd2Handler(socket, packet);
    }
    return false;
}

bool DestroyQuest(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0c8e8.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    int questId = 0;
    ::Disassemble(packet, "d", &questId);
    if (user->m_nFieldCycleID == 0 || user->m_nFieldCycleQuestID != questId ||
        user->m_nFieldCycleID != user->m_nFieldCycleIDOfQuest) {
        if (user->OnDestroyQuest(questId)) {
            FieldCycleStuff::OnUserDestroyQuest(user, questId);
        }
    } else {
        user->SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MISSION_IN_KEUCEREUS_BASE_DEFENSE_AREA);
        user->SendQuestList(nullptr);
    }
    return false;
}

bool OpenMinimap(CSocket* const baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00c0f128.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->GetSocket()->Send("cdc", 0xa3, 0, static_cast<int>(SSQSystem::inst().GetCurrentStatus()));
    }
    return user == nullptr;
}

bool RequestSSQStatus(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c0ed60.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char requestType = 0;
    ::Disassemble(packet, "c", &requestType);
    if (requestType == 1) {
        SSQSystem::inst().SendStatusInfoTo(user);
    } else if (requestType == 2) {
        std::array<char, 0x5000> buffer{};
        const int length = TimeAttackBoard::inst().MakeTimeAttackInfo(buffer.data(), static_cast<int>(buffer.size()));
        SSQSystem& ssq = SSQSystem::inst();
        if (length > 0) {
            socket->Send(
                "ccccb", 0xfb, static_cast<int>(requestType), static_cast<int>(ssq.GetCurrentStatus()), 1, length,
                buffer.data()
            );
        }
    } else if (requestType == 3) {
        SSQSystem::inst().SendSealInfoTo(user);
    } else if (requestType == 4) {
        SSQSystem::inst().SendPreResultTo(user);
    }
    return false;
}

bool RequestPackageSend(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c18da0.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }
    if (user->GetPrivateStoreType() != 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr || user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr ||
        user->GetPendingUseETCItem(PUIT_ONE) != nullptr || user->GetPendingUseETCItem(PUIT_TWO) != nullptr ||
        user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        return false;
    }

    if (user->m_pEconomy != nullptr && user->m_pEconomy->IsWarehouse()) {
        auto* const warehouse = static_cast<CWareHouseDeposit*>(user->m_pEconomy);
        CSPointer<CNPC> npc = warehouse->GetNPCSP();
        if (npc && !user->IsTalkableDistance(npc.get())) {
            user->SendSystemMessage(SystemMessageId::PACKAGE_SEND_FAILED_WAREHOUSE_TOO_FAR);
            return false;
        }
    }
    user->RequestPackageSend(packet);
    return false;
}

bool GMCommand(CSocket* const baseSocket, const std::uint8_t* const packet) {
    // Current L2Server 0x00c1f324.
    L2_GUARD_FUNCTION();
    if (baseSocket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error, Cannot find Usersocket.",
        //     "UserSocket.cpp",
        //     0x1720)
        // Log.Add(
        //     LOG_ERROR,
        //     "[builder] Command ignored: the handler received no user socket.")
        L2_LOG_ERROR(Log, "[builder] Command ignored: the handler received no user socket.");
        return false;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error, Cannot find Usersocket's user.",
        //     "UserSocket.cpp",
        //     0x1728)
        // Log.Add(
        //     LOG_WARN,
        //     "[builder] Command ignored on socket %u: no character is bound to this socket.",
        //     socket->m_nId)
        L2_LOG_WARN(Log, "[builder] Command ignored on socket %u: no character is bound to this socket.", socket->m_nId);
        return false;
    }
    if (!user->IsBuilder()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack. Not a GM try GMCommand packet. [%s]",
        //     "UserSocket.cpp",
        //     0x172e,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[builder] Command rejected for character %s on socket %u: builder permission is required. Requesting socket
        //     closure.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[builder] Command rejected for character %s on socket %u: builder permission is required. Requesting socket
        //     closure.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[builder] Command rejected for character %s on socket %u: builder permission is required; requesting socket "
            u"closure.",
            user->GetName(), socket->m_nId
        );
        user->LogAbnormalAction(AUA_TRY_GM_COMMAND, true, 0, 0, 0, 0);
        return true;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    targetName[0] = 0;
    std::memset(targetName.data() + 1, 0, 0x30);
    std::array<SharedUtilsWideChar, 28> lowerName;
    lowerName[0] = 0;
    std::memset(lowerName.data() + 1, 0, 0x30);
    int commandType = 0;
    int unused = 0;
    ::Disassemble(packet, "Sdd", 0x32, targetName.data(), &commandType, &unused);
    static_cast<void>(unused);
    ::ToLowerCase(targetName.data(), lowerName.data());
    const std::u16string hostLowerNameUtf16(FixedUtf16View(lowerName));
    const CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        return false;
    }

    switch (commandType) {
        case 1:
            target->SendUserInfo(socket, nullptr);
            break;
        case 2: {
            CPledge* const pledge = target->GetPledge();
            if (pledge != nullptr) {
                pledge->OpenAllWindowPacket(user, true, target->GetName());
            }
            break;
        }
        case 3:
            target->SendSkillList(socket, false, 0);
            break;
        case 4:
            target->SendQuestList(socket);
            break;
        case 5:
            target->SendItemList(false, true, socket);
            break;
        case 6:
            if (runtime::raw_atomic::ExchangeI32(&target->m_lPackageItemPacketJobFlag, 1) == 1) {
                const std::wstring accountName = ::Utf16ToWide(std::u16string_view(target->m_sAccountName.data()));
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] UsePackageItemPacketWait. "
                //     L"ACCOUNT(id[%d]/name[%s]) // CHAR[%s] // "
                //     L"ItemCount(%d/%d)",
                //     L"UserSocket.cpp",
                //     0x1764,
                //     target->GetAccountID(),
                //     accountName.c_str(),
                //     target->GetName(),
                //     target->m_lCurrentReceivedPackageItemCount,
                //     target->m_lTotalPackageItemCount)
                // Log.Add(
                //     LOG_NORMAL,
                //     L"[builder] Package-item inspection request ignored on socket %u for account %u (%s), character %s:
                //     loading is already pending (observed items %d of %d). No additional load was requested.",
                //     socket->m_nId, target->GetAccountID(), accountName.c_str(), target->GetName(),
                //     target->m_lCurrentReceivedPackageItemCount, target->m_lTotalPackageItemCount)
                // L2_LOG_INFO(Log,
                //     L"[builder] Package-item inspection request ignored on socket %u for account %u (%s), character %s:
                //     loading is already pending (observed items %d of %d). No additional load was requested.",
                //     socket->m_nId, target->GetAccountID(), accountName.c_str(), target->GetName(),
                //     target->m_lCurrentReceivedPackageItemCount, target->m_lTotalPackageItemCount)
                L2_LOG_INFO(
                    Log,
                    u"[builder] Package-item inspection request ignored on socket %u for account %u (%s), character %s: "
                    u"loading is already pending (observed items %d of %d); no additional load was requested.",
                    socket->m_nId, target->GetAccountID(), accountName.c_str(), target->GetName(),
                    target->m_lCurrentReceivedPackageItemCount, target->m_lTotalPackageItemCount
                );
            } else {
                db.RequestWareHouseItemList(*target.get(), *user, 1);
            }
            break;
        case 7:
        case 8:
        case 9:
            break;
        default:
            socket->SendSystemMessage(u"SYS", u"Unknown builder command type");
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Erorr. Unknown nGMCommandType[%d].",
            //     "UserSocket.cpp",
            //     0x177d,
            //     commandType)
            // Log.Add(
            //     LOG_WARN,
            //     "[builder] Command type %d is not supported on socket %u. An unknown-command reply was requested.",
            //     commandType, socket->m_nId)
            L2_LOG_WARN(
                Log, "[builder] Command type %d is not supported on socket %u; an unknown-command reply was requested.",
                commandType, socket->m_nId
            );
            break;
    }
    return false;
}

bool GetOffVehiclePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b638.
    L2_GUARD_FUNCTION();

    std::uint32_t vehicleId = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "dddd", &vehicleId, &x, &y, &z);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    // Current FindObject keeps the raw result, and this exact packet path
    // has no matching Release before returning.
    CVehicle* const vehicle = CVehicle::FindObject(vehicleId);
    if (vehicle == nullptr || !vehicle->IsVehicle()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    int adjustedX = x;
    int adjustedY = y;
    const int queryZ = runtime::AddInt32Wrapping(z, 0x20);
    int groundZ = CWorld::Inst().GetGeo()->FindGround(adjustedX, adjustedY, queryZ);
    bool foundGround = runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20;
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(runtime::AddInt32Wrapping(x, 0x10), y, queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedX = runtime::AddInt32Wrapping(x, 0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(runtime::AddInt32Wrapping(x, -0x10), y, queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedX = runtime::AddInt32Wrapping(x, -0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(x, runtime::AddInt32Wrapping(y, 0x10), queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedY = runtime::AddInt32Wrapping(y, 0x10);
            foundGround = true;
        }
    }
    if (!foundGround) {
        groundZ = CWorld::Inst().GetGeo()->FindGround(x, runtime::AddInt32Wrapping(y, -0x10), queryZ);
        if (runtime::AbsoluteInt32Wrapping(runtime::SubtractInt32Wrapping(groundZ, z)) < 0x20) {
            adjustedY = runtime::AddInt32Wrapping(y, -0x10);
            foundGround = true;
        }
    }

    FVector destination(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    if (foundGround) {
        destination = FVector(static_cast<double>(adjustedX), static_cast<double>(adjustedY), static_cast<double>(groundZ));
    }

    user->StopMove();
    CWorld::Inst().GetOffVehicle(user, vehicle->CastVehicle(), destination, true);
    return false;
}

bool IconAction(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0b944.
    L2_GUARD_FUNCTION();

    int actionId = 0;
    int controlPressed = 0;
    char shiftPressed = 0;
    ::Disassemble(packet, "ddc", &actionId, &controlPressed, &shiftPressed);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const bool fifthClassBlockedAction = actionId == 10 || actionId == 0x1c || actionId == 0x33 || actionId == 0x3d ||
        actionId == 0x41 || (actionId > 0x46 && (actionId < 0x4a || (actionId > 0x4d && actionId < 0x56)));
    if (fifthClassBlockedAction && User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    user->BasicAction(actionId, controlPressed != 0, shiftPressed != 0, STP_MAX);
    return false;
}

bool CUserSocket::MoveToLocationPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c11d9c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return false;
    }

    socket->SetLastUsePacket(0x0f, true);
    int targetX = 0;
    int targetY = 0;
    int targetZ = 0;
    int originX = 0;
    int originY = 0;
    int originZ = 0;
    int moveType = 0;
    ::Disassemble(packet, "ddddddd", &targetX, &targetY, &targetZ, &originX, &originY, &originZ, &moveType);
    return user->OnMoveToLocationPacket(targetX, targetY, targetZ, originX, originY, originZ, moveType);
}

bool EnterWorldPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2d710.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    socket->SetLastUsePacket(0x11, true);
    if (user == nullptr) {
        return true;
    }

    int eventState = 0;
    if (g_L2Time.IsForceShillienMoon()) {
        eventState = 1;
    } else {
        CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_AGATHION);
        auto* const agathion = static_cast<CEvent_AgathionDrop*>(event);
        if (event != nullptr && event->InEventTime() && !agathion->IsForceReachGoal() && !agathion->IsConquested()) {
            eventState = 1;
        }
    }
    socket->Send("chh", 0xfe, 0x117, eventState);

    if (!g_bChargeFree && user->m_data->m_nWorldId == g_nWorldId && g_bDualBillingSystem &&
        socket->CheckPremiumType(PremiumUserType::PUT_KOREA_PAID_USER) &&
        runtime::raw_atomic::ExchangeI32(&socket->m_ChargeStart, 1) == 0) {
        authd.AuthChargeStart(static_cast<int>(user->GetAccountID()));
        CLogFormatter formatter;
        formatter.WriteLog1(0x1fb1, user, nullptr, nullptr, nullptr, nullptr, 1, 0, 0, 0, 0, 0, 0);
    }
    return user->OnEnterWorldPacket(socket, packet);
}

bool AppearingPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a770.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnAppearingPacket();
}

bool RestartPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c240dc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    {
        L2_GUARD(socket->mLock);
        socket->SetLastUsePacket(0x57, true);

        User* const user = socket->GetUser();
        if (user == nullptr) {
            return false;
        }
        if (user->m_data->m_bIsAlive == 0) {
            socket->Send("cd", 0x71, 0);
            return false;
        }
        if (user->m_data->m_bIsInCombatMode != 0) {
            socket->Send("cd", 0x71, 0);
            socket->SendSystemMessage(SystemMessageId::CANNOT_RESTART_DURING_COMBAT);
            return false;
        }

        user->m_bRestartState = true;
        user->GetController()->DoNothing(true);
        user->StopMove();

        const int accountId = static_cast<int>(socket->m_nAccountID);
        if (accountId > 0) {
            g_UserSocketMapEnterChar.DeleteObject(accountId, const_cast<char*>("UserSocket.cpp"), 0x12de);
        }

        if (user->m_data->m_nWorldId == g_nWorldId) {
            if (g_bDualBillingSystem) {
                const int characterId = static_cast<int>(user->GetDBID());
                const int level = user->m_data->m_nLevel;
                if (socket->m_nMaxCharId == characterId || socket->m_nMaxLevel < level) {
                    socket->m_nMaxCharId = characterId;
                    socket->m_nMaxLevel = level;
                }
            }

            user->SendLogout();
            if (g_bDualBillingSystem && runtime::raw_atomic::ExchangeI32(&socket->m_ChargeStart, 0) == 1) {
                authd.AuthChargeStop(accountId);
                CLogFormatter formatter;
                formatter.WriteLog1(0x1fb1, user, nullptr, nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
            }
            socket->UnBindUser();
            // Current L2Server 0x00c243e1 loads R9D=1 before the virtual
            // Send("cd", 0x71, ...) call. Its rejection paths at 0x00c241cd
            // and 0x00c2442b instead clear R9D and therefore keep zero.
            // NA P152 UGameEngine::OnRestartResponse@0x203cd850 observes the
            // same success convention and enters the lobby only for nonzero.
            socket->Send("cd", 0x71, 1);
            socket->m_nCloseReason = SCR_RESTART;
            db.RequestLoadChar(user->GetAccountID(), socket);
        } else if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
            WOlympiadService::inst().Restart(CSPointer<User>(user, ObjectRefType::FunctionChain));
        } else {
            RaidServerManager::inst().RestartOnRaidServer(*user);
        }
    }
    return false;
}

bool CUserSocket::ValidateLocationPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c0baa4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return socket->m_nCloseReason != SCR_RESTART && socket->m_nCloseReason != SCR_RETURN_TO_ORIGIN;
    }

    int x = 0;
    int y = 0;
    int z = 0;
    int heading = 0;
    int vehicleId = 0;
    char validationType = 0;
    ::Disassemble(packet, "dddddc", &x, &y, &z, &heading, &vehicleId, &validationType);
    return user->OnValidateLocationPacket(x, y, z, heading, vehicleId, validationType);
}

bool SekCostume(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0bbd8.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    socket->Send("cd", 0x1f, 0);
    socket->SendSystemMessage(u"SYS:", u"Use //home");
    return false;
}

bool StartRotatingPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0bca8.
    L2_GUARD_FUNCTION();

    int angle = 0;
    int side = 0;
    ::Disassemble(packet, "dd", &angle, &side);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->OnStartRotating(angle, side);
    }
    return user == nullptr;
}

bool FinishRotatingPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0bd80.
    L2_GUARD_FUNCTION();

    int angle = 0;
    ::Disassemble(packet, "d", &angle);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->OnFinishRotating(angle);
    }
    return user == nullptr;
}

bool RestartPointPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0cd84.
    L2_GUARD_FUNCTION();

    int restartPoint = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &restartPoint);
    int restartItemId = 0;
    if (restartPoint == RESTART_BY_NPC) {
        ::Disassemble(cursor, "d", &restartItemId);
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnRestartPointPacket(static_cast<RestartPoint>(restartPoint), restartItemId);
}

bool PrivateStoreListSet(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c13eb8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (!user->IsItemUsable(0)) {
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_data->m_cIsGuilty > 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_COMBAT);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_COMBAT);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_STORE_OR_WORKSHOP_IN_CEREMONY_OF_CHAOS);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_DuelUserInfo.IsInPartyDuel()) {
        return false;
    }

    const int storeType = user->GetPrivateStoreType();
    if ((storeType != PST_MANAGE && storeType != PST_WHOLESALE_MANAGE) || user->IsRiding()) {
        return false;
    }
    if (!AreaBasedRule::PrivateStore(user) || CAreaDB::inst().IsFlyMoveZone(user->GetPos(), user->GetInZoneID())) {
        user->QuitPrivateStore(false);
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_HERE);
        return false;
    }

    if (user->CheckAndResetPendingUseETCItem(PUIT_ENCHANT)) {
        user->GetSocket()->Send(
            "cddQdddd", 0x87,
            // 2,
            IER_CANCELED, 0, std::int64_t{0}, 0, 0, 0, 0
        );
    }
    if (user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_DISGUISE_SEAL_OR_UNSEAL);
        return false;
    }
    user->SetPrivateStoreList(packet);
    return false;
}

bool PrivateStoreManageCancel(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d110.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->CancelPrivateStore(PST_MANAGE);
    }
    return user == nullptr;
}

bool PrivateStoreBuyListSend(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1f884.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (!user->IsItemUsable(0)) {
        return false;
    }

    int shopServerId = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "d", &shopServerId);
    CSPointer<User> shop = g_UserMap.FindObject(shopServerId);
    if (!shop || !shop->IsAlive() || !user->IsTalkableDistance(shop.get())) {
        return false;
    }
    if (user->GetDBID() == shop->GetDBID()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        // Log.Add(
        //     LOG_ERROR,
        //     L"try hack? user dbid[%d], shop dbid[%d], "
        //     L"disconnect both",
        //     user->GetDBID(),
        //     shop->GetDBID())
        // Log.Add(
        //     LOG_WARN,
        //     "[private store] Purchase rejected on socket %u: buyer and seller matched the same character DB ID (observed
        //     buyer %u, seller %u). Requesting closure of both connections.", socket->m_nId, user->GetDBID(),
        //     shop->GetDBID())
        L2_LOG_WARN(
            Log,
            "[private store] Purchase rejected on socket %u: buyer and seller matched the same character DB ID (observed "
            "buyer %u, seller %u); requesting closure of both connections.",
            socket->m_nId, user->GetDBID(), shop->GetDBID()
        );
        shop->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return true;
    }
    const int storeType = shop->GetPrivateStoreType();
    if (storeType == PST_OPEN || storeType == PST_WHOLESALE_OPEN) {
        shop->PrivateStoreBuy(cursor, user);
    }
    return false;
}

bool PrivateStoreQuit(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d1d0.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (user->IsShopManaging()) {
        user->QuitPrivateStore(false);
    }
    return false;
}

bool PrivateStoreSetMsg(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d2b4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    const int storeType = user->GetPrivateStoreType();
    if ((storeType == PST_OPEN || storeType == PST_MANAGE) && !user->IsRiding() && !user->IsNowTrade()) {
        if (user->GetTrade() != nullptr) {
            user->TradeCancel(false);
        }
        if (user->IsItemUsable(0)) {
            user->SetPrivateStoreMsg(packet);
        }
    }
    return false;
}

bool PrivateStoreBuyListSet(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c14974.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (!user->IsItemUsable(0)) {
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_data->m_cIsGuilty > 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_COMBAT);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_COMBAT);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_STORE_OR_WORKSHOP_IN_CEREMONY_OF_CHAOS);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->m_DuelUserInfo.IsInPartyDuel() || user->GetPrivateStoreType() != PST_BUY_MANAGE || user->IsRiding()) {
        return false;
    }
    if (!AreaBasedRule::PrivateStore(user) || CAreaDB::inst().IsFlyMoveZone(user->GetPos(), user->GetInZoneID())) {
        user->QuitPrivateStore(false);
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_HERE);
        return false;
    }
    user->SetPrivateStoreBuyList(packet);
    return false;
}

bool PrivateStoreBuyManageCancel(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d9c0.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->CancelPrivateStore(PST_BUY_MANAGE);
    }
    return user == nullptr;
}

bool PrivateStoreBuyQuit(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0da80.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsShopManaging()) {
        if (user->GetTrade() != nullptr) {
            user->TradeCancel(false);
        }
        user->QuitPrivateStore(false);
    }
    return false;
}

bool PrivateStoreBuySetMSG(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c14c14.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (!user->IsItemUsable(0)) {
        return false;
    }

    const int storeType = user->GetPrivateStoreType();
    if ((storeType != PST_BUY_OPEN && storeType != PST_BUY_MANAGE) || user->IsRiding() ||
        user->m_DuelUserInfo.IsInPartyDuel()) {
        return false;
    }
    if (!AreaBasedRule::PrivateStore(user) || CAreaDB::inst().IsFlyMoveZone(user->GetPos(), user->GetInZoneID())) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_HERE);
        return false;
    }
    user->SetPrivateStoreBuyMsg(packet);
    return false;
}

bool PrivateStoreBuyBuyListSend(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0db64.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnPrivateStoreBuyBuyListSend(packet);
}

bool RequestRecipeBookOpen(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c24484.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsAlive()) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    const int storeType = user->GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE || storeType == PST_RECIPE_SHOP_OPEN) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_MANAGE_RECIPE_BOOK_DURING_PRIVATE_MANUFACTURE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user) ||
        user->IsTransformed(TransformType::Max) != nullptr) {
        return false;
    }

    int recipeType = 0;
    ::Disassemble(packet, "d", &recipeType);
    user->SendRecipeBookList(recipeType);
    return false;
}

bool RequestRecipeBookItemDelete(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c24634.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsAlive()) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }

    int recipeId = 0;
    ::Disassemble(packet, "d", &recipeId);
    const int storeType = user->GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE || storeType == PST_RECIPE_SHOP_OPEN) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_MANAGE_RECIPE_BOOK_DURING_PRIVATE_MANUFACTURE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user) ||
        user->IsTransformed(TransformType::Max) != nullptr) {
        return false;
    }

    user->DeleteRecipeBook(recipeId);
    user->m_ShortCut.DeleteRecipeShortCut(recipeId);
    return false;
}

bool RequestRecipeBookItemInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c247f8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsAlive()) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }

    int recipeId = 0;
    ::Disassemble(packet, "d", &recipeId);
    const int storeType = user->GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE || storeType == PST_RECIPE_SHOP_OPEN) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_MANAGE_RECIPE_BOOK_DURING_PRIVATE_MANUFACTURE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user) ||
        user->IsTransformed(TransformType::Max) != nullptr) {
        return false;
    }

    user->SendRecipeInfo(recipeId, -1);
    return false;
}

bool RequestRecipeBookItemMake(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c27e08.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int recipeId = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &recipeId);
    if (!user->IsAlive()) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    const int storeType = user->GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE || storeType == PST_RECIPE_SHOP_OPEN) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_MANAGE_RECIPE_BOOK_DURING_PRIVATE_MANUFACTURE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user) ||
        user->IsTransformed(TransformType::Max) != nullptr) {
        return false;
    }

    std::vector<RecipeOfferingItemStruct*> offeringItems;
    CRecipeInfo* const recipe = user->GetRecipeInfo(recipeId);
    if (recipe != nullptr) {
        if (recipe->m_bUseOffering) {
            int offeringCount = 0;
            cursor = ::Disassemble(cursor, "d", &offeringCount);
            for (int index = 0; index < offeringCount; ++index) {
                int itemServerId = 0;
                std::int64_t count = 0;
                cursor = ::Disassemble(cursor, "dQ", &itemServerId, &count);
                offeringItems.push_back(new RecipeOfferingItemStruct(itemServerId, count));
            }
        }

        const int createLevel =
            recipe->m_nCommonRecipe == 0 ? user->GetDwarfCreateItemLevel() : user->GetCommonCreateItemLevel();
        if (createLevel < recipe->m_nLevel) {
            user->SendSystemMessage(SystemMessageId::CREATE_ITEM_SKILL_MISSING_OR_LEVEL_TOO_LOW);
        } else {
            recipe->MakeProduct(user, offeringItems, false);
        }
    }

    for (RecipeOfferingItemStruct* const item : offeringItems) {
        delete item;
    }
    return false;
}

bool RequestRecipeShopSetMsg(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c249b0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsAlive() || user->GetPrivateStoreType() != PST_RECIPE_SHOP_MANAGE) {
        return false;
    }
    if (user->IsRiding()) {
        if (user->GetPrivateStoreType() == PST_RECIPE_SHOP_MANAGE) {
            user->QuitRecipeStore();
        }
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user)) {
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (!AreaBasedRule::PrivateRecipeShop(user) || CAreaDB::inst().IsFlyMoveZone(user->GetPos(), user->GetInZoneID())) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_WORKSHOP_HERE);
        user->QuitRecipeStore();
        return false;
    }

    user->SetRecipeStoreMsg(packet);
    return false;
}

bool RequestRecipeShopSetList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c24c2c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->IsAlive() || user->GetPrivateStoreType() != PST_RECIPE_SHOP_MANAGE) {
        return false;
    }
    if (user->IsRiding()) {
        if (user->GetPrivateStoreType() == PST_RECIPE_SHOP_MANAGE) {
            user->QuitRecipeStore();
        }
        return false;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (user->m_data->m_cIsGuilty > 0 || user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_COMBAT);
        user->QuitRecipeStore();
        return false;
    }
    if (user->m_PledgeGameUser.IsPlayer()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_STORE_OR_WORKSHOP_IN_CEREMONY_OF_CHAOS);
        user->QuitPrivateStore(false);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user)) {
        return false;
    }
    if (!AreaBasedRule::PrivateRecipeShop(user) || CAreaDB::inst().IsFlyMoveZone(user->GetPos(), user->GetInZoneID())) {
        user->SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_WORKSHOP_HERE);
        user->QuitRecipeStore();
        return false;
    }

    user->SetRecipeStoreList(packet);
    return false;
}

bool RequestRecipeShopQuit(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e338.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    const int storeType = user->GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE || storeType == PST_RECIPE_SHOP_OPEN) {
        user->QuitRecipeStore();
    } else {
        // Noise: A close request with no managed/open recipe shop performs no recipe-shop operation.
        // Log.Add(
        //     LOG_ERROR,
        //     L"Not recipe shop manage try "
        //     L"RequestRecipeShopQuit")
    }
    return false;
}

bool RequestRecipeShopCancel(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e444.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        user->CancelRecipeStore();
    }
    return user == nullptr;
}

bool RequestRecipeShopItemInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c24f2c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user)) {
        return false;
    }

    int shopServerId = 0;
    int recipeId = 0;
    ::Disassemble(packet, "dd", &shopServerId, &recipeId);
    CSPointer<User> shop = g_UserMap.FindObject(shopServerId);
    if (shop && shop->IsAlive() && user->IsAlive() && user->IsTalkableDistance(shop.get()) &&
        shop->GetPrivateStoreType() == PST_RECIPE_SHOP_OPEN) {
        shop->RecipeStoreShowInfo(user, recipeId, -1);
    }
    return false;
}

bool RequestRecipeShopItemMake(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2524c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user)) {
        return false;
    }

    int shopServerId = 0;
    const std::uint8_t* const cursor = ::Disassemble(packet, "d", &shopServerId);
    CSPointer<User> shop = g_UserMap.FindObject(shopServerId);
    if (shop && shop->IsAlive() && user->IsAlive() && user->IsTalkableDistance(shop.get()) &&
        shop->GetPrivateStoreType() == PST_RECIPE_SHOP_OPEN) {
        shop->RecipeStoreBuy(cursor, user);
    }
    return false;
}

bool RequestMultiSellChoose(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2b420.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int listId = 0;
    int entryId = 0;
    std::int64_t amount = 0;
    int enchanted = 0;
    ItemOptionKey optionKey{};
    std::map<int, EnsoulOptionData*> ensoulOptions[3];
    const std::uint8_t* cursor = ::Disassemble(
        packet, "ddQhdd", &listId, &entryId, &amount, &enchanted, &optionKey.variationOption1, &optionKey.variationOption2
    );
    BaseAttribute attribute;
    cursor = CItem::DisassembleAttribute(cursor, attribute);
    CItem::DisassembleEnsoul(cursor, ensoulOptions);

    CMultiSellList* const list = static_cast<std::uint32_t>(listId) < UINT32_C(0x80000000)
        ? g_MultiSellDB.find(listId)
        : CEventMakerManager::inst().GetMultisellListPointer(listId);
    if (list == nullptr) {
        return false;
    }
    if (!list->IsWithoutNPC() &&
        !TalkChannelCache::inst().mChannels[1].IsValidChannel(user->Id(), user->m_nMultisellNpcSid, 0xfa, 0)) {
        return false;
    }
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }
    user->MultiSellChoose(listId, entryId, amount, enchanted, optionKey, attribute, ensoulOptions);
    return false;
}

bool CUserSocket::RequestNetPing(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c18fbc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    runtime::raw_atomic::ExchangeI32(&socket->m_nRecvPingCount, 0);

    std::int32_t clientTick = 0;
    std::int32_t clientValue1 = 0;
    std::int32_t clientValue2 = 0;
    ::Disassemble(packet, "ddd", &clientTick, &clientValue1, &clientValue2);

    User* const user = socket->GetUser();
    const std::uint32_t currentTick = runtime::GetTickCount();
    if (user == nullptr) {
        return false;
    }

    Agent* const agent = user->m_pAgent;
    if (agent == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"m_pAgent is not GameGuardAgent")
        // Log.Add(
        //     LOG_WARN,
        //     "[ping] Statistics not recorded on socket %u: the GameGuard agent is unavailable. The received-ping counter
        //     was reset.", socket->m_nId)
        L2_LOG_WARN(
            Log,
            "[ping] Statistics not recorded on socket %u: the GameGuard agent is unavailable; the received-ping counter was "
            "reset.",
            socket->m_nId
        );
        return false;
    }

    const auto* const gameGuardAgent = static_cast<const GameGuardAgent*>(agent);
    const std::u16string ipAddress = socket->GetIPAddressString();
    const std::u16string pcCafeCode = Utf16Text(socket->m_swPCCafeCode.data()).String();
    const std::uint32_t latency = currentTick - static_cast<std::uint32_t>(clientTick);

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x36e, user, nullptr, std::u16string(ipAddress.c_str()), pcCafeCode.c_str(), nullptr,
        static_cast<std::int64_t>(latency), static_cast<std::int64_t>(clientValue1), static_cast<std::int64_t>(clientValue2),
        static_cast<std::int64_t>(gameGuardAgent->m_lastRequestTick),
        static_cast<std::int64_t>(gameGuardAgent->m_lastResponseTick), 0, 0, 0, 0, 0, 0
    );
    return false;
}

bool RequestRemainTime(CSocket* baseSocket, const std::uint8_t*) {
    // Current L2Server 0x00c191a8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (g_nPlayTimeLimit < 1) {
        socket->SendSystemMessage(SystemMessageId::COMMAND_REQUIRES_RELAX_SERVER);
        return false;
    }

    const std::int32_t remaining = runtime::SubtractInt32Wrapping(g_nPlayTimeLimit, socket->GetSocketPlayTime());
    socket->Send(
        "chccdcd", 0x62, SystemMessageId::WEEKLY_PLAYTIME_HOURS_AND_MINUTES_REMAINING, 2, 1, remaining / 3600, 1,
        (remaining % 3600) / 60
    );

    if (socket->m_bForceLogin) {
        const std::int32_t builderLevel = user->m_data->m_nBuilder;
        if (builderLevel > 0 && builderLevel < 9) {
            socket->SendSystemMessage(u"%s", u"You have no time limit!!!");
        }
    }
    return false;
}

bool ReplyGameGuardInfo(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c25800.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        const std::uint8_t* cursor = packet;
        Agent* const agent = user->m_AgentMap[GAME_GUARD];
        if (agent != nullptr) {
            static_cast<void>(agent->ResponseSecond(&cursor));
        }
    }
    return false;
}

bool RequestMakeMacro(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c19334.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t macroId = 0;
    std::array<char16_t, 13> macroName{};
    std::array<char16_t, 33> macroDescription{};
    std::array<char16_t, 5> macroAcronym{};
    std::int32_t macroIcon = 0;
    std::uint32_t actionCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet, "dSSSdc", &macroId, 0x1a, macroName.data(), 0x42, macroDescription.data(), 10, macroAcronym.data(),
        &macroIcon, reinterpret_cast<char*>(&actionCount)
    );

    if (actionCount < 25U) {
        CPacketHelperEx<20480> macroPacket;
        macroPacket.Assemble(
            "dSSSdc", macroId, macroName.data(), macroDescription.data(), macroAcronym.data(), macroIcon,
            static_cast<int>(actionCount)
        );

        for (std::uint32_t index = 0; index < actionCount; ++index) {
            std::uint32_t actionType = 0;
            std::uint32_t actionParam = 0;
            std::int32_t actionId = 0;
            std::uint32_t actionUse = 0;
            std::array<char16_t, 81> actionText{};
            cursor = ::Disassemble(
                cursor, "ccdcS", reinterpret_cast<char*>(&actionType), reinterpret_cast<char*>(&actionParam), &actionId,
                reinterpret_cast<char*>(&actionUse), 0xa2, actionText.data()
            );
            macroPacket.Assemble(
                "ccdcS", static_cast<int>(actionType), static_cast<int>(actionParam), actionId, static_cast<int>(actionUse),
                actionText.data()
            );
        }

        const auto* const macroData = reinterpret_cast<const unsigned char*>(macroPacket.GetPacket());
        if (macroId < 1) {
            db.RequestCreateMacro(user, macroPacket.GetLength(), macroData);
        } else {
            db.RequestUpdateMacro(user, macroPacket.GetLength(), macroData);
        }
    }
    return false;
}

bool RequestDeleteMacro(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e874.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::int32_t macroId = 0;
    ::Disassemble(packet, "d", &macroId);
    db.RequestDeleteMacro(user, macroId);
    return false;
}

bool RequestRecipeShopSellList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c25540.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (user->IsInDuel() || CPVPMatchSystem::inst().IsPVPPlayer(user)) {
        return false;
    }
    if (user->GetTrade() != nullptr) {
        user->TradeCancel(false);
    }

    int shopServerId = 0;
    ::Disassemble(packet, "d", &shopServerId);
    CSPointer<User> shop = g_UserMap.FindObject(shopServerId);
    if (shop && shop->IsAlive() && user->IsTalkableDistance(shop.get()) &&
        shop->GetPrivateStoreType() == PST_RECIPE_SHOP_OPEN) {
        shop->RecipeStoreListReq(user->GetServerId());
    }
    return false;
}

bool UnequipItemPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c098b8.
    L2_GUARD_FUNCTION();

    std::int64_t slot = 0;
    ::Disassemble(packet, "Q", &slot);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnUnequipItemPacket(slot);
}

bool UseItemPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c09b3c.
    L2_GUARD_FUNCTION();

    std::uint32_t itemServerId = 0;
    int force = 0;
    ::Disassemble(packet, "dd", &itemServerId, &force);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user != nullptr && user->OnUseItemPacket(itemServerId, force);
}

bool CUserSocket::DropItemPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c09c1c.
    L2_GUARD_FUNCTION();

    std::uint32_t itemServerId = 0;
    std::int64_t amount = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    ::Disassemble(packet, "dQddd", &itemServerId, &amount, &x, &y, &z);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    if (::g_nCountryCode == 3) {
        HTML_PACKET_QUEUE& queue = socket->kHtmlPacketQueue;
        if (g_nPacketRepeatLimit > 1) {
            queue.dwReceiveTime[static_cast<std::size_t>(queue.pushpos)] = runtime::GetTickCount();
            ++queue.pushpos;
            while (queue.pushpos >= g_nPacketRepeatLimit) {
                queue.pushpos -= g_nPacketRepeatLimit;
            }
        }

        auto interval = [&queue]() -> int {
            if (g_nPacketRepeatLimit < 2) {
                return 1000000;
            }
            int previous = queue.pushpos - 1;
            while (previous < 0) {
                previous += g_nPacketRepeatLimit;
            }
            const std::uint32_t oldest = queue.dwReceiveTime[static_cast<std::size_t>(queue.pushpos)];
            if (oldest == std::numeric_limits<std::uint32_t>::max()) {
                return 1000000;
            }
            const std::uint32_t elapsed = queue.dwReceiveTime[static_cast<std::size_t>(previous)] - oldest;
            std::int32_t signedElapsed = 0;
            std::memcpy(&signedElapsed, &elapsed, sizeof(signedElapsed));
            return signedElapsed;
        };

        const int receivedInterval = interval();
        if (receivedInterval < g_nPacketRepeatLimitTime) {
            socket->m_nCloseReason = SCR_NORMAL;
            socket->Close();
            const std::wstring accountName =
                Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
            // Log.Add(
            //     LOG_WARN,
            //     L"[User:%d, %s][recived: %d] Too Many DropItemPacket",
            //     user->GetAccountID(),
            //     accountName.c_str(),
            //     interval())
            // Log.Add(
            //     LOG_WARN,
            //     L"[items] Drop request rejected for account %u (%s): observed request-window interval %d ms failed the
            //     repeat-rate check. Socket closure was requested.", user->GetAccountID(), accountName.c_str(),
            //     receivedInterval)
            L2_LOG_WARN(
                Log,
                u"[items] Drop request rejected for account %u (%s): observed request-window interval %d ms failed the "
                u"repeat-rate check; socket closure was requested.",
                user->GetAccountID(), accountName.c_str(), receivedInterval
            );
            return false;
        }
    }

    return user->OnDropItemPacket(itemServerId, amount, x, y, z);
}

bool CUserSocket::TradeRequestPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1b960.
    L2_GUARD_FUNCTION();

    int targetServerId = 0;
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    ::Disassemble(packet, "d", &targetServerId);
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    CSPointer<User> target = g_UserMap.FindObject(targetServerId);
    if (target && User::IsInFifthClassGroup(static_cast<ClassType>(target->GetClass()))) {
        user->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, target->GetName()
        );
        return false;
    }
    return user->OnTradeRequestPacket(targetServerId);
}

bool CUserSocket::TradeStartPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c09e78.
    L2_GUARD_FUNCTION();

    int synchronize = 0;
    ::Disassemble(packet, "d", &synchronize);
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    return user == nullptr ? true : user->OnTradeStartPacket(synchronize);
}

bool CUserSocket::TradeAddItemsPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c09f50.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        static_cast<void>(user->TradeItemAdd(packet));
    }
    return user == nullptr;
}

bool CUserSocket::TradeDonePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a010.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    std::uint32_t result = 0;
    ::Disassemble(packet, "d", &result);
    return user->OnTradeDonePacket(result);
}

bool BuyPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a0f0.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        socket->Send("cd", 0x1f, 0);
        return true;
    }
    return user->OnBuyPacket(packet);
}

bool SellPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0a1cc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        socket->Send("cd", 0x1f, 0);
        return true;
    }
    return user->OnSellPacket(packet);
}

bool RequestBuySeed(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c2bdf4, original owner UserSocket.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (!TalkChannelCache::inst().mChannels[3].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request buy seed "
        //     L"with invalid talk channel.",
        //     kUserSocketSource,
        //     0x25df,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[manor] Seed purchase ignored for character %s on socket %u: the NPC interaction is no longer valid. No
        //     purchase was requested.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[manor] Seed purchase ignored for character %s on socket %u: the NPC interaction is no longer valid. No
        //     purchase was requested.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[manor] Seed purchase ignored for character %s on socket %u: the NPC interaction is no longer valid; no "
            u"purchase was requested.",
            user->GetName(), socket->m_nId
        );
        return false;
    }

    int manorId = 0;
    int itemCount = 0;
    const std::uint8_t* const itemList = ::Disassemble(packet, "dd", &manorId, &itemCount);
    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor != nullptr) {
        static_cast<void>(manor->SellSeed(user, itemCount, itemList));
    }
    return false;
}

bool BuyPreviewPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1aaa8, original owner UserSocket.cpp.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        socket->Send("cd", 0x1f, 0);
        return true;
    }

    int npcServerId = 0;
    int listId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "ddd", &npcServerId, &listId, &itemCount);

    // The current wrapper calls this for its timeout/cancel side effects and
    // ignores the returned trade state.
    static_cast<void>(user->IsNowTrade());
    enum class PreviewStatus { Ready, Rejected, ActionFailed, AlreadyTrading, PrivateStore };
    CSPointer<CCreature> merchant;
    BuySellListCache::List* list = nullptr;
    const PreviewStatus previewStatus = [&] {
        L2_GUARD(*user->m_lock);

        CEconomy* const economy = user->m_pEconomy;
        if (economy == nullptr || typeid(*economy) != typeid(CBuy)) {
            return PreviewStatus::ActionFailed;
        }

        auto* const buy = static_cast<CBuy*>(economy);
        if (user->GetTrade() != nullptr) {
            return PreviewStatus::AlreadyTrading;
        }
        if (user->GetPrivateStoreType() != 0) {
            return PreviewStatus::PrivateStore;
        }
        if (!user->IsItemUsable(0)) {
            return PreviewStatus::Rejected;
        }

        merchant = CCreatureFactory::Inst().FindObjectSP(npcServerId);
        if (!merchant) {
            // Log.Add(LOG_ERROR, L"cannot find merchant npc.")
            // Log.Add(
            //     LOG_WARN,
            //     "[preview] Request stopped on socket %u: merchant NPC runtime ID %u was not found. No item preview was
            //     sent.", socket->m_nId, static_cast<std::uint32_t>(npcServerId))
            L2_LOG_WARN(
                Log,
                "[preview] Request stopped on socket %u: merchant NPC runtime ID %u was not found; no item preview was "
                "sent.",
                socket->m_nId, static_cast<std::uint32_t>(npcServerId)
            );
            return PreviewStatus::Rejected;
        }
        if (!user->IsTalkableDistance(merchant.get())) {
            return PreviewStatus::Rejected;
        }

        // This is the literal current-machine comparison: the CBuy pointer is
        // divided by its complete object extent. The real list ID is fetched
        // separately below when constructing the cache key.
        const std::intptr_t currentListToken =
            reinterpret_cast<std::intptr_t>(buy) / static_cast<std::intptr_t>(sizeof(CBuy));
        if (static_cast<std::int32_t>(currentListToken) != listId) {
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
            return PreviewStatus::Rejected;
        }

        const BuySellListCache::Key key{buy->GetResidenceID(), static_cast<int>(user->GetInZoneID()), buy->GetListID()};
        list = BuySellListCache::Inst().GetList(key);
        if (static_cast<std::uint32_t>(itemCount) - 1U >= 200U || list == nullptr) {
            user->ReleaseEconomy();
            return PreviewStatus::ActionFailed;
        }

        return PreviewStatus::Ready;
    }();
    if (previewStatus != PreviewStatus::Ready) {
        if (previewStatus == PreviewStatus::AlreadyTrading) {
            user->SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        } else if (previewStatus == PreviewStatus::PrivateStore) {
            user->SendSystemMessage(SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        }
        if (previewStatus != PreviewStatus::Rejected) {
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
        }
        return false;
    }
    std::array<int, 200> previewItems;
    int validItemCount;
    {
        L2_READ_GUARD(user->m_inv);

        std::memset(previewItems.data(), 0, previewItems.size() * sizeof(previewItems[0]));
        validItemCount = 0;
        for (int requestIndex = 0; requestIndex < itemCount; ++requestIndex) {
            int requestedItemId = 0;
            cursor = ::Disassemble(cursor, "d", &requestedItemId);
            for (const BuySell& entry : *list) {
                if (entry.m_nItemClassId != requestedItemId) {
                    continue;
                }

                CObject* const object = g_ObjectDB.GetObjectW(requestedItemId, false);
                if (object == nullptr || !object->IsItem()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"invalid item(classid : %d) at [%S], [%d]",
                    //     requestedItemId,
                    //     "UserSocket.cpp",
                    //     0x2751)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "[preview] Skipping item class %d from the shop list on socket %u: %s. Preview will continue with
                    //     valid entries.", requestedItemId, socket->m_nId, object == nullptr ? "the object definition is
                    //     missing" : "the definition is not an item")
                    L2_LOG_ERROR(
                        Log,
                        "[preview] Skipping item class %d from the shop list on socket %u: %s; preview will continue with "
                        "valid entries.",
                        requestedItemId, socket->m_nId,
                        object == nullptr ? "the object definition is missing" : "the definition is not an item"
                    );
                } else {
                    previewItems[validItemCount] = requestedItemId;
                    ++validItemCount;
                }
                break;
            }
        }
    }
    user->ShowPreview(validItemCount, previewItems.data());
    return false;
}

bool CUserSocket::ItemCrystallizePacket(CSocket* const baseSocket, const unsigned char*) {
    // Current L2Server 0x00c0ce74. The packet body is unused.
    // the target item is the already-published PUIT_CRYSTAL entry.
    L2_GUARD_FUNCTION();
    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->GetWorldId() != g_nWorldId) {
        return false;
    }

    CItem* const pending = user->GetPendingUseETCItem(PUIT_CRYSTAL);
    if (pending == nullptr) {
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        user->SendSystemMessage(SystemMessageId::CANNOT_CRYSTALLIZE_NO_REGISTERED_ITEMS);
        return false;
    }
    if (CItemRestrict::Inst().CheckItemRestrict(pending->m_data->m_nClassID)) {
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        return false;
    }

    CItem* target = user->m_inv.GetByServerID(pending->m_data->m_nId);
    CPet* pet = nullptr;
    if (target == nullptr) {
        pet = user->GetPet();
        if (pet == nullptr) {
            user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
            return false;
        }
        // Current 0x00c0cff8 uses the same runtime item ID for the pet-inventory fallback.
        target = pet->m_inv.GetByServerID(pending->m_data->m_nId);
        if (target == nullptr) {
            user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
            return false;
        }
    }

    if (!user->CheckCrystalItem(target, 1)) {
        user->CheckAndResetPendingUseETCItem(PUIT_CRYSTAL);
        return false;
    }

    const std::int64_t existingCrystals =
        user->m_inv.GetItemAmount(CrystalClassByCrystalType[static_cast<int>(target->m_const->GetCrystalType())], 0, true);
    if (target->GetCrystalizeCount(1) + existingCrystals >= 1000000000000LL) {
        user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
        user->SendSystemMessage(SystemMessageId::CRYSTALLIZATION_QUANTITY_LIMIT_REACHED);
        return false;
    }

    if (user->m_lWaitVariation == 1) {
        user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
        user->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        return false;
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 1);
    target->Crystallize(user, pet);
    return false;
}

NullUserSocket* NullUserSocket::GetNullUserSocket() {
    L2_GUARD_FUNCTION();
    return &Singleton<NullUserSocket>::inst();
}

void NullUserSocket::InitializeNullUserSocket() {
    L2_GUARD_FUNCTION();
    static_cast<void>(Singleton<NullUserSocket>::inst());
}

MemoryObject* NullUserSocket::AddRef(const char*, int, ObjectRefType) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid addref called!")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Reference increment requested on NullUserSocket, its reference count is unchanged.")
    L2_LOG_ERROR(Log, "[socket] Reference increment requested on NullUserSocket; its reference count is unchanged.");
    runtime::process::ReportCallStack(2, false);
    return &Singleton<NullUserSocket>::inst();
}

std::int32_t NullUserSocket::Release(const char*, int, ObjectRefType, bool) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid release called!")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Reference release requested on NullUserSocket, returning its unchanged reference count.")
    L2_LOG_ERROR(Log, "[socket] Reference release requested on NullUserSocket; returning its unchanged reference count.");
    runtime::process::ReportCallStack(2, false);
    return Singleton<NullUserSocket>::inst().RefCount();
}

void NullUserSocket::Send(const char*, ...) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, L"[NullUserSocket] send called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Formatted send requested on NullUserSocket, no packet was sent.")
    L2_LOG_ERROR(Log, "[socket] Formatted send requested on NullUserSocket; no packet was sent.");
    runtime::process::ReportCallStack(2, false);
}

void NullUserSocket::Send(S_PROTOCOL, network::IPacket&) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, L"[NullUserSocket] send called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Basic packet send requested on NullUserSocket, no packet was sent.")
    L2_LOG_ERROR(Log, "[socket] Basic packet send requested on NullUserSocket; no packet was sent.");
    runtime::process::ReportCallStack(2, false);
}

void NullUserSocket::Send(S_PROTOCOL, S_EX_PROTOCOL, network::IPacket&) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, L"[NullUserSocket] send called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Extended packet send requested on NullUserSocket, no packet was sent.")
    L2_LOG_ERROR(Log, "[socket] Extended packet send requested on NullUserSocket; no packet was sent.");
    runtime::process::ReportCallStack(2, false);
}

void NullUserSocket::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid Timer expired Called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Timer callback reached NullUserSocket, no timer work was performed.")
    L2_LOG_ERROR(Log, "[socket] Timer callback reached NullUserSocket; no timer work was performed.");
}

void NullUserSocket::OnClose() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid OnClose called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Close callback reached NullUserSocket, no connection cleanup was performed.")
    L2_LOG_ERROR(Log, "[socket] Close callback reached NullUserSocket; no connection cleanup was performed.");
}

void NullUserSocket::OnIOCallback(int, std::uint32_t, PortableOverlapped*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid OnIOCallback called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] IO callback reached NullUserSocket, no IO completion was processed.")
    L2_LOG_ERROR(Log, "[socket] IO callback reached NullUserSocket; no IO completion was processed.");
}

void NullUserSocket::OnRead() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NullUserSocket] invalid OnRead called")
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket] Read callback reached NullUserSocket, no input was processed.")
    L2_LOG_ERROR(Log, "[socket] Read callback reached NullUserSocket; no input was processed.");
}

MemoryObject* CUserSocket::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1);
    helper.m_pAddCaller = callerFile;
    helper.m_nAddLine = callerLine;
    return this;
}

std::int32_t CUserSocket::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);

    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile ? callerFile : "",
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[socket references] Socket %u has negative %s reference count %d after release at %S:%d. Last add at %S:%d,
        //     last release at %S:%d.", m_nId, ObjectRefTypeName(type), subtypeRemaining, callerFile ? callerFile :
        //     "unknown", callerLine, helper.m_pAddCaller ? helper.m_pAddCaller : "unknown", helper.m_nAddLine,
        //     helper.m_pReleaseCaller ? helper.m_pReleaseCaller : "unknown", helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            u"[socket references] Socket %u has negative %s reference count %d after release at %S:%d; last add at %S:%d, "
            u"last release at %S:%d.",
            m_nId, ObjectRefTypeName(type), subtypeRemaining, callerFile ? callerFile : "unknown", callerLine,
            helper.m_pAddCaller ? helper.m_pAddCaller : "unknown", helper.m_nAddLine,
            helper.m_pReleaseCaller ? helper.m_pReleaseCaller : "unknown", helper.m_nReleaseLine
        );
        // Noise: Last add/release locations are included in the preceding negative-reference diagnostic.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     subtypeRemaining,
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        runtime::process::ReportCallStack(2, false);
        ReportOrt("UserSocket.cpp", 0x2c38);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CUserSocket();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CUserSocket", callerFile, callerLine);
            ReportOrt("UserSocket.cpp", 0x2c38);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CUserSocket));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CUserSocket", MemoryObjectID(), callerFile, callerLine);
        ReportOrt("UserSocket.cpp", 0x2c38);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CUserSocket::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();

    static thread_local int totalCall = 0;
    const int threadId = CThreadManager::GetThreadID();
    int sequence = 1;
    ++totalCall;
    if (callerFile == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin "
        //     "---------------------------",
        //     threadId,
        //     totalCall,
        //     sequence)
        // Log.Add(
        //     LOG_ERROR,
        //     "[socket references] Begin report %d, entry %d, for socket %u (diagnostic thread %d).",
        //     totalCall, sequence, m_nId, threadId)
        L2_LOG_ERROR(
            Log, "[socket references] Begin report %d, entry %d, for socket %u (diagnostic thread %d).", totalCall, sequence,
            m_nId, threadId
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     L"file[%S], line[%d] -------------",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // Log.Add(
        //     LOG_ERROR,
        //     "[socket references] Begin report %d, entry %d, for socket %u (diagnostic thread %d), requested at %s:%d.",
        //     totalCall, sequence, m_nId, threadId, callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "[socket references] Begin report %d, entry %d, for socket %u (diagnostic thread %d), requested at %s:%d.",
            totalCall, sequence, m_nId, threadId, callerFile, callerLine
        );
    }

    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        if (helper.m_pAddCaller == nullptr || helper.m_pReleaseCaller == nullptr) {
            continue;
        }
        const std::int32_t referenceCount = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (referenceCount == 0) {
            continue;
        }

        ++sequence;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     referenceCount,
        //     ObjectRefTypeName(
        //         static_cast<ObjectRefType>(index)),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[socket references] Report %d, entry %d, socket %u (diagnostic thread %d): %s count %d. Last add at %S:%d,
        //     last release at %S:%d.", totalCall, sequence, m_nId, threadId,
        //     ObjectRefTypeName(static_cast<ObjectRefType>(index)), referenceCount,
        //     helper.m_pAddCaller, helper.m_nAddLine,
        //     helper.m_pReleaseCaller, helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            u"[socket references] Report %d, entry %d, socket %u (diagnostic thread %d): %s count %d; last add at %S:%d, "
            u"last release at %S:%d.",
            totalCall, sequence, m_nId, threadId, ObjectRefTypeName(static_cast<ObjectRefType>(index)), referenceCount,
            helper.m_pAddCaller, helper.m_nAddLine, helper.m_pReleaseCaller, helper.m_nReleaseLine
        );
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end "
    //     "-----------------------------",
    //     threadId,
    //     totalCall,
    //     sequence + 1)
    // Log.Add(
    //     LOG_ERROR,
    //     "[socket references] End report %d, entry %d, for socket %u (diagnostic thread %d).",
    //     totalCall, sequence + 1, m_nId, threadId)
    L2_LOG_ERROR(
        Log, "[socket references] End report %d, entry %d, for socket %u (diagnostic thread %d).", totalCall, sequence + 1,
        m_nId, threadId
    );
}

bool CUserSocket::VersionPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c2368c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    std::int32_t clientVersion;
    const unsigned char* cursor = ::Disassemble(packet, "d", &clientVersion);
    constexpr std::int32_t kNorthAmericanProtocolVersion = runtime::client_protocol::kNorthAmericanRevision;

    if (clientVersion == -1) {
        return false;
    }
    if (clientVersion == -2) {
        socket->Send("cd", 0, 1);
        return true;
    }
    if (clientVersion == -3) {
        std::array<wchar_t, 64> memoryMonitor{};
        g_Memory.GetMemoryMon(memoryMonitor.data());

        NpcSpawnInfo* const creationInfo = NpcSpawnInfo::Inst();
        const std::int32_t deleted = creationInfo->Server_Delete;
        const std::int32_t created = creationInfo->Server_Create;
        const std::int32_t createBalance = runtime::SubtractInt32Wrapping(created, deleted);

        NpcSpawnInfo* const worldInfo = NpcSpawnInfo::Inst();
        const std::int32_t leftWorld = worldInfo->Server_LeaveWorld;
        const std::int32_t enteredWorld = worldInfo->Server_EnterWorld;
        const std::int32_t enterWorldBalance = runtime::SubtractInt32Wrapping(enteredWorld, leftWorld);

        static std::int32_t EnterWorldCountPeek = 0;
        const std::int32_t previousPeek = EnterWorldCountPeek;
        EnterWorldCountPeek = enterWorldBalance < previousPeek ? previousPeek : enterWorldBalance;

        const std::int32_t protocolVersion = GetProtocolVersion();
        const NewInspectorUnsignedLong inspectorCrc = g_NewInspectorDB.GetCRC();
        const std::int32_t npcStatus = g_npcserver.GetNpcSocketCount() < 1 ? 2 : 1;
        const wchar_t* const buildTime = CBuildDate::inst().BuildTime();
        const wchar_t* const buildDate = CBuildDate::inst().BuildDate();

        const char queueTicketEnabled = static_cast<char>(g_bQueueTicketEnabled != false);
        const std::int32_t cpuUsage = g_CPUUsage;
        const std::int32_t petitionDisableTime2 = petitionCli.disabletime2;
        const std::int32_t petitionDisableTime1 = petitionCli.disabletime1;
        const std::int32_t guardHistoryType = g_nServerGuardHistoryType;
        const std::int32_t peek = EnterWorldCountPeek;
        const std::int32_t storeUserCount = g_nStoreUserCount;
        const std::int32_t playingUserCount = g_nPlayingUserCount;
        const std::int32_t serverUserCount = g_nServerUserCount;
        const std::int32_t maxServerUserCount = g_nMaxServerUserCount;

        socket->Send(
            "cdddddSdddddSSdddddc", 0x2e, npcStatus, maxServerUserCount, serverUserCount, playingUserCount, storeUserCount,
            target_utf16::FromRecoveredWindowsWide(memoryMonitor.data()).c_str(), createBalance, enterWorldBalance, peek, 0,
            guardHistoryType, target_utf16::FromRecoveredWindowsWide(buildDate).c_str(),
            target_utf16::FromRecoveredWindowsWide(buildTime).c_str(), protocolVersion, inspectorCrc, petitionDisableTime1,
            petitionDisableTime2, cpuUsage, static_cast<int>(queueTicketEnabled)
        );
        return false;
    }
    if (clientVersion == -4) {
        return false;
    }
    if (clientVersion == -5) {
        const ::in_addr address = socket->GetAddress();
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
        std::array<char16_t, 20> addressString{};
        FormatUtf16To(
            addressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
            static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
        );

        CIOObject::SetShortTimeStamp(&socket->m_nLastTimeCheck, 0);
        socket->AddTimer(1000, 0);

        if (!g_bServerStopperEnabled) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"This server doesn't allow ServerStopper by setting, "
            //     L"but someone attempted!! [IP:%s]",
            //     addressString.data())
            // Log.Add(
            //     LOG_WARN,
            //     L"[admin stop] Shutdown request from %s on socket %u ignored: ServerStopper is disabled in
            //     configuration.", addressString.data(), socket->m_nId)
            L2_LOG_WARN(
                Log,
                u"[admin stop] Shutdown request from %s on socket %u ignored: ServerStopper is disabled in configuration.",
                addressString.data(), socket->m_nId
            );
            return false;
        }

        std::int32_t attemptedKey = 0;
        std::int32_t stopLoader = 0;
        ::Disassemble(cursor, "dd", &attemptedKey, &stopLoader);
        if (!CheckIPIsAdmin(addressString.data())) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"UnAuthenticated user attempted crash server!! [IP:%s]",
            //     addressString.data())
            // Log.Add(
            //     LOG_WARN,
            //     L"[admin stop] Shutdown request from %s on socket %u ignored: peer IP is not in the administrator address
            //     list.", addressString.data(), socket->m_nId)
            L2_LOG_WARN(
                Log,
                u"[admin stop] Shutdown request from %s on socket %u ignored: peer IP is not in the administrator address "
                u"list.",
                addressString.data(), socket->m_nId
            );
            return false;
        }

        const std::uint32_t serverKey = g_nServerStopperKey != 0 ? g_nServerStopperKey : 0x04dd0e8fU;
        if (static_cast<std::uint32_t>(attemptedKey) != serverKey) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"UnAuthenticated key used to crash server!! "
            //     L"[Server Key:%d][Attempted Key:%d] [IP:%s]",
            //     runtime::Int32FromBits(serverKey),
            //     attemptedKey,
            //     addressString.data())
            // Log.Add(
            //     LOG_WARN,
            //     L"[admin stop] Shutdown request from %s on socket %u ignored: authentication key does not match.",
            //     addressString.data(), socket->m_nId)
            L2_LOG_WARN(
                Log, u"[admin stop] Shutdown request from %s on socket %u ignored: authentication key does not match.",
                addressString.data(), socket->m_nId
            );
            return false;
        }

        // Log.Add(
        //     LOG_NORMAL,
        //     L"Server stopped by admin tool [IP:%s]",
        //     addressString.data())
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[admin stop] Authorized shutdown request from %s on socket %u. Posting shutdown work (stop loader: %S).",
        //     addressString.data(), socket->m_nId, stopLoader == 1 ? "yes" : "no")
        L2_LOG_INFO(
            Log, u"[admin stop] Authorized shutdown request from %s on socket %u; posting shutdown work (stop loader: %S).",
            addressString.data(), socket->m_nId, stopLoader == 1 ? "yes" : "no"
        );
        g_IsBySystemManager = true;
        (new CShutDownMessage(stopLoader == 1))->Post(0);
        return false;
    }

    if (clientVersion == kNorthAmericanProtocolVersion) {
        // Portable P152 extension. A live NA 5.0.0.35 C_VERSION packet
        // carries the AuthD account UID between the protocol revision and
        // the 256-byte nkey. The captured value 0x1771 matched the
        // authenticated local account UID 6001, and the following 252 bytes
        // matched target nkey bytes 0..251 exactly. Current revision 162 has
        // no such DWORD. Account ownership is still established by the
        // normal authenticated login packet below this handshake.
        [[maybe_unused]] std::int32_t authAccountUid = 0;
        cursor = ::Disassemble(cursor, "d", &authAccountUid);
    }

    std::array<char, 1024> clientKey;
    cursor = ::Disassemble(cursor, "b", g_nClientFileDecryptionKeyLen, clientKey.data());
    if (std::memcmp(clientKey.data(), g_nClientFileDecryptionKey, static_cast<std::size_t>(g_nClientFileDecryptionKeyLen)) !=
        0) {
        const ::in_addr address = socket->GetAddress();
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
        std::array<char16_t, 20> addressString{};
        FormatUtf16To(
            addressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
            static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid nkey (ClientKey != "
        //     L"g_nClientFileDecryptionKey) [IP:%s]",
        //     addressString.data())
        // Log.Add(LOG_ERROR,
        //     L"[client] Rejected version handshake from %ls: file decryption key does not match (protocol %d).",
        //     addressString.data(), clientVersion)
        // Log.Add(
        //     LOG_WARN,
        //     L"[client handshake] File decryption key mismatch on socket %u, peer %s, protocol %d. Requesting socket
        //     closure.", socket->m_nId, addressString.data(), clientVersion)
        L2_LOG_WARN(
            Log,
            u"[client handshake] File decryption key mismatch on socket %u, peer %s, protocol %d; requesting socket "
            u"closure.",
            socket->m_nId, addressString.data(), clientVersion
        );
        // Noise: A key mismatch is diagnosed above. Never publish client key bytes.
        // if (clientVersion == 152) {
        //     // Portable protocol-extension diagnostic. The current target
        //     // accepts only revision 162 and logs no key bytes. Keep any
        //     // future alternate-key mismatch limited to revision 152.
        //     L2Util::ReportByteArrayToHexStr(
        //         L"ClientNKey-P152",
        //         clientKey.data(),
        //         clientKey.data() + g_nClientFileDecryptionKeyLen)
        // }
        return true;
    }

    std::uint32_t inspectorCrc = 0;
    ::Disassemble(cursor, "d", &inspectorCrc);
    socket->m_crc = inspectorCrc;

    const std::int32_t protocolVersion = GetProtocolVersion();
    std::int32_t randomSeed = 0;
    std::uint64_t publicKey = 0;
    socket->GetShuffleSeedAndPublicKey(randomSeed, publicKey);

    const char queueTicketEnabled = static_cast<char>(g_bQueueTicketEnabled != false);
    const char mergedServer = static_cast<char>(g_bMergedServer != false);

    // Current L2Server.exe 0x00c2368c accepts only its embedded protocol
    // revision 162. The portable product also accepts the
    // North American protocol revision 152.
    const bool supportedClientProtocol = clientVersion == protocolVersion || clientVersion == kNorthAmericanProtocolVersion;
    const char protocolMatched = static_cast<char>(supportedClientProtocol);
    const std::int32_t worldId = g_nWorldId;
    socket->Send(
        "ccQddcdcc", 0x2e, static_cast<int>(protocolMatched), publicKey, 1, worldId, static_cast<int>(mergedServer),
        randomSeed, 0, static_cast<int>(queueTicketEnabled)
    );
    socket->SetKey(publicKey);

    if (supportedClientProtocol) {
        runtime::client_protocol::RecordNegotiatedRevision(socket, clientVersion);
        CIOObject::SetShortTimeStamp(&socket->m_nLastTimeCheck, 0);
        socket->AddTimer(1000, 0);
        return false;
    }

    if (g_bAcceptLowerProtocol) {
        const ::in_addr address = socket->GetAddress();
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
        std::array<char16_t, 20> addressString{};
        FormatUtf16To(
            addressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
            static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
        );
        CIOObject::SetShortTimeStamp(&socket->m_nLastTimeCheck, 0);
        socket->AddTimer(1000, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid protocol [Client:%d] [SERVER:%d] [IP:%s]",
        //     clientVersion,
        //     protocolVersion,
        //     addressString.data())
        // Log.Add(
        //     LOG_WARN,
        //     L"[client handshake] Unsupported protocol %d on socket %u, peer %s (supported %d or %d). AcceptLowerProtocol
        //     keeps the connection open.", clientVersion, socket->m_nId, addressString.data(), protocolVersion,
        //     kNorthAmericanProtocolVersion)
        L2_LOG_WARN(
            Log,
            u"[client handshake] Unsupported protocol %d on socket %u, peer %s (supported %d or %d); AcceptLowerProtocol "
            u"keeps the connection open.",
            clientVersion, socket->m_nId, addressString.data(), protocolVersion, kNorthAmericanProtocolVersion
        );
        return false;
    }

    const ::in_addr address = socket->GetAddress();
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
    std::array<char16_t, 20> addressString{};
    FormatUtf16To(
        addressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
        static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"Invalid protocol [Client:%d] [SERVER:%d] [IP:%s]"
    //     L"..... disconnect ",
    //     clientVersion,
    //     protocolVersion,
    //     addressString.data())
    // Log.Add(
    //     LOG_WARN,
    //     L"[client handshake] Unsupported protocol %d on socket %u, peer %s (supported %d or %d). Requesting socket
    //     closure.", clientVersion, socket->m_nId, addressString.data(), protocolVersion, kNorthAmericanProtocolVersion)
    L2_LOG_WARN(
        Log,
        u"[client handshake] Unsupported protocol %d on socket %u, peer %s (supported %d or %d); requesting socket closure.",
        clientVersion, socket->m_nId, addressString.data(), protocolVersion, kNorthAmericanProtocolVersion
    );
    return true;
}

bool CUserSocket::CharacterSelectPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c28d28.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    socket->InitCheckPacketAccept();

    int index = 0;
    std::int16_t additionalLocationType = 0;
    int value1 = 0;
    int value2 = 0;
    int value3 = 0;

    if (g_bCheckSecondaryAuth) {
        const SECONDARY_AUTH_STATE state = socket->GetSecondaryAuthState();
        if (state == SAS_REQUIRED) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Try hack? Invalid Secondary Auth State "
            //     L"[SAS_REQUIRED]",
            //     "UserSocket.cpp",
            //     0x2b0)
            // Log.Add(
            //     LOG_WARN,
            //     "[character select] Rejected request on socket %u, account %u: secondary authentication is still required.
            //     requesting socket closure.", socket->m_nId, socket->m_nAccountID)
            L2_LOG_WARN(
                Log,
                "[character select] Rejected request on socket %u, account %u: secondary authentication is still required; "
                "requesting socket closure.",
                socket->m_nId, socket->m_nAccountID
            );
            return true;
        }
        if (state != SAS_NOT_NEED && state != SAS_COMPLETED) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Invalid Secondary Auth State [%d]",
            //     "UserSocket.cpp",
            //     0x2b6,
            //     static_cast<int>(state))
            // Log.Add(
            //     LOG_WARN,
            //     "[character select] Rejected request on socket %u, account %u: secondary-authentication state %d is
            //     neither not-needed nor completed. Requesting socket closure.", socket->m_nId, socket->m_nAccountID,
            //     static_cast<int>(state))
            L2_LOG_WARN(
                Log,
                "[character select] Rejected request on socket %u, account %u: secondary-authentication state %d is neither "
                "not-needed nor completed; requesting socket closure.",
                socket->m_nId, socket->m_nAccountID, static_cast<int>(state)
            );
            return true;
        }
    }

    const unsigned char* cursor = ::Disassemble(packet, "d", &index);
    if (static_cast<std::uint32_t>(index) >= 7U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack? invalid char index[%d]",
        //     "UserSocket.cpp",
        //     0x2be,
        //     index)
        // Log.Add(
        //     LOG_WARN,
        //     "[character select] Rejected slot %d on socket %u, account %u: valid slots are 0..6. Requesting socket
        //     closure.", index, socket->m_nId, socket->m_nAccountID)
        L2_LOG_WARN(
            Log,
            "[character select] Rejected slot %d on socket %u, account %u: valid slots are 0..6; requesting socket closure.",
            index, socket->m_nId, socket->m_nAccountID
        );
        return true;
    }

    const auto deactivateReason = socket->m_eCharacterDeactivateReasons[static_cast<std::size_t>(index)];
    switch (deactivateReason) {
        case CharacterDeactivateReason::CDR_DEACTIVATE_DEFAULT:
        case CharacterDeactivateReason::CDR_DEACTIVATE_SLOT:
        case CharacterDeactivateReason::CDR_DEACTIVATE_OVER_LEVEL:
        case CharacterDeactivateReason::CDR_DEACTIVATE_CLASS:
            socket->Send("chdd", 0xfe, 0x148, index, static_cast<int>(deactivateReason));
            return false;
        default:
            break;
    }

    const int characterId = socket->m_nCharacterDBIDs[static_cast<std::size_t>(index)];
    if ((g_bMergedServer && socket->m_nCharacterPrevServers[static_cast<std::size_t>(index)] > 0) ||
        (g_bChangeWrongCharName && socket->m_nCharacterPrevServers[static_cast<std::size_t>(index)] == 0xff)) {
        const std::uint32_t now = runtime::GetTickCount();
        std::int32_t nowBits = 0;
        std::memcpy(&nowBits, &now, sizeof(nowBits));
        const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&socket->m_nTimeForChangingMergedName, nowBits);
        if (previous < 1 || runtime::SubtractInt32Wrapping(nowBits, previous) > 1000) {
            socket->Send(
                "chddS", 0xfe, 0x6a, 0, g_bMergedServer ? 0 : 1,
                socket->m_sCharacterOriginNames[static_cast<std::size_t>(index)].data()
            );
        }
        return false;
    }

    const int previousPacket = socket->SetLastUsePacket(0x12, false);
    if (previousPacket == 0x12) {
        return false;
    }
    if (previousPacket != 0x13 && previousPacket != 0x0c && previousPacket != 0x2b) {
        if (characterId > 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Try hack ? char_db[%d] last use packet [%d]",
            //     "UserSocket.cpp",
            //     0x309,
            //     characterId,
            //     previousPacket)
            // Log.Add(
            //     LOG_WARN,
            //     "[character select] Rejected character DB ID %d on socket %u, account %u: previous selection-flow opcode
            //     %d is not 0x13, 0x0c or 0x2b. Requesting socket closure.", characterId, socket->m_nId,
            //     socket->m_nAccountID, previousPacket)
            L2_LOG_WARN(
                Log,
                "[character select] Rejected character DB ID %d on socket %u, account %u: previous selection-flow opcode %d "
                "is not 0x13, 0x0c or 0x2b; requesting socket closure.",
                characterId, socket->m_nId, socket->m_nAccountID, previousPacket
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Try hack ? char index[%d] last use packet [%d]",
            //     "UserSocket.cpp",
            //     0x30d,
            //     index,
            //     previousPacket)
            // Log.Add(
            //     LOG_WARN,
            //     "[character select] Rejected slot %d on socket %u, account %u: previous selection-flow opcode %d is not
            //     0x13, 0x0c or 0x2b. Requesting socket closure.", index, socket->m_nId, socket->m_nAccountID,
            //     previousPacket)
            L2_LOG_WARN(
                Log,
                "[character select] Rejected slot %d on socket %u, account %u: previous selection-flow opcode %d is not "
                "0x13, 0x0c or 0x2b; requesting socket closure.",
                index, socket->m_nId, socket->m_nAccountID, previousPacket
            );
        }
        return true;
    }

    User* const user = socket->GetUser();
    if (user != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BOT User Try Clone Login!!! "
        //     L"(CharacterName = %s)",
        //     L"UserSocket.cpp",
        //     0x315,
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[character select] Rejected request on socket %u, account %u: character %s is already bound to this socket.
        //     requesting forced closure.", socket->m_nId, socket->m_nAccountID, user->GetName())
        // L2_LOG_WARN(Log,
        //     L"[character select] Rejected request on socket %u, account %u: character %s is already bound to this socket.
        //     requesting forced closure.", socket->m_nId, socket->m_nAccountID, user->GetName())
        L2_LOG_WARN(
            Log,
            u"[character select] Rejected request on socket %u, account %u: character %s is already bound to this socket; "
            u"requesting forced closure.",
            socket->m_nId, socket->m_nAccountID, user->GetName()
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    cursor = ::Disassemble(cursor, "h", &additionalLocationType);
    if (additionalLocationType != 0) {
        ::Disassemble(cursor, "ddd", &value1, &value2, &value3);
    }

    if (characterId <= 0) {
        // Log.Add(LOG_WARN,
        //     "[character select] Slot %d on socket %u, account %u, has no positive character DB ID (observed %d)
        //     requesting socket closure.", index, socket->m_nId, socket->m_nAccountID, characterId)
        L2_LOG_WARN(
            Log,
            "[character select] Slot %d on socket %u, account %u, has no positive character DB ID (observed %d); requesting "
            "socket closure.",
            index, socket->m_nId, socket->m_nAccountID, characterId
        );
        return true;
    }
    if (!socket->m_bLoggedIn) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? not login char_db[%d]",
        //     "UserSocket.cpp",
        //     0x327,
        //     characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[character select] Rejected character DB ID %d on socket %u: login has not completed. Requesting socket
        //     closure.", characterId, socket->m_nId)
        L2_LOG_WARN(
            Log,
            "[character select] Rejected character DB ID %d on socket %u: login has not completed; requesting socket "
            "closure.",
            characterId, socket->m_nId
        );
        return true;
    }
    const std::uint32_t accountId = socket->m_nAccountID;
    if (accountId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Try hack ? char_db[%d], account[%d]",
        //     "UserSocket.cpp",
        //     0x32f,
        //     characterId,
        //     accountId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[character select] Rejected character DB ID %d on logged-in socket %u: account ID is zero. Requesting socket
        //     closure.", characterId, socket->m_nId)
        L2_LOG_ERROR(
            Log,
            "[character select] Rejected character DB ID %d on logged-in socket %u: account ID is zero; requesting socket "
            "closure.",
            characterId, socket->m_nId
        );
        return true;
    }
    if (CBotBlockManager::inst().IsBotUser(accountId)) {
        const std::wstring accountName =
            Utf16ToWide(socket->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        // Log.Add(
        //     LOG_ERROR,
        //     L"BotUser Try login. C_CHARACTER_SELECT account[%s][%d]",
        //     accountName.c_str(),
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[character select] Rejected request on socket %u for account %s (%u): account is on the bot-block list.
        //     requesting socket closure.", socket->m_nId, accountName.c_str(), accountId)
        L2_LOG_WARN(
            Log,
            u"[character select] Rejected request on socket %u for account %s (%u): account is on the bot-block list; "
            u"requesting socket closure.",
            socket->m_nId, accountName.c_str(), accountId
        );
        return true;
    }

    CSPointer<User> existingUser = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, characterId));
    if (existingUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][RUSDUPE] BOT User Try Clone Login!!! "
        //     L"(DBID = %d)",
        //     L"UserSocket.cpp",
        //     0x33f,
        //     characterId)
        // Log.Add(
        //     LOG_WARN,
        //     "[character select] Rejected character DB ID %d on socket %u, account %u: this world already has an entry for
        //     that character in its user map. Requesting forced closure.", characterId, socket->m_nId,
        //     socket->m_nAccountID)
        L2_LOG_WARN(
            Log,
            "[character select] Rejected character DB ID %d on socket %u, account %u: this world already has an entry for "
            "that character in its user map; requesting forced closure.",
            characterId, socket->m_nId, socket->m_nAccountID
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return true;
    }

    if (!socket->IsUnderReloading()) {
        const std::uint32_t raidAccountId = socket->m_nAccountID;
        CSPointer<CUserSocket> enteringSocket = g_UserSocketMapEnterChar.FindObject(static_cast<int>(raidAccountId));
        if (enteringSocket) {
            const unsigned int existingSocketId = enteringSocket->m_nId;
            const unsigned int enteringSocketId = socket->m_nId;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[RUSDUPE] Duplicate character? !!! close socket. "
            //     L"account id[%d]. exists[%d] entering[%d]",
            //     raidAccountId,
            //     existingSocketId,
            //     enteringSocketId)
            // Log.Add(
            //     LOG_WARN,
            //     "[character select] Rejected entering socket %u for account %u: socket %u is already registered for
            //     character entry. Requesting closure of the entering socket.", enteringSocketId, raidAccountId,
            //     existingSocketId)
            L2_LOG_WARN(
                Log,
                "[character select] Rejected entering socket %u for account %u: socket %u is already registered for "
                "character entry; requesting closure of the entering socket.",
                enteringSocketId, raidAccountId, existingSocketId
            );
            return true;
        }
    }

    db.LoadSelectedCharacter(
        g_nWorldId, characterId, socket, static_cast<AddtionalLocationType>(static_cast<int>(additionalLocationType)),
        value1, value2, value3, false
    );
    socket->m_nCloseReason = SCR_NORMAL;
    return false;
}

bool CUserSocket::LoginPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c2acf4.
    L2_GUARD_FUNCTION();

    // The current stack object is exactly the 0x82-byte decode capacity:
    // 65 UTF-16 code units, including the terminator.
    std::array<SharedUtilsWideChar, 65> accountName{};
    int accountId = 0;
    int loginKey = 0;
    int oneTimeKey = 0;
    int gameId = 0;
    int countryCode = 0;
    std::int64_t macAddress = 0;
    int pptp = 0;
    ::Disassemble(
        packet, "SdddddQd", 0x82, accountName.data(), &accountId, &loginKey, &oneTimeKey, &gameId, &countryCode, &macAddress,
        &pptp
    );

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    if (!g_bQueueTicketEnabled) {
        socket->m_nMacAddress = macAddress;
        return socket->_LoginPacket(accountName.data(), accountId, oneTimeKey, gameId, loginKey, countryCode, pptp);
    }

    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [SHANGHAI_QUEUETICKET] socket Is NULL ",
        //     kUserSocketSource,
        //     0x609)
        // Log.Add(
        //     LOG_ERROR,
        //     "[login] Queue-ticket login received without a socket, returning a close request.")
        L2_LOG_ERROR(Log, "[login] Queue-ticket login received without a socket; returning a close request.");
    } else if (socket->m_bPassQueueTicket && socket->m_nQueueTicketAccountID != 0) {
        // Current queue-ticket success jumps directly to _LoginPacket and
        // bypasses the non-queue MAC-address store.
        return socket->_LoginPacket(accountName.data(), accountId, oneTimeKey, gameId, loginKey, countryCode, pptp);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [SHANGHAI_QUEUETICKET] socket Is Not Pass "
        //     L"IsPassQueueTicket",
        //     kUserSocketSource,
        //     0x610)
        // Log.Add(
        //     LOG_WARN,
        //     "[login] Queue-ticket check rejected socket %u, requested account %u: passed flag %d, ticket account %u.
        //     requesting socket closure.", socket->m_nId, static_cast<std::uint32_t>(accountId), socket->m_bPassQueueTicket
        //     ? 1 : 0, socket->m_nQueueTicketAccountID)
        L2_LOG_WARN(
            Log,
            "[login] Queue-ticket check rejected socket %u, requested account %u: passed flag %d, ticket account %u; "
            "requesting socket closure.",
            socket->m_nId, static_cast<std::uint32_t>(accountId), socket->m_bPassQueueTicket ? 1 : 0,
            socket->m_nQueueTicketAccountID
        );
    }
    return true;
}

bool CUserSocket::_LoginPacket(
    SharedUtilsWideChar* packetAccountName,
    int accountId,
    int oneTimeKey,
    int gameId,
    int loginKey,
    int countryCode,
    int pptp
) {
    L2_GUARD_FUNCTION();
    (void)packetAccountName;

    if (g_nIPBlock != 0) {
        const ::in_addr address = GetAddress();
        const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
        IPAddress ipAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
        if (IPBlockManager::inst().IsBlockedIP(ipAddress)) {
            // Log.Add(LOG_WARN,
            //     "[login] Rejected socket %u, requested account %u, peer %d.%d.%d.%d: IP address is blocked. Requesting
            //     forced closure.", m_nId, static_cast<std::uint32_t>(accountId), static_cast<int>(bytes[0]),
            //     static_cast<int>(bytes[1]), static_cast<int>(bytes[2]), static_cast<int>(bytes[3]))
            L2_LOG_WARN(
                Log,
                "[login] Rejected socket %u, requested account %u, peer %d.%d.%d.%d: IP address is blocked; requesting "
                "forced closure.",
                m_nId, static_cast<std::uint32_t>(accountId), static_cast<int>(bytes[0]), static_cast<int>(bytes[1]),
                static_cast<int>(bytes[2]), static_cast<int>(bytes[3])
            );
            ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return true;
        }
    }

    m_CountryCode =
        static_cast<std::uint8_t>(static_cast<std::uint32_t>(countryCode) < 14U ? countryCode : ::g_nCountryCode);

    const std::uint32_t accountKey = static_cast<std::uint32_t>(accountId);
    {
        L2_GUARD(g_ReadyAccountMapSect);
        const auto readyAccount = g_ReadyAccountMap.find(accountKey);
        if (readyAccount == g_ReadyAccountMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] DualLogin-Login : account not exists in "
            //     L"ReadyAccountMap [%d]",
            //     "UserSocket.cpp",
            //     0x63e,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[login] Rejected socket %u, account %u: account is absent from the ready-account map. Requesting socket
            //     closure.", m_nId, accountKey)
            L2_LOG_WARN(
                Log,
                "[login] Rejected socket %u, account %u: account is absent from the ready-account map; requesting socket "
                "closure.",
                m_nId, accountKey
            );
            return true;
        }
    }

    std::array<SharedUtilsWideChar, 15> accountName{};
    stAccountAuthInfo authInfo;
    {
        L2_READ_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountKey);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "UserSocket.cpp",
            //     0x64b,
            //     accountId)
            // Log.Add(
            //     LOG_WARN,
            //     "[login] Rejected socket %u, account %u: cached authentication information is missing. Requesting socket
            //     closure.", m_nId, accountKey)
            L2_LOG_WARN(
                Log,
                "[login] Rejected socket %u, account %u: cached authentication information is missing; requesting socket "
                "closure.",
                m_nId, accountKey
            );
            return true;
        }
        std::memcpy(&authInfo, &account->second, sizeof(authInfo));
        AnsiToUnicode(authInfo.sAccountName, 0x0e, accountName.data(), 0x0e);
    }

    const std::size_t accountNameLength = std::char_traits<SharedUtilsWideChar>::length(accountName.data());
    bool invalidAccountName = accountNameLength == 0;
    for (std::size_t index = 0; index < accountNameLength; ++index) {
        const std::uint16_t character = static_cast<std::uint16_t>(accountName[index]);
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') || character == '_')) {
            invalidAccountName = true;
            break;
        }
    }

    const std::wstring accountNameForLog = Utf16ToWide(accountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    if (invalidAccountName && !m_bForceLogin) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Trying to login with invalid account name.[%s][%d]",
        //     accountNameForLog.c_str(),
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[login] Rejected socket %u, account %u: cached account name is empty or contains characters outside A-Z, a-z,
        //     0-9 and underscore, requesting socket closure.", m_nId, accountKey)
        L2_LOG_WARN(
            Log,
            "[login] Rejected socket %u, account %u: cached account name is empty or contains characters outside A-Z, a-z, "
            "0-9 and underscore; requesting socket closure.",
            m_nId, accountKey
        );
        return true;
    }

    if (CBotBlockManager::inst().IsBotUser(accountKey)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"BotUser Try login. C_LOGIN. account[%s][%d]",
        //     accountNameForLog.c_str(),
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[login] Rejected socket %u, account %u: account is on the bot-block list. Requesting socket closure.",
        //     m_nId, accountKey)
        L2_LOG_WARN(
            Log, "[login] Rejected socket %u, account %u: account is on the bot-block list; requesting socket closure.",
            m_nId, accountKey
        );
        return true;
    }

    int socketUseTime;
    const bool accountRegistered = [&] {
        L2_GUARD(accountMapSect);
        const auto existingAccount = accountMap.find(accountKey);
        if (existingAccount == accountMap.end()) {
            accountMap.emplace(accountKey, this);
            L2_ADD_REF(this, ObjectRefType::AccountMap);
            m_nAccountID = accountKey;
            m_bLoggedIn = true;
            return true;
        }
        socketUseTime = existingAccount->second->GetSocketUseTime();
        return false;
    }();
    if (!accountRegistered) {
        // Log.Add(
        //     LOG_ERROR,
        //     socketUseTime < 301
        //         ? L"try dual login. but continue.[%s][%d][%d]"
        //         : L"try dual login. kick ?.[%s][%d][%d]",
        //     accountNameForLog.c_str(),
        //     accountId,
        //     socketUseTime)
        // Log.Add(
        //     LOG_WARN,
        //     "[login] Rejected incoming socket %u for account %u: account already has a registered socket (reported use
        //     time %d seconds), requesting closure of the incoming socket.", m_nId, accountKey, socketUseTime)
        L2_LOG_WARN(
            Log,
            "[login] Rejected incoming socket %u for account %u: account already has a registered socket (reported use time "
            "%d seconds); requesting closure of the incoming socket.",
            m_nId, accountKey, socketUseTime
        );
        return true;
    }

    const auto* const expectedAddress = reinterpret_cast<const std::uint8_t*>(&authInfo.client_addr.s_addr);
    const auto currentAddressByte = [this](std::size_t index) {
        const ::in_addr address = GetAddress();
        return reinterpret_cast<const std::uint8_t*>(&address.s_addr)[index];
    };
    if (expectedAddress[0] != currentAddressByte(0) || expectedAddress[1] != currentAddressByte(1) ||
        expectedAddress[2] != currentAddressByte(2) || expectedAddress[3] != currentAddressByte(3)) {
        std::array<char16_t, 20> expectedAddressString{};
        FormatUtf16To(
            expectedAddressString.data(), 0x13, u"%d.%d.%d.%d", static_cast<int>(expectedAddress[0]),
            static_cast<int>(expectedAddress[1]), static_cast<int>(expectedAddress[2]), static_cast<int>(expectedAddress[3])
        );
        const std::u16string currentAddressString = GetIPAddressString();
        Log.Add(
            LOG_BOTH, u"%I64d,,%I64d,,,,,,%s,%s,,,,,,,,,,,,,%s,,,", static_cast<long long>(0x36f),
            static_cast<long long>(accountId), expectedAddressString.data(), currentAddressString.c_str(),
            accountNameForLog.c_str()
        );
    }

    m_bAuthCharge = authInfo.nStat != 2000;
    const int socketCountPerIP = g_SocketLimiter.IncUserSocketPerIP(GetIPAddressUINT());
    const int socketCountPerMAC = g_SocketLimiter.IncUserSocketPerMAC(m_nMacAddress);
    if (g_nMaxUserSocketPerIP < socketCountPerIP) {
        const std::u16string macAddressString = GetMacAddressString();
        const std::u16string ipAddressString = GetIPAddressString();
        Log.Add(
            LOG_AUDIT, u"%I64d,,%I64d,,,,,,,%s,%s,%I64d,,,,,,,,,,,%s,,,", static_cast<long long>(0x59d),
            static_cast<long long>(accountId), ipAddressString.c_str(), macAddressString.c_str(),
            static_cast<long long>(g_nMaxUserSocketPerMAC), accountNameForLog.c_str()
        );
    }
    if (g_nMaxUserSocketPerMAC < socketCountPerMAC) {
        const std::u16string macAddressString = GetMacAddressString();
        const std::u16string ipAddressString = GetIPAddressString();
        Log.Add(
            LOG_AUDIT, u"%I64d,,%I64d,,,,,,,%s,%s,%I64d,,,,,,,,,,,%s,,,", static_cast<long long>(0x59e),
            static_cast<long long>(accountId), ipAddressString.c_str(), macAddressString.c_str(),
            static_cast<long long>(g_nMaxUserSocketPerMAC), accountNameForLog.c_str()
        );
    }

    m_nSecondaryAuthState = SAS_REQUIRED;
    SetAccountName(accountName.data());
    const bool insertPlayingSocket = authd.RequestLogin(accountName.data(), accountId, loginKey, oneTimeKey, gameId, this);
    IncUserCount(insertPlayingSocket);

    if (pptp != 1) {
        return false;
    }

    const int cafeCodeComparison = std::strncmp(m_sPCCafeCode.data(), "0000000", 8);
    if (g_nPPTPOption == 1) {
        Log.Add(
            LOG_AUDIT, u"%I64d,,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,,%s,,,", static_cast<long long>(0x59f),
            static_cast<long long>(accountId), static_cast<long long>(0), static_cast<long long>(0),
            accountNameForLog.c_str()
        );
        return false;
    }
    if (g_nPPTPOption == 2) {
        if (cafeCodeComparison == 0) {
            return false;
        }
        Log.Add(
            LOG_AUDIT, u"%I64d,,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,,%s,,,", static_cast<long long>(0x59f),
            static_cast<long long>(accountId), static_cast<long long>(1), static_cast<long long>(1),
            accountNameForLog.c_str()
        );
        ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return true;
    }
    if (g_nPPTPOption == 3) {
        Log.Add(
            LOG_AUDIT, u"%I64d,,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,,%s,,,", static_cast<long long>(0x59f),
            static_cast<long long>(accountId), static_cast<long long>(1), static_cast<long long>(0),
            accountNameForLog.c_str()
        );
        ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return true;
    }
    return false;
}

void CUserSocket::OnLoginSuccess(CUserSocket* socket, int tokenLength, const char* token) {
    L2_GUARD_FUNCTION();

    if (socket == nullptr || !socket->m_bActive) {
        return;
    }

    const unsigned int accountId = socket->m_nAccountID;
    if (!g_bUseNPLogin) {
        socket->Send("cdd", 10, -1, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][NO_ERROR]NPError account_id(%d) login success",
        //     kUserSocketSource,
        //     0x34dc,
        //     accountId)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[login] Authentication succeeded for socket %u, account %u (NP login disabled). Requesting the character
        //     list.", socket->m_nId, accountId)
        L2_LOG_INFO(
            Log,
            "[login] Authentication succeeded for socket %u, account %u (NP login disabled); requesting the character list.",
            socket->m_nId, accountId
        );
    } else {
        if (token == nullptr || token[0] == '\0') {
            ForceCrash(CrashInfo::NP_SOCKET_ERROR);
        }

        socket->Send("cddb", 10, 0, tokenLength, tokenLength, token);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][NO_ERROR]NPError account_id(%d) login success",
        //     kUserSocketSource,
        //     0x34f1,
        //     accountId)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[login] Authentication succeeded for socket %u, account %u (NP login enabled). Requesting the character
        //     list.", socket->m_nId, accountId)
        L2_LOG_INFO(
            Log,
            "[login] Authentication succeeded for socket %u, account %u (NP login enabled); requesting the character list.",
            socket->m_nId, accountId
        );
    }

    const std::u16string ipAddress = std::u16string(socket->GetIPAddressString().c_str());
    const std::u16string macAddress = std::u16string(socket->GetMacAddressString().c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fe0, 0, socket->m_nAccountID, 0, 0, 0, 0, 0, ipAddress.c_str(), macAddress.c_str(), nullptr, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, nullptr, socket->m_sAccountName.data(), nullptr, nullptr, 0
    );

    db.RequestLoadChar(accountId, socket);
    if (g_bProcessClientINI) {
        db.RequestLoadClientINI(static_cast<int>(accountId));
    }
}

void CUserSocket::OnLoginTimeout(CUserSocket* socket) {
    L2_GUARD_FUNCTION();

    if (socket == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) OnLoginTimeout(), "
        //     L"invalid argument(pUserSocket is null)",
        //     kUserSocketSource,
        //     0x353f,
        //     0,
        //     0)
        // Log.Add(
        //     LOG_ERROR,
        //     "[login] Login timeout callback received a null socket, no connection was closed.")
        L2_LOG_ERROR(Log, "[login] Login timeout callback received a null socket; no connection was closed.");
        return;
    }

    if (!socket->m_bActive || NPSystem::CNPD::LoginStep_IsComplete(socket->m_nAccountID)) {
        return;
    }

    const std::u16string ipAddress = std::u16string(socket->GetIPAddressString().c_str());
    const std::u16string macAddress = std::u16string(socket->GetMacAddressString().c_str());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x1fe0, 0, socket->m_nAccountID, 0, 0, 0, 0, 0, ipAddress.c_str(), macAddress.c_str(), nullptr, 5, 0, 0, 0, 0, 0, 0,
        0, 0, 0, nullptr, socket->m_sAccountName.data(), nullptr, nullptr, 0
    );
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d]NPError(a=%d, e=%d) OnLoginTimeout()",
    //     kUserSocketSource,
    //     0x3550,
    //     socket->m_nAccountID,
    //     0)
    // Log.Add(
    //     LOG_WARN,
    //     "[login] Authentication timed out for socket %u, account %u. Requesting forced closure.",
    //     socket->m_nId, socket->m_nAccountID)
    L2_LOG_WARN(
        Log, "[login] Authentication timed out for socket %u, account %u; requesting forced closure.", socket->m_nId,
        socket->m_nAccountID
    );
    socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
}

void CUserSocket::OnLoginFailed(CUserSocket* socket, unsigned int accountId, int errorCode) {
    L2_GUARD_FUNCTION();

    if (socket == nullptr) {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account != accountMap.end()) {
            socket = account->second;
        }
    }

    if (socket != nullptr && socket->m_bActive) {
        socket->Send("cd", 10, errorCode);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) OnLoginFailed",
        //     kUserSocketSource,
        //     0x3523,
        //     accountId,
        //     errorCode)
        // Log.Add(
        //     LOG_WARN,
        //     "[login] Authentication failed for account %u on socket %u (socket account %u), error %d. A failure reply was
        //     requested.", accountId, socket->m_nId, socket->m_nAccountID, errorCode)
        L2_LOG_WARN(
            Log,
            "[login] Authentication failed for account %u on socket %u (socket account %u), error %d; a failure reply was "
            "requested.",
            accountId, socket->m_nId, socket->m_nAccountID, errorCode
        );

        const std::u16string ipAddress = std::u16string(socket->GetIPAddressString().c_str());
        const std::u16string macAddress = std::u16string(socket->GetMacAddressString().c_str());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x1fe0, 0, socket->m_nAccountID, 0, 0, 0, 0, 0, ipAddress.c_str(), macAddress.c_str(), nullptr, errorCode, 0, 0,
            0, 0, 0, 0, 0, 0, 0, nullptr, socket->m_sAccountName.data(), nullptr, nullptr, 0
        );
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d]NPError(a=%d, e=%d) OnLoginFailed, "
    //     L"fail to send(S_LOGIN_RESULT)",
    //     kUserSocketSource,
    //     0x3531,
    //     accountId,
    //     errorCode)
    // Log.Add(
    //     LOG_WARN,
    //     "[login] Authentication failed for account %u, error %d. No failure reply was requested because the socket is
    //     %s.", accountId, errorCode, socket ? "inactive" : "unavailable")
    L2_LOG_WARN(
        Log,
        "[login] Authentication failed for account %u, error %d; no failure reply was requested because the socket is %s.",
        accountId, errorCode, socket ? "inactive" : "unavailable"
    );
}

void CUserSocket::PopGradeSkill(const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_GradeSkillLock);
    const auto found = m_mGradeSkillList.find(skillInfo->m_nSkillId);
    if (found != m_mGradeSkillList.end()) {
        m_mGradeSkillList.erase(found);
    }
}

void CUserSocket::_PushGradeSkill(InfectedSkillType& infectedSkill) {
    L2_GUARD_FUNCTION();
    const int skillId = infectedSkill.pSkillInfo->m_nSkillId;
    const auto found = m_mGradeSkillList.find(skillId);
    if (found != m_mGradeSkillList.end()) {
        m_mGradeSkillList.erase(found);
    }
    m_mGradeSkillList.emplace(skillId, infectedSkill);
}

void CUserSocket::PushGradeSkill(InfectedSkillType& infectedSkill) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_GradeSkillLock);
    _PushGradeSkill(infectedSkill);
}

void CUserSocket::PushGradeSkillToUser(InfectedSkillType& infectedSkill) {
    L2_GUARD_FUNCTION();
    User* const user = GetUser();
    if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
        return;
    }

    L2_GUARD(user->m_InfectedSkillsSect);
    const int now = runtime::crt::Time32Now();
    if (now >= runtime::AddInt32Wrapping(infectedSkill.nStartTime, infectedSkill.nDuration)) {
        return;
    }

    bool existingSkill = false;
    bool addSkill = true;
    int previousRemainingTime = 0;
    for (std::size_t index = 0; index < user->m_InfectedSkills.size(); ++index) {
        InfectedSkillType& current = runtime::CheckedVectorAt(user->m_InfectedSkills, index);
        if (current.pSkillInfo->m_nSkillId != infectedSkill.pSkillInfo->m_nSkillId) {
            continue;
        }

        existingSkill = true;
        previousRemainingTime = runtime::SubtractInt32Wrapping(
            runtime::AddInt32Wrapping(current.nStartTime, current.nDuration), runtime::crt::Time32Now()
        );
        if (current.pSkillInfo->m_nLevel == infectedSkill.pSkillInfo->m_nLevel &&
            current.pSkillInfo->m_nSubLevel == infectedSkill.pSkillInfo->m_nSubLevel) {
            current.nStartTime = infectedSkill.nStartTime;
            current.nDuration = infectedSkill.nDuration;
            addSkill = false;
        } else {
            user->_DeleteAbnormalStatus(static_cast<int>(index), user, infectedSkill.pSkillInfo, false, SPET_REPLACE, true);
        }
        break;
    }

    if (addSkill) {
        user->m_InfectedSkills.push_back(infectedSkill);
        user->_AddAbnormalStatus(*infectedSkill.pSkillInfo, user, true, true, SPST_ETC);
    }

    const std::uint32_t skillLevelBits = (static_cast<std::uint32_t>(infectedSkill.pSkillInfo->m_nSubLevel) << 16) |
        static_cast<std::uint16_t>(infectedSkill.pSkillInfo->m_nLevel);
    const std::int64_t skillLevel = static_cast<std::int32_t>(skillLevelBits);
    CLogFormatter formatter;
    formatter.WriteLog2(
        existingSkill ? 0x1b8 : 0x1b6, user, user, infectedSkill.pSkillInfo->m_nSkillId, skillLevel,
        existingSkill ? previousRemainingTime : infectedSkill.nDuration, existingSkill ? infectedSkill.nDuration : 0, 0, 0, 0
    );
    user->m_bValidateChanged = true;
}

void CUserSocket::PopGradeSkillFromUser(const CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    User* const user = GetUser();
    if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
        return;
    }

    L2_GUARD(user->m_InfectedSkillsSect);
    for (std::size_t index = 0; index < user->m_InfectedSkills.size(); ++index) {
        if (runtime::CheckedVectorAt(user->m_InfectedSkills, index).pSkillInfo->m_nSkillId != skillInfo->m_nSkillId) {
            continue;
        }

        // Current L2Server.exe CUserSocket::PopGradeSkillFromUser at
        // 0x00c165e4 calls CCreature::_DeleteAbnormalStatus and then reads
        // m_InfectedSkills[index] again. MSVC's unchecked vector access
        // therefore observes the shifted next entry, or the stale bytes of
        // the just-destroyed last entry. libc++ may trap that last-entry
        // access, so keep the exact defective expression below and emulate
        // its effective target result without indexing past the new size.
        const CSkillInfo* const erasedSkillInfo = runtime::CheckedVectorAt(user->m_InfectedSkills, index).pSkillInfo;
        user->_DeleteAbnormalStatus(static_cast<int>(index), user, skillInfo, false, SPET_ETC, true);
#if 0
        // Shipping post-erase access kept for the recovery trail.
        const InfectedSkillType& loggedSkill =
            user->m_InfectedSkills[index];
        const CSkillInfo* const loggedSkillInfo =
            loggedSkill.pSkillInfo;
#else
        const CSkillInfo* const loggedSkillInfo = index < user->m_InfectedSkills.size()
            ? runtime::CheckedVectorAt(user->m_InfectedSkills, index).pSkillInfo
            : erasedSkillInfo;
#endif
        const std::uint32_t skillLevelBits = (static_cast<std::uint32_t>(loggedSkillInfo->m_nSubLevel) << 16) |
            static_cast<std::uint16_t>(loggedSkillInfo->m_nLevel);
        const std::int64_t skillLevel = static_cast<std::int32_t>(skillLevelBits);
        CLogFormatter formatter;
        formatter.WriteLog2(0x1b7, user, user, loggedSkillInfo->m_nSkillId, skillLevel, 0, 0, 0, 0, 0);
        break;
    }
    user->m_bValidateChanged = true;
}

void CUserSocket::CopyGradeSkillsToInfectedSkillList() {
    L2_GUARD_FUNCTION();

    User* const user = GetUser();
    if (user == nullptr || user->IsDeleting() || user->m_Deleted != 0) {
        return;
    }

    L2_GUARD(m_GradeSkillLock);
    for (const auto& entry : m_mGradeSkillList) {
        const InfectedSkillType& infectedSkill = entry.second;
        if (runtime::crt::Time32Now() < runtime::AddInt32Wrapping(infectedSkill.nStartTime, infectedSkill.nDuration)) {
            user->m_InfectedSkills.push_back(infectedSkill);
            user->_AddAbnormalStatus(*infectedSkill.pSkillInfo, user, false, true, SPST_ETC);
        }
    }
}

CSocketLimiter::CSocketLimiter() = default;

CSocketLimiter::~CSocketLimiter() = default;

int CSocketLimiter::IncUserSocketPerIP(std::uint32_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_nMaxUserSocketPerIP < 1) {
        return 0;
    }

    const auto found = m_mapIP.find(address);
    if (found == m_mapIP.end()) {
        m_mapIP.emplace(address, 1);
        return 1;
    }
    found->second = runtime::AddInt32Wrapping(found->second, 1);
    return found->second;
}

int CSocketLimiter::DecUserSocketPerIP(std::uint32_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_nMaxUserSocketPerIP < 1) {
        return 0;
    }

    const auto found = m_mapIP.find(address);
    if (found == m_mapIP.end()) {
        return 0;
    }
    if (found->second == 1) {
        m_mapIP.erase(found);
        return 0;
    }
    found->second = runtime::AddInt32Wrapping(found->second, -1);
    return found->second;
}

void CSocketLimiter::DeleteUserSocketPerIP(std::uint32_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_mapIP.erase(address);
}

void CSocketLimiter::DeleteAllIP() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_mapIP.clear();
}

int CSocketLimiter::IncUserSocketPerMAC(std::int64_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_nMaxUserSocketPerMAC < 1) {
        return 0;
    }

    const auto found = m_mapMAC.find(address);
    if (found == m_mapMAC.end()) {
        m_mapMAC.emplace(address, 1);
        return 1;
    }
    found->second = runtime::AddInt32Wrapping(found->second, 1);
    return found->second;
}

int CSocketLimiter::DecUserSocketPerMAC(std::int64_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_nMaxUserSocketPerMAC < 1) {
        return 0;
    }

    const auto found = m_mapMAC.find(address);
    if (found == m_mapMAC.end()) {
        return 0;
    }
    if (found->second == 1) {
        m_mapMAC.erase(found);
        return 0;
    }
    found->second = runtime::AddInt32Wrapping(found->second, -1);
    return found->second;
}

void CSocketLimiter::DeleteUserSocketPerMAC(std::int64_t address) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_mapMAC.erase(address);
}

void CSocketLimiter::DeleteAllMAC() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    m_mapMAC.clear();
}

CUserSocket* CUserSocket::CastUserSocket() { return this; }

User* CUserSocket::GetUser() const { return m_pUser; }

unsigned int CUserSocket::GetServerId() const { return m_nId; }

void CUserSocket::BindUser(User* user) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserSocket] bound NULL user !")
        // Log.Add(
        //     LOG_ERROR,
        //     "[socket] Binding a null user to socket %u, account %u. The user pointer will be cleared.",
        //     m_nId, m_nAccountID)
        L2_LOG_ERROR(
            Log, "[socket] Binding a null user to socket %u, account %u; the user pointer will be cleared.", m_nId,
            m_nAccountID
        );
    }
    if (m_pUser != nullptr && user != nullptr) {
        // Noise: The following diagnostic identifies both user bindings and the actual replacement.
        // Log.Add(
        //     LOG_ERROR,
        //     L"CUserSocket has a user but try to rebind")
        const std::uint32_t newSid = user->Id();
        User* const previousUser = m_pUser;
        const std::uint32_t previousSid = previousUser->Id();
        const char16_t* const newName = user->GetName();
        const std::uint32_t newDbId = user->GetDBID();
        const char16_t* const previousName = previousUser->GetName();
        const std::uint32_t previousDbId = previousUser->GetDBID();
        // Log.Add(
        //     LOG_ERROR,
        //     L"prev user: dbid(%d) name(%s) sid(%d)  "
        //     L"new user: dbid(%d) name(%s) sid(%d)",
        //     previousDbId,
        //     previousName,
        //     previousSid,
        //     newDbId,
        //     newName,
        //     newSid)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[socket] Socket %u, account %u, is already bound. Replacing character %s (DB ID %u, user object %u) with %s
        //     (DB ID %u, user object %u).", m_nId, m_nAccountID, previousName, previousDbId, previousSid, newName, newDbId,
        //     newSid)
        // L2_LOG_ERROR(Log,
        //     L"[socket] Socket %u, account %u, is already bound. Replacing character %s (DB ID %u, user object %u) with %s
        //     (DB ID %u, user object %u).", m_nId, m_nAccountID, previousName, previousDbId, previousSid, newName, newDbId,
        //     newSid)
        L2_LOG_ERROR(
            Log,
            u"[socket] Socket %u, account %u, is already bound; replacing character %s (DB ID %u, user object %u) with %s "
            u"(DB ID %u, user object %u).",
            m_nId, m_nAccountID, previousName, previousDbId, previousSid, newName, newDbId, newSid
        );
    }
    m_pUser = user;
}

void CUserSocket::UnBindUser() {
    // Current L2Server 0x00c1a1c0.
    L2_GUARD_FUNCTION();

    if (m_pUser != nullptr) {
        const int sampleCount = std::max(1, m_SumCount);
        const std::int64_t averagePacketReadCount = m_SumPacketReadCountPerTick / sampleCount;

        runtime::raw_atomic::AddI64(&g_nSumOfAvgPacketReadCount, averagePacketReadCount);
        runtime::raw_atomic::AddI32(&g_nCountOfAvgPacketReadSum, 1);
        runtime::raw_atomic::AddI64(&g_nSumOfMaxPacketReadCount, m_MaxPacketReadCountPerTick);
        runtime::raw_atomic::AddI32(&g_nCountOfMaxPacketReadSum, 1);

        const int threadId = CThreadManager::GetThreadID();
        if (g_nMaxPacketReadCountPerSec[threadId] < m_MaxPacketReadCountPerTick) {
            g_nMaxPacketReadCountPerSec[threadId] = m_MaxPacketReadCountPerTick;
        }

        const std::wstring accountName =
            Utf16ToWide(m_pUser->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        // Log.Add(
        //     LOG_AUDIT,
        //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,%s,%s,,,,",
        //     static_cast<std::int64_t>(0x59b),
        //     static_cast<std::int64_t>(m_pUser->GetDBID()),
        //     static_cast<std::int64_t>(m_pUser->GetAccountID()),
        //     averagePacketReadCount,
        //     static_cast<std::int64_t>(m_MaxPacketReadCountPerTick),
        //     m_pUser->GetName(),
        //     accountName.c_str())
        Log.Add(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,%s,%s,,,,", static_cast<std::int64_t>(0x59b),
            static_cast<std::int64_t>(m_pUser->GetDBID()), static_cast<std::int64_t>(m_pUser->GetAccountID()),
            averagePacketReadCount, static_cast<std::int64_t>(m_MaxPacketReadCountPerTick), m_pUser->GetName(),
            accountName.c_str()
        );

        m_nCharacterDBIDs.fill(0);
        m_eCharacterDeactivateReasons.fill(CharacterDeactivateReason::CDR_DEACTIVATE_DEFAULT);

        User* const user = m_pUser;
        if (user->m_data->m_nWorldId != g_nWorldId && m_nCloseReason != SCR_RETURN_TO_ORIGIN) {
            CSPointer<User> retainedUser(user, ObjectRefType::FunctionChain);
            WOlympiadService::inst().OnUserDisconnected(retainedUser);
            RaidServerManager::inst().ClearRaidUserMap(*user);
            RaidServerManager::inst().NotifyRaidUserDisconnected(*user);
        }

        user->LeaveWorld(m_nCloseReason != SCR_RAID_QUIT);
        user->Delete();
    }
    m_pUser = nullptr;
}

void CUserSocket::IncUserCount(bool insertPlayingSocket) {
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::IncrementI32(&g_nServerUserCount);
    if (g_nMaxServerUserCount < g_nServerUserCount) {
        g_nMaxServerUserCount = g_nServerUserCount;
        g_tMaxServerUserCount = runtime::GetLocalTimeNow();
    }

    if (insertPlayingSocket) {
        g_UserSocketMapPlaying.Insert(static_cast<int>(GetServerId()), this, const_cast<char*>("UserSocket.cpp"), 0x6f8);
    }
    m_bUserCountProcessed = true;
}

void CUserSocket::DecUserCount(bool deletePlayingSocket) {
    L2_GUARD_FUNCTION();

    if (!m_bUserCountProcessed) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] invalid user count, account_name(%s), g_nServerUserCount:(%d)",
        //     L"UserSocket.cpp",
        //     0x706,
        //     m_sAccountName.data(),
        //     g_nServerUserCount)
        // Log.Add(
        //     LOG_WARN,
        //     "[socket] User-count cleanup for socket %u, account %u, started with its count-processed flag unset (observed
        //     server count %d), the existing count-update path continues.", m_nId, m_nAccountID, g_nServerUserCount)
        L2_LOG_WARN(
            Log,
            "[socket] User-count cleanup for socket %u, account %u, started with its count-processed flag unset (observed "
            "server count %d); the existing count-update path continues.",
            m_nId, m_nAccountID, g_nServerUserCount
        );
    }
    m_bUserCountProcessed = false;

    if ((static_cast<std::uint32_t>(GetRaidStatus()) & static_cast<std::uint32_t>(URS_ON_RAID)) == 0) {
        runtime::raw_atomic::DecrementI32(&g_nServerUserCount);
    }

    const std::size_t accountCount = [&] {
        L2_GUARD(accountMapSect);
        return accountMap.size();
    }();
    // if (accountCount != static_cast<std::size_t>(
    //         static_cast<std::int64_t>(g_nServerUserCount))) {
    const std::int32_t observedServerCount = g_nServerUserCount;
    if (accountCount != static_cast<std::size_t>(static_cast<std::int64_t>(observedServerCount))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] invalid user count, account_name(%s), g_nServerUserCount:(%d), accoutMap.size(%d)",
        //     L"UserSocket.cpp",
        //     0x71e,
        //     m_sAccountName.data(),
        //     g_nServerUserCount,
        //     accountCount)
        // Log.Add(
        //     LOG_WARN,
        //     "[socket] User-count observations differ during cleanup for socket %u, account %u: server count %d,
        //     account-map entries %zu, cleanup continues.", m_nId, m_nAccountID, observedServerCount, accountCount)
        L2_LOG_WARN(
            Log,
            "[socket] User-count observations differ during cleanup for socket %u, account %u: server count %d, account-map "
            "entries %zu; cleanup continues.",
            m_nId, m_nAccountID, observedServerCount, accountCount
        );
    }

    if (bWriteConnLog) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Decrease user count. g_nServerUserCount(%d)",
        //     g_nServerUserCount)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[socket] User-count cleanup for socket %u, account %u: observed server count %d.",
        //     m_nId, m_nAccountID, g_nServerUserCount)
        L2_LOG_INFO(
            Log, "[socket] User-count cleanup for socket %u, account %u: observed server count %d.", m_nId, m_nAccountID,
            g_nServerUserCount
        );
    }

    if (deletePlayingSocket) {
        g_UserSocketMapPlaying.DeleteObject(static_cast<int>(GetServerId()), const_cast<char*>("UserSocket.cpp"), 0x729);
    }
}

bool CUserSocket::SafeSend(int pendingWrite, const char* format, ...) {
    L2_GUARD_FUNCTION();
    if (!CheckPendingWriteSafe(pendingWrite)) {
        // Noise: Account name was used only by the replaced queue diagnostic. Numeric account ID identifies the connection.
        // const std::wstring accountName =
        //     ToHostWide(m_sAccountName.data())
        // Log.Add(
        //     LOG_ERROR,
        //     L"SafeSend. UserSocket pending write > %d KB, "
        //     L"magin[%d], account[%s]",
        //     0x800,
        //     pendingWrite,
        //     accountName.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     "[socket] Send skipped for socket %u, account %u: the pending-write safety check rejected it (requested margin
        //     %d bytes, limit 2097152 bytes), the handle may already be closed.", m_nId, m_nAccountID, pendingWrite)
        L2_LOG_WARN(
            Log,
            "[socket] Send skipped for socket %u, account %u: the pending-write safety check rejected it (requested margin "
            "%d bytes, limit 2097152 bytes); the handle may already be closed.",
            m_nId, m_nAccountID, pendingWrite
        );
        return false;
    }

    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
    return true;
}

bool CUserSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == std::numeric_limits<std::uint64_t>::max()) {
        // Noise: the replaced message's account-name conversion is unnecessary with numeric account ID.
        // const std::wstring accountName =
        //     ToHostWide(m_sAccountName.data())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Write to invalid user socket. account[%s] %s",
        //     "UserSocket.cpp",
        //     0x10a,
        //     accountName.c_str(),
        //     m_nCloseReason == SCR_ILLEGAL_BEHAVIOR
        //         ? L"by force close"
        //         : L"")
        // Log.Add(
        //     LOG_WARN,
        //     "[socket] Send rejected for socket %u, account %u: the connection handle is invalid. %s.",
        //     m_nId, m_nAccountID,
        //     m_nCloseReason == SCR_ILLEGAL_BEHAVIOR
        //         ? "the forced-close reason is retained"
        //         : "marking the close reason as pending write")
        L2_LOG_WARN(
            Log, "[socket] Send rejected for socket %u, account %u: the connection handle is invalid; %s.", m_nId,
            m_nAccountID,
            m_nCloseReason == SCR_ILLEGAL_BEHAVIOR ? "the forced-close reason is retained"
                                                   : "marking the close reason as pending write"
        );
        if (m_nCloseReason == SCR_ILLEGAL_BEHAVIOR) {
            return false;
        }
        // } else if (m_nPendingWrite.load(
        //                std::memory_order_relaxed) <= 0x200000) {
    } else if (const std::int32_t pendingBytes = m_nPendingWrite.load(std::memory_order_relaxed); pendingBytes <= 0x200000) {
        return true;
    } else {
        // Noise: the replaced message's account-name conversion is unnecessary with numeric account ID.
        // const std::wstring accountName =
        //     ToHostWide(m_sAccountName.data())
        // Log.Add(
        //     LOG_ERROR,
        //     L"UserSocket pending write > %d KB, Close! account[%s]",
        //     0x800,
        //     accountName.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     "[socket] Send rejected for socket %u, account %u: pending writes total %d bytes, exceeding the 2097152-byte
        //     limit, marking the close reason as pending write.", m_nId, m_nAccountID, pendingBytes)
        L2_LOG_WARN(
            Log,
            "[socket] Send rejected for socket %u, account %u: pending writes total %d bytes, exceeding the 2097152-byte "
            "limit; marking the close reason as pending write.",
            m_nId, m_nAccountID, pendingBytes
        );
        runtime::process::ReportCallStack(2, false);
    }

    m_nCloseReason = SCR_PENDING;
    return false;
}

bool CUserSocket::CheckPendingWriteSafe(int pendingWrite) const {
    if (m_hSocket.load(std::memory_order_relaxed) == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    return runtime::AddInt32Wrapping(pendingWrite, m_nPendingWrite.load(std::memory_order_relaxed)) <= 0x200000;
}

void CUserSocket::OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const {
    L2_GUARD_FUNCTION();
    if (g_bServerToUserPacketStatMonitor) {
        CPacketStat<768, 254>::Inst().Accumulate(static_cast<int>(opcode) + static_cast<int>(extendedOpcode), packetSize);
    }
}

bool CUserSocket::CheckAttendanceTime(bool sendRemainTime) {
    L2_GUARD_FUNCTION();
    if (!g_bUseAttendanceSystem || m_pUser == nullptr) {
        return false;
    }
    if (m_VipAttendnaceCheck == 1) {
        return true;
    }

    std::int32_t elapsed = 0;
    if (m_pUser->m_nUseTimeStamp > 0) {
        elapsed = runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), m_pUser->m_nUseTimeStamp);
    }
    std::int32_t elapsedMinutes = runtime::AddInt32Wrapping(elapsed, m_nAttendanceDelayTime) / 60;
    if (elapsedMinutes < 0) {
        elapsedMinutes = 0;
    }

    const std::int32_t remainingMinutes = runtime::SubtractInt32Wrapping(g_nAttendanceDelayTime, elapsedMinutes);
    if (remainingMinutes > 0) {
        if (sendRemainTime) {
            Send(
                "chcchch", 0x62, SystemMessageId::ATTENDANCE_REWARD_LOGIN_WAIT_TIME, 2, 0x15,
                runtime::Int16FromBits(static_cast<std::uint32_t>(g_nAttendanceDelayTime)), 0x15,
                runtime::Int16FromBits(static_cast<std::uint32_t>(remainingMinutes))
            );
        }
        return false;
    }

    Send("chdddddddddddS", 0xfe, 0x39, 0, 0x10e2, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"");
    runtime::raw_atomic::ExchangeI32(&m_VipAttendnaceCheck, 1);
    return true;
}

bool CUserSocket::CheckLoginTime() {
    L2_GUARD_FUNCTION();

    const int elapsedMinutes = GetSocketUseTime() / 60;
    if (elapsedMinutes > 0) {
        const int elapsedHours = elapsedMinutes / 60;
        if (elapsedMinutes == elapsedHours * 60 && m_nLastSayTime != elapsedHours) {
            m_nLastSayTime = elapsedHours;
            SendSystemMessage_d(SystemMessageId::PLAYTIME_HOURS_AND_BREAK_REMINDER, elapsedHours);
            ShowRemainTime();

            User* const user = GetUser();
            if (user != nullptr && g_nPlayTimeLimit > 0) {
                const FVector position = user->GetPos();
                const int playTime = GetSocketPlayTime();
                const char16_t* const userName = user->GetName();
                const int remainingTime = runtime::SubtractInt32Wrapping(g_nPlayTimeLimit, playTime);
                const std::uint32_t accountId = user->GetAccountID();
                const std::uint32_t userDbId = user->GetDBID();
                const std::wstring accountName =
                    Utf16ToWide(user->m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,,,,,,,,,%s,%s,,,",
                //     static_cast<std::int64_t>(0x348),
                //     static_cast<std::int64_t>(userDbId),
                //     static_cast<std::int64_t>(accountId),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(position.z)),
                //     static_cast<std::int64_t>(playTime),
                //     static_cast<std::int64_t>(remainingTime),
                //     userName,
                //     accountName.c_str())
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,,,,,,,,,%s,%s,,,",
                    static_cast<std::int64_t>(0x348), static_cast<std::int64_t>(userDbId),
                    static_cast<std::int64_t>(accountId),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                    static_cast<std::int64_t>(playTime), static_cast<std::int64_t>(remainingTime), userName,
                    accountName.c_str()
                );
            }
        }
    }
    return true;
}

void CUserSocket::Decrypt(std::uint8_t* buffer, SocketSecureKey& key, int length) {
    SocketSecureKey::KeyType& keyBytes = key.GetKey();
    std::uint8_t previousCipher = buffer[0];
    buffer[0] = static_cast<std::uint8_t>(keyBytes[0]) ^ previousCipher;

    for (int index = 1; index < length; ++index) {
        const std::uint8_t currentCipher = buffer[index];
        buffer[index] =
            static_cast<std::uint8_t>(keyBytes[static_cast<unsigned int>(index) & SocketSecureKey::CircularMask]) ^
            currentCipher ^ previousCipher;
        previousCipher = currentCipher;
    }
    key.IncreaseKey(length);
}

int CUserSocket::GetOriginalPacket(int shuffledPacket) {
    if (shuffledPacket >= 0 && static_cast<std::size_t>(shuffledPacket) < m_vShuffledPacket.size()) {
        return m_vShuffledPacket[static_cast<std::size_t>(shuffledPacket)];
    }
    return 0xd1;
}

int CUserSocket::GetOriginalExPacket(int shuffledPacket) {
    if (shuffledPacket >= 0 && static_cast<std::size_t>(shuffledPacket) < m_vShuffledExPacket.size()) {
        return m_vShuffledExPacket[static_cast<std::size_t>(shuffledPacket)];
    }
    return 0x160;
}

void CUserSocket::MaintainOriginalPacket(int packet) {
    auto position = m_vShuffledPacket.begin();
    while (position != m_vShuffledPacket.end() && *position != packet) {
        ++position;
    }
    if (position != m_vShuffledPacket.end()) {
        *position = m_vShuffledPacket[static_cast<std::size_t>(packet)];
        m_vShuffledPacket[static_cast<std::size_t>(packet)] = packet;
    }
}

void CUserSocket::MaintainOriginalExPacket(int packet) {
    auto position = m_vShuffledExPacket.begin();
    while (position != m_vShuffledExPacket.end() && *position != packet) {
        ++position;
    }
    if (position != m_vShuffledExPacket.end()) {
        *position = m_vShuffledExPacket[static_cast<std::size_t>(packet)];
        m_vShuffledExPacket[static_cast<std::size_t>(packet)] = packet;
    }
}

void CUserSocket::InitShuffledPacket() {
    m_vShuffledPacket.clear();
    m_vShuffledPacket.resize(0xd1);
    for (int packet = 0; packet < 0xd1; ++packet) {
        m_vShuffledPacket[static_cast<std::size_t>(packet)] = packet;
    }

    m_vShuffledExPacket.clear();
    m_vShuffledExPacket.resize(0x160);
    for (int packet = 0; packet < 0x160; ++packet) {
        m_vShuffledExPacket[static_cast<std::size_t>(packet)] = packet;
    }
}

void CUserSocket::MakeShuffledPacket(int randomSeed) {
    static int aMaintainPacket[0xd1] = {
        0x11,
        0x12,
        0xb1,
        0xd0,
        0xd1,
    };
    static int aMaintainExPacket[0x160] = {
        0x70,
        0x71,
        0x160,
    };

    InitShuffledPacket();
    PacketShuffler shuffler(static_cast<unsigned int>(randomSeed));
    shuffler.ShufflePacket(m_vShuffledPacket, static_cast<unsigned int>(m_vShuffledPacket.size()));
    shuffler.ShufflePacket(m_vShuffledExPacket, static_cast<unsigned int>(m_vShuffledExPacket.size()));

    for (int index = 0; index < 0xd1 && aMaintainPacket[index] != 0xd1; ++index) {
        MaintainOriginalPacket(aMaintainPacket[index]);
    }
    for (int index = 0; index < 0x160 && aMaintainExPacket[index] != 0x160; ++index) {
        MaintainOriginalExPacket(aMaintainExPacket[index]);
    }
    SetShuffleNum(randomSeed);
}

void CUserSocket::GetShuffleSeedAndPublicKey(int& randomSeed, std::uint64_t& publicKey) {
    publicKey = ::genrand64_int64();
    randomSeed = 0;
    if (g_bEnablePacketShuffle) {
        double generatedSeed = ::genrand64_real2();
        generatedSeed *= 2147483647.0;
        generatedSeed += 1.0;
        randomSeed = runtime::Win64CvttDoubleToInt32(generatedSeed);
        MakeShuffledPacket(randomSeed);
    }
}

void CUserSocket::SendSystemMessage(const char16_t* context, const char16_t* message) {
    L2_GUARD_FUNCTION();
    if (!message) {
        return;
    }

    const std::u16string_view targetMessage(message);

    int remaining = static_cast<int>(targetMessage.size());
    std::size_t cursor = 0;
    std::size_t buffered = 0;
    std::array<char16_t, 256> buffer{};
    const auto unitAt = [&targetMessage](std::size_t index) noexcept {
        return index < targetMessage.size() ? targetMessage[index] : u'\0';
    };

    while (remaining > 0) {
        --remaining;
        std::size_t next = cursor + 1;
        const char16_t current = unitAt(cursor);
        if (current == u'\r') {
            if (unitAt(next) == u'\n') {
                ++next;
            }
            if (buffered > 0) {
                Send("cddSdS", 0x4a, 0, 5, context, -1, buffer.data());
                buffered = 0;
                buffer.fill(u'\0');
            }
        } else {
            buffer[buffered] = current;
            ++buffered;
            if (buffered > 199) {
                Send("cddSdS", 0x4a, 0, 5, context, -1, buffer.data());
                buffered = 0;
                buffer.fill(u'\0');
            }
        }
        cursor = next;
    }

    if (buffered > 0) {
        Send("cddSdS", 0x4a, 0, 5, context, -1, buffer.data());
    }
}

void CUserSocket::SendSystemMessage_Announce(const char16_t* message) {
    L2_GUARD_FUNCTION();
    Send("cddSdS", 0x4a, 0, 10, u"", -1, message);
}

void CUserSocket::SendSystemMessage(unsigned int messageId) {
    L2_GUARD_FUNCTION();
    Send("chc", 0x62, runtime::Int16FromBits(messageId), 0);
}

void CUserSocket::SendSystemMessage_Char(unsigned int messageId, const char16_t* value) {
    L2_GUARD_FUNCTION();
    Send("chccS", 0x62, runtime::Int16FromBits(messageId), 1, 0x0c, value);
}

void CUserSocket::SendSystemMessage_S(unsigned int messageId, const char16_t* value) {
    L2_GUARD_FUNCTION();
    Send("chccS", 0x62, runtime::Int16FromBits(messageId), 1, 0, value);
}

void CUserSocket::SendSystemMessage_d(unsigned int messageId, int value) {
    L2_GUARD_FUNCTION();
    Send("chccd", 0x62, runtime::Int16FromBits(messageId), 1, 1, value);
}

void CUserSocket::SendSystemMessage_SysString(unsigned int messageId, unsigned int value) {
    L2_GUARD_FUNCTION();
    Send("chcch", 0x62, runtime::Int16FromBits(messageId), 1, 0x0d, runtime::Int16FromBits(value));
}

void CUserSocket::SendSystemMessage_Q(unsigned int messageId, std::int64_t value) {
    L2_GUARD_FUNCTION();
    Send("chccQ", 0x62, runtime::Int16FromBits(messageId), 1, 6, value);
}

void CUserSocket::SendSystemMessage_S_d(unsigned int messageId, const char16_t* stringValue, int integerValue) {
    L2_GUARD_FUNCTION();
    Send("chccScd", 0x62, runtime::Int16FromBits(messageId), 2, 0, stringValue, 1, integerValue);
}

void CUserSocket::SendSystemMessage_Friend(unsigned int messageId, const char16_t* value) {
    L2_GUARD_FUNCTION();
    Send("cddSdS", 0x4a, 0, 0x18, u"", messageId, value);
}

void CUserSocket::SendSystemMessage_i(unsigned int messageId, int value) {
    L2_GUARD_FUNCTION();
    Send("chcch", 0x62, runtime::Int16FromBits(messageId), 1, 0x0d, value);
}

void CUserSocket::SetApplyChinaFatigueSystem(bool apply) {
    {
        L2_GUARD(mLock);
        m_bApplyChinaFatigueSystem = apply;
    }
}

int CUserSocket::GetPlayTime_china() {
    const int playTime = [&] {
        L2_GUARD(mLock);
        int playTime = 0;
        if (!m_bApplyChinaFatigueSystem || g_nChinaFatigueSystemType < 2) {
            playTime = m_nPlayTime_china;
        } else if (m_nStartPlayTimeStamp_china >= 1) {
            playTime = runtime::AddInt32Wrapping(
                runtime::SubtractInt32Wrapping(m_nPlayTime_china, m_nStartPlayTimeStamp_china), CurrentTime64Low32()
            );
        }
        return playTime;
    }();
    return playTime > 0 ? playTime : 0;
}

void CUserSocket::SetRestTime_china(int restTime) {
    {
        L2_GUARD(mLock);
        m_nRestTime_china = restTime;
    }
}

void CUserSocket::SetPlayTime_china(int playTime) {
    {
        L2_GUARD(mLock);
        if (m_bApplyChinaFatigueSystem && g_nChinaFatigueSystemType > 1) {
            m_nStartPlayTimeStamp_china = CurrentTime64Low32();
        }
        m_nPlayTime_china = playTime;
    }
}

void CUserSocket::ShowFatigueStatus_china() {
    const int playTime = GetPlayTime_china();
    const int playRemainder = playTime % 3600;
    const int restRemainder = m_nRestTime_china % 3600;
    Send(
        "chcchccchcc", 0x62, SystemMessageId::ACCUMULATED_PLAY_AND_REST_TIME, 4, 0x15,
        runtime::Int16FromBits(static_cast<std::uint32_t>(playTime / 3600)), 0x14,
        runtime::Int8FromBits(static_cast<std::uint32_t>(playRemainder / 60)), 0x15,
        runtime::Int16FromBits(static_cast<std::uint32_t>(m_nRestTime_china / 3600)), 0x14,
        runtime::Int8FromBits(static_cast<std::uint32_t>(restRemainder / 60))
    );
}

void CUserSocket::SetSitTimeStart() {
    if (g_nPlayTimeLimit != 0 && m_nSitTimeStamp == 0) {
        CIOObject::SetTimeStamp(&m_nSitTimeStamp);
        if (g_nPlayTimeLimit > 0) {
            SendSystemMessage(SystemMessageId::PLAYTIME_NO_LONGER_CONSUMED);
            ShowRemainTime();
        }
    }
}

void CUserSocket::SetEndSitTimeStart() {
    if (g_nPlayTimeLimit == 0) {
        return;
    }

    if (m_nSitTimeStamp > 0) {
        const int currentTime = runtime::crt::Time32Now();
        const int sitTimeStamp = m_nSitTimeStamp;
        m_nSitTimeStamp = 0;
        m_nSitTime = runtime::AddInt32Wrapping(m_nSitTime, runtime::SubtractInt32Wrapping(currentTime, sitTimeStamp));
        if (g_nPlayTimeLimit < 1) {
            return;
        }
        SendSystemMessage(SystemMessageId::PLAYTIME_NOW_CONSUMED);
    }
    if (g_nPlayTimeLimit > 0) {
        ShowRemainTime();
    }
}

void CUserSocket::ShowRemainTime() {
    if ((g_nCountryCode != 4 && g_bFreeOpenServer) || g_nPlayTimeLimit < 1) {
        return;
    }

    const int remaining = runtime::SubtractInt32Wrapping(g_nPlayTimeLimit, GetSocketPlayTime());
    const int hours = remaining / 3600;
    const int minutes = (remaining % 3600) / 60;
    if (hours < 1) {
        Send(
            "chccc", 0x62, SystemMessageId::WEEKLY_USAGE_MINUTES_REMAINING, 1, 0x14,
            runtime::Int8FromBits(static_cast<std::uint32_t>(minutes))
        );
        return;
    }
    Send(
        "chcchcc", 0x62, SystemMessageId::WEEKLY_USAGE_HOURS_AND_MINUTES_REMAINING, 2, 0x15,
        runtime::Int16FromBits(static_cast<std::uint32_t>(hours)), 0x14,
        runtime::Int8FromBits(static_cast<std::uint32_t>(minutes))
    );
}

int CUserSocket::GetSitTime() {
    if (m_nSitTimeStamp < 1) {
        return m_nSitTime;
    }
    return runtime::AddInt32Wrapping(
        runtime::SubtractInt32Wrapping(m_nSitTime, m_nSitTimeStamp), runtime::crt::Time32Now()
    );
}

void CUserSocket::SetLastSavePlayTime(_SYSTEMTIME* time) {
    std::memcpy(&m_tmLastSaveWeekPlayTime, time, sizeof(m_tmLastSaveWeekPlayTime));
}

bool CUserSocket::CheckWeekChanged(_SYSTEMTIME* time) {
    if (time->wYear == m_tmLastSaveWeekPlayTime.wYear && time->wMonth == m_tmLastSaveWeekPlayTime.wMonth &&
        time->wDay == m_tmLastSaveWeekPlayTime.wDay) {
        return false;
    }

    if (time->wDayOfWeek != 5) {
        const int dayDifference = GetDayDiff(time, &m_tmLastSaveWeekPlayTime);
        if (dayDifference < 7 &&
            (static_cast<unsigned int>(m_tmLastSaveWeekPlayTime.wDayOfWeek) + 2U) % 7U <=
                (static_cast<unsigned int>(time->wDayOfWeek) + 2U) % 7U) {
            return false;
        }
    }
    return true;
}

void CUserSocket::SetUseTime(int seconds) { m_nUseTimeSec = runtime::AddInt32Wrapping(m_nUseTimeSec, seconds); }

void CUserSocket::SetLeftTime(int leftTime, int userStat, int warnFlag, char mode, int reservations) {
    m_nLeftTime = static_cast<std::uint32_t>(leftTime);
    m_nUserStat = static_cast<std::uint32_t>(userStat);
    m_nWarnFlag = static_cast<std::uint32_t>(warnFlag);
    if (mode == 0) {
        if (m_nUserStat < 1000U) {
            m_nPayKind = 2 + (leftTime > 0 ? 1 : 0);
        } else {
            m_nPayKind = leftTime > 0 ? 1 : 0;
        }
        return;
    }
    if (mode == 1) {
        m_nPayKind = 4;
        m_nReservations = reservations;
    }
}

void CUserSocket::ChangeTimeLeft(int leftTime, int reservations) {
    m_nLeftTime = static_cast<std::uint32_t>(leftTime);
    m_nReservations = reservations;
    m_nLeftTimeStamp = runtime::crt::Time32Now();
}

void CUserSocket::ChangeReservation(int reservations) { m_nReservations = reservations; }

void CUserSocket::SetSocketUseStart() {
    m_nSocketUseTimeStamp = runtime::crt::Time32Now();
    m_nSocketCloseTimer = 0;
}

std::int32_t CUserSocket::GetSocketUseTime() {
    if (m_nSocketUseTimeStamp < 1) {
        return 0;
    }
    return runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), m_nSocketUseTimeStamp);
}

std::int32_t CUserSocket::GetSocketCharacterUseTime() {
    if (m_nSocketCharacterTimeStamp < 1) {
        return 0;
    }
    return runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), m_nSocketCharacterTimeStamp);
}

void CUserSocket::SetSocketCharacterUseTime() { m_nSocketCharacterTimeStamp = runtime::crt::Time32Now(); }

int CUserSocket::ClearSocketCharacterUseTime() {
    const int sitTime = GetSitTime();
    const int characterUseTime = GetSocketCharacterUseTime();
    m_nSocketCharacterTimeStamp = 0;
    m_nSitTime = 0;
    m_nWeekPlayTime =
        runtime::AddInt32Wrapping(m_nWeekPlayTime, runtime::SubtractInt32Wrapping(characterUseTime, sitTime));
    return m_nWeekPlayTime;
}

void CUserSocket::SetCloseTimer() {
    if (m_nSocketCloseTimer < 1) {
        m_nSocketCloseTimer = runtime::crt::Time32Now();
        return;
    }

    // Noise: Account name preparation belonged only to the extra timer message.
    // const std::wstring accountName =
    //     ToHostWide(m_sAccountName.data())
    // Noise: Repeated setup leaves the existing close timestamp unchanged. No failure or new timer is created.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[%S][%d] already set close timer. [%d], [%d][%s]",
    //     "UserSocket.cpp",
    //     0x3051,
    //     m_nSocketCloseTimer,
    //     runtime::Int32FromBits(m_nAccountID),
    //     accountName.c_str())
}

void CUserSocket::MakePacketBlockList() {
    // Current L2Server 0x00c0f6cc.
    L2_GUARD_FUNCTION();

    std::fill_n(aPacketActionFailTable, 209, false);
    std::fill_n(aPacketAllowTable_ForRaidUser, 209, false);
    std::fill_n(aPacketBlockTable_CannotControl, 209, false);
    std::fill_n(aPacketBlockTable_ForRaidUser, 255, false);
    std::fill_n(aPacketExBlockTable_ForRaidUser, 514, false);

    for (std::size_t index = 0; index < kCannotControlPacketList.size(); ++index) {
        const int cannotControl = kCannotControlPacketList[index];
        if (cannotControl > 0) {
            aPacketBlockTable_CannotControl[cannotControl] = true;
        }

        const int raidAllowed = kRaidAllowedPacketList[index];
        if (raidAllowed >= 0) {
            aPacketAllowTable_ForRaidUser[raidAllowed] = true;
        }

        const int actionFail = kActionFailPacketList[index];
        if (actionFail > 0) {
            aPacketActionFailTable[actionFail] = true;
        }
    }

    for (const int packet : kRaidAllowedExPacketList) {
        if (packet >= 0) {
            aPacketExAllowTable_ForRaidUser[packet] = true;
        }
    }
    for (const int packet : kRaidBlockedPacketList) {
        if (packet > 0) {
            aPacketBlockTable_ForRaidUser[packet] = true;
        }
    }
    for (const int packet : kRaidBlockedExPacketList) {
        if (packet > 0) {
            aPacketExBlockTable_ForRaidUser[packet] = true;
        }
    }
}

void CUserSocket::InitCheckPacketAccept() {
    m_lastPacketReadTick = runtime::GetTickCount();
    m_SumCount = 0;
    m_PacketReadCount = 0;
    m_SumPacketReadCountPerTick = 0;
    m_MaxPacketReadCountPerTick = 0;
}

bool CUserSocket::CheckMaxPacketAccept(int packetNo) {
    // Current L2Server 0x00c0fb80.
    L2_GUARD_FUNCTION();
    if (!aPacketBlockTable_CannotControl[packetNo]) {
        return true;
    }

    const std::uint32_t now = runtime::GetTickCount();
    const std::uint32_t elapsed = now - m_lastPacketReadTick;
    if (elapsed < 1000U) {
        m_PacketReadCount = runtime::AddInt32Wrapping(m_PacketReadCount, 1);
        return true;
    }

    m_lastPacketReadTick = now;
    const std::uint32_t countBits = static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(m_PacketReadCount, 1000));
    const int packetRate = static_cast<int>(countBits / elapsed);
    m_MaxPacketReadCountPerTick = std::max(m_MaxPacketReadCountPerTick, packetRate);
    m_SumPacketReadCountPerTick = runtime::AddInt32Wrapping(m_SumPacketReadCountPerTick, packetRate);
    m_SumCount = runtime::AddInt32Wrapping(m_SumCount, 1);
    m_PacketReadCount = 0;
    return true;
}

void CUserSocket::ReportPacketLog(bool force) {
    L2_GUARD_FUNCTION();
    if (!force) {
        if (!g_bWritePacketLog) {
            return;
        }
        const std::uint32_t reportThreshold = runtime::GetTickCount() - 600000U;
        if (reportThreshold <= m_nLastPacketLogTick) {
            return;
        }
    }

    const std::uint64_t totalPacketCount = m_nPacketConunt - std::uint64_t{1};
    std::array<std::int32_t, 0x80> packetList{};
    std::array<std::int32_t, 0x80> packetTime{};
    std::memcpy(packetList.data(), m_nPacketList.data(), sizeof(packetList));
    std::memcpy(packetTime.data(), m_nPacketTime.data(), sizeof(packetTime));

    std::uint32_t characterId = 0;
    std::array<SharedUtilsWideChar, 0x18> characterName{};
    User* const user = GetUser();
    if (user != nullptr) {
        characterId = user->GetDBID();
        const char16_t* const sourceName = user->GetName();
        if (sourceName == nullptr) {
            characterName[0] = SharedUtilsWideChar{};
            errno = EINVAL;
            myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
        } else {
#if defined(_WIN32)
            std::size_t length = 0;
            while (length < characterName.size() && sourceName[length] != L'\0') {
                ++length;
            }
            if (length == characterName.size()) {
                characterName[0] = SharedUtilsWideChar{};
                errno = ERANGE;
                myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
            } else {
                std::memcpy(characterName.data(), sourceName, (length + 1U) * sizeof(SharedUtilsWideChar));
            }
#else
            const std::u16string targetName = std::u16string(sourceName);
            if (targetName.size() >= characterName.size()) {
                characterName[0] = SharedUtilsWideChar{};
                errno = ERANGE;
                myInvalidParameterHandler(nullptr, nullptr, nullptr, 0, 0);
            } else {
                std::memcpy(
                    characterName.data(), targetName.c_str(), (targetName.size() + 1U) * sizeof(SharedUtilsWideChar)
                );
            }
#endif
        }
    }

    const std::wstring accountName = Utf16ToWide(m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    const std::wstring hostCharacterName = Utf16ToWide(characterName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[PLog] Account[%d:%s] Char[%d:%s] "
    //     L"Total Packet Count[%I64d]",
    //     runtime::Int32FromBits(m_nAccountID),
    //     accountName.c_str(),
    //     runtime::Int32FromBits(characterId),
    //     hostCharacterName.c_str(),
    //     runtime::Int64FromBits(totalPacketCount))
    // Log.Add(
    //     LOG_ERROR,
    //     L"[packet history] Socket %u, account %s (%u), character %s (DB ID %u): ring sequence %llu. Reporting 128 slots in
    //     reverse order, including any unused slots.", m_nId, accountName.c_str(), m_nAccountID, hostCharacterName.c_str(),
    //     characterId, static_cast<unsigned long long>(totalPacketCount))
    L2_LOG_ERROR(
        Log,
        u"[packet history] Socket %u, account %s (%u), character %s (DB ID %u): ring sequence %llu; reporting 128 slots in "
        u"reverse order, including any unused slots.",
        m_nId, accountName.c_str(), m_nAccountID, hostCharacterName.c_str(), characterId,
        static_cast<unsigned long long>(totalPacketCount)
    );

    std::uint32_t packetCursor = static_cast<std::uint32_t>(totalPacketCount);
    for (int reportIndex = 0; reportIndex < 0x80; ++reportIndex) {
        const std::size_t index = static_cast<std::size_t>(packetCursor & 0x7fU);
        std::tm* const packetLocalTime = runtime::crt::LocalTime32(packetTime[index]);
        std::uint32_t packedPacket = 0;
        std::memcpy(&packedPacket, &packetList[index], sizeof(packedPacket));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLog]_%s_%3d. %4d-%2d-%2d %2d:%2d:%2d "
        //     L"C[%3d] C_EX[%5d]",
        //     hostCharacterName.c_str(),
        //     reportIndex,
        //     packetLocalTime->tm_year + 1900,
        //     packetLocalTime->tm_mon + 1,
        //     packetLocalTime->tm_mday,
        //     packetLocalTime->tm_hour,
        //     packetLocalTime->tm_min,
        //     packetLocalTime->tm_sec,
        //     runtime::Int16FromBits(packedPacket >> 16U),
        //     static_cast<int>(packedPacket & 0xffffU))
        if (packetLocalTime == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[packet history] Socket %u, character %s, row %d: recorded epoch %d could not be converted to server
            //     local time. Basic opcode %d, extended opcode %d.", m_nId, hostCharacterName.c_str(), reportIndex,
            //     packetTime[index], runtime::Int16FromBits(packedPacket >> 16U), static_cast<int>(packedPacket &
            //     0xffffU))
            L2_LOG_ERROR(
                Log,
                u"[packet history] Socket %u, character %s, row %d: recorded epoch %d could not be converted to server "
                u"local time; basic opcode %d, extended opcode %d.",
                m_nId, hostCharacterName.c_str(), reportIndex, packetTime[index],
                runtime::Int16FromBits(packedPacket >> 16U), static_cast<int>(packedPacket & 0xffffU)
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[packet history] Socket %u, character %s, row %d: recorded %04d-%02d-%02d %02d:%02d:%02d server local
            //     time. Basic opcode %d, extended opcode %d.", m_nId, hostCharacterName.c_str(), reportIndex,
            //     packetLocalTime->tm_year + 1900, packetLocalTime->tm_mon + 1,
            //     packetLocalTime->tm_mday, packetLocalTime->tm_hour,
            //     packetLocalTime->tm_min, packetLocalTime->tm_sec,
            //     runtime::Int16FromBits(packedPacket >> 16U),
            //     static_cast<int>(packedPacket & 0xffffU))
            L2_LOG_ERROR(
                Log,
                u"[packet history] Socket %u, character %s, row %d: recorded %04d-%02d-%02d %02d:%02d:%02d server local "
                u"time; basic opcode %d, extended opcode %d.",
                m_nId, hostCharacterName.c_str(), reportIndex, packetLocalTime->tm_year + 1900, packetLocalTime->tm_mon + 1,
                packetLocalTime->tm_mday, packetLocalTime->tm_hour, packetLocalTime->tm_min, packetLocalTime->tm_sec,
                runtime::Int16FromBits(packedPacket >> 16U), static_cast<int>(packedPacket & 0xffffU)
            );
        }
        --packetCursor;
    }

    m_nLastPacketLogTick = runtime::GetTickCount();
}

void CUserSocket::SaveINI(int type, std::string value) {
    L2_GUARD_FUNCTION();
    if (g_bProcessClientINI) {
        std::array<char, 0x5000>& destination = m_szClientINI[static_cast<std::size_t>(type)];
        std::memset(destination.data(), 0, destination.size());
        std::memcpy(destination.data(), value.data(), value.size());
        m_bNeedUpdateClientINI[static_cast<std::size_t>(type)] = true;
    }
}

void CUserSocket::SaveINIToDB() {
    L2_GUARD_FUNCTION();
    if (!g_bProcessClientINI) {
        return;
    }

    for (int type = 0; type < 3; ++type) {
        const std::size_t index = static_cast<std::size_t>(type);
        if (m_bNeedUpdateClientINI[index]) {
            db.RequestSaveClientINI(
                runtime::Int32FromBits(m_nAccountID), static_cast<char>(type), std::string(m_szClientINI[index].data())
            );
        }
    }
}

std::int32_t CUserSocket::SetLastUsePacket(int packetId, bool checkUser) {
    L2_GUARD_FUNCTION();
    if (checkUser && GetUser() == nullptr) {
        return runtime::raw_atomic::LoadI32Relaxed(&m_nLastUsePacketId);
    }

    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_nLastUsePacketId, packetId);
    m_nLastActionStamp = runtime::crt::Time32Now();
    return previous;
}

void CUserSocket::ForceClose(SOCKET_CLOSE_REASON reason) {
    L2_GUARD_FUNCTION();
    m_nCloseReason = reason;
    Close();
}

void CUserSocket::ResetRelaxTime(_SYSTEMTIME* time) {
    L2_GUARD_FUNCTION();
    m_nWeekPlayTime = 0;
    m_nSitTime = 0;
    if (m_nSitTimeStamp > 0) {
        m_nSitTimeStamp = 0;
        CIOObject::SetTimeStamp(&m_nSitTimeStamp);
    }
    if (m_nSocketCharacterTimeStamp > 0) {
        m_nSocketCharacterTimeStamp = 0;
        m_nSocketCharacterTimeStamp = runtime::crt::Time32Now();
    }
    std::memcpy(&m_tmLastSaveWeekPlayTime, time, sizeof(m_tmLastSaveWeekPlayTime));
}

bool CUserSocket::CheckPremiumType(PremiumUserType::Enum type) {
    L2_GUARD_FUNCTION();
    return static_cast<int>(type) == m_iPremiumLevel;
}

int CUserSocket::GetAge() { return g_bIsNonPKServer ? 0 : m_nAge; }

void CUserSocket::SetAge(int age) { m_nAge = age; }

bool CUserSocket::IsReadBBSBlocked() { return ((m_nWarnFlag >> 4U) & 1U) != 0; }

bool CUserSocket::IsWriteBBSBlocked() { return ((m_nWarnFlag >> 4U) & 1U) != 0 || (m_nWarnFlag & 8U) != 0; }

void CUserSocket::SetWebSessionId(char* webSessionId) {
    L2_GUARD_FUNCTION();
    m_wsidReceivedTick = runtime::GetTickCount();
    std::memcpy(m_WebSessionId.data(), webSessionId, m_WebSessionId.size());
}

int CUserSocket::GetCurrentLeftTime() {
    if (m_nLeftTimeStamp < 1) {
        return runtime::Int32FromBits(m_nLeftTime);
    }
    return runtime::Int32FromBits(
        m_nLeftTime - static_cast<std::uint32_t>(runtime::crt::Time32Now()) + static_cast<std::uint32_t>(m_nLeftTimeStamp)
    );
}

bool CUserSocket::IsDormantUser() {
    L2_GUARD_FUNCTION();
    if (m_nDormantType != 3) {
        return false;
    }
    if (m_nDormantMarkingTime == -1) {
        return true;
    }
    if (m_nDormantMarkingTime < 1) {
        return false;
    }

    const std::int32_t elapsed = runtime::SubtractInt32Wrapping(CurrentTime64Low32(), m_nDormantMarkingTime);
    if (runtime::MultiplyInt32Wrapping(g_DormantPostponeTime, 3600) < elapsed) {
        std::memset(&m_nDormantMarkingTime, 0, 0x08);
        return false;
    }
    return true;
}

bool CUserSocket::IsNewbieUser() {
    L2_GUARD_FUNCTION();
    if (m_nDormantType != 2) {
        return false;
    }
    if (m_nDormantMarkingTime == -1) {
        return true;
    }
    if (m_nDormantMarkingTime < 1) {
        return false;
    }

    const std::int32_t elapsed = runtime::SubtractInt32Wrapping(CurrentTime64Low32(), m_nDormantMarkingTime);
    if (runtime::MultiplyInt32Wrapping(g_NewbiePostponeTime, 3600) < elapsed) {
        std::memset(&m_nDormantMarkingTime, 0, 0x08);
        return false;
    }
    return true;
}

void CUserSocket::SetDormantUser(bool enabled, int dormantType) {
    if (enabled) {
        m_nDormantMarkingTime = -1;
        m_nDormantType = dormantType;
        return;
    }
    m_nDormantMarkingTime = 0;
    m_nDormantType = 0;
}

void CUserSocket::SetDormantMarkingTime(int markingTime, int dormantType) {
    m_nDormantMarkingTime = markingTime;
    if (dormantType >= 0) {
        m_nDormantType = dormantType;
    }
}

bool CUserSocket::CanDoAction_china() {
    const int fullThreshold = runtime::MultiplyInt32Wrapping(g_ChinaFullFatigueTime, 3600);
    const int halfThreshold = runtime::MultiplyInt32Wrapping(g_ChinaHalfFatigueTime, 3600);
    if (m_bApplyChinaFatigueSystem && g_nChinaFatigueSystemType > 1 &&
        (GetPlayTime_china() > fullThreshold ||
            (m_bApplyChinaFatigueSystem && GetPlayTime_china() <= fullThreshold && GetPlayTime_china() > halfThreshold &&
                (std::rand() & 1) == 0))) {
        return false;
    }
    return true;
}

int CUserSocket::GetSocketPlayTime() {
    const int sitTime = GetSitTime();
    const int characterUseTime = GetSocketCharacterUseTime();
    const int currentPlayTime = runtime::SubtractInt32Wrapping(characterUseTime, sitTime);
    const int totalPlayTime = runtime::AddInt32Wrapping(currentPlayTime, m_nWeekPlayTime);
    if (totalPlayTime >= 0) {
        return totalPlayTime;
    }

    User* const user = GetUser();
    if (user && !user->IsBuilder()) {
        const std::wstring accountName = Utf16ToWide(m_sAccountName.data(), Utf16WidePolicy::PreserveInvalidOnWindows);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] account(%s[%d]) total play([%d]sec), "
        //     L"sit([%d]sec), current play[%d]",
        //     "UserSocket.cpp",
        //     0x2fa4,
        //     accountName.c_str(),
        //     runtime::Int32FromBits(m_nAccountID),
        //     totalPlayTime,
        //     sitTime,
        //     currentPlayTime)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[play time] Socket %u, account %s (%u): calculated total play time is negative (%d seconds). Returning 0
        //     seconds. Observed sitting time %d, current play time %d, character-session elapsed time %d seconds.", m_nId,
        //     accountName.c_str(), m_nAccountID, totalPlayTime, sitTime, currentPlayTime, characterUseTime)
        L2_LOG_ERROR(
            Log,
            u"[play time] Socket %u, account %s (%u): calculated total play time is negative (%d seconds); returning 0 "
            u"seconds. Observed sitting time %d, current play time %d, character-session elapsed time %d seconds.",
            m_nId, accountName.c_str(), m_nAccountID, totalPlayTime, sitTime, currentPlayTime, characterUseTime
        );
    }
    return 0;
}

int CUserSocket::GetCharacterPlayTime() {
    const int sitTime = GetSitTime();
    const int characterUseTime = GetSocketCharacterUseTime();
    const int currentPlayTime = runtime::SubtractInt32Wrapping(characterUseTime, sitTime);
    if (currentPlayTime >= 0) {
        return currentPlayTime;
    }

    User* const user = GetUser();
    if (user && !user->IsBuilder()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]accountId([%d]) total use([%d]) "
        //     L"sit([%d]), unsaved char play[%d]",
        //     "UserSocket.cpp",
        //     0x2fbb,
        //     runtime::Int32FromBits(m_nAccountID),
        //     runtime::AddInt32Wrapping(m_nWeekPlayTime, currentPlayTime),
        //     sitTime,
        //     currentPlayTime)
        // Log.Add(
        //     LOG_ERROR,
        //     "[play time] Socket %u, account %u: calculated current character play time is negative. Returning 0 seconds.
        //     Observed total including saved time %d, sitting time %d, current play time %d, character-session elapsed time
        //     %d seconds.", m_nId, m_nAccountID, runtime::AddInt32Wrapping(m_nWeekPlayTime, currentPlayTime), sitTime,
        //     currentPlayTime, characterUseTime)
        L2_LOG_ERROR(
            Log,
            "[play time] Socket %u, account %u: calculated current character play time is negative; returning 0 seconds. "
            "Observed total including saved time %d, sitting time %d, current play time %d, character-session elapsed time "
            "%d seconds.",
            m_nId, m_nAccountID, runtime::AddInt32Wrapping(m_nWeekPlayTime, currentPlayTime), sitTime, currentPlayTime,
            characterUseTime
        );
    }
    return 0;
}

std::u16string CUserSocket::GetMacAddressString() {
    L2_GUARD_FUNCTION();
    const std::uint64_t bits = static_cast<std::uint64_t>(m_nMacAddress);
    std::array<char16_t, 1000> buffer{};
    FormatUtf16To(
        buffer.data(), 999, u"%02X-%02X-%02X-%02X-%02X-%02X", static_cast<unsigned int>((bits >> 40U) & 0xffU),
        static_cast<unsigned int>((bits >> 32U) & 0xffU), static_cast<unsigned int>((bits >> 24U) & 0xffU),
        static_cast<unsigned int>((bits >> 16U) & 0xffU), static_cast<unsigned int>((bits >> 8U) & 0xffU),
        static_cast<unsigned int>(bits & 0xffU)
    );
    return std::u16string(buffer.data());
}

void CUserSocket::AccumulateWorldChatPacket(int count, std::uint32_t size) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI64(&g_WorldChatPacketSendCount, static_cast<std::int64_t>(count));
    runtime::raw_atomic::AddI64(&g_WorldChatPacketSendSize, static_cast<std::int64_t>(size));
}

// Current C02 alliance ingress slice. These remain free UserSocket.cpp
// packet owners, matching the shipping callback-table structure.

bool AllianceInfoPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c20948.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    CAlliance* const alliance = pledge != nullptr ? pledge->GetAlliance() : nullptr;
    if (alliance == nullptr) {
        socket->SendSystemMessage(SystemMessageId::NOT_IN_ALLIANCE);
        return false;
    }

    int totalDatabaseMembers = 0;
    int totalLiveMembers = 0;
    int memberPledgeCount = 0;
    int memberBlobLength = 0;
    const char16_t* const allianceName = alliance->m_data->m_sAllianceName;
    std::u16string targetMasterPledgeName;
    std::u16string targetMasterName;
    std::array<char, 0x5000> memberBlob;
    std::vector<CPledge*> memberPledges;
    alliance->GetAllMemberPledge(memberPledges);

    for (CPledge* const memberPledge : memberPledges) {
        const char16_t* const pledgeName = memberPledge->GetPledgeName();
        const char16_t* const pledgeMasterName = memberPledge->GetPledgeMasterName();
        const std::u16string targetPledgeName = std::u16string(pledgeName);
        const std::u16string targetPledgeMasterName = std::u16string(pledgeMasterName);
        const int databaseMemberCount = memberPledge->GetDbMemberCount();
        const int liveMemberCount = memberPledge->GetMemberCount();
        if (memberPledge->GetDbId() == alliance->m_data->m_nMasterPledgeDbId) {
            targetMasterPledgeName = targetPledgeName;
            targetMasterName = targetPledgeMasterName;
        }

        const std::uint32_t pledgeLevelBits = memberPledge->GetPledgeLevel();
        const int pledgeStatus = memberPledge->GetStatus();
        const int assembledLength = ::Assemble(
            memberBlob.data() + memberBlobLength, runtime::SubtractInt32Wrapping(0x5000, memberBlobLength), "SddSdd",
            targetPledgeName.c_str(), pledgeStatus, runtime::Int32FromBits(pledgeLevelBits),
            targetPledgeMasterName.c_str(), databaseMemberCount, liveMemberCount
        );
        memberBlobLength = runtime::AddInt32Wrapping(memberBlobLength, assembledLength);
        totalDatabaseMembers = runtime::AddInt32Wrapping(totalDatabaseMembers, databaseMemberCount);
        totalLiveMembers = runtime::AddInt32Wrapping(totalLiveMembers, liveMemberCount);
        memberPledgeCount = runtime::AddInt32Wrapping(memberPledgeCount, 1);
    }

    if (memberPledgeCount != 0) {
        socket->Send(
            "cSddSSdb", 0xb5, allianceName, totalDatabaseMembers, totalLiveMembers, targetMasterPledgeName.c_str(),
            targetMasterName.c_str(), memberPledgeCount, memberBlobLength, memberBlob.data()
        );
    } else {
        socket->Send(
            "cSddSSd", 0xb5, allianceName, totalDatabaseMembers, totalLiveMembers, targetMasterPledgeName.c_str(),
            targetMasterName.c_str(), 0
        );
    }

    socket->SendSystemMessage(SystemMessageId::ALLIANCE_INFORMATION_HEADING);
    socket->SendSystemMessage_S(SystemMessageId::ALLIANCE_NAME_ENTRY, allianceName);
    socket->Send(
        "chccScS", 0x62, SystemMessageId::ALLIANCE_LEADER_AND_CLAN_ENTRY, 2, 0, targetMasterPledgeName.c_str(), 0,
        targetMasterName.c_str()
    );
    socket->Send(
        "chcchch", 0x62, SystemMessageId::ONLINE_AND_TOTAL_MEMBER_COUNTS, 2, 0x15,
        runtime::Int16FromBits(static_cast<std::uint32_t>(totalLiveMembers)), 0x15,
        runtime::Int16FromBits(static_cast<std::uint32_t>(totalDatabaseMembers))
    );
    socket->SendSystemMessage_d(SystemMessageId::ALLIANCE_CLAN_COUNT_ENTRY, memberPledgeCount);
    socket->SendSystemMessage(SystemMessageId::AFFILIATED_CLAN_INFORMATION_HEADING);

    const std::uint8_t* memberCursor = reinterpret_cast<const std::uint8_t*>(memberBlob.data());
    for (int index = 0; index < memberPledgeCount; ++index) {
        std::array<char16_t, 28> pledgeName;
        std::array<char16_t, 28> pledgeMasterName;
        std::memset(pledgeName.data(), 0, 0x32);
        std::memset(pledgeMasterName.data(), 0, 0x32);
        int pledgeStatus = 0;
        int pledgeLevel = 0;
        int databaseMemberCount = 0;
        int liveMemberCount = 0;
        memberCursor = ::Disassemble(
            memberCursor, "SddSdd", 0x32, pledgeName.data(), &pledgeStatus, &pledgeLevel, 0x32, pledgeMasterName.data(),
            &databaseMemberCount, &liveMemberCount
        );
        static_cast<void>(pledgeStatus);

        socket->SendSystemMessage_S(SystemMessageId::CLAN_NAME_ENTRY, pledgeName.data());
        socket->SendSystemMessage_S(SystemMessageId::CLAN_LEADER_ENTRY, pledgeMasterName.data());
        socket->SendSystemMessage_d(SystemMessageId::CLAN_LEVEL_ENTRY, pledgeLevel);
        socket->Send(
            "chcchch", 0x62, SystemMessageId::ONLINE_AND_TOTAL_MEMBER_COUNTS, 2, 0x15,
            runtime::Int16FromBits(static_cast<std::uint32_t>(liveMemberCount)), 0x15,
            runtime::Int16FromBits(static_cast<std::uint32_t>(databaseMemberCount))
        );
        if (index != memberPledgeCount - 1) {
            socket->SendSystemMessage(SystemMessageId::ALLIANCE_CLAN_ENTRY_SEPARATOR);
        }
    }
    socket->SendSystemMessage(SystemMessageId::ALLIANCE_OR_CLAN_WAR_LIST_FOOTER);
    return false;
}

bool CUserSocket::JoinPartyPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c1c5b8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_bGMInvisible) {
        return false;
    }
    if (user->m_bBlockAll) {
        user->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (g_nCountryCode == 3 && user->GetSocket()->m_bEconomicRestriction) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }
    if (!user->m_data->m_bCanJoinParty) {
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!user->IsPartyJoinable()) {
        return false;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    targetName[0] = 0;
    std::memset(targetName.data() + 1, 0, 0x30);
    int routing = 0;
    ::Disassemble(packet, "Sd", 0x32, targetName.data(), &routing);

    std::array<SharedUtilsWideChar, 28> lowerName;
    lowerName[0] = 0;
    std::memset(lowerName.data() + 1, 0, 0x30);
    ::ToLowerCase(targetName.data(), lowerName.data());
    const std::u16string hostLowerNameUtf16(FixedUtf16View(lowerName));
    const CSPointer<User> target = GetUserFromNameMap(user->m_data->m_nWorldId, hostLowerNameUtf16);
    if (!target) {
        socket->Send("cdd", 0x3a, 0, 0);
        socket->SendSystemMessage(SystemMessageId::INVALID_PARTY_INVITATION_TARGET);
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        user->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(target->GetClass()))) {
        user->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, target->GetName()
        );
        return false;
    }
    if (target->m_data->m_bGMInvisible) {
        return false;
    }
    if (user->m_nId == target->m_nId) {
        socket->Send("cdd", 0x3a, 0, 0);
        socket->SendSystemMessage(SystemMessageId::INVALID_PARTY_INVITATION_TARGET);
        return false;
    }
    if (g_nCountryCode == 3 && target->GetSocket()->m_bEconomicRestriction) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }
    if (CReplyChecker::IsUserWaitingReply(user)) {
        const CSPointer<User> oldReplier = CReplyChecker::GetReplierFromRequester(user);
        if (oldReplier) {
            socket->Send("cdd", 0x3a, 0, 0);
            socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
            return false;
        }
        CReplyChecker::FinishWaitingReplyByRequester(user);
    }
    if (target->m_bBlockAll) {
        socket->SendSystemMessage_Char(SystemMessageId::TARGET_BLOCKS_ALL, target->GetName());
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (target->IsInBlockList(*user)) {
        socket->SendSystemMessage_Char(SystemMessageId::CHARACTER_BLOCKED_YOU, target->GetName());
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!target->AllowInteractWith(user, ClientINI::IT_PARTY_INVITE)) {
        socket->SendSystemMessage_Char(SystemMessageId::CHARACTER_REFUSES_PARTY_REQUESTS, target->GetName());
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!target->m_data->m_bCanJoinParty) {
        socket->Send("cdd", 0x3a, 0, 0);
        return false;
    }
    if (!target->IsPartyJoinable()) {
        return false;
    }

    CParty* const party = user->GetParty();
    if (party != nullptr) {
        if (party->IsLootingTypeModifyInProgress()) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_MAKE_ONE_TO_ONE_REQUEST_DURING_PARTY_LOOT_CHANGE);
            socket->Send("cdd", 0x3a, 0, 0);
            return false;
        }
        if (party->m_data->m_OnlineMaster.id != user->m_nId) {
            socket->Send("cdd", 0x3a, 0, 0);
            socket->SendSystemMessage(SystemMessageId::ONLY_PARTY_LEADER_CAN_INVITE);
            return false;
        }
        if (party->GetMemberCount() > 6) {
            socket->Send("cdd", 0x3a, 0, 0);
            socket->SendSystemMessage(SystemMessageId::PARTY_FULL);
            return false;
        }
    }
    if (target->GetParty() != nullptr) {
        socket->Send("cdd", 0x3a, 0, 0);
        socket->SendSystemMessage_Char(SystemMessageId::CANNOT_INVITE_CHARACTER_ALREADY_IN_PARTY, target->GetName());
        return false;
    }
    if (user->m_EventMatchUser.m_byEventMatchTeamId != 0) {
        const CSPointer<CEventMatch> eventMatch = user->m_EventMatchUser.GetEventMatch();
        if (eventMatch && eventMatch->IsLocked(user->m_nId, static_cast<std::uint32_t>(target->m_data->m_nDBID)) != 0) {
            socket->Send("cdd", 0x3a, 0, 0);
            socket->SendSystemMessage(SystemMessageId::CANNOT_INVITE_PARTY_LOCKED);
            return false;
        }
    }
    if (!CReplyChecker::AttachReplyChecker(2, user, target.get(), 0x3c)) {
        socket->Send("cdd", 0x3a, 0, 0);
        socket->SendSystemMessage_Char(SystemMessageId::INVITED_CHARACTER_BUSY, target->GetName());
        return false;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    if (targetSocket != nullptr) {
        int sendRouting = routing;
        CParty* const currentParty = user->GetParty();
        if (currentParty == nullptr) {
            user->m_ItemRoutingType = static_cast<ItemRoutingType>(routing);
        } else {
            sendRouting = static_cast<int>(currentParty->GetRoutingType());
        }
        targetSocket->Send("cSd", 0x39, user->GetName(), sendRouting);
        socket->SendSystemMessage_Char(SystemMessageId::CHARACTER_INVITED_TO_PARTY, target->GetName());
    }
    return false;
}

bool CUserSocket::AnswerJoinPartyPacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c29de4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const replier = socket->GetUser();
    if (replier == nullptr) {
        return true;
    }
    if (!replier->m_data->m_bCanJoinParty || !replier->IsPartyJoinable()) {
        return false;
    }

    int answer;
    ::Disassemble(packet, "d", &answer);
    const CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(replier);
    if (!requester) {
        socket->SendSystemMessage(SystemMessageId::PARTY_INVITATION_USER_NOT_FOUND);
        CReplyChecker::FinishWaitingReplyByReplier(replier);
        return false;
    }

    static_cast<void>(CCursedWeaponMgr::inst());
    if (requester->m_nCursedWeaponClassId > 0) {
        return false;
    }
    if (CPVPMatchSystem::inst().IsCrataeCubeUser(*requester.get())) {
        replier->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_FORM_PARTY_IN_THIS_AREA);
        return false;
    }
    if (replier->m_EventMatchUser.m_byEventMatchTeamId != 0) {
        const CSPointer<CEventMatch> eventMatch = replier->m_EventMatchUser.GetEventMatch();
        if (eventMatch &&
            eventMatch->IsLocked(replier->m_nId, static_cast<std::uint32_t>(requester->m_data->m_nDBID)) != 0) {
            socket->Send("cdd", 0x3a, 0, 0);
            CReplyChecker::FinishWaitingReplyByReplier(replier);
            return false;
        }
    }

    if (answer == 1) {
        CParty* party = requester->GetParty();
        int routing = 0;
        if (party == nullptr) {
            CPartyMatchingRequest::inst().DismissRoomOnly(CSPointer<User>(replier, ObjectRefType::FunctionChain));

            party = new CParty(requester->m_ItemRoutingType);
            party->SetMaster(requester.get());
            party->AddMember(requester.get());
            party->AddMember(replier);
            g_PartyMap.Insert(static_cast<int>(party->m_nId), party, const_cast<char*>("UserSocket.cpp"), 0xd08);

            requester->SetParty(party, nullptr);
            replier->SetParty(party, nullptr);
            party->OpenAllWindowPacket(requester.get());
            party->OpenAllWindowPacket(replier);

            requester->m_bSpelledInfoChanged = true;
            requester->m_NeedUpdatePacketBits |= 4U;
            replier->m_bSpelledInfoChanged = true;
            replier->m_NeedUpdatePacketBits |= 4U;
            replier->SetPreInitData();
            requester->SetPreInitData();

            CPartyMatchingRequest::inst().OnJoinParty(
                CSPointer<User>(requester.get(), ObjectRefType::FunctionChain),
                CSPointer<User>(replier, ObjectRefType::FunctionChain)
            );
            g_PartyMatchingWaitingRoom.SetNonInvitable(requester.get());
            g_PartyMatchingWaitingRoom.SetNonInvitable(replier);

            L2_RELEASE(party, ObjectRefType::CreateDelete);

            std::u16string memberNames = party->GetAllMemberName();
            const std::u16string hostMemberNames = Utf16Text(memberNames.c_str()).String();
            CLogFormatter formatter;
            formatter.WriteLog1_1(
                0x331, requester.get(), replier, hostMemberNames.data(), nullptr, nullptr,
                static_cast<std::int64_t>(replier->m_data->m_Race), static_cast<std::int64_t>(replier->m_data->m_Sex),
                static_cast<std::int64_t>(replier->m_data->m_nClass), static_cast<std::int64_t>(replier->m_data->m_nLevel),
                0, static_cast<std::int64_t>(party->GetRoutingType()), 0
            );
            routing = static_cast<int>(requester->m_ItemRoutingType);
        } else {
            User* const partyMaster = party->GetMaster();
            if (partyMaster->GetPrivateID() != 0) {
                replier->GetSocket()->Send("cdd", 0x3a, 0, 0);
                CReplyChecker::FinishWaitingReplyByReplier(replier);
                return false;
            }

            CMultiPartyCommandChannel* const mpcc = party->GetMPCC();
            if (mpcc != nullptr && mpcc->m_isTeleporting) {
                replier->SendSystemMessage(SystemMessageId::CANNOT_JOIN_PARTY_WHILE_COMMAND_CHANNEL_TELEPORTING);
                return false;
            }

            CPartyMatchingRequest::inst().DismissRoomOnly(CSPointer<User>(replier, ObjectRefType::FunctionChain));
            party->AddMember(replier);
            replier->SetParty(party, nullptr);
            if (mpcc != nullptr) {
                replier->m_data->m_nMPCCId = mpcc->m_nId;
                CPartyMatchingRequest::inst().DismissMPCCRoomOnly(CSPointer<User>(replier, ObjectRefType::FunctionChain));
                CPartyMatchingRequest::inst().WithdrawMPCCRoom(CSPointer<User>(replier, ObjectRefType::FunctionChain));
            }

            party->AddWindowPacket(replier);
            party->OpenAllWindowPacket(replier);
            replier->m_bSpelledInfoChanged = true;
            replier->m_NeedUpdatePacketBits |= 4U;
            replier->GatherSpelledInfoChanged();
            replier->SetPreInitData();

            CPartyMatchingRequest::inst().OnJoinParty(
                CSPointer<User>(partyMaster, ObjectRefType::FunctionChain),
                CSPointer<User>(replier, ObjectRefType::FunctionChain)
            );
            g_PartyMatchingWaitingRoom.SetNonInvitable(replier);

            CMultiPartyCommandChannelManager manager;
            manager.OnJoinParty(CSPointer<User>(replier, ObjectRefType::FunctionChain));

            std::u16string memberNames = party->GetAllMemberName();
            const std::u16string hostMemberNames = Utf16Text(memberNames.c_str()).String();
            CLogFormatter formatter;
            formatter.WriteLog1_1(
                0x332, replier, requester.get(), hostMemberNames.data(), nullptr, nullptr,
                static_cast<std::int64_t>(requester->m_data->m_Race), static_cast<std::int64_t>(requester->m_data->m_Sex),
                static_cast<std::int64_t>(requester->m_data->m_nClass),
                static_cast<std::int64_t>(requester->m_data->m_nLevel), 0,
                static_cast<std::int64_t>(party->GetRoutingType()), 0
            );
            routing = static_cast<int>(party->GetRoutingType());
        }

        requester->GetSocket()->Send("cdd", 0x3a, 1, routing);
        replier->GetSocket()->Send("cdd", 0x3a, 2, routing);

        const std::uint8_t requesterEventTeam = requester->m_EventMatchUser.m_byEventMatchTeamId;
        if (static_cast<std::int8_t>(requesterEventTeam) > 0) {
            const CSPointer<CEventMatch> eventMatch = requester->m_EventMatchUser.GetEventMatch();
            if (eventMatch) {
                eventMatch->AddUser(requester.get(), requesterEventTeam);
            }
            replier->ParamChanged(PCB_CHAR);
        }
    } else {
        if (answer == -1) {
            requester->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::CHARACTER_REFUSES_PARTY_REQUESTS, replier->GetName()
            );
        }
        requester->GetSocket()->Send("cdd", 0x3a, 0, 0);
    }

    CReplyChecker::FinishWaitingReplyByReplier(replier);
    return false;
}

bool CUserSocket::JoinPledgePacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c1bb74.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int targetServerId = 0;
    PledgeType pledgeType;
    ::Disassemble(packet, "dd", &targetServerId, &pledgeType);
    if (pledgeType != MASTER_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NOT IMPLEMENTED!!!!!",
        //     kUserSocketSource,
        //     0xa13)
        // Log.Add(
        //     LOG_WARN,
        //     "[clan] Invitation ignored on socket %u: clan unit type %d is not handled here. This handler accepts the main
        //     clan only.", socket->m_nId, static_cast<int>(pledgeType))
        L2_LOG_WARN(
            Log,
            "[clan] Invitation ignored on socket %u: clan unit type %d is not handled here; this handler accepts the main "
            "clan only.",
            socket->m_nId, static_cast<int>(pledgeType)
        );
        return false;
    }

    const CSPointer<User> target = g_UserMap.FindObject(targetServerId);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0x2d, 0);
        return false;
    }
    if (User::IsInFifthClassGroup(static_cast<ClassType>(target->GetClass()))) {
        user->GetSocket()->SendSystemMessage_Char(
            SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, target->GetName()
        );
        return false;
    }
    return socket->DoJoinPledge(user, target.get(), pledgeType);
}

bool CUserSocket::DoJoinPledge(User* const inviter, User* const invitee, const PledgeType pledgeType) {
    // Current L2Server 0x00c13174.
    L2_GUARD_FUNCTION();
    if (inviter == nullptr || invitee == nullptr) {
        return false;
    }

    CUserSocket* const inviterSocket = inviter->GetSocket();
    if (inviter->m_bBlockAll) {
        inviter->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        inviterSocket->Send("cd", 0x2d, 0);
        return false;
    }
    if (inviter->m_data->m_TrainingState == 1) {
        inviter->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        inviterSocket->Send("cd", 0x2d, 0);
        return false;
    }
    if (g_nCountryCode == 3 && inviter->GetSocket() != nullptr && inviter->GetSocket()->m_bEconomicRestriction) {
        inviter->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        return false;
    }

    CPledge* const pledge = inviter->GetPledge();
    if (pledge == nullptr || !inviter->HavePledgePower(PP_JOIN)) {
        inviterSocket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        inviterSocket->Send("cd", 0x2d, 0);
        return false;
    }
    if (pledge->IsWaitingToDismiss()) {
        inviterSocket->SendSystemMessage_S(
            SystemMessageId::CLAN_DISSOLUTION_ALREADY_REQUESTED, std::u16string(pledge->GetPledgeName()).c_str()
        );
        inviterSocket->Send("cd", 0x2d, 0);
        return false;
    }
    if (!pledge->CanDoPledgeWork(0x78)) {
        inviterSocket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        inviterSocket->Send("cd", 0x2d, 0);
        return false;
    }
    if (!pledge->HasPledgeType(pledgeType)) {
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (!pledge->CanJoinMoreMember(pledgeType)) {
        inviterSocket->Send("cd", 0x2d, 0);
        inviterSocket->SendSystemMessage(SystemMessageId::CANNOT_RECRUIT_CLAN_FULL);
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (!pledge->CanAcceptNewMember()) {
        inviterSocket->Send("cd", 0x2d, 0);
        if (BP::_PLEDGE_OUST_PENALTY_TIMEOUT < 0x15180) {
            inviter->SendSystemMessage_d(
                SystemMessageId::CLAN_RECRUITMENT_DELAY_AFTER_EXPULSION_IN_MINUTES, BP::_PLEDGE_OUST_PENALTY_TIMEOUT / 60
            );
        } else {
            inviterSocket->SendSystemMessage(SystemMessageId::CANNOT_RECRUIT_DURING_POST_EXPULSION_RESTRICTION);
        }
        pledge->SetPledgeWorkDone();
        return false;
    }

    if (invitee->m_bBlockAll) {
        inviterSocket->SendSystemMessage_Char(SystemMessageId::TARGET_BLOCKS_ALL, invitee->GetName());
        inviterSocket->Send("cd", 0x2d, 0);
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (invitee->IsInBlockList(*inviter)) {
        inviterSocket->SendSystemMessage_Char(SystemMessageId::CHARACTER_BLOCKED_YOU, invitee->GetName());
        inviterSocket->Send("cd", 0x2d, 0);
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (invitee->m_data->m_TrainingState == 1) {
        inviter->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
        inviterSocket->Send("cd", 0x2d, 0);
        pledge->SetPledgeWorkDone();
        return false;
    }

    if (pledgeType == ACADEMY) {
        bool canJoinAcademy = true;
        CSharedCreatureData* const data = invitee->m_data;
        if (data->m_nSubJob1Class != -1 || data->m_nSubJob2Class != -1 || data->m_nSubJob3Class != -1) {
            for (int index = 0; index < 4; ++index) {
                if (data->m_SubJobLevel[index] >= 85) {
                    canJoinAcademy = false;
                }
            }
            if ((data->m_nSubJob0Class > 0 && !User::CanJoinAcademyClass(static_cast<ClassType>(data->m_nSubJob0Class))) ||
                (data->m_nSubJob1Class > 0 && !User::CanJoinAcademyClass(static_cast<ClassType>(data->m_nSubJob1Class))) ||
                (data->m_nSubJob2Class > 0 && !User::CanJoinAcademyClass(static_cast<ClassType>(data->m_nSubJob2Class))) ||
                (data->m_nSubJob3Class > 0 && !User::CanJoinAcademyClass(static_cast<ClassType>(data->m_nSubJob3Class)))) {
                canJoinAcademy = false;
            }
        }
        if (data->m_nLevel >= 85 || !User::CanJoinAcademyClass(data->m_nClass)) {
            canJoinAcademy = false;
        }
        if (!canJoinAcademy) {
            inviterSocket->SendSystemMessage(SystemMessageId::CLAN_ACADEMY_MEMBERSHIP_REQUIREMENTS);
            inviterSocket->Send("cd", 0x2d, 0);
            pledge->SetPledgeWorkDone();
            return false;
        }
        if (data->m_nAcademyPledgeDBID != 0) {
            inviter->GetSocket()->SendSystemMessage_Char(
                SystemMessageId::CHARACTER_CANNOT_REJOIN_CLAN_ACADEMY_ALREADY_GRADUATED, invitee->GetName()
            );
            pledge->SetPledgeWorkDone();
            return false;
        }
    }

    if (invitee->m_data->m_nPledgeID != 0) {
        inviterSocket->SendSystemMessage(SystemMessageId::ALREADY_IN_CLAN);
        inviterSocket->Send("cd", 0x2d, 0);
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (!invitee->CanJoinPledge()) {
        inviterSocket->Send("cd", 0x2d, 0);
        if (BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT < 0x15180) {
            const std::u16string inviteeName = std::u16string(invitee->GetName());
            inviterSocket->Send(
                "chccScd", 0x62, SystemMessageId::CHARACTER_CANNOT_JOIN_CLAN_BEFORE_WITHDRAWAL_DELAY_EXPIRES, 2, 0x0c,
                inviteeName.c_str(), 1, BP::_PLEDGE_WITHDRAW_PENALTY_TIMEOUT / 60
            );
        } else {
            inviterSocket->SendSystemMessage_Char(
                SystemMessageId::CHARACTER_CANNOT_JOIN_CLAN_DURING_WITHDRAWAL_RESTRICTION, invitee->GetName()
            );
        }
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (g_nCountryCode == 3 && invitee->GetSocket() != nullptr && invitee->GetSocket()->m_bEconomicRestriction) {
        inviter->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        pledge->SetPledgeWorkDone();
        return false;
    }
    if (!CReplyChecker::AttachReplyChecker(0, inviter, invitee, 0x3c)) {
        inviterSocket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        inviterSocket->Send("cd", 0x2d, 0);
        pledge->SetPledgeWorkDone();
        return false;
    }

    invitee->m_pledgeTypeWhenAskJoin = pledgeType;
    CUserSocket* const inviteeSocket = invitee->GetSocket();
    const wchar_t* const subPledgeName = pledge->GetSubPledgeName(pledgeType);
    const char16_t* const pledgeName = pledge->GetPledgeName();
    const char16_t* const inviterName = inviter->GetName();
    const std::u16string targetSubPledgeName = WideToUtf16(subPledgeName);
    const std::u16string targetPledgeName = std::u16string(pledgeName);
    const std::u16string targetInviterName = std::u16string(inviterName);
    inviteeSocket->Send(
        "cdSSdS", 0x2c, runtime::Int32FromBits(inviter->m_nId), targetInviterName.c_str(), targetPledgeName.c_str(),
        static_cast<int>(pledgeType), targetSubPledgeName.c_str()
    );
    inviterSocket->SendSystemMessage_Char(SystemMessageId::CLAN_INVITATION_SENT_TO_CHARACTER, invitee->GetName());
    return false;
}

bool CUserSocket::AnswerJoinPledgePacket(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c175f8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    const PledgeType pledgeType = user->m_pledgeTypeWhenAskJoin;
    user->m_pledgeTypeWhenAskJoin = MASTER_PLEDGE;
    int answer;
    ::Disassemble(packet, "d", &answer);

    {
        const CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(user);
        if (!requester) {
            socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        } else {
            CPledge* const pledge = requester->GetPledge();
            if (pledge == nullptr) {
                requester->GetSocket()->SendSystemMessage_Char(
                    SystemMessageId::NAMED_CHARACTER_CLAN_INVITATION_FAILED, user->GetName()
                );
            } else if (pledgeType == ACADEMY && user->m_data->m_nAcademyPledgeDBID != 0) {
                requester->GetSocket()->SendSystemMessage_Char(
                    SystemMessageId::CHARACTER_CANNOT_REJOIN_CLAN_ACADEMY_ALREADY_GRADUATED, user->GetName()
                );
            } else if (answer == 1) {
                int transaction = 0;
                if (pledge->BeginTransaction(&transaction)) {
                    db.RequestUpdateUserPledge(user, pledge, 1, pledgeType);
                } else {
                    socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
                    requester->GetSocket()->Send("cd", 0x2d, 0);
                    pledge->SetPledgeWorkDone();
                }
            } else {
                requester->GetSocket()->SendSystemMessage_Char(
                    SystemMessageId::NAMED_CHARACTER_CLAN_INVITATION_FAILED, user->GetName()
                );
                pledge->SetPledgeWorkDone();
            }
        }
    }
    CReplyChecker::FinishWaitingReplyByReplier(user);
    return false;
}

bool WithdrawPledgePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1bde0.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        socket->SendSystemMessage(SystemMessageId::NOT_IN_CLAN);
        socket->Send("cd", 0x34, 0);
        return false;
    }
    if (user->m_data->m_bPledgeMaster) {
        socket->SendSystemMessage(SystemMessageId::CLAN_LEADER_CANNOT_LEAVE);
        socket->Send("cd", 0x34, 0);
        return false;
    }
    if (user->m_data->m_bIsInCombatMode) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_LEAVE_CLAN_DURING_COMBAT);
        socket->Send("cd", 0x34, 0);
        return false;
    }
    if (pledge->IsMemberInTeamBattleEntry(user)) {
        socket->SendSystemMessage(
            SystemMessageId::CANNOT_LEAVE_OR_EXPEL_CLAN_MEMBER_DURING_TEAM_CLAN_HALL_BATTLE_REGISTRATION
        );
        socket->Send("cd", 0x34, 0);
        return false;
    }
    if (user->m_data->m_nDominionSiegeStatus != 0) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_LEAVE_OR_EXPEL_CLAN_MEMBER_IN_TERRITORY_WAR);
        socket->Send("cd", 0x34, 0);
        return false;
    }

    {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
            user, summon, if (summon->m_data->m_bIsInCombatMode) {
                socket->SendSystemMessage(SystemMessageId::CANNOT_LEAVE_CLAN_DURING_COMBAT);
                socket->Send("cd", 0x34, 0);
                return false;
            }
        );
    }

    int transaction = 0;
    if (!pledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        socket->Send("cd", 0x34, 0);
        return false;
    }
    db.RequestUpdateUserPledge(user, pledge, 2, static_cast<PledgeType>(user->m_data->m_nPledgeType));
    return false;
}

bool OustPledgeMemberPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1c1e8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    ::Disassemble(packet, "S", 0x32, targetName.data());
    const std::u16string hostTargetName = Utf16Text(targetName.data()).String();

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || (!user->m_data->m_bPledgeMaster && !user->HavePledgePower(PP_OUST_MEMBER))) {
        socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        socket->Send("cd", 0x35, 0);
        return false;
    }
    if (CompareWideCaseInsensitive(hostTargetName.c_str(), pledge->GetMasterName()) == 0) {
        socket->Send("cd", 0x35, 0);
        return false;
    }
    if (CompareWideCaseInsensitive(hostTargetName.c_str(), user->GetName()) == 0) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_SELF);
        socket->Send("cd", 0x35, 0);
        return false;
    }

    std::array<SharedUtilsWideChar, 32> lowerName;
    std::memset(lowerName.data(), 0, 0x32);
    ::ToLowerCase(targetName.data(), lowerName.data());
    const std::u16string hostLowerNameUtf16(FixedUtf16View(lowerName));
    const CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerNameUtf16);
    if (target) {
        if (target->m_data->m_bIsInCombatMode) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_CLAN_MEMBER_IN_COMBAT);
            socket->Send("cd", 0x35, 0);
            return false;
        }
        if (pledge->IsMemberInTeamBattleEntry(target.get())) {
            socket->SendSystemMessage(
                SystemMessageId::CANNOT_LEAVE_OR_EXPEL_CLAN_MEMBER_DURING_TEAM_CLAN_HALL_BATTLE_REGISTRATION
            );
            socket->Send("cd", 0x35, 0);
            return false;
        }
        if (target->m_data->m_nDominionSiegeStatus != 0) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_LEAVE_OR_EXPEL_CLAN_MEMBER_IN_TERRITORY_WAR);
            socket->Send("cd", 0x35, 0);
            return false;
        }
    }

    int transaction = 0;
    if (!pledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        socket->Send("cd", 0x35, 0);
        return false;
    }
    db.RequestOustPledgeMember(user, pledge, hostTargetName.c_str());
    return false;
}

bool JoinAlliancePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1fc7c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsBlockAll()) {
        user->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        socket->Send("cd", 0xbc, 0);
        return false;
    }
    if (user->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }

    int targetServerId = 0;
    ::Disassemble(packet, "d", &targetServerId);
    CPledge* const masterPledge = user->GetPledge();
    if (masterPledge == nullptr || !user->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xbc, 0);
        return false;
    }
    CAlliance* const alliance = masterPledge->GetAlliance();
    if (alliance == nullptr || !masterPledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xbc, 0);
        return false;
    }

    if (static_cast<unsigned int>(alliance->GetMemberPledgeCount()) >=
        static_cast<unsigned int>(g_CurrentAllianceMemberLimit)) {
        socket->Send("cd", 0xbc, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_ACCEPT_CLAN_ALLIANCE_FULL);
        return false;
    }
    if (!alliance->CanAcceptNewMemberPledge()) {
        socket->Send("cd", 0xbc, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_ACCEPT_CLAN_DURING_POST_EXPULSION_RESTRICTION);
        return false;
    }

    const CSPointer<User> target = g_UserMap.FindObject(targetServerId);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        socket->Send("cd", 0xbc, 0);
        return false;
    }

    CPledge* const joiningPledge = target->GetPledge();
    if (joiningPledge == nullptr || !target->m_data->m_bPledgeMaster) {
        socket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        socket->Send("cd", 0xbc, 0);
        return false;
    }
    if (target->IsBlockAll()) {
        user->GetSocket()->SendSystemMessage_Char(SystemMessageId::TARGET_BLOCKS_ALL, target->GetName());
        socket->Send("cd", 0xbc, 0);
        return false;
    }
    if (target->IsInBlockList(*user)) {
        socket->SendSystemMessage_Char(SystemMessageId::CHARACTER_BLOCKED_YOU, target->GetName());
        socket->Send("cd", 0xbc, 0);
        return false;
    }
    if (target->m_data->m_TrainingState == 1) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SEND_REQUEST_TO_CHARACTER_IN_TRAINING_CAMP);
        return false;
    }
    if (joiningPledge->IsWaitingToDismiss()) {
        socket->Send("cd", 0xbc, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_INVITE_DISSOLVING_CLAN_TO_ALLIANCE);
        return false;
    }

    if (CAlliance* const targetAlliance = joiningPledge->GetAlliance()) {
        socket->Send("cd", 0xbc, 0);
        const char16_t* const targetAllianceName = targetAlliance->m_data->m_sAllianceName;
        const std::u16string targetPledgeName = std::u16string(joiningPledge->GetPledgeName());
        socket->Send(
            "chccScS", 0x62, SystemMessageId::CLAN_ALREADY_IN_NAMED_ALLIANCE, 2, 0, targetPledgeName.c_str(), 0,
            targetAllianceName
        );
        return false;
    }
    if (!joiningPledge->CanJoinAlliance()) {
        socket->Send("cd", 0xbc, 0);
        socket->SendSystemMessage_S(
            SystemMessageId::CLAN_CANNOT_JOIN_ALLIANCE_DURING_WITHDRAWAL_RESTRICTION,
            std::u16string(joiningPledge->GetPledgeName()).c_str()
        );
        return false;
    }
    if (!CReplyChecker::AttachReplyChecker(6, user, target.get(), 0x3c)) {
        socket->SendSystemMessage(SystemMessageId::WAITING_FOR_ANOTHER_OPERATION_REPLY);
        socket->Send("cd", 0xbc, 0);
        return false;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    const char16_t* const allianceName = alliance->m_data->m_sAllianceName;
    const std::u16string targetUserName = std::u16string(user->GetName());
    const std::u16string targetMasterPledgeName = std::u16string(masterPledge->GetPledgeName());
    const std::int32_t userServerId = runtime::Int32FromBits(user->GetServerId());
    targetSocket->Send("cdSSS", 0xbb, userServerId, allianceName, targetMasterPledgeName.c_str(), targetUserName.c_str());
    return false;
}

bool AnswerJoinAlliancePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c18080.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int answer;
    ::Disassemble(packet, "d", &answer);
    CReplyCheckerHelper replyCheckerHelper(user, 0);
    const CSPointer<User> requester = CReplyChecker::GetRequesterFromReplier(user);
    if (!requester) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        return false;
    }

    CPledge* const requesterPledge = requester->GetPledge();
    if (requesterPledge == nullptr || !requester->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }
    CAlliance* const alliance = requesterPledge->GetAlliance();
    if (alliance == nullptr || !requesterPledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }

    CPledge* const joiningPledge = user->GetPledge();
    if (joiningPledge == nullptr || !user->m_data->m_bPledgeMaster) {
        socket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }
    if (answer != 1) {
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }

    int transaction = 6;
    if (!joiningPledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }
    if (!requesterPledge->BeginTransaction(&transaction)) {
        joiningPledge->EndTransaction();
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        requester->GetSocket()->Send("cd", 0xbc, 0);
        return false;
    }

    db.RequestJoinAlliance(requester.get(), alliance, requesterPledge, joiningPledge);
    return false;
}

bool WithdrawAlliancePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1434c.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    CAlliance* const alliance = pledge != nullptr ? pledge->GetAlliance() : nullptr;
    if (alliance == nullptr) {
        socket->SendSystemMessage(SystemMessageId::NOT_IN_ALLIANCE);
        socket->Send("cd", 0xab, 0);
        return false;
    }
    if (!user->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_LEAVE_ALLIANCE);
        socket->Send("cd", 0xab, 0);
        return false;
    }
    if (pledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ALLIANCE_LEADER_CANNOT_LEAVE);
        socket->Send("cd", 0xab, 0);
        return false;
    }
    if (pledge->HaveCastleUnderSiege() || pledge->GetFortressUnderDefenceSiege() != nullptr ||
        pledge->GetFortressUnderOffenceSiege() != nullptr) {
        socket->Send("cd", 0xab, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_LEAVE_WHILE_PARTICIPATING_IN_SIEGE);
        return false;
    }

    int transaction = 8;
    if (!pledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        socket->Send("cd", 0xab, 0);
        return false;
    }
    db.RequestWithdrawAlliance(user, alliance, pledge);
    return false;
}

bool OustAllianceMemberPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1869c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 28> targetPledgeName;
    ::Disassemble(packet, "S", 0x32, targetPledgeName.data());
    const std::u16string hostTargetPledgeName = Utf16Text(targetPledgeName.data()).String();

    CPledge* const masterPledge = user->GetPledge();
    if (masterPledge == nullptr || !user->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xac, 0);
        return false;
    }
    CAlliance* const alliance = masterPledge->GetAlliance();
    if (alliance == nullptr || !masterPledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xac, 0);
        return false;
    }
    if (CompareWideCaseInsensitive(hostTargetPledgeName.c_str(), masterPledge->GetPledgeName()) == 0) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_OWN_CLAN_FROM_ALLIANCE);
        socket->Send("cd", 0xac, 0);
        return false;
    }

    const CSPointer<CPledge> targetPledge = GetPledgeFromNameMap(user->m_data->m_nWorldId, targetPledgeName.data());
    if (!targetPledge) {
        socket->SendSystemMessage(SystemMessageId::TARGET_CLAN_DOES_NOT_EXIST);
        socket->Send("cd", 0xac, 0);
        return false;
    }
    if (targetPledge->GetAllianceDbId() == 0) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_CLAN_OUTSIDE_ALLIANCE);
        socket->Send("cd", 0xac, 0);
        return false;
    }
    const int targetAllianceDatabaseId = targetPledge->GetAllianceDbId();
    const int masterAllianceDatabaseId = masterPledge->GetAllianceDbId();
    if (masterAllianceDatabaseId != targetAllianceDatabaseId) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_CLAN_OUTSIDE_ALLIANCE);
        socket->Send("cd", 0xac, 0);
        return false;
    }
    if (targetPledge->HaveCastleUnderSiege() || targetPledge->GetFortressUnderDefenceSiege() != nullptr ||
        targetPledge->GetFortressUnderOffenceSiege() != nullptr) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_EXPEL_ALLIANCE_CLAN_PARTICIPATING_IN_SIEGE);
        socket->Send("cd", 0xac, 0);
        return false;
    }

    int transaction = 7;
    if (!masterPledge->BeginTransaction(&transaction)) {
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        socket->Send("cd", 0x35, 0);
        return false;
    }
    if (!targetPledge->BeginTransaction(&transaction)) {
        masterPledge->EndTransaction();
        socket->SendSystemMessage(SystemMessageId::CLAN_OPERATION_ALREADY_IN_PROGRESS);
        socket->Send("cd", 0x35, 0);
        return false;
    }

    db.RequestOustAllianceMemberPledge(user, alliance, masterPledge, targetPledge.get());
    return false;
}

bool DismissAlliancePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c204bc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const masterPledge = user->GetPledge();
    if (masterPledge == nullptr || !user->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xad, 0);
        return false;
    }
    CAlliance* const alliance = masterPledge->GetAlliance();
    if (alliance == nullptr || !masterPledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xad, 0);
        return false;
    }

    std::vector<CPledge*> memberPledges;
    alliance->GetAllMemberPledge(memberPledges);
    for (CPledge* const memberPledge : memberPledges) {
        if (memberPledge->HaveCastleUnderSiege() || memberPledge->GetFortressUnderDefenceSiege() != nullptr ||
            memberPledge->GetFortressUnderOffenceSiege() != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_ALLIANCE_MEMBER_CLAN_IN_SIEGE);
            socket->Send("cd", 0xad, 0);
            return false;
        }
    }

    db.RequestDismissAlliance(user, alliance, masterPledge);
    return false;
}

bool SetAllianceCrestPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c145a4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const masterPledge = user->GetPledge();
    if (masterPledge == nullptr || !user->IsPledgeMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xae, 0);
        return false;
    }
    CAlliance* const alliance = masterPledge->GetAlliance();
    if (alliance == nullptr || !masterPledge->IsAllianceMaster()) {
        socket->SendSystemMessage(SystemMessageId::ONLY_ALLIANCE_LEADER_CAN_ACT);
        socket->Send("cd", 0xae, 0);
        return false;
    }

    std::uint32_t size = 0;
    const std::uint8_t* const data = ::Disassemble(packet, "d", &size);
    if (size < 0xc01U) {
        db.RequestSetAllianceCrest(user, alliance, data, static_cast<int>(size));
        socket->SendSystemMessage(size == 0 ? SystemMessageId::CREST_DELETED : SystemMessageId::CREST_REGISTERED);
        return false;
    }

    socket->SendSystemMessage(SystemMessageId::IMAGE_REQUIRES_8_BY_12_DIMENSIONS);
    return false;
}

bool RequestManagePledgePower(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c15878.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return false;
    }

    int gradeId;
    int manageType;
    const std::uint8_t* const cursor = ::Disassemble(packet, "dd", &gradeId, &manageType);
    if (manageType == 2) {
        if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_MANAGE_GRADE)) {
            socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
            return false;
        }

        std::uint32_t pledgePowerBits;
        ::Disassemble(cursor, "d", &pledgePowerBits);
        pledge->SetPowerGrade(gradeId, static_cast<unsigned long>(pledgePowerBits));
        db.RequestSavePledgePower(*pledge, gradeId, pledgePowerBits);
        user->SendSystemMessage(SystemMessageId::CLAN_PRIVILEGES_CHANGED);
    }

    const std::uint32_t pledgePowerBits = pledge->GetPowerGrade(gradeId);
    user->GetSocket()->Send("cddd", 0x2a, gradeId, manageType, pledgePowerBits);
    UpdatePledgePower service(gradeId, pledgePowerBits);
    pledge->ServeForAllMember(service, true);
    return false;
}

bool GiveNicknamePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1e8d4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    targetName[0] = SharedUtilsWideChar{};
    std::memset(targetName.data() + 1, 0, 0x30);
    std::array<SharedUtilsWideChar, 28> nickname;
    nickname[0] = SharedUtilsWideChar{};
    std::memset(nickname.data() + 1, 0, 0x30);
    ::Disassemble(packet, "SS", 0x32, targetName.data(), 0x32, nickname.data());

    std::array<char, 4000> ansiNickname{};
    const int convertedLength = UnicodeToAnsi(
        reinterpret_cast<char16_t*>(nickname.data()), static_cast<int>(TargetWideLength(nickname.data())),
        ansiNickname.data(), static_cast<int>(ansiNickname.size())
    );
    const int maximumLength = ::g_nCountryCode == 8 ? 0x22 : 0x11;
    const auto sendReply = [&]() {
        socket->Send(
            "cSSd", 0x8d, reinterpret_cast<const char16_t*>(targetName.data()),
            reinterpret_cast<const char16_t*>(nickname.data()), 0
        );
    };
    if (maximumLength < convertedLength) {
        sendReply();
        socket->SendSystemMessage(SystemMessageId::INVALID_TITLE_LENGTH);
        return false;
    }

    std::array<SharedUtilsWideChar, 28> lowerTargetName;
    std::memset(lowerTargetName.data(), 0, 0x32);
    ::ToLowerCase(targetName.data(), lowerTargetName.data());
    const std::u16string hostLowerTargetNameUtf16(FixedUtf16View(lowerTargetName));
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerTargetNameUtf16);
    if (!target) {
        socket->SendSystemMessage(SystemMessageId::TARGET_PLAYER_NOT_ONLINE);
        sendReply();
        return false;
    }

    bool canSetNickname = false;
    const int noblessType = user->m_data->m_nNoblessType;
    if (noblessType == 1 || noblessType == 2) {
        const std::uint32_t actorDatabaseId = user->GetDBID();
        const std::uint32_t targetDatabaseId = target->GetDBID();
        canSetNickname = actorDatabaseId == targetDatabaseId;
    }

    if (!canSetNickname) {
        CPledge* const pledge = user->GetPledge();
        if (pledge == nullptr || !user->HavePledgePower(PP_GIVE_NICKNAME)) {
            socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
            sendReply();
            return false;
        }
        if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::NICKNAME)) {
            sendReply();
            socket->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_TO_GRANT_TITLE);
            return false;
        }
        if (pledge->GetDbId() != target->m_data->m_nPledgeID) {
            socket->SendSystemMessage(SystemMessageId::TARGET_MUST_BE_CLAN_MEMBER);
            sendReply();
            return false;
        }
    }

    db.RequestSetUserNickname(user, target.get(), nickname.data());
    return false;
}

bool SayPacket2(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1d374.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    std::array<SharedUtilsWideChar, 1024> message;
    message[0] = SharedUtilsWideChar{};
    std::memset(message.data() + 1, 0, 0x7fe);
    if (user == nullptr) {
        return true;
    }
    if (user->m_dwStopSay != 0 || !user->m_bCanSay) {
        user->SendSystemMessage(SystemMessageId::CHAT_PROHIBITED);
        return false;
    }

    // Current leaves the destination lane uninitialized before Disassemble.
    // malformed input therefore keeps the target's indeterminate value.
    int sayType;
    const std::uint8_t* const trailingPacket = ::Disassemble(packet, "Sd", 0x800, message.data(), &sayType);
    if (sayType != SPT_USER_PET && sayType != SPT_GM_PET) {
        message[255] = SharedUtilsWideChar{};
    }

    std::array<SharedUtilsWideChar, 1024> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x7fe);
    const std::size_t messageLength = TargetWideLength(message.data());
    std::size_t inputIndex = 0;
    std::size_t outputIndex = 0;
    while (inputIndex < messageLength) {
        SharedUtilsWideChar value = message[inputIndex];
        if (value == static_cast<SharedUtilsWideChar>(',')) {
            filteredMessage[outputIndex++] = static_cast<SharedUtilsWideChar>(' ');
        } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
            filteredMessage[outputIndex++] = value;
        } else {
            bool copyFormattingSegment = false;
            for (;;) {
                ++inputIndex;
                value = message[inputIndex];
                if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                    break;
                }
                if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                    copyFormattingSegment = !copyFormattingSegment;
                }
                if (copyFormattingSegment) {
                    filteredMessage[outputIndex++] = value;
                }
            }
        }
        ++inputIndex;
    }

    if (g_bChatLimitByOverChat &&
        user->SetChatLimitValue(static_cast<int>(TargetWideLength(filteredMessage.data()))) >= 22001) {
        user->SendSystemMessage(SystemMessageId::CHAT_BANNED_FOR_SPAM);
        return false;
    }

    runtime::raw_atomic::IncrementI32(&g_nSayCount);
    const std::size_t countedMessageLength = TargetWideLength(message.data());
    runtime::raw_atomic::AddI32(&g_nSaySize, static_cast<std::int32_t>(countedMessageLength));

    const char16_t* const chatMessage = reinterpret_cast<const char16_t*>(message.data());

    if (user->IsBuilder() && user->m_bScreenNotice) {
        user->ScreenNotice(chatMessage);
        return false;
    }
    if (sayType == SPT_TELL && user->m_bBlockAll) {
        user->SendSystemMessage(SystemMessageId::BLOCK_ALL_ENABLED);
        return false;
    }

    const bool writeGenericChatLog = sayType != SPT_TELL && sayType != SPT_GM_PET && sayType != SPT_MARKET &&
        sayType != SPT_PARTY_ROOM_CHAT && sayType != SPT_COMMANDER_CHAT && sayType != SPT_INTER_PARTYMASTER_CHAT &&
        sayType != SPT_HERO && sayType != SPT_DOMINIONWAR && sayType != SPT_WORLD;
    if (writeGenericChatLog) {
        const FVector position = user->GetPos();
        const char16_t* const actorName = user->GetName();
        const unsigned int actorDatabaseId = user->GetDBID();
        CLogFormatter formatter;
        // formatter.WriteChatLog(
        //     sayType,
        //     static_cast<std::int64_t>(actorDatabaseId),
        //     0,
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     hostFilteredMessage,
        //     const_cast<wchar_t*>(actorName),
        //     u"")
        formatter.WriteChatLog(
            sayType, static_cast<std::int64_t>(actorDatabaseId), 0, runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
            FixedUtf16View(filteredMessage), actorName, u""
        );
    }

    if (user->m_data->m_nWorldId != g_nWorldId && (sayType == SPT_PLEDGE || sayType == SPT_ALLIANCE)) {
        CPacketHelperEx<20480> delegatedPacket;
        delegatedPacket.Assemble(
            "SdS", reinterpret_cast<const char16_t*>(message.data()),
            sayType == SPT_PLEDGE ? SPT_PLEDGE_INRAIDSERVER : SPT_ALLIANCE_INRAIDSERVER, user->m_sChatName
        );
        Singleton<RaidServerManager>::inst().DelegatePacketHandle(
            *user, 0x49, delegatedPacket.GetLength(), delegatedPacket.GetPacket()
        );
        return false;
    }

    if (user->m_PledgeGameUser.IsPlayer()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHAT_IN_CEREMONY_OF_CHAOS);
        return false;
    }
    if (user->IsOlympiadPlaying()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_CHAT_DURING_OLYMPIAD);
        return false;
    }

    const auto sendChatLimitMessage = [user](const int messageId, const int level) {
        user->GetSocket()->Send("chccc", 0x62, messageId, 1, 0x14, static_cast<int>(static_cast<std::int8_t>(level)));
    };

    switch (sayType) {
        case SPT_NORMAL:
            if (user->m_data->m_nLevel > g_nChatLimitLevelNormal) {
                user->Say(chatMessage);
                return false;
            }
            if (g_nChatLimitTarget == 0 ||
                (g_nChatLimitTarget == 1 && user->CheckPremiumType(PremiumUserType::PUT_FREE_USER))) {
                sendChatLimitMessage(
                    SystemMessageId::FREE_USER_GENERAL_CHAT_RESTRICTED_UNTIL_LEVEL, g_nChatLimitLevelNormal
                );
                return false;
            }
            if (g_nChatLimitTarget == 2) {
                user->Say(chatMessage);
            }
            return false;

        case SPT_SHOUT: {
            CUserSocket* const userSocket = user->GetSocket();
            if (userSocket->m_bEconomicRestriction) {
                user->SendSystemMessage(SystemMessageId::ACCOUNT_CANNOT_SHOUT);
                return false;
            }
            if (user->m_data->m_nLevel > g_nChatLimitLevelShout) {
                user->Shout(chatMessage);
                return false;
            }
            if (g_nChatLimitTarget == 0 ||
                (g_nChatLimitTarget == 1 && user->CheckPremiumType(PremiumUserType::PUT_FREE_USER))) {
                sendChatLimitMessage(SystemMessageId::FREE_USER_SHOUT_RESTRICTED_UNTIL_LEVEL, g_nChatLimitLevelShout);
                return false;
            }
            if (g_nChatLimitTarget == 2) {
                user->Shout(chatMessage);
            }
            return false;
        }

        case SPT_TELL: {
            std::array<SharedUtilsWideChar, 28> targetName;
            std::array<SharedUtilsWideChar, 28> lowerTargetName;
            std::memset(targetName.data(), 0, 0x32);
            std::memset(lowerTargetName.data(), 0, 0x32);
            ::Disassemble(trailingPacket, "S", 0x32, targetName.data());
            ::ToLowerCase(targetName.data(), lowerTargetName.data());

            const std::u16string hostLowerTargetNameUtf16(FixedUtf16View(lowerTargetName));
            CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerTargetNameUtf16);
            if (!target) {
                std::u16string hostTargetName = SanitizeUtf16(targetName.data());
                user->SendSystemMessage(
                    CGMListMgr::inst().Exist(hostTargetName.c_str()) ? SystemMessageId::TARGET_REFUSES_MESSAGES
                                                                                  : SystemMessageId::TARGET_PLAYER_NOT_ONLINE
                );
                return false;
            }
            if (target->IsOlympiadPlaying()) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_WHISPER_TO_OLYMPIAD_PARTICIPANT);
                return false;
            }
            if ((!target->IsBuilder() || !user->IsBuilder()) && !target->m_bCanHearWhisper) {
                user->SendSystemMessage(SystemMessageId::TARGET_REFUSES_MESSAGES);
                return false;
            }

            bool canTalk = true;
            if (g_nCountryCode == 3) {
                if (target->GetSocket() != nullptr && target->GetSocket()->m_bEconomicRestriction) {
                    user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
                    canTalk = false;
                }
            } else if (g_nCountryCode == 1) {
                CSPointer<CFriends> friends = user->GetFriends();
                CUserSocket* const userSocket = user->GetSocket();
                canTalk = true;
                if (userSocket->m_bEconomicRestriction &&
                    (!friends ||
                        (!friends->HasFriend(target->GetDBID()) && !friends->HasWhisperFriend(target->GetDBID())))) {
                    user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
                    canTalk = false;
                }
            }

            if (user->m_data->m_nLevel <= g_nChatLimitLevelWhisper) {
                if (g_nChatLimitTarget == 0 ||
                    (g_nChatLimitTarget == 1 && user->CheckPremiumType(PremiumUserType::PUT_FREE_USER))) {
                    canTalk = false;
                }
                if (g_bChatLimitWhisperReplyOnly && !canTalk) {
                    CSPointer<CFriends> friends = user->GetFriends();
                    canTalk =
                        friends && (friends->HasFriend(target->GetDBID()) || friends->HasWhisperFriend(target->GetDBID()));
                }
            }
            if (!canTalk) {
                sendChatLimitMessage(
                    SystemMessageId::FREE_USER_CAN_REPLY_TO_WHISPERS_BUT_NOT_INITIATE_UNTIL_LEVEL, g_nChatLimitLevelWhisper
                );
                return false;
            }

            user->Tell(target.get(), chatMessage);
            if (target->m_dwStopSay != 0 || !target->m_bCanSay) {
                user->SendSystemMessage(SystemMessageId::TARGET_CHAT_BANNED);
            }

            const FVector position = user->GetPos();
            const char16_t* const targetLogNameSource = target->GetName();
            // On the portable non-Windows owner GetName uses one shared
            // thread-local host buffer. Materialize the first current-ordered
            // lookup so the accounting record owns the name through formatting.
            const std::u16string targetLogName(targetLogNameSource != nullptr ? targetLogNameSource : u"");
            const char16_t* const actorLogName = user->GetName();
            const unsigned int targetDatabaseId = target->GetDBID();
            const unsigned int actorDatabaseId = user->GetDBID();
            CLogFormatter formatter;
            // formatter.WriteChatLog(
            //     sayType,
            //     static_cast<std::int64_t>(actorDatabaseId),
            //     static_cast<std::int64_t>(targetDatabaseId),
            //     runtime::Win64CvttDoubleToInt32(position.x),
            //     runtime::Win64CvttDoubleToInt32(position.y),
            //     runtime::Win64CvttDoubleToInt32(position.z),
            //     hostFilteredMessage,
            //     const_cast<wchar_t*>(actorLogName),
            //     const_cast<wchar_t*>(targetLogName.c_str()))
            formatter.WriteChatLog(
                sayType, static_cast<std::int64_t>(actorDatabaseId), static_cast<std::int64_t>(targetDatabaseId),
                runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                runtime::Win64CvttDoubleToInt32(position.z), FixedUtf16View(filteredMessage), actorLogName,
                targetLogName.c_str()
            );
            return false;
        }

        case SPT_PARTY: {
            CParty* const party = user->GetParty();
            if (party == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_PARTY_CHAT_NOT_IN_PARTY);
                return false;
            }
            std::vector<User*> members;
            party->GetAllMember(members);
            const char16_t* const chatName = user->m_sChatName;
            for (User* const member : members) {
                member->Hear(user, chatName, chatMessage, SPT_PARTY);
            }
            return false;
        }

        case SPT_PLEDGE: {
            CPledge* const pledge = user->GetPledge();
            if (pledge == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_CLAN_CHAT_NOT_IN_CLAN);
                return false;
            }
            pledge->Say(user, chatMessage, SPT_PLEDGE, true, u"");
            CSPointer<User> relayUser(user, ObjectRefType::FunctionChain);
            RelayClient* const relayClient = RelayClient::Inst();
            relayClient->SendSayPledge(std::move(relayUser), chatMessage);
            return false;
        }

        case SPT_USER_PET:
            if (!g_PetitionSetting.UseWebPetition) {
                static_cast<void>(petitionCli.NotifyUserChat2(user, chatMessage));
            }
            return false;

        case SPT_GM_PET:
            if (!g_PetitionSetting.UseWebPetition && user->m_data->m_nBuilder > 0 && user->m_data->m_nBuilder < 7) {
                const double z = user->GetPos().z;
                const double y = user->GetPos().y;
                const double x = user->GetPos().x;
                const char16_t* const actorName = user->GetName();
                const unsigned int actorDatabaseId = user->GetDBID();
                CLogFormatter formatter;
                // formatter.WriteChatLog(
                //     SPT_GM_PET,
                //     static_cast<std::int64_t>(actorDatabaseId),
                //     0,
                //     runtime::Win64CvttDoubleToInt32(x),
                //     runtime::Win64CvttDoubleToInt32(y),
                //     runtime::Win64CvttDoubleToInt32(z),
                //     hostFilteredMessage,
                //     const_cast<wchar_t*>(actorName),
                //     u"")
                formatter.WriteChatLog(
                    SPT_GM_PET, static_cast<std::int64_t>(actorDatabaseId), 0, runtime::Win64CvttDoubleToInt32(x),
                    runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z), FixedUtf16View(filteredMessage),
                    actorName, u""
                );
                static_cast<void>(petitionCli.NotifyGmChat(
                    user, std::u16string(message.begin(), message.begin() + countedMessageLength).c_str()
                ));
            }
            return false;

        case SPT_MARKET:
            user->Market(chatMessage);
            return false;

        case SPT_ALLIANCE: {
            CPledge* const pledge = user->GetPledge();
            CAlliance* const alliance = pledge != nullptr ? pledge->GetAlliance() : nullptr;
            if (alliance == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_ALLIANCE_CHAT_NOT_IN_ALLIANCE);
                return false;
            }
            alliance->Say(user, chatMessage, SPT_ALLIANCE, true, u"");
            CSPointer<User> relayUser(user, ObjectRefType::FunctionChain);
            RelayClient* const relayClient = RelayClient::Inst();
            relayClient->SendSayAlliance(std::move(relayUser), chatMessage);
            return false;
        }

        case SPT_PARTY_ROOM_CHAT: {
            CSPointer<User> roomUser(user, ObjectRefType::FunctionChain);
            CPartyMatchingRequest& partyMatching = Singleton<CPartyMatchingRequest>::inst();
            static_cast<void>(partyMatching.SayInRoom(std::move(roomUser), chatMessage));
            return false;
        }

        case SPT_COMMANDER_CHAT: {
            CMultiPartyCommandChannelManager manager;
            manager.Command(CSPointer<User>(user, ObjectRefType::FunctionChain), chatMessage);
            return false;
        }

        case SPT_INTER_PARTYMASTER_CHAT: {
            CMultiPartyCommandChannelManager manager;
            manager.InterPartyMasterChat(CSPointer<User>(user, ObjectRefType::FunctionChain), chatMessage);
            return false;
        }

        case SPT_HERO:
            user->Harangue(chatMessage);
            return false;

        case SPT_DOMINIONWAR: {
            if (!CDominionManager::Inst()->IsInReadyToDominionSiege()) {
                return false;
            }
            CDominion* const dominion = g_ResidenceDB.GetDominion(user->m_data->m_nDominionSiegeId);
            if (dominion == nullptr) {
                return false;
            }
            const double z = user->GetPos().z;
            const double y = user->GetPos().y;
            const double x = user->GetPos().x;
            const char16_t* const actorName = user->GetName();
            const unsigned int actorDatabaseId = user->GetDBID();
            CLogFormatter formatter;
            // formatter.WriteChatLog(
            //     SPT_DOMINIONWAR,
            //     static_cast<std::int64_t>(actorDatabaseId),
            //     0,
            //     runtime::Win64CvttDoubleToInt32(x),
            //     runtime::Win64CvttDoubleToInt32(y),
            //     runtime::Win64CvttDoubleToInt32(z),
            //     hostFilteredMessage,
            //     const_cast<wchar_t*>(actorName),
            //     u"")
            formatter.WriteChatLog(
                SPT_DOMINIONWAR, static_cast<std::int64_t>(actorDatabaseId), 0, runtime::Win64CvttDoubleToInt32(x),
                runtime::Win64CvttDoubleToInt32(y), runtime::Win64CvttDoubleToInt32(z), FixedUtf16View(filteredMessage),
                actorName, u""
            );
            dominion->Say(user, chatMessage, SPT_DOMINIONWAR);
            return false;
        }

        case SPT_MPCC_ROOM_CHAT: {
            CSPointer<User> roomUser(user, ObjectRefType::FunctionChain);
            CPartyMatchingRequest& partyMatching = Singleton<CPartyMatchingRequest>::inst();
            static_cast<void>(partyMatching.SayInMPCCRoom(std::move(roomUser), chatMessage));
            return false;
        }

        case SPT_WORLD:
            user->WorldSay(chatMessage);
            return false;

        case SPT_PLEDGE_INRAIDSERVER: {
            std::array<SharedUtilsWideChar, 28> targetName;
            std::memset(targetName.data(), 0, 0x32);
            ::Disassemble(trailingPacket, "S", 0x32, targetName.data());
            CPledge* const pledge = user->GetPledge();
            if (pledge == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_CLAN_CHAT_NOT_IN_CLAN);
                return false;
            }
            const char16_t* const hostTargetName = reinterpret_cast<const char16_t*>(targetName.data());
            pledge->Say(user, chatMessage, SPT_PLEDGE_INRAIDSERVER, true, hostTargetName);
            return false;
        }

        case SPT_ALLIANCE_INRAIDSERVER: {
            std::array<SharedUtilsWideChar, 28> targetName;
            std::memset(targetName.data(), 0, 0x32);
            ::Disassemble(trailingPacket, "S", 0x32, targetName.data());
            CPledge* const pledge = user->GetPledge();
            CAlliance* const alliance = pledge != nullptr ? pledge->GetAlliance() : nullptr;
            if (alliance == nullptr) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_USE_ALLIANCE_CHAT_NOT_IN_ALLIANCE);
                return false;
            }
            const char16_t* const hostTargetName = reinterpret_cast<const char16_t*>(targetName.data());
            alliance->Say(user, chatMessage, SPT_ALLIANCE_INRAIDSERVER, true, hostTargetName);
            return false;
        }

        default:
            // Log.Add(
            //     LOG_ERROR,
            //     "Invalid Say Type : %d char_name[%s]",
            //     sayType,
            //     user->GetName())
            // Log.Add(LOG_WARN,
            //     L"[chat] Rejected unsupported chat type %d from character \"%ls\".",
            //     sayType, user->GetName())
            // L2_LOG_WARN(Log,
            //     L"[chat] Rejected unsupported chat type %d from character \"%ls\".",
            //     sayType, user->GetName())
            L2_LOG_WARN(Log, u"[chat] Rejected unsupported chat type %d from character \"%ls\".", sayType, user->GetName());
            return true;
    }
}

bool MSNChatLogPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c15b4c.
    L2_GUARD_FUNCTION();
    if (!g_bLogMsnMsg) {
        return false;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    std::array<SharedUtilsWideChar, 256> message;
    message[0] = SharedUtilsWideChar{};
    std::memset(message.data() + 1, 0, 0x1fe);
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 256> remoteName;
    remoteName[0] = SharedUtilsWideChar{};
    std::memset(remoteName.data() + 1, 0, 0x1fe);
    std::memset(remoteName.data(), 0, 0x200);
    int direction = 0;
    ::Disassemble(packet, "SSd", 0x200, message.data(), 0x200, remoteName.data(), &direction);

    const FVector position = user->GetPos();
    std::array<SharedUtilsWideChar, 256> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x1fe);
    std::copy_n(message.data(), message.size(), filteredMessage.data());
    for (std::size_t index = 0; index < TargetWideLength(filteredMessage.data()); ++index) {
        if (filteredMessage[index] == static_cast<SharedUtilsWideChar>(',')) {
            filteredMessage[index] = static_cast<SharedUtilsWideChar>(' ');
        }
    }
    if (TargetWideLength(remoteName.data()) > 0x20U) {
        remoteName[0x1f] = SharedUtilsWideChar{};
    }

    CLogFormatter formatter;
    if (direction == 0) {
        const char16_t* const userName = user->GetName();
        const unsigned int userDatabaseId = user->GetDBID();
        // formatter.WriteChatLog(
        //     0x0d,
        //     static_cast<std::int64_t>(userDatabaseId),
        //     0,
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     hostMessage,
        //     const_cast<wchar_t*>(userName),
        //     hostRemoteName)
        formatter.WriteChatLog(
            0x0d, static_cast<std::int64_t>(userDatabaseId), 0, runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
            FixedUtf16View(message), userName, remoteName.data()
        );
    } else if (direction == 1) {
        const char16_t* const userName = user->GetName();
        const unsigned int userDatabaseId = user->GetDBID();
        // formatter.WriteChatLog(
        //     0x0d,
        //     0,
        //     static_cast<std::int64_t>(userDatabaseId),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     hostMessage,
        //     hostRemoteName,
        //     const_cast<wchar_t*>(userName))
        formatter.WriteChatLog(
            0x0d, 0, static_cast<std::int64_t>(userDatabaseId), runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
            FixedUtf16View(message), remoteName.data(), userName
        );
    }
    return false;
}

bool ReplyL2FriendList(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c15a68.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->m_data->m_nWorldId == g_nWorldId) {
        user->OnRequestL2FriendList();
    } else {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_UNAVAILABLE_AFTER_DIMENSIONAL_TRANSFER);
    }
    return false;
}

bool L2FriendSayPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c20ffc.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    std::array<SharedUtilsWideChar, 520> message;
    message[0] = SharedUtilsWideChar{};
    std::memset(message.data() + 1, 0, 0x400);
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 28> targetName;
    targetName[0] = SharedUtilsWideChar{};
    std::memset(targetName.data() + 1, 0, 0x30);
    std::array<SharedUtilsWideChar, 28> lowerTargetName;
    lowerTargetName[0] = SharedUtilsWideChar{};
    std::memset(lowerTargetName.data() + 1, 0, 0x30);
    std::memset(targetName.data(), 0, 0x32);
    std::memset(lowerTargetName.data(), 0, 0x32);
    ::Disassemble(packet, "SS", 0x402, message.data(), 0x32, targetName.data());
    ::ToLowerCase(targetName.data(), lowerTargetName.data());

    const auto sendReply = [&](const int result) {
#if defined(_WIN32)
        const char16_t* const sourceName = reinterpret_cast<const char16_t*>(user->GetName());
#else
        const std::u16string sourceNameStorage = std::u16string(user->GetName());
        const char16_t* const sourceName = sourceNameStorage.c_str();
#endif
        user->GetSocket()->Send(
            "cdSSS", 0x78, result, reinterpret_cast<const char16_t*>(targetName.data()), sourceName,
            reinterpret_cast<const char16_t*>(message.data())
        );
    };

    if (user->m_dwStopSay != 0 || !user->m_bCanSay) {
        sendReply(0x93);
        return false;
    }

    runtime::raw_atomic::IncrementI32(&g_nSayCount);
    const std::size_t messageLength = TargetWideLength(message.data());
    runtime::raw_atomic::AddI32(&g_nSaySize, static_cast<std::int32_t>(messageLength));

    const std::u16string hostLowerTargetNameUtf16(FixedUtf16View(lowerTargetName));
    CSPointer<User> target = GetUserFromNameMap(g_nWorldId, hostLowerTargetNameUtf16);
    if (!target) {
        sendReply(0x91);
        return false;
    }

    CSPointer<CFriends> friends = user->GetFriends();
    const bool mayTalk = (friends && friends->HasFriend(target->GetDBID())) ||
        user->IsMyMentor(static_cast<int>(target->GetDBID())) || user->IsMyMentee(static_cast<int>(target->GetDBID()));
    if (!mayTalk) {
        sendReply(0x1e6);
        return false;
    }

    user->SayToFriend(target, reinterpret_cast<const char16_t*>(message.data()));
    if (target->m_dwStopSay != 0 || !target->m_bCanSay) {
        sendReply(0x437);
    }

    const FVector position = user->GetPos();
    std::array<SharedUtilsWideChar, 520> filteredMessage;
    filteredMessage[0] = SharedUtilsWideChar{};
    std::memset(filteredMessage.data() + 1, 0, 0x400);
    std::size_t inputIndex = 0;
    std::size_t outputIndex = 0;
    while (inputIndex < messageLength) {
        SharedUtilsWideChar value = message[inputIndex];
        if (value == static_cast<SharedUtilsWideChar>(',')) {
            value = static_cast<SharedUtilsWideChar>(' ');
            filteredMessage[outputIndex++] = value;
        } else if (value != static_cast<SharedUtilsWideChar>('\b')) {
            filteredMessage[outputIndex++] = value;
        } else {
            bool copyFormattingSegment = false;
            for (;;) {
                ++inputIndex;
                value = message[inputIndex];
                if (value == static_cast<SharedUtilsWideChar>('\b') || value == SharedUtilsWideChar{}) {
                    break;
                }
                if (value == static_cast<SharedUtilsWideChar>(0x1b)) {
                    copyFormattingSegment = !copyFormattingSegment;
                }
                if (copyFormattingSegment) {
                    filteredMessage[outputIndex++] = value;
                }
            }
        }
        ++inputIndex;
    }

    CLogFormatter formatter;
    // formatter.WriteChatLog(
    //     0x0c,
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(target->GetDBID()),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     hostFilteredMessage,
    //     const_cast<wchar_t*>(user->GetName()),
    //     const_cast<wchar_t*>(target->GetName()))
    formatter.WriteChatLog(
        0x0c, static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(target->GetDBID()),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), FixedUtf16View(filteredMessage), user->GetName(), target->GetName()
    );
    return false;
}

bool FriendAddReply(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0c678.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char replyType;
    int result = 0;
    ::Disassemble(packet, "cd", &replyType, &result);
    if (replyType == 1) {
        return user->OnAddFriendReply(result, false);
    }
    if (replyType == 4) {
        return user->OnAddFriendReply(result, true);
    }
    return false;
}

bool FriendAddRequest(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1f0ec.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<char16_t, 28> targetName;
    targetName[0] = SharedUtilsWideChar{};
    std::memset(targetName.data() + 1, 0, 0x30);
    ::Disassemble(packet, "S", 0x32, targetName.data());
    if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass()))) {
        socket->SendSystemMessage(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_IN_CURRENT_STATE);
        return false;
    }

    const CSPointer<User> target = GetUserFromNameMap(g_nWorldId, std::u16string_view(targetName.data()));
    if (target && User::IsInFifthClassGroup(static_cast<ClassType>(target->GetClass()))) {
        socket->SendSystemMessage_Char(SystemMessageId::FUNCTION_NOT_YET_AVAILABLE_TO_NAMED_CHARACTER, target->GetName());
        return false;
    }
    return user->OnAddFriendRequest(targetName.data(), false);
}

bool FriendRemove(CSocket* baseSocket, const char16_t* friendName) {
    // Current L2Server 0x00c1a5a8.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CSPointer<CFriends> friends = user->GetFriends();
    if (!friends) {
        socket->SendSystemMessage(SystemMessageId::FRIEND_LIST_NOT_READY_FOR_USE);
        return false;
    }
    const unsigned int friendDatabaseId = friends->GetFriendDbId(friendName);
    if (friendDatabaseId == 0) {
        socket->SendSystemMessage(SystemMessageId::USER_NOT_IN_FRIEND_LIST);
        return false;
    }
    if (user->m_nSpouseDbID == static_cast<int>(friendDatabaseId)) {
        db.RequestUpdateWedding(*user, static_cast<int>(friendDatabaseId), 2);
    } else {
        db.RequestBreakFriendship(user, friendDatabaseId);
    }
    return false;
}

bool FriendRemove(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c1a748.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 28> friendName;
    ::Disassemble(packet, "S", 0x30, friendName.data());
    return FriendRemove(baseSocket, reinterpret_cast<const char16_t*>(friendName.data()));
}

bool GMListPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d6cc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CGMListMgr::inst().SendGMList(socket);
    runtime::raw_atomic::IncrementI32(&socket->m_UseSendGMListCount);
    int elapsed = 0;
    if (socket->m_LastUseSendGMListTime != 0) {
        elapsed = runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), socket->m_LastUseSendGMListTime);
    }
    const int requestCount = runtime::raw_atomic::LoadI32Relaxed(&socket->m_UseSendGMListCount);
    CLogFormatter formatter;
    formatter.WriteAuditLog1(0x5a3, user, nullptr, nullptr, nullptr, nullptr, requestCount, elapsed, 0, 0, 0, 0, 0);
    socket->m_LastUseSendGMListTime = runtime::crt::Time32Now();
    return false;
}

bool SnoopQuit(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c20ed4.
    L2_GUARD_FUNCTION();
    static_cast<void>(baseSocket);

    int userServerId = 0;
    ::Disassemble(packet, "d", &userServerId);
    CSPointer<User> user = g_UserMap.FindObject(userServerId);
    if (user && user->m_nSnoopGmId > 0) {
        static_cast<void>(user->SetSnoop(0));
    }
    return false;
}

bool ObserverEndPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e500.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (!user->m_data->m_bGMInvisible || !user->m_bIsObserving) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"observer end without starting -_- username[%s]",
        //     user->GetName())
        // Log.Add(
        //     LOG_WARN,
        //     L"[observer] Exit request ignored for character %s on socket %u: the required observer/invisibility state is
        //     not active.", user->GetName(), socket->m_nId)
        // L2_LOG_WARN(Log,
        //     L"[observer] Exit request ignored for character %s on socket %u: the required observer/invisibility state is
        //     not active.", user->GetName(), socket->m_nId)
        L2_LOG_WARN(
            Log,
            u"[observer] Exit request ignored for character %s on socket %u: the required observer/invisibility state is "
            u"not active.",
            user->GetName(), socket->m_nId
        );
    } else if (!user->m_data->m_bIsActive) {
        // Noise: Observer exit is ignored while the character is inactive. This branch measures no request interval.
        // Log.Add(
        //     LOG_ERROR,
        //     L"too fast observer end, ignore")
    } else {
        user->ObserverEnd();
    }
    return false;
}

bool RequestVoteSociality(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0e61c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    int targetServerId;
    ::Disassemble(packet, "d", &targetServerId);
    return user->OnVoteSocialityPacket(targetServerId);
}

void CheckResurrectable(CCreature* creature, const bool answer, const int requestId) {
    // Current L2Server 0x00c0e958.
    L2_GUARD_FUNCTION();

    if (static_cast<std::uint32_t>(requestId) != creature->m_data->m_dwResurrectTime) {
        return;
    }
    if (creature->IsAlive()) {
        return;
    }
    if (answer) {
        static_cast<void>(creature->ResurrectBy(nullptr, creature->m_data->m_nResurrectExp));
        return;
    }

    runtime::raw_atomic::ExchangeI32(&creature->m_data->m_bCanResurrection, 0);
    creature->m_data->m_nResurrectExp = 0;
    creature->m_data->m_nResurrectRatio = 0;
    creature->m_data->m_dwResurrectTime = runtime::GetTickCount();
    if (creature->m_SkillDurationMod.m_nResurrectionSpecial > 0) {
        --creature->m_SkillDurationMod.m_nResurrectionSpecial;
    }
}

bool ConfirmDlg_Resurretion(CUserSocket* socket, User* user, const bool answer, const int requestId) {
    // Current L2Server 0x00c0ea98. Target spelling is "Resurretion".
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    CCreature* resurrectable = user;
    if (user->m_data->m_bCanResurrection == 0) {
        resurrectable = user->GetPet();
        if (resurrectable == nullptr) {
            return false;
        }
    }
    CheckResurrectable(resurrectable, answer, requestId);
    return false;
}

bool ConfirmDlg_CallPC(CUserSocket* socket, User* user, const bool answer, const int requestId) {
    // Current L2Server 0x00c0eb70.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    user->CallPCAnswer(requestId, answer);
    return false;
}

bool ConfirmDlg_WithdrawAuctionBid(CUserSocket* socket, User* user, const bool answer, const int requestId) {
    // Current L2Server 0x00c2ab94.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);

    if (answer) {
        CAuction* const auction = CAuctionManager::inst().GetAuction(requestId);
        if (auction != nullptr) {
            static_cast<void>(auction->WithdrawBiddingPrice(user));
        }
    }
    return false;
}

bool ConfirmDlg_OpenDoor(CUserSocket* socket, User* user, const bool answer, const int requestId) {
    // Current L2Server 0x00c2bf74.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    static_cast<void>(requestId);

    if (!answer) {
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[6].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0x474)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] OpenDoor failed User[%s] Reason[%d]",
        //     kUserSocketSource,
        //     0x2602,
        //     user->GetName(),
        //     g_TalkChannelError)
        // Log.Add(
        //     LOG_WARN,
        //     L"[door] Open confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x). No
        //     door operation was requested.", user->GetName(), g_TalkChannelError)
        // L2_LOG_WARN(Log,
        //     L"[door] Open confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x). No
        //     door operation was requested.", user->GetName(), g_TalkChannelError)
        L2_LOG_WARN(
            Log,
            u"[door] Open confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x); no "
            u"door operation was requested.",
            user->GetName(), g_TalkChannelError
        );
        return false;
    }

    CDoor* const door = user->m_spTalkTarget.GetDoor();
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door",
        //     kUserSocketSource,
        //     0x2609)
        // Log.Add(
        //     LOG_WARN,
        //     "[door] Open confirmation ignored for character runtime ID %u: the current interaction target has no available
        //     door, no door operation was requested.", user->m_nId)
        L2_LOG_WARN(
            Log,
            "[door] Open confirmation ignored for character runtime ID %u: the current interaction target has no available "
            "door; no door operation was requested.",
            user->m_nId
        );
    } else if (!door->IsOpened()) {
        door->Open(false, false);
    } else {
        // Noise: The door already has the requested state. Repeating the confirmation requires no operation.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Door is not closed",
        //     kUserSocketSource,
        //     0x260f)
    }
    return false;
}

bool ConfirmDlg_CloseDoor(CUserSocket* socket, User* user, const bool answer, const int requestId) {
    // Current L2Server 0x00c2c120.
    L2_GUARD_FUNCTION();
    static_cast<void>(socket);
    static_cast<void>(requestId);

    if (!answer) {
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[6].IsValidChannel(user->Id(), user->m_spTalkTarget.Sid(), 0xfa, 0x475)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CloseDoor failed User[%s] Reason[%d]",
        //     kUserSocketSource,
        //     0x2626,
        //     user->GetName(),
        //     g_TalkChannelError)
        // Log.Add(
        //     LOG_WARN,
        //     L"[door] Close confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x). No
        //     door operation was requested.", user->GetName(), g_TalkChannelError)
        // L2_LOG_WARN(Log,
        //     L"[door] Close confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x). No
        //     door operation was requested.", user->GetName(), g_TalkChannelError)
        L2_LOG_WARN(
            Log,
            u"[door] Close confirmation ignored for character %s: interaction validation failed (reason flags 0x%02x); no "
            u"door operation was requested.",
            user->GetName(), g_TalkChannelError
        );
        return false;
    }

    CDoor* const door = user->m_spTalkTarget.GetDoor();
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid door",
        //     kUserSocketSource,
        //     0x262d)
        // Log.Add(
        //     LOG_WARN,
        //     "[door] Close confirmation ignored for character runtime ID %u: the current interaction target has no
        //     available door, no door operation was requested.", user->m_nId)
        L2_LOG_WARN(
            Log,
            "[door] Close confirmation ignored for character runtime ID %u: the current interaction target has no available "
            "door; no door operation was requested.",
            user->m_nId
        );
    } else if (door->IsOpened()) {
        door->Close(false, false);
    } else {
        // Noise: The door already has the requested state. Repeating the confirmation requires no operation.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Door is not open",
        //     kUserSocketSource,
        //     0x2633)
    }
    return false;
}

bool CUserSocket::ConfirmDialog(CSocket* baseSocket, const unsigned char* packet) {
    // Current L2Server 0x00c0ec28.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        socket->Send("cd", 0x1f, 0);
        return true;
    }

    int messageId;
    int answer;
    int requestId;
    ::Disassemble(packet, "ddd", &messageId, &answer, &requestId);
    for (ConfirmDialogFuncTable& entry : g_ConfirmDialogTable) {
        if (entry.MessageNum == messageId) {
            return entry.Func(socket, user, answer != 0, requestId);
        }
    }
    return false;
}

bool PetitionCancelPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d5fc.
    L2_GUARD_FUNCTION();
    static_cast<void>(packet);

    if (g_PetitionSetting.UseWebPetition) {
        return true;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user != nullptr) {
        static_cast<void>(petitionCli.ReqCancelPetition(user, nullptr));
    }
    return user == nullptr;
}

bool ChangePetNamePacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0d81c.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<SharedUtilsWideChar, 4000> petName{};
    std::array<char, 4000> ansiName{};
    ::Disassemble(packet, "S", 8000, petName.data());
    static_assert(sizeof(SharedUtilsWideChar) == sizeof(char16_t));
    const int convertedLength = UnicodeToAnsi(
        reinterpret_cast<char16_t*>(petName.data()), static_cast<int>(TargetWideLength(petName.data())), ansiName.data(),
        static_cast<int>(ansiName.size())
    );
    if (convertedLength < 18) {
        CPet* const pet = user->GetPet();
        if (pet == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ChangePetNamePacket failed. don't have pet",
            //     kUserSocketSource,
            //     0x1ba9)
            // Log.Add(
            //     LOG_WARN,
            //     "[pet] Rename request ignored on socket %u: the character has no active pet.",
            //     socket->m_nId)
            L2_LOG_WARN(Log, "[pet] Rename request ignored on socket %u: the character has no active pet.", socket->m_nId);
        } else {
            static_cast<void>(pet->ChangePetName(reinterpret_cast<const char16_t*>(petName.data())));
        }
    } else {
        user->SendSystemMessage(SystemMessageId::PET_NAME_LENGTH_LIMIT);
    }
    return false;
}

bool RequestPetitionVote(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c0ef14.
    L2_GUARD_FUNCTION();

    if (g_PetitionSetting.UseWebPetition) {
        return true;
    }

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    std::array<int, 50> petitionIds{};
    if (user->m_nWaitPetitionVote == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No petition vote is allowed.[%d]",
        //     user->GetDBID())
        // Log.Add(
        //     LOG_WARN,
        //     "[petition] Vote ignored for character DB ID %u on socket %u: no vote is pending.",
        //     user->GetDBID(), socket->m_nId)
        L2_LOG_WARN(
            Log, "[petition] Vote ignored for character DB ID %u on socket %u: no vote is pending.", user->GetDBID(),
            socket->m_nId
        );
        return false;
    }

    int petitionCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &petitionCount);
    if (static_cast<std::uint32_t>(petitionCount) > 50U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Petition vote count is exceed max.[%d]",
        //     petitionCount)
        // Log.Add(
        //     LOG_WARN,
        //     "[petition] Vote ignored on socket %u: requested petition count %d is outside 0..50. No vote was submitted.",
        //     socket->m_nId, petitionCount)
        L2_LOG_WARN(
            Log,
            "[petition] Vote ignored on socket %u: requested petition count %d is outside 0..50; no vote was submitted.",
            socket->m_nId, petitionCount
        );
        return false;
    }

    for (int index = 0; index < petitionCount; ++index) {
        cursor = ::Disassemble(cursor, "d", &petitionIds[static_cast<std::size_t>(index)]);
    }

    std::array<SharedUtilsWideChar, 512> message{};
    ::Disassemble(cursor, "S", 0x400, message.data());
    const int messageLength = static_cast<int>(TargetWideLength(message.data()));
    std::wstring hostMessage = target_utf16::ToHostCodeUnits(message.data(), static_cast<std::size_t>(messageLength));
    user->SendPetitionVote(petitionIds.data(), petitionCount, hostMessage.data(), messageLength);
    return false;
}

bool PetitionPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c141f4.
    L2_GUARD_FUNCTION();

    if (g_PetitionSetting.UseWebPetition) {
        return true;
    }

    int categoryId = 0;
    std::array<char16_t, 0x4000> text;
    ::Disassemble(packet, "Sd", 0x8000, text.data(), &categoryId);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->IsFrozen(true)) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_SUBMIT_PETITION_WHILE_FROZEN);
        return false;
    }

    static_cast<void>(petitionCli.ReqSubmitPetition(user, text.data(), categoryId, nullptr));
    return false;
}

bool PetUseItemPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c14770.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);
    CPet* const pet = user->GetPet();
    if (pet == nullptr) {
        const unsigned int userDatabaseId = user->GetDBID();
        const char16_t* const userName = user->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PetUseItemPacket failed. don't have pet [%s, %d]",
        //     kUserSocketSource,
        //     0x1bbf,
        //     userName,
        //     userDatabaseId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[pet] Item-use request ignored for character %s (DB ID %u) on socket %u: no active pet is available
        //     (requested item runtime ID %u).", userName, userDatabaseId, socket->m_nId,
        //     static_cast<std::uint32_t>(itemServerId))
        // L2_LOG_WARN(Log,
        //     L"[pet] Item-use request ignored for character %s (DB ID %u) on socket %u: no active pet is available
        //     (requested item runtime ID %u).", userName, userDatabaseId, socket->m_nId,
        //     static_cast<std::uint32_t>(itemServerId))
        L2_LOG_WARN(
            Log,
            u"[pet] Item-use request ignored for character %s (DB ID %u) on socket %u: no active pet is available "
            u"(requested item runtime ID %u).",
            userName, userDatabaseId, socket->m_nId, static_cast<std::uint32_t>(itemServerId)
        );
        return false;
    }

    CItem* const item = pet->m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        user->LogAbnormalAction(AUA_TRY_TO_PET_USE_INVALID_ITEM, false, 0, 0, 0, 0);
        return false;
    }
    if (!pet->IsAlive()) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_USE_ITEMS_WHILE_DEAD);
        return false;
    }
    if (pet->IsPet()) {
        CPet* const castPet = pet->CastPet();
        auto* const foodSkill = static_cast<CSkillInfo*>(item->m_const->m_pSkill);
        if (castPet->m_data->m_bPetActionBlocked && (foodSkill == nullptr || !foodSkill->m_bPetFoodSkill)) {
            socket->SendSystemMessage(SystemMessageId::PET_CANNOT_BE_CONTROLLED_WITH_EMPTY_HUNGER_GAUGE);
            return false;
        }
    }

    static_cast<void>(pet->UseItem(*item, 0, false, 0));
    return false;
}

bool PetGetItem(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c18bd4.
    L2_GUARD_FUNCTION();

    int itemServerId = 0;
    ::Disassemble(packet, "d", &itemServerId);

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr || user->m_data->m_bIsAlive == 0) {
        return false;
    }

    CPet* const pet = user->GetPet();
    if (pet == nullptr) {
        user->SendSystemMessage(SystemMessageId::NO_PET);
        return false;
    }

    CItem* const item = ItemSmartIdManager()->GetObjectW(itemServerId);
    if (item == nullptr) {
        return false;
    }
    if (item->m_const->m_EtcitemType == 8) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_GIVEN_TO_PET);
        return false;
    }
    if (pet->m_data->m_bPetActionBlocked) {
        socket->SendSystemMessage(SystemMessageId::PET_CANNOT_BE_CONTROLLED_WITH_EMPTY_HUNGER_GAUGE);
        return false;
    }
    if (!item->CanGetByPet(user)) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_GIVEN_TO_PET);
        return false;
    }
    if (!item->m_const->m_bExImmediateEffect && !pet->m_inv.CheckAddable(item->GetIndex(), false)) {
        return false;
    }

    static_cast<void>(pet->GetItem(item, false));
    return false;
}

bool GiveItemToPetPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c28130.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    if (user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr) {
        user->SendSystemMessage(SystemMessageId::ENCHANT_OR_ATTRIBUTE_ENCHANT_IN_PROGRESS);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr) {
        user->SendSystemMessage(SystemMessageId::ENCHANT_OR_ATTRIBUTE_ENCHANT_IN_PROGRESS);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE) != nullptr) {
        user->SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_IN_PROGRESS);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        user->SendSystemMessage(SystemMessageId::ITEM_APPEARANCE_MODIFICATION_OR_RESTORATION_IN_PROGRESS);
        return false;
    }
    if (user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        user->SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_IN_PROGRESS);
        return false;
    }
    if (user->GetTrade() != nullptr) {
        return false;
    }

    int itemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "dQ", &itemServerId, &amount);
    if (amount <= 0) {
        return false;
    }

    const std::uint32_t tick = runtime::GetTickCount();
    if (tick - user->m_PetActionLastTick <= 999U) {
        return false;
    }
    user->m_PetActionLastTick = tick;

    CItem* const item = user->m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GiveItemToPet failed. (invalid item)",
        //     kUserSocketSource,
        //     0x1c2c)
        // Log.Add(
        //     LOG_WARN,
        //     "[pet items] Transfer to pet rejected on socket %u for item runtime ID %u, amount %lld: the item was not found
        //     in the character inventory. No transfer was requested.", socket->m_nId,
        //     static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount))
        L2_LOG_WARN(
            Log,
            "[pet items] Transfer to pet rejected on socket %u for item runtime ID %u, amount %lld: the item was not found "
            "in the character inventory; no transfer was requested.",
            socket->m_nId, static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount)
        );
        return false;
    }
    if (item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0) {
        return false;
    }
    if (!item->CanGetByPet(user)) {
        user->SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_GIVEN_TO_PET);
        return false;
    }
    if (!user->m_inv.Have(item->m_data->index)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GiveItemToPet failed. (not owning item)",
        //     kUserSocketSource,
        //     0x1c42)
        // Log.Add(
        //     LOG_WARN,
        //     "[pet items] Transfer to pet rejected on socket %u for item runtime ID %u, amount %lld: the inventory
        //     ownership check failed, no transfer was requested.", socket->m_nId, static_cast<std::uint32_t>(itemServerId),
        //     static_cast<long long>(amount))
        L2_LOG_WARN(
            Log,
            "[pet items] Transfer to pet rejected on socket %u for item runtime ID %u, amount %lld: the inventory ownership "
            "check failed; no transfer was requested.",
            socket->m_nId, static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount)
        );
        return false;
    }

    CCreature* const petCreature = g_CreatureSmartIdManager.GetObjectW(user->m_PetSID);
    if (petCreature == nullptr || !petCreature->IsPet()) {
        return false;
    }
    CPet* const pet = petCreature->CastPet();
    if (pet->GetMaster() != user) {
        return false;
    }
    if (petCreature->m_data->m_bIsAlive == 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_GIVE_ITEMS_TO_DEAD_PET);
        return false;
    }
    if (!pet->m_inv.CheckAddable(item->m_data->m_nClassID, amount, false)) {
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> emptyPacket;
    User::etcMsg message =
        user->SaveEtcMsg(false, false, static_cast<int>(item->Id()), 0, amount, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_PET_ITEM, 0, 0, nullptr, nullptr, 0, emptyPacket, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        db.GiveItemToPet(user, item, amount);
    } else if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == User::MRT_JOBNOTMAP) {
        // Noise: MRT_JOBNOTMAP means the transfer was queued behind existing work. Normal queue insertion is not an error.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kUserSocketSource,
        //     0x1c78)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     kUserSocketSource,
        //     0x1c7d)
        // Log.Add(
        //     LOG_ERROR,
        //     "[pet items] Transfer to pet stopped on socket %u for item runtime ID %u, amount %lld: unexpected item-job
        //     result %d. No immediate database transfer was requested.", socket->m_nId,
        //     static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount), static_cast<int>(result))
        L2_LOG_ERROR(
            Log,
            "[pet items] Transfer to pet stopped on socket %u for item runtime ID %u, amount %lld: unexpected item-job "
            "result %d; no immediate database transfer was requested.",
            socket->m_nId, static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount), static_cast<int>(result)
        );
    }
    return false;
}

bool GetItemFromPetPacket(CSocket* baseSocket, const std::uint8_t* packet) {
    // Current L2Server 0x00c287f4.
    L2_GUARD_FUNCTION();

    auto* const socket = static_cast<CUserSocket*>(baseSocket);
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (user->GetTrade() != nullptr) {
        return false;
    }

    int itemServerId = 0;
    std::int64_t amount = 0;
    int unused = 0;
    ::Disassemble(packet, "dQd", &itemServerId, &amount, &unused);
    if (user->m_data->m_bIsAlive == 0 || amount < 1) {
        return false;
    }

    CPet* const pet = user->GetPet();
    if (pet == nullptr || pet->m_data->m_bIsAlive == 0) {
        return false;
    }
    CItem* const item = pet->m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        return false;
    }
    if (!user->m_inv.CheckAddable(item->m_data->m_nClassID, amount, false)) {
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> emptyPacket;
    User::etcMsg message =
        user->SaveEtcMsg(false, false, static_cast<int>(item->Id()), 1, amount, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_PET_ITEM, 0, 0, nullptr, nullptr, 0, emptyPacket, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        db.GetItemFromPet(user, item, amount);
    } else if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == User::MRT_JOBNOTMAP) {
        // Noise: MRT_JOBNOTMAP means the transfer was queued behind existing work. Normal queue insertion is not an error.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     kUserSocketSource,
        //     0x1cd5)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     kUserSocketSource,
        //     0x1cda)
        // Log.Add(
        //     LOG_ERROR,
        //     "[pet items] Transfer from pet stopped on socket %u for item runtime ID %u, amount %lld: unexpected item-job
        //     result %d. No immediate database transfer was requested.", socket->m_nId,
        //     static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount), static_cast<int>(result))
        L2_LOG_ERROR(
            Log,
            "[pet items] Transfer from pet stopped on socket %u for item runtime ID %u, amount %lld: unexpected item-job "
            "result %d; no immediate database transfer was requested.",
            socket->m_nId, static_cast<std::uint32_t>(itemServerId), static_cast<long long>(amount), static_cast<int>(result)
        );
    }
    return false;
}
