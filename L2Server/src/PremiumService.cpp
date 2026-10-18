#include "PremiumService.h"
#include "Shared/src/MemorySub.h"

#include "BRShopServer.h"
#include "CConfig.h"
#include "DB.h"
#include "Item.h"
#include "ObjectDB.h"
#include "ParserPremiumService.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {


int CurrentPremiumLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

// int LegacyPremiumSocketSymbolForLog() noexcept {
[[maybe_unused]] int LegacyPremiumSocketSymbolForLog() noexcept {
    // Current 0x009ecb6b loads the imported socket symbol for this
    // diagnostic instead of the socket member.
    return static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket));
}

void StoreLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

void ClosePremiumNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

std::uint32_t PremiumConnectThread(void*) {
    CThreadManager::RegisterThisThread(ThreadID::PREMIUM);
    const int threadId = CThreadManager::GetThreadID();
    g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(0x80);
    g_IOBuffer8kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer8k>(0x80);

    const auto run = [] {
        for (;;) {
            g_PremiumSvc.Init();
            std::this_thread::sleep_for(std::chrono::milliseconds(g_PremiumSetting.heartBeat));
        }
    };
    if (ExceptionMailing()) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"premium service in mailing mode")
        // L2_LOG_INFO(Log,
        //     L"premium service in mailing mode")
        L2_LOG_INFO(Log, "Premium-service connection worker uses exception-mail reporting.");
        try {
            run();
        } catch (...) {
            runtime::process::HandleUnhandledSharedThreadException(
                "PremiumService.cpp", 0x732, "Thu May 10 05:00:26 2018"
            );
        }
    } else {
        run();
    }
    return 0;
}

void CopyBuyerCharName(std::array<char16_t, 24>& destination, const wchar_t* source) {
    if (!source) {
        destination[0] = u'\0';
        errno = EINVAL;
        runtime::process::HandleInvalidParameter();
        return;
    }

    std::size_t copied = 0;
    const auto append = [&](char16_t unit) {
        if (copied == destination.size()) {
            return false;
        }
        destination[copied++] = unit;
        return true;
    };
    for (std::size_t input = 0; source[input] != L'\0'; ++input) {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(source[input]);
#if WCHAR_MAX <= 0xffff
        if (!append(static_cast<char16_t>(codePoint))) {
            break;
        }
#else
        if (codePoint <= 0xffffU) {
            if (!append(static_cast<char16_t>(codePoint))) {
                break;
            }
        } else if (codePoint <= 0x10ffffU) {
            const std::uint32_t surrogate = codePoint - 0x10000U;
            if (!append(static_cast<char16_t>(0xd800U + (surrogate >> 10U))) ||
                !append(static_cast<char16_t>(0xdc00U + (surrogate & 0x3ffU)))) {
                break;
            }
        } else {
            if (!append(u'\ufffd')) {
                break;
            }
        }
#endif
    }

    if (copied == destination.size()) {
        destination[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
        return;
    }
    destination[copied] = u'\0';
}

template <std::size_t Capacity>
void CopyBillingText(std::array<char16_t, Capacity>& destination, const wchar_t* source) {
    destination.fill(u'\0');
    if (source == nullptr) {
        errno = EINVAL;
        runtime::process::HandleInvalidParameter();
        return;
    }

    const std::u16string target = ::WideToUtf16(source);
    if (target.size() >= destination.size()) {
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::copy(target.begin(), target.end(), destination.begin());
}

template <std::size_t Capacity>
std::wstring BillingTextToHost(const std::array<char16_t, Capacity>& source) {
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    return ::Utf16ToWide(std::u16string_view(source.data(), length));
}

CItem* FindPremiumItemPrototype(int itemClassId) {
    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr || !object->IsItem()) {
        return nullptr;
    }
    return object->CastItem();
}

bool IsValidPremiumItem(
    int itemClassId,
    int amount,
    // int invalidLine,
    [[maybe_unused]] int invalidLine,
    // int nonStackableLine,
    [[maybe_unused]] int nonStackableLine,
    // int periodicLine,
    [[maybe_unused]] int periodicLine,
    // int amountLine) {
    [[maybe_unused]] int amountLine
) {
    CItem* const item = FindPremiumItemPrototype(itemClassId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item[%d] is invalid",
        //     L"PremiumService.cpp",
        //     invalidLine,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item[%d] is invalid",
        //     L"PremiumService.cpp",
        //     invalidLine,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Premium item validation failed: class %d has no item prototype.", itemClassId);
        return false;
    }
    if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) && amount > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item[%d] is not stackable and amount is not 1pcs",
        //     L"PremiumService.cpp",
        //     nonStackableLine,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item[%d] is not stackable and amount is not 1pcs",
        //     L"PremiumService.cpp",
        //     nonStackableLine,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Premium item validation failed: non-stackable class %d has quantity %d greater than one.", itemClassId,
            amount
        );
        return false;
    }
    if (item->m_const->m_period > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item[%d] is periodic",
        //     L"PremiumService.cpp",
        //     periodicLine,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item[%d] is periodic",
        //     L"PremiumService.cpp",
        //     periodicLine,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Premium item validation failed: class %d has a positive duration and is not accepted here.", itemClassId
        );
        return false;
    }
    if (amount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item[%d] amount is below zero",
        //     L"PremiumService.cpp",
        //     amountLine,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item[%d] amount is below zero",
        //     L"PremiumService.cpp",
        //     amountLine,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Premium item validation failed: class %d quantity %d must be positive.", itemClassId, amount);
        return false;
    }
    return true;
}

void SendBuyResult(User& user, int clientOpcode, BrBuyProductResultType result) {
    user.GetSocket()->Send("chd", 0xfe, clientOpcode, static_cast<int>(result));
}

void LogPremiumPurchase(
    User& user,
    std::int64_t eventId,
    int productId,
    std::int64_t warehouseNo,
    int amount,
    std::int64_t balanceOrCost,
    int paymentType,
    int price,
    const wchar_t* receiverName = nullptr
) {
    const FVector position = user.GetPos();
    const std::wstring accountName = BillingTextToHost(user.m_sAccountName);
    if (receiverName == nullptr) {
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        //     L"%I64d,%I64d,%s,%s,,,,",
        //     eventId,
        //     static_cast<std::int64_t>(user.GetAccountID()),
        //     static_cast<std::int64_t>(user.GetDBID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(user.m_data->m_Race),
        //     static_cast<std::int64_t>(user.m_data->m_Sex),
        //     static_cast<std::int64_t>(user.m_data->m_nClass),
        //     static_cast<std::int64_t>(user.m_data->m_nLevel),
        //     static_cast<std::int64_t>(productId),
        //     warehouseNo,
        //     static_cast<std::int64_t>(amount),
        //     balanceOrCost,
        //     static_cast<std::int64_t>(paymentType),
        //     static_cast<std::int64_t>(price),
        //     user.GetName(),
        //     accountName.c_str())
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
            u"%I64d,%I64d,%s,%s,,,,",
            eventId, static_cast<std::int64_t>(user.GetAccountID()), static_cast<std::int64_t>(user.GetDBID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(user.m_data->m_Race), static_cast<std::int64_t>(user.m_data->m_Sex),
            static_cast<std::int64_t>(user.m_data->m_nClass), static_cast<std::int64_t>(user.m_data->m_nLevel),
            static_cast<std::int64_t>(productId), warehouseNo, static_cast<std::int64_t>(amount), balanceOrCost,
            static_cast<std::int64_t>(paymentType), static_cast<std::int64_t>(price), user.GetName(), accountName.c_str()
        );
        return;
    }
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,%s,%s,%s,,,,",
    //     eventId,
    //     static_cast<std::int64_t>(user.GetAccountID()),
    //     static_cast<std::int64_t>(user.GetDBID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(user.m_data->m_Race),
    //     static_cast<std::int64_t>(user.m_data->m_Sex),
    //     static_cast<std::int64_t>(user.m_data->m_nClass),
    //     static_cast<std::int64_t>(user.m_data->m_nLevel),
    //     static_cast<std::int64_t>(productId),
    //     warehouseNo,
    //     static_cast<std::int64_t>(amount),
    //     balanceOrCost,
    //     static_cast<std::int64_t>(paymentType),
    //     static_cast<std::int64_t>(price),
    //     user.GetName(),
    //     accountName.c_str(),
    //     receiverName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,%s,%s,%s,,,,",
        eventId, static_cast<std::int64_t>(user.GetAccountID()), static_cast<std::int64_t>(user.GetDBID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(user.m_data->m_Race), static_cast<std::int64_t>(user.m_data->m_Sex),
        static_cast<std::int64_t>(user.m_data->m_nClass), static_cast<std::int64_t>(user.m_data->m_nLevel),
        static_cast<std::int64_t>(productId), warehouseNo, static_cast<std::int64_t>(amount), balanceOrCost,
        static_cast<std::int64_t>(paymentType), static_cast<std::int64_t>(price), user.GetName(), accountName.c_str(),
        receiverName
    );
}

} // namespace

CPremiumCallbackMap g_PremiumPacketTable(DummyPremiumPacket, u"");

namespace {

L2_BIND_PACKET(IsRegistered_IBO_PROTOCOL_VER, g_PremiumPacketTable, 0x00, IBVersionPacket);
L2_BIND_PACKET(IsRegistered_IBO_BUY_GOODS_FOR_ACCOUNT_ACK, g_PremiumPacketTable, 0x01, IBBuyGoodsForAccountAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_BUY_GOODS_FOR_CHARACTER_ACK, g_PremiumPacketTable, 0x02, IBBuyGoodsForCharacterAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GIFT_GOODS_TO_ACCOUNT_ACK, g_PremiumPacketTable, 0x03, IBGiftGoodsToAccountAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GIFT_GOODS_TO_CHARACTER_ACK, g_PremiumPacketTable, 0x04, IBGiftGoodsToCharacterAckPacket);
L2_BIND_PACKET(
    IsRegistered_IBO_NEW_ITEM_DELIVERED_TO_ACCOUNT_NOTI, g_PremiumPacketTable, 0x05, IBNewItemDeliveredToAccountNotiPacket
);
L2_BIND_PACKET(
    IsRegistered_IBO_NEW_ITEM_DELIVERED_TO_CHARACTER_NOTI,
    g_PremiumPacketTable,
    0x06,
    IBNewItemDeliveredToCharacterNotiPacket
);
L2_BIND_PACKET(IsRegistered_IBO_BUY_ITEM_ACK, g_PremiumPacketTable, 0x0c, IBBuyItemAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GET_ITEMS_FOR_SERVER_ACK, g_PremiumPacketTable, 0x14, IBGetItemsForServerAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GET_ITEMS_FOR_ACCOUNT_ACK, g_PremiumPacketTable, 0x15, IBGetItemsForAccountAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GET_ITEMS_ACK, g_PremiumPacketTable, 0x16, IBGetItemsForCharacterAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_DELETE_ITEMS_ACK, g_PremiumPacketTable, 0x17, IBDeleteItemsAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_SUB_POINT_ACK, g_PremiumPacketTable, 0x18, IBSubPointAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_ADD_POINT_ACK, g_PremiumPacketTable, 0x19, IBAddPointAckPacket);
L2_BIND_PACKET(IsRegistered_IBO_GET_POINT_ACK, g_PremiumPacketTable, 0x1a, IBGetPointAckPacket);

} // namespace

CPremiumService g_PremiumSvc;

// static_assert(L2ServerMemory::kCPremiumServiceSocketClassId == 241)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CPremiumServiceSocket) == 241);
// static_assert(L2ServerMemory::kCBillingJobClassId == 242)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CBillingJob) == 242);
// static_assert(L2ServerMemory::kCPremiumItemClassId == 243)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CPremiumItem) == 243);

L2_DEFINE_OBJECT_NEW(CPremiumServiceSocket)

L2_DEFINE_OBJECT_ALLOCATOR(CPremiumServiceSocket, m_pClassName_CPremiumServiceSocket)

CPremiumServiceSocket::CPremiumServiceSocket(std::uint64_t socket) :
    CIOSocket(socket, 0x1000),
    m_socket(socket),
    m_threadCount(0),
    m_alignmentBcToC0{},
    m_packetTable(g_PremiumPacketTable.GetData()),
    m_mode(SocketMode::SM_READ_LEN),
    m_packetLength(0) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"PremiumSocket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // L2_LOG_INFO(Log,
    //     L"PremiumSocket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    L2_LOG_INFO(
        Log, "Premium-service socket %llu created (object %p); protocol validation is pending.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this)
    );
}

CPremiumServiceSocket::~CPremiumServiceSocket() { m_packetTable = nullptr; }

L2_DEFINE_OBJECT_RELEASE(CPremiumServiceSocket)

bool CPremiumServiceSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid premium socket.",
        //     L"PremiumService.cpp",
        //     0x3ef)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Write to invalid premium socket.",
        //     L"PremiumService.cpp",
        //     0x3ef)
        L2_LOG_ERROR(Log, "Premium-service write rejected: socket object %p is closed.", static_cast<void*>(this));
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"premium socket pending write > %d KB, Close!",
    //     0x2000)
    // L2_LOG_ERROR(Log,
    //     L"premium socket pending write > %d KB, Close!",
    //     0x2000)
    L2_LOG_ERROR(
        Log, "Premium-service write rejected: pending data exceeds 8192 KiB (socket %llu).",
        static_cast<unsigned long long>(m_socket)
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

void CPremiumServiceSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CPremiumServiceSocket::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    if (m_mode == SocketMode::SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    const int bodySize = VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x1ffe, format, arguments);
    std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize);
    if (bodySize == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%d: assemble too large packet. format %s",
        //     LegacyPremiumSocketSymbolForLog(),
        //     format)
        // L2_LOG_ERROR(Log,
        //     L"%d: assemble too large packet. format %s",
        //     LegacyPremiumSocketSymbolForLog(),
        //     format)
        L2_LOG_ERROR(
            Log,
            "Premium-service packet assembly returned zero for socket %llu (capacity 8190 bytes, format %s); submitting an "
            "empty buffer.",
            static_cast<unsigned long long>(m_socket), format != nullptr ? format : "<null>"
        );
    } else {
        frameSize = static_cast<std::uint16_t>(bodySize + 2);
        StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
    }
    buffer->m_dwSize = frameSize;
    Write(buffer);
}

void CPremiumServiceSocket::OnClose() {
    L2_GUARD_FUNCTION();
    m_mode = SocketMode::SM_CLOSE;
    const int error = CurrentPremiumLastError();
    // Log.Add(
    //     LOG_ERROR,
    //     L"CPremiumServiceSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    // L2_LOG_ERROR(Log,
    //     L"CPremiumServiceSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    L2_LOG_WARN(
        Log, "Premium-service socket %llu closed (object %p); last-error snapshot %d.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this), error
    );
    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,,", 0x51b, 1, static_cast<unsigned int>(m_socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(this)), error
    );
    g_PremiumSvc.m_bConnected = false;
}

void CPremiumServiceSocket::OnRead() {
    L2_GUARD_FUNCTION();
    if (m_mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_mode == SocketMode::SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                             ) -
                2;
            if (m_packetLength < 1 || m_packetLength > 0x1ffe) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"premium socket %d: bad packet size %d",
                //     LegacyPremiumSocketSymbolForLog(),
                //     m_packetLength)
                // L2_LOG_ERROR(Log,
                //     L"premium socket %d: bad packet size %d",
                //     LegacyPremiumSocketSymbolForLog(),
                //     m_packetLength)
                L2_LOG_ERROR(
                    Log, "Closing premium-service socket %llu: packet body length %d is outside 1..8190 bytes.",
                    static_cast<unsigned long long>(m_socket), static_cast<int>(m_packetLength)
                );
                m_mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        } else if (m_mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + m_packetLength) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (opcode > 0x2d) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[premium]unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     L"[premium]unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "Closing premium-service socket %llu: opcode %u exceeds 45.", static_cast<unsigned long long>(m_socket),
                static_cast<unsigned>(opcode)
            );
            m_mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CPremiumPacket;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[opcode];
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_packetLength;
        m_mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CBillingJob, m_pClassName_CBillingJob)

L2_DEFINE_OBJECT_RELEASE(CBillingJob)

CBillingJob::CBillingJob(int requestedUserId, std::int16_t requestedCommand) :
    transactionId(0),
    userId(requestedUserId),
    command(requestedCommand),
    m_alignment22To28{},
    warehouseNo(0),
    productId(0),
    productAmount(0),
    extensionSize(0),
    extension{},
    m_alignment139To140{},
    pointDiff(0),
    option(0),
    bGift(false),
    m_alignment14dTo14e{},
    sReceiverName{},
    sTitle{},
    sContent{},
    m_tailPadding9ccTo9d0{} {
    transactionId = g_PremiumSvc.CreateTransactionId();
}

CBillingJob::~CBillingJob() = default;

CPremiumService::CPremiumService() :
    m_nConnectThread(0),
    m_alignment04To08{},
    m_hConnectThread(nullptr),
    m_bConnected(false),
    m_bSendable(false),
    m_alignment12To18{},
    m_pPremiumSvcSocket(nullptr),
    m_nLogTick(0),
    m_nReqDeleteSequence(0),
    m_nTransactionId(0),
    m_tailPaddingBcToC0{} {}

CPremiumService::~CPremiumService() {
    m_hConnectThread = nullptr;
    m_pPremiumSvcSocket = nullptr;
}

void CPremiumService::ConnectToPremiumAgent() {
    L2_GUARD_FUNCTION();
    if (!m_bConnected) {
        runtime::ResumeOwnedThread(m_hConnectThread);
    }
}

void CPremiumService::RequestCheckVersion() {
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        m_pPremiumSvcSocket->Send("Cddd", 0, 0, 0x20, g_nWorldId);
    }
}

void CPremiumService::RequestGetPremiumItems() {
    L2_GUARD_FUNCTION();
    if (m_bSendable && m_bConnected && (!g_PremiumSetting.cashshopPause || !g_PremiumSetting.pause)) {
        m_pPremiumSvcSocket->Send("Cd", 0x14, g_nWorldId);
    }
}

bool CPremiumService::Connect() {
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Already connected to IBServer")
        // L2_LOG_ERROR(Log,
        //     L"Already connected to IBServer")
        L2_LOG_WARN(Log, "Premium-service connection attempt skipped: already connected to IBServer.");
        return false;
    }

    const std::uint64_t socket = static_cast<std::uint64_t>(::socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr = g_PremiumSetting.serverAddr;
    address.sin_port = htons(static_cast<std::uint16_t>(g_PremiumSetting.serverPort));
    if (::connect(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
#if defined(_WIN32)
        const int connectError = ::WSAGetLastError();
#else
        const int connectError = errno;
#endif
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Can't connect to IBServer")
            // L2_LOG_ERROR(Log,
            //     L"Can't connect to IBServer")
            // L2_LOG_ERROR(Log,
            //     "Premium-service TCP connection to IBServer failed.")
            L2_LOG_ERROR(
                Log, "Premium-service TCP connection to %u.%u.%u.%u:%u failed: socket error=%d.",
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 24U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 16U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 8U) & 255U,
                static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) & 255U, static_cast<unsigned>(ntohs(address.sin_port)),
                connectError
            );
            m_nLogTick = runtime::GetTickCount();
        }
        ClosePremiumNativeSocket(socket);
        return false;
    }

    // Log.Add(LOG_NORMAL, L"Connected to IBServer")
    // L2_LOG_INFO(Log, L"Connected to IBServer")
    L2_LOG_INFO(Log, "Premium-service TCP connection to IBServer established; protocol validation is pending.");
    m_pPremiumSvcSocket = new CPremiumServiceSocket(socket);
    m_pPremiumSvcSocket->Initialize();
    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,,", 0x51b, 0, static_cast<unsigned int>(socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(m_pPremiumSvcSocket)), 0
    );
    return true;
}

bool CPremiumService::Init() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_PremiumSetting.g_bStandAloneShopTest) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Not Connect to IBServer. Use StandAlone Mode")
        // L2_LOG_INFO(Log,
        //     L"Not Connect to IBServer. Use StandAlone Mode")
        L2_LOG_INFO(Log, "Premium service uses standalone shop test mode; IBServer connection is skipped.");
        return true;
    }
    if (g_PremiumSetting.enable && !m_bConnected && (!g_PremiumSetting.pause || !g_PremiumSetting.cashshopPause)) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            // Log.Add(LOG_NORMAL, L"Connecting IBServer...")
            // L2_LOG_INFO(Log, L"Connecting IBServer...")
            L2_LOG_INFO(Log, "Attempting premium-service TCP connection to IBServer.");
        }
        if (!Connect()) {
            m_bConnected = false;
            return false;
        }
        m_bConnected = true;
        RequestCheckVersion();
    }
    return true;
}

void CPremiumService::CreateThread() {
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(PremiumConnectThread, nullptr, true);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::PREMIUM, m_hConnectThread);
}

void CPremiumService::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\PremiumService.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/PremiumService.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from PremiumService file failed",
        //     L"PremiumService.cpp",
        //     0x4f1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from PremiumService file failed",
        //     L"PremiumService.cpp",
        //     0x4f1)
        L2_LOG_ERROR(Log, "Cannot read PremiumService.txt; premium-service data loading stopped.");
        return;
    }

    CParserForPremiumService parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] PremiumServiceParse.create failed",
        //     L"PremiumService.cpp",
        //     0x4fa)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] PremiumServiceParse.create failed",
        //     L"PremiumService.cpp",
        //     0x4fa)
        L2_LOG_ERROR(Log, "Cannot initialize the PremiumService.txt parser; data loading stopped.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing PremiumService file failed after line[%d]",
        //     L"PremiumService.cpp",
        //     0x507,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing PremiumService file failed after line[%d]",
        //     L"PremiumService.cpp",
        //     0x507,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "PremiumService.txt parsing failed near line %lld; loading did not complete.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

std::int32_t CPremiumService::CreateTransactionId() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::AddI32(&m_nTransactionId, 1);
}

CBillingJobSP CPremiumService::PopJob(int transactionId) {
    L2_GUARD_FUNCTION();

    CBillingJobSP billingJob = m_Jobs.FindObject(transactionId);
    if (!billingJob) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot find Billingjob->[%d]",
        //     L"PremiumService.cpp",
        //     0x701,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot find Billingjob->[%d]",
        //     L"PremiumService.cpp",
        //     0x701,
        //     transactionId)
        // Noise: all four callers report the missing transaction with operation context.
        // L2_LOG_WARN(Log,
        //     "Cannot consume billing job: transaction %d was not found.",
        //     transactionId)
    } else {
        m_Jobs.DeleteObject(transactionId);
    }
    return billingJob;
}

bool CPremiumService::PushJob(CBillingJob* billingJob) {
    L2_GUARD_FUNCTION();

    if (billingJob != nullptr) {
        m_Jobs.Insert(billingJob->transactionId, billingJob);
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Push null Billingjob->",
    //     L"PremiumService.cpp",
    //     0x70f)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Push null Billingjob->",
    //     L"PremiumService.cpp",
    //     0x70f)
    L2_LOG_ERROR(Log, "Cannot register billing job: job pointer is null.");
    return false;
}

BrBuyProductResultType CPremiumService::SendBuyItemReq(
    int userId, int productId, int productAmount, bool gift, wchar_t* receiverName, wchar_t* title, wchar_t* content
) {
    L2_GUARD_FUNCTION();
    if (!g_PremiumSetting.g_bStandAloneShopTest && (!m_bSendable || !m_bConnected || g_PremiumSetting.cashshopPause)) {
        return BR_BUY_SERVER_ERROR;
    }

    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestBuyItem : GetUser Fail, userId(%d)",
        //     userId)
        // L2_LOG_ERROR(Log,
        //     L"RequestBuyItem : GetUser Fail, userId(%d)",
        //     userId)
        L2_LOG_WARN(Log, "Premium purchase rejected: player object ID %d is unavailable.", userId);
        return BR_BUY_INVALID_USER;
    }

    CProductSP product = g_ShopServer.GetProduct(productId);
    if (!product) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestBuyItem : GetProduct Fail, productId(%d)",
        //     productId)
        // L2_LOG_ERROR(Log,
        //     L"RequestBuyItem : GetProduct Fail, productId(%d)",
        //     productId)
        L2_LOG_WARN(Log, "Premium purchase rejected: product %d was not found.", productId);
        return BR_BUY_INVALID_PRODUCT;
    }
    if (productAmount < 1 || productAmount > 100) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestBuyItem : invalid productAmount(%d)",
        //     productAmount)
        // L2_LOG_ERROR(Log,
        //     L"RequestBuyItem : invalid productAmount(%d)",
        //     productAmount)
        L2_LOG_WARN(Log, "Premium purchase rejected: product quantity %d is outside 1..100.", productAmount);
        return BR_BUY_INVALID_PRODUCT;
    }

    const std::int64_t unitPrice = product->m_price;
    const std::int64_t totalPrice = static_cast<std::int64_t>(productAmount) * unitPrice;
    const int itemCount = product->GetItemCount();
    const int paymentType = product->m_paymentType;

    if (paymentType == 0 && !g_PremiumSetting.g_bStandAloneShopTest) {
        auto* const job = new CBillingJob(userId, 0x0c);
        job->productId = productId;
        job->productAmount = productAmount;
        job->pointDiff = -totalPrice;
        job->bGift = gift;
        CopyBillingText(job->sReceiverName, receiverName);
        CopyBillingText(job->sTitle, title);
        CopyBillingText(job->sContent, content);
        PushJob(job);

        const std::u16string targetName = std::u16string(user->GetName());
        const std::int64_t transmittedPrice = g_bCashshopPriceIsTotal ? totalPrice : unitPrice;
        m_pPremiumSvcSocket->Send(
            "CddddddhhQS", 0x0c, job->transactionId, static_cast<int>(user->GetAccountID()),
            static_cast<int>(user->GetDBID()), productId, productAmount, 1, static_cast<int>(GetPointCategory()),
            static_cast<int>(GetPointId()), transmittedPrice, targetName.c_str()
        );
        return BR_BUY_SUCCESS;
    }

    CPacketHelperEx<20480> packet;
    user->EndBuyProduct();
    if (paymentType == 0) {
        if (packet.Assemble(
                "Qddddd", std::int64_t{-9999}, static_cast<int>(user->GetDBID()), productId, productAmount, itemCount,
                product->m_stock
            ) == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[RequestBuyItem] helper.Assemble error")
            // L2_LOG_ERROR(Log,
            //     L"[RequestBuyItem] helper.Assemble error")
            L2_LOG_ERROR(Log, "Premium purchase failed: could not assemble product %d request header.", productId);
            return BR_BUY_SERVER_ERROR;
        }
        if (!gift) {
            packet.Assemble("dd", product->m_limitCount, product->m_limitPeriod);
        }
        for (int index = 0; index < itemCount; ++index) {
            CProduct::ItemData* const item = product->GetItemData(index);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[RequestBuyItem] invalid product item [%d] "
                //     L"is below zero",
                //     productId)
                // L2_LOG_ERROR(Log,
                //     L"[RequestBuyItem] invalid product item [%d] "
                //     L"is below zero",
                //     productId)
                L2_LOG_ERROR(Log, "Premium purchase rejected: product %d has no item data at index %d.", productId, index);
                return BR_BUY_INVALID_ITEM;
            }
            packet.Assemble("dd", item->typeId, item->amount);
            if (gift) {
                CItem* const prototype = FindPremiumItemPrototype(item->typeId);
                if (prototype == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[BuyItemAck] itemclassid %d is wrong!!!",
                    //     item->typeId)
                    // L2_LOG_ERROR(Log,
                    //     L"[BuyItemAck] itemclassid %d is wrong!!!",
                    //     item->typeId)
                    L2_LOG_ERROR(
                        Log, "Premium gift item processing failed: prototype for item class %d was not found.", item->typeId
                    );
                    return BR_BUY_INVALID_ITEM;
                }
                packet.Assemble("c", CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType) ? 1 : 0);
            }
        }
        if (!gift) {
            db.RequestGiveItem(0x62, &packet);
            return BR_BUY_SUCCESS;
        }

        const std::u16string targetReceiver = WideToUtf16(receiverName);
        const std::u16string targetTitle = WideToUtf16(title);
        const std::u16string targetContent = WideToUtf16(content);
        packet.Assemble("SSS", targetReceiver.c_str(), targetTitle.c_str(), targetContent.c_str());
        db.RequestSendGiftPost(&packet);
        return BR_BUY_SUCCESS;
    }

    if (paymentType != 1 && paymentType != 2) {
        return BR_BUY_INVALID_PRODUCT;
    }
    if (packet.Assemble(
            "Qddddddd", totalPrice, static_cast<int>(user->GetDBID()), productId, productAmount, itemCount, product->m_stock,
            product->m_limitCount, product->m_limitPeriod
        ) == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[RequestBuyItem] helper.Assemble error")
        // L2_LOG_ERROR(Log,
        //     L"[RequestBuyItem] helper.Assemble error")
        L2_LOG_ERROR(Log, "Premium purchase failed: could not assemble product %d request header.", productId);
        return BR_BUY_SERVER_ERROR;
    }
    for (int index = 0; index < itemCount; ++index) {
        CProduct::ItemData* const item = product->GetItemData(index);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[RequestBuyItem] invalid product item [%d] "
            //     L"is below zero",
            //     productId)
            // L2_LOG_ERROR(Log,
            //     L"[RequestBuyItem] invalid product item [%d] "
            //     L"is below zero",
            //     productId)
            L2_LOG_ERROR(Log, "Premium purchase rejected: product %d has no item data at index %d.", productId, index);
            return BR_BUY_INVALID_ITEM;
        }
        packet.Assemble("dd", item->typeId, item->amount);
    }
    LogPremiumPurchase(
        *user.get(), 0x2711, productId, -1, productAmount, -totalPrice, paymentType, static_cast<int>(unitPrice)
    );
    db.RequestGiveItem(paymentType, &packet);
    return BR_BUY_SUCCESS;
}

std::int16_t GetPointCategory() {
    switch (g_nCountryCode) {
        case 0:
        case 1:
        case 3:
        case 4:
        case 7:
            return 3;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            return 4;
        default:
            return 0;
    }
}

std::int16_t GetPointId() {
    switch (g_nCountryCode) {
        case 0:
        case 1:
        case 3:
        case 4:
            return 4;
        case 7:
            return 10;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
            return 0;
        default:
            return -1;
    }
}

bool CheckPointType(int category, int pointId) {
    const bool supportedCountry = g_nCountryCode >= 0 &&
        (g_nCountryCode < 2 ||
            (g_nCountryCode > 2 && (g_nCountryCode < 5 || static_cast<unsigned int>(g_nCountryCode - 7) < 7U)));
    return supportedCountry && GetPointCategory() == category && GetPointId() == pointId;
}

void CPremiumService::RequestGetGamePoint(int userId, int option) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected || g_PremiumSetting.cashshopPause) {
        return;
    }

    const std::int16_t category = GetPointCategory();
    const std::int16_t pointId = GetPointId();
    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RequestGetGamePoint : GetUser Fail, userId(%d)",
        //     userId)
        // L2_LOG_ERROR(Log,
        //     L"RequestGetGamePoint : GetUser Fail, userId(%d)",
        //     userId)
        L2_LOG_WARN(Log, "Premium balance query not requested: player object ID %d is unavailable.", userId);
        return;
    }

    auto* const job = new CBillingJob(userId, 0x1a);
    job->option = option;
    PushJob(job);
    m_pPremiumSvcSocket->Send(
        "Cdddhh", 0x1a, job->transactionId, user->GetAccountID(), 1, static_cast<int>(category), static_cast<int>(pointId)
    );
}

void CPremiumService::RequestAddGamePoint(int userId, std::int64_t point) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected || g_PremiumSetting.cashshopPause || point < 0) {
        return;
    }

    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AddGamePointReq : GetUser Fail, userId(%d)",
        //     userId)
        // L2_LOG_ERROR(Log,
        //     L"AddGamePointReq : GetUser Fail, userId(%d)",
        //     userId)
        L2_LOG_WARN(Log, "Premium point credit not requested: player object ID %d is unavailable.", userId);
        return;
    }

    auto* const job = new CBillingJob(userId, 0x19);
    job->option = 1;
    PushJob(job);
    m_pPremiumSvcSocket->Send(
        "CdddhhQ", 0x19, job->transactionId, user->GetAccountID(), 1, static_cast<int>(GetPointCategory()),
        static_cast<int>(GetPointId()), point
    );
}

void CPremiumService::RequestSubGamePoint(int userId, std::int64_t point) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected || g_PremiumSetting.cashshopPause || point < 0) {
        return;
    }

    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"SubGamePointReq : GetUser Fail, userId(%d)",
        //     userId)
        // L2_LOG_ERROR(Log,
        //     L"SubGamePointReq : GetUser Fail, userId(%d)",
        //     userId)
        L2_LOG_WARN(Log, "Premium point debit not requested: player object ID %d is unavailable.", userId);
        return;
    }

    // Current 0x009efb64 constructs command 0x19 even though the
    // transmitted opcode is 0x18.
    auto* const job = new CBillingJob(userId, 0x19);
    job->option = 1;
    PushJob(job);
    m_pPremiumSvcSocket->Send(
        "CdddhhQ", 0x18, job->transactionId, user->GetAccountID(), 1, static_cast<int>(GetPointCategory()),
        static_cast<int>(GetPointId()), point
    );
}

void CPremiumService::RequestDeleteItem(std::int64_t warehouseNo) {
    L2_GUARD_FUNCTION();
    if (warehouseNo <= 0) {
        return;
    }
    L2_GUARD(m_lock);
    const int sequence = ++m_nReqDeleteSequence;
    DeleteItemList items;
    items.push_back(warehouseNo);
    m_mapReqDelete.emplace(sequence, items);
    SendRequestDeleteItem(sequence);
}

void CPremiumService::RequestDeleteItem(int itemCount, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const int sequence = ++m_nReqDeleteSequence;
    DeleteItemList items;
    const std::uint8_t* cursor = packet;
    for (int index = 0; index < itemCount; ++index) {
        std::int64_t warehouseNo = 0;
        cursor = ::Disassemble(cursor, "Q", &warehouseNo);
        if (warehouseNo > 0) {
            items.push_back(warehouseNo);
        }
    }
    if (!items.empty()) {
        m_mapReqDelete.emplace(sequence, items);
        SendRequestDeleteItem(sequence);
    }
}

void CPremiumService::SendRequestDeleteItem(int sequence) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        return;
    }
    const auto request = m_mapReqDelete.find(sequence);
    if (request == m_mapReqDelete.end()) {
        return;
    }

    std::array<char, 0x2000> packet{};
    int packetSize = 0;
    int itemCount = 0;
    for (const std::int64_t warehouseNo : request->second) {
        packetSize += ::Assemble(packet.data() + packetSize, static_cast<int>(packet.size()) - packetSize, "Q", warehouseNo);
        if (packetSize > 0) {
            Log.Add(
                LOG_LONG_KEEP, u"%I64d,,,,,,,,,,,%I64d,%I64d,,,,,,,,,,,,,", static_cast<std::int64_t>(0x3e2),
                static_cast<std::int64_t>(sequence), warehouseNo
            );
        }
        ++itemCount;
    }
    if (packetSize > 0) {
        m_pPremiumSvcSocket->Send("Cddb", 0x17, sequence, itemCount, packetSize, packet.data());
    }
}

void CPremiumService::SetDeletedInIBServer(int sequence) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto request = m_mapReqDelete.find(sequence);
    if (request == m_mapReqDelete.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] cannot find transaction id[%d]",
        //     L"PremiumService.cpp",
        //     0x58d,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Premium] cannot find transaction id[%d]",
        //     L"PremiumService.cpp",
        //     0x58d,
        //     sequence)
        L2_LOG_WARN(Log, "Cannot confirm premium item deletion: request sequence %d was not found.", sequence);
        return;
    }

    std::array<char, 0x5000> packet{};
    int packetSize = 0;
    int itemCount = 0;
    for (const std::int64_t warehouseNo : request->second) {
        packetSize += ::Assemble(packet.data() + packetSize, static_cast<int>(packet.size()) - packetSize, "Q", warehouseNo);
        ++itemCount;
    }
    m_mapReqDelete.erase(request);
    db.SetDeletedInIBServer(itemCount, packetSize, packet.data());
}

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CPremiumItem, m_pClassName_CPremiumItem)

L2_DEFINE_OBJECT_ADD_REF(CPremiumItem)

L2_DEFINE_OBJECT_RELEASE(CPremiumItem)

CPremiumItem::CPremiumItem(
    std::int64_t warehouseNo, int itemClassId, std::int64_t itemAmount, bool isGift, wchar_t* buyerCharName
) :
    m_nWarehouseNo(warehouseNo), m_nItemClassId(itemClassId), m_nItemAmount(itemAmount), m_bIsGift(isGift) {
    m_sBuyerCharName.fill(u'\0');
    CopyBuyerCharName(m_sBuyerCharName, buyerCharName);
}

CPremiumItem::~CPremiumItem() = default;

BrBuyProductResultType GetBrBuyResultFromBilling(int billingResult) {
    L2_GUARD_FUNCTION();
    switch (billingResult) {
        case 0:
            return BR_BUY_SUCCESS;
        case 1:
            return BR_BUY_INVALID_USER;
        case 2:
            return BR_BUY_LACK_OF_POINT;
        case 5:
        case 6:
            return BR_BUY_INVALID_PRODUCT;
        default:
            return BR_BUY_SERVER_ERROR;
    }
}

namespace {

void SendGamePointResponse(User& user, const CBillingJob& job, std::int64_t point) {
    user.GetSocket()->Send("chdQd", 0xfe, 0xd6, job.userId, point, job.option);
}

} // namespace

bool DummyPremiumPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in CPremiumServiceSocket")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in CPremiumServiceSocket")
    L2_LOG_ERROR(Log, "Premium-service packet reached the reserved packet handler.");
    return false;
}

bool IBBuyGoodsForAccountAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBBuyGoodsForCharacterAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBGiftGoodsToAccountAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBGiftGoodsToCharacterAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBGetItemsForAccountAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBGetItemsForCharacterAckPacket(CPremiumServiceSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool IBNewItemDeliveredToAccountNotiPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    int purchaseId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet + 1, "ddd", &accountId, &purchaseId, &itemCount);
    if (itemCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item is notified, but no item. "
        //     L"packet[IBO_NEW_ITEM_DELIVERED_TO_ACCOUNT_NOTI]",
        //     L"PremiumService.cpp",
        //     0xd0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item is notified, but no item. "
        //     L"packet[IBO_NEW_ITEM_DELIVERED_TO_ACCOUNT_NOTI]",
        //     L"PremiumService.cpp",
        //     0xd0)
        L2_LOG_WARN(
            Log, "Premium account-delivery notification rejected: item count %d must be positive (purchase %d).", itemCount,
            purchaseId
        );
        return false;
    }

    CPacketHelperEx<20480> items;
    int validCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        std::int64_t warehouseNo = 0;
        int itemClassId = 0;
        int amount = 0;
        char extensionSize = 0;
        std::array<char, 20480> extension{};
        cursor = ::Disassemble(cursor, "QddB", &warehouseNo, &itemClassId, &amount, &extensionSize, extension.data());
        if (!IsValidPremiumItem(itemClassId, amount, 0xf1, 0xf7, 0xfc, 0x107)) {
            continue;
        }
        items.Assemble("QdQ", warehouseNo, itemClassId, static_cast<std::int64_t>(amount));
        ++validCount;
    }

    std::array<char16_t, 1> empty{};
    db.RequestSavePremiumItemForChar(
        accountId, 0, empty.data(), purchaseId, 0, empty.data(), validCount, items.GetLength(),
        items.GetPacket()
    );
    return false;
}

bool IBNewItemDeliveredToCharacterNotiPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int accountId = 0;
    int characterId = 0;
    int purchaseId = 0;
    int unused = 0;
    int receiverCharacterId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(
        packet + 1, "dddddd", &accountId, &characterId, &purchaseId, &unused, &receiverCharacterId, &itemCount
    );
    if (itemCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Premium item is notified, but no item. "
        //     L"packet[IBO_NEW_ITEM_DELIVERED_TO_CHARACTER_NOTI]",
        //     L"PremiumService.cpp",
        //     0x12a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Premium item is notified, but no item. "
        //     L"packet[IBO_NEW_ITEM_DELIVERED_TO_CHARACTER_NOTI]",
        //     L"PremiumService.cpp",
        //     0x12a)
        L2_LOG_WARN(
            Log, "Premium character-delivery notification rejected: item count %d must be positive (purchase %d).",
            itemCount, purchaseId
        );
        return false;
    }

    CPacketHelperEx<20480> items;
    int validCount = 0;
    for (int index = 0; index < itemCount; ++index) {
        std::int64_t warehouseNo = 0;
        int itemClassId = 0;
        int amount = 0;
        char extensionSize = 0;
        std::array<char, 20480> extension{};
        cursor = ::Disassemble(cursor, "QddB", &warehouseNo, &itemClassId, &amount, &extensionSize, extension.data());
        if (!IsValidPremiumItem(itemClassId, amount, 0x14b, 0x151, 0x156, 0x161)) {
            continue;
        }
        items.Assemble("QdQ", warehouseNo, itemClassId, static_cast<std::int64_t>(amount));
        ++validCount;
    }

    std::array<char16_t, 24> buyerName{};
    std::array<char16_t, 28> receiverName{};
    ::Disassemble(cursor, "SS", 0x30, buyerName.data(), 0x30, receiverName.data());
    db.RequestSavePremiumItemForChar(
        accountId, characterId, buyerName.data(), purchaseId, receiverCharacterId,
        receiverName.data(), validCount, items.GetLength(), items.GetPacket()
    );
    return false;
}

bool IBGetItemsForServerAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int worldId = 0;
    int result = 0;
    std::int16_t pointCategory = 0;
    std::int16_t pointId = 0;
    int itemCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet + 1, "ddhhd", &worldId, &result, &pointCategory, &pointId, &itemCount);
    if (worldId != g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Request from [%d], but reply to [%d]",
        //     L"PremiumService.cpp",
        //     0x187,
        //     worldId,
        //     g_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Request from [%d], but reply to [%d]",
        //     L"PremiumService.cpp",
        //     0x187,
        //     worldId,
        //     g_nWorldId)
        L2_LOG_ERROR(Log, "Premium item-list response rejected: received world %d, local world %d.", worldId, g_nWorldId);
        return false;
    }
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Result Error[%d]",
        //     L"PremiumService.cpp",
        //     0x18c,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Result Error[%d]",
        //     L"PremiumService.cpp",
        //     0x18c,
        //     result)
        L2_LOG_ERROR(Log, "Premium item-list request failed: billing result %d.", result);
        return false;
    }

    for (int index = 0; index < itemCount; ++index) {
        std::int64_t warehouseNo = 0;
        int itemClassId = 0;
        int amount = 0;
        char extensionSize = 0;
        std::array<char, 20480> extension{};
        int value1 = 0;
        int ignoredValue = 0;
        int value2 = 0;
        std::array<char16_t, 24> buyerName{};
        int value3 = 0;
        int characterId = 0;
        std::array<char16_t, 24> receiverName{};
        cursor = ::Disassemble(
            cursor, "QddBdddSddS", &warehouseNo, &itemClassId, &amount, &extensionSize, extension.data(), &value1,
            &ignoredValue, &value2, 0x30, buyerName.data(), &value3, &characterId, 0x30, receiverName.data()
        );

        CPacketHelperEx<20480> helper;
        const std::uint32_t productOffset = static_cast<std::uint32_t>(itemClassId) - 100001U;
        if (productOffset < 1899999U) {
            CProductSP product = g_ShopServer.GetProduct(itemClassId);
            if (!product) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] product [%d] is invalid",
                //     L"PremiumService.cpp",
                //     0x1b4,
                //     itemClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] product [%d] is invalid",
                //     L"PremiumService.cpp",
                //     0x1b4,
                //     itemClassId)
                L2_LOG_ERROR(
                    Log, "Premium item-list processing stopped at entry %d of %d: product %d was not found.", index + 1,
                    itemCount, itemClassId
                );
                break;
            }
            if (amount < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] product amount [%d] is below zero",
                //     L"PremiumService.cpp",
                //     0x1ba,
                //     amount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] product amount [%d] is below zero",
                //     L"PremiumService.cpp",
                //     0x1ba,
                //     amount)
                L2_LOG_ERROR(
                    Log, "Premium item-list processing stopped at entry %d of %d: product %d quantity %d must be positive.",
                    index + 1, itemCount, itemClassId, amount
                );
                break;
            }
            const int productItemCount = product->GetItemCount();
            if (helper.Assemble(
                    "Qddddddd", warehouseNo, characterId, itemClassId, amount, productItemCount, product->m_stock,
                    product->m_limitCount, product->m_limitPeriod
                ) == 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] helper.Assemble error",
                //     L"PremiumService.cpp",
                //     0x1c3)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] helper.Assemble error",
                //     L"PremiumService.cpp",
                //     0x1c3)
                L2_LOG_ERROR(
                    Log,
                    "Premium item-list processing stopped at entry %d of %d: cannot assemble delivery header for product "
                    "%d.",
                    index + 1, itemCount, itemClassId
                );
                break;
            }
            bool validProductItems = true;
            for (int itemIndex = 0; itemIndex < productItemCount; ++itemIndex) {
                CProduct::ItemData* const productItem = product->GetItemData(itemIndex);
                if (productItem == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] invalid product item [%d]",
                    //     L"PremiumService.cpp",
                    //     0x1cc,
                    //     itemClassId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] invalid product item [%d]",
                    //     L"PremiumService.cpp",
                    //     0x1cc,
                    //     itemClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Premium item-list processing stopped at entry %d of %d: product %d has no item data at index %d.",
                        index + 1, itemCount, itemClassId, itemIndex
                    );
                    validProductItems = false;
                    break;
                }
                helper.Assemble("dd", productItem->typeId, productItem->amount);
            }
            if (!validProductItems) {
                break;
            }
            db.RequestGiveItem(99, &helper);
            continue;
        }

        if (!IsValidPremiumItem(itemClassId, amount, 0x1d9, 0x1df, 0x1e4, 0x1ea)) {
            continue;
        }
        const int assembled = helper.Assemble(
            "QdQddSddS", warehouseNo, itemClassId, static_cast<std::int64_t>(amount), value1, value2, buyerName.data(),
            value3, characterId, receiverName.data()
        );
        if (helper.GetLength() > 0) {
            db.RequestSavePremiumItemsForServer(assembled != 0 ? 1 : 0, helper.GetLength(), helper.GetPacket());
        }
    }
    return false;
}

bool IBBuyItemAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    int result = 0;
    std::int64_t warehouseNo = 0;
    ::Disassemble(packet + 1, "ddQ", &transactionId, &result, &warehouseNo);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][BuyItemAck] transaction=%d, result=%d, warehouse=%d",
    //     transactionId,
    //     result,
    //     warehouseNo)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][BuyItemAck] transaction=%d, result=%d, warehouse=%d",
    //     transactionId,
    //     result,
    //     warehouseNo)
    L2_LOG_INFO(
        Log,
        "Received premium purchase response: transaction %d, result %d, warehouse %lld; delivery has not yet been "
        "processed.",
        transactionId, result, static_cast<long long>(warehouseNo)
    );

    CBillingJobSP job = g_PremiumSvc.PopJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BuyItemAck] cannot get transaction(%d)",
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[BuyItemAck] cannot get transaction(%d)",
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium purchase response cannot be processed: billing job for transaction %d is unavailable.",
            transactionId
        );
        return false;
    }
    CSPointer<User> user = g_UserMap.FindObject(job->userId);
    if (!user) {
        return false;
    }
    user->EndBuyProduct();

    const int clientOpcode = job->bGift ? 0x11a : 0xd9;
    if (result != 0) {
        SendBuyResult(*user.get(), clientOpcode, GetBrBuyResultFromBilling(result));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BuyItemAck] Result is not ok[%d], warehouseNo[%I64d]",
        //     result,
        //     warehouseNo)
        // L2_LOG_ERROR(Log,
        //     L"[BuyItemAck] Result is not ok[%d], warehouseNo[%I64d]",
        //     result,
        //     warehouseNo)
        L2_LOG_WARN(
            Log,
            "Billing rejected premium purchase transaction %d: result %d, warehouse %lld; player failure-response routine "
            "returned.",
            transactionId, result, static_cast<long long>(warehouseNo)
        );
        return false;
    }

    CProductSP product = g_ShopServer.GetProduct(job->productId);
    if (!product) {
        SendBuyResult(*user.get(), clientOpcode, BR_BUY_INVALID_PRODUCT);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BuyItemAck] Cannot find product, product id[%d]",
        //     job->productId)
        // L2_LOG_ERROR(Log,
        //     L"[BuyItemAck] Cannot find product, product id[%d]",
        //     job->productId)
        L2_LOG_ERROR(
            Log, "Premium purchase delivery rejected: product %d was not found for transaction %d.", job->productId,
            transactionId
        );
        return false;
    }

    CPacketHelperEx<20480> helper;
    const int productItemCount = product->GetItemCount();
    if (helper.Assemble(
            "Qddddd", warehouseNo, static_cast<int>(user->GetDBID()), job->productId, job->productAmount, productItemCount,
            product->m_stock
        ) == 0) {
        SendBuyResult(*user.get(), clientOpcode, BR_BUY_SERVER_ERROR);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[BuyItemAck] helper.Assemble error")
        // L2_LOG_ERROR(Log,
        //     L"[BuyItemAck] helper.Assemble error")
        L2_LOG_ERROR(
            Log, "Cannot assemble premium purchase delivery header for transaction %d, product %d.", transactionId,
            job->productId
        );
        return false;
    }
    if (!job->bGift) {
        helper.Assemble("dd", product->m_limitCount, product->m_limitPeriod);
    }
    for (int index = 0; index < productItemCount; ++index) {
        CProduct::ItemData* const item = product->GetItemData(index);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[BuyItemAck] invalid product item [%d] is below zero",
            //     job->productId)
            // L2_LOG_ERROR(Log,
            //     L"[BuyItemAck] invalid product item [%d] is below zero",
            //     job->productId)
            L2_LOG_ERROR(
                Log, "Premium purchase delivery rejected: product %d has no item data at index %d (transaction %d).",
                job->productId, index, transactionId
            );
            SendBuyResult(*user.get(), clientOpcode, BR_BUY_INVALID_ITEM);
            return false;
        }
        helper.Assemble("dd", item->typeId, item->amount);
        if (job->bGift) {
            CItem* const prototype = FindPremiumItemPrototype(item->typeId);
            if (prototype == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[BuyItemAck] itemclassid %d is wrong!!!",
                //     item->typeId)
                // L2_LOG_ERROR(Log,
                //     L"[BuyItemAck] itemclassid %d is wrong!!!",
                //     item->typeId)
                L2_LOG_ERROR(
                    Log, "Premium gift item processing failed: prototype for item class %d was not found.", item->typeId
                );
                SendBuyResult(*user.get(), clientOpcode, BR_BUY_INVALID_ITEM);
                return false;
            }
            helper.Assemble("c", CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType) ? 1 : 0);
        }
    }

    if (!job->bGift) {
        LogPremiumPurchase(
            *user.get(), 0x2711, job->productId, warehouseNo, job->productAmount, user->m_GamePoint, product->m_paymentType,
            product->m_price
        );
        db.RequestGiveItem(0, &helper);
        return false;
    }

    const std::wstring receiverName = BillingTextToHost(job->sReceiverName);
    LogPremiumPurchase(
        *user.get(), 0x2717, job->productId, warehouseNo, job->productAmount, user->m_GamePoint, product->m_paymentType,
        product->m_price, receiverName.c_str()
    );
    helper.Assemble("SSS", job->sReceiverName.data(), job->sTitle.data(), job->sContent.data());
    db.RequestSendGiftPost(&helper);
    return false;
}

bool IBVersionPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int version = 0;
    ::Disassemble(packet + 1, "d", &version);
    // Log.Add(LOG_NORMAL, L"IBServer Ver: %d", version)
    // L2_LOG_INFO(Log, L"IBServer Ver: %d", version)
    L2_LOG_INFO(Log, "Received IBServer protocol version %d; expected %d.", version, 0x13a27);
    if (version == 0x13a27) {
        g_PremiumSvc.m_bSendable = true;
        g_PremiumSvc.RequestGetPremiumItems();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBServer Version is not math with Game Server(%d)",
        //     0x13a27)
        // L2_LOG_ERROR(Log,
        //     L"IBServer Version is not math with Game Server(%d)",
        //     0x13a27)
        L2_LOG_ERROR(
            Log, "IBServer protocol version mismatch: received %d, expected %d; existing send-readiness flag is unchanged.",
            version, 0x13a27
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"Billing Function will not work !")
        // Noise: preceding mismatch log describes the failure. This branch does not reset readiness.
        // L2_LOG_ERROR(Log,
        //     L"Billing Function will not work !")
    }
    db.RequestLoadPremiumItemForBatch();
    return false;
}

bool IBDeleteItemsAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    int result = 0;
    ::Disassemble(packet + 1, "dd", &transactionId, &result);
    if (result == 0) {
        g_PremiumSvc.SetDeletedInIBServer(transactionId);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] Result Error[%d]",
        //     L"PremiumService.cpp",
        //     0x21e,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][Premium] Result Error[%d]",
        //     L"PremiumService.cpp",
        //     0x21e,
        //     result)
        L2_LOG_ERROR(
            Log,
            "Premium item deletion was not acknowledged: transaction %d, billing result %d; pending request is retained.",
            transactionId, result
        );
    }
    return false;
}

bool IBGetPointAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    int result = 0;
    int pointCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet + 1, "ddd", &transactionId, &result, &pointCount);
    CBillingJobSP job = g_PremiumSvc.PopJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBGetPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"IBGetPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium balance response cannot be processed: billing job for transaction %d is unavailable.",
            transactionId
        );
        return false;
    }

    CSPointer<User> user = g_UserMap.FindObject(job->userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot find user, user id[%d]",
        //     L"PremiumService.cpp",
        //     0x240,
        //     job->userId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot find user, user id[%d]",
        //     L"PremiumService.cpp",
        //     0x240,
        //     job->userId)
        L2_LOG_WARN(
            Log, "Premium balance response cannot update player object ID %d: player unavailable (transaction %d).",
            job->userId, transactionId
        );
        return false;
    }
    if (result != 0) {
        SendGamePointResponse(*user.get(), *job.get(), 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBGetPointAckPacket : result is not ok (%d)",
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"IBGetPointAckPacket : result is not ok (%d)",
        //     result)
        L2_LOG_WARN(Log, "Premium balance response rejected for transaction %d: billing result %d.", transactionId, result);
        return false;
    }
    if (pointCount != 1) {
        SendGamePointResponse(*user.get(), *job.get(), 0);
        // Log.Add(
        //     LOG_WARN,
        //     L"IBGetPointAckPacket : pointCount should be 1")
        // L2_LOG_WARN(Log,
        //     L"IBGetPointAckPacket : pointCount should be 1")
        L2_LOG_WARN(
            Log, "Premium balance response rejected for transaction %d: received %d point records, expected one.",
            transactionId, pointCount
        );
        return false;
    }

    std::int64_t point = 0;
    for (int index = 0; index < pointCount; ++index) {
        std::int16_t category = 0;
        std::int16_t pointId = 0;
        cursor = ::Disassemble(cursor, "hhQ", &category, &pointId, &point);
        if (!CheckPointType(category, pointId)) {
            // Log.Add(
            //     LOG_WARN,
            //     L"IBGetPointAckPacket : invalid point type category(%d), pointid(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            // L2_LOG_WARN(Log,
            //     L"IBGetPointAckPacket : invalid point type category(%d), pointid(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            L2_LOG_WARN(
                Log, "Premium balance response rejected for transaction %d: unsupported point category %d, point ID %d.",
                transactionId, static_cast<int>(category), static_cast<int>(pointId)
            );
            SendGamePointResponse(*user.get(), *job.get(), 0);
            return false;
        }
    }
    user->m_GamePoint = point;
    SendGamePointResponse(*user.get(), *job.get(), point);
    return false;
}

bool IBAddPointAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x009ed974.
    int transactionId = 0;
    int result = 0;
    int pointCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet + 1, "ddd", &transactionId, &result, &pointCount);
    CBillingJobSP job = g_PremiumSvc.PopJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBAddPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"IBAddPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium point-credit response cannot be processed: billing job for transaction %d is unavailable.",
            transactionId
        );
        return false;
    }
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBAddPointAckPacket : result is not ok (%d)",
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"IBAddPointAckPacket : result is not ok (%d)",
        //     result)
        L2_LOG_WARN(
            Log, "Premium point-credit response rejected for transaction %d: billing result %d.", transactionId, result
        );
        return false;
    }
    if (pointCount != 1) {
        // Log.Add(
        //     LOG_WARN,
        //     L"IBAddPointAckPacket : pointCount should be 1")
        // L2_LOG_WARN(Log,
        //     L"IBAddPointAckPacket : pointCount should be 1")
        L2_LOG_WARN(
            Log, "Premium point-credit response rejected for transaction %d: received %d point records, expected one.",
            transactionId, pointCount
        );
        return false;
    }

    std::int64_t point = 0;
    for (int index = 0; index < pointCount; ++index) {
        std::int16_t category = 0;
        std::int16_t pointId = 0;
        cursor = ::Disassemble(cursor, "hhQ", &category, &pointId, &point);
        if (category != GetPointCategory() || pointId != GetPointId()) {
            // Log.Add(
            //     LOG_WARN,
            //     L"IBAddPointAckPacket : game point is invalid, "
            //     L"category(%d), pointId(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            // L2_LOG_WARN(Log,
            //     L"IBAddPointAckPacket : game point is invalid, "
            //     L"category(%d), pointId(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            L2_LOG_WARN(
                Log,
                "Premium point-credit response rejected for transaction %d: unsupported point category %d, point ID %d.",
                transactionId, static_cast<int>(category), static_cast<int>(pointId)
            );
            return false;
        }
    }

    CSPointer<User> user = g_UserMap.FindObject(job->userId);
    if (!user) {
        // Current body prints the transaction field although the diagnostic
        // labels it as a character ID.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot find user, char id[%d]",
        //     L"PremiumService.cpp",
        //     0x2a6,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot find user, char id[%d]",
        //     L"PremiumService.cpp",
        //     0x2a6,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium point-credit response cannot update player object ID %d: player unavailable (transaction %d).",
            job->userId, transactionId
        );
        return false;
    }
    user->m_GamePoint = point;
    user->GetSocket()->Send("chdQd", 0xfe, 0xd6, job->userId, point, job->option);
    return false;
}

bool IBSubPointAckPacket(CPremiumServiceSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x009eddd8.
    int transactionId = 0;
    int result = 0;
    int pointCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet + 1, "ddd", &transactionId, &result, &pointCount);
    CBillingJobSP job = g_PremiumSvc.PopJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBSubPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"IBSubPointAckPacket : cannot get transaction(%d)",
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium point-debit response cannot be processed: billing job for transaction %d is unavailable.",
            transactionId
        );
        return false;
    }
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"IBSubPointAckPacket : result is not ok (%d)",
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"IBSubPointAckPacket : result is not ok (%d)",
        //     result)
        L2_LOG_WARN(
            Log, "Premium point-debit response rejected for transaction %d: billing result %d.", transactionId, result
        );
        return false;
    }
    if (pointCount != 1) {
        // Log.Add(
        //     LOG_WARN,
        //     L"IBSubPointAckPacket : pointCount should be 1")
        // L2_LOG_WARN(Log,
        //     L"IBSubPointAckPacket : pointCount should be 1")
        L2_LOG_WARN(
            Log, "Premium point-debit response rejected for transaction %d: received %d point records, expected one.",
            transactionId, pointCount
        );
        return false;
    }

    std::int64_t point = 0;
    for (int index = 0; index < pointCount; ++index) {
        std::int16_t category = 0;
        std::int16_t pointId = 0;
        cursor = ::Disassemble(cursor, "hhQ", &category, &pointId, &point);
        if (category != GetPointCategory() || pointId != GetPointId()) {
            // Log.Add(
            //     LOG_WARN,
            //     L"IBSubPointAckPacket : game point is invalid, "
            //     L"category(%d), pointId(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            // L2_LOG_WARN(Log,
            //     L"IBSubPointAckPacket : game point is invalid, "
            //     L"category(%d), pointId(%d)",
            //     static_cast<int>(category),
            //     static_cast<int>(pointId))
            L2_LOG_WARN(
                Log, "Premium point-debit response rejected for transaction %d: unsupported point category %d, point ID %d.",
                transactionId, static_cast<int>(category), static_cast<int>(pointId)
            );
            return false;
        }
    }

    CSPointer<User> user = g_UserMap.FindObject(job->userId);
    if (!user) {
        // Current body prints the transaction field although the diagnostic
        // labels it as a character ID.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot find user, char id[%d]",
        //     L"PremiumService.cpp",
        //     0x2eb,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot find user, char id[%d]",
        //     L"PremiumService.cpp",
        //     0x2eb,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Premium point-debit response cannot update player object ID %d: player unavailable (transaction %d).",
            job->userId, transactionId
        );
        return false;
    }
    user->m_GamePoint = point;
    user->GetSocket()->Send("chdQd", 0xfe, 0xd6, job->userId, point, job->option);
    return false;
}
