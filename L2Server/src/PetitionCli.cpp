#include "PetitionCli.h"
#include "CConfig.h"
#include "CWorld.h"
#include "PetitionSocket.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

CPetitionCli petitionCli;

namespace {

void ClosePetitionNativeSocket(const std::uint64_t nativeSocket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(nativeSocket));
#else
    ::close(static_cast<int>(nativeSocket));
#endif
}

int TargetUtf16Length(const char16_t* const value) noexcept {
    int length = 0;
    while (value[length] != u'\0') {
        ++length;
    }
    return length;
}


std::u16string PacketWideLanesToTargetUtf16(const wchar_t* const value) {
    if (value == nullptr) {
        return {};
    }

    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        const auto* const target = reinterpret_cast<const char16_t*>(value);
        return std::u16string(target);
    }

    std::u16string result;
    for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
        // Portable Disassemble widens each target UTF-16 lane into one
        // host wchar_t lane. Narrow it back one-for-one: this is a packet
        // carrier, not a Unicode scalar-value conversion.
        result.push_back(static_cast<char16_t>(*cursor));
    }
    return result;
}

template <std::size_t Size>
void CopyTargetUtf16Bounded(
    std::array<char16_t, Size>& destination, const char16_t* const source, const std::size_t maximumCount
) {
    std::size_t index = 0;
    while (index < maximumCount && index + 1 < destination.size() && source[index] != u'\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = u'\0';
}

void FormatPetitionLocation(
    std::array<char16_t, 0x100>& destination,
    const std::int32_t x,
    const std::int32_t y,
    const std::int32_t z,
    const int zoneX,
    const int zoneY
) {
    FormatUtf16To(destination.data(), destination.size(), u"%d, %d, %d[%d-%d]", x, y, z, zoneX, zoneY);
}

std::uint32_t PetitionWorker(void*) {
    CThreadManager::RegisterThisThread(ThreadID::PETITION);
    InitializeCurrentIOBufferPool(0x80);
    for (;;) {
        static_cast<void>(petitionCli.Init());
        if (g_PetitionSetting.heartBeat == 0) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(g_PetitionSetting.heartBeat));
        }
    }
}

} // namespace

CGMListMgr& CGMListMgr::inst() {
    static CGMListMgr instance;
    return instance;
}

bool CGMListMgr::Exist(const char16_t* const name) {
    // Current L2Server 0x009654e4.
    L2_GUARD_FUNCTION();
    const bool found = [&] {
        L2_READ_GUARD(m_lock);
        bool found = false;
        for (const std::u16string& current : m_GMCharList) {
            if (std::u16string_view(name).substr(0, 0x18) == std::u16string_view(current).substr(0, 0x18)) {
                found = true;
                break;
            }
        }
        return found;
    }();
    return found;
}

bool CGMListMgr::Insert(const char16_t* const name) {
    // Current L2Server 0x009682cc.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        for (const std::u16string& current : m_GMCharList) {
            if (std::u16string_view(name).substr(0, 0x18) == std::u16string_view(current).substr(0, 0x18)) {
                return false;
            }
        }
        m_GMCharList.push_back(std::u16string(name));
    }
    return true;
}

bool CGMListMgr::Delete(const char16_t* const name) {
    // Current L2Server 0x009679dc.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_lock);
        for (auto position = m_GMCharList.begin(); position != m_GMCharList.end(); ++position) {
            if (std::u16string_view(name).substr(0, 0x18) == std::u16string_view(*position).substr(0, 0x18)) {
                m_GMCharList.erase(position);
                return true;
            }
        }
    }
    return false;
}

void CGMListMgr::SendGMList(CSocket* const socket) {
    // Current L2Server 0x009655e8.
    L2_GUARD_FUNCTION();
    CUserSocket* const userSocket = static_cast<CUserSocket*>(socket);
    {
        L2_READ_GUARD(m_lock);
        if (m_GMCharList.empty()) {
            userSocket->SendSystemMessage(SystemMessageId::NO_GM_CURRENTLY_CONSULTING);
        } else {
            userSocket->SendSystemMessage(SystemMessageId::GM_LIST_HEADING);
            for (const std::u16string& name : m_GMCharList) {
                userSocket->SendSystemMessage_Char(SystemMessageId::GM_NAME_ENTRY, name.c_str());
            }
            userSocket->SendSystemMessage(SystemMessageId::GM_LIST_HEADING);
        }
    }
}

CPetitionCli::CPetitionCli() :
    enabled(false),
    sendable(false),
    supressed(false),
    m_pPetitionSocket(nullptr) {
    // Current L2Server 0x00968014. Built-in lanes not named above keep the
    // target's pre-constructor storage. The process-global owner starts in
    // zeroed BSS.
    L2_GUARD_FUNCTION();
    ResetCheckedOutList();
}

CPetitionCli::~CPetitionCli() {
    // Current L2Server 0x00968164 does not stop or destroy the
    // connection thread and does not delete raw checked-out entries here.
    L2_GUARD_FUNCTION();
    m_hConnectThread = nullptr;
    m_pPetitionSocket = nullptr;
}

void CPetitionCli::ConnectToPetitionD() {
    // Current L2Server 0x00964704.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        static_cast<void>(runtime::ResumeOwnedThread(m_hConnectThread));
    }
}

bool CPetitionCli::Connect() {
    // Current L2Server 0x009647a4.
    L2_GUARD_FUNCTION();
    if (sendable) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already connected to petition server")
        // L2_LOG_ERROR(Log,
        //     L"Already connected to petition server")
        L2_LOG_WARN(Log, "Petition connection request skipped: the client is already marked sendable.");
        return false;
    }

#if defined(_WIN32)
    const SOCKET created = ::socket(AF_INET, SOCK_STREAM, 0);
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);
#else
    const int created = ::socket(AF_INET, SOCK_STREAM, 0);
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);
#endif

    ::sockaddr_in remoteAddress{};
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_addr = g_PetitionSetting.serverAddr;
    remoteAddress.sin_port = htons(static_cast<std::uint16_t>(g_PetitionSetting.serverPort));
    if (::connect(created, reinterpret_cast<const ::sockaddr*>(&remoteAddress), sizeof(remoteAddress)) == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to petition server")
        // L2_LOG_ERROR(Log,
        //     L"Can't connect to petition server")
        const int socketError = portable::GetNativeSocketLastError();
        const std::uint32_t address = ntohl(remoteAddress.sin_addr.s_addr);
        L2_LOG_ERROR(
            Log,
            "Cannot connect to petition server %u.%u.%u.%u:%u (socket error %d); closing the socket and marking the client "
            "unavailable.",
            static_cast<unsigned>((address >> 24) & 255U), static_cast<unsigned>((address >> 16) & 255U),
            static_cast<unsigned>((address >> 8) & 255U), static_cast<unsigned>(address & 255U),
            static_cast<unsigned>(ntohs(remoteAddress.sin_port)), socketError
        );
        ClosePetitionNativeSocket(nativeSocket);
        sendable = false;
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to petition server")
    // L2_LOG_INFO(Log,
    //     L"Connected to petition server")
    L2_LOG_INFO(Log, "Petition TCP connection established; transport setup and server negotiation still follow.");
    m_pPetitionSocket = new CPetitionSocket(nativeSocket, petitionPacketTable.data());
    const bool bound = portable::BindNativeSocketTransport(
        m_pPetitionSocket, nativeSocket,
        [](void* owner, const std::uint8_t* data, const std::size_t length) {
            return portable::FeedSocketRead(static_cast<CPetitionSocket*>(owner), data, length);
        },
        [](void* owner, const int error) { portable::FeedSocketTerminal(static_cast<CPetitionSocket*>(owner), error); }
    );
    if (!bound) {
        ClosePetitionNativeSocket(nativeSocket);
    }
    m_pPetitionSocket->Initialize();
    sendable = true;
    return true;
}

void CPetitionCli::Free() {
    // Current L2Server 0x00964930.
    L2_GUARD_FUNCTION();
    if (m_pPetitionSocket == nullptr) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Petition server is not connected.")
        // Noise: Closing an absent petition connection requires no action. Repeated cleanup is not an operator event.
        // L2_LOG_INFO(Log,
        //     L"Petition server is not connected.")
        return;
    }
    m_pPetitionSocket->Close();
    sendable = false;
    m_pPetitionSocket = nullptr;
}

bool CPetitionCli::ReplyCharList(const unsigned char* packet) {
    // Current L2Server 0x00964a00.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        return false;
    }

    std::int32_t requestValue = 0;
    std::uint8_t characterCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dc", &requestValue, &characterCount);

    std::array<char, 1024> output{};
    int outputLength = ::Assemble(output.data(), static_cast<int>(output.size()), "cdc", 0x85, requestValue, characterCount);

    constexpr std::array<std::int32_t, 10> categoryRemap{0, 3, 3, 2, 2, 1, 1, 0, 0, 0};
    for (int index = 0; index < static_cast<int>(characterCount); ++index) {
        std::int32_t extraValue = 0;
        char16_t characterName[1024];
        for (char16_t& value : characterName) {
            value = u'\0';
        }
        std::uint16_t category = 0;
        cursor = ::Disassemble(cursor, "Shd", 0x800, characterName, &category, &extraValue);
        if (category < categoryRemap.size()) {
            category = static_cast<std::uint16_t>(categoryRemap[category]);
        } else {
            category = 0;
        }

        const int nameByteLength = TargetUtf16Length(characterName) * 2;
        const int nameLength = TargetUtf16Length(characterName);
        outputLength += ::Assemble(
            output.data() + outputLength, static_cast<int>(output.size()) - outputLength, "hbdc", nameLength, nameByteLength,
            characterName, extraValue, static_cast<std::int16_t>(category)
        );
    }

    m_pPetitionSocket->Send("b", outputLength, output.data());
    return true;
}

bool CPetitionCli::ReqCancelPetition(User* const user, User* const target) {
    // Current L2Server 0x00964c5c.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::PETITION_SYSTEM_UNAVAILABLE);
        return false;
    }

    User* const selected = target != nullptr ? target : user;
    const std::int32_t selectedDatabaseId = runtime::Int32FromBits(selected->GetDBID());
    const std::u16string selectedName = std::u16string(selected->GetName());
    const int selectedNameByteLength = static_cast<int>(std::u16string(selected->GetName()).size()) * 2;
    const int selectedNameLength = static_cast<int>(std::u16string(selected->GetName()).size());

    const std::int32_t userDatabaseId = runtime::Int32FromBits(user->GetDBID());
    const std::u16string userName = std::u16string(user->GetName());
    const int userNameByteLength = static_cast<int>(std::u16string(user->GetName()).size()) * 2;
    const int userNameLength = static_cast<int>(std::u16string(user->GetName()).size());

    m_pPetitionSocket->Send(
        "chbdhbd", 0x9a, userNameLength, userNameByteLength, userName.data(), userDatabaseId, selectedNameLength,
        selectedNameByteLength, selectedName.data(), selectedDatabaseId
    );
    return true;
}

bool CPetitionCli::ReqShowPetition(User* const user) {
    // Current L2Server 0x00966ff4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CPacketHelperEx<20480> packet;
    const int count = GetDepthOne(packet);
    user->GetSocket()->Send("chdb", 0xfe, 0xaf, count, packet.GetLength(), packet.GetPacket());
    return true;
}

bool CPetitionCli::ReqShowStepTwo(User* const user, const std::uint8_t categoryId) {
    // Current L2Server 0x009677e8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CPacketHelperEx<20480> packet;
    const int count = GetDepthTwo(packet, categoryId);
    user->GetSocket()->Send("chdb", 0xfe, 0xb0, count, packet.GetLength(), packet.GetPacket());
    if (count == 0) {
        ReqShowStepThree(user, categoryId);
    }
    return true;
}

bool CPetitionCli::ReqShowStepThree(User* const user, const std::uint8_t categoryId) {
    // Current L2Server 0x0096712c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CPacketHelperEx<20480> packet;
    const int count = GetDepthThree(packet, categoryId);
    user->GetSocket()->Send("chdb", 0xfe, 0xb2, count, packet.GetLength(), packet.GetPacket());
    return true;
}

int CPetitionCli::GetDepthOne(IPacketHelper& packet) {
    // Current L2Server 0x0096581c. The current body addresses
    // the process-global category map instead of the receiver-relative map.
    L2_GUARD_FUNCTION();
    int count = 0;
    FOR_ALL_OBJECT_IN_OBJECT_MAP(petitionCli.vCategoryList, category, {
        if (category->nParentID == 0) {
            packet.Assemble("cS", category->nID, category->wsName);
            count = runtime::AddInt32Wrapping(count, 1);
        }
    });
    return count;
}

int CPetitionCli::GetDepthTwo(IPacketHelper& packet, const std::uint8_t categoryId) {
    // Current L2Server 0x00967278. Both lookups use the
    // process-global category map.
    L2_GUARD_FUNCTION();
    int count = 0;
    CSPointer<PetitionCategory> parent = petitionCli.vCategoryList.FindObject(categoryId);
    if (parent) {
        packet.Assemble("S", parent->wsDescription);
    }

    FOR_ALL_OBJECT_IN_OBJECT_MAP(petitionCli.vCategoryList, category, {
        if (category->nParentID == categoryId) {
            packet.Assemble("cS", category->nID, category->wsName);
            count = runtime::AddInt32Wrapping(count, 1);
        }
    });
    return count;
}

int CPetitionCli::GetDepthThree(IPacketHelper& packet, const std::uint8_t categoryId) {
    // Current L2Server 0x00965a6c. The current body addresses
    // the process-global FAQ map instead of the receiver-relative map.
    L2_GUARD_FUNCTION();
    int count = 0;
    FOR_ALL_OBJECT_IN_OBJECT_MAP(petitionCli.vFAQList, faq, {
        if (faq->nCategoryID == categoryId) {
            packet.Assemble("dcS", faq->nMessageID, faq->nCategoryID, faq->wsTitle);
            count = runtime::AddInt32Wrapping(count, 1);
        }
    });
    return count;
}

void CPetitionCli::ShowConTents(User* const user, const int messageId) {
    // Current L2Server 0x009675a4. The current body addresses
    // the process-global FAQ map instead of the receiver-relative member.
    L2_GUARD_FUNCTION();
    CSPointer<PetitionFAQ> faq = petitionCli.vFAQList.FindObject(messageId);
    if (faq) {
        user->GetSocket()->Send("chS", 0xfe, 0xb1, faq->wsContents);
    }
}

void CPetitionCli::ShowMessage(User* const user, wchar_t* const message) {
    // Current L2Server 0x00964e80.
    L2_GUARD_FUNCTION();
    if (std::wcslen(message) > 0) {
        user->PetitionSayBySystem(PacketWideLanesToTargetUtf16(message).c_str());
        user->GetSocket()->SendSystemMessage(SystemMessageId::PETITION_CONSULTATION_COMPLETED_FEEDBACK_REQUESTED);
    }
}

void CPetitionCli::SetDisableTime(const int first, const int second) {
    // Current L2Server 0x00964f4c.
    L2_GUARD_FUNCTION();
    disabletime1 = first % 24;
    disabletime2 = second % 24;
}

bool CPetitionCli::IsDisableTime() {
    // Current L2Server 0x00965024.
    L2_GUARD_FUNCTION();
    if (disabletime1 == disabletime2) {
        return false;
    }

    const int hour = static_cast<int>(runtime::GetLocalTimeNow().wHour);
    if (disabletime2 <= disabletime1) {
        return hour >= disabletime1 || hour < disabletime2;
    }
    return disabletime1 <= hour && hour < disabletime2;
}

bool CPetitionCli::RequestFeedback(const int length, char* const buffer) {
    // Current L2Server 0x009650e4.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        return false;
    }
    m_pPetitionSocket->Send("cb", 0xaa, length, buffer);
    return true;
}

bool CPetitionCli::ReqSubmitPetition(User* const user, char16_t* text, const int categoryId, User* const target) {
    // Current L2Server 0x00965dac.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    if (IsDisableTime() && target == nullptr) {
        user->GetSocket()->Send(
            "chccccc", 0x62, SystemMessageId::PETITION_SERVICE_UNAVAILABLE_DURING_TIME_RANGE, 2, 20,
            static_cast<int>(static_cast<std::int8_t>(disabletime1)), 20,
            static_cast<int>(static_cast<std::int8_t>(disabletime2))
        );
        return false;
    }
    if (!sendable) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::PETITION_SYSTEM_UNAVAILABLE);
        return false;
    }

    CSPointer<PetitionCategory> category = petitionCli.vCategoryList.FindObject(categoryId);
    if (!category) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Category Number is not Exist!! (CategoryID:%d)",
        //     categoryId)
        // L2_LOG_ERROR(Log,
        //     L"Category Number is not Exist!! (CategoryID:%d)",
        //     categoryId)
        L2_LOG_WARN(
            Log, "Cannot submit petition: category %d is not present in the current category list; no petition is sent.",
            categoryId
        );
        return false;
    }

    while (*text == u' ') {
        ++text;
    }
    std::int64_t last = static_cast<std::int64_t>(std::char_traits<char16_t>::length(text)) - 1;
    if (last > 0) {
        do {
            if (last < 0 || text[last] != u' ') {
                break;
            }
            text[last] = u'\0';
            --last;
        } while (last > 0);
    }

    const std::u16string petitionText = std::u16string(text);
    if (TargetUtf16Length(petitionText.data()) < 6) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::PETITION_TEXT_TOO_SHORT);
        return false;
    }

    std::array<char16_t, 0x401> userCharacterName{};
    std::array<char16_t, 0x401> targetCharacterName{};
    std::array<char16_t, 0x0e> targetAccountName{};
    std::int32_t targetDatabaseId = 0;
    std::int32_t targetAccountId = 0;
    if (target != nullptr) {
        CopyTargetUtf16Bounded(targetCharacterName, target->GetName(), targetCharacterName.size() - 1);
        targetDatabaseId = runtime::Int32FromBits(target->GetDBID());
    }

    CopyTargetUtf16Bounded(userCharacterName, user->GetName(), userCharacterName.size() - 1);
    const std::int32_t userObjectId = runtime::Int32FromBits(user->Id());
    const std::int32_t userDatabaseId = runtime::Int32FromBits(user->GetDBID());

    std::array<char, 0x400> infoBuffer;
    int infoLength = 0;
    {
        GuardHelper makeInfoBufferGuard(L"make_info_buffer", CThreadManager::GetThreadID());
        infoLength =
            ::Assemble(infoBuffer.data(), static_cast<int>(infoBuffer.size()), "d", static_cast<int>(user->m_data->m_Race));
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "d",
            static_cast<int>(user->m_data->m_nClass)
        );
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "d", user->m_data->m_nLevel
        );
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "d", user->m_data->m_nKarma
        );

        int ssqPart = 0;
        SharedSSQInfo* const ssqInfo = SharedSSQInfo::GetInfo();
        if (user->m_data->m_nSSQRoundNumber == ssqInfo->mRoundNumber) {
            ssqPart = user->m_data->m_nSSQPart;
        }
        infoLength +=
            ::Assemble(infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "d", ssqPart);
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "d",
            static_cast<int>(User::IsNewChar(user->GetDBID()))
        );

        std::array<char16_t, 0x100> location{};
        const FVector position = user->m_data->GetPos();
        const std::int32_t x = runtime::Win64CvttDoubleToInt32(position.x);
        const std::int32_t y = runtime::Win64CvttDoubleToInt32(position.y);
        const std::int32_t z = runtime::Win64CvttDoubleToInt32(position.z);
        CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
            (runtime::AddInt32Wrapping(x, 0x50000) >> 15) + 10, (runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10
        );
        int zoneX = 0;
        int zoneY = 0;
        if (zone != nullptr && zone->m_data != nullptr) {
            zoneX = static_cast<int>(zone->m_data->zone_x);
            zoneY = static_cast<int>(zone->m_data->zone_y);
        }
        FormatPetitionLocation(location, x, y, z, zoneX, zoneY);
        const int locationLength = TargetUtf16Length(location.data());
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "h", locationLength
        );
        const int locationByteLength = TargetUtf16Length(location.data()) * 2;
        infoLength += ::Assemble(
            infoBuffer.data() + infoLength, static_cast<int>(infoBuffer.size()) - infoLength, "b", locationByteLength,
            location.data()
        );
    }

    if (target != nullptr) {
        CopyTargetUtf16Bounded(targetAccountName, target->m_sAccountName.data(), 0x0d);
        targetAccountId = runtime::Int32FromBits(target->GetAccountID());
    }
    const std::int32_t userAccountId = runtime::Int32FromBits(user->GetAccountID());

    std::array<char, 0x5000> output{};
    int outputLength = 0;
    const int opcodeLength = ::Assemble(output.data(), static_cast<int>(output.size()), "c", 0xa9);
    if (opcodeLength > 0) {
        outputLength = opcodeLength;
    }

    const auto appendPositive = [&output, &outputLength](const char* const format, auto... arguments) {
        const int appended =
            ::Assemble(output.data() + outputLength, static_cast<int>(output.size()) - outputLength, format, arguments...);
        if (appended > 0) {
            outputLength += appended;
        }
    };

    appendPositive("d", userObjectId);
    appendPositive("d", categoryId);

    const int userAccountNameByteLength = TargetUtf16Length(user->m_sAccountName.data()) * 2;
    const int userAccountNameLength = TargetUtf16Length(user->m_sAccountName.data());
    appendPositive("hb", userAccountNameLength, userAccountNameByteLength, user->m_sAccountName.data());
    appendPositive("d", userAccountId);

    const int userCharacterNameByteLength = TargetUtf16Length(userCharacterName.data()) * 2;
    const int userCharacterNameLength = TargetUtf16Length(userCharacterName.data());
    appendPositive("hb", userCharacterNameLength, userCharacterNameByteLength, userCharacterName.data());
    appendPositive("d", userDatabaseId);

    const int petitionTextByteLength = TargetUtf16Length(petitionText.data()) * 2;
    const int petitionTextLength = TargetUtf16Length(petitionText.data());
    appendPositive("hb", petitionTextLength, petitionTextByteLength, petitionText.data());

    const int targetAccountNameByteLength = TargetUtf16Length(targetAccountName.data()) * 2;
    const int targetAccountNameLength = TargetUtf16Length(targetAccountName.data());
    appendPositive("hb", targetAccountNameLength, targetAccountNameByteLength, targetAccountName.data());
    appendPositive("d", targetAccountId);

    const int targetCharacterNameByteLength = TargetUtf16Length(targetCharacterName.data()) * 2;
    const int targetCharacterNameLength = TargetUtf16Length(targetCharacterName.data());
    appendPositive("hb", targetCharacterNameLength, targetCharacterNameByteLength, targetCharacterName.data());
    appendPositive("d", targetDatabaseId);
    appendPositive("b", infoLength, infoBuffer.data());

    m_pPetitionSocket->Send("b", outputLength, output.data());
    return true;
}

bool CPetitionCli::Init() {
    // Current L2Server 0x009651c4.
    L2_GUARD_FUNCTION();
    const bool result = [&] {
        L2_GUARD(mLock);
        bool result = false;
        if (!petitionCli.sendable) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"Connecting petition server...")
            // Noise: Connect already reports failure or TCP establishment. Repeating an attempt-start message on every
            // heartbeat adds no outcome. L2_LOG_INFO(Log,
            //     L"Connecting petition server...")
            result = Connect();
            petitionCli.sendable = result;
        }
        return result;
    }();
    return result;
}

bool CPetitionCli::CreateThread() {
    // Current L2Server 0x0096540c.
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(PetitionWorker, nullptr, true);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::PETITION, m_hConnectThread);
    return true;
}

int CPetitionCli::GetMyPetition(const int databaseId) {
    // Current L2Server 0x00965700.
    L2_GUARD_FUNCTION();
    int result = 0;
    {
        L2_READ_GUARD(lock);
        for (const auto& entry : mapPetition) {
            PetitionCheckedOut* const checkedOut = entry.second;
            if (checkedOut->nClient_dbid == databaseId || checkedOut->nGM_dbid == databaseId) {
                result = checkedOut->petitionID;
                break;
            }
        }
    }
    return result;
}

void CPetitionCli::ResetCheckedOutList() {
    // Current L2Server 0x009676fc.
    {
        L2_WRITE_GUARD(lock);
        for (const auto& entry : mapPetition) {
            delete entry.second;
        }
        mapPetition.clear();
    }
}

bool CPetitionCli::NotifyUserChat2(User* const user, const char16_t* const message) {
    // Current L2Server 0x009666c4. The complete current PetitionSocket table
    // has no checked-out producer: mapPetition is constructor-empty and only
    // ResetCheckedOutList mutates it. The current hit path below is kept
    // literally from its direct field/call evidence even though the pinned
    // process lifecycle cannot produce it.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        return false;
    }

    const int petitionId = user->m_nPetitionId;
    {
        L2_READ_GUARD(lock);
        const auto found = mapPetition.find(petitionId);
        if (found != mapPetition.end()) {
            PetitionCheckedOut* const checkedOut = found->second;
            const int gmDatabaseId = checkedOut->nGM_dbid;
            const int checkedOutPetitionId = checkedOut->petitionID;
            if (!checkedOut->bIsChat) {
                if (!checkedOut->bIsPetitionChat) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::NO_PETITION_CONSULTATION_IN_PROGRESS);
                } else {
                    const std::size_t messageByteLengthSource = target_utf16::Length(message);
                    const std::size_t messageLengthSource = target_utf16::Length(message);
                    const char16_t* const userNamePointer = user->GetName();
                    const char16_t* const userNameByteLengthSource = user->GetName();
                    const std::size_t userNameByteLength = TargetUtf16Length(userNameByteLengthSource) * 2U;
                    const char16_t* const userNameLengthSource = user->GetName();
                    const std::size_t userNameLength = TargetUtf16Length(userNameLengthSource);
                    const std::u16string targetUserName = std::u16string(userNamePointer);
                    const std::u16string targetMessage = std::u16string(message);
                    m_pPetitionSocket->Send(
                        "cdhbhb", 0xa5, checkedOutPetitionId, static_cast<int>(userNameLength),
                        static_cast<int>(userNameByteLength), targetUserName.c_str(), static_cast<int>(messageLengthSource),
                        static_cast<int>(messageByteLengthSource * 2U), targetMessage.c_str()
                    );

                    for (const int serverId : checkedOut->vPetiChatServerIdList) {
                        CSPointer<User> member = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, serverId));
                        if (member) {
                            if (member->m_nPetitionId == 0) {
                                member->m_nPetitionId = checkedOutPetitionId;
                            }
                            member->Hear(user, user->GetName(), message, 6);
                        }
                    }
                }
            } else {
                bool gmReceivedChat = false;
                for (const int serverId : checkedOut->vPetiChatServerIdList) {
                    CSPointer<User> member = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, serverId));
                    if (member && member->Id() != user->Id()) {
                        if (member->m_nPetitionId == 0) {
                            member->m_nPetitionId = checkedOutPetitionId;
                        }
                        member->Hear(user, user->GetName(), message, 6);
                        if (gmDatabaseId == static_cast<int>(member->GetDBID())) {
                            gmReceivedChat = true;
                        }
                    }
                }

                user->Hear(user, user->GetName(), message, 6);
                const char16_t* const sourceName = user->GetName();
                std::array<char16_t, 1024> formattedName{};
                FormatUtf16To(formattedName.data(), formattedName.size(), u"%ls", sourceName);
                const std::size_t messageByteLengthSource = target_utf16::Length(message);
                const std::size_t messageLengthSource = target_utf16::Length(message);
                const std::size_t nameByteLengthSource = std::char_traits<char16_t>::length(formattedName.data());
                const std::size_t nameLengthSource = nameByteLengthSource;
                const std::u16string targetName(formattedName.data());
                const std::u16string targetMessage = std::u16string(message);
                m_pPetitionSocket->Send(
                    "cdhbhb", 0x83, checkedOutPetitionId, static_cast<int>(nameLengthSource),
                    static_cast<int>(nameByteLengthSource * 2U), targetName.c_str(), static_cast<int>(messageLengthSource),
                    static_cast<int>(messageByteLengthSource * 2U), targetMessage.c_str()
                );
                if (!gmReceivedChat) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::NO_CHAT_RECIPIENT_IN_GAME);
                }
            }
        }
    }
    return true;
}

bool CPetitionCli::NotifyGmChat(User* const user, const char16_t* const message) {
    // Current L2Server 0x00966cd8. See NotifyUserChat2 for the exact
    // constructor-empty/reset-only checked-out lifecycle.
    L2_GUARD_FUNCTION();
    if (!sendable) {
        return false;
    }

    const int petitionId = user->m_nPetitionId;
    {
        L2_READ_GUARD(lock);
        const auto found = mapPetition.find(petitionId);
        if (found != mapPetition.end()) {
            PetitionCheckedOut* const checkedOut = found->second;
            const int checkedOutPetitionId = checkedOut->petitionID;
            if (!checkedOut->bIsChat) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::NO_PETITION_CONSULTATION_IN_PROGRESS);
            } else {
                int recipientCount = 0;
                const int recipientLimit =
                    runtime::Int32FromBits(static_cast<std::uint32_t>(checkedOut->vPetiChatServerIdList.size()));
                for (int index = 0; index < recipientLimit; ++index) {
                    const int serverId = checkedOut->vPetiChatServerIdList[static_cast<std::size_t>(index)];
                    CSPointer<User> member = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, serverId));
                    if (member && member->Id() != user->Id()) {
                        if (member->m_nPetitionId == 0) {
                            member->m_nPetitionId = checkedOutPetitionId;
                        }
                        member->Hear(user, user->GetName(), message, 7);
                        ++recipientCount;
                    }
                }

                user->Hear(user, user->GetName(), message, 7);
                const std::size_t messageLength = std::char_traits<char16_t>::length(message);
                m_pPetitionSocket->Send("cdcS", 0x76, checkedOutPetitionId, static_cast<int>(messageLength), message);
                if (recipientCount == 0) {
                    user->GetSocket()->SendSystemMessage(SystemMessageId::NO_CHAT_RECIPIENT_IN_GAME);
                }
            }
        }
    }
    return true;
}
