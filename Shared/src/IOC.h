#pragma once

#include "Shared/src/IOCPacket.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/SourceFile.h"
#include "Shared/src/Runtime/SourceLog.h"
#include "ProcessRuntime.h"
#include "Runtime.h"
#include "Shared/src/Obj.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <source_location>

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <netinet/in.h>
#endif

// Global Shared/ioc.cpp entry points recovered from the target. The PDB
// records these without a namespace, and the executable confirms the packet
// format behavior.
int GetIOThreadCount();

// Shared/ioc.cpp lifecycle flag (current image 0x019eead0). It is not the
// exception subsystem's owned ExceptionTerminating state.
extern int g_bTerminating;

// Exact PDB name for the interlocked counter surrounding each completion
// wait. The current image stores it at 0x019eeafc.
extern std::atomic<std::int32_t> g_nRunningThread;

class IOThreadCallback;

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
// Original recovery note before the operator logging review:
// Current CacheD 0x004e5974 and L2Server 0x007cc07c both format into a
// 512-byte local buffer and publish nothing. CIOServer uses
// this exact sink for native create/accept diagnostics. It is distinct from
// CLog-backed errors.
void AddLog(char* format, ...);
// Portable CIOServer diagnostics now use CLog. This no-op definition remains.
#endif

#if defined(L2_BUILD_CACHED)
void IOThread_common(void* argument);
std::uint32_t IOThread(void* argument);
int CreateIOThread(int threadCount);
#endif

std::uint32_t IOThread2(void* argument);
bool CreateIOThread2(IOThreadCallback* callback);


// Cross-platform stand-in for the Win32 _OVERLAPPED type in the recovered
// virtual callback signature. CIOCPMessage::Post always queues nullptr.
struct PortableOverlapped {};

class CIOObject;

namespace portable {

// Cross-platform completion-port transport. This is a free
// adapter instead of a method on the target CIOObject surface.
bool PostIOCompletion(CIOObject* object, std::uint32_t transferred, PortableOverlapped* overlapped);
bool PostIOCompletionResult(
    CIOObject* object, int success, std::uint32_t transferred, PortableOverlapped* overlapped, int socketError
);
SocketWriteSubmission SubmitSocketWrite(
    CIOObject* object, const std::uint8_t* data, int length, PortableOverlapped* overlapped
);

#if defined(L2_BUILD_CACHED)
// Cooperative host teardown for CacheD's two completion-port transports.
// Cached.exe has process-lifetime workers and therefore no source-level stop
// method. This adapter is outside the recovered target classes.
void StopCacheDIOThreadRuntime();
#endif

} // namespace portable

// Current globals g_ObjectInTimer (0x019eeb60) and
// g_ObjectInShortTime (0x019eeb64). The target uses interlocked updates for
// the timer count, std::atomic preserves that contract on every host.
extern std::atomic<std::int32_t> g_ObjectInTimer;
extern std::atomic<std::int32_t> g_ObjectInShortTime;

// Exact free functions used by L2Exception::PrintIOBuffer. They sum the
// recovered process-specific per-thread queue generation in the same way as
// each image.
int GetIOBufferPoolCount();
int GetIOBufferPoolTotalCount();
int GetIOBufferPoolThreadCount(int threadId);
int GetIOBufferPoolThreadTotalCount(int threadId);
int GetCurrentThreadRemainIOBufferPoolCount();
void InitIOThread();
void InitializeIOBufferPool();
void InitializeCurrentIOBufferPool(int count);
int GetSocketLastError();
void SetSocketLastError(int error);
bool IsExpectedSocketCloseError(int error);
// Portable diagnostics stay out of this header's CLog include cycle.
namespace portable {
void ReportSocketReadFailure(std::uint64_t socket, int error);
void ReportSocketFailureDiagnostics(std::uint64_t socket, int error, std::int32_t pendingBytes);
void ReportSocketPeerClose(std::uint64_t socket);
void ReportSocketReadArmFailure(std::uint64_t socket, int error, std::uint32_t retainedBytes, std::size_t capacity);
} // namespace portable
void LogIOError(const char* format, ...);
void LogIOError(const wchar_t* format, ...);
void LogIOLocalError(const wchar_t* format, ...);

namespace runtime::logging {
// The existing product adapters reach CLog::AddV, which consumes this source
// at entry. Keep CLog out of ioc.h's include cycle. Argument evaluation occurs
// before the scope, and the original error destination/formatting stays intact.
template <class Char, class... Args>
inline void LogIOErrorAt(const char* source, const Char* format, Args... arguments) {
    const SourceFileScope location(source);
    ::LogIOError(format, arguments...);
}
} // namespace runtime::logging

#define L2_LOG_IO_ERROR(...) ::runtime::logging::LogIOErrorAt(__FILE__, __VA_ARGS__)

// Out-of-line to avoid the Exception.h -> log.h -> logSocket.h -> ioc.h
// include cycle. Current EXE 0x0041f74f calls ReportCallStack immediately
// after the abortive native socket close.
void ReportIOCloseCallStack();

// One cache-line-padded heartbeat per IO worker. L2NPC creates the
// eight-slot specialization. Current L2Server creates sixteen slots at
// 0x0d0b2b80 (0x400 bytes). Both methods are inlined in the shipping images.
template <std::size_t ThreadCount>
class CIOSyncDeadlockChecker {
public:
    struct alignas(64) LastTickWithPadding {
        std::atomic<std::uint64_t> tick64{0};
    };

    void CheckForDeadlock();
    void UpdateCurrentThread();

private:
    std::array<LastTickWithPadding, ThreadCount> m_lastTickArr{};
};


#if defined(L2_BUILD_L2SERVER)
extern CIOSyncDeadlockChecker<16> g_IOSyncDeadlockChecker;
#else
extern CIOSyncDeadlockChecker<8> g_IOSyncDeadlockChecker;
#endif

// Current L2NPC.exe 0x004566dc, Cached.exe 0x004e678c and L2Server.exe
// 0x007cc924. Each product builds its own physical Shared/ioc.cpp body. The
// shipping loop never returns. The portable runtime permits cooperative
// shutdown after a complete iteration.
std::uint32_t WaitThread(void* argument);

// Recovered IO-thread synchronization callback. CreateIOThread2 stores the
// logical TLS id in m_Thread before the worker loop begins.
class IOThreadCallback {
public:
    explicit IOThreadCallback(int threadId) noexcept;
    ~IOThreadCallback() = default;

    void IOThreadNextSyncTime();
    void IOThread_common();
    void IOThread_mailing();
    void IOThread_debug();
    virtual void IOThreadNextSyncTimeCallback() = 0;

    int m_Thread = 0;
};

// Recovered from Shared/ioc.h and Shared/ioc.cpp. Timers keep their owner
// with ORT_TIMER until the callback epilogue has completed.
class CIOObject : public MemoryObject {
public:
    CIOObject();
    CIOObject(const CIOObject&);
    ~CIOObject() override;

    L2_DECLARE_MEMORY_ALLOCATION(CIOObject)

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    virtual void TimerExpired(int id);
    virtual void OnTimer(int id);
    virtual void TimerExpired2(int id, std::int64_t uid);
    virtual void OnTimer2(int id, std::int64_t uid);
    // Current EXE vtable slots +0x40..+0x58. OnTimerCallback is the
    // LISTEN-thread hop back onto IOCP. OnIOCallback consumes either that
    // timer completion or a derived socket/packet completion.
    virtual void OnTimerCallback(int id, std::int64_t uid);
    virtual void OnWaitCallback();
    virtual void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped);
    virtual int PostObject(int id);

#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x007cb580 and its embedded guard string both
    // carry the delay as __int64. L2NPC/CacheD are older product
    // versions of this Shared header and keep unsigned int below.
    bool AddTimer(std::int64_t delayMilliseconds, int id);
    bool AddTimer2(std::int64_t delayMilliseconds, int id, std::int64_t uid);
#else
    bool AddTimer(std::uint32_t delayMilliseconds, int id);
    bool AddTimer2(std::uint32_t delayMilliseconds, int id, std::int64_t uid);
#endif

    // The target owns one priority queue per logical TLS thread and calls
    // this at the I/O-thread synchronization boundary.
    static void TimerDispatch();
    static void TimerDispatchAndClearAll();
    static void ClearAllTimer();
    static void GetTimerCount(int& timerCount, int& shortTimeCount);

    static bool SetTimeStamp(std::int32_t* timestamp);
    static bool IsInTime(std::int32_t timestamp, int additionalSeconds);

    static bool SetShortTimeStamp(std::uint32_t* timestamp, std::uint32_t additionalMilliseconds);
    static bool IsInShortTime(std::uint32_t timestamp, std::uint32_t additionalMilliseconds);

    int RegisterWait(void* handle);
};


#if defined(L2_BUILD_L2SERVER)
// Current L2Server.exe 0x00aead8c. The callback forwards the
// integer uid through the native OVERLAPPED* carrier without interpreting it.
void SafeIOCallback(CIOObject* object, int id, std::int64_t uid);
#endif

// Shared/ioc.h:1511-1513. RTTI and both the current and NASC64_2015 vtables
// prove that CAbstractPacket derives directly from CIOObject. It is not a
// CIOCPMessage: the latter adds a thirteenth, pure OnReceive slot.
class CAbstractPacket : public CIOObject {
public:
    CAbstractPacket() = default;
    ~CAbstractPacket() override = default;

    L2_DECLARE_OBJECT_TYPE(CAbstractPacket)

    static std::atomic<std::int32_t> g_nPendingPacket;
};


namespace io_buffer_detail {

void FailIOBufferAllocation(std::source_location location = std::source_location::current());
void ReportInvalidIOBufferAllocation(
    int threadId, int remaining, int referenceCount, std::uint32_t dataSize, const void* next
);

} // namespace io_buffer_detail

template <typename TBuffer>
class BufferPoolSelector;

// Shared/ioc.h pooled I/O buffer. Current L2NPC.exe confirms the exact
// 20480-byte specialization and every field offset below. Allocation and
// zero-reference release both use the target CMwsrQueue ring.
template <std::size_t Size>
class CIOBufferEx {
public:
    enum { MaxSize = Size };

    CIOBufferEx() = default;
    ~CIOBufferEx() = default;

    static CIOBufferEx* Alloc() {
        CIOBufferEx* buffer = BufferPoolSelector<CIOBufferEx>::PopBuffer();
        while (buffer &&
            (buffer->m_nRefCount.load(std::memory_order_relaxed) != 0 || buffer->m_dwSize != 0 ||
                buffer->m_pNext != nullptr)) {
            // Current 0x0041fca0 observes the pool size first, then the
            // candidate's next/size/refcount fields, and only then rereads
            // the logical thread ID used by the diagnostic.
            const int poolThreadId = CThreadManager::GetThreadID();
            const int remaining = BufferPoolSelector<CIOBufferEx>::InstantSize(poolThreadId);
            const void* const next = buffer->m_pNext;
            const std::uint32_t dataSize = buffer->m_dwSize;
            const int referenceCount = buffer->m_nRefCount.load(std::memory_order_relaxed);
            const int logThreadId = CThreadManager::GetThreadID();
            io_buffer_detail::ReportInvalidIOBufferAllocation(logThreadId, remaining, referenceCount, dataSize, next);
            buffer = BufferPoolSelector<CIOBufferEx>::PopBuffer();
        }
        if (!buffer) {
            io_buffer_detail::FailIOBufferAllocation();
            return nullptr;
        }

        buffer->m_nOwnThreadID = CThreadManager::GetThreadID();
        buffer->m_dwSize = 0;
        buffer->m_nRefCount.fetch_add(1, std::memory_order_seq_cst);
        buffer->m_pNext = nullptr;
        return buffer;
    }

    CIOBufferEx* Copy() const {
        CIOBufferEx* copy = Alloc();
        copy->m_dwSize = m_dwSize;
        copy->m_pNext = nullptr;
        std::memcpy(copy->m_Buffer.data(), m_Buffer.data(), m_dwSize);
        return copy;
    }

    bool AssignFrame(std::uint8_t opcode, const std::uint8_t* payload, std::size_t payloadLength) {
        const std::size_t total = payloadLength + 3;
        if (total > m_Buffer.size() || (!payload && payloadLength != 0)) {
            return false;
        }
        m_dwSize = static_cast<std::uint32_t>(total);
        m_Buffer[0] = static_cast<std::uint8_t>(total);
        m_Buffer[1] = static_cast<std::uint8_t>(total >> 8);
        m_Buffer[2] = opcode;
        if (payloadLength != 0) {
            std::memcpy(m_Buffer.data() + 3, payload, payloadLength);
        }
        return true;
    }

    void AddRef() { m_nRefCount.fetch_add(1, std::memory_order_seq_cst); }

    void Release() {
        if (m_nRefCount.fetch_sub(1, std::memory_order_seq_cst) == 1) {
            m_dwSize = 0;
            m_pNext = nullptr;
            BufferPoolSelector<CIOBufferEx>::PushBuffer(this);
        }
    }

    std::array<std::uint8_t, 0x10> pad1{};
    std::array<std::uint8_t, Size> m_Buffer{};
    std::uint32_t m_dwSize = 0;
    CIOBufferEx* m_pNext = nullptr;
    std::atomic<std::int32_t> m_nRefCount{0};
    std::array<std::uint8_t, 0x10> pad2{};
    std::int32_t m_nOwnThreadID = 0;
};

using DefaultIOBuffer = CIOBufferEx<20480>;
using IOBuffer8k = CIOBufferEx<8192>;
using IOBuffer32k = CIOBufferEx<32768>;

#if defined(L2_BUILD_L2SERVER)
namespace ioc_memory_detail {

// The original static OBJECT_TYPE members belong to each complete template
// specialization, not to one semantic CPacketEx/CIOSocketEx name. Current
// L2Server RTTI and the class-specific Release functions bind these three
// physical buffer carriers to six independent registration globals.
template <typename TBuffer>
struct ShippingL2ServerIOClassIds;

template <>
struct ShippingL2ServerIOClassIds<CIOBufferEx<20480>> final {
    static constexpr int CPacketExClassId = 5;
    static constexpr int CIOSocketExClassId = 6;
};

template <>
struct ShippingL2ServerIOClassIds<CIOBufferEx<32768>> final {
    static constexpr int CPacketExClassId = 29;
    static constexpr int CIOSocketExClassId = 30;
};

template <>
struct ShippingL2ServerIOClassIds<CIOBufferEx<8192>> final {
    static constexpr int CPacketExClassId = 55;
    static constexpr int CIOSocketExClassId = 56;
};

} // namespace ioc_memory_detail
#endif

#if defined(L2_BUILD_L2SERVER)
extern CNewMwsrQueue<DefaultIOBuffer>* g_IOBufferPools[ThreadID::PORTABLE_CAPACITY];
extern CNewMwsrQueue<IOBuffer8k>* g_IOBuffer8kPools[ThreadID::PORTABLE_CAPACITY];
extern CNewMwsrQueue<IOBuffer32k>* g_IOBuffer32kPools[ThreadID::PORTABLE_CAPACITY];
#else
extern CMwsrQueue<DefaultIOBuffer>* g_IOBufferPools[ThreadID::PORTABLE_CAPACITY];
extern CMwsrQueue<IOBuffer8k>* g_IOBuffer8kPools[ThreadID::PORTABLE_CAPACITY];
extern CMwsrQueue<IOBuffer32k>* g_IOBuffer32kPools[ThreadID::PORTABLE_CAPACITY];
#endif

template <>
class BufferPoolSelector<DefaultIOBuffer> {
public:
    static DefaultIOBuffer* PopBuffer();
    static void PushBuffer(DefaultIOBuffer* buffer);
    static int InstantSize(int threadId);
};

template <>
class BufferPoolSelector<IOBuffer8k> {
public:
    static IOBuffer8k* PopBuffer();
    static void PushBuffer(IOBuffer8k* buffer);
    static int InstantSize(int threadId);
};

template <>
class BufferPoolSelector<IOBuffer32k> {
public:
    static IOBuffer32k* PopBuffer();
    static void PushBuffer(IOBuffer32k* buffer);
    static int InstantSize(int threadId);
};


// Original Shared/ioc.h variadic helper. The current L2Server body at
// 0x005441b0 and the TotalChecker COMDAT both allocate the default buffer,
// assemble after the two-byte frame header and publish the wrapped size.
inline DefaultIOBuffer* MakeConstBuffer(const char* format, ...) {
    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();

    va_list arguments;
    va_start(arguments, format);
    const std::uint16_t bodySize = static_cast<std::uint16_t>(
        ::VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 2), 0x4ffe, format, arguments)
    );
    va_end(arguments);

    if (bodySize == 0) {
        buffer->Release();
        return nullptr;
    }

    const std::uint16_t packetSize = static_cast<std::uint16_t>(bodySize + 2);
    std::memcpy(buffer->m_Buffer.data(), &packetSize, sizeof(packetSize));
    buffer->m_dwSize = packetSize;
    return buffer;
}

enum SocketMode : std::int32_t {
    SM_READ_LEN = 0,
    SM_READ_BODY = 1,
    SM_CLOSE = 2,
};

enum RQ_PROTOCOL : std::int32_t {
    RQ_VERSION = 0,
    RQ_LOAD_CHARACTER = 1,
    RQ_CREATE_CHARACTER = 2,
    RQ_LOAD_SUMMONS = 3,
    RQ_LOAD_ITEMS = 4,
    RQ_CREATE_INIT_ITEMS = 5,
    RQ_DROP_ITEM = 6,
    RQ_SAVE_CHARACTER = 7,
    RQ_SAVE_ITEMS = 8,
    RQ_DELETE_CHARACTER = 9,
    RQ_UPDATE_USER_PLEDGE = 10,
    RQ_DELETE_PLEDGE = 11,
    RQ_LOAD_WAREHOUSE_ITEMS = 12,
    RQ_WITHDRAW_WAREHOUSE_ITEMS = 13,
    RQ_SHORTCUT_INFO = 14,
    RQ_SET_SHORTCUT = 15,
    RQ_DEPOSIT_WAREHOUSE_ITEMS = 16,
    RQ_SELL_ITEMS = 17,
    RQ_BUY_ITEMS = 18,
    RQ_USE_ITEM = 19,
    RQ_LOAD_PLEDGE = 20,
    RQ_LOGIN_CHARACTER = 21,
    RQ_LOGOUT_CHARACTER = 22,
    RQ_SET_QUEST = 23,
    RQ_OWN_AGIT_OWNER = 24,
    RQ_UPDATE_CASTLE_OWNER = 25,
    RQ_LOAD_CASTLES = 26,
    RQ_LOAD_AGITS = 27,
    RQ_WRITE_TAX = 28,
    RQ_NOT_USED2 = 29,
    RQ_MANIPULATE_ITEM = 30,
    RQ_OUST_PLEDGE_MEMBER = 31,
    RQ_TRADE_ITEMS = 32,
    RQ_SET_PLEDGE_CREST = 33,
    RQ_NOT_USED3 = 34,
    RQ_NOT_USED4 = 35,
    RQ_NOT_USED5 = 36,
    RQ_SET_USER_NICKNAME = 37,
    RQ_ACQUIRE_SKILL = 38,
    RQ_DESTROY_ITEM = 39,
    RQ_ENCHANT_ITEM = 40,
    RQ_ENCHANT_ITEM_FAIL = 41,
    RQ_SET_ITEM_COUNT_LIMIT = 42,
    RQ_LOAD_SKILL = 43,
    RQ_LOAD_FRIEND_LIST = 44,
    RQ_ESTABLISH_FRIENDSHIP = 45,
    RQ_BREAK_FRIENDSHIP = 46,
    RQ_CHANGE_ITEM_DATA = 47,
    RQ_SET_PUNISH = 48,
    RQ_GET_PUNISH = 49,
    RQ_CHANGE_POS = 50,
    RQ_GET_LOGIN_ANNOUNCE = 51,
    RQ_SET_LOGIN_ANNOUNCE = 52,
    RQ_DEL_LOGIN_ANNOUNCE = 53,
    RQ_SET_BUILDER = 54,
    RQ_LOAD_SELECTED_CHAR = 55,
    RQ_DELETE_SKILL = 56,
    RQ_RESTORE_CHAR = 57,
    RQ_SET_PLEDGE_INFO = 58,
    RQ_NOT_USED16 = 59,
    RQ_GET_PLEDGE_CREST = 60,
    RQ_NOT_USED6 = 61,
    RQ_NOT_USED7 = 62,
    RQ_CREATE_PLEDGE_BY_NPC = 63,
    RQ_DELETE_PLEDGE_BY_TIMER = 64,
    RQ_NOT_USED17 = 65,
    RQ_LOAD_DISMISS_RESERVED_PLEDGE = 66,
    RQ_NOT_USED8 = 67,
    RQ_PETI_GET_CHAR_LIST = 68,
    RQ_SAVE_PET_MSG = 69,
    RQ_SET_USER_PLEDGE_INFO = 70,
    RQ_LOG_CHAR_INFO = 71,
    RQ_NOT_USED9 = 72,
    RQ_RESET_USER_NICKNAME = 73,
    RQ_MANAGE_BATTLE_CAMP = 74,
    RQ_LOAD_BATTLE_CAMP = 75,
    RQ_PREPARE_CASTLE_WAR = 76,
    RQ_VIEW_SIEGE_LIST = 77,
    RQ_APPROVE_BATTLE = 78,
    RQ_CONFIRM_CASTLE_DEFENCE = 79,
    RQ_QUERY_CASTLE_SIEGE_FOR_REGISTER = 80,
    RQ_QUERY_CASTLE_SIEGE_FOR_UNREGISTER = 81,
    RQ_QUERY_CASTLE_SIEGE_FOR_INFO = 82,
    RQ_QUERY_CASTLE_SIEGE_FOR_DISMISS = 83,
    RQ_DEL_PLEDGE_CREST = 84,
    RQ_MOD_PLEDGE_NAME = 85,
    RQ_RESET_CASTLE_SIEGE_PLEDGE = 86,
    RQ_PARTY_TEL_BOOKMARK = 87,
    RQ_NOT_USED10 = 88,
    RQ_LOAD_DOOR = 89,
    RQ_SAVE_CASTLE_STATUS = 90,
    RQ_SAVE_DOOR_STATUS = 91,
    RQ_LOAD_WAREHOUSE_ITEM_LIST = 92,
    RQ_CRYSTALLIZE_ITEM = 93,
    RQ_SAVE_CASTLE_INCOME = 94,
    RQ_INSTALL_AGIT_DECO = 95,
    RQ_SAVE_PLEDGE_CONTRIBUTION = 96,
    RQ_GET_CONTRIBUTION_RELATED_PLEDGE = 97,
    RQ_SET_AGIT_INFO = 98,
    RQ_PRIVATE_STORE_COMMIT = 99,
    RQ_SAVE_DIE_DROP_EXP = 100,
    RQ_RESET_CONTRIBUTION = 101,
    RQ_SAVE_OBJECT_HP = 102,
    RQ_SAVE_CASTLE_SIEGE_ELAPSED_TIME = 103,
    RQ_SAVE_SKILL_USE = 104,
    RQ_LOAD_PLEDGE2 = 105,
    RQ_PING = 106,
    RQ_PING_ONE_ADENA_DROP = 107,
    RQ_PING_NETWORK = 108,
    RQ_SAVE_ABNORMALSTATUS = 109,
    RQ_LOAD_ABNORMALSTATUS = 110,
    RQ_CREATE_ALLIANCE_BY_NPC = 111,
    RQ_JOIN_ALLIANCE = 112,
    RQ_DISMISS_ALLIANCE = 113,
    RQ_OUST_ALLIANCE_MEMBER_PLEDGE = 114,
    RQ_WITHDRAW_ALLIANCE = 115,
    RQ_CREATE_PET = 116,
    RQ_DELETE_PET = 117,
    RQ_EVOLVE_PET = 118,
    RQ_WITHDRAW_PET = 119,
    RQ_LOAD_PET_ITEMS = 120,
    RQ_DEPOSIT_PET = 121,
    RQ_SAVE_PET = 122,
    RQ_CHANGE_PET_NAME = 123,
    RQ_DISMISS_PET = 124,
    RQ_PET_GET_ITEM = 125,
    RQ_PET_DROP_ITEM = 126,
    RQ_GIVE_ITEM_TO_PET = 127,
    RQ_GET_ITEM_FROM_PET = 128,
    RQ_PET_USE_ITEM = 129,
    RQ_SET_ALLIANCE_CREST = 130,
    RQ_GET_ALLIANCE_CREST = 131,
    RQ_NOT_USED11 = 132,
    RQ_LOAD_ALL_ALLIANCE = 133,
    RQ_SET_ALLIANCE_INFO = 134,
    RQ_GET_DB_RELATED_COUNTS = 135,
    RQ_LOAD_ALL_PLEDGE_WAR = 136,
    RQ_INCREASE_USER_PKCOUNT = 137,
    RQ_INCREASE_USER_DUELCOUNT = 138,
    RQ_RECIPE_MAKE_ITEM = 139,
    RQ_PICK_ITEM = 140,
    RQ_PACKAGE_LOAD_TARGET_LIST = 141,
    RQ_PACKAGE_SEND_TO_TARGET = 142,
    RQ_ADD_BLOCK_LIST = 143,
    RQ_DELETE_BLOCK_LIST = 144,
    RQ_LOAD_WAREHOUSE_ITEMS_EX = 145,
    RQ_WITHDRAW_WAREHOUSE_ITEMS_EX = 146,
    RQ_DEPOSIT_WAREHOUSE_ITEMS_EX = 147,
    RQ_SET_NEXT_CASTLE_SIEGE = 148,
    RQ_CREATE_AGIT_AUCTION = 149,
    RQ_CREATE_AGIT_BID = 150,
    RQ_SET_AGIT_AUCTION = 151,
    RQ_LOAD_CASTLE_GUARD = 152,
    RQ_LOAD_AGIT_BID = 153,
    RQ_LOAD_CONTROL_TOWER = 154,
    RQ_SAVE_CONTROL_TOWER = 155,
    RQ_SAVE_MERCENARY = 156,
    RQ_LOAD_MERCENARY = 157,
    RQ_DELETE_MERCENARY = 158,
    RQ_UPDATE_MERCENARY = 159,
    RQ_AGIT_PLEDGE_ADENA = 160,
    RQ_CANCEL_AGIT_AUCTION = 161,
    RQ_AGIT_COST = 162,
    RQ_SHOW_INTERVAL_ANNOUNCE = 163,
    RQ_SET_INTERVAL_ANNOUNCE = 164,
    RQ_DEL_INTERVAL_ANNOUNCE = 165,
    RQ_DELETE_CASTLE_GUARD = 166,
    RQ_AGIT_AUTOSELL = 167,
    RQ_AGIT_BID_CANCEL = 168,
    RQ_LOAD_INTERVAL_ANNOUNCE = 169,
    RQ_NOT_USED12 = 170,
    RQ_END_SIEGE = 171,
    RQ_GET_CHAR_DBID = 172,
    RQ_NOT_USED13 = 173,
    RQ_LOAD_LAST_CHANGE_RESIDENCE = 174,
    RQ_SET_LAST_CHANGE_RESIDENCE = 175,
    RQ_MNG_COMMENT = 176,
    RQ_LOAD_PLAY_TIME = 177,
    RQ_BOOKMARK = 178,
    RQ_NOT_USED14 = 179,
    RQ_LOAD_TEAM_BATTLE_AGIT = 180,
    RQ_NEW_TEAM_BATTLE_AGIT_PLEDGE = 181,
    RQ_NEW_TEAM_BATTLE_AGIT_MEMBER = 182,
    RQ_TEAM_BATTLE_AGIT_STATUS = 183,
    RQ_CHANGE_TIME_LIMIT = 184,
    RQ_SAVE_USED_TIME = 185,
    RQ_UNREGISTER_TEAM_BATTLE_AGIT_PLEDGE = 186,
    RQ_UNREGISTER_TEAM_BATTLE_AGIT_MEMBER = 187,
    RQ_MONRACE = 188,
    RQ_LOAD_SIEGE_AGIT = 189,
    RQ_REGISTER_SIEGE_AGIT_PLEDGE = 190,
    RQ_UNREGISTER_SIEGE_AGIT_PLEDGE = 191,
    RQ_SET_TEAM_BATTLE_STATUS = 192,
    RQ_SET_TEAM_BATTLE_WINNER = 193,
    RQ_USE_RECIPE_ITEM = 194,
    RQ_SAVE_RECIPE_BOOK_INFO = 195,
    RQ_DELETE_RECIPE_BOOK_INFO = 196,
    RQ_LOAD_RECIPE_BOOK_INFO = 197,
    RQ_MAKE_RECIPE_SHOP_ITEM = 198,
    RQ_UPDATE_SOCIALITY = 199,
    RQ_SET_TEAM_BATTLE_NPC_TYPE = 200,
    RQ_RESET_SIEGE_AGIT_PLEDGE = 201,
    RQ_RESET_TEAM_BATTLE_AGIT_PLEDGE = 202,
    RQ_EQUIP_HENNA = 203,
    RQ_UNEQUIP_HENNA = 204,
    RQ_MAN_MACRO = 205,
    RQ_MAN_MEMO = 206,
    RQ_MAN_PLEDGEBBS = 207,
    RQ_MANOR_BUY_SEED = 208,
    RQ_MANOR_SELL_CROP = 209,
    RQ_MANOR_MOVE_CROP = 210,
    RQ_SAVE_MANOR_NEXT_SEED = 211,
    RQ_SAVE_MANOR_NOW_SEED = 212,
    RQ_SAVE_MANOR_INFO = 213,
    RQ_LOAD_ALL_MANOR = 214,
    RQ_LOAD_EVENT_POINT = 215,
    RQ_SAVE_EVENT_POINT = 216,
    RQ_BBS_CLANACTION = 217,
    RQ_MOD_ALLIANCE_NAME = 218,
    RQ_NOT_USED15 = 219,
    RQ_CREATE_SUBJOB = 220,
    RQ_DELETE_SUBJOB = 221,
    RQ_CHANGE_SUBJOB = 222,
    RQ_LOAD_SUBJOB = 223,
    RQ_EVENT_AMOUNT = 224,
    RQ_SAVE_CHARACTER_FOR_SUBJOB = 225,
    RQ_RELOAD_SKILL = 226,
    RQ_SAVE_ACQUIRED_SKILLS = 227,
    RQ_DX = 228,
    RQ_QUERY = 229,
    RQ_SAVE_PUNISHMENT_HISTORY = 230,
    RQ_LOAD_ALL_SUSPEND_PLEDGE = 231,
    RQ_LOAD_MAIN_CLASS_SKILL = 232,
    RQ_DELETE_PLEDGE_SKILL = 233,
    RQ_EX = 234,
    RQ_MAX = 235,
};

enum RQ_EX_PROTOCOL : std::int32_t {
    RQ_LOAD_SSQ_INFO = 0,
    RQ_MAIL_LIST_EX = 1,
    RQ_READ_MAIL_EX = 2,
    RQ_LOAD_TIMEATTACK_INFO_EX = 3,
    RQ_SEND_MAIL_EX = 4,
    RQ_SAVE_MAIL_EX = 5,
    RQ_SET_DAWN_ROUND_TO_PLEDGE_EX = 6,
    RQ_START_SSQ_EVENT_EX = 7,
    RQ_INIT_TIME_ATTACK_DB_EX = 8,
    RQ_GET_TEMP_MAIL_EX = 9,
    RQ_REPLY_MAIL_EX = 10,
    RQ_FORWARD_MAIL_EX = 11,
    RQ_MODIFY_TEMP_MAIL_EX = 12,
    RQ_MODIFY_SEND_TEMP_MAIL_EX = 13,
    RQ_DELETE_MAIL_EX = 14,
    RQ_ARCHIVE_MAIL_EX = 15,
    RQ_CHANGE_SSQ_DROP_GUARD_STATUS = 16,
    RQ_UNREAD_MAIL_COUNT_EX = 17,
    RQ_SEND_PLEDGE_MAIL_EX = 18,
    RQ_DELETE_FRIEND_BATCH_EX = 19,
    RQ_DELETE_BLOCK_BATCH_EX = 20,
    RQ_LOAD_MARKET_PRICE_LIST_EX = 21,
    RQ_RENEW_SUBJOB_EX = 22,
    RQ_LOAD_ALL_NOBLESS_EX = 23,
    RQ_LOAD_NOBLESS_EX = 24,
    RQ_EX_SET_PLEDGE_EMBLEM = 25,
    RQ_EX_GET_PLEDGE_EMBLEM = 26,
    RQ_EX_DEL_PLEDGE_EMBLEM = 27,
    RQ_GET_NOBLESS_TOP10_EX = 28,
    RQ_GET_NOBLESS_ACHIEVEMENTS_EX = 29,
    RQ_GET_PLEDGE_ANNOUNCE_EX = 30,
    RQ_SET_PLEDGE_ANNOUNCE_EX = 31,
    RQ_SWITCH_PLEDGE_ANNOUNCE_SHOW_FLAG_EX = 32,
    RQ_LOAD_PLEDGE_SIEGE_KILL_DEATH_COUNT_EX = 33,
    RQ_LOAD_PCCAFE_POINT_EX = 34,
    RQ_SAVE_PCCAFE_POINT_EX = 35,
    RQ_MANIPULATE_ITEM_WITH_POINT_EX = 36,
    RQ_NPC_PICK_ITEM_EX = 37,
    RQ_FISHING_EVENT_FINISH_EX = 38,
    RQ_FISHING_EVENT_REGISTER_EX = 39,
    RQ_FISHING_EVENT_SAVE_PRIZE_EX = 40,
    RQ_UPDATE_BOSS_RECORD_EX = 41,
    RQ_LOAD_BOSS_RECORD_EX = 42,
    RQ_CREATE_ACADEMY_BY_NPC_EX = 43,
    RQ_LOAD_BOSS_RECORD_RANKING_EX = 44,
    RQ_LOAD_BOSS_RECORD_ROUND_EX = 45,
    RQ_MOVE_TO_NEXT_BOSS_RECORD_ROUND_EX = 46,
    RQ_LOAD_MINIGAME_AGIT_EX = 47,
    RQ_SET_MINIGAME_AGIT_STATUS_EX = 48,
    RQ_ADD_MINIGAME_AGIT_PLEDGE_EX = 49,
    RQ_DEL_MINIGAME_AGIT_PLEDGE_EX = 50,
    RQ_ADD_MINIGAME_AGIT_MEMBER_EX = 51,
    RQ_RESET_MINIGAME_AGIT_EX = 52,
    RQ_UPDATE_PLEDGE_NAMEVALUE_EX = 53,
    RQ_LOAD_SUB_PLEDGE_EX = 54,
    RQ_LOAD_PLEDGE_SKILL_EX = 55,
    RQ_LOAD_CURSED_WEAPON_LIST_EX = 56,
    RQ_ADD_CURSED_WEAPON_EX = 57,
    RQ_UPDATE_CURSED_WEAPON_EX = 58,
    RQ_DEL_CURSED_WEAPON_EX = 59,
    RQ_DEL_CURSED_WEAPON_ONLY_IN_DB_EX = 60,
    RQ_LOAD_PLEDGE_RANKING_LIST_EX = 61,
    RQ_SET_PLEDGE_POWER_GRADE_EX = 62,
    RQ_CREATE_SUBPLEDGE_BY_NPC_EX = 63,
    RQ_SAVE_PLEDGE_SKILL_EX = 64,
    RQ_SAVE_SUBPLEDGE_SKILL_EX = 65,
    RQ_DEL_PLEDGE_SKILL_EX = 66,
    RQ_DEL_SUBPLEDGE_SKILL_EX = 67,
    RQ_SAVE_PLEDGE_POWER_EX = 68,
    RQ_UPDATE_SUBPLEDGE_MASTER_BY_NPC_EX = 69,
    RQ_UPGRADE_SUBPLEDGE_MEMBER_COUNT_EX = 70,
    RQ_INITIALIZE_BOSS_RECORD_EX = 71,
    RQ_GRADUATE_ACADEMY_EX = 72,
    RQ_SET_ACADEMY_MASTER_EX = 73,
    RQ_PLEDGE_MASTER_TRANSFER_BY_NPC_EX = 74,
    RQ_PLEDGE_MASTER_TRANSFER_CANCEL_BY_NPC_EX = 75,
    RQ_UPDATE_PLEDGE_NAMEVALUE_BY_USER_EX = 76,
    RQ_PCCAFE_COUPON_CHECK_EX = 77,
    RQ_PCCAFE_COUPON_USED_EX = 78,
    RQ_RENAME_SUBPLEDGE_BY_NPC_EX = 79,
    RQ_LOAD_TRANSFERRED_PLEDGE_MASTER_EX = 80,
    RQ_SAVE_CLANCOMM_TRANSFER_RESULT_EX = 81,
    RQ_SET_PREVIOUS_CASTLE_OWNER_ID_EX = 82,
    RQ_ACADEMY_GRADUATE_LIST_EX = 83,
    RQ_PICK_CURSED_WEAPON_EX = 84,
    RQ_REORGANIZE_PLEDGE_MEMBER_EX = 85,
    RQ_OLYMPIAD_PENALTY_LOG_EX = 86,
    RQ_LOAD_SHARED_REUSE_DELAYS_OF_ITEMS_EX = 87,
    RQ_SAVE_SHARED_REUSE_DELAY_OF_ITEM_EX = 88,
    RQ_VARIATION_ITEM_EX = 89,
    RQ_CANCEL_VARIATION_ITEM_EX = 90,
    RQ_UPDATE_ITEM_DURATION_EX = 91,
    RQ_SAVE_DUEL_RECORD_EX = 92,
    RQ_LOAD_DUEL_RECORD_EX = 93,
    RQ_LARGE_PACKET_EX = 94,
    RQ_GET_HERO_MATCH_RESULT_EX = 95,
    RQ_SAVE_FIELD_CYCLE_INFO_EX = 96,
    RQ_LOAD_FIELD_CYCLE_INFO_EX = 97,
    RQ_SAVE_CHARACTER_SLOT_EX = 98,
    RQ_LOAD_FORTRESS_EX = 99,
    RQ_LOAD_FORTRESS_SIEGE_EX = 100,
    RQ_LOAD_FORTRESS_SIEGE_REGISTRY_EX = 101,
    RQ_UPDATE_FORTRESS_OWNER_EX = 102,
    RQ_PAY_FORTRESS_RENT_EX = 103,
    RQ_SAVE_FORTRESS_STATE_EX = 104,
    RQ_SAVE_FORTRESS_SIEGE_STATE_EX = 105,
    RQ_SAVE_FORTRESS_CONTRACT_INFO_EX = 106,
    RQ_SAVE_FORTRESS_REWARD_INFO_EX = 107,
    RQ_SAVE_FORTRESS_SIEGE_REGISTRY_EX = 108,
    RQ_SAVE_FORTRESS_SIEGE_REGISTER_PLEDGE_EX = 109,
    RQ_LOAD_ALL_AUCTION_EX = 110,
    RQ_LOAD_ALL_AUCTION_BID_EX = 111,
    RQ_GET_LAST_START_TIME_EX = 112,
    RQ_CREATE_ITEM_AUCTION_EX = 113,
    RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST = 114,
    RQ_SET_ITEM_AUCTION_END_TIME_EX = 115,
    RQ_SET_AUCTION_STATUS_EX = 116,
    RQ_SET_AUCTION_ITEM_EX = 117,
    RQ_MANIPULATE_WAREHOUSE_ITEM_EX = 118,
    RQ_CONVERT_ITEM_EX = 119,
    RQ_CHANGE_MERGED_NAME_EX = 120,
    RQ_SET_TUTORIAL_EX = 121,
    RQ_GET_TUTORIAL_EX = 122,
    RQ_LOAD_INZONE_RESTRICTION_EX = 123,
    RQ_MARK_INZONE_RESTRICTION_EX = 124,
    RQ_LOAD_UI_SETTING_EX = 125,
    RQ_SAVE_UI_SETTING_EX = 126,
    RQ_RESET_INZONE_RESTRICTION_EX = 127,
    RQ_SAVE_INVENTORY_SLOT_INDEX = 128,
    RQ_SET_CASTLE_SIEGE_DEFENCE_COUNT_EX = 129,
    RQ_SET_ASSOCIATED_INZONE_EX = 130,
    RQ_WAITING_TRANSFER_PLEDGE_MASTER_EX = 131,
    RQ_DELETE_SKILL_LIST_EX = 132,
    RQ_SAVE_USER_POINT_EX = 133,
    RQ_LOAD_USER_POINT_EX = 134,
    RQ_SAVE_PVPPOINT_RESTRAIN_EX = 135,
    RQ_LOAD_PVPPOINT_RESTRAIN_EX = 136,
    RQ_SAVE_LAST_COLOR_EX = 137,
    RQ_LOAD_LAST_COLOR_EX = 138,
    RQ_SAVE_NR_MEMO_EX = 139,
    RQ_LOAD_NR_MEMO_EX = 140,
    RQ_DELETE_NR_MEMO_EX = 141,
    RQ_SAVE_PREMIUM_ITEM_FOR_CHAR_EX = 142,
    RQ_SAVE_PREMIUM_ITEM_FOR_SERVER_EX = 143,
    RQ_LOAD_USER_PREMIUM_ITEM_EX = 144,
    RQ_USER_BOOKMARK_JOB_EX = 145,
    RQ_PERIOD_ITEM_JOB_EX = 146,
    RQ_MANIPULATE_PREMIUM_ITEMS_EX = 147,
    RQ_SET_DELETED_IN_IBSERVER_EX = 148,
    RQ_DELETE_PREMIUM_ITEM_EX = 149,
    RQ_SET_RIMPOINT_EX = 150,
    RQ_GET_RIMPOINT_TOP10_EX = 151,
    RQ_SET_BOT_REPORT_EX = 152,
    RQ_SAVE_BOT_REPORT_EX = 153,
    RQ_LOAD_BOT_REPORT_EX = 154,
    RQ_LOAD_BOT_REPORT_TOP_TEN_EX = 155,
    RQ_RESET_BOT_REPORT_EX = 156,
    RQ_SET_BOT_ADMIN_EX = 157,
    RQ_DECAY_BOT_REPORT_EX = 158,
    RQ_CHANGE_NICKNAME_COLOR_EX = 159,
    RQ_LOAD_PREMIUM_ITEMS_FOR_BATCH_EX = 160,
    RQ_LOAD_DOMINION_EX = 161,
    RQ_SAVE_DOMINION_EX = 162,
    RQ_SAVE_DOMINION_REGISTRY_EX = 163,
    RQ_LOAD_DOMINION_REGISTRY_EX = 164,
    RQ_SAVE_OWNTHING_EX = 165,
    RQ_LOAD_OWNTHING_EX = 166,
    RQ_SAVE_DOMINION_SIEGE_EX = 167,
    RQ_LOAD_DOMINION_SIEGE_EX = 168,
    RQ_LOAD_BIRTH_INFO_EX = 169,
    RQ_SAVE_GET_BIRTHDAY_GIFT_EX = 170,
    RQ_GET_LAST_BIRTHDAY_GIFT_DATE__EX = 171,
    RQ_LOAD_5YEAR_INFO_EX = 172,
    RQ_SAVE_GET_5YEAR_GIFT_EX = 173,
    RQ_SAVE_DOMINION_RENAMED_EX = 174,
    RQ_LOAD_DOMINION_RENAMED_EX = 175,
    RQ_DELETE_ALL_DOMINION_RENAMED_EX = 176,
    RQ_SET_DAILY_QUEST_EX = 177,
    RQ_LOAD_DAILY_QUEST = 178,
    RQ_SET_BIRTHDAY = 179,
    RQ_AIRSHIP_JOB_EX = 180,
    RQ_CACHE_PING_EX = 181,
    RQ_WRITE_POST_EX = 182,
    RQ_SEND_POST_EX = 183,
    RQ_RECEIVE_POST_EX = 184,
    RQ_REJECT_POST_EX = 185,
    RQ_CANCEL_POST_EX = 186,
    RQ_DELETE_POST_EX = 187,
    RQ_OPEN_POST_EX = 188,
    RQ_LOAD_POST_LIST_EX = 189,
    RQ_LOAD_RECEIVED_POST_LIST_EX = 190,
    RQ_LOAD_SENT_POST_LIST_EX = 191,
    RQ_CACHE_TEST_EX = 192,
    RQ_LOAD_BLOCKED_IP_EX = 193,
    RQ_LOAD_PET_ITEM_COUNT_EX = 194,
    RQ_EX_RELOAD_CHAR = 195,
    RQ_SUMMON_SAVE_ABNORMALSTATUS_EX = 196,
    RQ_SUMMON_LOAD_ABNORMALSTATUS_EX = 197,
    RQ_SAVE_SUMMON_EX = 198,
    RQ_CHECK_RAID_WORLD_EX = 199,
    RQ_LOAD_ALLIANCE_EX = 200,
    RQ_SET_QUEST_NPC_LOG_EX = 201,
    RQ_LOAD_INZONE_FLAG_EX = 202,
    RQ_CREATE_INZONE_EX = 203,
    RQ_SAVE_INZONE_FLAG_EX = 204,
    RQ_LOAD_INZONE_MAX_DBID_EX = 205,
    RQ_SAVE_VOTE_SYSTEM_EX = 206,
    RQ_CLEAR_EVENT_TIME_EX = 207,
    RQ_ADD_POST_FRIEND_EX = 208,
    RQ_LOAD_POST_FRIEND_EX = 209,
    RQ_DELETE_POST_FRIEND_EX = 210,
    RQ_LOAD_OLYMPIAD_MATCH_COUNT_EX = 211,
    RQ_SAVE_OLYMPIAD_MATCH_COUNT_EX = 212,
    RQ_GET_OLYMPIAD_SEASON_PLAYER_COUNT_EX = 213,
    RQ_BR_GIVE_ITEM_EX = 214,
    RQ_BR_PLAYING_USER_EX = 215,
    RQ_BR_PRODUCT_SOLD_COUNT_EX = 216,
    RQ_BR_RECENT_PRODUCT_EX = 217,
    RQ_BR_INSERT_EVENT_RANKING_EX = 218,
    RQ_BR_LOAD_EVENT_TOP_RANKERS_EX = 219,
    RQ_BR_UPDATE_EVENT_RANKING_EX = 220,
    RQ_BR_GET_EVENT_SCORE_EX = 221,
    RQ_BR_SET_EVENT_SCORE_EX = 222,
    RQ_BR_LOAD_MINIGAME_SCORES_EX = 223,
    RQ_BR_INSERT_MINIGAME_SCORE_EX = 224,
    RQ_BR_GET_LECTURE_MARK_EX = 225,
    RQ_BR_SET_LECTURE_MARK_EX = 226,
    RQ_BR_SET_CREATE_DATE_EX = 227,
    RQ_BR_LOAD_DROP_ITEM_AMOUNT_PER_SERVER_EX = 228,
    RQ_BR_SAVE_DROP_ITEM_AMOUNT_PER_SERVER_EX = 229,
    RQ_BR_LOAD_ITEM_AGATHION_ENERGY_EX = 230,
    RQ_BR_SAVE_ITEM_AGATHION_ENERGY_EX = 231,
    RQ_BR_RESET_EVENT_DATA_EX = 232,
    RQ_BR_SET_EVENT_DATA_PERIOD_EX = 233,
    RQ_UPDATE_USER_RACE_SEX_EX = 234,
    RQ_LOAD_RESERVED_ANNOUNCE_EX = 235,
    RQ_ADD_RESERVED_ANNOUNCE_EX = 236,
    RQ_DELETE_RESERVED_ANNOUNCE_EX = 237,
    RQ_LOAD_EVENTDATA_EX = 238,
    RQ_RELOAD_EVENTDATA_EX = 239,
    RQ_DUMP_EVENTDATA_EX = 240,
    RQ_DELETE_SUMMON_EX = 241,
    RQ_ITEM_CHECK_EX = 242,
    RQ_DB_SECURITY_SETTING_EX = 243,
    RQ_SEND_BIRTHDAY_MAIL_EX = 244,
    RQ_DELETE_BIRTHDAY_MAIL_EX = 245,
    RQ_LOAD_GROUPTOGGLES_EX = 246,
    RQ_SAVE_GROUPTOGGLES_EX = 247,
    RQ_RESET_PROFILE_EX = 248,
    RQ_REPORT_PROFILE_EX = 249,
    RQ_SAVE_PLEDGE_WAR_POINT_EX = 250,
    RQ_DYNAMIC_QUEST_EX = 251,
    RQ_OTHER_USER_MACRO_EX = 252,
    RQ_OTHER_BUILDER_BOOKMARK_EX = 253,
    RQ_UPDATE_BLOCK_MEMO_EX = 254,
    RQ_UPDATE_FRIEND_MEMO_EX = 255,
    RQ_REQUEST_COMMISSION_LOAD_INFO_EX = 256,
    RQ_REQUEST_COMMISSION_LOAD_ALL_EX = 257,
    RQ_REQUEST_COMMISSION_REGISTER_EX = 258,
    RQ_REQUEST_COMMISSION_DELETE_EX = 259,
    RQ_REQUEST_COMMISSION_BUY_ITEM_EX = 260,
    RQ_REQUEST_COMMISSION_REGISTERED_LIST_EX = 261,
    RQ_REQUEST_COMMISSION_SEARCH_LIST_EX = 262,
    RQ_REQUEST_COMMISSION_DELETE_ALL_EX = 263,
    RQ_REQUEST_COMMISSION_REG_TOTAL_COUNT_EX = 264,
    RQ_INSTANT_AGIT_JOB_EX = 265,
    RQ_REQUEST_SAVE_VITALITY_EX = 266,
    RQ_SAVE_AGATHION_SUMMON_TIME = 267,
    RQ_SAVE_AGATHION_DESPAWN_TIME = 268,
    RQ_AGATHION_ELAPSED_TIME_REQ = 269,
    RQ_UPDATE_AGATHION_ELAPSED_TIME = 270,
    RQ_INIT_AGATHION_ELAPSED_TIME = 271,
    RQ_CHECK_CHAR_NAME_EX = 272,
    RQ_SAVE_RETRIEVE_GOODS = 273,
    RQ_SAVE_USER_JOB_GROUP = 274,
    RQ_SAVE_DAILY_PCCAFE_POINT_EX = 275,
    RQ_SET_DAILY_RESET_TIME_EX = 276,
    RQ_CLEAR_ONE_EVENT_TIME = 277,
    RQ_LOAD_TIME_TO_GIVE_KARMA_POINT_EX = 278,
    RQ_SAVE_TIME_TO_GIVE_KARMA_POINT_EX = 279,
    RQ_SAVE_CHAR_EXTRA_INFO_EX = 280,
    RQ_REQUEST_CHANGE_ATTRIBUTE_ITEM = 281,
    RQ_DELETE_AND_SAVE_SKILL_LIST_EX = 282,
    RQ_CANCEL_DUALCLASS_EX = 283,
    RQ_BR_CAN_BUY_SHOP_ITEM_EX = 284,
    RQ_BR_SEND_SHOP_GIFT_MAIL_EX = 285,
    RQ_GET_DAILY_COUNT_EX = 286,
    RQ_SET_DAILY_COUNT_EX = 287,
    RQ_CLEAR_DAILY_COUNT_EX = 288,
    RQ_ADD_MENTEE_EX = 289,
    RQ_LOAD_MENTEE_LIST_EX = 290,
    RQ_LOAD_MENTOR_EX = 291,
    RQ_UPDATE_MENTOR_STATUS_EX = 292,
    RQ_UPDATE_MENTEE_PLAYTIME_EX = 293,
    RQ_UPDATE_MENTOR_PLAYTIME_EX = 294,
    RQ_RESET_MENTOR_PENALTY_EX = 295,
    RQ_SHAPE_SHIFT_ITEM_EX = 296,
    RQ_SHAPE_SHIFT_ITEM_FIXED_EX = 297,
    RQ_RESTORE_SHAPE_SHIFT_ITEM_EX = 298,
    RQ_EXPIRE_SHAPE_SHIFT_ITEM_EX = 299,
    RQ_SET_CASTLE_CAMPAIGN_EX = 300,
    RQ_LOAD_PLEDGE_GAME_EX = 301,
    RQ_START_PLEDGE_GAME_EX = 302,
    RQ_END_PLEDGE_GAME_EX = 303,
    RQ_UPDATE_PLEDGE_GAME_RESULT_EX = 304,
    RQ_SET_PLEDGE_GAME_GOT_ITEM_EX = 305,
    RQ_INCREASE_PLEDGE_GAME_SURRENDER_COUNT_EX = 306,
    RQ_GET_PLEDGE_GAME_RECORD_EX = 307,
    RQ_SET_CASTLE_TYPE_EX = 308,
    RQ_DELETE_MACRO_ALL_EX = 309,
    RQ_MPCC_TEL_BOOKMARK_EX = 310,
    RQ_LOAD_KALIEEVENT_EX = 311,
    RQ_LOAD_KALIEEVENT_USER_EX = 312,
    RQ_UPDATE_KALIEEVENT_EX = 313,
    RQ_UPDATE_KALIEEVENT_USER_EX = 314,
    RQ_SET_COMPETE_AGIT_STATUS_EX = 315,
    RQ_SET_COMPETE_AGIT_WINNER_EX = 316,
    RQ_RESET_COMPETE_AGIT_EX = 317,
    RQ_LOAD_COMPETE_AGIT_EX = 318,
    RQ_LOAD_EXDROP_COUNT_EX = 319,
    RQ_UPDATE_EXDROP_COUNT_EX = 320,
    RQ_RESET_EXDROP_COUNT_EX = 321,
    RQ_SAVE_PLEDGE_WAR_POINT_PREV_EX = 322,
    RQ_UPDATE_PLEDGE_WAR_KILL_COUNT = 323,
    RQ_SET_PREVIOUS_AGIT_OWNER_ID_EX = 324,
    RQ_UPDATE_PLEDGE_NAMEVALUE_BY_PLEDGE_WAR_EX = 325,
    RQ_REQUEST_BEAUTY_LOAD_EX = 326,
    RQ_REQUEST_BUY_BEAUTY_ITEM_EX = 327,
    RQ_REQUEST_RESET_BEAUTY_ITEM_EX = 328,
    RQ_REQUEST_RESET_BEAUTY_LIST_EX = 329,
    RQ_EX_REQUEST_LOAD_NPC_QUEST_LOG = 330,
    RQ_RESET_WAR_TARGETED_EX = 331,
    RQ_EX_LEAVE_WORLD_SAVE_CHAR = 332,
    RQ_EX_LEAVE_WORLD_SAVE_SKILL_USE = 333,
    RQ_EX_LEAVE_WORLD_SAVE_ABNORMALSTATUS = 334,
    RQ_LOAD_MENTEE_STATE_EX = 335,
    RQ_CANCEL_EQUIPMENT_ADDED_EFFECT_AND_DROP = 336,
    RQ_EX_CHAR_LEVEL_UP = 337,
    RQ_BR_INTERESTED_PRODUCT_EX = 338,
    RQ_BR_ADD_INTERESTED_PRODUCT_EX = 339,
    RQ_BR_DELETE_INTERESTED_PRODUCT_EX = 340,
    RQ_PERIOD_SHAPE_SHIFTED_ITEM_JOB_EX = 341,
    RQ_PLEDGE_JOIN_SYS_INIT = 342,
    RQ_LOAD_PLEDGE_ON_PLEDGE_RECRUIT_BOARD = 343,
    RQ_WRITE_PLEDGE_RECRUIT_BOARD = 344,
    RQ_MODIFY_PLEDGE_RECRUIT_BOARD = 345,
    RQ_DELETE_PLEDGE_RECRUIT_BOARD = 346,
    RQ_SEARCH_PLEDGE_RECRUIT_BOARD = 347,
    RQ_MODIFY_PLEDGE_INFO_ON_BOARD = 348,
    RQ_PLEDGE_ON_PLEDGE_RECRUIT_BOARD = 349,
    RQ_PLEDGE_WAITING_LIST_APPLIED = 350,
    RQ_WRITE_PLEDGE_WAITING_USER_LIST = 351,
    RQ_DELETE_PLEDGE_WAITING_USER_LIST = 352,
    RQ_PLEDGE_WAITING_USER_LIST = 353,
    RQ_PLEDGE_WAITING_USER = 354,
    RQ_WRITE_PLEDGE_DRAFT_USER_LIST = 355,
    RQ_DELETE_PLEDGE_DRAFT_USER_LIST = 356,
    RQ_PLEDGE_DRAFT_USER_LIST = 357,
    RQ_PLEDGE_RECRUIT_APPLY_INFO = 358,
    RQ_PLEDGE_JOIN_SYSTEM_CODE = 359,
    RQ_MODIFY_APPLIED_USER_INFO = 360,
    RQ_UPDATE_MAX_LEVEL_EX = 361,
    RQ_SAVE_PETITION_EXTERNAL_NOTIFY = 362,
    RQ_DELETE_PETITION_EXTERNAL_NOTIFY = 363,
    RQ_LOAD_PETITION_EXTERNAL_NOTIFY = 364,
    RQ_SET_DORMANT_GIFT_TIME_EX = 365,
    RQ_LOAD_ACCOUNT_MAX_LEVEL = 366,
    RQ_RELOAD_USER_FLAG = 367,
    RQ_REGISTER_XMAS_WISH = 368,
    RQ_CONFIRM_XMAS_WISH = 369,
    RQ_GET_DORMANT_GIFT_INFO = 370,
    RQ_MANIPULATE_ITEM_NEW = 371,
    RQ_DIVIDE_ADENA_EX = 372,
    RQ_REQUEST_BEAUTY_PURCHASE_LIST = 373,
    RQ_UPDATE_HAIR_FLAG = 374,
    RQ_ACQUIRE_POTENTIAL_SKILL = 375,
    RQ_REQUEST_RESET_POTENTIAL = 376,
    RQ_EX_CHANGE_AP = 377,
    RQ_START_NEXT_FISHING_CHAMPIONSHIP = 378,
    RQ_SAVE_FISHING_CHAMPIONSHIP_RESULT = 379,
    RQ_RESET_FISHING_CHAMPIONSHIP = 380,
    RQ_GIVE_FISHING_CHAMPIONSHIP_REWARD = 381,
    RQ_BR_START_TRAINING_ZONE_EX = 382,
    RQ_BR_END_TRAINING_ZONE_EX = 383,
    RQ_BR_REWARD_TRAINING_ZONE_EX = 384,
    RQ_BR_LOAD_TRAINING_ZONE_EX = 385,
    RQ_PERIOD_PREMIUM_HENNA_JOB_EX = 386,
    RQ_BR_RESET_SHOP_LIMIT = 387,
    RQ_ACCOUNT_STAMP_INFO = 388,
    RQ_ACCOUNT_STAMP_BUILDER = 389,
    RQ_DELETE_ACCOUNT_STAMP_BUILDER = 390,
    RQ_LOAD_WORLDCHAT_POINT = 391,
    RQ_INC_USED_WORLDCHAT_POINT = 392,
    RQ_RESET_WORLDCHAT_POINT = 393,
    RQ_LOAD_WORLDCHAT_TIME = 394,
    RQ_EX_LOAD_MATCHING_POINT = 395,
    RQ_EX_SAVE_MATCHING_POINT = 396,
    RQ_EX_CREATE_DUALJOB = 397,
    RQ_KAMAROKA_ROUND = 398,
    RQ_KAMAROKA_RANKING_LIST = 399,
    RQ_KAMAROKA_ADD_NEW_RECORD = 400,
    RQ_KAMAROKA_PERSONAL_RANK_INFO = 401,
    RQ_UPDATE_UI_COUNT = 402,
    RQ_LOAD_CLIENT_INI = 403,
    RQ_SAVE_CLIENT_INI = 404,
    RQ_EX_LOAD_DROIDNPC_APPEARANCE = 405,
    RQ_EX_LOAD_HONORS_SKILL = 406,
    RQ_EX_SET_HONORS_SKILL = 407,
    RQ_EX_DELETE_HONORS_SKILL = 408,
    RQ_EX_RESET_HONORS_SKILL = 409,
    RQ_UPDATE_WEDDING_EX = 410,
    RQ_LOAD_WEDDING_EX = 411,
    RQ_EX_LOAD_FRIEND_DETAIL_INFO = 412,
    RQ_EX_SIGNIN_PLEDGE = 413,
    RQ_EX_LOAD_FACTION_POINT = 414,
    RQ_EX_UPDATE_FACTION_POINT = 415,
    RQ_EX_LOAD_BALTHUS_EVENT_USER_INFO = 416,
    RQ_EX_UPDATE_BALTHUS_EVENT_USER_INFO = 417,
    RQ_EX_UPDATE_USER_INZONE_POINT = 418,
    RQ_EX_LOAD_USER_INZONE_POINT = 419,
    RQ_EX_WOLYMPIAD_LOAD_ALL_USER = 420,
    RQ_EX_WOLYMPIAD_GET_TOP10 = 421,
    RQ_EX_WOLYMPIAD_GET_MATCH_COUNT = 422,
    RQ_EX_WOLYMPIAD_GET_ACHIEVEMENTS = 423,
    RQ_EX_WOLYMPIAD_GET_MATCH_RESULT = 424,
    RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD = 425,
    RQ_EX_SAVE_CLASS_CATEGORY_DATA = 426,
    RQ_EX_UPDATE_ONE_DAY_REWARD_INFO = 427,
    RQ_EX_LOAD_AUTO_SOULSHOT_UI = 428,
    RQ_EX_SAVE_AUTO_SOULSHOT_UI = 429,
    RQ_EX_INIT_USER_PLEDGE_BONUS = 430,
    RQ_EX_UPDATE_USER_PLEDGE_BONUS = 431,
    RQ_EX_LOAD_USER_PLEDGE_BONUS = 432,
    RQ_EX_UPDATE_USER_PLEDGE_BONUS_REWARD = 433,
    RQ_EX_UPDATE_USER_MONSTER_BOOK = 434,
    RQ_EX_LOAD_USER_MONSTER_BOOK = 435,
    RQ_EX_REQUEST_UPGRADE_SYSTEM = 436,
    RQ_EX_LOAD_CARD_UPDOWN_RECORD = 437,
    RQ_EX_UPDATE_CARD_UPDOWN_RECORD = 438,
    RQ_EX_PLEDGE_MANIPULATE_ITEMS = 439,
    RQ_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO = 440,
    RQ_EX_PLEDGE_CONTRIBUTION_RANK = 441,
    RQ_EX_PLEDGE_ITEM_LOAD = 442,
    RQ_EX_PLEDGE_ITEM_SAVE = 443,
    RQ_EX_PLEDGE_ITEM_DELETE = 444,
    RQ_EX_PLEDGE_RAID_GET_INFO_LIST = 445,
    RQ_EX_PLEDGE_RAID_INFO_UPDATE_REWARD_REASON = 446,
    RQ_EX_PLEDGE_RAID_INZONE_LOAD = 447,
    RQ_EX_PLEDGE_MISSION_LOAD = 448,
    RQ_EX_PLEDGE_MISSION_ADD_COUNT = 449,
    RQ_EX_PLEDGE_ANNOUNCE = 450,
    RQ_EX_PLEDGE_ANNOUNCE_SET = 451,
    RQ_EX_SEQUENCE_JOB = 452,
    RQ_EX_BUILDER_COMMAND = 453,
    RQ_EX_LOCKED_ITEM = 454,
    RQ_EX_SET_ITEM_REUSE_DELAY = 455,
    RQ_EX_PLEDGE_MANIPULATE_ITEMS_NEW = 456,
    RQ_EX_LOAD_RESERVATION_ANNOUNCE = 457,
    RQ_EX_DELETE_RESERVATION_ANNOUNCE = 458,
    RQ_EX_SET_RESERVATION_ANNOUNCE = 459,
    RQ_EX_SHOW_RESERVATION_ANNOUNCE = 460,
    RQ_EX_MAX = 461,
};

class CSocket;
class CUserSocket;

using PacketFunc = bool (*)(CSocket*, const std::uint8_t*);


namespace network {
struct IPacket;
}

DefaultIOBuffer* MakeConstBufferFromPacketEx(char packetId, std::int16_t extendedPacketId, const network::IPacket& packet);


extern std::atomic<std::int32_t> g_nSocketPendingWrite;
extern std::int32_t g_nTotalOutgoingPacketCount;

#if defined(L2_BUILD_L2SERVER)
extern std::int32_t g_nTotalOutgoingNpcPacketCount;

// Current L2Server ioc.obj qword counters at 04d793f8/04d79400. They are
// updated with interlocked exchange-add by
// CUserSocket::AccumulateWorldChatPacket and read by the IO report path.
extern std::int64_t g_WorldChatPacketSendCount;
extern std::int64_t g_WorldChatPacketSendSize;
#endif

#if defined(L2_BUILD_CACHED)
// Current Cached.exe Shared/ioc.h diagnostics at 0x01c6b888..0x01c6b8c0.
// Their original identifiers are absent from the usable PDB. The semantic
// names below keep the exact eight interlocked qword counters read by
// Cached.cpp's packet-buffer report.
extern std::atomic<std::int64_t> g_nSocketSendBytes;
extern std::atomic<std::int64_t> g_nSocketReceiveBytes;
extern std::atomic<std::int64_t> g_nSocketSendCount;
extern std::atomic<std::int64_t> g_nSocketReceiveCount;
extern std::atomic<std::int64_t> g_nSocketSendBytesOver7K;
extern std::atomic<std::int64_t> g_nSocketReceiveBytesOver7K;
extern std::atomic<std::int64_t> g_nSocketSendCountOver7K;
extern std::atomic<std::int64_t> g_nSocketReceiveCountOver7K;
#endif

// Shared/ioc.h CISocket. The PDB records this as the fieldless 0x18-byte
// interface between CIOObject and every CIOSocketEx specialization. It owns
// the socket-specific virtual contract while CIOObject owns the storage and
// the timer/IO callback prefix.
class CISocket : public CIOObject {
public:
    virtual void Initialize() = 0;
    virtual void OnCreate() = 0;
    virtual bool CheckPendingWrite() = 0;
    virtual bool CheckPendingWriteSafe(int pendingWrite) const = 0;
    virtual void Send(RQ_PROTOCOL protocol, RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet) = 0;
    virtual void Send(const char* format, ...) = 0;
    virtual void SendV(const char* format, va_list arguments) = 0;
    virtual void GracefulClose() = 0;
    virtual void OnClose() = 0;
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override = 0;
    virtual void OnRead() = 0;
};


class CIOSocketSerializedWriteControl;
class CIOSocketCriticalSectionWriteControl;

template <typename TBuffer, typename TWriteControl = CIOSocketSerializedWriteControl>
class CIOSocketEx;

namespace portable {

struct CIOSocketTransportAccess {
    template <typename TBuffer, typename TWriteControl>
    static bool FeedRead(CIOSocketEx<TBuffer, TWriteControl>* socket, const std::uint8_t* data, std::size_t length);

    template <typename TBuffer, typename TWriteControl>
    static void FeedTerminal(CIOSocketEx<TBuffer, TWriteControl>* socket, int error);
};

template <typename TBuffer, typename TWriteControl>
void ExecuteCIOSocketAsyncWrite(MemoryObject* owner, void* argument);

template <typename TBuffer, typename TWriteControl>
void ExecuteCIOSocketWriteCallback(MemoryObject* owner, void* argument);

template <typename TBuffer, typename TWriteControl>
bool FeedSocketRead(CIOSocketEx<TBuffer, TWriteControl>* socket, const std::uint8_t* data, std::size_t length);

template <typename TBuffer, typename TWriteControl>
void FeedSocketTerminal(CIOSocketEx<TBuffer, TWriteControl>* socket, int error);

template <typename TBuffer, typename TWriteControl>
void CancelSocketReads(CIOSocketEx<TBuffer, TWriteControl>* socket);

} // namespace portable

// Exact empty lock selected by the target's serialized socket build. The
// methods do nothing. CExecutionSerializerFast owns ordering.
class CIODummyLock {
public:
    void Enter(const wchar_t*, int) {}
    void Leave(const wchar_t*, int) {}
    void Leave() {}
    bool IsEntered() const { return true; }
};


// Shared/ioc.h was compiled with two different write-control members in the
// shipping products recovered here. Current L2NPC has the 0x30-byte
// CExecutionSerializerFast member. Current Cached.exe 0x0047affc and
// 0x0042f110 instead have a 0x28-byte CIOCriticalSection at the same
// source-level position. Policy carriers let both executables consume this
// one physical Shared template without falsifying either observed layout.
class CIOSocketSerializedWriteControl {
public:
    static constexpr bool CallerRetainsSyncWrite = false;

    explicit CIOSocketSerializedWriteControl(int limit) : m_serializer(limit) {}

    int AddExecutable(
        MemoryObject* owner, CExecutionSerializerFast::Executable executable, void* argument, const wchar_t* executableName
    ) {
        return m_serializer.AddExecutable(owner, executable, argument, executableName);
    }

    void Enter(const wchar_t*, int) {}
    void Leave(const wchar_t*, int) {}

private:
    CExecutionSerializerFast m_serializer;
};


class CIOSocketCriticalSectionWriteControl {
public:
    static constexpr bool CallerRetainsSyncWrite = true;

    explicit CIOSocketCriticalSectionWriteControl(int) {}

    int AddExecutable(MemoryObject* owner, CExecutionSerializerFast::Executable executable, void* argument, const wchar_t*) {
        {
            L2_GUARD(*this);
            executable(owner, argument);
        }
        return 1;
    }

    void Enter(const wchar_t* file, int line) { m_lock.Enter(file, line); }

    void Leave(const wchar_t* file, int line) { m_lock.Leave(file, line); }

private:
    CIOCriticalSection m_lock;
};


// Shared/ioc.h: CIOSocketEx<CIOBufferEx<20480>>. The virtual order through
// OnRead is confirmed by the current L2NPC vtable at 0x005a5748 and by the
// symbol-complete NASC64_2015 donor. The byte fields keep the target's
// CExecutionSerializerFast and two Win32 OVERLAPPED storage regions. Only
// their platform transport is replaced in the portable process.
template <typename TBuffer, typename TWriteControl>
class CIOSocketEx : public CISocket {
public:
    explicit CIOSocketEx(std::uint64_t socket = 0, int serializerLimit = 0) : m_WriteControl(serializerLimit) {
        L2_GUARD_FUNCTION();
        m_hSocket.store(socket, std::memory_order_relaxed);
        std::memset(m_overlappedRead.data(), 0, m_overlappedRead.size());
        std::memset(m_overlappedWrite.data(), 0, m_overlappedWrite.size());
        m_pReadBuf = TBuffer::Alloc();
        m_nPendingWrite.store(0, std::memory_order_relaxed);
        m_pLastBuf = nullptr;
        m_pFirstBuf = nullptr;
    }

    ~CIOSocketEx() override {
        L2_GUARD_FUNCTION();
        if (m_pReadBuf) {
            m_pReadBuf->Release();
            m_pReadBuf = nullptr;
        }
        while (m_pFirstBuf) {
            TBuffer* const completed = m_pFirstBuf;
            m_pFirstBuf = completed->m_pNext;
            g_nSocketPendingWrite.fetch_sub(static_cast<std::int32_t>(completed->m_dwSize), std::memory_order_acq_rel);
            completed->Release();
        }
    }

#if defined(L2_BUILD_L2SERVER)
private:
    L2_DECLARE_OBJECT_REGISTRATION(CIOSocketEx)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CIOSocketEx)

public:
    static int __GetClassID() noexcept { return L2_MEMORY_CLASS_ID(CIOSocketEx); }

    static const wchar_t* __GetClassName() noexcept { return m_pClassName_CIOSocketEx; }

    static void* operator new(std::size_t requestedSize, bool objectAllocation) {
        return runtime::process::AllocateMemoryObject(
            requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(CIOSocketEx), __GetClassName(), "CIOSocketEx",
            0x80 + sizeof(TWriteControl)
        );
    }

    static void* operator new(std::size_t requestedSize) { return operator new(requestedSize, true); }

    L2_DECLARE_OBJECT_ADD_REF()
    L2_DECLARE_OBJECT_RELEASE(CIOSocketEx)
#else
    L2_DECLARE_OBJECT_TYPE(CIOSocketEx)
#endif

protected:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override {
        // Current Cached.exe versions are 0x0042fa40 for the
        // 20480-byte buffer and 0x004dbe08 for the 8192-byte buffer.
        PortableOverlapped* const readOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedRead.data());
        PortableOverlapped* const writeOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedWrite.data());

        // Current EXE 0x00421734, donor Shared/ioc.h:781-811.
        if (!success) {
            if (overlapped == readOverlapped) {
                // LogIOError(
                //     L"CIOSocketEx::OnIOCallback "
                //     L"bSuccess false ErrorCode[%d]",
                //     GetSocketLastError())
                // portable::ReportSocketReadFailure(m_hSocket.load(std::memory_order_acquire), GetSocketLastError())
                const int error = GetSocketLastError();
                portable::ReportSocketFailureDiagnostics(
                    m_hSocket.load(std::memory_order_acquire), error, m_nPendingWrite.load(std::memory_order_relaxed)
                );
                portable::ReportSocketReadFailure(m_hSocket.load(std::memory_order_acquire), error);
                Close();
            }
            if (overlapped == writeOverlapped) {
                const int error = GetSocketLastError();
                portable::ReportSocketFailureDiagnostics(
                    m_hSocket.load(std::memory_order_acquire), error, m_nPendingWrite.load(std::memory_order_relaxed)
                );
                if (!IsExpectedSocketCloseError(error)) {
                    // LogIOError(
                    //     "[network] Asynchronous write failed on connection %llu (socket error %d, transferred %u bytes)
                    //     releasing its IO reference without advancing the write queue.", static_cast<unsigned long
                    //     long>(m_hSocket.load(std::memory_order_acquire)), error, static_cast<unsigned>(transferred))
                    L2_LOG_IO_ERROR(
                        "[network] Asynchronous write failed on connection %llu (socket error %d; transferred %u bytes); "
                        "releasing its IO reference without advancing the write queue.",
                        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error,
                        static_cast<unsigned>(transferred)
                    );
                }
            }
            L2_RELEASE(this, ObjectRefType::IoRetain);
            return;
        }
        if (!overlapped) {
            OnTimer(static_cast<int>(transferred));
            L2_RELEASE(this, ObjectRefType::Timer);
            return;
        }
        if (overlapped == readOverlapped) {
            ReadCallback(transferred);
            L2_RELEASE(this, ObjectRefType::IoRetain);
            return;
        }
        if (overlapped == writeOverlapped) {
            WriteCallback(transferred);
            L2_RELEASE(this, ObjectRefType::IoRetain);
            return;
        }
        // LogIOError(
        //     L"[%s][%d] Invalid overlapped!!!",
        //     L"C:\\Work\\Lineage2Build\\Server\\Main\\shared\\ioc.h",
        //     0x328)
        // LogIOError(
        //     "[network] Unrecognized IO completion on connection %llu (completion %p, read %p, write %p. Transferred %u
        //     bytes), no callback is dispatched or IO reference released.", static_cast<unsigned long
        //     long>(m_hSocket.load(std::memory_order_acquire)), static_cast<void*>(overlapped),
        //     static_cast<void*>(readOverlapped), static_cast<void*>(writeOverlapped), static_cast<unsigned>(transferred))
        L2_LOG_IO_ERROR(
            "[network] Unrecognized IO completion on connection %llu (completion %p, read %p, write %p; transferred %u "
            "bytes); no callback is dispatched or IO reference released.",
            static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), static_cast<void*>(overlapped),
            static_cast<void*>(readOverlapped), static_cast<void*>(writeOverlapped), static_cast<unsigned>(transferred)
        );
    }

public:
    void Initialize() override {
        L2_GUARD_FUNCTION();
        // Current EXE 0x004201a8 sets SO_RCVBUF and SO_SNDBUF to zero before
        // attaching the socket to IOCP. Association failure enters Close.
        // only success reaches virtual OnCreate.
        if (!portable::InitializeSocketTransport(this)) {
            // LogIOError(
            //     "[network] Cannot initialize the IO transport for connection %llu. Closing before the creation callback.",
            //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_IO_ERROR(
                "[network] Cannot initialize the IO transport for connection %llu; closing before the creation callback.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
            Close();
        } else {
            OnCreate();
        }
    }

    void OnCreate() override {
        L2_GUARD_FUNCTION();
        OnRead();
    }

    bool CheckPendingWrite() override { return true; }

    bool CheckPendingWriteSafe(int) const override { return true; }

    void Send(RQ_PROTOCOL, RQ_EX_PROTOCOL, network::IPacket&) override {}

    void Send(const char*, ...) override {
        // Both current Cached.exe specializations fold this unused virtual
        // default to the empty body at 0x0047b23c.
    }

    void SendV(const char*, va_list) override {}

    void GracefulClose() override {
        L2_GUARD_FUNCTION();
        const std::uint64_t previous = m_hSocket.exchange(UINT64_C(0xffffffffffffffff), std::memory_order_acq_rel);
        if (previous != UINT64_C(0xffffffffffffffff)) {
            OnClose();
            portable::GracefulCloseSocketTransport(this);
            // Noise: Closing a connection is a normal lifecycle operation. A stack dump on every close obscures actual
            // failures. ReportIOCloseCallStack()
            L2_RELEASE(this, ObjectRefType::CreateDelete);
        }
    }

protected:
    void OnClose() override {}
    void OnRead() override = 0;

public:
    void Close() {
        L2_GUARD_FUNCTION();
        const std::uint64_t previous = m_hSocket.exchange(UINT64_C(0xffffffffffffffff), std::memory_order_acq_rel);
        if (previous != UINT64_C(0xffffffffffffffff)) {
            OnClose();
            // The target sets SO_LINGER={1,0} and calls closesocket here.
            // Native handles remain outside the fixed target
            // object, so the portable binding owns that OS-specific pair.
            portable::CloseSocketTransport(this);
            // Noise: Closing a connection is a normal lifecycle operation. A stack dump on every close obscures actual
            // failures. ReportIOCloseCallStack()
            L2_RELEASE(this, ObjectRefType::CreateDelete);
        }
    }

    // Exact CIOSocketEx::Read compaction used by all three target parsers.
    // dwLeft is the number of trailing bytes to keep, not the number to
    // consume. A posted CPacketEx may still reference the old buffer, so a
    // shared buffer is replaced before the kept bytes are moved.
    void Read(std::uint32_t dwLeft) {
        static_assert(TBuffer::MaxSize == 20480 || TBuffer::MaxSize == 32768 || TBuffer::MaxSize == 8192);
        L2_GUARD_FUNCTION();
        TBuffer* readBuffer = runtime::raw_atomic::LoadPointerAcquire(&m_pReadBuf);
        const std::uint32_t compactedSize = runtime::raw_atomic::LoadU32Relaxed(&readBuffer->m_dwSize) - dwLeft;
        runtime::raw_atomic::StoreU32Relaxed(&readBuffer->m_dwSize, compactedSize);
        {
            GuardHelper readCopy(L"read_copy", CThreadManager::GetThreadID());
            const std::uint32_t sourceOffset = compactedSize;
            if (readBuffer->m_nRefCount.load(std::memory_order_acquire) < 2) {
                std::memcpy(readBuffer->m_Buffer.data(), readBuffer->m_Buffer.data() + sourceOffset, dwLeft);
            } else {
                GuardHelper readAlloc(L"read_alloc", CThreadManager::GetThreadID());
                TBuffer* const replacement = TBuffer::Alloc();
                std::memcpy(replacement->m_Buffer.data(), readBuffer->m_Buffer.data() + sourceOffset, dwLeft);
                readBuffer->Release();
                runtime::raw_atomic::StorePointerRelease(&m_pReadBuf, replacement);
                readBuffer = replacement;
            }
            runtime::raw_atomic::StoreU32Relaxed(&readBuffer->m_dwSize, dwLeft);
        }
        // The current target takes one IoRetain immediately before posting
        // each WSARecv. The native reader is outside the fixed object on
        // portable hosts, but its completion must keep the same lifetime.
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        if (!portable::ArmSocketReadTransport(
                this, readBuffer->m_Buffer.size() - runtime::raw_atomic::LoadU32Relaxed(&readBuffer->m_dwSize)
            )) {
            const int firstError = GetSocketLastError();
            if (firstError != 997) {
                const int error = GetSocketLastError();
                if (!IsExpectedSocketCloseError(error)) {
                    // Noise: The truncated owner pointer does not identify the connection. Report the full socket handle and
                    // read capacity. const unsigned int socketValue =
                    //     static_cast<unsigned int>(
                    //         m_hSocket.load(
                    //             std::memory_order_acquire))
                    // const unsigned int ownerValue =
                    //     static_cast<unsigned int>(
                    //         reinterpret_cast<
                    //             std::uintptr_t>(this))
                    const std::uint64_t socketValue = m_hSocket.load(std::memory_order_acquire);
                    // LogIOError(
                    //     L"CIOSocket::Read %x(%x) err = %d",
                    //     socketValue,
                    //     ownerValue,
                    //     error)
                    portable::ReportSocketReadArmFailure(socketValue, error, dwLeft, TBuffer::MaxSize);
                    // Noise: ReportSocketReadArmFailure already emits one ERROR record and keeps the local copy.
                    // LogIOLocalError(
                    //     L"CIOSocket::Read %x(%x) err = %d",
                    //     socketValue,
                    //     ownerValue,
                    //     error)
                }
                Close();
                L2_RELEASE(this, ObjectRefType::IoRetain);
            }
        }
    }

    std::int32_t PendingWrite() { return m_nPendingWrite.load(std::memory_order_relaxed); }

    std::uint32_t GetReadBufSize() {
        TBuffer* const readBuffer = runtime::raw_atomic::LoadPointerAcquire(&m_pReadBuf);
        return readBuffer ? runtime::raw_atomic::LoadU32Relaxed(&readBuffer->m_dwSize) : 0;
    }

    TBuffer* AllocBuffer() { return TBuffer::Alloc(); }

protected:
    void ReadCallback(std::uint32_t transferred) {
        // Current L2NPC.exe 0x00420350. Cached.exe instantiates the same
        // source body for CIOBufferEx<20480> at 0x00411828 and
        // CIOBufferEx<8192> at 0x004db0c0.
        if (transferred == 0) {
            // LogIOError(
            //     L"CIOSocketEx::ReadCallback "
            //     L"dwTransferred zero")
            portable::ReportSocketPeerClose(m_hSocket.load(std::memory_order_acquire));
            Close();
            return;
        }
        if (RefCount() < 1) {
            // LogIOError(
            //     L"invalid socket!")
            // LogIOError(
            //     "[network] Received %u bytes on connection %llu whose reference count is below one. The read callback is
            //     skipped.", static_cast<unsigned>(transferred), static_cast<unsigned long
            //     long>(m_hSocket.load(std::memory_order_acquire)))
            L2_LOG_IO_ERROR(
                "[network] Received %u bytes on connection %llu whose reference count is below one; the read callback is "
                "skipped.",
                static_cast<unsigned>(transferred),
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire))
            );
            return;
        }
        TBuffer* const readBuffer = runtime::raw_atomic::LoadPointerAcquire(&m_pReadBuf);
        const std::uint32_t readBufferSize32 = runtime::raw_atomic::LoadU32Relaxed(&readBuffer->m_dwSize) + transferred;
        runtime::raw_atomic::StoreU32Relaxed(&readBuffer->m_dwSize, readBufferSize32);
#if defined(L2_BUILD_CACHED)
        // Current Cached.exe accounts the accumulated read-buffer size at
        // each completed WSARecv, immediately before dispatching OnRead.
        const std::int64_t readBufferSize = static_cast<std::int64_t>(readBufferSize32);
        g_nSocketReceiveBytes.fetch_add(readBufferSize, std::memory_order_acq_rel);
        g_nSocketReceiveCount.fetch_add(1, std::memory_order_acq_rel);
        if (readBufferSize > 7000) {
            g_nSocketReceiveBytesOver7K.fetch_add(readBufferSize, std::memory_order_acq_rel);
            g_nSocketReceiveCountOver7K.fetch_add(1, std::memory_order_acq_rel);
        }
#endif
        OnRead();
    }

public:
    void _Write(TBuffer* buffer) {
        // Current EXE 0x00536a40. Donor Shared/ioc.h:1085-1169.
        ReleaseHelper<CIOSocketEx<TBuffer, TWriteControl>, ObjectRefType::IoSocketWrite> releaseHelper(
            this, L2_SOURCE_FILE, __LINE__
        );
        if (buffer->m_dwSize == 0) {
            buffer->Release();
            return;
        }

        const std::int32_t bufferSize = static_cast<std::int32_t>(buffer->m_dwSize);
        m_nPendingWrite.fetch_add(bufferSize, std::memory_order_acq_rel);
        if (!CheckPendingWrite()) {
            buffer->Release();
            Close();
            return;
        }

        g_nSocketPendingWrite.fetch_add(bufferSize, std::memory_order_acq_rel);
        if (!m_pLastBuf) {
            m_pFirstBuf = buffer;
            m_pLastBuf = buffer;
            L2_ADD_REF(this, ObjectRefType::IoRetain);
            PortableOverlapped* const writeOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedWrite.data());
#if defined(L2_BUILD_CACHED)
            // These counters surround the actual WSASend in the target. A
            // buffer merely queued or coalesced below is not counted yet.
            const std::int64_t submittedSize = static_cast<std::int64_t>(buffer->m_dwSize);
            g_nSocketSendBytes.fetch_add(submittedSize, std::memory_order_acq_rel);
            g_nSocketSendCount.fetch_add(1, std::memory_order_acq_rel);
            if (submittedSize > 7000) {
                g_nSocketSendBytesOver7K.fetch_add(submittedSize, std::memory_order_acq_rel);
                g_nSocketSendCountOver7K.fetch_add(1, std::memory_order_acq_rel);
            }
#endif
            const portable::SocketWriteSubmission submission = portable::SubmitSocketWrite(
                this, buffer->m_Buffer.data(), static_cast<int>(buffer->m_dwSize), writeOverlapped
            );
            if (submission.IsFailed()) {
                const int error = GetSocketLastError();
                portable::ReportSocketFailureDiagnostics(
                    m_hSocket.load(std::memory_order_acquire), error, m_nPendingWrite.load(std::memory_order_relaxed)
                );
                if (!IsExpectedSocketCloseError(error)) {
                    // LogIOError(
                    //     "CIOSocket::Write %x(%x) "
                    //     "err=%d",
                    //     static_cast<unsigned int>(
                    //         m_hSocket.load(
                    //             std::memory_order_acquire)),
                    //     static_cast<unsigned int>(
                    //         reinterpret_cast<
                    //             std::uintptr_t>(
                    //                 this)),
                    //     error)
                    // LogIOError(
                    //     "[network] Cannot submit the first write buffer of %u bytes on connection %llu (socket error %d)
                    //     dropping the buffer and clearing the write queue.", static_cast<unsigned>(buffer->m_dwSize),
                    //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error)
                    L2_LOG_IO_ERROR(
                        "[network] Cannot submit the first write buffer of %u bytes on connection %llu (socket error %d); "
                        "dropping the buffer and clearing the write queue.",
                        static_cast<unsigned>(buffer->m_dwSize),
                        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error
                    );
                }
                // Noise: The unexpected send error above has context. Expected close errors should not produce a second
                // error record. LogIOError(
                //     L"[%s][%d] CIOSocket::_Write - "
                //     L"WSASend fail. error(%d)",
                //     L"C:\\Work\\Lineage2Build\\Server\\Main\\shared\\ioc.h",
                //     0x47e,
                //     error)
                m_pFirstBuf = nullptr;
                m_pLastBuf = nullptr;
                buffer->Release();
                L2_RELEASE(this, ObjectRefType::IoRetain);
            } else if (submission.IsCompleted()) {
                if (!portable::PostIOCompletion(this, submission.transferred, writeOverlapped)) {
                    OnIOCallback(1, submission.transferred, writeOverlapped);
                }
            }
        } else if (m_pFirstBuf != m_pLastBuf &&
            m_pLastBuf->m_dwSize + buffer->m_dwSize <= static_cast<std::uint32_t>(TBuffer::MaxSize)) {
            std::memcpy(m_pLastBuf->m_Buffer.data() + m_pLastBuf->m_dwSize, buffer->m_Buffer.data(), buffer->m_dwSize);
            m_pLastBuf->m_dwSize += buffer->m_dwSize;
            buffer->Release();
        } else {
            m_pLastBuf->m_pNext = buffer;
            m_pLastBuf = buffer;
        }
    }

    void __async_AsyncWrite(void* argument) {
        L2_GUARD_FUNCTION();
        auto* const buffer = static_cast<TBuffer*>(argument);
        ReleaseHelper<CIOSocketEx<TBuffer, TWriteControl>, ObjectRefType::IoSocketWrite> releaseHelper(
            this, L2_SOURCE_FILE, __LINE__
        );
        if (buffer->m_dwSize == 0) {
            buffer->Release();
            return;
        }

        const std::int32_t bufferSize = static_cast<std::int32_t>(buffer->m_dwSize);
        m_nPendingWrite.fetch_add(bufferSize, std::memory_order_acq_rel);
        if (!CheckPendingWrite()) {
            buffer->Release();
            Close();
            return;
        }

        g_nSocketPendingWrite.fetch_add(bufferSize, std::memory_order_acq_rel);
        if (!m_pLastBuf) {
            m_pFirstBuf = buffer;
            m_pLastBuf = buffer;
            L2_ADD_REF(this, ObjectRefType::IoRetain);
            PortableOverlapped* const writeOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedWrite.data());
#if defined(L2_BUILD_CACHED)
            const std::int64_t submittedSize = static_cast<std::int64_t>(buffer->m_dwSize);
            g_nSocketSendBytes.fetch_add(submittedSize, std::memory_order_acq_rel);
            g_nSocketSendCount.fetch_add(1, std::memory_order_acq_rel);
            if (submittedSize > 7000) {
                g_nSocketSendBytesOver7K.fetch_add(submittedSize, std::memory_order_acq_rel);
                g_nSocketSendCountOver7K.fetch_add(1, std::memory_order_acq_rel);
            }
#endif
            const portable::SocketWriteSubmission submission = portable::SubmitSocketWrite(
                this, buffer->m_Buffer.data(), static_cast<int>(buffer->m_dwSize), writeOverlapped
            );
            if (submission.IsFailed()) {
                const int error = GetSocketLastError();
                portable::ReportSocketFailureDiagnostics(
                    m_hSocket.load(std::memory_order_acquire), error, m_nPendingWrite.load(std::memory_order_relaxed)
                );
                if (!IsExpectedSocketCloseError(error)) {
                    // LogIOError(
                    //     "CIOSocket::Write %x(%x) "
                    //     "err=%d",
                    //     static_cast<unsigned int>(
                    //         m_hSocket.load(
                    //             std::memory_order_acquire)),
                    //     static_cast<unsigned int>(
                    //         reinterpret_cast<
                    //             std::uintptr_t>(
                    //                 this)),
                    //     error)
                    // LogIOError(
                    //     "[network] Cannot submit the first write buffer of %u bytes on connection %llu (socket error %d)
                    //     dropping the buffer and clearing the write queue.", static_cast<unsigned>(buffer->m_dwSize),
                    //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error)
                    L2_LOG_IO_ERROR(
                        "[network] Cannot submit the first write buffer of %u bytes on connection %llu (socket error %d); "
                        "dropping the buffer and clearing the write queue.",
                        static_cast<unsigned>(buffer->m_dwSize),
                        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error
                    );
                }
                m_pFirstBuf = nullptr;
                m_pLastBuf = nullptr;
                buffer->Release();
                L2_RELEASE(this, ObjectRefType::IoRetain);
            } else if (submission.IsCompleted()) {
                if (!portable::PostIOCompletion(this, submission.transferred, writeOverlapped)) {
                    OnIOCallback(1, submission.transferred, writeOverlapped);
                }
            }
        } else if (m_pFirstBuf != m_pLastBuf &&
            m_pLastBuf->m_dwSize + buffer->m_dwSize <= static_cast<std::uint32_t>(TBuffer::MaxSize)) {
            std::memcpy(m_pLastBuf->m_Buffer.data() + m_pLastBuf->m_dwSize, buffer->m_Buffer.data(), buffer->m_dwSize);
            m_pLastBuf->m_dwSize += buffer->m_dwSize;
            buffer->Release();
        } else {
            m_pLastBuf->m_pNext = buffer;
            m_pLastBuf = buffer;
        }
    }

    int AsyncWrite(TBuffer* buffer) {
        return m_WriteControl.AddExecutable(
            this, &portable::ExecuteCIOSocketAsyncWrite<TBuffer, TWriteControl>, buffer, L"AsyncWrite"
        );
    }

    // Current Cached.exe instantiates the same source template for the
    // 8192-byte GIP socket with the synchronous entry point kept by the
    // linker (0x004db948). It enters the identical queue/coalescing body
    // directly instead of passing through CExecutionSerializerFast.
    void SyncWrite(TBuffer* buffer) {
        L2_GUARD_FUNCTION();
        L2_GUARD(m_WriteControl);
        if constexpr (!TWriteControl::CallerRetainsSyncWrite) {
            L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
        }
        _Write(buffer);
    }

    void __async_WriteCallback(void* argument) {
        // Current L2NPC.exe 0x00420ea0, donor Shared/ioc.h:904-981.
        // Cached.exe specializations are 0x0042e5e4 for CIOBufferEx<20480>
        // and 0x004db17c for CIOBufferEx<8192>.
        const std::uint32_t transferred = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(argument));
        if (transferred != m_pFirstBuf->m_dwSize) {
            // LogIOError(
            //     "different write count %x(%x) "
            //     "%d != %d",
            //     static_cast<unsigned int>(
            //         m_hSocket.load(
            //             std::memory_order_acquire)),
            //     static_cast<unsigned int>(
            //         reinterpret_cast<
            //             std::uintptr_t>(
            //                 this)),
            //     transferred,
            //     m_pFirstBuf->m_dwSize)
            // LogIOError(
            //     "[network] Write completion on connection %llu reported %u bytes for a %u-byte buffer. The queue is left
            //     unchanged and this callback returns.", static_cast<unsigned long
            //     long>(m_hSocket.load(std::memory_order_acquire)), static_cast<unsigned>(transferred),
            //     static_cast<unsigned>(m_pFirstBuf->m_dwSize))
            L2_LOG_IO_ERROR(
                "[network] Write completion on connection %llu reported %u bytes for a %u-byte buffer; the queue is left "
                "unchanged and this callback returns.",
                static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)),
                static_cast<unsigned>(transferred), static_cast<unsigned>(m_pFirstBuf->m_dwSize)
            );
            // Noise: The unlabelled object-header/padding byte dumps add 32 records without explaining the write-size
            // mismatch reported above. const auto* const bytes =
            //     reinterpret_cast<
            //         const std::uint8_t*>(
            //             m_pFirstBuf)
            // for (std::size_t index = 0
            //      index < 0x10.
            //      ++index) {
            //     LogIOError(
            //         L"[%x]",
            //         static_cast<int>(
            //             static_cast<std::int8_t>(
            //                 bytes[index])))
            // }
            // for (std::size_t index = 0
            //      index < 0x10.
            //      ++index) {
            //     LogIOError(
            //         L"[%x]",
            //         static_cast<int>(
            //             static_cast<std::int8_t>(
            //                 bytes[
            //                     offsetof(
            //                         TBuffer,
            //                         pad2) +
            //                     index])))
            // }
            return;
        }

        TBuffer* const completed = m_pFirstBuf;
        const std::int32_t completedSize = static_cast<std::int32_t>(completed->m_dwSize);
        m_nPendingWrite.fetch_sub(completedSize, std::memory_order_acq_rel);
        g_nSocketPendingWrite.fetch_sub(completedSize, std::memory_order_acq_rel);
        m_pFirstBuf = completed->m_pNext;
        if (!m_pFirstBuf) {
            m_pLastBuf = nullptr;
        } else {
            L2_ADD_REF(this, ObjectRefType::IoRetain);
            PortableOverlapped* const writeOverlapped = reinterpret_cast<PortableOverlapped*>(m_overlappedWrite.data());
#if defined(L2_BUILD_CACHED)
            const std::int64_t submittedSize = static_cast<std::int64_t>(m_pFirstBuf->m_dwSize);
            g_nSocketSendBytes.fetch_add(submittedSize, std::memory_order_acq_rel);
            g_nSocketSendCount.fetch_add(1, std::memory_order_acq_rel);
            if (submittedSize > 7000) {
                g_nSocketSendBytesOver7K.fetch_add(submittedSize, std::memory_order_acq_rel);
                g_nSocketSendCountOver7K.fetch_add(1, std::memory_order_acq_rel);
            }
#endif
            const portable::SocketWriteSubmission submission = portable::SubmitSocketWrite(
                this, m_pFirstBuf->m_Buffer.data(), static_cast<int>(m_pFirstBuf->m_dwSize), writeOverlapped
            );
            if (submission.IsFailed()) {
                const int error = GetSocketLastError();
                portable::ReportSocketFailureDiagnostics(
                    m_hSocket.load(std::memory_order_acquire), error, m_nPendingWrite.load(std::memory_order_relaxed)
                );
                if (!IsExpectedSocketCloseError(error)) {
                    // LogIOError(
                    //     "CIOSocket::WriteCallback "
                    //     "%x(%x) err=%d",
                    //     static_cast<unsigned int>(
                    //         m_hSocket.load(
                    //             std::memory_order_acquire)),
                    //     static_cast<unsigned int>(
                    //         reinterpret_cast<
                    //             std::uintptr_t>(
                    //                 this)),
                    //     error)
                    // LogIOError(
                    //     "[network] Cannot submit the next queued buffer of %u bytes on connection %llu (socket error %d)
                    //     the next buffer remains queued and its IO reference is released.",
                    //     static_cast<unsigned>(m_pFirstBuf->m_dwSize),
                    //     static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error)
                    L2_LOG_IO_ERROR(
                        "[network] Cannot submit the next queued buffer of %u bytes on connection %llu (socket error %d); "
                        "the next buffer remains queued and its IO reference is released.",
                        static_cast<unsigned>(m_pFirstBuf->m_dwSize),
                        static_cast<unsigned long long>(m_hSocket.load(std::memory_order_acquire)), error
                    );
                }
                // Noise: The unexpected send error above has context. Expected close errors should not produce a second
                // error record. LogIOError(
                //     L"[%s][%d] "
                //     L"CIOSocket::WriteCallback - "
                //     L"WSASend fail. error(%d)",
                //     L"C:\\Work\\Lineage2Build\\Server\\Main\\shared\\ioc.h",
                //     0x3c6,
                //     error)
                L2_RELEASE(this, ObjectRefType::IoRetain);
            } else if (submission.IsCompleted()) {
                if (!portable::PostIOCompletion(this, submission.transferred, writeOverlapped)) {
                    OnIOCallback(1, submission.transferred, writeOverlapped);
                }
            }
        }
        completed->Release();
    }

    int WriteCallback(std::uint64_t transferred) {
        return m_WriteControl.AddExecutable(
            this, &portable::ExecuteCIOSocketWriteCallback<TBuffer, TWriteControl>,
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(transferred)), L"WriteCallback"
        );
    }

    void Write(TBuffer* buffer) {
        L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
        AsyncWrite(buffer);
    }

    // Public in the exact PDB at +0x18 and +0x20.
    std::atomic<std::uint64_t> m_hSocket;
    std::atomic<std::int32_t> m_nPendingWrite;

protected:
    // Common prefix offsets are +0x18, +0x20, +0x24 and +0x28. With the
    // serialized L2NPC policy the remaining fields begin at +0x58. With the
    // Cached critical-section policy they begin at +0x50, exactly as current
    // Cached.exe's WSARecv/OnIOCallback bodies show.
    CIODummyLock m_lock;
    TWriteControl m_WriteControl;
    std::array<std::uint8_t, 0x20> m_overlappedRead;
    std::array<std::uint8_t, 0x20> m_overlappedWrite;
    TBuffer* m_pReadBuf;
    TBuffer* m_pFirstBuf;
    TBuffer* m_pLastBuf;

private:
    friend struct portable::CIOSocketTransportAccess;
};


template <typename TBuffer, typename TWriteControl>
void portable::ExecuteCIOSocketAsyncWrite(MemoryObject* owner, void* argument) {
    static_cast<CIOSocketEx<TBuffer, TWriteControl>*>(owner)->__async_AsyncWrite(argument);
}

template <typename TBuffer, typename TWriteControl>
void portable::ExecuteCIOSocketWriteCallback(MemoryObject* owner, void* argument) {
    static_cast<CIOSocketEx<TBuffer, TWriteControl>*>(owner)->__async_WriteCallback(argument);
}

template <typename TBuffer, typename TWriteControl>
bool portable::CIOSocketTransportAccess::FeedRead(
    CIOSocketEx<TBuffer, TWriteControl>* socket, const std::uint8_t* data, std::size_t length
) {
    if (!socket || (!data && length != 0) ||
        socket->m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        return false;
    }

    TBuffer* const readBuffer = runtime::raw_atomic::LoadPointerAcquire(&socket->m_pReadBuf);
    if (!readBuffer) {
        return false;
    }
    const std::size_t used = runtime::raw_atomic::LoadU32Relaxed(&readBuffer->m_dwSize);
    if (length == 0 || used > readBuffer->m_Buffer.size() || length > readBuffer->m_Buffer.size() - used ||
        !portable::ConsumeSocketReadTransport(socket)) {
        return false;
    }

    std::memcpy(readBuffer->m_Buffer.data() + used, data, length);
    PortableOverlapped* const readOverlapped = reinterpret_cast<PortableOverlapped*>(socket->m_overlappedRead.data());
    const std::uint32_t transferred = static_cast<std::uint32_t>(length);
    if (!portable::PostIOCompletion(socket, transferred, readOverlapped)) {
        socket->OnIOCallback(1, transferred, readOverlapped);
    }
    return true;
}

template <typename TBuffer, typename TWriteControl>
void portable::CIOSocketTransportAccess::FeedTerminal(CIOSocketEx<TBuffer, TWriteControl>* socket, int error) {
    if (!socket || !portable::ConsumeSocketReadTransport(socket)) {
        return;
    }
    PortableOverlapped* const readOverlapped = reinterpret_cast<PortableOverlapped*>(socket->m_overlappedRead.data());
    const int success = error == 0 ? 1 : 0;
    if (!portable::PostIOCompletionResult(socket, success, 0, readOverlapped, error)) {
        SetSocketLastError(error);
        socket->OnIOCallback(success, 0, readOverlapped);
    }
}

template <typename TBuffer, typename TWriteControl>
bool portable::FeedSocketRead(CIOSocketEx<TBuffer, TWriteControl>* socket, const std::uint8_t* data, std::size_t length) {
    return CIOSocketTransportAccess::FeedRead(socket, data, length);
}

template <typename TBuffer, typename TWriteControl>
void portable::FeedSocketTerminal(CIOSocketEx<TBuffer, TWriteControl>* socket, int error) {
    CIOSocketTransportAccess::FeedTerminal(socket, error);
}

template <typename TBuffer, typename TWriteControl>
void portable::CancelSocketReads(CIOSocketEx<TBuffer, TWriteControl>* socket) {
    const std::size_t pending = portable::CancelSocketReadTransport(socket);
    for (std::size_t index = 0; index < pending; ++index) {
        L2_RELEASE(socket, ObjectRefType::IoRetain);
    }
}

using CIOSocket = CIOSocketEx<DefaultIOBuffer>;
// CIOServer's socket specialization is product-selected in the original
// physical Shared source. Current L2Server uses the serialized 0xb0 form.
// current CacheD uses the critical-section-backed 0xa8 form.
#if defined(L2_BUILD_L2SERVER)
using CIOServerSocket = CIOSocket;
#else
using CIOServerSocket = CIOSocketEx<DefaultIOBuffer, CIOSocketCriticalSectionWriteControl>;
#endif

// Shared/ioc.h CIOServer. The current PDB keeps this complete 0x28-byte
// abstract type even though no concrete server survives linker elimination
// in L2NPC.exe. NASC64_2015 supplies the eliminated method bodies. Its
// field offsets, virtual order and socket state agree with the current PDB.
class CIOServer : public CIOObject {
public:
    CIOServer();
    ~CIOServer() override;

#if defined(L2_BUILD_L2SERVER)
    L2_DECLARE_OBJECT_TYPE(CIOServer)
#else
    L2_DECLARE_MEMORY_ALLOCATION(CIOServer)
#endif

    int Create(int port);
    void Close();
    void Stop();

    // Public in the target PDB at +0x18.
    std::uint64_t m_hSocket;

protected:
    void OnWaitCallback() override;
    virtual CIOServerSocket* CreateSocket(std::uint64_t socket, ::sockaddr_in* address) = 0;
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;

    // WSAEVENT in the Windows source, kept as the target's pointer-sized
    // field. POSIX stores only an opaque readiness handle here.
    void* m_hAcceptEvent;
};


namespace packet_ex_detail {

// L2NPC compiles Shared/CPacketEx without L2CACHED. The empty policy is
// therefore removed by empty-base optimization and leaves the three target
// pointers at +0x18/+0x20/+0x28. Current Cached.exe has the same source
// template compiled with L2CACHED and so inserts the four timing
// dwords recovered in Shared/QsCheck.h. An explicit policy represents that
// source preprocessor distinction without compiling two physical copies of
// Shared/ioc.h into the portable project.
class CPacketExNoInstrumentation {
public:
    static constexpr std::size_t TargetExtent = 0;

    void SetPacketType(std::uint32_t) noexcept {}
    void BeginCallback(std::uint8_t*) noexcept {}
};

} // namespace packet_ex_detail

// Shared/ioc.h:1524-1604. The default shipping L2NPC instantiation has the
// three pointers below at +0x18/+0x20/+0x28. The target ABI erases the
// concrete socket and callback types behind CIOSocketEx/PacketFunc. CacheD's
// L2CACHED-only timing fields are supplied by TInstrumentation and keep the
// current CacheD +0x18..+0x24 / +0x28..+0x38 layout.
template <
    typename TBuffer,
    typename TSocket = typename TBuffer::PacketSocket,
    typename TPacketFunc = typename TBuffer::PacketFunc,
    typename TInstrumentation = packet_ex_detail::CPacketExNoInstrumentation>
class CPacketEx : public CAbstractPacket, private TInstrumentation {
public:
    using Socket = TSocket;
    using PacketFunc = TPacketFunc;

    CPacketEx() : TInstrumentation(), m_pSocket(nullptr), m_pBuf(nullptr), m_pFunc(nullptr) {}

    ~CPacketEx() override = default;

#if defined(L2_BUILD_L2SERVER)
private:
    // Shipping metadata: L2Server's 295-entry registration stream assigns
    // IDs 5/29/55 to the 20480/32768/8192-byte specializations. Portable IDs
    // are registered for every actual specialization. The source
    // template still accounts the 0x30 base specialization even when a
    // derived packet asks operator new for additional bytes.
    L2_DECLARE_OBJECT_REGISTRATION(CPacketEx)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPacketEx)

public:
    static int __GetClassID() noexcept { return L2_MEMORY_CLASS_ID(CPacketEx); }

    static const wchar_t* __GetClassName() noexcept { return m_pClassName_CPacketEx; }

    static void* operator new(std::size_t requestedSize, bool objectAllocation) {
        return runtime::process::AllocateMemoryObject(
            requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(CPacketEx), __GetClassName(), "CPacketEx",
            0x30 + TInstrumentation::TargetExtent
        );
    }

    static void* operator new(std::size_t requestedSize) { return operator new(requestedSize, true); }

    L2_DECLARE_OBJECT_ADD_REF()
    L2_DECLARE_OBJECT_RELEASE(CPacketEx)
#else
    L2_DECLARE_OBJECT_TYPE(CPacketEx)
#endif

    void SetPacketType(std::uint32_t packetType) noexcept { TInstrumentation::SetPacketType(packetType); }

    void OnIOCallback(
        int success [[maybe_unused]], std::uint32_t transferred, PortableOverlapped* overlapped [[maybe_unused]]
    ) override {
        // Current Cached.exe versions are 0x004814c4 for the
        // 20480-byte packet and 0x004dbf04 for the 8192-byte packet.

        // Current 0x004814c4/0x004dbf04 uses buffer + 0x11 +
        // completion-key directly. It does not reread CIOBufferEx::m_dwSize.
        // the socket may already be compacting or replacing its read buffer,
        // while this packet keeps the old storage independently.
        auto* const packet = m_pBuf->m_Buffer.data() + static_cast<std::size_t>(transferred) + 1;
        TInstrumentation::BeginCallback(packet);
        const bool closeRequested = m_pFunc(m_pSocket, packet);
        if (closeRequested) {
            m_pSocket->Close();
        }
        L2_RELEASE(m_pSocket, ObjectRefType::IoRetain);
        m_pBuf->Release();
        g_nPendingPacket.fetch_sub(1, std::memory_order_seq_cst);
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    }

    Socket* m_pSocket;
    TBuffer* m_pBuf;
    PacketFunc m_pFunc;
};


#if defined(L2_BUILD_L2SERVER)
class CAuthSocket;
class NpcSocket;

using CAuthCallback = bool (*)(CAuthSocket*, const std::uint8_t*);
using CAuthPacketBase = CPacketEx<IOBuffer8k, CAuthSocket, CAuthCallback>;
using NpcPacketCallback = bool (*)(NpcSocket*, const std::uint8_t*);
using CNPCPacketBase = CPacketEx<DefaultIOBuffer, NpcSocket, NpcPacketCallback>;

// CAuthPacket is defined in Auth.h and CNPCPacket in NpcSocket.h, after their
// sockets are complete. MSVC instantiates the virtual members of a CPacketEx
// base as soon as it is named, and those members need the complete socket.
// Do not name CAuthPacketBase or CNPCPacketBase before the socket header.
#endif

// Shared/ioc.h exact 0x40 watchdog object. It is also used as a scoped
// activity marker around long startup/barrier waits.
class CIODeadlockChecker final : public CIOObject, public Singleton<CIODeadlockChecker> {
public:
    struct DeadlockCrashInfo {
        std::int32_t m_nThreadID = -1;
        std::uint32_t m_CrashTick = 0;
        std::uint32_t m_nLastTick = 0;
        bool m_bForcedCrash = false;

        DeadlockCrashInfo();

        void SetCrashInfo(int threadId, std::uint32_t crashTick, std::uint32_t lastTick);
        bool ForcedCrashByDeadlockChecker();
        std::uint32_t GetCrashTick();
        std::uint32_t GetLastTick();
        int GetThreadID();
        void PrintLog(void* file);
    };


    CIODeadlockChecker();
    // Current primary scalar deleting destructor 0x004cb944 tears down the
    // Singleton and MemoryObject bases. The secondary Singleton-base vtable
    // reaches it through the this-adjusting thunk at 0x004cae28.
    ~CIODeadlockChecker() override = default;

    L2_DECLARE_OBJECT_TYPE(CIODeadlockChecker)

    void Init();
    void TimerExpired(int id) override;
    bool IsOver49Days(std::uint32_t currentTick, std::uint32_t lastTick);
    bool IsTimeOver(std::uint32_t currentTick, std::uint32_t lastTick);
    void TestDeadlock();
    void Test49Days();
    void ProcessDeadlock(std::uint32_t currentTick, std::uint32_t lastTick);

    std::uint32_t m_nLastTick = 0;
    std::int32_t m_nThreadID = -1;
    bool m_bActive = true;
    DeadlockCrashInfo m_DeadlockCrashInfo;
};


// Shared/ioc.h:1340-1350. Construction suppresses the watchdog and
// destruction publishes a fresh timestamp and the current logical TLS id.
class CDeadlockCheckerInactivator {
public:
    CDeadlockCheckerInactivator();
    ~CDeadlockCheckerInactivator();

    CDeadlockCheckerInactivator(const CDeadlockCheckerInactivator&) = delete;
    CDeadlockCheckerInactivator& operator=(const CDeadlockCheckerInactivator&) = delete;
};
