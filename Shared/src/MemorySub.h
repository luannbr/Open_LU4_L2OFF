#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "ProcessRuntime.h"
#include "Shared/src/Obj.h"
#include "Shared/src/ScopedLockGuards.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>

// Shared/Exception.h declaration. Keep this forward declaration here to
// avoid making the allocator header depend on the complete crash/log layer.
void CrashImpl();

// Shared/MemorySub.cpp global. Class registration starts at zero. Each new
// process-local type increments it. Shipping numbers below are metadata only.
extern int g_nClassCount;

// PDB: Shared/MemorySub.cpp::IssuedReportCallStack. The current executable
// reads and clears this per-thread flag in
// CNpcMakerDB::ProcessOneNpcMakerEx(int). No producer is linked into this
// image, so the portable recovery does not invent one.
extern thread_local int IssuedReportCallStack;

// MemorySub.obj owns this nearby-PDB global. The target's fixed
// CMwsrQueue<CIOBufferEx<...>> constructors add their backing-store sizes.
// the portable recovery now creates those same per-thread pools.
extern volatile std::int64_t g_CMwsrQueueAlloc;

// Exact PDB global toggled by the status-window command. It controls the
// target's optional diagnostic for allocations larger than 32 KiB.
extern bool g_bShowMallocLog;


inline constexpr std::size_t kMemoryClassCapacity = 65536;
inline constexpr std::size_t kMemoryWorkTypeCount = 50;
inline constexpr std::size_t kMemoryPoolCount = 49;
inline constexpr std::size_t kMaximumPooledAllocation = 0x8000;

// Exact CRT initializer order recovered from the current L2NPC.exe. The two
// entries absent from its public PDB stream are not guesses:
// CIOSocketEx is used by the class-specific Release body at 0x0041ff74 and
// AtomicLog owns the adjacent CObjectMap<AtomicLog,...> static.
enum class ShippingMemoryClassId : std::int32_t {
    Invalid = 0,
    CIOObject = 1,
    CIODeadlockChecker = 2,
    CIOServer = 3,
    CAbstractPacket = 4,
    CIOSocketEx = 5,
#if defined(L2_BUILD_L2SERVER)
    AtomicLog = 49,
    AtomicJob = 50,
#else
    AtomicLog = 6,
    AtomicJob = 7,
#endif
    CCreature = 8,
    CDBNPC = 9,
    CDBNPCSocket = 10,
    CPacketEx = 11,
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server RelaySocket.cpp/AccountClientSocket.cpp registration
    // cells 0x01810a88/0x01810a98. Their class-specific Release bodies
    // consume numbers 7/8 with exact 0x110/0x130-byte extents.
    RelaySocket = 7,
    AccountClientSocket = 8,
    // Current L2Server AnnounceTimer.cpp CRT initializer 0x00d732b0
    // publishes number 47 in 0x020f4fb8. CAnnounceTimer's complete
    // CIOObject owner is 0x3ea0 bytes and its class-specific Release uses
    // that same registration slot.
    CAnnounceTimer = 47,
    // Current L2Server EventMaker.cpp CRT initializer 0x00d779ec stores
    // number 32 in 0x023783d4. CEventMakerManager's class-specific
    // allocator consumes the same slot with the exact 0x98-byte extent.
    CEventMakerManager = 32,
    // Current L2Server Auction.cpp CRT initializer 0x00d755b4 stores
    // number 51 in 0x020f9964. CAuction allocates exactly 0xd0 bytes.
    CAuction = 51,
    // Current L2Server BaseCampAgit.cpp CRT registration at 0x00d6e858
    // writes number 10 to 0x01811440. The class-specific allocation and
    // Release owners consume that slot with target size 0x498.
    CBaseCampAgit = 10,
    // Current Agit.cpp CRT initializer 0x00d721c8 publishes number 36.
    // CWorldAgit's class-specific Release consumes the same cell. The
    // abstract base has no surviving direct allocation call site.
    CWorldAgit = 36,
    // Current AirShip.cpp CRT initializers 0x00d72a38/0x00d72a50 publish
    // the two cpp-local alarm owners at numbers 38/39. Only their exact
    // class-id/name carriers survive in the current image.
    CAlarm = 38,
    CFireAllCannonAlarm = 39,
    // Current AirShipPart.cpp CRT initializer 0x00d72f84 publishes number
    // 46 in 0x01e74adc. The current image keeps only the exact class-name
    // carrier. No concrete allocation, vtable or runtime body survives.
    CAirShipCorePart = 46,
    // Current L2Server Castle.cpp CRT registration stores number 79 in
    // 0x02377aa0. CCastle's class allocator consumes the same slot with
    // the current 0x146f8-byte concrete extent.
    CCastle = 79,
    // Current L2Server Dominion.cpp CRT registration stores number 100 in
    // 0x04d22ed4. CDominion's class allocator consumes that slot with the
    // current 0x6c0-byte concrete extent.
    CDominion = 100,
    // Current L2Server AuctionAgit.cpp class allocator registration. The
    // class-specific Release body consumes number 53 and target size 0x708.
    CAuctionAgit = 53,
    // Current BeautyManager.h registration numbers, not donor IDs.
    BeautyFace = 59,
    BeautyHair = 60,
    BeautyColor = 61,
    CBeautyManager = 62,
    // Current L2Server Captcha.cpp registration stream publishes
    // CCaptchaJob at number 12.
    CCaptchaJob = 12,
    // Current ChannelChattingSocket.cpp CRT initializer 0x00d6f280
    // publishes number 13 in 0x01a57350. The kept class-name carrier
    // at 0x015c1148 identifies the physical owner exactly.
    CChannelChattingSocket = 13,
    // Current LambdaMemoryObject.cpp CRT initializer 0x00d701e0 publishes
    // number 27 in 0x01a574e0. The class itself is an exact 0x18-byte
    // MemoryObject derivative. The current link keeps no instance, vtable
    // or runtime method body beyond this class-id/name publication.
    LambdaMemoryObject = 27,
    // Current BuySell.cpp CRT initializers at 0x00d7b734/0x00d7b74c
    // register CBuy immediately before the already recovered CBuySell
    // number 76. The CBuy allocator consumes number 75 and size 0x838.
    CBuy = 75,
    // Current L2Server commcli.cpp CRT initializer 0x00d7dc74 stores
    // number 83 in 0x0237844c. The CCommCli allocator consumes the same
    // slot with the exact 0x58-byte concrete extent.
    CCommCli = 83,
    // Current L2Server CommissionTrade.cpp CRT registration stream. The
    // six category records, expiry record and concrete manager consume the
    // contiguous class-id interval 84..91.
    CommissionTradeWeapon = 84,
    CommissionTradeArmor = 85,
    CommissionTradeAccessary = 86,
    CommissionTradeGoods = 87,
    CommissionTradePet = 88,
    CommissionTradeEtc = 89,
    ExpiredCommissionData = 90,
    CCommissionTradeManager = 91,
    // Current L2Server commsocket.cpp CRT initializer 0x00d7e18c stores
    // number 92 in 0x023784e0. CCommSocket allocates exactly 0xd0 bytes.
    CCommSocket = 92,
    // Current L2Server DynamicQuest.cpp manager allocator registration.
    // The class-specific operator new/Release pair consumes number 110.
    DynamicQuestManager = 110,
    // Current Economy.cpp keeps the private class-id/name-pointer carrier
    // immediately after the DynamicQuest registration cohort.
    CEconomy = 111,
    // Current L2Server CRT registration stream: initializer 0x00d8b954,
    // storage 0x04d26ecc, number 112. Shared sources are compiled once per
    // product, so this differs from the current L2NPC number.
    EtcMani = 112,
    // Shared fhtml.cpp is compiled into each product. Current L2Server's
    // private CFHTML carrier is number 115. L2NPC keeps number 13 below.
    CFHTML = 115,
    // Current FieldCycle.cpp CRT initializer triplet at
    // 0x00d8c784/0x00d8c79c/0x00d8c7b4. The destinations are the three
    // class-id cells consumed by the exact FieldCycle allocation/release
    // bodies (0x04d330a8..0x04d330b0).
    FieldCycleUser = 116,
    CFieldCycle = 117,
    CFieldCycleSystem = 118,
    // Current fishing.cpp and Shared/GIPSocket.cpp registrations publish
    // the concrete allocation owners at numbers 119 and 124.
    CFishingController = 119,
    CGIPSocket = 124,
    // Current L2Server Manor.cpp CRT initializer 0x00d9a87c stores number
    // 185 in 0x16659e90. CManor's class allocator consumes the same slot
    // with the exact 0x80-byte concrete extent.
    CManor = 185,
    // Current L2Server MiniGameAgit.cpp CRT initializer 0x00d9b1bc
    // stores number 187 in 0x173da5b8. The concrete parser allocation
    // and current registration helper both fix the extent at 0x580.
    CMiniGameAgit = 187,
    // Current L2Server NpcBattleAgit.cpp CRT registration writes number
    // 190 to 0x17677778. Its concrete allocation is exactly 0x4a8 bytes.
    CNpcBattleAgit = 190,
    // Current L2Server OlympiadStuff.cpp CRT initializer 0x00da561c writes
    // number 205 to 0x1dca9948. WOlympiadUser's allocator and Release body
    // consume that same slot with the exact 0x78-byte concrete extent.
    WOlympiadUser = 205,
    // Current OlympiadNetwork registration family. The socket base has
    // class-specific Release but no surviving direct allocation call site.
    // the three concrete owners publish 0xe0/0x110/0x110-byte allocations.
    WOlympiadSocketBase = 199,
    WOlympiadNetworkHost = 200,
    WOlympiadSocketHost = 201,
    WOlympiadSocketPeer = 202,
    // Current pet.cpp CRT initializers 0x00da801c..0x00da8064 publish the
    // four eliminated pet-despawn policy owners at numbers 212..215.
    CPetDespawnInfo = 212,
    CPetDespawnOnOlympiadPlayer = 213,
    CPetDespawnOnOlympiadObserver = 214,
    CPetDespawnOnEventMatchObserver = 215,
    // Current L2Server PetitionCli.cpp CRT initializer stream at
    // 0x00da82d1..0x00da8301 writes the three concrete petition-data owner
    // numbers to 0x20022398..0x200223a0. PetitionSocket.cpp then writes the
    // adjacent socket owner at 0x00da8529 / 0x200223b0.
    PetitionCategory = 216,
    PetitionFAQ = 217,
    PetitionCategoryAttribute = 218,
    CPetitionSocket = 219,
    // Current PredefinedListDB.cpp registration numbers.
    BasePredefinedList = 237,
    PredefinedSkillList = 238,
    PredefinedItemList = 239,
    PredefinedTerritoryList = 240,
    // Current L2Server CRT initializers 0x00dad614/0x00dad62c,
    // 0x00dad88c and 0x00dadcf8 increment the shared class counter into
    // these four consecutive registration cells. RaidServerManager's
    // recovered number 246 anchors the sequence.
    RaidSocket = 245,
    RaidServerManager = 246,
    RaidServerSocket = 247,
    RaidClientSocket = 248,
    // Current RelayClientSocket.cpp CRT stream at 0x00dafa44/0x00dafa74
    // publishes the concrete socket and packet owners at numbers 249/250.
    RelayClientSocket = 249,
    CRelayPacket = 250,
    // Current ReservationNPCSpawnTimer.cpp keeps the abstract base carrier
    // immediately before the concrete spawn-info owner.
    CReservationInfo = 252,
    // Current L2Server ReservationNPCSpawnTimer.cpp CRT initializer
    // 0x00dafd0c stores number 253 in 0x2013e90c. The concrete
    // CReservationSpawnInfo allocator consumes that slot at size 0x48.
    CReservationSpawnInfo = 253,
    // Current ReservationNPCSpawnTimer.cpp CRT initializer 0x00dafd24
    // publishes number 254 in 0x2013e910. Only the exact class-name
    // carrier survives. No concrete runtime owner is compiled here.
    CReservationEventInfo = 254,
    // Current L2Server Residence.cpp CRT initializer 0x00daffe0 stores
    // number 256 in 0x2013eb10. CExpelTimer's class-specific Release
    // consumes that exact slot.
    CExpelTimer = 256,
    // Current L2Server RestrictArea.cpp CRT initializer 0x00db0260 stores
    // number 257 in 0x2013ec80. CRestrictArea has no payload beyond its
    // exact 0x18-byte MemoryObject base and consumes that registration slot.
    CRestrictArea = 257,
    // Current SequenceJob.cpp keeps this typed job carrier even though its
    // constructor and concrete vtable are linker-eliminated.
    CSequenceJobObject = 258,
    // Current L2Server SiegeAgit.cpp CRT registration at 0x00db1700 writes
    // number 261 to 0x20c81bf0. The concrete owner allocates 0xa510 bytes.
    CSiegeAgit = 261,
    // Current L2Server SkillEnchant.cpp CRT initializer stream registers the
    // complete six-class operator hierarchy at numbers 264..269. Every
    // concrete allocation consumes the same current 0x20-byte extent.
    SkillEnchantOperator = 264,
    SkillEnchantNormal = 265,
    SkillEnchantSafe = 266,
    SkillEnchantUntrain = 267,
    SkillEnchantRouteChange = 268,
    SkillEnchantForce = 269,
    // Current L2Server TeamBattleAgit.cpp CRT registration at 0x00db59ec
    // writes number 272 to 0x20d87054. Its concrete allocation is 0x6b0.
    CTeamBattleAgit = 272,
    // The current WareHouse.cpp CRT stream registers the complete deposit and
    // withdraw owners consecutively. Their class-specific allocators consume
    // numbers 292/293 at the exact 0x40/0x70 extents. The withdraw initializer
    // at 0x00dd3630 stores number 293 in 0x2129a078.
    CWareHouseDeposit = 292,
    CWareHouseWithdraw = 293,
    // Current L2Server UserSocket.cpp CRT initializer 0x00dd0b84:
    // its xadd at 0x00dd0b8a is number 284 in the same 295-entry stream
    // that reproduces CItem=169 and the established object
    // class numbers. Both CUserSocket and its NullUserSocket derivative
    // allocate through this one physical owner.
    CUserSocket = 284,
    // Current Inspector.cpp private static carrier.
    CInspector = 161,
#else
    EtcMani = 12,
    CFHTML = 13,
#endif
    CFormationManager = 14,
#if defined(L2_BUILD_L2SERVER)
    CLogSocket = 183,
    CNPC = 189,
#else
    CLogSocket = 15,
    CNPC = 16,
#endif
    ScriptEvent = 17,
    CNpcMakerWorld = 18,
    CNpcMaker = 19,
    CNpcMakerDB = 20,
    CNpcMakerReleaseVerifier = 21,
    NpcRespawnInfo = 22,
    NpcMakerEx = 23,
    CNpcMakerExReleaseVerifier = 24,
    CNpcSpawnDefine = 25,
    NpcSpawnDefineEx = 26,
#if defined(L2_BUILD_L2SERVER)
    MemoryObject = 193,
    CObject = 194,
#else
    MemoryObject = 27,
    CObject = 28,
#endif
    CServerSocket = 29,
#if defined(L2_BUILD_L2SERVER)
    CSocket = 270,
    CTerritory = 275,
    User = 283,
#else
    CSocket = 30,
    CTerritory = 31,
    User = 32,
#endif
    ReverseHateManager = 33,
    ReverseHateNPCList = 34,
#if defined(L2_BUILD_L2SERVER)
    // Historical IDs formerly checked through L2ServerMemory. Metadata only.
    BotBlockData = 71,
    CBillingJob = 242,
    CBossRecord = 65,
    CBossRecordList = 66,
    CBossRecordRanking = 67,
    CBossRecordRound = 68,
    CBotCheckInfo = 69,
    CBotChecker = 70,
    CBotReportAgent = 72,
    CFriends = 122,
    CGoodsInventoryJob = 126,
    CGoodsInventorySocket = 125,
    CNewGoods = 127,
    CPathToAwakeningNotice = 211,
    CPledgeMember = 233,
    CPledgeMemberList = 234,
    CPremiumItem = 243,
    CPremiumServiceSocket = 241,
    CReplyChecker = 251,
    PledgeFunction = 225,
    PledgeLevel = 229,
    PledgeMastery = 232,
    PledgeSkill = 231,
#endif
};

static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::ReverseHateNPCList) == 34);

// Target symbols are three fixed 65536-entry arrays. The reference counters
// are updated with the compiled Interlocked operations. Names and class sizes
// are ordinary published slots in the shipping image.
extern volatile std::int32_t g_pClassRef[kMemoryClassCapacity];
extern const char* volatile g_pClassRefName[kMemoryClassCapacity];
extern volatile std::int32_t g_ClassSize[kMemoryClassCapacity];

// PDB: long g_pClassWorkRefCount[65536][50]. Its 0xc80000-byte BSS range is
// present in the current executable between g_ClassSize and g_pMemoryImpl.
// Current Cached.cpp::ReportClassWorkRefCount at 0x00454d10 reads the first
// g_nClassCount rows. The interlocked writers are compiled from allocation
// work-type instrumentation in products which enable that source path.
extern std::int32_t g_pClassWorkRefCount[kMemoryClassCapacity][kMemoryWorkTypeCount];


namespace memory_sub_detail {

// The target stores only a four-byte logical size before each allocation.
// A wider portable prefix keeps the returned pointer max-aligned, but its
// final four bytes remain the shipping prefix at allocation - 4. The leading
// bytes store the class ID for cleanup after a failed constructor.
struct alignas(std::max_align_t) AllocationHeader {
    std::byte transportPadding[alignof(std::max_align_t) - sizeof(std::uint32_t)];
    std::uint32_t logicalSize;
};

static_assert(alignof(std::max_align_t) >= sizeof(std::uint32_t));
static_assert(offsetof(AllocationHeader, logicalSize) == sizeof(AllocationHeader) - sizeof(std::uint32_t));
static_assert(sizeof(AllocationHeader) == alignof(std::max_align_t));

inline std::size_t PhysicalPoolStride(std::uint32_t targetUnitSize) noexcept {
    // The Windows allocator returns targetUnitSize-byte blocks with a four
    // byte prefix. Portable callers need a max-aligned AllocationHeader, so
    // only the unobservable backing stride grows. Target pool selection and
    // all published byte counters continue to use targetUnitSize.
    const std::size_t payloadCapacity = static_cast<std::size_t>(targetUnitSize) - sizeof(std::uint32_t);
    const std::size_t unalignedStride = sizeof(AllocationHeader) + payloadCapacity;
    const std::size_t alignment = alignof(std::max_align_t);
    return (unalignedStride + alignment - 1U) & ~(alignment - 1U);
}

} // namespace memory_sub_detail

// MemorySub.h:92-175 in the matching PDB. The current executable confirms
// the same 0x38-byte carrier and every Push/Grow/Pop operation.
template <int PageSize>
class CMwmrGrowingQueue {
public:
    explicit CMwmrGrowingQueue(int unitSize) {
        // Current 0x0046925c..0x00469291 store this exact subset/order in the
        // inlined template constructor. No blanket value-initialization is
        // present around it.
        UnitSize = unitSize;
        TotalCount = 0;
        Lock = 0;
        First = 0;
        Last = 0;
        FreeCount = 0;
        Index = nullptr;
        CountPerPage = PageSize / UnitSize;
        GrowingCount = 0;
    }

    void SpinLock() noexcept {
        int retryCount = 0;
        while (!runtime::raw_atomic::CompareExchangeI32Acquire(&Lock, 0, 1)) {
            ++retryCount;
            if (retryCount % 20 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void SpinUnlock() noexcept { runtime::raw_atomic::StoreI32Release(&Lock, 0); }

    void Grow() {
        ++GrowingCount;
        const std::size_t physicalStride = memory_sub_detail::PhysicalPoolStride(static_cast<std::uint32_t>(UnitSize));
        const std::size_t pageBytes = physicalStride * static_cast<std::size_t>(CountPerPage);

        void* const page = std::malloc(pageBytes);
        if (!page) {
            GuardHelper callStack(
                L"CMwmrGrowingQueue - allocate new "
                L"memory failed",
                CThreadManager::GetThreadID()
            );
            // Current 0x00469540 writes through address zero
            // after recording this failure, then leaves Grow if a platform
            // crash handler resumes execution.
            CrashImpl();
            return;
        }

        TotalCount += static_cast<unsigned>(CountPerPage);
        FreeCount = CountPerPage;
        First = 0;
        Last = CountPerPage;

        std::free(Index);
        Index = static_cast<void**>(std::malloc(static_cast<std::size_t>(TotalCount) * sizeof(void*)));
        if (!Index) {
            GuardHelper callStack(
                L"CMwmrGrowingQueue - allocate new "
                L"index failed",
                CThreadManager::GetThreadID()
            );
            std::free(page);
            CrashImpl();
            return;
        }

        auto* const block = static_cast<std::byte*>(page);
        for (int index = 0; index < CountPerPage; ++index) {
            Index[index] = block + static_cast<std::size_t>(index) * physicalStride;
        }
    }

    void* Pop(int& allocCount) {
        L2_GUARD(*this, (scoped_lock_detail::OwnerMethods<&CMwmrGrowingQueue::SpinLock, &CMwmrGrowingQueue::SpinUnlock>{}));
        if (FreeCount == 0) {
            Grow();
            allocCount = CountPerPage;
        } else {
            allocCount = 0;
        }

        const std::uint64_t firstBits = static_cast<std::uint64_t>(First);
        const std::uint64_t position = firstBits % TotalCount;
        const std::uint64_t nextFirstBits = (firstBits + UINT64_C(1)) % TotalCount;
        std::int64_t nextFirst = 0;
        std::memcpy(&nextFirst, &nextFirstBits, sizeof(nextFirst));
        First = nextFirst;
        void* const allocation = Index[position];
        const std::uint32_t freeBits = static_cast<std::uint32_t>(FreeCount) - UINT32_C(1);
        std::int32_t nextFreeCount = 0;
        std::memcpy(&nextFreeCount, &freeBits, sizeof(nextFreeCount));
        FreeCount = nextFreeCount;
        return allocation;
    }

    void Push(void* allocation) {
        L2_GUARD(*this, (scoped_lock_detail::OwnerMethods<&CMwmrGrowingQueue::SpinLock, &CMwmrGrowingQueue::SpinUnlock>{}));
        const std::uint64_t lastBits = static_cast<std::uint64_t>(Last);
        const std::uint64_t position = lastBits % TotalCount;
        const std::uint64_t nextLastBits = (lastBits + UINT64_C(1)) % TotalCount;
        std::int64_t nextLast = 0;
        std::memcpy(&nextLast, &nextLastBits, sizeof(nextLast));
        Last = nextLast;
        Index[position] = allocation;
        const std::uint32_t freeBits = static_cast<std::uint32_t>(FreeCount) + UINT32_C(1);
        std::int32_t nextFreeCount = 0;
        std::memcpy(&nextFreeCount, &freeBits, sizeof(nextFreeCount));
        FreeCount = nextFreeCount;
    }

    int GetFreeCount() noexcept { return runtime::raw_atomic::LoadI32Relaxed(&FreeCount); }

    int GetTotalMallocMemSize() noexcept { return GrowingCount * PageSize; }

private:
    volatile std::int32_t Lock;
    void** Index;
    unsigned TotalCount;
    volatile std::int32_t FreeCount;
    int CountPerPage;
    int UnitSize;
    int GrowingCount;
    volatile std::int64_t First;
    volatile std::int64_t Last;
};


// Current CacheD and L2Server Shared/MemorySub.h owner. L2NPC instead links
// the older CMwmrGrowingQueue<65536> above. The two current CSmartQueue
// versions have the same 0x40-byte layout and instruction graph. The
// process compilation fixes only the admission sentinel/initial ring size
// (0x17 in CacheD, 0x21 in L2Server).
class CSmartQueue {
public:
    explicit CSmartQueue(std::uint64_t unitSize) {
        m_pIndex = nullptr;
        m_nUnitSize = static_cast<std::uint32_t>(unitSize);
        m_nMonopolyGate = 0;
        m_isLogging = 0;
        m_nActiveCount = 0;
        m_nGrowthCount = 1;

        const runtime::process::MemoryPoolQueueConfiguration configuration =
            runtime::process::GetMemoryPoolQueueConfiguration();
        m_nTotalCount = static_cast<std::uint64_t>(configuration.smartQueueSemaphoreLimit);

        {
            GuardHelper callStack(
                L"TEXT(\"SmartQueue::SmartQueue - Alloc initial RoundQueue\")", CThreadManager::GetThreadID()
            );
            m_pIndex = static_cast<void**>(std::malloc(static_cast<std::size_t>(m_nTotalCount) * sizeof(void*)));
        }

        if (!m_pIndex) {
            GuardHelper callStack(L"NewItems is null", CThreadManager::GetThreadID());
            CrashImpl();
            return;
        }

        std::memset(m_pIndex, 0, static_cast<std::size_t>(m_nTotalCount) * sizeof(void*));

        const std::size_t physicalStride = memory_sub_detail::PhysicalPoolStride(m_nUnitSize);
        void* const initialChunk = std::malloc(physicalStride * static_cast<std::size_t>(m_nGrowthCount));
        if (!initialChunk) {
            GuardHelper callStack(L"newitems is null", CThreadManager::GetThreadID());
            CrashImpl();
            return;
        }

        auto* const bytes = static_cast<std::byte*>(initialChunk);
        for (std::uint64_t index = 0; index < m_nGrowthCount; ++index) {
            m_pIndex[index] = bytes + static_cast<std::size_t>(index) * physicalStride;
        }

        m_nPopCursor = -1;
        m_nPushCursor = runtime::Int64FromBits(m_nGrowthCount - 1U);
        m_nTotalMallocBytes = 0;
        runtime::raw_atomic::AddI64(
            &m_nTotalMallocBytes, runtime::Int64FromBits(static_cast<std::uint64_t>(m_nUnitSize) * m_nGrowthCount)
        );
    }

    bool MonopolizeSemaphore() noexcept {
        L2_GUARD_FUNCTION();

        if (runtime::raw_atomic::LoadI32Relaxed(&m_nMonopolyGate) != 0) {
            return false;
        }

        const std::int32_t contenders = runtime::raw_atomic::AddI32(&m_nMonopolyGate, 1);
        bool acquired = false;
        if (contenders < 2) {
            const std::int32_t sentinel = runtime::process::GetMemoryPoolQueueConfiguration().smartQueueSemaphoreLimit;
            do {
                while (runtime::raw_atomic::LoadI32Relaxed(&m_nActiveCount) > sentinel - 1) {
                    std::this_thread::yield();
                }
                while (runtime::raw_atomic::LoadI32Relaxed(&m_nActiveCount) != 0) {
                }
                acquired = runtime::raw_atomic::CompareExchangeI32(&m_nActiveCount, sentinel, 0) == 0;
            } while (!acquired);
        }

        runtime::raw_atomic::AddI32(&m_nMonopolyGate, -1);
        return acquired;
    }

    void ReleaseMonopolizedSemaphore() noexcept {
        L2_GUARD_FUNCTION();
        const std::int32_t sentinel = runtime::process::GetMemoryPoolQueueConfiguration().smartQueueSemaphoreLimit;
        while (runtime::raw_atomic::CompareExchangeI32(&m_nActiveCount, 0, sentinel) != sentinel) {
        }
    }

    void Push(void* allocation) noexcept {
        const std::int32_t sentinel = runtime::process::GetMemoryPoolQueueConfiguration().smartQueueSemaphoreLimit;
        for (;;) {
            if (runtime::raw_atomic::LoadI32Relaxed(&m_nMonopolyGate) == 0) {
                const std::int32_t active = runtime::raw_atomic::AddI32(&m_nActiveCount, 1);
                if (active < sentinel) {
                    const std::int64_t cursor = runtime::raw_atomic::AddI64(&m_nPushCursor, 1);
                    const std::uint64_t position = static_cast<std::uint64_t>(cursor) % m_nTotalCount;
                    void* volatile* const slot = m_pIndex + position;
                    while (runtime::raw_atomic::CompareExchangePointer(slot, allocation, static_cast<void*>(nullptr)) !=
                        nullptr) {
                        std::this_thread::yield();
                    }
                    runtime::raw_atomic::AddI32(&m_nActiveCount, -1);
                    return;
                }
                runtime::raw_atomic::AddI32(&m_nActiveCount, -1);
            }
            std::this_thread::yield();
        }
    }

    int CreateItems() {
        L2_GUARD_FUNCTION();

        const std::uint64_t observedTotalCount = runtime::raw_atomic::LoadU64Relaxed(&m_nTotalCount);
        if (!MonopolizeSemaphore()) {
            return 0;
        }
        if (observedTotalCount != m_nTotalCount) {
            ReleaseMonopolizedSemaphore();
            return 0;
        }

        m_nGrowthCount *= UINT64_C(2);
        const std::uint64_t growthCount = m_nGrowthCount;
        const std::uint64_t oldTotalCount = m_nTotalCount;
        const std::uint64_t newTotalCount = growthCount + oldTotalCount;
        const std::size_t indexBytes = static_cast<std::size_t>(newTotalCount) * sizeof(void*);
        auto** const newIndex = static_cast<void**>(std::malloc(indexBytes));
        if (!newIndex) {
            GuardHelper failure(L"SmartQueue::CreateItmes - Low Memory (index)", CThreadManager::GetThreadID());
            CrashImpl();
            return 0;
        }
        std::memset(newIndex, 0, indexBytes);

        std::int64_t copiedIndex = -1;
        auto sourceCursor = static_cast<std::uint64_t>(m_nPopCursor);
        const auto lastCursor = static_cast<std::uint64_t>(m_nPushCursor);
        while (++sourceCursor <= lastCursor) {
            ++copiedIndex;
            newIndex[static_cast<std::size_t>(copiedIndex)] = m_pIndex[sourceCursor % oldTotalCount];
        }

        const std::uint64_t newFreeCount = growthCount - static_cast<std::uint64_t>(copiedIndex) - UINT64_C(1);
        const std::size_t physicalStride = memory_sub_detail::PhysicalPoolStride(m_nUnitSize);
        void* const chunk = std::malloc(physicalStride * static_cast<std::size_t>(newFreeCount));
        if (!chunk) {
            GuardHelper failure(L"SmartQueue::CreateItems - Low Memory (chunk)", CThreadManager::GetThreadID());
            CrashImpl();
            return 0;
        }

        auto* const bytes = static_cast<std::byte*>(chunk);
        for (std::uint64_t index = 0; index < newFreeCount; ++index) {
            ++copiedIndex;
            newIndex[static_cast<std::size_t>(copiedIndex)] = bytes + static_cast<std::size_t>(index) * physicalStride;
        }

        std::free(m_pIndex);
        runtime::raw_atomic::StoreU64Relaxed(&m_nTotalCount, newTotalCount);
        m_pIndex = newIndex;
        m_nPopCursor = -1;
        m_nPushCursor = copiedIndex;
        runtime::raw_atomic::AddI64(
            &m_nTotalMallocBytes, runtime::Int64FromBits(static_cast<std::uint64_t>(m_nUnitSize) * newFreeCount)
        );
        ReleaseMonopolizedSemaphore();
        return runtime::Int32FromBits(static_cast<std::uint32_t>(newFreeCount));
    }

    void* Pop(int& allocCount) {
        const std::int32_t sentinel = runtime::process::GetMemoryPoolQueueConfiguration().smartQueueSemaphoreLimit;
        for (;;) {
            if (runtime::raw_atomic::LoadI32Relaxed(&m_nMonopolyGate) == 0) {
                const std::int32_t active = runtime::raw_atomic::AddI32(&m_nActiveCount, 1);
                if (active < sentinel) {
                    const std::int64_t cursor = runtime::raw_atomic::AddI64(&m_nPopCursor, 1);
                    const std::uint64_t position = static_cast<std::uint64_t>(cursor) % m_nTotalCount;
                    void* const allocation =
                        runtime::raw_atomic::ExchangePointer(m_pIndex + position, static_cast<void*>(nullptr));
                    runtime::raw_atomic::AddI32(&m_nActiveCount, -1);
                    if (allocation) {
                        return allocation;
                    }

                    allocCount = runtime::AddInt32Wrapping(allocCount, CreateItems());
                    continue;
                }
                runtime::raw_atomic::AddI32(&m_nActiveCount, -1);
            }
            std::this_thread::yield();
        }
    }

    int GetFreeCount() const noexcept {
        const auto push = static_cast<std::uint32_t>(runtime::raw_atomic::LoadI64Relaxed(&m_nPushCursor));
        const auto pop = static_cast<std::uint32_t>(runtime::raw_atomic::LoadI64Relaxed(&m_nPopCursor));
        return runtime::Int32FromBits(push - pop);
    }

    std::int64_t GetTotalMallocMemSize() const noexcept {
        return runtime::raw_atomic::LoadI64Relaxed(&m_nTotalMallocBytes);
    }

    void SetLogging(const long logging) noexcept { runtime::raw_atomic::ExchangeI32(&m_isLogging, logging > 0 ? 1 : 0); }

private:
    volatile std::int64_t m_nTotalMallocBytes;
    volatile std::int64_t m_nPopCursor;
    volatile std::int64_t m_nPushCursor;
    std::uint64_t m_nGrowthCount;
    std::uint64_t m_nTotalCount;
    void** m_pIndex;
    std::uint32_t m_nUnitSize;
    volatile std::int32_t m_nActiveCount;
    volatile std::int32_t m_nMonopolyGate;
    volatile std::int32_t m_isLogging;
};


class CMemoryPool;
class CMemoryManager;
extern CMemoryManager* g_pMemoryImpl;

class Memory {
public:
    void* Malloc(std::size_t size, bool objectAllocation, const wchar_t* className, int allocationType);
    void Free(void* allocation, int allocationType) noexcept;
    void Report();
    void SetLogging(long logging);
    void GetMemoryMon(wchar_t* output) noexcept;
    std::int64_t GetTotalAllocSize() noexcept;
    // Size that Malloc received for this allocation, from the selected
    // backend's own header.
    static std::size_t RequestedSize(const void* allocation) noexcept;
};

extern Memory g_Memory;

// Exact source-level allocation behavior shared by every OBJECT_TYPE class:
// allocate through Memory, increment the class counter, publish its name and
// class size, then zero the requested bytes.
void* AllocateMemoryClass(
    std::size_t requestedSize,
    bool objectAllocation,
    std::int32_t classId,
    const wchar_t* allocationName,
    const char* reportName,
    std::int32_t classSize
);

void DecrementMemoryClassReference(std::int32_t classId) noexcept;

// The object may not be alive here. Read its class ID from allocator metadata.
void FreeFailedMemoryClass(void* allocation) noexcept;

// ShippingMemoryClassId is metadata only. It cannot be used for runtime accounting.
void* AllocateMemoryClass(std::size_t, bool, ShippingMemoryClassId, const wchar_t*, const char*, std::int32_t) = delete;
void DecrementMemoryClassReference(ShippingMemoryClassId) = delete;

void ReportInstance();
void ReportMemory();


// Composable object-type members keep the existing methods and lifetimes.
// allocation/accessor definitions also register their type.
// allocation and release obtain the same process-local registered ID.
// Release report coordinates identify the macro invocation.
// external caller coordinates are forwarded unchanged.
#define L2_DETAIL_OBJECT_RELEASE_BODY(ClassToken)                                                                           \
    {                                                                                                                       \
        return ReleaseAs(L2_MEMORY_CLASS_ID(ClassToken), #ClassToken, L2_SOURCE_FILE, __LINE__, callerFile, callerLine);          \
    }

#define L2_DEFINE_OBJECT_RELEASE(ClassToken)                                                                                \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    std::int32_t ClassToken::Release(const char* callerFile, int callerLine, ::ObjectRefType, bool)                         \
        L2_DETAIL_OBJECT_RELEASE_BODY(ClassToken)

// Preserve the typed virtual-destructor expression and post-destruction
// accounting of this distinct source family. Do not route it via ReleaseAs.
#define L2_DEFINE_OBJECT_TYPED_RELEASE(ClassToken)                                                                          \
    std::int32_t ClassToken::Release(const char* callerFile, int callerLine, ::ObjectRefType, bool) {                       \
        const std::int32_t remaining = ::runtime::raw_atomic::AddI32(&m_nRefCount, -1);                                  \
        if (remaining == 0) {                                                                                               \
            if (!IsInTimer()) {                                                                                             \
                void* const allocation = this;                                                                              \
                this->~ClassToken();                                                                                        \
                ::runtime::process::FreeMemoryObject(allocation);                                                        \
            } else {                                                                                                        \
                ::runtime::process::ReportDeletingInTimer(#ClassToken, callerFile, callerLine);                          \
                ReportOrt(L2_SOURCE_FILE, __LINE__);                                                                              \
            }                                                                                                               \
            ::runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(ClassToken));                           \
        } else if (remaining < 0) {                                                                                         \
            ::runtime::process::ReportInvalidReference(                                                                  \
                remaining, #ClassToken, MemoryObjectID(), callerFile, callerLine                                            \
            );                                                                                                              \
            ReportOrt(L2_SOURCE_FILE, __LINE__);                                                                                  \
            ::runtime::process::ReportCallStack(2, true);                                                                \
        }                                                                                                                   \
        return remaining;                                                                                                   \
    }

#define L2_DECLARE_OBJECT_RELEASE(ClassToken)                                                                               \
    std::int32_t Release(const char* callerFile, int callerLine, ::ObjectRefType, bool)                                     \
        override L2_DETAIL_OBJECT_RELEASE_BODY(ClassToken)

#define L2_DETAIL_OBJECT_ADD_REF_BODY                                                                                       \
    {                                                                                                                       \
        ::runtime::raw_atomic::AddI32(&m_nRefCount, 1);                                                                  \
        return this;                                                                                                        \
    }

#define L2_DEFINE_OBJECT_ADD_REF(ClassToken)                                                                                \
    ::MemoryObject* ClassToken::AddRef(const char*, int, ::ObjectRefType) L2_DETAIL_OBJECT_ADD_REF_BODY

#define L2_DECLARE_OBJECT_ADD_REF()                                                                                         \
    ::MemoryObject* AddRef(const char*, int, ::ObjectRefType) override L2_DETAIL_OBJECT_ADD_REF_BODY

#define L2_DEFINE_OBJECT_CLASS_ID(ClassToken)                                                                               \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    int ClassToken::__GetClassID() noexcept { return L2_MEMORY_CLASS_ID(ClassToken); }

#define L2_DEFINE_OBJECT_CLASS_NAME(ClassToken, ClassName)                                                                  \
    const wchar_t* ClassToken::__GetClassName() noexcept { return ClassName; }

#define L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(ClassToken) L2_DEFINE_OBJECT_CLASS_NAME(ClassToken, L2_MEMORY_WIDEN(#ClassToken))

#define L2_DETAIL_OBJECT_ALLOCATION_BODY(ClassToken, ClassName, Flag)                                                       \
    {                                                                                                                       \
        return ::runtime::process::AllocateMemoryObject(                                                                 \
            requestedSize, Flag, L2_MEMORY_CLASS_ID(ClassToken), ClassName, #ClassToken, sizeof(ClassToken)                 \
        );                                                                                                                  \
    }

#define L2_DEFINE_OBJECT_ALLOCATOR(ClassToken, ClassName)                                                                   \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    void* ClassToken::operator new(std::size_t requestedSize, bool objectAllocation)                                        \
        L2_DETAIL_OBJECT_ALLOCATION_BODY(ClassToken, ClassName, objectAllocation)

#define L2_DEFINE_OBJECT_ALLOCATOR_LITERAL(ClassToken) L2_DEFINE_OBJECT_ALLOCATOR(ClassToken, L2_MEMORY_WIDEN(#ClassToken))

#define L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(ClassToken, ClassName)                                                            \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    void* ClassToken::operator new(std::size_t requestedSize) L2_DETAIL_OBJECT_ALLOCATION_BODY(ClassToken, ClassName, true)

#define L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(ClassToken)                                                               \
    L2_DEFINE_OBJECT_SINGLE_ALLOCATOR(ClassToken, L2_MEMORY_WIDEN(#ClassToken))

#define L2_DECLARE_OBJECT_SINGLE_ALLOCATOR(ClassToken, ClassName)                                                           \
    static void* operator new(std::size_t requestedSize) L2_DETAIL_OBJECT_ALLOCATION_BODY(ClassToken, ClassName, true)

#define L2_DECLARE_OBJECT_SINGLE_ALLOCATOR_LITERAL(ClassToken)                                                              \
    L2_DECLARE_OBJECT_SINGLE_ALLOCATOR(ClassToken, L2_MEMORY_WIDEN(#ClassToken))

#define L2_DEFINE_OBJECT_NEW(ClassToken)                                                                                    \
    void* ClassToken::operator new(std::size_t requestedSize) { return operator new(requestedSize, true); }

// Recovered source-level OBJECT_TYPE expansion. The current PDB records both
// private statics for every expansion: the class id and the literal
// class-name pointer returned by __GetClassName. Portable IDs now follow
// registration order while retaining the class API and object layout.
#define L2_DECLARE_MEMORY_ALLOCATION(ClassToken)                                                                            \
private:                                                                                                                    \
    L2_DECLARE_OBJECT_REGISTRATION(ClassToken)                                                                              \
    inline static const wchar_t* m_pClassName_##ClassToken = L2_MEMORY_WIDEN(#ClassToken);                                  \
                                                                                                                            \
public:                                                                                                                     \
    static int __GetClassID() noexcept { return L2_MEMORY_CLASS_ID(ClassToken); }                                           \
    static const wchar_t* __GetClassName() noexcept { return m_pClassName_##ClassToken; }                                   \
    static void* operator new(std::size_t requestedSize, bool objectAllocation) {                                           \
        return ::runtime::process::AllocateMemoryObject(                                                                 \
            requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(ClassToken), __GetClassName(), #ClassToken,                 \
            static_cast<std::int32_t>(sizeof(ClassToken))                                                                   \
        );                                                                                                                  \
    }                                                                                                                       \
    static void* operator new(std::size_t requestedSize) { return operator new(requestedSize, true); }

// infer allocation extent and report the macro invocation site.
#define L2_DECLARE_OBJECT_TYPE(ClassToken)                                                                                  \
    L2_DECLARE_MEMORY_ALLOCATION(ClassToken)                                                                                \
    ::MemoryObject* AddRef(                                                                                                 \
        const char* callerFile [[maybe_unused]], int callerLine [[maybe_unused]], ::ObjectRefType type [[maybe_unused]]     \
    ) override {                                                                                                            \
        ::runtime::raw_atomic::AddI32(&m_nRefCount, 1);                                                                  \
        return this;                                                                                                        \
    }                                                                                                                       \
    std::int32_t Release(                                                                                                   \
        const char* callerFile, int callerLine, ::ObjectRefType type [[maybe_unused]],                                      \
        bool releaseFromTimer [[maybe_unused]]                                                                              \
    ) override {                                                                                                            \
        return ReleaseAs(L2_MEMORY_CLASS_ID(ClassToken), #ClassToken, L2_SOURCE_FILE, __LINE__, callerFile, callerLine);          \
    }

// Out-of-line OBJECT_TYPE policy for owners whose current vtable/key-function
// placement requires a physical Release body in the original .cpp. This
// variant deliberately models only the proved "single-argument allocation +
// inherited AddRef + ReleaseAs" cohort. Timer-aware/custom Release owners and
// two-argument allocation surfaces are not handled by this policy.
#define L2_DECLARE_OBJECT_TYPE_OUT_OF_LINE(ClassToken)                                                                      \
public:                                                                                                                     \
    static void* operator new(std::size_t requestedSize);                                                                   \
    std::int32_t Release(const char* callerFile, int callerLine, ::ObjectRefType type, bool releaseFromTimer) override;     \
                                                                                                                            \
private:                                                                                                                    \
    L2_DECLARE_OBJECT_REGISTRATION(ClassToken)                                                                              \
    inline static const wchar_t* m_pClassName_##ClassToken = L2_MEMORY_WIDEN(#ClassToken);                                  \
                                                                                                                            \
public:

// Portable diagnostics use the invocation site. Allocation size comes from
// the complete class, whose shipping extent is checked by static_assert.
#define L2_DETAIL_DEFINE_OBJECT_RELEASE(ClassToken)                                                                         \
    std::int32_t ClassToken::Release(const char* callerFile, int callerLine, ::ObjectRefType, bool) {                       \
        return ReleaseAs(L2_MEMORY_CLASS_ID(ClassToken), #ClassToken, L2_SOURCE_FILE, __LINE__, callerFile, callerLine);          \
    }

#define L2_DEFINE_OBJECT_TYPE_OUT_OF_LINE(ClassToken)                                                                       \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    void* ClassToken::operator new(std::size_t requestedSize) {                                                             \
        return ::runtime::process::AllocateMemoryObject(                                                                 \
            requestedSize, true, L2_MEMORY_CLASS_ID(ClassToken), m_pClassName_##ClassToken, #ClassToken, sizeof(ClassToken) \
        );                                                                                                                  \
    }                                                                                                                       \
    L2_DETAIL_DEFINE_OBJECT_RELEASE(ClassToken)

// Out-of-line surface with class accessors and both allocation overloads.
// AddRef remains inherited. Keep Release in the original .cpp so the vtable
// owner remains unchanged. Report coordinates name the macro invocation.
// caller coordinates are forwarded from the external AddRef/Release caller.
#define L2_DECLARE_OBJECT_TYPE_FULL_OUT_OF_LINE(ClassToken)                                                                 \
    L2_DECLARE_OBJECT_TYPE_OUT_OF_LINE(ClassToken)                                                                          \
    static int __GetClassID() noexcept;                                                                                     \
    static const wchar_t* __GetClassName() noexcept;                                                                        \
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

#define L2_DEFINE_OBJECT_TYPE_FULL_OUT_OF_LINE(ClassToken)                                                                  \
    L2_REGISTER_OBJECT_TYPE(ClassToken)                                                                                     \
    int ClassToken::__GetClassID() noexcept { return L2_MEMORY_CLASS_ID(ClassToken); }                                      \
    const wchar_t* ClassToken::__GetClassName() noexcept { return m_pClassName_##ClassToken; }                              \
    void* ClassToken::operator new(std::size_t requestedSize, bool objectAllocation) {                                      \
        return ::runtime::process::AllocateMemoryObject(                                                                 \
            requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(ClassToken), m_pClassName_##ClassToken, #ClassToken,        \
            sizeof(ClassToken)                                                                                              \
        );                                                                                                                  \
    }                                                                                                                       \
    void* ClassToken::operator new(std::size_t requestedSize) { return operator new(requestedSize, true); }                 \
    L2_DETAIL_DEFINE_OBJECT_RELEASE(ClassToken)

// Retired product adapter declarations. Runtime ownership is now MemorySub.
// #pragma once
//
// #include <array>
// #include <atomic>
// #include <cstddef>
// #include <cstdint>
//
// struct L2ServerMemoryClassInfo final {
//     std::int32_t classId = 0
//     const char* className = nullptr
//     std::int32_t targetSize = 0
//     std::int32_t liveInstances = 0
//     bool exactCurrentIdentity = false
// }
//
// // Product-owned allocation/accounting adapter for the current L2Server
// // process. The main Shared MemoryObject stays layout-neutral and calls
// // this owner through the host-only Portable/ProcessRuntime binding.
// class L2ServerMemory final {
// public:
//     // Shipping metadata only. Runtime validity uses g_nClassCount.
//     static constexpr std::int32_t kClassCount = 295
//     static constexpr std::int32_t kCIOServerClassId = 3
//     static constexpr std::int32_t kCSTSResponseClassId = 28
//     static constexpr std::int32_t kCPacketEx20kClassId = 5
//     static constexpr std::int32_t kCIOSocketEx20kClassId = 6
//     static constexpr std::int32_t kRelaySocketClassId = 7
//     static constexpr std::int32_t kAccountClientSocketClassId = 8
//     static constexpr std::int32_t kBalthusEventClassId = 9
//     static constexpr std::int32_t kCBaseCampAgitClassId = 10
//     static constexpr std::int32_t kCWorldAgitClassId = 36
//     static constexpr std::int32_t kCAirShipCorePartClassId = 46
//     static constexpr std::int32_t kCCaptchaSocketClassId = 11
//     static constexpr std::int32_t kCCaptchaJobClassId = 12
//     static constexpr std::int32_t kCPacketEx32kClassId = 29
//     static constexpr std::int32_t kCIOSocketEx32kClassId = 30
//     static constexpr std::int32_t kICDailyMissionClassId = 14
//     static constexpr std::int32_t kCDailyMissionLevelClassId = 15
//     static constexpr std::int32_t kCDailyMissionWeekendClassId = 16
//     static constexpr std::int32_t kCDailyMissionMonthClassId = 17
//     static constexpr std::int32_t kCDailyMissionCountPerDayClassId = 18
//     static constexpr std::int32_t kCDailyMissionQuestCountClassId = 19
//     static constexpr std::int32_t kCDailyMissionOlympiadClassId = 20
//     static constexpr std::int32_t kCDailyMissionCastleWarClassId = 21
//     static constexpr std::int32_t kCDailyMissionPledgeGameClassId = 22
//     static constexpr std::int32_t kCDailyMissionInzoneCountClassId = 23
//     static constexpr std::int32_t kCDailyMissionFishingCountClassId = 24
//     static constexpr std::int32_t kCDailyMissionPledgeClassId = 25
//     static constexpr std::int32_t kCDailyMissionItemPkShieldClassId = 26
//     static constexpr std::int32_t kCNPSocketClassId = 31
//     static constexpr std::int32_t kCAreaClassId = 48
//     static constexpr std::int32_t kCBlockUpsetMemberInfoClassId = 63
//     static constexpr std::int32_t kCBossRecordClassId = 65
//     static constexpr std::int32_t kCBossRecordListClassId = 66
//     static constexpr std::int32_t kCBossRecordRankingClassId = 67
//     static constexpr std::int32_t kCBossRecordRoundClassId = 68
//     static constexpr std::int32_t kCBotCheckInfoClassId = 69
//     static constexpr std::int32_t kCBotCheckerClassId = 70
//     static constexpr std::int32_t kBotBlockDataClassId = 71
//     static constexpr std::int32_t kCBotReportAgentClassId = 72
//     static constexpr std::int32_t kCProductClassId = 73
//     static constexpr std::int32_t kCShopServerClassId = 74
//     static constexpr std::int32_t kCBuyClassId = 75
//     static constexpr std::int32_t kCBuySellClassId = 76
//     static constexpr std::int32_t kCEventCampaignClassId = 77
//     static constexpr std::int32_t kCEventCampaignManagerClassId = 78
//     static constexpr std::int32_t kCBattleCampClassId = 58
//     static constexpr std::int32_t kCPacketEx8kClassId = 55
//     static constexpr std::int32_t kCIOSocketEx8kClassId = 56
//     static constexpr std::int32_t kCChairClassId = 80
//     static constexpr std::int32_t kCCleftMemberInfoClassId = 81
//     static constexpr std::int32_t kCControlTowerClassId = 94
//     static constexpr std::int32_t kCDBClassId = 98
//     static constexpr std::int32_t kCDBSocketClassId = 99
//     static constexpr std::int32_t kCDominionManagerClassId = 101
//     static constexpr std::int32_t kCDoorClassId = 102
//     static constexpr std::int32_t kDynamicQuestInfoClassId = 107
//     static constexpr std::int32_t kDynamicContentInfoClassId = 108
//     static constexpr std::int32_t kZoneQuestInfoClassId = 109
//     static constexpr std::int32_t kDynamicQuestManagerClassId = 110
//     static constexpr std::int32_t kCEconomyClassId = 111
//     static constexpr std::int32_t kCEventControllerClassId = 113
//     static constexpr std::int32_t kCEventMatchClassId = 114
//     static constexpr std::int32_t kCFHTMLClassId = 115
//     static constexpr std::int32_t kCFriendsClassId = 122
//     static constexpr std::int32_t kCEventManagerClassId = 123
//     static constexpr std::int32_t kCGoodsInventorySocketClassId = 125
//     static constexpr std::int32_t kCGoodsInventoryJobClassId = 126
//     static constexpr std::int32_t kCNewGoodsClassId = 127
//     static constexpr std::int32_t kCInspectorClassId = 161
//     static constexpr std::int32_t kFieldCycleUserClassId = 116
//     static constexpr std::int32_t kCFieldCycleClassId = 117
//     static constexpr std::int32_t kCFieldCycleSystemClassId = 118
//     static constexpr std::int32_t kCFishingControllerClassId = 119
//     static constexpr std::int32_t kCGIPSocketClassId = 124
//     static constexpr std::int32_t kCLuckyGameSystemClassId = 184
//     static constexpr std::int32_t kCMultiPartyCommandChannelClassId = 188
//     static constexpr std::int32_t kMemoryObjectClassId = 193
//     static constexpr std::int32_t kCObjectClassId = 194
//     static constexpr std::int32_t kCWorldObjectClassId = 195
//     static constexpr std::int32_t kCAbstractCarrierClassId = 196
//     static constexpr std::int32_t kCGeneralObjectClassId = 197
//     static constexpr std::int32_t kCObjectDBClassId = 198
//     static constexpr std::int32_t kWOlympiadSocketBaseClassId = 199
//     static constexpr std::int32_t kWOlympiadNetworkHostClassId = 200
//     static constexpr std::int32_t kWOlympiadSocketHostClassId = 201
//     static constexpr std::int32_t kWOlympiadSocketPeerClassId = 202
//     static constexpr std::int32_t kWOlympiadUserClassId = 205
//     static constexpr std::int32_t kOptionClassId = 206
//     static constexpr std::int32_t kCPartyClassId = 208
//     static constexpr std::int32_t kCPathToAwakeningNoticeClassId = 211
//     static constexpr std::int32_t kPetitionCategoryClassId = 216
//     static constexpr std::int32_t kPetitionFAQClassId = 217
//     static constexpr std::int32_t kPetitionCategoryAttributeClassId = 218
//     static constexpr std::int32_t kCPetitionSocketClassId = 219
//     static constexpr std::int32_t kPledgeContributionSystemClassId = 224
//     static constexpr std::int32_t kPledgeMasteryClassId = 232
//     static constexpr std::int32_t kPledgeLevelClassId = 229
//     static constexpr std::int32_t kPledgeFunctionClassId = 225
//     static constexpr std::int32_t kPledgeSkillClassId = 231
//     static constexpr std::int32_t kCPledgeMemberClassId = 233
//     static constexpr std::int32_t kCPledgeMemberListClassId = 234
//     static constexpr std::int32_t kBasePredefinedListClassId = 237
//     static constexpr std::int32_t kPredefinedSkillListClassId = 238
//     static constexpr std::int32_t kPredefinedItemListClassId = 239
//     static constexpr std::int32_t kPredefinedTerritoryListClassId = 240
//     static constexpr std::int32_t kCPremiumServiceSocketClassId = 241
//     static constexpr std::int32_t kCBillingJobClassId = 242
//     static constexpr std::int32_t kCPremiumItemClassId = 243
//     static constexpr std::int32_t kCPVPMatchMemberInfoClassId = 244
//     static constexpr std::int32_t kCReplyCheckerClassId = 251
//     static constexpr std::int32_t kCReservationInfoClassId = 252
//     static constexpr std::int32_t kCReservationEventInfoClassId = 254
//     static constexpr std::int32_t kCSequenceJobObjectClassId = 258
//     static constexpr std::int32_t kServerClassId = 259
//     static constexpr std::int32_t kCSignBoardClassId = 263
//     static constexpr std::int32_t kCStaticObjectClassId = 271
//     static constexpr std::int32_t kCOneDayRewardDataClassId = 276
//     static constexpr std::int32_t kCToDoListManagerClassId = 277
//     static constexpr std::int32_t kOptionGroupClassId = 285
//     static constexpr std::int32_t kVariationClassId = 286
//     static constexpr std::int32_t kVariationDBClassId = 287
//
//     static L2ServerMemory& Instance() noexcept.
//
//     void* Allocate(
//         std::size_t requestedSize,
//         bool objectAllocation,
//         std::int32_t classId,
//         const wchar_t* allocationName,
//         const char* reportName,
//         std::int32_t targetClassSize)
//     void Free(void* allocation) noexcept.
//     void DecrementClassReference(std::int32_t classId) noexcept
//
//     L2ServerMemoryClassInfo ClassInfo(std::int32_t classId) const noexcept
//     std::uint32_t AllocationLogicalSize(const void* allocation) const noexcept
//
// private:
//     L2ServerMemory() noexcept.
//
//     static bool ValidClassId(std::int32_t classId) noexcept
//
//
// }
//
// static_assert(L2ServerMemory::kClassCount == 295)
// static_assert(L2ServerMemory::kCIOServerClassId == 3)
// static_assert(L2ServerMemory::kCSTSResponseClassId == 28)
// static_assert(L2ServerMemory::kCPacketEx20kClassId == 5)
// static_assert(L2ServerMemory::kCIOSocketEx20kClassId == 6)
// static_assert(L2ServerMemory::kRelaySocketClassId == 7)
// static_assert(L2ServerMemory::kAccountClientSocketClassId == 8)
// static_assert(L2ServerMemory::kBalthusEventClassId == 9)
// static_assert(L2ServerMemory::kCBaseCampAgitClassId == 10)
// static_assert(L2ServerMemory::kCWorldAgitClassId == 36)
// static_assert(L2ServerMemory::kCAirShipCorePartClassId == 46)
// static_assert(L2ServerMemory::kCCaptchaSocketClassId == 11)
// static_assert(L2ServerMemory::kCCaptchaJobClassId == 12)
// static_assert(L2ServerMemory::kCPacketEx32kClassId == 29)
// static_assert(L2ServerMemory::kCIOSocketEx32kClassId == 30)
// static_assert(L2ServerMemory::kICDailyMissionClassId == 14)
// static_assert(L2ServerMemory::kCDailyMissionItemPkShieldClassId == 26)
// static_assert(L2ServerMemory::kCNPSocketClassId == 31)
// static_assert(L2ServerMemory::kCAreaClassId == 48)
// static_assert(L2ServerMemory::kCPacketEx8kClassId == 55)
// static_assert(L2ServerMemory::kCIOSocketEx8kClassId == 56)
// static_assert(L2ServerMemory::kCBlockUpsetMemberInfoClassId == 63)
// static_assert(L2ServerMemory::kCBossRecordClassId == 65)
// static_assert(L2ServerMemory::kCBossRecordListClassId == 66)
// static_assert(L2ServerMemory::kCBossRecordRankingClassId == 67)
// static_assert(L2ServerMemory::kCBossRecordRoundClassId == 68)
// static_assert(L2ServerMemory::kCBotCheckInfoClassId == 69)
// static_assert(L2ServerMemory::kCBotCheckerClassId == 70)
// static_assert(L2ServerMemory::kBotBlockDataClassId == 71)
// static_assert(L2ServerMemory::kCBotReportAgentClassId == 72)
// static_assert(L2ServerMemory::kCProductClassId == 73)
// static_assert(L2ServerMemory::kCShopServerClassId == 74)
// static_assert(L2ServerMemory::kCBuyClassId == 75)
// static_assert(L2ServerMemory::kCBuySellClassId == 76)
// static_assert(L2ServerMemory::kCEventCampaignClassId == 77)
// static_assert(L2ServerMemory::kCEventCampaignManagerClassId == 78)
// static_assert(L2ServerMemory::kCBattleCampClassId == 58)
// static_assert(L2ServerMemory::kCChairClassId == 80)
// static_assert(L2ServerMemory::kCCleftMemberInfoClassId == 81)
// static_assert(L2ServerMemory::kCControlTowerClassId == 94)
// static_assert(L2ServerMemory::kCDBClassId == 98)
// static_assert(L2ServerMemory::kCDBSocketClassId == 99)
// static_assert(L2ServerMemory::kCDominionManagerClassId == 101)
// static_assert(L2ServerMemory::kCDoorClassId == 102)
// static_assert(L2ServerMemory::kDynamicQuestInfoClassId == 107)
// static_assert(L2ServerMemory::kDynamicContentInfoClassId == 108)
// static_assert(L2ServerMemory::kZoneQuestInfoClassId == 109)
// static_assert(L2ServerMemory::kDynamicQuestManagerClassId == 110)
// static_assert(L2ServerMemory::kCEconomyClassId == 111)
// static_assert(L2ServerMemory::kCEventControllerClassId == 113)
// static_assert(L2ServerMemory::kCEventMatchClassId == 114)
// static_assert(L2ServerMemory::kCFHTMLClassId == 115)
// static_assert(L2ServerMemory::kCFriendsClassId == 122)
// static_assert(L2ServerMemory::kCEventManagerClassId == 123)
// static_assert(L2ServerMemory::kCGoodsInventorySocketClassId == 125)
// static_assert(L2ServerMemory::kCGoodsInventoryJobClassId == 126)
// static_assert(L2ServerMemory::kCNewGoodsClassId == 127)
// static_assert(L2ServerMemory::kCInspectorClassId == 161)
// static_assert(L2ServerMemory::kFieldCycleUserClassId == 116)
// static_assert(L2ServerMemory::kCFieldCycleClassId == 117)
// static_assert(L2ServerMemory::kCFieldCycleSystemClassId == 118)
// static_assert(L2ServerMemory::kCFishingControllerClassId == 119)
// static_assert(L2ServerMemory::kCGIPSocketClassId == 124)
// static_assert(L2ServerMemory::kCLuckyGameSystemClassId == 184)
// static_assert(
//     L2ServerMemory::kCMultiPartyCommandChannelClassId == 188)
// static_assert(L2ServerMemory::kMemoryObjectClassId == 193)
// static_assert(L2ServerMemory::kCObjectClassId == 194)
// static_assert(L2ServerMemory::kCWorldObjectClassId == 195)
// static_assert(L2ServerMemory::kCAbstractCarrierClassId == 196)
// static_assert(L2ServerMemory::kCGeneralObjectClassId == 197)
// static_assert(L2ServerMemory::kCObjectDBClassId == 198)
// static_assert(L2ServerMemory::kWOlympiadSocketBaseClassId == 199)
// static_assert(L2ServerMemory::kWOlympiadNetworkHostClassId == 200)
// static_assert(L2ServerMemory::kWOlympiadSocketHostClassId == 201)
// static_assert(L2ServerMemory::kWOlympiadSocketPeerClassId == 202)
// static_assert(L2ServerMemory::kWOlympiadUserClassId == 205)
// static_assert(L2ServerMemory::kOptionClassId == 206)
// static_assert(L2ServerMemory::kCPartyClassId == 208)
// static_assert(
//     L2ServerMemory::kCPathToAwakeningNoticeClassId == 211)
// static_assert(L2ServerMemory::kPetitionCategoryClassId == 216)
// static_assert(L2ServerMemory::kPetitionFAQClassId == 217)
// static_assert(
//     L2ServerMemory::kPetitionCategoryAttributeClassId == 218)
// static_assert(L2ServerMemory::kCPetitionSocketClassId == 219)
// static_assert(L2ServerMemory::kPledgeContributionSystemClassId == 224)
// static_assert(L2ServerMemory::kPledgeMasteryClassId == 232)
// static_assert(L2ServerMemory::kPledgeLevelClassId == 229)
// static_assert(L2ServerMemory::kPledgeFunctionClassId == 225)
// static_assert(L2ServerMemory::kPledgeSkillClassId == 231)
// static_assert(L2ServerMemory::kCPledgeMemberClassId == 233)
// static_assert(L2ServerMemory::kCPledgeMemberListClassId == 234)
// static_assert(L2ServerMemory::kBasePredefinedListClassId == 237)
// static_assert(L2ServerMemory::kPredefinedSkillListClassId == 238)
// static_assert(L2ServerMemory::kPredefinedItemListClassId == 239)
// static_assert(L2ServerMemory::kPredefinedTerritoryListClassId == 240)
// static_assert(L2ServerMemory::kCPremiumServiceSocketClassId == 241)
// static_assert(L2ServerMemory::kCBillingJobClassId == 242)
// static_assert(L2ServerMemory::kCPremiumItemClassId == 243)
// static_assert(L2ServerMemory::kCPVPMatchMemberInfoClassId == 244)
// static_assert(L2ServerMemory::kCReplyCheckerClassId == 251)
// static_assert(L2ServerMemory::kCReservationInfoClassId == 252)
// static_assert(L2ServerMemory::kCReservationEventInfoClassId == 254)
// static_assert(L2ServerMemory::kCSequenceJobObjectClassId == 258)
// static_assert(L2ServerMemory::kServerClassId == 259)
// static_assert(L2ServerMemory::kCSignBoardClassId == 263)
// static_assert(L2ServerMemory::kCStaticObjectClassId == 271)
// static_assert(L2ServerMemory::kCOneDayRewardDataClassId == 276)
// static_assert(L2ServerMemory::kCToDoListManagerClassId == 277)
// static_assert(L2ServerMemory::kOptionGroupClassId == 285)
// static_assert(L2ServerMemory::kVariationClassId == 286)
// static_assert(L2ServerMemory::kVariationDBClassId == 287)

struct MemoryClassInfo final {
    std::int32_t classId = 0;
    const char* className = nullptr;
    std::int32_t classSize = 0;
    std::int32_t liveInstances = 0;
    bool exactCurrentIdentity = false;
};

MemoryClassInfo GetMemoryClassInfo(std::int32_t classId) noexcept;
std::uint32_t AllocationLogicalSize(const void* allocation) noexcept;
