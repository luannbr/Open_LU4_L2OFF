#include "GoodsInventory.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TextRuntime.h"

#include "CConfig.h"
#include "Item.h"
#include "ObjectDB.h"
#include "PremiumService.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/OwnedThread.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <new>
#include <string>
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

constexpr std::uint64_t kFileTimeUnixSkew = UINT64_C(116444736000000000);
constexpr std::uint64_t kFileTimeTicksPerSecond = UINT64_C(10000000);

int CurrentGoodsInventoryLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

// int LegacyGoodsInventorySocketSymbolForLog() noexcept {
[[maybe_unused]] int LegacyGoodsInventorySocketSymbolForLog() noexcept {
    return static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket));
}

void CloseGoodsInventoryNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

void StoreLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}

std::uint64_t FileTimeBits(FileTime value) noexcept {
    return static_cast<std::uint64_t>(value.dwLowDateTime) | (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U);
}

bool FileTimeToUtcSystemTime(FileTime value, runtime::SystemTime& output) noexcept {
#if defined(_WIN32)
    const FILETIME native{
        value.dwLowDateTime,
        value.dwHighDateTime,
    };
    SYSTEMTIME converted{};
    if (::FileTimeToSystemTime(&native, &converted) == 0) {
        return false;
    }
    output = {
        converted.wYear,
        converted.wMonth,
        converted.wDayOfWeek,
        converted.wDay,
        converted.wHour,
        converted.wMinute,
        converted.wSecond,
        converted.wMilliseconds,
    };
    return true;
#else
    const std::uint64_t bits = FileTimeBits(value);
    std::int64_t ticksFromEpoch = 0;
    if (bits >= kFileTimeUnixSkew) {
        const std::uint64_t positive = bits - kFileTimeUnixSkew;
        if (positive > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        ticksFromEpoch = static_cast<std::int64_t>(positive);
    } else {
        ticksFromEpoch = -static_cast<std::int64_t>(kFileTimeUnixSkew - bits);
    }

    std::int64_t seconds = ticksFromEpoch / static_cast<std::int64_t>(kFileTimeTicksPerSecond);
    if (ticksFromEpoch < 0 && ticksFromEpoch % static_cast<std::int64_t>(kFileTimeTicksPerSecond) != 0) {
        --seconds;
    }
    if (seconds < static_cast<std::int64_t>(std::numeric_limits<std::time_t>::min()) ||
        seconds > static_cast<std::int64_t>(std::numeric_limits<std::time_t>::max())) {
        return false;
    }

    const std::time_t hostTime = static_cast<std::time_t>(seconds);
    std::tm utc{};
    if (::gmtime_r(&hostTime, &utc) == nullptr) {
        return false;
    }
    output = {
        static_cast<std::uint16_t>(utc.tm_year + 1900),
        static_cast<std::uint16_t>(utc.tm_mon + 1),
        static_cast<std::uint16_t>(utc.tm_wday),
        static_cast<std::uint16_t>(utc.tm_mday),
        static_cast<std::uint16_t>(utc.tm_hour),
        static_cast<std::uint16_t>(utc.tm_min),
        static_cast<std::uint16_t>(utc.tm_sec),
        static_cast<std::uint16_t>(
            (ticksFromEpoch < 0
                    ? (kFileTimeTicksPerSecond - static_cast<std::uint64_t>(-ticksFromEpoch) % kFileTimeTicksPerSecond) %
                        kFileTimeTicksPerSecond
                    : static_cast<std::uint64_t>(ticksFromEpoch) % kFileTimeTicksPerSecond) /
            UINT64_C(10000)
        ),
    };
    return true;
#endif
}

std::uint64_t CurrentLocalWallFileTimeBits() noexcept {
#if defined(_WIN32)
    SYSTEMTIME local{};
    FILETIME fileTime{};
    ::GetLocalTime(&local);
    ::SystemTimeToFileTime(&local, &fileTime);
    return static_cast<std::uint64_t>(fileTime.dwLowDateTime) | (static_cast<std::uint64_t>(fileTime.dwHighDateTime) << 32U);
#else
    const auto now = std::chrono::system_clock::now();
    const auto sinceEpoch = now.time_since_epoch();
    const auto ticks =
        std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>>(sinceEpoch).count();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&seconds, &local);
    const std::time_t localAsUtc = timegm(&local);
    const std::int64_t localOffset = static_cast<std::int64_t>(localAsUtc) - static_cast<std::int64_t>(seconds);
    return kFileTimeUnixSkew +
        static_cast<std::uint64_t>(ticks + localOffset * static_cast<std::int64_t>(kFileTimeTicksPerSecond));
#endif
}

template <std::size_t Capacity>
void CopyTargetText(char16_t (&destination)[Capacity], const wchar_t* source) {
    std::fill(std::begin(destination), std::end(destination), u'\0');
    if (source == nullptr) {
        runtime::process::HandleInvalidParameter();
        return;
    }
    const auto* const targetSource = reinterpret_cast<const char16_t*>(source);
    std::size_t length = 0;
    while (targetSource[length] != u'\0') {
        ++length;
    }
    if (length >= Capacity) {
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::copy_n(targetSource, length, destination);
}

template <std::size_t Capacity>
void CopyTargetText(char16_t (&destination)[Capacity], const char16_t* source) {
    CopyTargetText(destination, reinterpret_cast<const wchar_t*>(source));
}

template <std::size_t Capacity>
bool TargetTextEquals(const char16_t (&left)[Capacity], const wchar_t* right) noexcept {
    if (right == nullptr) {
        return false;
    }
    const auto* const targetRight = reinterpret_cast<const char16_t*>(right);
    std::size_t index = 0;
    while (index < Capacity && left[index] == targetRight[index]) {
        if (left[index] == u'\0') {
            return true;
        }
        ++index;
    }
    return false;
}


template <std::size_t Capacity>
void CopyNarrowText(char (&destination)[Capacity], const char* source) {
    std::fill(std::begin(destination), std::end(destination), '\0');
    if (source == nullptr) {
        runtime::process::HandleInvalidParameter();
        return;
    }
    const std::size_t length = std::strlen(source);
    if (length >= Capacity) {
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::memcpy(destination, source, length);
}

struct DecodedGoodsInfo {
    int goodsPrimaryId = 0;
    int itemPrimaryId = 0;
    int itemClassId = 0;
    char restrictionCondition[32]{};
    char ignored1e[30]{};
    std::int16_t goodsIconId = 0;
    char ignored40[64]{};
    char ignored20[32]{};
    std::int16_t ignoredShort1 = 0;
    char16_t goodsName[41]{};
    std::int16_t ignoredShort2 = 0;
    char16_t goodsDesc[151]{};
};

const std::uint8_t* DecodeGoodsInfo(const std::uint8_t* packet, DecodedGoodsInfo& info) {
    return Disassemble(
        packet, "dddbbhbbhShS", &info.goodsPrimaryId, &info.itemPrimaryId, &info.itemClassId, 0x20,
        info.restrictionCondition, 0x1e, info.ignored1e, &info.goodsIconId, 0x40, info.ignored40, 0x20, info.ignored20,
        &info.ignoredShort1, 0x52, info.goodsName, &info.ignoredShort2, 0x12e, info.goodsDesc
    );
}

void SendGoodsInventoryClientResult(User* user, int result) {
    if (user != nullptr) {
        user->GetSocket()->Send("chd", 0xfe, 0x10e, result);
    }
}

std::uint32_t GoodsInventoryConnectThread(void*) {
    CThreadManager::RegisterThisThread(ThreadID::GOODS_INVENTORY);
    const int threadId = CThreadManager::GetThreadID();
    g_IOBufferPools[threadId] = new (std::nothrow) CNewMwsrQueue<DefaultIOBuffer>(0x80);
    g_IOBuffer8kPools[threadId] = new (std::nothrow) CNewMwsrQueue<IOBuffer8k>(0x80);

    const auto run = [] {
        for (;;) {
            g_GoodsInventory.Init();
            std::this_thread::sleep_for(std::chrono::milliseconds(g_GoodsInventorySetting.heartBeat));
        }
    };
    if (ExceptionMailing()) {
        // Log.Add(LOG_NORMAL, L"goods inventory in mailing mode")
        // L2_LOG_INFO(Log, L"goods inventory in mailing mode")
        L2_LOG_INFO(Log, "Goods-inventory connection worker uses exception-mail reporting.");
        try {
            run();
        } catch (...) {
            runtime::process::HandleUnhandledSharedThreadException(
                "GoodsInventory.cpp", 0x57d, "Thu May 10 05:00:26 2018"
            );
        }
    } else {
        run();
    }
    return 0;
}

} // namespace

CGoodsInventoryCallbackMap g_GoodsInventoryPacketTable(DummyGoodsInventoryPacket, u"");

namespace {

L2_BIND_PACKET(IsRegistered_IBO_PROTOCOL_VER, g_GoodsInventoryPacketTable, 0x00, IBVersionPacket);
L2_BIND_PACKET(
    IsRegistered_IBO_INQUIRY_ALL_GOODS_INFO_ACK, g_GoodsInventoryPacketTable, 0x24, IBInquiryAllGoodsInfoAckPacket
);
L2_BIND_PACKET(
    IsRegistered_IBO_INQUIRY_ONE_GOODS_INFO_ACK, g_GoodsInventoryPacketTable, 0x25, IBInquiryOneGoodsInfoAckPacket
);
L2_BIND_PACKET(
    IsRegistered_IBO_GOODS_INVENTORY_CHANGED_NOTI, g_GoodsInventoryPacketTable, 0x1e, IBGoodsInventoryChangedNoti
);
L2_BIND_PACKET(
    IsRegistered_IBO_INQUIRY_GOODS_INVENTORY_ACK, g_GoodsInventoryPacketTable, 0x1f, IBInquiryGoodsInventoryAckPacket
);
L2_BIND_PACKET(
    IsRegistered_IBO_RETRIEVE_GOODS_FROM_INVENTORY_ACK, g_GoodsInventoryPacketTable, 0x20, IBRetrieveGoodsFromInventory
);
L2_BIND_PACKET(IsRegistered_IBO_INQUIRY_EXIST_GOODS_ACK, g_GoodsInventoryPacketTable, 0x22, IBInquiryExistGoodsAck);
L2_BIND_PACKET(IsRegistered_IBO_GOODSINFO_CHANGED_NOTI, g_GoodsInventoryPacketTable, 0x2d, IBGoodsInfoChangedNoti);

} // namespace

CGoodsInventory g_GoodsInventory;

// static_assert(L2ServerMemory::kCGoodsInventorySocketClassId == 125)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CGoodsInventorySocket) == 125);
// static_assert(L2ServerMemory::kCGoodsInventoryJobClassId == 126)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CGoodsInventoryJob) == 126);
// static_assert(L2ServerMemory::kCNewGoodsClassId == 127)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CNewGoods) == 127);

L2_DEFINE_OBJECT_NEW(CGoodsInventorySocket)

L2_DEFINE_OBJECT_ALLOCATOR(CGoodsInventorySocket, m_pClassName_CGoodsInventorySocket)

CGoodsInventorySocket::CGoodsInventorySocket(std::uint64_t socket) :
    CIOSocket(socket, 0x1000),
    m_socket(socket),
    m_threadCount(0),
    m_packetTable(g_GoodsInventoryPacketTable.GetData()),
    m_mode(SocketMode::SM_READ_LEN),
    m_packetLength(0) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_NORMAL,
    //     L"GoodsInventorySocket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    // L2_LOG_INFO(Log,
    //     L"GoodsInventorySocket open %x(%x)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)))
    L2_LOG_INFO(
        Log, "Goods-inventory socket %llu created (object %p); protocol validation is pending.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this)
    );
}

CGoodsInventorySocket::~CGoodsInventorySocket() { m_packetTable = nullptr; }

L2_DEFINE_OBJECT_RELEASE(CGoodsInventorySocket)

bool CGoodsInventorySocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid goods inventory socket.",
        //     L"GoodsInventory.cpp",
        //     0x4e0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Write to invalid goods inventory socket.",
        //     L"GoodsInventory.cpp",
        //     0x4e0)
        L2_LOG_ERROR(Log, "Goods-inventory write rejected: socket object %p is closed.", static_cast<void*>(this));
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"goods inventory socket pending write > %d KB, Close!",
    //     0x2000)
    // L2_LOG_ERROR(Log,
    //     L"goods inventory socket pending write > %d KB, Close!",
    //     0x2000)
    L2_LOG_ERROR(
        Log, "Goods-inventory write rejected: pending data exceeds 8192 KiB (socket %llu).",
        static_cast<unsigned long long>(m_socket)
    );
    runtime::process::ReportCallStack(2, false);
    return false;
}

void CGoodsInventorySocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CGoodsInventorySocket::SendV(const char* format, va_list arguments) {
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
        //     LegacyGoodsInventorySocketSymbolForLog(),
        //     format)
        // L2_LOG_ERROR(Log,
        //     L"%d: assemble too large packet. format %s",
        //     LegacyGoodsInventorySocketSymbolForLog(),
        //     format)
        L2_LOG_ERROR(
            Log,
            "Goods-inventory packet assembly returned zero for socket %llu (capacity 8190 bytes, format %s); submitting an "
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

void CGoodsInventorySocket::OnClose() {
    L2_GUARD_FUNCTION();
    m_mode = SocketMode::SM_CLOSE;
    const int error = CurrentGoodsInventoryLastError();
    // Log.Add(
    //     LOG_ERROR,
    //     L"CGoodsInventorySocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    // L2_LOG_ERROR(Log,
    //     L"CGoodsInventorySocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     error)
    L2_LOG_WARN(
        Log, "Goods-inventory socket %llu closed (object %p); last-error snapshot %d.",
        static_cast<unsigned long long>(m_socket), static_cast<void*>(this), error
    );
    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,,", 0x51c, 1, static_cast<unsigned int>(m_socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(this)), error
    );
    g_GoodsInventory.m_bConnected = false;
}

void CGoodsInventorySocket::OnRead() {
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
                //     L"goods inventory socket %d: bad packet size %d",
                //     LegacyGoodsInventorySocketSymbolForLog(),
                //     m_packetLength)
                // L2_LOG_ERROR(Log,
                //     L"goods inventory socket %d: bad packet size %d",
                //     LegacyGoodsInventorySocketSymbolForLog(),
                //     m_packetLength)
                L2_LOG_ERROR(
                    Log, "Closing goods-inventory socket %llu: packet body length %d is outside 1..8190 bytes.",
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
            //     L"[Goods Inventory] unknown protocol %d",
            //     static_cast<int>(opcode))
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory] unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "Closing goods-inventory socket %llu: opcode %u exceeds 45.", static_cast<unsigned long long>(m_socket),
                static_cast<unsigned>(opcode)
            );
            m_mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const packet = new CGoodsInventoryPacket;
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

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CGoodsInventoryJob, m_pClassName_CGoodsInventoryJob)

L2_DEFINE_OBJECT_RELEASE(CGoodsInventoryJob)

CGoodsInventoryJob::CGoodsInventoryJob(int userId, std::int16_t command) :
    m_nUserId(userId), m_nCommand(command), m_bIsUserAction(false), m_nGoodsUid(0) {
    m_nTransactionId = g_GoodsInventory.CreateTransactionId();
}

CGoodsInventoryJob::CGoodsInventoryJob(int userId, std::int16_t command, std::int64_t goodsUid) :
    m_nUserId(userId), m_nCommand(command), m_bIsUserAction(false), m_nGoodsUid(goodsUid) {
    m_nTransactionId = g_GoodsInventory.CreateTransactionId();
}

CGoodsInventoryJob::CGoodsInventoryJob(int userId, std::int16_t command, bool isUserAction) :
    m_nUserId(userId), m_nCommand(command), m_bIsUserAction(isUserAction), m_nGoodsUid(0) {
    m_nTransactionId = g_GoodsInventory.CreateTransactionId();
}

CGoodsInventoryJob::~CGoodsInventoryJob() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(CNewGoods, m_pClassName_CNewGoods)

L2_DEFINE_OBJECT_RELEASE(CNewGoods)

CNewGoods::CNewGoods(
    int goodsPrimaryId, wchar_t* goodsName, wchar_t* goodsDesc, int goodsIconId, char* restrictionCondition
) :
    m_nGoodsPrimaryId(goodsPrimaryId),
    m_nIncludedItemCount(0),
    m_nGoodsIconId(goodsIconId) {
    CopyTargetText(m_sGoodsName, goodsName);
    CopyTargetText(m_sGoodsDesc, goodsDesc);
    CopyNarrowText(m_GoodsRestrictionCondition, restrictionCondition);
}

CNewGoods::~CNewGoods() = default;

void CNewGoods::SetGoodsIconId(int goodsIconId) {
    L2_GUARD_FUNCTION();
    m_nGoodsIconId = goodsIconId;
}

void CNewGoods::SetGoodsName(wchar_t* goodsName) {
    L2_GUARD_FUNCTION();
    CopyTargetText(m_sGoodsName, goodsName);
}

void CNewGoods::SetGoodsDesc(wchar_t* goodsDesc) {
    L2_GUARD_FUNCTION();
    CopyTargetText(m_sGoodsDesc, goodsDesc);
}

void CNewGoods::SetGoodsRestrictionCondition(char* restrictionCondition) {
    L2_GUARD_FUNCTION();
    CopyNarrowText(m_GoodsRestrictionCondition, restrictionCondition);
}

void CNewGoods::AddIncludededItem(int itemPrimaryId, int itemClassId) {
    L2_GUARD_FUNCTION();
    if (m_mapIncludedItem.find(itemPrimaryId) == m_mapIncludedItem.end()) {
        m_mapIncludedItem.emplace(itemPrimaryId, ItemIncludedInNewGoods{itemPrimaryId, itemClassId});
        ++m_nIncludedItemCount;
    }
}

void CNewGoods::ChangeIncludedItem(int itemPrimaryId, int itemClassId) {
    L2_GUARD_FUNCTION();
    const auto found = m_mapIncludedItem.find(itemPrimaryId);
    if (found == m_mapIncludedItem.end()) {
        m_mapIncludedItem.emplace(itemPrimaryId, ItemIncludedInNewGoods{itemPrimaryId, itemClassId});
        ++m_nIncludedItemCount;
    } else {
        found->second.m_nItemClassId = itemClassId;
    }
}

int CNewGoods::GetIncludedItemClassId(int itemPrimaryId) {
    L2_GUARD_FUNCTION();
    const auto found = m_mapIncludedItem.find(itemPrimaryId);
    return found == m_mapIncludedItem.end() ? -1 : found->second.m_nItemClassId;
}

CUserNewGoods::CUserNewGoods() : m_nGoodsUid(-1), m_nGoodsPrimaryId(-1), m_nIncludedItemCount(-1) {}

void CUserNewGoods::Init(std::int64_t goodsUid, int goodsPrimaryId) {
    L2_GUARD_FUNCTION();
    m_nGoodsUid = goodsUid;
    m_nGoodsPrimaryId = goodsPrimaryId;
}

void CUserNewGoods::AddIncludededItem(int itemPrimaryId, int itemClassId, int itemAmount, FileTime deliveryDate) {
    L2_GUARD_FUNCTION();
    if (m_mapIncludedItem.find(itemPrimaryId) == m_mapIncludedItem.end()) {
        m_mapIncludedItem.emplace(
            itemPrimaryId, UserItemIncludedInNewGoods{itemPrimaryId, itemClassId, itemAmount, deliveryDate}
        );
        ++m_nIncludedItemCount;
    }
}

CGoodsInventory::CGoodsInventory() {
    m_bConnected = false;
    m_bSendable = false;
    m_nLogTick = 0;
}

CGoodsInventory::~CGoodsInventory() {
    m_hConnectThread = nullptr;
    m_pGoodsInventorySocket = nullptr;
}

void CGoodsInventory::ConnectToGoodsInventoryAgent() {
    L2_GUARD_FUNCTION();
    if (!m_bConnected) {
        runtime::ResumeOwnedThread(m_hConnectThread);
    }
}

void CGoodsInventory::RequestCheckVersion() {
    L2_GUARD_FUNCTION();
    if (m_bConnected && g_GoodsInventorySetting.enable) {
        if (m_pGoodsInventorySocket == nullptr) {
            m_bConnected = false;
        } else {
            m_pGoodsInventorySocket->Send("Cdddd", 0, 0, 0x20, g_nWorldId, 1);
        }
    }
}

void CGoodsInventory::RequestAllGoodsInfo() {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x639)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x639)
        L2_LOG_WARN(
            Log, "Cannot send full goods catalogue request: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return;
    }
    m_pGoodsInventorySocket->Send("Cdd", 0x24, g_nWorldId, 0x20);
}

void CGoodsInventory::SendInquiryResultErrorNoti(int transactionId, int result, std::int64_t goodsUid) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x706)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x706)
        L2_LOG_WARN(
            Log, "Cannot send inquiry-error notification: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return;
    }
    m_pGoodsInventorySocket->Send("CddQ", 0x21, transactionId, result, goodsUid);
}

int CGoodsInventory::CreateTransactionId() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::AddI32(&m_nTransactionId, 1);
}

bool CGoodsInventory::IsDeliveryDateExpired(FileTime deliveryDate) {
    L2_GUARD_FUNCTION();
    const runtime::SystemTime current = runtime::GetLocalTimeNow();
    runtime::SystemTime delivery{};
    static_cast<void>(FileTimeToUtcSystemTime(deliveryDate, delivery));
    const std::int32_t currentTime = runtime::crt::SystemTimeToTime32(current);
    const std::int32_t deliveryTime = runtime::crt::SystemTimeToTime32(delivery);
    return runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(currentTime, deliveryTime), -1800) > 0;
}

std::int64_t CGoodsInventory::GetRemainTimeUntilExpired(FileTime deliveryDate) {
    L2_GUARD_FUNCTION();
    const std::uint64_t delivery = FileTimeBits(deliveryDate);
    const std::uint64_t current = CurrentLocalWallFileTimeBits();
    if (delivery <= current) {
        return 0;
    }
    return static_cast<std::int64_t>((delivery - current) / kFileTimeTicksPerSecond);
}

bool CGoodsInventory::Connect() {
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory] Already connected to IBServer")
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory] Already connected to IBServer")
        L2_LOG_WARN(Log, "Goods-inventory connection attempt skipped: already connected to IBServer.");
        return false;
    }

    const std::uint64_t socket = static_cast<std::uint64_t>(::socket(AF_INET, SOCK_STREAM, 0));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr = g_GoodsInventorySetting.serverAddr;
    address.sin_port = htons(static_cast<std::uint16_t>(g_GoodsInventorySetting.serverPort));
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
            //     L"[Goods Inventory] Can't connect to IBServer")
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory] Can't connect to IBServer")
            // L2_LOG_ERROR(Log,
            //     "Goods-inventory TCP connection to %u.%u.%u.%u:%u failed.",
            //     (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 24U) & 255U,
            //     (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 16U) & 255U,
            //     (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 8U) & 255U,
            //     static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) & 255U,
            //     static_cast<unsigned>(ntohs(address.sin_port)))
            L2_LOG_ERROR(
                Log, "Goods-inventory TCP connection to %u.%u.%u.%u:%u failed: socket error=%d.",
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 24U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 16U) & 255U,
                (static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) >> 8U) & 255U,
                static_cast<unsigned>(ntohl(address.sin_addr.s_addr)) & 255U, static_cast<unsigned>(ntohs(address.sin_port)),
                connectError
            );
            m_nLogTick = runtime::GetTickCount();
        }
        CloseGoodsInventoryNativeSocket(socket);
        return false;
    }

    // Log.Add(LOG_NORMAL, L"[Goods Inventory] Connected to IBServer")
    // L2_LOG_INFO(Log, L"[Goods Inventory] Connected to IBServer")
    L2_LOG_INFO(Log, "Goods-inventory TCP connection to IBServer established; protocol validation is pending.");
    m_pGoodsInventorySocket = new CGoodsInventorySocket(socket);
    m_pGoodsInventorySocket->Initialize();

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, { user->EndGoodsInventoryJob(); });

    Log.Add(
        LOG_DB, u"%d,,,,,,,,,,,%d,%x,%x,%d,,,,,,,,,,,,", 0x51c, 0, static_cast<unsigned int>(socket),
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(m_pGoodsInventorySocket)), 0
    );
    return true;
}

bool CGoodsInventory::Init() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (g_GoodsInventorySetting.enable && !m_bConnected) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[Goods Inventory] Connecting IBServer...")
            // L2_LOG_INFO(Log,
            //     L"[Goods Inventory] Connecting IBServer...")
            L2_LOG_INFO(Log, "Attempting goods-inventory TCP connection to IBServer.");
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

void CGoodsInventory::CreateThread() {
    L2_GUARD_FUNCTION();
    m_hConnectThread = runtime::CreateOwnedThread(GoodsInventoryConnectThread, nullptr, true);
    m_nConnectThread = runtime::OwnedThreadId(m_hConnectThread);
    CThreadManager::SetThread(ThreadID::GOODS_INVENTORY, m_hConnectThread);
}

void CGoodsInventory::AllGoodsInfoClear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (const auto& entry : m_mapNewGoodsInfo) {
        if (entry.second != nullptr) {
            const int referenceCount = L2_RELEASE(entry.second, ObjectRefType::CreateDelete);
            if (referenceCount != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"goods info release error. reference count[%d]",
                //     referenceCount)
                // L2_LOG_ERROR(Log,
                //     L"goods info release error. reference count[%d]",
                //     referenceCount)
                L2_LOG_ERROR(
                    Log,
                    "Goods catalogue cleanup: Release returned nonzero reference count %d; catalogue clearing continues.",
                    referenceCount
                );
            }
        }
    }
    m_mapNewGoodsInfo.clear();
}

CNewGoods* CGoodsInventory::_FindGoodsInfo(int goodsPrimaryId) {
    L2_GUARD_FUNCTION();
    const auto found = m_mapNewGoodsInfo.find(goodsPrimaryId);
    return found == m_mapNewGoodsInfo.end() ? nullptr : found->second;
}

CNewGoods* CGoodsInventory::FindGoodsInfo(int goodsPrimaryId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    return _FindGoodsInfo(goodsPrimaryId);
}

ItemIncludedInNewGoods* CGoodsInventory::FindIncludedItemInGoodsInfo(int goodsPrimaryId, int itemPrimaryId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    CNewGoods* const goods = _FindGoodsInfo(goodsPrimaryId);
    if (goods == nullptr) {
        return nullptr;
    }
    const auto found = goods->m_mapIncludedItem.find(itemPrimaryId);
    return found == goods->m_mapIncludedItem.end() ? nullptr : &found->second;
}

void CGoodsInventory::AddNewGoodsInfo(
    int goodsPrimaryId,
    int itemPrimaryId,
    int itemClassId,
    wchar_t* goodsName,
    wchar_t* goodsDesc,
    int goodsIconId,
    char* restrictionCondition
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    CNewGoods* goods = _FindGoodsInfo(goodsPrimaryId);
    if (goods == nullptr) {
        goods = new CNewGoods(goodsPrimaryId, goodsName, goodsDesc, goodsIconId, restrictionCondition);
        goods->AddIncludededItem(itemPrimaryId, itemClassId);
        m_mapNewGoodsInfo.emplace(goodsPrimaryId, goods);
    } else {
        goods->AddIncludededItem(itemPrimaryId, itemClassId);
    }
}

bool CGoodsInventory::ChangeGoodsInfo(
    int goodsPrimaryId,
    int itemPrimaryId,
    int itemClassId,
    wchar_t* goodsName,
    wchar_t* goodsDesc,
    int goodsIconId,
    char* restrictionCondition
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    CNewGoods* goods = _FindGoodsInfo(goodsPrimaryId);
    if (goods == nullptr) {
        goods = new CNewGoods(goodsPrimaryId, goodsName, goodsDesc, goodsIconId, restrictionCondition);
        goods->AddIncludededItem(itemPrimaryId, itemClassId);
        m_mapNewGoodsInfo.emplace(goodsPrimaryId, goods);
        return true;
    }

    const bool iconChanged = goods->m_nGoodsIconId != goodsIconId;
    if (iconChanged) {
        goods->SetGoodsIconId(goodsIconId);
    }
    const bool nameChanged = !TargetTextEquals(goods->m_sGoodsName, goodsName);
    if (nameChanged) {
        goods->SetGoodsName(goodsName);
    }
    const bool descChanged = !TargetTextEquals(goods->m_sGoodsDesc, goodsDesc);
    if (descChanged) {
        goods->SetGoodsDesc(goodsDesc);
    }
    const bool restrictionChanged = std::strcmp(goods->m_GoodsRestrictionCondition, restrictionCondition) != 0;
    if (restrictionChanged) {
        goods->SetGoodsRestrictionCondition(restrictionCondition);
    }
    const int oldItemClassId = goods->GetIncludedItemClassId(itemPrimaryId);
    const bool itemChanged = oldItemClassId != itemClassId;
    if (itemChanged) {
        goods->ChangeIncludedItem(itemPrimaryId, itemClassId);
    }
    if (iconChanged || nameChanged || descChanged || restrictionChanged || itemChanged) {
        CUserBroadcaster::inst().BroadcastToAllUser("chc", 0xfe, 0x10c, 3);
    }
    return true;
}

void CGoodsInventory::RequestOneGoodsInfo(int userId, int accountId, int goodsPrimaryId) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x64c)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x64c)
        L2_LOG_WARN(
            Log, "Cannot send single-goods info request: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return;
    }
    auto* const job = new CGoodsInventoryJob(userId, 0x25);
    PushGoodsInventoryJob(job);
    m_pGoodsInventorySocket->Send("Cdhdd", 0x25, job->m_nTransactionId, 0x20, accountId, goodsPrimaryId);
}

GoodsInventoryJobResult CGoodsInventory::RequestInquiryGoodsInventory(
    int userId, int accountId, int, bool isUserAction
) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x65f)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x65f)
        L2_LOG_WARN(
            Log, "Cannot send goods inventory query: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return GIJR_SA_SERVER_DISCONNECTED;
    }

    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Invalid User Id(%d)",
        //     L"GoodsInventory.cpp",
        //     0x666,
        //     userId)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Invalid User Id(%d)",
        //     L"GoodsInventory.cpp",
        //     0x666,
        //     userId)
        L2_LOG_WARN(Log, "Cannot request goods inventory: player object ID %d is unavailable.", userId);
        return GIJR_INVALID_JOB;
    }

    L2_GUARD(user->m_PremiumItemLock);
    const std::int16_t premiumCount = static_cast<std::int16_t>(user->m_mapPremiumItem.size());
    if (premiumCount < 25) {
        auto* const job = new CGoodsInventoryJob(userId, 0x1f, isUserAction);
        PushGoodsInventoryJob(job);
        m_pGoodsInventorySocket->Send(
            "CdhcQcdcQ", 0x1f, job->m_nTransactionId, 0x20, 25 - premiumCount, static_cast<std::int64_t>(accountId), 1,
            g_nWorldId, 2, std::int64_t{-1}
        );
        return GIJR_S_OK;
    }

    std::list<InquiryPremiumItemAckPacketInfo> premiumItems;
    _ContainValidPremiumItem(user.get(), 25, premiumItems);
    CPacketHelperEx<20480> packet;
    packet.Assemble("h", 25);
    for (const auto& item : premiumItems) {
        packet.Assemble(
            "cQcSdQ", static_cast<int>(item.isNewGoods), item.warehouseId, static_cast<int>(item.isGift), item.senderName,
            item.itemClassId, item.amount
        );
    }
    SendGoodsInventoryClientResult(user.get(), 1);
    user->GetSocket()->Send("chb", 0xfe, 0x10d, packet.GetLength(), packet.GetPacket());
    user->EndGoodsInventoryJob();
    return GIJR_S_OK;
}

GoodsInventoryJobResult CGoodsInventory::RequestRetrieveGoodsInventory(
    int userId, int accountId, int characterDatabaseId, std::int64_t goodsUid
) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x6b8)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x6b8)
        L2_LOG_WARN(
            Log, "Cannot send goods retrieval request: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return GIJR_SA_SERVER_DISCONNECTED;
    }
    auto* const job = new CGoodsInventoryJob(userId, 0x20, goodsUid);
    PushGoodsInventoryJob(job);
    m_pGoodsInventorySocket->Send(
        "CdQhQdQ", 0x20, job->m_nTransactionId, goodsUid, 0x20, static_cast<std::int64_t>(accountId), g_nWorldId,
        static_cast<std::int64_t>(characterDatabaseId)
    );
    return GIJR_S_OK;
}

GoodsInventoryJobResult CGoodsInventory::RequestInquiryExistGoods(int userId, int accountId, int) {
    L2_GUARD_FUNCTION();
    if (!m_bSendable || !m_bConnected) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x6d2)
        // L2_LOG_INFO(Log,
        //     L"[Goods Inventory][%s][%d] Close connection to SA Server...",
        //     L"GoodsInventory.cpp",
        //     0x6d2)
        L2_LOG_WARN(
            Log, "Cannot send goods existence query: connected=%s, protocol ready=%s.", m_bConnected ? "yes" : "no",
            m_bSendable ? "yes" : "no"
        );
        return GIJR_SA_SERVER_DISCONNECTED;
    }
    auto* const job = new CGoodsInventoryJob(userId, 0x22);
    PushGoodsInventoryJob(job);
    m_pGoodsInventorySocket->Send(
        "CdhQcdcQ", 0x22, job->m_nTransactionId, 0x20, static_cast<std::int64_t>(accountId), 1, g_nWorldId, 2,
        std::int64_t{-1}
    );
    return GIJR_S_OK;
}

bool CGoodsInventory::PushGoodsInventoryJob(CGoodsInventoryJob* job) {
    L2_GUARD_FUNCTION();
    if (job != nullptr) {
        m_GoodsInventoryJobs.Insert(job->m_nTransactionId, job);
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[Goods Inventory][%s][%d] Push null GoodsInventoryJob->",
    //     L"GoodsInventory.cpp",
    //     0x797)
    // L2_LOG_ERROR(Log,
    //     L"[Goods Inventory][%s][%d] Push null GoodsInventoryJob->",
    //     L"GoodsInventory.cpp",
    //     0x797)
    L2_LOG_ERROR(Log, "Cannot register goods-inventory job: job pointer is null.");
    return false;
}

CGoodsInventoryJobSP CGoodsInventory::PopGoodsInventoryJob(int transactionId) {
    L2_GUARD_FUNCTION();
    CGoodsInventoryJobSP job = m_GoodsInventoryJobs.FindObject(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] Cannot find CGoodsInventoeyJob->[%d]",
        //     L"GoodsInventory.cpp",
        //     0x7ae,
        //     transactionId)
        // Noise: all four callers report missing jobs with operation and transaction context.
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] Cannot find CGoodsInventoeyJob->[%d]",
        //     L"GoodsInventory.cpp",
        //     0x7ae,
        //     transactionId)
    } else {
        m_GoodsInventoryJobs.DeleteObject(transactionId);
    }
    return job;
}

bool DummyGoodsInventoryPacket(CGoodsInventorySocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     "DummyPacket called in CGoodsInventorySocket")
    // L2_LOG_ERROR(Log,
    //     "DummyPacket called in CGoodsInventorySocket")
    L2_LOG_ERROR(Log, "Goods-inventory packet reached the reserved packet handler.");
    return false;
}

void SendInquiryGoodsInventoryErrMsgToClient(User* user, int result, bool isUserAction) {
    L2_GUARD_FUNCTION();
    int clientResult = -2;
    switch (result) {
        case -102:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Account Id.",
            //     L"GoodsInventory.cpp",
            //     0x1ba)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Account Id.",
            //     L"GoodsInventory.cpp",
            //     0x1ba)
            L2_LOG_WARN(
                Log, "Goods-inventory inquiry failed: service reported invalid account ID (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -903:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Goods Inventory Info Changed.",
            //     L"GoodsInventory.cpp",
            //     0x1d5)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Goods Inventory Info Changed.",
            //     L"GoodsInventory.cpp",
            //     0x1d5)
            L2_LOG_WARN(
                Log,
                "Goods-inventory inquiry failed: service reported inventory data changed (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -902:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Previous Request On Process.",
            //     L"GoodsInventory.cpp",
            //     0x1cf)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Previous Request On Process.",
            //     L"GoodsInventory.cpp",
            //     0x1cf)
            L2_LOG_WARN(
                Log,
                "Goods-inventory inquiry failed: service reported previous request is still processing (result %d, user "
                "initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            clientResult = -103;
            break;
        case -901:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA Server Busy.",
            //     L"GoodsInventory.cpp",
            //     0x1c9)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA Server Busy.",
            //     L"GoodsInventory.cpp",
            //     0x1c9)
            L2_LOG_WARN(
                Log, "Goods-inventory inquiry failed: service reported SA server is busy (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            clientResult = -101;
            break;
        case -108:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Character DB Id.",
            //     L"GoodsInventory.cpp",
            //     0x1c4)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Character DB Id.",
            //     L"GoodsInventory.cpp",
            //     0x1c4)
            L2_LOG_WARN(
                Log,
                "Goods-inventory inquiry failed: service reported invalid character DB ID (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -104:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Max Goods Count.",
            //     L"GoodsInventory.cpp",
            //     0x1bf)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid Max Goods Count.",
            //     L"GoodsInventory.cpp",
            //     0x1bf)
            L2_LOG_WARN(
                Log,
                "Goods-inventory inquiry failed: service reported invalid maximum goods count (result %d, user "
                "initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -101:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
            //     L"GoodsInventory.cpp",
            //     0x1b5)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
            //     L"GoodsInventory.cpp",
            //     0x1b5)
            L2_LOG_ERROR(
                Log, "Goods-inventory inquiry failed: service reported unknown service code (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -11:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Goods DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1b0)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Goods DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1b0)
            L2_LOG_ERROR(
                Log,
                "Goods-inventory inquiry failed: service reported goods database system error (result %d, user "
                "initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -10:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1ab)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1ab)
            L2_LOG_ERROR(
                Log,
                "Goods-inventory inquiry failed: service reported SA database system error (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        case -1:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1a6)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x1a6)
            L2_LOG_ERROR(
                Log,
                "Goods-inventory inquiry failed: service reported SA server system error (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Unknown Error.",
            //     L"GoodsInventory.cpp",
            //     0x1db)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Unknown Error.",
            //     L"GoodsInventory.cpp",
            //     0x1db)
            L2_LOG_ERROR(
                Log, "Goods-inventory inquiry failed: service reported unrecognized failure (result %d, user initiated=%s).",
                result, isUserAction ? "yes" : "no"
            );
            break;
    }
    if (!isUserAction) {
        clientResult = -7;
    }
    SendGoodsInventoryClientResult(user, clientResult);
}

void SendRetrieveGoodsErrMsgToClient(User* user, int result) {
    L2_GUARD_FUNCTION();
    switch (result) {
        case -905:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Already Retrieved Goods.",
            //     L"GoodsInventory.cpp",
            //     0x3b9)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Already Retrieved Goods.",
            //     L"GoodsInventory.cpp",
            //     0x3b9)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported goods already retrieved (result %d).", result);
            SendGoodsInventoryClientResult(user, -106);
            return;
        case -904:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Already Refunded Goods.",
            //     L"GoodsInventory.cpp",
            //     0x3b3)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Already Refunded Goods.",
            //     L"GoodsInventory.cpp",
            //     0x3b3)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported goods already refunded (result %d).", result);
            SendGoodsInventoryClientResult(user, -105);
            return;
        case -901:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA Server Busy.",
            //     L"GoodsInventory.cpp",
            //     0x3ad)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA Server Busy.",
            //     L"GoodsInventory.cpp",
            //     0x3ad)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported SA server is busy (result %d).", result);
            SendGoodsInventoryClientResult(user, -102);
            return;
        case -111:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Need For Restriction Check.",
            //     L"GoodsInventory.cpp",
            //     0x3a2)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Need For Restriction Check.",
            //     L"GoodsInventory.cpp",
            //     0x3a2)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported restriction check required (result %d).", result);
            return;
        case -113:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Need For Character Id Check.",
            //     L"GoodsInventory.cpp",
            //     0x3aa)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Need For Character Id Check.",
            //     L"GoodsInventory.cpp",
            //     0x3aa)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported character ID check required (result %d).", result);
            return;
        case -112:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Need For World Id Check.",
            //     L"GoodsInventory.cpp",
            //     0x3a6)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Need For World Id Check.",
            //     L"GoodsInventory.cpp",
            //     0x3a6)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported world ID check required (result %d).", result);
            return;
        case -102:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Invalid Account Id.",
            //     L"GoodsInventory.cpp",
            //     0x39e)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Invalid Account Id.",
            //     L"GoodsInventory.cpp",
            //     0x39e)
            L2_LOG_WARN(Log, "Goods retrieval failed: service reported invalid account ID (result %d).", result);
            return;
        case -101:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
            //     L"GoodsInventory.cpp",
            //     0x39b)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
            //     L"GoodsInventory.cpp",
            //     0x39b)
            L2_LOG_ERROR(Log, "Goods retrieval failed: service reported unknown service code (result %d).", result);
            return;
        case -10:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x398)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x398)
            L2_LOG_ERROR(Log, "Goods retrieval failed: service reported SA database system error (result %d).", result);
            return;
        case -1:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x394)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
            //     L"GoodsInventory.cpp",
            //     0x394)
            L2_LOG_ERROR(Log, "Goods retrieval failed: service reported SA server system error (result %d).", result);
            return;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Unknown Error.",
            //     L"GoodsInventory.cpp",
            //     0x3bf)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Unknown Error.",
            //     L"GoodsInventory.cpp",
            //     0x3bf)
            L2_LOG_ERROR(Log, "Goods retrieval failed: service reported unrecognized failure (result %d).", result);
            return;
    }
}

bool IBVersionPacket(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int version = 0;
    Disassemble(packet + 1, "d", &version);
    // Log.Add(LOG_NORMAL, L"[Goods Inventory] IBServer Ver: %d", version)
    // L2_LOG_INFO(Log, L"[Goods Inventory] IBServer Ver: %d", version)
    L2_LOG_INFO(Log, "Received goods-inventory protocol version %d; expected %d.", version, 0x132b78d);
    if (version == 0x132b78d) {
        g_GoodsInventory.m_bSendable = true;
        g_GoodsInventory.RequestAllGoodsInfo();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory] SA Server Version is not match with Game Server(%d)",
        //     0x132b78d)
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory] SA Server Version is not match with Game Server(%d)",
        //     0x132b78d)
        L2_LOG_ERROR(
            Log,
            "Goods-inventory protocol version mismatch: received %d, expected %d; existing send-readiness flag is "
            "unchanged.",
            version, 0x132b78d
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory] Goods Inventory Function will not work !")
        // Noise: the mismatch is reported above. This branch does not reset readiness.
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory] Goods Inventory Function will not work !")
    }
    return false;
}

void _ContainValidPremiumItem(User* user, int itemCount, std::list<InquiryPremiumItemAckPacketInfo>& premiumItems) {
    L2_GUARD_FUNCTION();
    auto item = user->m_mapPremiumItem.begin();
    for (int index = 0; index < itemCount; ++index, ++item) {
        CPremiumItem* const premiumItem = item->second;
        InquiryPremiumItemAckPacketInfo info{};
        // info.isNewGoods = 0.
        info.isNewGoods = GT_PREMIUM_ITEM;
        info.warehouseId = premiumItem->m_nWarehouseNo;
        info.isGift = static_cast<char>(premiumItem->m_bIsGift);
        CopyTargetText(info.senderName, premiumItem->m_sBuyerCharName.data());
        info.itemClassId = premiumItem->m_nItemClassId;
        info.amount = premiumItem->m_nItemAmount;
        premiumItems.push_back(info);
    }
}

bool IBRetrieveGoodsFromInventory(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    packet = Disassemble(packet + 1, "d", &transactionId);
    CGoodsInventoryJobSP job = g_GoodsInventory.PopGoodsInventoryJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Cannot Get Transaction(%d).",
        //     L"GoodsInventory.cpp",
        //     0x3d1,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Cannot Get Transaction(%d).",
        //     L"GoodsInventory.cpp",
        //     0x3d1,
        //     transactionId)
        L2_LOG_WARN(Log, "Cannot process goods retrieval response: job for transaction %d was not found.", transactionId);
        return false;
    }
    CSPointer<User> user = g_UserMap.FindObject(job->m_nUserId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid User Id(%d).",
        //     L"GoodsInventory.cpp",
        //     0x3d8,
        //     job->m_nUserId)
        // L2_LOG_ERROR(Log,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Invalid User Id(%d).",
        //     L"GoodsInventory.cpp",
        //     0x3d8,
        //     job->m_nUserId)
        L2_LOG_WARN(
            Log, "Cannot process goods retrieval response: player object ID %d is unavailable (transaction %d).",
            job->m_nUserId, transactionId
        );
        return false;
    }
    int result = 0;
    Disassemble(packet, "d", &result);
    if (result == 0) {
        user->RetrieveGoods(job->m_nGoodsUid);
        user->EndGoodsInventoryJob();
        user->RequestInquiryGoodsInventory(true);
    } else {
        SendRetrieveGoodsErrMsgToClient(user.get(), result);
        user->EndGoodsInventoryJob();
    }
    return false;
}

bool IBInquiryExistGoodsAck(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    packet = Disassemble(packet + 1, "d", &transactionId);
    CGoodsInventoryJobSP job = g_GoodsInventory.PopGoodsInventoryJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Cannot Get Transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0x401,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[GoodsInventory][%s][%d] IBInquiryGoodsInventoryAckPacket : Cannot Get Transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0x401,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Cannot process goods existence query response: job for transaction %d was not found.", transactionId
        );
        return false;
    }
    CSPointer<User> user = g_UserMap.FindObject(job->m_nUserId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GoodsInventory][%s][%d] IBInquiryExistGoodsAck : Invalid User Id(%d).",
        //     L"GoodsInventory.cpp",
        //     0x408,
        //     job->m_nUserId)
        // L2_LOG_ERROR(Log,
        //     L"[GoodsInventory][%s][%d] IBInquiryExistGoodsAck : Invalid User Id(%d).",
        //     L"GoodsInventory.cpp",
        //     0x408,
        //     job->m_nUserId)
        L2_LOG_WARN(
            Log, "Cannot process goods existence response: player object ID %d is unavailable (transaction %d).",
            job->m_nUserId, transactionId
        );
        return false;
    }

    int result = 0;
    std::int16_t errorLength = 0;
    Disassemble(packet, "dh", &result, &errorLength);
    if (result == 0) {
        L2_GUARD(user->m_PremiumItemLock);
        errorLength = static_cast<std::int16_t>(errorLength + user->m_mapPremiumItem.size());
        if (errorLength > 0) {
            user->GetSocket()->Send("chd", 0xfe, 0x10c, 1);
        }
        user->EndGoodsInventoryJob();
    } else {
        // const wchar_t* message = nullptr
        const char* message = nullptr;
        // int line = 0
        [[maybe_unused]] int line = 0;
        switch (result) {
            case -109:
                // message = L"[Goods Inventory][%s][%d] SA Server Too Busy."
                message = "SA server is busy";
                line = 0x426;
                break;
            case -108:
                // message = L"[Goods Inventory][%s][%d] Invalid Character Id."
                message = "invalid character ID";
                line = 0x423;
                break;
            case -102:
                // message = L"[Goods Inventory][%s][%d] Invalid Account Id."
                message = "invalid account ID";
                line = 0x420;
                break;
            case -101:
                // message = L"[Goods Inventory][%s][%d] Unknown Service Code."
                message = "unknown service code";
                line = 0x41d;
                break;
            case -10:
                // message = L"[Goods Inventory][%s][%d] SA DB Native System Error."
                message = "SA database system error";
                line = 0x41a;
                break;
            case -1:
                // message = L"[Goods Inventory][%s][%d] SA Server Native System Error."
                message = "SA server system error";
                line = 0x417;
                break;
            default:
                // message = L"[Goods Inventory][%s][%d] Unknown Error."
                message = "unrecognized failure";
                line = 0x429;
                break;
        }
        // Log.Add(LOG_ERROR, message, L"GoodsInventory.cpp", line)
        // L2_LOG_ERROR(Log, message, L"GoodsInventory.cpp", line)
        L2_LOG_ERROR(
            Log, "Goods existence request failed for transaction %d: service reported %s (result %d).", transactionId,
            message, result
        );
        user->EndGoodsInventoryJob();
    }
    return false;
}

bool IBGoodsInventoryChangedNoti(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::int16_t count = 0;
    packet = Disassemble(packet + 1, "h", &count);
    for (int index = 0; index < count; ++index) {
        char status = 0;
        std::int16_t serviceCode = 0;
        std::int64_t accountId = 0;
        packet = Disassemble(packet, "chQ", &status, &serviceCode, &accountId);
        if (serviceCode == 0x20) {
            CSPointer<User> user = g_UserAccountDBIDMap.FindObject(static_cast<std::uint32_t>(accountId));
            if (user) {
                user->GetSocket()->Send("chd", 0xfe, 0x10c, static_cast<int>(status));
                user->RequestInquiryGoodsInventory(false);
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Invalid Service Code. (Lineage2 Is 32, but %d)",
            //     L"GoodsInventory.cpp",
            //     0x165,
            //     static_cast<int>(serviceCode))
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Invalid Service Code. (Lineage2 Is 32, but %d)",
            //     L"GoodsInventory.cpp",
            //     0x165,
            //     static_cast<int>(serviceCode))
            L2_LOG_WARN(
                Log, "Goods-inventory change notification ignored: service code %d is not 32.", static_cast<int>(serviceCode)
            );
        }
    }
    return false;
}

bool IBInquiryAllGoodsInfoAckPacket(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int worldId = 0;
    packet = Disassemble(packet + 1, "d", &worldId);
    if (worldId != g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] Request from [%d], but reply to [%d]",
        //     L"GoodsInventory.cpp",
        //     0x5d,
        //     worldId,
        //     g_nWorldId)
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] Request from [%d], but reply to [%d]",
        //     L"GoodsInventory.cpp",
        //     0x5d,
        //     worldId,
        //     g_nWorldId)
        L2_LOG_ERROR(Log, "Goods catalogue response rejected: received world %d, local world %d.", worldId, g_nWorldId);
        return false;
    }

    int result = 0;
    std::int16_t errorLength = 0;
    packet = Disassemble(packet, "dh", &result, &errorLength);
    if (result == 0) {
        std::uint16_t firstFlag = 0;
        std::uint16_t totalCount = 0;
        int ignored = 0;
        std::int16_t itemCount = 0;
        packet = Disassemble(packet, "hhdh", &firstFlag, &totalCount, &ignored, &itemCount);
        if (firstFlag == 1) {
            g_GoodsInventory.AllGoodsInfoClear();
        }
        for (int index = 0; index < itemCount; ++index) {
            DecodedGoodsInfo info;
            packet = DecodeGoodsInfo(packet, info);
            g_GoodsInventory.AddNewGoodsInfo(
                info.goodsPrimaryId, info.itemPrimaryId, info.itemClassId, reinterpret_cast<wchar_t*>(info.goodsName),
                reinterpret_cast<wchar_t*>(info.goodsDesc), info.goodsIconId, info.restrictionCondition
            );
        }
        if (totalCount == firstFlag) {
            // Log.Add(
            //     LOG_NORMAL,
            //     L"[Goods Inventory] All GoodsData Cached Complete.")
            // L2_LOG_INFO(Log,
            //     L"[Goods Inventory] All GoodsData Cached Complete.")
            L2_LOG_INFO(
                Log,
                "Goods catalogue response processed with matching completion markers (%u); this response declared %d item "
                "entries.",
                static_cast<unsigned>(totalCount), static_cast<int>(itemCount)
            );
        }
        return false;
    }

    if (errorLength < 1) {
        switch (result) {
            case -901:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] SA Server Busy.",
                //     L"GoodsInventory.cpp",
                //     0x79)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] SA Server Busy.",
                //     L"GoodsInventory.cpp",
                //     0x79)
                L2_LOG_WARN(
                    Log, "Goods catalogue request failed: SA server is busy (result %d); retrying the request.", result
                );
                g_GoodsInventory.RequestAllGoodsInfo();
                break;
            case -115:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] Goods Data Empty.",
                //     L"GoodsInventory.cpp",
                //     0x76)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] Goods Data Empty.",
                //     L"GoodsInventory.cpp",
                //     0x76)
                L2_LOG_WARN(Log, "Goods catalogue request failed: service reported empty goods data (result %d).", result);
                break;
            case -114:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
                //     L"GoodsInventory.cpp",
                //     0x73)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] Unknown Service Code.",
                //     L"GoodsInventory.cpp",
                //     0x73)
                L2_LOG_ERROR(
                    Log, "Goods catalogue request failed: service reported an unknown service code (result %d).", result
                );
                break;
            case -11:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
                //     L"GoodsInventory.cpp",
                //     0x70)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] SA DB Native System Error.",
                //     L"GoodsInventory.cpp",
                //     0x70)
                L2_LOG_ERROR(
                    Log, "Goods catalogue request failed: service reported an SA database system error (result %d).", result
                );
                break;
            case -1:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
                //     L"GoodsInventory.cpp",
                //     0x6d)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] SA Server Native System Error.",
                //     L"GoodsInventory.cpp",
                //     0x6d)
                L2_LOG_ERROR(
                    Log, "Goods catalogue request failed: service reported an SA server system error (result %d).", result
                );
                break;
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] Unknown Error.",
                //     L"GoodsInventory.cpp",
                //     0x7d)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] Unknown Error.",
                //     L"GoodsInventory.cpp",
                //     0x7d)
                L2_LOG_ERROR(Log, "Goods catalogue request failed with unrecognized service result %d.", result);
                break;
        }
    } else {
        std::array<char16_t, 6> error{};
        Disassemble(packet, "S", 0x0c, error.data());
        const std::wstring wideError = target_utf16::ToHostCodeUnits(error.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] %s",
        //     L"GoodsInventory.cpp",
        //     0x88,
        //     wideError.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] %s",
        //     L"GoodsInventory.cpp",
        //     0x88,
        //     wideError.c_str())
        L2_LOG_ERROR(
            Log, "Goods catalogue request failed (result %d); service message: %s", result,
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(wideError.c_str())).c_str()
        );
    }
    return false;
}

bool IBInquiryOneGoodsInfoAckPacket(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    packet = Disassemble(packet + 1, "d", &transactionId);
    CGoodsInventoryJobSP job = g_GoodsInventory.PopGoodsInventoryJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] IBInquiryOneGoodsInfoAckPacket : cannot get transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0xe5,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] IBInquiryOneGoodsInfoAckPacket : cannot get transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0xe5,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Cannot process single-goods info query response: job for transaction %d was not found.", transactionId
        );
        return false;
    }

    int result = 0;
    std::int16_t errorLength = 0;
    packet = Disassemble(packet, "dh", &result, &errorLength);
    if (result == 0) {
        DecodedGoodsInfo info;
        DecodeGoodsInfo(packet, info);
        g_GoodsInventory.AddNewGoodsInfo(
            info.goodsPrimaryId, info.itemPrimaryId, info.itemClassId, reinterpret_cast<wchar_t*>(info.goodsName),
            reinterpret_cast<wchar_t*>(info.goodsDesc), info.goodsIconId, info.restrictionCondition
        );
        CSPointer<User> user = g_UserMap.FindObject(job->m_nUserId);
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Cannot find user, user id[%d]",
            //     L"GoodsInventory.cpp",
            //     0x139,
            //     job->m_nUserId)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Cannot find user, user id[%d]",
            //     L"GoodsInventory.cpp",
            //     0x139,
            //     job->m_nUserId)
            L2_LOG_WARN(
                Log,
                "Goods information was cached, but player object ID %d is unavailable for inventory refresh (transaction "
                "%d).",
                job->m_nUserId, transactionId
            );
        } else {
            user->RequestInquiryGoodsInventory(false);
        }
        return false;
    }

    if (errorLength < 1) {
        // const wchar_t* message = nullptr
        const char* message = nullptr;
        // int line = 0
        [[maybe_unused]] int line = 0;
        switch (result) {
            case -901:
                // message = L"[Goods Inventory][%s][%d] SA Server Too Busy."
                message = "SA server is busy";
                line = 0x101;
                break;
            case -115:
                // message = L"[Goods Inventory][%s][%d] Goods Data Empty."
                message = "empty goods data";
                line = 0xfe;
                break;
            case -114:
                // message = L"[Goods Inventory][%s][%d] Unknown Service Code."
                message = "unknown service code";
                line = 0xfb;
                break;
            case -11:
                // message = L"[Goods Inventory][%s][%d] SA DB Native System Error."
                message = "SA database system error";
                line = 0xf8;
                break;
            case -1:
                // message = L"[Goods Inventory][%s][%d] SA Server Native System Error."
                message = "SA server system error";
                line = 0xf5;
                break;
            default:
                // message = L"[Goods Inventory][%s][%d] Unknown Error."
                message = "unrecognized failure";
                line = 0x104;
                break;
        }
        // Log.Add(LOG_ERROR, message, L"GoodsInventory.cpp", line)
        // L2_LOG_ERROR(Log, message, L"GoodsInventory.cpp", line)
        L2_LOG_ERROR(
            Log, "Single-goods information request failed for transaction %d: service reported %s (result %d).",
            transactionId, message, result
        );
    } else {
        std::array<char16_t, 6> error{};
        Disassemble(packet, "S", 0x0c, error.data());
        const std::wstring wideError = target_utf16::ToHostCodeUnits(error.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] %s",
        //     L"GoodsInventory.cpp",
        //     0x10f,
        //     wideError.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] %s",
        //     L"GoodsInventory.cpp",
        //     0x10f,
        //     wideError.c_str())
        L2_LOG_ERROR(
            Log, "Goods single-goods information request failed (result %d); service message: %s", result,
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(wideError.c_str())).c_str()
        );
    }
    return false;
}

bool IBGoodsInfoChangedNoti(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    DecodedGoodsInfo info;
    DecodeGoodsInfo(packet + 1, info);
    g_GoodsInventory.ChangeGoodsInfo(
        info.goodsPrimaryId, info.itemPrimaryId, info.itemClassId, reinterpret_cast<wchar_t*>(info.goodsName),
        reinterpret_cast<wchar_t*>(info.goodsDesc), info.goodsIconId, info.restrictionCondition
    );
    return false;
}

void ContainValidGoods(
    int transactionId,
    User* user,
    std::int16_t& goodsCount,
    std::list<InquiryGoodsAckPacketInfo>& goods,
    const std::uint8_t* packet
) {
    L2_GUARD_FUNCTION();
    const std::int16_t originalCount = goodsCount;
    std::int16_t validCount = originalCount;
    for (int goodsIndex = 0; goodsIndex < originalCount; ++goodsIndex) {
        std::int64_t goodsUid = 0;
        int goodsPrimaryId = 0;
        packet = Disassemble(packet, "Qd", &goodsUid, &goodsPrimaryId);

        CUserNewGoods userGoods;
        CNewGoods* const goodsInfo = g_GoodsInventory.FindGoodsInfo(goodsPrimaryId);
        bool valid = goodsInfo != nullptr;
        if (valid) {
            userGoods.Init(goodsUid, goodsPrimaryId);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] Goods[%d] Info Search Fail.",
            //     L"GoodsInventory.cpp",
            //     0x22a,
            //     goodsPrimaryId)
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] Goods[%d] Info Search Fail.",
            //     L"GoodsInventory.cpp",
            //     0x22a,
            //     goodsPrimaryId)
            L2_LOG_ERROR(
                Log, "Goods inventory entry cannot be included: goods definition %d was not found.", goodsPrimaryId
            );
        }

        char refundableFlag = 0;
        char refundType = 0;
        FileTime refundExpirationDate{};
        FileTime deliveryDate{};
        std::int16_t senderInfoLength = 0;
        packet = Disassemble(
            packet, "ccQQh", &refundableFlag, &refundType, &refundExpirationDate, &deliveryDate, &senderInfoLength
        );

        std::uint16_t senderNameLength = 0;
        std::array<char16_t, 51> senderName{};
        std::uint16_t messageLength = 0;
        std::array<char16_t, 81> sendMessage{};
        if (senderInfoLength > 0) {
            if (senderInfoLength < 8) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] Warning!! Invalid Sender Info Length.",
                //     L"GoodsInventory.cpp",
                //     0x243)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] Warning!! Invalid Sender Info Length.",
                //     L"GoodsInventory.cpp",
                //     0x243)
                L2_LOG_ERROR(
                    Log, "Goods %d sender information length %d is below eight bytes; decoding continues.", goodsPrimaryId,
                    static_cast<int>(senderInfoLength)
                );
            }
            packet = Disassemble(
                packet, "hShS", &senderNameLength, 0x66, senderName.data(), &messageLength, 0xa2, sendMessage.data()
            );
            if (senderNameLength > 0x66) {
                g_GoodsInventory.SendInquiryResultErrorNoti(transactionId, -218, goodsUid);
            }
            if (messageLength > 0xa2) {
                g_GoodsInventory.SendInquiryResultErrorNoti(transactionId, -219, goodsUid);
            }
        }

        int worldId = -1;
        std::int64_t characterId = -1;
        packet = Disassemble(packet, "dQ", &worldId, &characterId);
        if (worldId != -1 && worldId != g_nWorldId) {
            g_GoodsInventory.SendInquiryResultErrorNoti(transactionId, -207, goodsUid);
        }
        if (characterId != -1 && characterId != static_cast<std::int64_t>(user->GetDBID())) {
            g_GoodsInventory.SendInquiryResultErrorNoti(transactionId, -210, goodsUid);
        }

        InquiryGoodsAckPacketInfo ack{};
        if (valid && goodsInfo != nullptr) {
            // ack.isNewGoods = 1.
            ack.isNewGoods = GT_GOODS;
            ack.goodsUid = goodsUid;
            ack.goodsIconId = goodsInfo->m_nGoodsIconId;
            CopyTargetText(ack.goodsName, goodsInfo->m_sGoodsName);
            CopyTargetText(ack.goodsDesc, goodsInfo->m_sGoodsDesc);
            ack.isGift = static_cast<char>(senderInfoLength > 0);
            CopyTargetText(ack.senderName, senderName.data());
            CopyTargetText(ack.sendMessage, sendMessage.data());

            ack.refundExpirationDate = 0;
            // ack.refundable = 0.
            ack.refundable = GRT_UNKNOWN;
            if (senderInfoLength < 1) {
                if (refundableFlag == 1) {
                    if (refundType == 1) {
                        // ack.refundable = 2.
                        ack.refundable = GRT_CAN_REFUND;
                        ack.refundExpirationDate = g_GoodsInventory.GetRemainTimeUntilExpired(refundExpirationDate);
                    } else if (refundType == 2) {
                        // ack.refundable = 3.
                        ack.refundable = GRT_CAN_REFUND_ALWAYS;
                    }
                } else if (refundableFlag == 0) {
                    // ack.refundable = 1.
                    ack.refundable = GRT_CANNOT_REFUND;
                }
                // if (ack.refundable == 0) {
                if (ack.refundable == GRT_UNKNOWN) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[Goods Inventory][%s][%d] Unknown Refundable Info.",
                    //     L"GoodsInventory.cpp",
                    //     0x2a4)
                    // L2_LOG_ERROR(Log,
                    //     L"[Goods Inventory][%s][%d] Unknown Refundable Info.",
                    //     L"GoodsInventory.cpp",
                    //     0x2a4)
                    L2_LOG_ERROR(
                        Log, "Goods %d has unsupported refund flag %d and type %d; refund status remains unknown.",
                        goodsPrimaryId, static_cast<int>(refundableFlag), static_cast<int>(refundType)
                    );
                }
            } else {
                if (refundableFlag == 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[Goods Inventory][%s][%d] Warning!! SA Server Send Invalid Goods Info. (IsGift : 1 && Refundable
                    //     : 1)", L"GoodsInventory.cpp", 0x287)
                    // L2_LOG_ERROR(Log,
                    //     L"[Goods Inventory][%s][%d] Warning!! SA Server Send Invalid Goods Info. (IsGift : 1 && Refundable
                    //     : 1)", L"GoodsInventory.cpp", 0x287)
                    L2_LOG_ERROR(
                        Log,
                        "Gift goods %d were marked refundable by the service; client refund status will be non-refundable.",
                        goodsPrimaryId
                    );
                }
                // ack.refundable = 1.
                ack.refundable = GRT_CANNOT_REFUND;
            }
        }

        std::int16_t includedItemCount = 0;
        packet = Disassemble(packet, "h", &includedItemCount);
        if (valid) {
            ack.includedItemCount = includedItemCount;
        }
        for (int itemIndex = 0; itemIndex < includedItemCount; ++itemIndex) {
            int itemPrimaryId = 0;
            int itemAmount = 0;
            packet = Disassemble(packet, "dd", &itemPrimaryId, &itemAmount);
            if (valid) {
                ItemIncludedInNewGoods* const included =
                    g_GoodsInventory.FindIncludedItemInGoodsInfo(goodsPrimaryId, itemPrimaryId);
                if (included == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[Goods Inventory][%s][%d] Can Not Find Item[%d] In Goods[%d] Info.",
                    //     L"GoodsInventory.cpp",
                    //     0x2bd,
                    //     itemPrimaryId,
                    //     goodsPrimaryId)
                    // L2_LOG_ERROR(Log,
                    //     L"[Goods Inventory][%s][%d] Can Not Find Item[%d] In Goods[%d] Info.",
                    //     L"GoodsInventory.cpp",
                    //     0x2bd,
                    //     itemPrimaryId,
                    //     goodsPrimaryId)
                    L2_LOG_ERROR(
                        Log,
                        "Item definition %d is missing from goods %d; excluding the goods entry and requesting information.",
                        itemPrimaryId, goodsPrimaryId
                    );
                    g_GoodsInventory.RequestOneGoodsInfo(user->GetServerId(), goodsPrimaryId, itemPrimaryId);
                    valid = false;
                } else {
                    ack.includedItems.push_back(InquiryItemInGoods{included->m_nItemClassId, itemAmount});
                    if (userGoods.m_nGoodsUid > 0) {
                        userGoods.AddIncludededItem(
                            itemPrimaryId, goodsInfo->GetIncludedItemClassId(itemPrimaryId), itemAmount, deliveryDate
                        );
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[Goods Inventory][%s][%d] Can Not Find Item[%d] In Goods[%d] Info.",
                //     L"GoodsInventory.cpp",
                //     0x2d6,
                //     itemPrimaryId,
                //     goodsPrimaryId)
                // L2_LOG_ERROR(Log,
                //     L"[Goods Inventory][%s][%d] Can Not Find Item[%d] In Goods[%d] Info.",
                //     L"GoodsInventory.cpp",
                //     0x2d6,
                //     itemPrimaryId,
                //     goodsPrimaryId)
                L2_LOG_ERROR(
                    Log, "Item %d belongs to invalid goods entry %d; skipping inclusion and requesting information.",
                    itemPrimaryId, goodsPrimaryId
                );
                g_GoodsInventory.RequestOneGoodsInfo(user->GetServerId(), goodsPrimaryId, itemPrimaryId);
            }
        }

        if (valid) {
            goods.push_back(ack);
            user->AddNewGoodsInfo(userGoods);
        } else if (validCount > 0) {
            --validCount;
        }
    }
    goodsCount = validCount;
}

bool IBInquiryGoodsInventoryAckPacket(CGoodsInventorySocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int transactionId = 0;
    packet = Disassemble(packet + 1, "d", &transactionId);
    CGoodsInventoryJobSP job = g_GoodsInventory.PopGoodsInventoryJob(transactionId);
    if (!job) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : cannot get transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0x314,
        //     transactionId)
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] IBInquiryGoodsInventoryAckPacket : cannot get transaction(%d)",
        //     L"GoodsInventory.cpp",
        //     0x314,
        //     transactionId)
        L2_LOG_WARN(
            Log, "Cannot process goods inventory query response: job for transaction %d was not found.", transactionId
        );
        return false;
    }
    CSPointer<User> user = g_UserMap.FindObject(job->m_nUserId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[Goods Inventory][%s][%d] Cannot find user, user id[%d]",
        //     L"GoodsInventory.cpp",
        //     0x31b,
        //     job->m_nUserId)
        // L2_LOG_ERROR(Log,
        //     L"[Goods Inventory][%s][%d] Cannot find user, user id[%d]",
        //     L"GoodsInventory.cpp",
        //     0x31b,
        //     job->m_nUserId)
        L2_LOG_WARN(
            Log, "Cannot process goods inventory response: player object ID %d is unavailable (transaction %d).",
            job->m_nUserId, transactionId
        );
        return false;
    }

    int result = 0;
    std::int16_t errorLength = 0;
    packet = Disassemble(packet, "dh", &result, &errorLength);
    if (result != 0) {
        if (errorLength < 1) {
            SendInquiryGoodsInventoryErrMsgToClient(user.get(), result, job->m_bIsUserAction);
        } else {
            std::array<char16_t, 6> error{};
            Disassemble(packet, "S", 0x0c, error.data());
            const std::wstring wideError = target_utf16::ToHostCodeUnits(error.data());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[Goods Inventory][%s][%d] %s",
            //     L"GoodsInventory.cpp",
            //     0x333,
            //     wideError.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[Goods Inventory][%s][%d] %s",
            //     L"GoodsInventory.cpp",
            //     0x333,
            //     wideError.c_str())
            L2_LOG_ERROR(
                Log, "Goods inventory request failed (result %d); service message: %s", result,
                Utf16ToUtf8(target_utf16::FromHostCodeUnits(wideError.c_str())).c_str()
            );
            if (job->m_bIsUserAction) {
                SendGoodsInventoryClientResult(user.get(), -2);
            }
        }
        user->EndGoodsInventoryJob();
        return false;
    }

    std::int16_t firstFlag = 0;
    std::int16_t totalCount = 0;
    int ignored = 0;
    std::int16_t goodsCount = 0;
    packet = Disassemble(packet, "hhdh", &firstFlag, &totalCount, &ignored, &goodsCount);
    if (goodsCount > 25) {
        g_GoodsInventory.SendInquiryResultErrorNoti(transactionId, -217, -1);
    }

    std::list<InquiryPremiumItemAckPacketInfo> premiumItems;
    std::list<InquiryGoodsAckPacketInfo> goods;
    CPacketHelperEx<20480> reply;

    L2_GUARD(user->m_PremiumItemLock);
    const int premiumCount = std::min(static_cast<int>(user->m_mapPremiumItem.size()), 25);
    _ContainValidPremiumItem(user.get(), premiumCount, premiumItems);
    ContainValidGoods(transactionId, user.get(), goodsCount, goods, packet);

    reply.Assemble("h", premiumCount + goodsCount);
    for (const auto& premium : premiumItems) {
        reply.Assemble(
            "cQcSdQ", static_cast<int>(premium.isNewGoods), premium.warehouseId, static_cast<int>(premium.isGift),
            premium.senderName, premium.itemClassId, premium.amount
        );
    }
    for (const auto& item : goods) {
        const std::int64_t remaining = (std::int64_t{0x9a7} - static_cast<std::int64_t>(item.includedItems.size())) * 8;
        if (static_cast<std::uint64_t>(remaining) <= static_cast<std::uint64_t>(reply.GetLength())) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] packet overflow, size[%d]",
            //     L"GoodsInventory.cpp",
            //     0x36d,
            //     reply.GetLength())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] packet overflow, size[%d]",
            //     L"GoodsInventory.cpp",
            //     0x36d,
            //     reply.GetLength())
            L2_LOG_ERROR(
                Log, "Goods inventory reply reached its size guard at %d bytes; sending the partial reply.",
                reply.GetLength()
            );
            break;
        }
        reply.Assemble(
            "cQdSSQccSSh", static_cast<int>(item.isNewGoods), item.goodsUid, item.goodsIconId, item.goodsName,
            item.goodsDesc, item.refundExpirationDate, static_cast<int>(item.refundable), static_cast<int>(item.isGift),
            item.senderName, item.sendMessage, item.includedItemCount
        );
        for (const auto& included : item.includedItems) {
            reply.Assemble("dd", included.classId, included.quantity);
        }
    }

    SendGoodsInventoryClientResult(user.get(), 1);
    user->GetSocket()->Send("chb", 0xfe, 0x10d, reply.GetLength(), reply.GetPacket());
    user->EndGoodsInventoryJob();
    return false;
}
