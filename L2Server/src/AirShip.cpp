#include "AirShip.h"
#include "AirShipAction.h"
#include "AirShipPart.h"
#include "CConfig.h"
#include "CWorld.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Fishing.h"
#include "IOCPMessageServer.h"
#include "Item.h"
#include "NPC.h"
#include "NewActionMove.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "ParserAirShip.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "ReplyChecker.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"


#include "Shared/src/Exception.h"


#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <limits>
#include <string_view>

// Current L2Server keeps only the two AirShip.cpp OBJECT_TYPE class-id/name
// publications. The matched PDB closes their physical declarations and
// layouts, but no current instance method, vtable or RTTI body survives.
class CAlarm : public CIOObject {
public:
    CAlarm(std::uint64_t reservedAlarmCount, std::uint32_t delay);
    ~CAlarm() override;

    L2_DECLARE_OBJECT_TYPE(CAlarm)

    void TimerExpired(int id) override;
    void Start();
    virtual bool OnWake(std::uint64_t index) = 0;

    std::uint64_t m_reservedAlarmCount;
    std::uint32_t m_delay;
    std::uint32_t m_padding24To28;
    std::uint64_t m_index;
};

class CFireAllCannonAlarm : public CAlarm {
public:
    CFireAllCannonAlarm(CAirShip* airShip, std::uint32_t delay);
    ~CFireAllCannonAlarm() override = default;

    L2_DECLARE_OBJECT_TYPE(CFireAllCannonAlarm)

    bool OnWake(std::uint64_t index) override;

    CSmartID m_spAirShip;
    std::uint32_t m_padding34To38;
};


namespace {

constexpr const char* kAirShipSource = "AirShip.cpp";
#if defined(_WIN32)
constexpr const wchar_t* kAirShipDataPath = L"..\\Script\\airship.txt";
#else
constexpr const wchar_t* kAirShipDataPath = L"../script/airship.txt";
#endif

int CompareAirShipBuilderToken(const char16_t* left, const char16_t* right) {
#if defined(_WIN32)
    return ::_wcsicmp(Utf16ToWide(left).c_str(), Utf16ToWide(right).c_str());
#else
    while (*left != u'\0' && *right != u'\0') {
        const wint_t foldedLeft = std::towlower(*left);
        const wint_t foldedRight = std::towlower(*right);
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
#endif
}
} // namespace

CAirShipConst::CAirShipConst(std::int32_t typeId) :
    m_TypeId(typeId),
    m_Type(ASTE_REGULAR),
    m_Speed{0.0, 0.0, 0.0},
    m_MaxPassenger(100) {
    // Current regular/free constructors leave padding_2c unwritten.
}

CAirShipConst::~CAirShipConst() = default;

bool CAirShipConst::IsForRegular() const { return false; }

bool CAirShipConst::IsForFree() const { return false; }

CAirShipConstRegular* CAirShipConst::CastForRegular() { return nullptr; }

CAirShipConstFree* CAirShipConst::CastForFree() { return nullptr; }

int CAirShipConst::GetMaxFuel() const { return 0; }

FVector CAirShipConst::GetSteeringPos() const { return {}; }

FVector CAirShipConst::GetDriverPos() const { return {}; }

int CAirShipConst::GetSteeringClassId() const { return 0; }

void CAirShipConst::AddCollision(const FVector& position, double radius, double height) {
    m_RawCollisionList.push_back(CollisionData{position, radius, height});
}

void CAirShipConst::OnLoad() {
    L2_GUARD_FUNCTION();
    int minimumIndex = -1;
    int maximumIndex = -1;
    int minimumLeft = std::numeric_limits<int>::max();
    int maximumLeft = std::numeric_limits<int>::min();

    for (std::size_t index = 0; index < m_RawCollisionList.size(); ++index) {
        const CollisionData& raw = m_RawCollisionList[index];
        const int x = static_cast<int>(raw.position.x);
        const int y = static_cast<int>(raw.position.y);
        const int z = static_cast<int>(raw.position.z);
        const int radius = static_cast<int>(raw.radius);
        const int halfHeight = static_cast<int>(raw.height * 0.5);
        const int upperOffset = static_cast<int>(raw.height * -0.5);
        const int left = x - radius;

        if (left < minimumLeft) {
            minimumLeft = left;
            minimumIndex = static_cast<int>(index);
        }
        if (maximumLeft < left) {
            maximumLeft = left;
            maximumIndex = static_cast<int>(index);
        }

        const double targetHeight = static_cast<double>(halfHeight);
        m_CollisionList.push_back(CollisionData{FVector{-x, y + radius, z - halfHeight}, raw.radius, targetHeight});
        m_CollisionList.push_back(CollisionData{FVector{-x, y + radius, z - upperOffset}, raw.radius, targetHeight});
        m_CollisionList.push_back(CollisionData{FVector{-x, y - radius, z - halfHeight}, raw.radius, targetHeight});
        m_CollisionList.push_back(CollisionData{FVector{-x, y - radius, z - upperOffset}, raw.radius, targetHeight});
    }

    const CollisionData& minimum = m_RawCollisionList[static_cast<std::size_t>(minimumIndex)];
    const int minimumX = static_cast<int>(minimum.position.x);
    const int minimumY = static_cast<int>(minimum.position.y);
    const int minimumZ = static_cast<int>(minimum.position.z);
    const int minimumRadius = static_cast<int>(minimum.radius);
    const int minimumHalf = static_cast<int>(minimum.height * 0.5);
    const int minimumUpper = static_cast<int>(minimum.height * -0.5);
    const double minimumTargetHeight = static_cast<double>(minimumHalf);
    m_CollisionList.push_back(
        CollisionData{
            FVector{minimumRadius - minimumX, minimumY, minimumZ - minimumHalf}, minimum.radius, minimumTargetHeight}
    );
    m_CollisionList.push_back(
        CollisionData{
            FVector{minimumRadius - minimumX, minimumY, minimumZ - minimumUpper}, minimum.radius, minimumTargetHeight}
    );

    const CollisionData& maximum = m_RawCollisionList[static_cast<std::size_t>(maximumIndex)];
    const int maximumX = static_cast<int>(maximum.position.x);
    const int maximumY = static_cast<int>(maximum.position.y);
    const int maximumZ = static_cast<int>(maximum.position.z);
    const int maximumRadius = static_cast<int>(maximum.radius);
    const int maximumHalf = static_cast<int>(maximum.height * 0.5);
    const int maximumUpper = static_cast<int>(maximum.height * -0.5);
    m_CollisionList.push_back(
        CollisionData{
            FVector{-(maximumX + maximumRadius), maximumY, maximumZ - maximumHalf}, maximum.radius, maximum.height * 0.5}
    );
    m_CollisionList.push_back(
        CollisionData{
            FVector{-(maximumX + maximumRadius), maximumY, maximumZ - maximumUpper}, maximum.radius, maximum.height * 0.5}
    );
}

CAirShipConstRegular::CAirShipConstRegular(std::int32_t typeId) : CAirShipConst(typeId) {}

CAirShipConstRegular::~CAirShipConstRegular() = default;

void CAirShipConstRegular::OnLoad() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x0046d734 does not call the base
    // OnLoad owner. It only appends one zero collision record.
    m_CollisionList.push_back(CollisionData{});
}

bool CAirShipConstRegular::IsForRegular() const { return true; }

CAirShipConstRegular* CAirShipConstRegular::CastForRegular() { return this; }

void CAirShipConstRegular::AddPathNode(const AirShipPathNode& node) { m_Path.push_back(node); }

void CAirShipConstRegular::SetReturnInfo(AirShipPathNodeType type) {
    L2_GUARD_FUNCTION();
    if (m_Path.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] start node does not exist.",
        //     kAirShipSourceWide,
        //     0x1d7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] start node does not exist.",
        //     kAirShipSourceWide,
        //     0x1d7)
        L2_LOG_ERROR(
            Log, "Empty path for regular airship template %d: cannot set starting node type %d.", m_TypeId,
            static_cast<int>(type)
        );
    }
    m_Path[0].nodeType = type;
}

const AirShipPathNode& CAirShipConstRegular::GetNode(std::int32_t index) const {
    L2_GUARD_FUNCTION();
    static AirShipPathNode nullNode;
    if (index < 0 || static_cast<std::size_t>(index) >= m_Path.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid node(%d) of regular airship path",
        //     index)
        // L2_LOG_ERROR(Log,
        //     L"Invalid node(%d) of regular airship path",
        //     index)
        L2_LOG_ERROR(
            Log, "Regular airship template %d: node index %d is outside the %zu-node path; returning an empty node.",
            m_TypeId, index, m_Path.size()
        );
        return nullNode;
    }
    return m_Path[static_cast<std::size_t>(index)];
}

std::int32_t CAirShipConstRegular::GetNextIndex(std::int32_t index) const {
    L2_GUARD_FUNCTION();
    const int size = static_cast<int>(m_Path.size());
    if (index >= 0 && index < size && index + 1 < size) {
        return index + 1;
    }
    return 0;
}

const AirShipPathNode& CAirShipConstRegular::GetNextNode(std::int32_t& index) const {
    L2_GUARD_FUNCTION();
    static AirShipPathNode nullNode;
    const int size = static_cast<int>(m_Path.size());
    if (index < 0 || index >= size) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid now node(%d) of regular airship path",
        //     index)
        // L2_LOG_ERROR(Log,
        //     L"Invalid now node(%d) of regular airship path",
        //     index)
        L2_LOG_ERROR(
            Log,
            "Regular airship template %d: cannot advance invalid node index %d in the %d-node path; returning an empty node "
            "without changing the index.",
            m_TypeId, index, size
        );
        return nullNode;
    }
    ++index;
    if (index >= size) {
        index = 0;
    }
    return m_Path[static_cast<std::size_t>(index)];
}

bool CAirShipConstRegular::IsEmptyPath() const { return m_Path.empty(); }

CAirShipConstFree::CAirShipConstFree(std::int32_t typeId) :
    CAirShipConst(typeId),
    // Current 00c7bc48..00c7bcb8 leaves fuel and steering scalars unwritten.
    // Default initialization of m_SteeringInfo zeroes only its two FVector
    // members, preserving the untouched steering class ID and padding.
    m_BuyCost{0, 0, 0},
    m_SummonCost{0, 0, 0} {}

CAirShipConstFree::~CAirShipConstFree() = default;

bool CAirShipConstFree::IsForFree() const { return true; }

CAirShipConstFree* CAirShipConstFree::CastForFree() { return this; }

int CAirShipConstFree::GetMaxFuel() const { return m_MaxFuel; }

FVector CAirShipConstFree::GetSteeringPos() const { return m_SteeringInfo.m_SteeringPos; }

FVector CAirShipConstFree::GetDriverPos() const { return m_SteeringInfo.m_DriverPos; }

int CAirShipConstFree::GetSteeringClassId() const { return m_SteeringInfo.m_SteeringClassId; }

void CAirShipConstFree::SetFuel(std::int32_t maximum, std::int32_t consume) {
    m_MaxFuel = maximum;
    m_FuelConsume = consume;
}

void CAirShipConstFree::SetSteeringPos(const FVector& position) { m_SteeringInfo.m_SteeringPos = position; }

void CAirShipConstFree::SetDriverPos(const FVector& position) { m_SteeringInfo.m_DriverPos = position; }

void CAirShipConstFree::SetSteeringItem(const char16_t* name) {
    m_SteeringInfo.m_SteeringClassId = g_ObjectDB.GetClassIdFromName(name, false);
}

void CAirShipConstFree::SetBuyCost(const char16_t* name, std::int32_t count) {
    L2_GUARD_FUNCTION();
    m_BuyCost.m_ItemClassId = g_ObjectDB.GetClassIdFromName(name, false);
    m_BuyCost.m_ItemCount = count;
}

void CAirShipConstFree::SetSummonCost(const char16_t* name, std::int32_t count) {
    L2_GUARD_FUNCTION();
    m_SummonCost.m_ItemClassId = g_ObjectDB.GetClassIdFromName(name, false);
    m_SummonCost.m_ItemCount = count;
}

bool CAirShipConstFree::CanPayCost(User* user, const AirShipCostType& cost, std::int32_t messageId) {
    // Current L2Server 0x004609c4, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (cost.m_ItemClassId == 0 && cost.m_ItemCount == 0) {
        return true;
    }

    if (!user->m_inv.HaveItemByClassId(cost.m_ItemClassId, static_cast<std::int64_t>(cost.m_ItemCount), false, false)) {
        if (cost.m_ErrorMessageId > 0) {
            user->GetSocket()->Send(
                "chccd", 0x62, static_cast<int>(static_cast<std::int16_t>(messageId)), 1, 3, cost.m_ItemClassId
            );
        }
        return false;
    }
    return true;
}

bool CAirShipConstFree::PayCost(User* user, const AirShipCostType& cost, std::int32_t messageId) {
    // Current L2Server 0x00460ae8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (cost.m_ItemClassId == 0 && cost.m_ItemCount == 0) {
        return true;
    }

    ItemManip manipulation(0, cost.m_ItemClassId, runtime::NegateInt32Wrapping(cost.m_ItemCount), 0, 0, 0, true);
    const bool paid = user->HandleItemManipulation(1, &manipulation, nullptr, 0, nullptr, false, ItemGetReason::Etc);
    if (!paid && cost.m_ErrorMessageId > 0) {
        user->GetSocket()->Send(
            "chccd", 0x62, static_cast<int>(static_cast<std::int16_t>(messageId)), 1, 3, cost.m_ItemClassId
        );
    }
    return paid;
}

bool CAirShipConstFree::CanPayBuyConst(User* user) {
    // Current L2Server 0x00463870, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    return CanPayCost(user, m_BuyCost, ERROR_MSG_ON_BUY);
}

bool CAirShipConstFree::PayBuyCost(User* user) {
    // Current L2Server 0x00463924, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    return PayCost(user, m_BuyCost, ERROR_MSG_ON_BUY);
}

bool CAirShipConstFree::CanPaySummonConst(User* user) {
    // Current L2Server 0x004639d8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    return CanPayCost(user, m_SummonCost, ERROR_MSG_ON_SUMMON);
}

bool CAirShipConstFree::PaySummonCost(User* user) {
    // Current L2Server 0x00463a8c, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    return PayCost(user, m_SummonCost, ERROR_MSG_ON_SUMMON);
}

CAirport::CAirport(std::int32_t id) : m_Id(id) {}

CAirport::~CAirport() = default;

bool CAirport::IsForRegular() const { return false; }

bool CAirport::IsForFree() const { return false; }

CAirportForRegular* CAirport::CastForRegular() { return nullptr; }

CAirportForFree* CAirport::CastForFree() { return nullptr; }

void CAirport::AddGetOffInfo(CAirportPlatform* platform, const FVector& position, std::int32_t sceneId, std::int32_t line) {
    L2_GUARD_FUNCTION();
    const int expected = static_cast<int>(m_GetOffInfoMap.size());
    const int id = platform != nullptr ? platform->Id() : expected;
    if (platform == nullptr || id == expected) {
        m_GetOffInfoMap.emplace(id, GetOffInfo{position, sceneId, 0});
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] platform id must be %d after %d, but now %d. at line[%d]",
    //     kAirShipSourceWide,
    //     0xda,
    //     expected,
    //     expected - 1,
    //     id,
    //     line)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] platform id must be %d after %d, but now %d. at line[%d]",
    //     kAirShipSourceWide,
    //     0xda,
    //     expected,
    //     expected - 1,
    //     id,
    //     line)
    L2_LOG_ERROR(
        Log,
        "Airport %d: disembarkation platform ID %d is out of sequence (expected %d) at airship.txt line %d; entry ignored.",
        m_Id, id, expected, line
    );
}

const CAirport::GetOffInfo& CAirport::GetGetOffInfo(std::int32_t platformId) {
    // Current L2Server 0x00468194.
    L2_GUARD_FUNCTION();
    if (platformId >= 0) {
        const auto position = m_GetOffInfoMap.find(platformId);
        if (position != m_GetOffInfoMap.end()) {
            return position->second;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] getoff info of platform[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0xfe,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] getoff info of platform[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0xfe,
        //     platformId)
        L2_LOG_ERROR(
            Log, "Airport %d has no disembarkation data for platform %d; trying a random entry among %zu entries.", m_Id,
            platformId, m_GetOffInfoMap.size()
        );
    }

    const int randomId = static_cast<int>(::genrand64_real2() * static_cast<double>(m_GetOffInfoMap.size()) + 0.0);
    const auto randomPosition = m_GetOffInfoMap.find(randomId);
    if (randomPosition != m_GetOffInfoMap.end()) {
        return randomPosition->second;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] getoff info of platform[%d] does not exist.",
    //     kAirShipSourceWide,
    //     0x10d,
    //     randomId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] getoff info of platform[%d] does not exist.",
    //     kAirShipSourceWide,
    //     0x10d,
    //     randomId)
    L2_LOG_ERROR(
        Log, "Airport %d: random disembarkation entry %d is missing (%zu entries); returning empty disembarkation data.",
        m_Id, randomId, m_GetOffInfoMap.size()
    );
    static GetOffInfo nullInfo;
    return nullInfo;
}

CAirportForRegular::CAirportForRegular(std::int32_t id) : CAirport(id) {}

CAirportForRegular::~CAirportForRegular() = default;

bool CAirportForRegular::IsForRegular() const { return true; }

CAirportForRegular* CAirportForRegular::CastForRegular() { return this; }

const FVector& CAirportForRegular::GetAirShipPos() const { return m_AirShipPos; }

void CAirportPlatform::SetOccupied(bool occupied) {
    // Current L2Server 0x004606d8.
    L2_GUARD_FUNCTION();
    SharedPlatformInfo::Inst()->SetOccupied(m_SharedIndex, occupied);
}

bool CAirportPlatform::IsOccupied() const {
    // Current L2Server 0x00460788.
    L2_GUARD_FUNCTION();
    return SharedPlatformInfo::Inst()->IsOccupied(m_SharedIndex);
}

const FVector& CAirportPlatform::GetLandingPath(std::int32_t index) const {
    // Current L2Server 0x00463230.
    L2_GUARD_FUNCTION();
    if (index < 0 || static_cast<std::size_t>(index) >= m_LandingPath.size()) {
        static FVector zero;
        return zero;
    }
    return m_LandingPath[static_cast<std::size_t>(index)];
}

const FVector& CAirportPlatform::GetTakeOffPath(std::int32_t index) const {
    // Current L2Server 0x0046333c.
    L2_GUARD_FUNCTION();
    if (index < 0 || static_cast<std::size_t>(index) >= m_TakeOffPath.size()) {
        static FVector zero;
        return zero;
    }
    return m_TakeOffPath[static_cast<std::size_t>(index)];
}

CAirportForFree::CAirportForFree(std::int32_t id) : CAirport(id) {}

CAirportForFree::~CAirportForFree() = default;

bool CAirportForFree::IsForFree() const { return true; }

CAirportForFree* CAirportForFree::CastForFree() { return this; }

void CAirportForFree::AddPlatform(CAirportPlatform* platform) {
    L2_GUARD_FUNCTION();
    const int sharedIndex = SharedPlatformInfo::Inst()->AddPlatform(m_Id, platform->Id());
    if (sharedIndex == -1) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"[%s][%d] platform[%d, %d] does not have shared info.",
        //     kAirShipSourceWide,
        //     0x158,
        //     m_Id,
        //     platform->Id())
        // L2_LOG(Log,
        //     LOG_LOCAL,
        //     L"[%s][%d] platform[%d, %d] does not have shared info.",
        //     kAirShipSourceWide,
        //     0x158,
        //     m_Id,
        //     platform->Id())
        // Noise: The following ERROR call keeps the mandatory local copy without a second console record.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] platform[%d, %d] does not have shared info.",
        //     kAirShipSourceWide,
        //     0x15a,
        //     m_Id,
        //     platform->Id())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] platform[%d, %d] does not have shared info.",
        //     kAirShipSourceWide,
        //     0x15a,
        //     m_Id,
        //     platform->Id())
        L2_LOG_ERROR_WITH_LOCAL_COPY(
            Log, "Airport %d, platform %d: shared platform allocation failed; initiating a fatal crash report.", m_Id,
            platform->Id()
        );
        ForceCrash(CrashInfo::SHARED_PLATFORM_INFO_IS_INVALID);
    }
    platform->SetSharedIndex(sharedIndex);
    m_PlatformMap.emplace(platform->Id(), platform);
}

void CAirportForFree::SendTeleportList(User* user) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x4f9c> buffer;
    int length = 0;
    int count = 0;
    for (const auto& entry : m_TeleportMap) {
        CAirportTeleport* const teleport = entry.second;
        if (teleport == nullptr) {
            continue;
        }

        ++count;
        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "ddddd", entry.first, teleport->m_FuelConsume,
            static_cast<int>(teleport->m_Pos.x), static_cast<int>(teleport->m_Pos.y), static_cast<int>(teleport->m_Pos.z)
        );
        if (assembled > 0) {
            length += assembled;
        }
    }

    user->GetSocket()->Send("chddb", 0xfe, 0x9b, m_Id, count, length, buffer.data());
}

CAirportTeleport* CAirportForFree::GetTeleport(std::int32_t id) {
    L2_GUARD_FUNCTION();
    const auto position = m_TeleportMap.find(id);
    if (position == m_TeleportMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Teleport info[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0x121,
        //     id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Teleport info[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0x121,
        //     id)
        L2_LOG_ERROR(Log, "Airport %d: teleport entry %d not found; lookup returns no destination.", m_Id, id);
        return nullptr;
    }
    return position->second;
}

CAirportPlatform* CAirportForFree::GetPlatform(std::int32_t id) {
    L2_GUARD_FUNCTION();
    const auto position = m_PlatformMap.find(id);
    if (position == m_PlatformMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] platform[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0x171,
        //     id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] platform[%d] does not exist.",
        //     kAirShipSourceWide,
        //     0x171,
        //     id)
        L2_LOG_ERROR(Log, "Airport %d: platform %d not found; lookup returns no platform.", m_Id, id);
        return nullptr;
    }
    return position->second;
}

CAirShipAreaDB& CAirShipAreaDB::Inst() {
    static CAirShipAreaDB instance;
    return instance;
}

void CAirShipAreaDB::AddArea(CAirShipArea* area) {
    L2_GUARD_FUNCTION();
    const std::size_t index = static_cast<std::size_t>((area->m_MapX - 10) * 17 + area->m_MapY - 10);
    m_AreaList[index].push_back(area);
}

bool CAirShipArea::Process(CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    if (m_Type == ASAT_AIRPORT) {
        airShip->Landing(m_Option);
    } else if (m_Type == ASAT_SPEED) {
        airShip->m_SpeedAreaFactor = std::min(airShip->m_SpeedAreaFactor, m_Option);
    } else {
        return false;
    }
    return true;
}

void CAirShipAreaDB::CheckAirShip(CAirShip* airShip) {
    L2_GUARD_FUNCTION();
    const FVector position = airShip->GetPos();
    const std::int32_t mapX = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.x), 0x50000) >> 15;
    const std::int32_t mapY = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(position.y), 0x40000) >> 15;

    if (CWorld::Inst().GetGeo()->GetZone(mapX + 10, mapY + 10) == nullptr) {
        return;
    }

    const std::size_t index =
        static_cast<std::size_t>(runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(mapX, 17), mapY));
    for (CAirShipArea* const area : m_AreaList[index]) {
        if (area->m_Territory.IsInside(position, true)) {
            area->Process(airShip);
        }
    }
}

CAirShipManager::CAirShipManager() = default;
CAirShipManager::~CAirShipManager() = default;

CAirShipManager& CAirShipManager::Inst() {
    static CAirShipManager instance;
    return instance;
}

void CAirShipManager::DebugAirShipInfo(User* const user, CAirShip* const airShip) {
    // Current L2Server 0x00461900, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (airShip != nullptr) {
        airShip->SendDebugInfo(user);
    }
}

bool CAirShipManager::CanGetOnAirShip(User* const user, const std::uint32_t airShipId) {
    // Current L2Server 0x0046fe10, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (candidate != nullptr && candidate->IsAirShip()) {
        return candidate->CastAirShip()->CanGetOn(user, false);
    }
    return false;
}

bool CAirShipManager::GetOnAirShip(User* const user, const std::uint32_t airShipId) {
    // Current L2Server 0x0046fef0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (candidate != nullptr && candidate->IsAirShip()) {
        return candidate->CastAirShip()->GetOn(user, false);
    }
    user->SendSystemMessage(SystemMessageId::CANNOT_BOARD_AIRSHIP_NONE_SUMMONED);
    return false;
}

void CAirShipManager::GetOnAirShip(User* const user, CAirShip* const airShip) {
    // Current L2Server 0x0046ffe4, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (airShip != nullptr) {
        static_cast<void>(airShip->GetOn(user, true));
    }
}

void CAirShipManager::LoadAirShipFromDB() {
    // Current L2Server 0x004616d8.
    L2_GUARD_FUNCTION();
    db.RequestLoadAirShip();
}

void CAirShipManager::OnLoadPledge(CPledge* const pledge) {
    // Current L2Server 0x004691b8.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }
    L2_GUARD(m_AirShipLock);
    if (m_FreeAirShipDBMap.find(pledge->GetDbId()) != m_FreeAirShipDBMap.end()) {
        pledge->SharedData()->m_AirShipCount = 1;
    }
}

bool CAirShipManager::AddConst(CAirShipConst* airShipConst) {
    L2_GUARD_FUNCTION();
    airShipConst->OnLoad();
    if (m_AirShipConstMap.find(airShipConst->m_TypeId) != m_AirShipConstMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated airship const[%d].",
        //     kAirShipSourceWide,
        //     0x992,
        //     airShipConst->m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] duplicated airship const[%d].",
        //     kAirShipSourceWide,
        //     0x992,
        //     airShipConst->m_TypeId)
        L2_LOG_ERROR(
            Log, "Duplicate airship template %d rejected; the previously registered template is retained.",
            airShipConst->m_TypeId
        );
        return false;
    }
    m_AirShipConstMap.emplace(airShipConst->m_TypeId, airShipConst);
    return true;
}

CAirport* CAirShipManager::GetAirport(std::int32_t id) {
    L2_GUARD_FUNCTION();
    const auto position = m_AirportMap.find(id);
    return position == m_AirportMap.end() ? nullptr : position->second;
}

CAirShip* CAirShipManager::_CreateAirShip(
    CAirShipConst* const airShipConst, const std::uint32_t dbId, const std::uint32_t ownerDbId, const std::int32_t fuel
) {
    // Current L2Server 0x0046de9c.
    L2_GUARD_FUNCTION();
    CAirShip* airShip = nullptr;
    if (!airShipConst->IsForFree()) {
        airShip = new CAirShipForRegular(airShipConst);
    } else {
        airShip = new CAirShipForFree(airShipConst);
    }
    airShip->OnCreate(dbId, ownerDbId, fuel);
    m_AirShipInstList.insert(airShip->m_nId);
    return airShip;
}

namespace AirShipManagerHelper {

bool CanBuy(User* const user, AirShipTypeEnum) {
    // Current L2Server 0x00461770. The current machine leaves
    // buying disabled after touching the real pledge owner and reporting
    // system message 0x998.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        static_cast<void>(user->GetPledge());
        user->SendSystemMessage(SystemMessageId::CLAN_LEVEL_TOO_LOW_TO_ACQUIRE_AIRSHIP);
    }
    return false;
}

bool CanSummon(User* const user) {
    // Current L2Server 0x00461830, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->HavePledgePower(PP_SUMMON_AIRSHIP)) {
        return true;
    }
    if (user != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_WITHOUT_LICENSE_OR_CLAN_AIRSHIP);
    }
    return false;
}

} // namespace AirShipManagerHelper

CAirShipConst* CAirShipManager::_CanBuyFreeAirShip(User* const user, const AirShipTypeEnum type, const std::int32_t typeId) {
    // Current L2Server 0x00468abc, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!AirShipManagerHelper::CanBuy(user, type)) {
        return nullptr;
    }

    const auto constantPosition = m_AirShipConstMap.find(typeId);
    if (constantPosition == m_AirShipConstMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0x9f8,
        //     typeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0x9f8,
        //     typeId)
        L2_LOG_ERROR(
            Log, "Airship purchase rejected for character %u: template %d is not registered (requested type %d).",
            static_cast<unsigned>(user->GetDBID()), typeId, static_cast<int>(type)
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_BUY_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    CAirShipConst* const airShipConst = constantPosition->second;
    if (airShipConst == nullptr || airShipConst->m_Type != type || !airShipConst->IsForFree()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0xa02,
        //     typeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0xa02,
        //     typeId)
        L2_LOG_ERROR(
            Log,
            "Airship purchase rejected for character %u: template %d is missing, has a different type, or is not freely "
            "controlled (requested type %d).",
            static_cast<unsigned>(user->GetDBID()), typeId, static_cast<int>(type)
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_BUY_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    const std::uint32_t ownerDbId = user->m_data->m_nPledgeID;
    if (m_FreeAirShipDBMap.find(ownerDbId) != m_FreeAirShipDBMap.end()) {
        user->SendSystemMessage(SystemMessageId::CLAN_AIRSHIP_ALREADY_EXISTS);
        return nullptr;
    }

    CAirShipConstFree* const freeConst = airShipConst->CastForFree();
    return freeConst->CanPayBuyConst(user) ? airShipConst : nullptr;
}

bool CAirShipManager::BuyFreeAirShip(
    User* const user, const AirShipTypeEnum type, const std::int32_t typeId, const std::uint32_t requestId
) {
    // Current L2Server 0x00468d18, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);

    CAirShipConst* const airShipConst = _CanBuyFreeAirShip(user, type, typeId);
    if (airShipConst != nullptr) {
        CAirShipConstFree* const freeConst = airShipConst->CastForFree();
        db.RequestBuyAirShip(
            *user, static_cast<int>(type), typeId, user->m_data->m_nPledgeID, freeConst->GetMaxFuel(), requestId
        );
    }
    return airShipConst != nullptr;
}

CAirShipConst* CAirShipManager::_CanSummonFreeAirShip(
    User* const user,
    const AirShipTypeEnum type,
    const std::int32_t airportId,
    const std::int32_t platformId,
    SummonParams& parameters
) {
    // Current L2Server 0x0046f774, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (type != ASTE_PLEDGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airship type[%d]",
        //     kAirShipSourceWide,
        //     0xa37,
        //     static_cast<int>(type))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airship type[%d]",
        //     kAirShipSourceWide,
        //     0xa37,
        //     static_cast<int>(type))
        L2_LOG_ERROR(
            Log, "Airship summon rejected for character %u: unsupported type %d; clan airship type is required.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(type)
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    if (!CContainerDB::Inst().CanAllocAirShipContainer()) {
        user->SendSystemMessage(SystemMessageId::AIRSHIP_SUMMON_LIMIT_REACHED);
        return nullptr;
    }
    if (!AirShipManagerHelper::CanSummon(user)) {
        return nullptr;
    }

    CAirport* const airport = CAirShipManager::Inst().GetAirport(airportId);
    if (airport == nullptr || !airport->IsForFree()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airport[%d], platform[%d]",
        //     kAirShipSourceWide,
        //     0xa86,
        //     airportId,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airport[%d], platform[%d]",
        //     kAirShipSourceWide,
        //     0xa86,
        //     airportId,
        //     platformId)
        L2_LOG_ERROR(
            Log,
            "Airship summon rejected for character %u: airport %d is missing or does not support freely controlled ships "
            "(platform %d).",
            static_cast<unsigned>(user->GetDBID()), airportId, platformId
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    CAirportPlatform* const platform = airport->CastForFree()->GetPlatform(platformId);
    if (platform == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airport[%d], platform[%d]",
        //     kAirShipSourceWide,
        //     0xa86,
        //     airportId,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airport[%d], platform[%d]",
        //     kAirShipSourceWide,
        //     0xa86,
        //     airportId,
        //     platformId)
        L2_LOG_ERROR(
            Log, "Airship summon rejected for character %u: platform %d does not exist at airport %d.",
            static_cast<unsigned>(user->GetDBID()), platformId, airportId
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    if (platform->IsOccupied()) {
        user->SendSystemMessage(SystemMessageId::ANOTHER_AIRSHIP_ALREADY_SUMMONED_AT_DOCK);
        return nullptr;
    }

    const std::uint32_t ownerDbId = user->m_data->m_nPledgeID;
    const auto infoPosition = m_FreeAirShipDBMap.find(ownerDbId);
    if (infoPosition == m_FreeAirShipDBMap.end()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_WITHOUT_LICENSE_OR_CLAN_AIRSHIP);
        return nullptr;
    }
    const FreeAirShipInfo& info = infoPosition->second;
    if (info.m_Summoned) {
        user->SendSystemMessage(SystemMessageId::CLAN_AIRSHIP_IN_USE_BY_ANOTHER_MEMBER);
        return nullptr;
    }

    parameters.ownerDBID = infoPosition->first;
    parameters.airshipDBID = info.m_DBID;
    parameters.fuel = info.m_Fuel;

    const auto constantPosition = m_AirShipConstMap.find(info.m_TypeId);
    CAirShipConst* const airShipConst = constantPosition == m_AirShipConstMap.end() ? nullptr : constantPosition->second;
    if (airShipConst == nullptr || !airShipConst->IsForFree()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0xa74,
        //     info.m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid airship type(%d)",
        //     kAirShipSourceWide,
        //     0xa74,
        //     info.m_TypeId)
        L2_LOG_ERROR(
            Log, "Airship summon rejected for character %u, clan %u: template %d is missing or not freely controlled.",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(ownerDbId), info.m_TypeId
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_SUMMON_AIRSHIP_REQUIREMENTS_NOT_MET);
        return nullptr;
    }

    return airShipConst->CastForFree()->CanPaySummonConst(user) ? airShipConst : nullptr;
}

bool CAirShipManager::SummonFreeAirShip(
    User* const user, const AirShipTypeEnum type, const std::int32_t airportId, const std::int32_t platformId
) {
    // Current L2Server 0x0046fb14, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    SummonParams parameters;
    L2_GUARD(m_AirShipLock);

    CAirShipConst* const airShipConst = _CanSummonFreeAirShip(user, type, airportId, platformId, parameters);
    if (airShipConst == nullptr) {
        return false;
    }

    const std::int32_t fuel = parameters.fuel;
    CAirShip* const airShip =
        CAirShipManager::Inst()._CreateAirShip(airShipConst, parameters.airshipDBID, parameters.ownerDBID, parameters.fuel);
    if (airShip == nullptr || !airShip->EnterWorld(airportId, platformId)) {
        return false;
    }

    if (!airShipConst->CastForFree()->PaySummonCost(user)) {
        static_cast<void>(CAirShipManager::Inst().DestoryAirShip(airShip->m_nId, ASDR_NOCOST));
        return false;
    }

    OnAirShipSummoned(parameters.ownerDBID);
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f60, user, nullptr, std::u16string(airShip->GetOwnerName()), nullptr, nullptr, type, airShipConst->m_TypeId,
        airShip->GetDBID(), fuel, 0, 0, 0
    );
    return true;
}

void CAirShipManager::OnAirShipSummoned(const std::uint32_t ownerDbId) {
    // Current L2Server 0x00468e70, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);

    const auto position = m_FreeAirShipDBMap.find(ownerDbId);
    if (position == m_FreeAirShipDBMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship does not exist. owner[%d]",
        //     kAirShipSourceWide,
        //     0xb02,
        //     ownerDbId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airship does not exist. owner[%d]",
        //     kAirShipSourceWide,
        //     0xb02,
        //     ownerDbId)
        L2_LOG_ERROR(
            Log, "Cannot mark clan %u airship summoned: no ownership record found.", static_cast<unsigned>(ownerDbId)
        );
        return;
    }
    position->second.m_Summoned = true;
}

bool CAirShipManager::SailRegularAirShip() {
    // Current L2Server 0x0046e028.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);
    for (const auto& entry : m_AirShipConstMap) {
        CAirShipConst* const airShipConst = entry.second;
        if (airShipConst != nullptr && airShipConst->IsForRegular()) {
            CAirShip* const airShip = _CreateAirShip(airShipConst, 0, 0, 0);
            if (airShip != nullptr) {
                static_cast<void>(airShip->EnterWorld(0, 0));
            }
        }
    }
    return true;
}

bool CAirShipManager::_SavePledgeAirShipInfo(std::uint32_t ownerDbId, std::int32_t fuel) {
    L2_GUARD_FUNCTION();
    const auto position = m_FreeAirShipDBMap.find(ownerDbId);
    if (position == m_FreeAirShipDBMap.end()) {
        return false;
    }
    position->second.m_Fuel = fuel;
    return true;
}

bool CAirShipManager::SaveFreeAirShipInfo(AirShipTypeEnum type, std::uint32_t ownerDbId, std::int32_t fuel) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);
    if (type == ASTE_PLEDGE) {
        return _SavePledgeAirShipInfo(ownerDbId, fuel);
    }
    return false;
}

bool CAirShipManager::_OnDespawnFreeAirShip(std::uint32_t ownerDbId) {
    L2_GUARD_FUNCTION();
    const auto position = m_FreeAirShipDBMap.find(ownerDbId);
    if (position == m_FreeAirShipDBMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship[%d] does not exist",
        //     kAirShipSourceWide,
        //     0xc57,
        //     ownerDbId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airship[%d] does not exist",
        //     kAirShipSourceWide,
        //     0xc57,
        //     ownerDbId)
        L2_LOG_ERROR(
            Log, "Cannot mark clan %u airship despawned: no ownership record found.", static_cast<unsigned>(ownerDbId)
        );
        return false;
    }
    position->second.m_Summoned = false;
    return true;
}

void CAirShipManager::DestroyAllAirShip() {
    // Current L2Server 0x0046b0d0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);

    auto position = m_AirShipInstList.begin();
    while (position != m_AirShipInstList.end()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(*position);
        if (airShip != nullptr && airShip->m_const->m_Type != ASTE_REGULAR && airShip->CanBeDestroyed()) {
            static_cast<void>(_OnDespawnFreeAirShip(airShip->m_data->m_OwnerDBID));
            airShip->Delete(ASDR_BUILDER);
            position = m_AirShipInstList.erase(position);
        } else {
            ++position;
        }
    }
}

bool CAirShipManager::DestroyAllRegularShip() {
    // Current L2Server 0x00467ad8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);

    bool destroyed = m_AirShipInstList.empty();
    auto position = m_AirShipInstList.begin();
    while (position != m_AirShipInstList.end()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(*position);
        if (airShip != nullptr && airShip->m_const->m_Type == ASTE_REGULAR) {
            if (airShip->CanBeDestroyed()) {
                airShip->Delete(ASDR_BUILDER);
                position = m_AirShipInstList.erase(position);
                destroyed = true;
            } else {
                ++position;
            }
        }
        // The current body does not advance a stale or
        // non-regular entry here.
    }
    return destroyed;
}

bool CAirShipManager::DestoryAirShip(std::uint32_t airShipId, AirShipDespawnReason reason) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AirShipLock);

    const auto position = m_AirShipInstList.find(airShipId);
    if (position == m_AirShipInstList.end()) {
        return false;
    }

    CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(*position);
    if (airShip == nullptr || !airShip->CanBeDestroyed()) {
        return false;
    }

    if (airShip->m_const->m_Type != ASTE_REGULAR) {
        _OnDespawnFreeAirShip(airShip->m_data->m_OwnerDBID);
    }
    airShip->Delete(reason);
    m_AirShipInstList.erase(position);
    return true;
}

bool CAirShipManager::_OnBuyAirShip(
    User* user,
    std::uint32_t airShipId,
    std::int32_t airShipType,
    std::int32_t airShipTypeId,
    std::uint32_t ownerId,
    std::int32_t fuel
) {
    // Current L2Server 0x0046a9dc, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (airShipType != ASTE_PLEDGE) {
        return false;
    }

    if (m_FreeAirShipDBMap.find(ownerId) != m_FreeAirShipDBMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated airship dbid[%d], airship(%d, %d, %d, %d)",
        //     kAirShipSourceWide,
        //     0xb46,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId,
        //     fuel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated airship dbid[%d], airship(%d, %d, %d, %d)",
        //     kAirShipSourceWide,
        //     0xb46,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId,
        //     fuel)
        L2_LOG_ERROR(
            Log,
            "Airship purchase reply rejected: clan %u already has an ownership record (incoming DBID %u, template %d, fuel "
            "%d).",
            static_cast<unsigned>(ownerId), static_cast<unsigned>(airShipId), airShipTypeId, fuel
        );
        return false;
    }

    std::int32_t failureReason = 0;
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Cannot buy airship(%d) because buying user does not exist. type(%d) typeid(%d) owner(%d)",
        //     kAirShipSourceWide,
        //     0xb51,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Cannot buy airship(%d) because buying user does not exist. type(%d) typeid(%d) owner(%d)",
        //     kAirShipSourceWide,
        //     0xb51,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId)
        L2_LOG_ERROR(
            Log,
            "Airship purchase reply cannot complete: buyer is missing or outside world %d (ship DBID %u, template %d, clan "
            "%u); requesting DB removal with reason 1.",
            g_nWorldId, static_cast<unsigned>(airShipId), airShipTypeId, static_cast<unsigned>(ownerId)
        );
        failureReason = 1;
    } else {
        CPledge* const pledge = CPledge::FindPledge(g_nWorldId, ownerId).get();
        if (pledge == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] no pledge?[%d] on buying airship(%d, %d, %d)",
            //     kAirShipSourceWide,
            //     0xb5b,
            //     ownerId,
            //     airShipId,
            //     ASTE_PLEDGE,
            //     airShipTypeId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] no pledge?[%d] on buying airship(%d, %d, %d)",
            //     kAirShipSourceWide,
            //     0xb5b,
            //     ownerId,
            //     airShipId,
            //     ASTE_PLEDGE,
            //     airShipTypeId)
            L2_LOG_ERROR(
                Log,
                "Airship purchase reply cannot complete: clan %u not found (ship DBID %u, template %d); requesting DB "
                "removal with reason 4.",
                static_cast<unsigned>(ownerId), static_cast<unsigned>(airShipId), airShipTypeId
            );
            failureReason = 4;
        } else {
            const auto position = m_AirShipConstMap.find(airShipTypeId);
            CAirShipConst* const airShipConst = position == m_AirShipConstMap.end() ? nullptr : position->second;
            if (airShipConst == nullptr || !airShipConst->IsForFree()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid airship type(%d). airship(%d) typeid(%d) user(%s)",
                //     kAirShipSourceWide,
                //     0xb6b,
                //     ASTE_PLEDGE,
                //     airShipId,
                //     airShipTypeId,
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] invalid airship type(%d). airship(%d) typeid(%d) user(%s)",
                //     kAirShipSourceWide,
                //     0xb6b,
                //     ASTE_PLEDGE,
                //     airShipId,
                //     airShipTypeId,
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     "Airship purchase reply for character '%s': template %d is missing or not freely controlled (ship DBID
                //     %u, clan %u). Requesting DB removal with reason 2.",
                //     WideToUtf8(std::wstring_view(user->GetName())).c_str(), airShipTypeId,
                //     static_cast<unsigned>(airShipId), static_cast<unsigned>(ownerId))
                L2_LOG_ERROR(
                    Log,
                    "Airship purchase reply for character '%s': template %d is missing or not freely controlled (ship DBID "
                    "%u, clan %u); requesting DB removal with reason 2.",
                    Utf16ToUtf8(user->GetName()).c_str(), airShipTypeId, static_cast<unsigned>(airShipId),
                    static_cast<unsigned>(ownerId)
                );
                failureReason = 2;
            } else if (!airShipConst->CastForFree()->PayBuyCost(user)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] cannot pay cost on buying airship(%d). type(%d) typeid(%d) user(%s)",
                //     kAirShipSourceWide,
                //     0xb74,
                //     airShipId,
                //     ASTE_PLEDGE,
                //     airShipTypeId,
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] cannot pay cost on buying airship(%d). type(%d) typeid(%d) user(%s)",
                //     kAirShipSourceWide,
                //     0xb74,
                //     airShipId,
                //     ASTE_PLEDGE,
                //     airShipTypeId,
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     "Airship purchase payment failed for character '%s' (ship DBID %u, template %d, clan %u). Requesting
                //     DB removal with reason 3.", WideToUtf8(std::wstring_view(user->GetName())).c_str(),
                //     static_cast<unsigned>(airShipId), airShipTypeId, static_cast<unsigned>(ownerId))
                L2_LOG_ERROR(
                    Log,
                    "Airship purchase payment failed for character '%s' (ship DBID %u, template %d, clan %u); requesting DB "
                    "removal with reason 3.",
                    Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(airShipId), airShipTypeId,
                    static_cast<unsigned>(ownerId)
                );
                failureReason = 3;
            } else {
                // The target initializes the four fields and deliberately
                // leaves the three padding bytes indeterminate.
                FreeAirShipInfo info;
                info.m_DBID = airShipId;
                info.m_Summoned = false;
                info.m_Fuel = fuel;
                info.m_TypeId = airShipTypeId;
                m_FreeAirShipDBMap.emplace(ownerId, info);
                return true;
            }
        }
    }

    db.RequestRemoveAirShip(airShipId, failureReason);
    return false;
}

bool CAirShipManager::_OnLoadAirShip(
    std::uint32_t airShipId, std::int32_t airShipType, std::int32_t airShipTypeId, std::uint32_t ownerId, std::int32_t fuel
) {
    // Current L2Server 0x0046adb4, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (airShipType != ASTE_PLEDGE) {
        return false;
    }

    if (m_FreeAirShipDBMap.find(ownerId) != m_FreeAirShipDBMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated airship dbid[%d], airship(%d, %d, %d, %d)",
        //     kAirShipSourceWide,
        //     0xba0,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId,
        //     fuel)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Duplicated airship dbid[%d], airship(%d, %d, %d, %d)",
        //     kAirShipSourceWide,
        //     0xba0,
        //     airShipId,
        //     ASTE_PLEDGE,
        //     airShipTypeId,
        //     ownerId,
        //     fuel)
        L2_LOG_ERROR(
            Log, "Airship load rejected: clan %u already has an ownership record (incoming DBID %u, template %d, fuel %d).",
            static_cast<unsigned>(ownerId), static_cast<unsigned>(airShipId), airShipTypeId, fuel
        );
        return false;
    }

    // As in _OnBuyAirShip, only the named fields are initialized.
    FreeAirShipInfo info;
    info.m_DBID = airShipId;
    info.m_Summoned = false;
    info.m_Fuel = fuel;
    info.m_TypeId = airShipTypeId;
    m_FreeAirShipDBMap.emplace(ownerId, info);

    CPledge* const pledge = CPledge::FindPledge(g_nWorldId, ownerId).get();
    if (pledge != nullptr) {
        pledge->SharedData()->m_AirShipCount = 1;
    }
    return true;
}

void CAirShipManager::OnLoadFreeAirShipAll(std::int32_t count, const unsigned char* packet) {
    // Current L2Server 0x0046afa8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AirShipLock);
        for (std::int32_t index = 0; index < count; ++index) {
            std::uint32_t airShipId;
            std::int32_t airShipType;
            std::int32_t airShipTypeId;
            std::uint32_t ownerId;
            std::int32_t fuel;
            packet = ::Disassemble(packet, "ddddd", &airShipId, &airShipType, &airShipTypeId, &ownerId, &fuel);
            static_cast<void>(_OnLoadAirShip(airShipId, airShipType, airShipTypeId, ownerId, fuel));
        }
    }
}

void CAirShipManager::OnBuyFreeAirShip(
    std::uint32_t userId,
    std::uint32_t requestId,
    std::uint32_t airShipId,
    std::int32_t airShipType,
    std::int32_t airShipTypeId,
    std::uint32_t ownerId,
    std::int32_t fuel
) {
    // Current L2Server 0x0046bc7c, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;

    {
        L2_GUARD(m_AirShipLock);
        if (!_OnBuyAirShip(user, airShipId, airShipType, airShipTypeId, ownerId, fuel)) {
            if (requestId == 0 && user != nullptr) {
                char16_t emptyContext[] = u"";
                user->SendSystemMessage(emptyContext, u"Buying airship is failed.");
            }
        } else {
            CPledge* const pledge = CPledge::FindPledge(g_nWorldId, ownerId).get();
            if (pledge != nullptr) {
                ++pledge->SharedData()->m_AirShipCount;
            }
            if (user != nullptr) {
                user->SendSystemMessage(SystemMessageId::AIRSHIP_SUMMON_LICENSE_REGISTERED);
            }

            CLogFormatter formatter;
            formatter.WriteLog1(
                0x1f63, user, nullptr, pledge != nullptr ? pledge->GetPledgeName() : nullptr, nullptr, nullptr, airShipType,
                airShipTypeId, airShipId, 0, 0, 0, 0
            );
        }
    }
}

void CAirShipManager::Load() {
    L2_GUARD_FUNCTION();
    CSecureStream input(kAirShipDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from airship file failed",
        //     kAirShipSourceWide,
        //     0x960)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from airship file failed",
        //     kAirShipSourceWide,
        //     0x960)
        L2_LOG_ERROR(Log, "Cannot read airship.txt; airship data loading stops.");
        return;
    }

    CParserForAirShip parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AirshipParser.create failed",
        //     kAirShipSourceWide,
        //     0x969)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] AirshipParser.create failed",
        //     kAirShipSourceWide,
        //     0x969)
        L2_LOG_ERROR(Log, "Cannot initialize airship.txt parser; airship data loading stops.");
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing airship file failed after line[%d]",
        //     kAirShipSourceWide,
        //     0x976,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing airship file failed after line[%d]",
        //     kAirShipSourceWide,
        //     0x976,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse airship.txt at script line %lld; data registered before the error is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

DriverHelper::AirShipDriverType DriverHelper::CanDrive(CAirShip* airShip, User* user, bool forced) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || airShip->m_const->m_Type == ASTE_REGULAR) {
        return ASDT_FAILED;
    }
    if (airShip->IsDriver(user)) {
        return ASDT_RESET;
    }

    CAirShipSteering* const steering = airShip->m_Steering;
    if (steering == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship steering does not exist. "
        //     L"ship(%d) user(%s)",
        //     kAirShipSourceWide,
        //     0x5e2,
        //     airShip->m_const->m_TypeId,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airship steering does not exist. "
        //     L"ship(%d) user(%s)",
        //     kAirShipSourceWide,
        //     0x5e2,
        //     airShip->m_const->m_TypeId,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Character '%s' cannot take control of airship %u (template %d): steering object is missing.",
        //     WideToUtf8(std::wstring_view(user->GetName())).c_str(),
        //     static_cast<unsigned>(airShip->m_nId), airShip->m_const->m_TypeId)
        L2_LOG_ERROR(
            Log, "Character '%s' cannot take control of airship %u (template %d): steering object is missing.",
            Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned>(airShip->m_nId), airShip->m_const->m_TypeId
        );
        return ASDT_FAILED;
    }

    if (!forced) {
        if (user->m_spTarget.GetStaticObject() != steering) {
            user->SendSystemMessage(SystemMessageId::SELECT_TARGET_TO_CONTROL);
            return ASDT_FAILED;
        }
        const FVector userPosition = user->GetRelPos();
        const FVector steeringPosition = steering->GetRelPos();
        if (userPosition.DistCube(steeringPosition) >= 10000.0) {
            user->SendSystemMessage(SystemMessageId::CANNOT_CONTROL_TARGET_TOO_FAR);
            return ASDT_FAILED;
        }
    }

    if (airShip->GetDriver() != nullptr) {
        user->SendSystemMessage(SystemMessageId::TARGET_ALREADY_CONTROLLED_BY_ANOTHER_PLAYER);
    } else if (user->IsRiding()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_REQUIREMENTS_NOT_MET);
    } else if (user->m_data->m_bIsInCombatMode) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_DURING_COMBAT);
    } else if (user->IsInDuel()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_DURING_DUEL);
    } else if (CReplyChecker::IsUserWaitingReply(user)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_REQUIREMENTS_NOT_MET);
    } else if (user->IsRegisteredAtOlympiad()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_REQUIREMENTS_NOT_MET);
    } else if (user->m_PledgeGameUser.IsPlayer()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_REQUIREMENTS_NOT_MET);
    } else if (user->m_data->m_MoveMode != MT_FAST) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_SITTING);
    } else if (!user->IsAlive()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_DEAD);
    } else if (user->GetController()->GetCurrentActionType() == ACT_SKILL) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_CASTING);
    } else if (user->GetPrivateStoreType() != 0 || user->IsNowTrade()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_REQUIREMENTS_NOT_MET);
    } else if (user->IsTransformed(TransformType::Max) != nullptr) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_TRANSFORMED);
    } else if (user->IsFrozen(true) || user->m_SkillMod.m_nBlockMoveCount != 0 || !user->m_data->m_bCanMove ||
        !user->m_SkillMod.m_bCanAct) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CONTROL_TARGET_WHILE_ACTIONS_DISABLED);
    } else if (user->HaveSummon() || user->HavePet()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_CONTROL_TARGET_WITH_PET_OR_SERVITOR);
    } else if (user->m_pFishingController != nullptr &&
        (user->m_pFishingController->m_FishingState != FISHINGS_NONE || user->m_pFishingController->m_bIsAutoFishing)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_FISHING);
    } else if (user->IsEquippedWithForcedItem(ST_RLHAND)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_STEER_WHILE_HOLDING_FLAG);
    } else {
        static_cast<void>(CCursedWeaponMgr::inst());
        if (user->m_nCursedWeaponClassId < 1) {
            return ASDT_SET;
        }
    }
    return ASDT_FAILED;
}

bool BuilderCmdHelper::CanUseBuilderCmd(User* const user, const int level) {
    // Current L2Server 0x004619b0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    return user->IsBuilder() && user->m_data->m_nBuilder <= level;
}

void BuilderCmdHelper::SummonAirShip(User* const user, char16_t* command) {
    // Current L2Server 0x00470090, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    char16_t* airportToken = nullptr;
    command = ::GetLeftString(command, &airportToken);
    if (airportToken != nullptr) {
        const int airportId = WindowsWtol(std::u16string_view(airportToken));
        char16_t* platformToken = nullptr;
        command = ::GetLeftString(command, &platformToken);
        if (platformToken != nullptr) {
            const int platformId = WindowsWtol(std::u16string_view(platformToken));
            static_cast<void>(CAirShipManager::Inst().SummonFreeAirShip(user, ASTE_PLEDGE, airportId, platformId));
            return;
        }
    }
    user->SendSystemMessage(u"SYS", u"//aship summon airport_id platform_id");
}

void BuilderCmdHelper::BuyFreeAirShip(User* const user, char16_t* command) {
    // Current L2Server 0x004701e8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    char16_t* typeToken = nullptr;
    command = ::GetLeftString(command, &typeToken);
    if (typeToken == nullptr) {
        user->SendSystemMessage(u"SYS", u"//aship buy type_id");
        return;
    }

    const int typeId = WindowsWtol(std::u16string_view(typeToken));
    static_cast<void>(CAirShipManager::Inst().BuyFreeAirShip(user, ASTE_PLEDGE, typeId, 0));
}

void BuilderCmdHelper::DestroyRegularShip(User* const user, char16_t*) {
    // Current L2Server 0x00470320, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (CanUseBuilderCmd(user, 2)) {
        static_cast<void>(CAirShipManager::Inst().DestroyAllRegularShip());
    }
}

void BuilderCmdHelper::SailRegularShip(User* const user, char16_t*) {
    // Current L2Server 0x00470404, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 2)) {
        return;
    }
    static_cast<void>(CAirShipManager::Inst().DestroyAllRegularShip());
    (new CRegularAirShipStartMessage)->Post(0);
}

void BuilderCmdHelper::GetOnAirShip(User* const user, char16_t* command) {
    // Current L2Server 0x00470510, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();

    CAirShip* airShip = nullptr;
    char16_t* idToken = nullptr;
    command = ::GetLeftString(command, &idToken);
    if (idToken == nullptr) {
        airShip = user->m_spTarget.GetAirShip();
    } else {
        const int id = WindowsWtol(std::u16string_view(idToken));
        CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(id));
        if (candidate != nullptr && candidate->IsAirShip()) {
            airShip = candidate->CastAirShip();
        }
    }

    if (airShip != nullptr) {
        CAirShipManager::Inst().GetOnAirShip(user, airShip);
    }
}

void BuilderCmdHelper::GetOffAirShip(User* const user, char16_t*) {
    // Current L2Server 0x00471aa8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    CAirShip* const airShip = user->GetMyAirShip();
    if (airShip != nullptr) {
        static_cast<void>(airShip->GetOff(user, true));
    }
}

void BuilderCmdHelper::SetDriver(User* const user, char16_t*) {
    // Current L2Server 0x00470654, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }
    CAirShip* const airShip = user->GetMyAirShip();
    if (airShip != nullptr) {
        static_cast<void>(airShip->SetDriver(user, true));
    }
}

void BuilderCmdHelper::Destroy(User* const user, char16_t* command) {
    // Current L2Server 0x0047071c, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    std::array<char16_t, 1024> message{};
    std::uint32_t airShipId = 0;
    if (!user->IsInAirShip()) {
        CAirShip* airShip = nullptr;
        char16_t* idToken = nullptr;
        command = ::GetLeftString(command, &idToken);
        if (idToken == nullptr) {
            airShip = user->m_spTarget.GetAirShip();
        } else {
            const int id = WindowsWtol(std::u16string_view(idToken));
            CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(id));
            if (candidate != nullptr && candidate->IsAirShip()) {
                airShip = candidate->CastAirShip();
            }
        }
        if (airShip == nullptr) {
            return;
        }

        airShipId = airShip->m_nId;
        const bool destroyed = CAirShipManager::Inst().DestoryAirShip(airShipId, ASDR_BUILDER);
        FormatUtf16To(
            message.data(), message.size(), destroyed ? u"destroy airship[%d]" : u"cannot destroy the airship[%d]",
            runtime::Int32FromBits(airShipId)
        );
    } else {
        airShipId = user->GetMyAirShipID();
        FormatUtf16To(
            message.data(), message.size(), u"cannot destroy the airship because u r in airship [%d]",
            runtime::Int32FromBits(airShipId)
        );
    }
    user->SendSystemMessage(u"SYS", message.data());
}

void BuilderCmdHelper::DestroyAll(User* const user, char16_t*) {
    // Current L2Server 0x00470930, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 2)) {
        return;
    }

    std::array<char16_t, 1024> message{};
    if (!user->IsInAirShip()) {
        CAirShipManager::Inst().DestroyAllAirShip();
        FormatUtf16To(message.data(), message.size(), u"destroy all airship");
    } else {
        const std::uint32_t airShipId = user->GetMyAirShipID();
        FormatUtf16To(
            message.data(), message.size(), u"cannot destroy all airship because u r in airship [%d]",
            runtime::Int32FromBits(airShipId)
        );
    }
    user->SendSystemMessage(u"SYS", message.data());
}

void BuilderCmdHelper::Ascent(User* const user, char16_t* command) {
    // Current L2Server 0x004661a0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    char16_t* idToken = nullptr;
    command = ::GetLeftString(command, &idToken);
    const std::uint32_t airShipId =
        idToken == nullptr ? user->GetMyAirShipID() : static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(idToken)));
    CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (candidate != nullptr && candidate->IsAirShip()) {
        CAirShip* const airShip = candidate->CastAirShip();
        if (airShip->IsDriver(user)) {
            airShip->Ascent();
        }
    }
}

void BuilderCmdHelper::Descent(User* const user, char16_t* command) {
    // Current L2Server 0x004662c8, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    char16_t* idToken = nullptr;
    command = ::GetLeftString(command, &idToken);
    const std::uint32_t airShipId =
        idToken == nullptr ? user->GetMyAirShipID() : static_cast<std::uint32_t>(WindowsWtol(std::u16string_view(idToken)));
    CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(airShipId);
    if (candidate != nullptr && candidate->IsAirShip()) {
        CAirShip* const airShip = candidate->CastAirShip();
        if (airShip->IsDriver(user)) {
            airShip->Descent();
        }
    }
}

void BuilderCmdHelper::Teleport(User* const user, char16_t* command) {
    // Current L2Server 0x004663f0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    char16_t* xToken = nullptr;
    command = ::GetLeftString(command, &xToken);
    if (xToken != nullptr) {
        const int x = WindowsWtol(std::u16string_view(xToken));
        char16_t* yToken = nullptr;
        command = ::GetLeftString(command, &yToken);
        if (yToken != nullptr) {
            const int y = WindowsWtol(std::u16string_view(yToken));
            char16_t* zToken = nullptr;
            command = ::GetLeftString(command, &zToken);
            if (zToken != nullptr) {
                const int z = WindowsWtol(std::u16string_view(zToken));
                CAirShip* const airShip = user->GetMyAirShip();
                if (airShip == nullptr) {
                    return;
                }
                if (!airShip->IsDriver(user)) {
                    user->SendSystemMessage(u"SYS", u"you are not driver.");
                    return;
                }

                FAngle direction = airShip->GetDir();
                direction.Yaw = static_cast<std::int32_t>(static_cast<std::uint32_t>(direction.Yaw) & 0xffffU);
                airShip->TeleportToLocation(x, y, z, 0, direction.Yaw);
                return;
            }
        }
    }
    user->SendSystemMessage(u"SYS", u"//aship tel x y z");
}

void BuilderCmdHelper::TakeOffAirShip(User* const user, char16_t* command) {
    // Current L2Server 0x00461a6c, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    int airportId = -1;
    char16_t* airportToken = nullptr;
    command = ::GetLeftString(command, &airportToken);
    if (airportToken != nullptr) {
        airportId = WindowsWtol(std::u16string_view(airportToken));
    }
    CAirShip* const airShip = user->GetMyAirShip();
    if (airShip != nullptr) {
        airShip->TakeOff(airportId, user);
    }
}

void BuilderCmdHelper::AirShipInfo(User* const user, char16_t* command) {
    // Current L2Server 0x00470a98, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();

    CAirShip* airShip = nullptr;
    char16_t* idToken = nullptr;
    command = ::GetLeftString(command, &idToken);
    if (idToken == nullptr) {
        airShip = user->IsAirShipDriver() ? user->GetMyAirShip() : user->m_spTarget.GetAirShip();
    } else {
        const int id = WindowsWtol(std::u16string_view(idToken));
        CAirShip* const candidate = g_AirShipSmartIdManager.GetObjectW(static_cast<std::uint32_t>(id));
        if (candidate != nullptr && candidate->IsAirShip()) {
            airShip = candidate->CastAirShip();
        }
    }

    if (airShip != nullptr) {
        CAirShipManager::Inst().DebugAirShipInfo(user, airShip);
    }
}

void BuilderCmdHelper::LandingInAirport(User* const user, char16_t* command) {
    // Current L2Server 0x0047215c, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanUseBuilderCmd(user, 3)) {
        return;
    }

    CAirShip* const airShip = user->IsAirShipDriver() ? user->GetMyAirShip() : user->m_spTarget.GetAirShip();
    if (airShip == nullptr || airShip->m_const->m_Type == ASTE_REGULAR) {
        return;
    }

    char16_t* airportToken = nullptr;
    command = ::GetLeftString(command, &airportToken);
    if (airportToken == nullptr) {
        user->SendSystemMessage(u"SYS", u"//aship landing airpot_id");
        return;
    }
    const int airportId = WindowsWtol(std::u16string_view(airportToken));
    airShip->Landing(airportId);
}

bool CAirShipHandler::BuilderCmdHandler(CUserSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00472288, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        return true;
    }

    char16_t* action = nullptr;
    command = ::GetLeftString(command, &action);
    if (action == nullptr) {
        user->GetSocket()->SendSystemMessage(u"SYS", u"//aship command");
        return false;
    }

    if (CompareAirShipBuilderToken(action, u"summon") == 0) {
        BuilderCmdHelper::SummonAirShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"buy") == 0) {
        BuilderCmdHelper::BuyFreeAirShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"geton") == 0) {
        BuilderCmdHelper::GetOnAirShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"getoff") == 0) {
        BuilderCmdHelper::GetOffAirShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"driver") == 0) {
        BuilderCmdHelper::SetDriver(user, command);
    } else if (CompareAirShipBuilderToken(action, u"destroy") == 0) {
        BuilderCmdHelper::Destroy(user, command);
    } else if (CompareAirShipBuilderToken(action, u"destroy_all") == 0) {
        BuilderCmdHelper::DestroyAll(user, command);
    } else if (CompareAirShipBuilderToken(action, u"up") == 0) {
        BuilderCmdHelper::Ascent(user, command);
    } else if (CompareAirShipBuilderToken(action, u"down") == 0) {
        BuilderCmdHelper::Descent(user, command);
    } else if (CompareAirShipBuilderToken(action, u"tel") == 0) {
        BuilderCmdHelper::Teleport(user, command);
    } else if (CompareAirShipBuilderToken(action, u"regular") == 0) {
        BuilderCmdHelper::SailRegularShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"rdestroy") == 0) {
        BuilderCmdHelper::DestroyRegularShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"start") == 0) {
        BuilderCmdHelper::TakeOffAirShip(user, command);
    } else if (CompareAirShipBuilderToken(action, u"info") == 0) {
        BuilderCmdHelper::AirShipInfo(user, command);
    } else if (CompareAirShipBuilderToken(action, u"landing") == 0) {
        BuilderCmdHelper::LandingInAirport(user, command);
    }
    return false;
}

bool CAirShipHandler::UserCmdHandler(User* user, UserAirShipActionType action) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    CAirShip* const airShip = user->GetMyAirShip();
    if (airShip == nullptr) {
        return false;
    }

    switch (action) {
        case UASAT_CONTROL_WHEEL:
            if (!airShip->IsDriver(user)) {
                airShip->SetDriver(user, false);
            }
            return true;
        case UASAT_LEAVE_WHEEL:
            if (airShip->IsDriver(user)) {
                airShip->SetDriver(user, false);
            }
            return true;
        case UASAT_START_SHIP:
            airShip->SendTeleportList(user);
            return true;
        case UASAT_LEAVE_SHIP:
            airShip->GetOff(user, false);
            return true;
        case UASAT_MAX:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid type(%d) User(%s)",
            //     kAirShipSourceWide,
            //     0xf1b,
            //     static_cast<int>(action),
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid type(%d) User(%s)",
            //     kAirShipSourceWide,
            //     0xf1b,
            //     static_cast<int>(action),
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     "Unsupported airship action %d from character '%s'. Request rejected.",
            //     static_cast<int>(action), WideToUtf8(std::wstring_view(user->GetName())).c_str())
            L2_LOG_ERROR(
                Log, "Unsupported airship action %d from character '%s'; request rejected.", static_cast<int>(action),
                Utf16ToUtf8(user->GetName()).c_str()
            );
            return false;
        default:
            return true;
    }
}

bool CAirShipHandler::DBPacketHandler(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00470bf0, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    std::int32_t jobType;
    const unsigned char* cursor = ::Disassemble(packet, "d", &jobType);
    if (jobType == 0) {
        std::uint32_t userId;
        std::uint32_t requestId;
        std::uint32_t airShipId;
        cursor = ::Disassemble(cursor, "ddd", &userId, &requestId, &airShipId);
        if (airShipId == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot buy airship.",
            //     kAirShipSourceWide,
            //     0xf3d)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Cannot buy airship.",
            //     kAirShipSourceWide,
            //     0xf3d)
            L2_LOG_ERROR(
                Log,
                "Airship purchase DB reply returned ship ID zero for player object %u, request %u; purchase completion is "
                "skipped.",
                static_cast<unsigned>(userId), static_cast<unsigned>(requestId)
            );
        } else {
            std::int32_t airShipType;
            std::int32_t airShipTypeId;
            std::uint32_t ownerId;
            std::int32_t fuel;
            ::Disassemble(cursor, "dddd", &airShipType, &airShipTypeId, &ownerId, &fuel);
            CAirShipManager::Inst().OnBuyFreeAirShip(
                userId, requestId, airShipId, airShipType, airShipTypeId, ownerId, fuel
            );
        }
    } else if (jobType == 2) {
        std::int32_t count;
        cursor = ::Disassemble(cursor, "d", &count);
        if (count > 0) {
            CAirShipManager::Inst().OnLoadFreeAirShipAll(count, cursor);
        }
    }
    return false;
}

L2_DEFINE_OBJECT_CLASS_ID(CAirShip)

L2_DEFINE_OBJECT_CLASS_NAME(CAirShip, m_pClassName_CAirShip)

CAirShip::CAirShip(CAirShipConst* airShipConst) :
    m_lock(nullptr),
    m_data(nullptr),
    m_statusType(ACTION_FREEZE),
    padding_a4{},
    m_nLastTick(0),
    m_nMoveTick(0),
    m_bMoving(false),
    padding_101{},
    m_pController(nullptr),
    m_pContainer(nullptr),
    m_const(airShipConst),
    m_Steering(nullptr),
    m_AutoInfo{ASIT_NONE, -1, -1, {}, nullptr, nullptr, -1, {}},
    m_Speed(0),
    m_SpeedAreaFactor(0),
    m_TickCount(0),
    m_PassengerCount(0) {}

CAirShip::CAirShip(const CAirShip& other) :
    CAbstractCarrier(other),
    m_lock(nullptr),
    m_data(nullptr),
    m_statusType(ACTION_FREEZE),
    padding_a4{},
    m_nLastTick(0),
    m_nMoveTick(0),
    m_bMoving(false),
    padding_101{},
    m_pController(nullptr),
    m_pContainer(nullptr),
    m_const(other.m_const),
    m_Steering(nullptr),
    m_AutoInfo{ASIT_NONE, -1, -1, {}, nullptr, nullptr, -1, {}},
    m_Speed(0),
    m_SpeedAreaFactor(0),
    m_TickCount(0),
    m_PassengerCount(0) {
    L2_GUARD_FUNCTION();
}

CAirShip::~CAirShip() {
    L2_GUARD_FUNCTION();
    CContainerDB::Inst().FreeAirShipContainer(m_pContainer);
    CAirShipFactory::Inst().Remove(this);
    g_ServerContext.AirShip.fetch_sub(1, std::memory_order_seq_cst);
}

L2_DEFINE_OBJECT_TYPED_RELEASE(CAirShip)

L2_DEFINE_OBJECT_CLASS_ID(CAirShipForFree)

L2_DEFINE_OBJECT_CLASS_NAME(CAirShipForFree, m_pClassName_CAirShipForFree)

L2_DEFINE_OBJECT_ALLOCATOR(CAirShipForFree, __GetClassName())

L2_DEFINE_OBJECT_NEW(CAirShipForFree)

CAirShipForFree::CAirShipForFree(CAirShipConst* airShipConst) :
    CAirShip(airShipConst), m_Fuel(0), m_FuelTick(0), m_EmptyTick(0), m_SomethingChanged(0) {}

CAirShipForFree::CAirShipForFree(const CAirShipForFree& other) :
    CAirShip(other), m_Fuel(0), m_FuelTick(0), m_EmptyTick(0), m_SomethingChanged(0) {}

L2_DEFINE_OBJECT_TYPED_RELEASE(CAirShipForFree)

CObject* CAirShipForFree::Copy() { return new CAirShipForFree(*this); }

int CAirShipForFree::GetFuel() { return m_Fuel; }

L2_DEFINE_OBJECT_CLASS_ID(CAirShipForRegular)

L2_DEFINE_OBJECT_CLASS_NAME(CAirShipForRegular, m_pClassName_CAirShipForRegular)

L2_DEFINE_OBJECT_ALLOCATOR(CAirShipForRegular, __GetClassName())

L2_DEFINE_OBJECT_NEW(CAirShipForRegular)

CAirShipForRegular::CAirShipForRegular(CAirShipConst* airShipConst) : CAirShip(airShipConst), m_StopDebugInfo{} {}

CAirShipForRegular::CAirShipForRegular(const CAirShipForRegular& other) : CAirShip(other), m_StopDebugInfo{} {}

L2_DEFINE_OBJECT_TYPED_RELEASE(CAirShipForRegular)

CObject* CAirShipForRegular::Copy() { return new CAirShipForRegular(*this); }

bool CAirShip::IsAirShip() { return true; }

CAirShip* CAirShip::CastAirShip() { return this; }

bool CAirShip::CanAction() { return true; }

void CAirShip::SendDebugInfo(User*) {}

const char16_t* CAirShip::GetOwnerName() { return nullptr; }

bool CAirShip::CanRefuel(int) { return false; }

bool CAirShip::Refuel(int, User*) { return false; }

void CAirShip::TakeOff(int, User*) {}

void CAirShip::SendTeleportList(User*) {}

bool CAirShip::IsLanded() { return false; }

bool CAirShip::CanBeDestroyed() { return true; }

void CAirShip::OnLeaveWorld() {}

int CAirShip::GetFuel() { return 0; }

unsigned int CAirShip::GetDBID() const { return m_data->m_DBID; }

void CAirShip::SetDBID(unsigned int dbId) { m_data->m_DBID = dbId; }

bool CAirShip::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    return creature != nullptr && m_nId != creature->GetMyAirShipID();
}

void CAirShip::OnCreate(unsigned int dbId, unsigned int ownerDbId, int) {
    L2_GUARD_FUNCTION();
    CAirShipFactory& factory = CAirShipFactory::Inst();
    m_data = factory.Add(this);
    m_nId = g_AirShipSmartIdManager.RegisterObject(
        // 7,
        SCLS_AIR_SHIP, static_cast<unsigned int>(m_data->index), this
    );
    m_pContainer = CContainerDB::Inst().AllocAirShipContainer(static_cast<int>(m_nId));
    factory.PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = m_nId;

    int thread = 0;
    int threadCount = CThreadManager::GetThreadCount();
    while (thread < threadCount) {
        L2_ADD_REF(this, ObjectRefType::ThreadLocalMap);
        ++thread;
        threadCount = CThreadManager::GetThreadCount();
    }
    IncreaseTLRef();

    m_pController = new CShipController(this);
    m_data->m_bIsInWorld = false;
    m_bMoving = false;
    m_nLastTick = runtime::GetTickCount();
    m_AirShipParts.clear();
    m_AirShipCannons.clear();
    m_statusType = ACTION_OK;
    m_data->m_DriverId = 0;
    SetDBID(dbId);
    m_data->m_OwnerDBID = ownerDbId;
    m_Speed = static_cast<int>(m_const->m_Speed[0]);
    m_SpeedAreaFactor = 100;
    g_ServerContext.AirShip.fetch_add(1, std::memory_order_seq_cst);
}

void CAirShipForFree::OnCreate(unsigned int dbId, unsigned int ownerDbId, int fuel) {
    L2_GUARD_FUNCTION();
    CAirShip::OnCreate(dbId, ownerDbId, fuel);
    m_Fuel = fuel;
    m_FuelTick = 0;
    m_EmptyTick = 0;
    m_Steering = new CAirShipSteering(this);
    m_Steering->m_data->m_bIsActive = true;
}

bool CAirShipForFree::EnterWorld(int airportId, int platformId) {
    L2_GUARD_FUNCTION();
    CAirport* const airport = CAirShipManager::Inst().GetAirport(airportId);
    if (airport == nullptr || !airport->IsForFree()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airport does not exist. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x106d,
        //     airportId,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airport does not exist. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x106d,
        //     airportId,
        //     platformId)
        L2_LOG_ERROR(
            Log,
            "Airship %u cannot enter the world at airport %d, platform %d: airport missing or not for freely controlled "
            "ships.",
            static_cast<unsigned>(m_nId), airportId, platformId
        );
        return false;
    }

    CAirportForFree* const freeAirport = airport->CastForFree();
    if (freeAirport == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airport is not free type. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x1067,
        //     airportId,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airport is not free type. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x1067,
        //     airportId,
        //     platformId)
        L2_LOG_ERROR(
            Log, "Airship %u cannot enter the world at airport %d, platform %d: airport specialization is unavailable.",
            static_cast<unsigned>(m_nId), airportId, platformId
        );
        return false;
    }

    CAirportPlatform* const platform = freeAirport->GetPlatform(platformId);
    if (platform == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] platform does not exist. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x1061,
        //     airportId,
        //     platformId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] platform does not exist. (%d, %d)",
        //     kAirShipSourceWide,
        //     0x1061,
        //     airportId,
        //     platformId)
        L2_LOG_ERROR(
            Log, "Airship %u cannot enter the world: platform %d does not exist at airport %d.",
            static_cast<unsigned>(m_nId), platformId, airportId
        );
        return false;
    }

    SetInZoneID(0);
    const FVector position = platform->GetLandingPath(0);
    m_AutoInfo.type = ASIT_LANDING;
    m_AutoInfo.nodeIndex = 0;
    m_AutoInfo.pPlatform = platform;
    m_AutoInfo.pAirport = freeAirport;
    m_AutoInfo.stopoverTick = -1;
    _EnterWorld(position);
    OnSummon(airport, platform);
    return true;
}

void CAirShip::_EnterWorld(const FVector& position) {
    L2_GUARD_FUNCTION();
    CWorld::Inst().EnterWorld(this, position);
}

void CAirShip::LeaveWorld() {
    L2_GUARD_FUNCTION();
    OnLeaveWorld();
    CWorld::Inst().LeaveWorld(this);
}

void CAirShip::Delete(AirShipDespawnReason reason) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::IncrementI32(&m_nDeleteCount) == 1) {
        SetDeleting(true);
        OnDelete(reason);
        static_cast<void>(new CAirShipReleaseVerifier(this));
        SetDelete();
        L2_RELEASE(this, ObjectRefType::CreateDelete);
        g_ptlAirShipMap->Remove(m_data->index, this);
        g_AirShipSmartIdManager.DeleteObject(m_nId);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid CAirShip::Delete",
    //     kAirShipSourceWide,
    //     0x318)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid CAirShip::Delete",
    //     kAirShipSourceWide,
    //     0x318)
    L2_LOG_ERROR(
        Log, "Duplicate deletion of airship %u (reason %d); this call skips further deletion.", static_cast<unsigned>(m_nId),
        static_cast<int>(reason)
    );
    runtime::process::ReportCallStack(2, false);
}

void CAirShip::OnEnterWorld() {
    L2_GUARD_FUNCTION();
    AddTimer(500, -1);
}

void CAirShipForFree::OnEnterWorld() {
    L2_GUARD_FUNCTION();
    if (m_Steering != nullptr) {
        m_Steering->SetInZoneID(GetInZoneID());
        m_Steering->SetPos(GetPos());
        m_Steering->SetRelPos(m_const->CastForFree()->GetSteeringPos());
        CWorld::Inst().LoadStaticObject(m_Steering, this);
    }
    CAirShip::OnEnterWorld();
}

void CAirShipForFree::OnLeaveWorld() {
    L2_GUARD_FUNCTION();
    if (m_Steering != nullptr) {
        CWorld::Inst().UnLoadStaticObject(m_Steering, this);
    }
}

void CAirShip::OnTimerExpired() {
    L2_GUARD_FUNCTION();
    if (m_AutoInfo.type != ASIT_NONE) {
        AutoSail();
    }
}

void CAirShip::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0 || !m_data->m_bIsActive) {
        return;
    }

    if (id == -2) {
        OnEnterWorld();
    } else if (id == -1) {
        OnTimerExpired();
        AddTimer(500, -1);
    } else {
        ProcessLanding(id);
    }
}

void CAirShip::UnloadAllParts() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        for (const CSmartID partId : m_AirShipParts) {
            CStaticObject* const object = partId.GetStaticObject();
            if (object == nullptr || !object->IsAirShipPart()) {
                continue;
            }
            CAirShipPart* const part = object->CastAirShipPart();
            if (part != nullptr) {
                part->LeaveWorld(this);
                part->Delete();
            }
        }
        m_AirShipParts.clear();
    }
}

void CAirShip::UnloadAllCannons() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        for (const auto& entry : m_AirShipCannons) {
            CNPC* const cannon = entry.second.GetNPC();
            if (cannon != nullptr) {
                cannon->Delete();
            }
        }
        m_AirShipCannons.clear();
    }
}

void CAirShip::OnDelete(AirShipDespawnReason) {
    L2_GUARD_FUNCTION();
    UnloadAllParts();
    UnloadAllCannons();
    LeaveWorld();
}

void CAirShipForFree::OnDelete(AirShipDespawnReason reason) {
    L2_GUARD_FUNCTION();
    if (m_Steering != nullptr) {
        m_Steering->Delete();
    }

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f61, GetDriver(), nullptr, std::u16string(GetOwnerName()), nullptr, nullptr, m_const->m_Type, m_const->m_TypeId,
        GetDBID(), GetFuel(), reason, 0, 0
    );
    CAirShip::OnDelete(reason);
}

void CAirShip::ProcessLanding(int airportId) {
    L2_GUARD_FUNCTION();
    CAirport* const airport = CAirShipManager::Inst().GetAirport(airportId);
    ProcessLanding(airport->GetGetOffInfo(-1).m_Pos);
}

void CAirShip::Landing(int airportId) {
    L2_GUARD_FUNCTION();
    StopMove(true);
    CAirport* const airport = CAirShipManager::Inst().GetAirport(airportId);
    if (airport == nullptr) {
        return;
    }

    const CAirport::GetOffInfo& getOffInfo = airport->GetGetOffInfo(-1);
    if (getOffInfo.m_SceneId < 1) {
        ProcessLanding(getOffInfo.m_Pos);
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        bool CanDo(CObject* object) override { return object->IsPC(); }
    } filter;

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(int sceneId) : m_sceneId(sceneId) {}

        void Do(CObject* object) override {
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            object->CastUser()->StartScenePlay(m_sceneId, -1, -1);
        }

    private:
        int m_sceneId;
    } function(getOffInfo.m_SceneId);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
    AddTimer(0x5dc, airportId);
}

void CAirShip::ProcessLanding(FVector position) {
    L2_GUARD_FUNCTION();
    User* const driver = GetDriver();
    if (driver != nullptr) {
        SetDriver(driver, false);
    }
    KickoutAllPassengers(position);
    CAirShipManager::Inst().DestoryAirShip(m_nId, ASDR_LANDING);
}

void CAirShip::OnLanding(int airportId, int platformId) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send("cdddd", 0x91, 0, airportId, platformId, static_cast<int>(m_nId));
}

void CAirShip::OnTakeOff(CAirport* airport, CAirportPlatform* platform) {
    L2_GUARD_FUNCTION();
    int platformId = 0;
    if (platform != nullptr) {
        platform->SetOccupied(false);
        platformId = platform->Id();
    }
    g_npcserver.Send("cdddd", 0x91, 1, airport->m_Id, platformId, static_cast<int>(m_nId));
}

void CAirShip::OnSummon(CAirport* airport, CAirportPlatform* platform) {
    L2_GUARD_FUNCTION();
    platform->SetOccupied(true);
    g_npcserver.Send("cdddd", 0x91, 2, airport->m_Id, platform->Id(), static_cast<int>(m_nId));
}

bool CAirShipForFree::CanAction() {
    L2_GUARD_FUNCTION();
    return m_AutoInfo.type == ASIT_NONE;
}

const char16_t* CAirShipForFree::GetOwnerName() {
    L2_GUARD_FUNCTION();
    if (m_const->m_Type != ASTE_PLEDGE) {
        return nullptr;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_data->m_OwnerDBID));
    return pledge.get() != nullptr ? pledge->GetPledgeName() : nullptr;
}

bool CAirShipForFree::CanRefuel(int) {
    L2_GUARD_FUNCTION();
    return m_Fuel < m_const->CastForFree()->GetMaxFuel();
}

void CAirShipForFree::SaveInfo() {
    L2_GUARD_FUNCTION();
    CAirShipManager::Inst().SaveFreeAirShipInfo(m_const->m_Type, m_data->m_OwnerDBID, m_Fuel);
    db.RequestSaveAirShip(m_data->m_DBID, m_Fuel);
}

void CAirShipForFree::ConsumeFuel(int fuel) {
    L2_GUARD_FUNCTION();
    m_Fuel = runtime::SubtractInt32Wrapping(m_Fuel, fuel);
    if (m_Fuel < 0) {
        m_Fuel = 0;
    }
    runtime::raw_atomic::StoreI32(&m_SomethingChanged, 1);
    SaveInfo();
}

bool CAirShipForFree::Refuel(int fuel, User* user) {
    L2_GUARD_FUNCTION();
    m_Fuel = runtime::AddInt32Wrapping(m_Fuel, fuel);
    const int maximum = m_const->CastForFree()->GetMaxFuel();
    if (maximum < m_Fuel) {
        m_Fuel = maximum;
    }
    runtime::raw_atomic::StoreI32(&m_SomethingChanged, 1);
    SaveInfo();

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f64, user, nullptr, std::u16string(GetOwnerName()), nullptr, nullptr, m_const->m_Type, m_const->m_TypeId,
        GetDBID(), fuel, 0, 0, 0
    );
    return true;
}

void CAirShipForFree::TakeOff(int teleportIndex, User* user) {
    L2_GUARD_FUNCTION();
    if (!IsLanded() || (user != nullptr && !IsDriver(user))) {
        return;
    }

    CAirportTeleport* const teleport = m_AutoInfo.pAirport->GetTeleport(teleportIndex);
    if (teleport == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid teleport index on airport[%d]",
        //     kAirShipSourceWide,
        //     0x11d2,
        //     teleportIndex,
        //     m_AutoInfo.pAirport->m_Id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid teleport index on airport[%d]",
        //     kAirShipSourceWide,
        //     0x11d2,
        //     teleportIndex,
        //     m_AutoInfo.pAirport->m_Id)
        L2_LOG_ERROR(
            Log, "Airship %u takeoff canceled: teleport entry %d is missing at airport %d.", static_cast<unsigned>(m_nId),
            teleportIndex, m_AutoInfo.pAirport->m_Id
        );
        return;
    }
    if (m_Fuel < teleport->m_FuelConsume) {
        if (user != nullptr) {
            user->SendSystemMessage(SystemMessageId::AIRSHIP_CANNOT_TELEPORT_NOT_ENOUGH_FUEL);
        }
        return;
    }

    OnTakeOff(m_AutoInfo.pAirport, m_AutoInfo.pPlatform);
    m_AutoInfo.type = ASIT_TAKEOFF;
    m_AutoInfo.nodeIndex = 0;
    m_AutoInfo.teleportIndex = teleportIndex;
    m_AutoInfo.stopoverTick = -1;
    const FVector target = m_AutoInfo.pPlatform->GetTakeOffPath(0);
    MoveToLocation(target);

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f65, user, nullptr, std::u16string(GetOwnerName()), nullptr, nullptr, m_const->m_Type, m_const->m_TypeId,
        GetDBID(), GetFuel(), static_cast<int>(target.x), static_cast<int>(target.y), static_cast<int>(target.z)
    );
}

void CAirShipForFree::AutoSail_Landing() {
    L2_GUARD_FUNCTION();
    FVector target = m_AutoInfo.pPlatform->GetLandingPath(m_AutoInfo.nodeIndex);
    const int halfSpeed = m_Speed / 2;
    const double reachDistanceSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(halfSpeed, halfSpeed));

    if (!target.IsZero()) {
        if (m_bMoving || GetPos().DistCube(target) <= reachDistanceSquared) {
            m_AutoInfo.nodeIndex = runtime::AddInt32Wrapping(m_AutoInfo.nodeIndex, 1);
            target = m_AutoInfo.pPlatform->GetLandingPath(m_AutoInfo.nodeIndex);
            if (target.IsZero()) {
                target = m_AutoInfo.pPlatform->GetShipPos();
            }
        }
        MoveToLocation(target);
        return;
    }

    target = m_AutoInfo.pPlatform->GetShipPos();
    if (GetPos().DistCube(target) <= reachDistanceSquared) {
        m_AutoInfo.stopoverTick = runtime::AddInt32Wrapping(m_AutoInfo.stopoverTick, 1);
        if (m_AutoInfo.stopoverTick == 0) {
            m_SavingPos = m_AutoInfo.pAirport->GetGetOffInfo(m_AutoInfo.pPlatform->Id()).m_Pos;
            OnLanding(m_AutoInfo.pAirport->m_Id, m_AutoInfo.pPlatform->Id());
        }
        if (runtime::MultiplyInt32Wrapping(m_AutoInfo.pAirport->m_Stopover, 1000) <
            runtime::MultiplyInt32Wrapping(m_AutoInfo.stopoverTick, 500)) {
            TakeOff(-1, nullptr);
        }
        return;
    }
    MoveToLocation(target);
}

void CAirShipForFree::AutoSail_Takeoff() {
    L2_GUARD_FUNCTION();
    FVector target = m_AutoInfo.pPlatform->GetTakeOffPath(m_AutoInfo.nodeIndex);
    if (target.IsZero()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] platform does not have takeoff path node(%d)",
        //     kAirShipSourceWide,
        //     0x1151,
        //     m_AutoInfo.nodeIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] platform does not have takeoff path node(%d)",
        //     kAirShipSourceWide,
        //     0x1151,
        //     m_AutoInfo.nodeIndex)
        L2_LOG_ERROR(
            Log, "Airship %u takeoff waits: platform %d returned a zero position for path node %d.",
            static_cast<unsigned>(m_nId), m_AutoInfo.pPlatform->Id(), m_AutoInfo.nodeIndex
        );
        return;
    }

    const int halfSpeed = m_Speed / 2;
    const double reachDistanceSquared = static_cast<double>(runtime::MultiplyInt32Wrapping(halfSpeed, halfSpeed));
    if (GetPos().DistCube(target) <= reachDistanceSquared) {
        m_AutoInfo.nodeIndex = runtime::AddInt32Wrapping(m_AutoInfo.nodeIndex, 1);
        target = m_AutoInfo.pPlatform->GetTakeOffPath(m_AutoInfo.nodeIndex);
        if (target.IsZero()) {
            if (!HavePassengers()) {
                m_AutoInfo.type = ASIT_NONE;
                CAirShipManager::Inst().DestoryAirShip(m_nId, ASDR_EMPTY);
                return;
            }

            CAirportTeleport* const teleport = m_AutoInfo.pAirport->GetTeleport(m_AutoInfo.teleportIndex);
            if (teleport != nullptr) {
                const FVector destination = teleport->m_Pos;
                m_AutoInfo.type = ASIT_NONE;
                m_AutoInfo.nodeIndex = -1;
                m_AutoInfo.pPlatform = nullptr;
                m_AutoInfo.pAirport = nullptr;
                m_AutoInfo.teleportIndex = -1;

                CLogFormatter formatter;
                formatter.WriteLog1(
                    0x1f62, GetDriver(), nullptr, std::u16string(GetOwnerName()), nullptr, nullptr, m_const->m_Type,
                    m_const->m_TypeId, GetDBID(), teleport->m_FuelConsume, 0, 0, 0
                );
                ConsumeFuel(teleport->m_FuelConsume);

                FAngle direction = GetDir();
                direction.Yaw = static_cast<std::int32_t>(static_cast<std::uint32_t>(direction.Yaw) & 0xffffU);
                TeleportToLocation(
                    static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z),
                    GetInZoneID(), direction.Yaw
                );
                return;
            }

            const FVector position = GetPos();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] airport does not teleport info(%d) (%d, %d, %d)",
            //     kAirShipSourceWide,
            //     0x1185,
            //     m_AutoInfo.teleportIndex,
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] airport does not teleport info(%d) (%d, %d, %d)",
            //     kAirShipSourceWide,
            //     0x1185,
            //     m_AutoInfo.teleportIndex,
            //     static_cast<int>(position.x),
            //     static_cast<int>(position.y),
            //     static_cast<int>(position.z))
            L2_LOG_ERROR(
                Log,
                "Airship %u at (%.17g, %.17g, %.17g): teleport entry %d is missing at airport %d after takeoff path end; "
                "movement to the zero path target is still requested.",
                static_cast<unsigned>(m_nId), position.x, position.y, position.z, m_AutoInfo.teleportIndex,
                m_AutoInfo.pAirport->m_Id
            );
        }
        MoveToLocation(target);
    } else if (!m_bMoving) {
        MoveToLocation(target);
    }
}

void CAirShipForFree::AutoSail() {
    L2_GUARD_FUNCTION();
    if (m_AutoInfo.type == ASIT_LANDING) {
        AutoSail_Landing();
    } else {
        AutoSail_Takeoff();
    }
}

void CAirShipForFree::OnTimerExpired() {
    L2_GUARD_FUNCTION();
    CAirShip::OnTimerExpired();

    if (m_AutoInfo.type == ASIT_NONE) {
        ++m_FuelTick;
        if (m_FuelTick % 120U == 0U) {
            ConsumeFuel(m_const->CastForFree()->m_FuelConsume);
        }

        double baseSpeed = m_const->m_Speed[0];
        m_SpeedAreaFactor = 100;
        CAirShipAreaDB::Inst().CheckAirShip(this);
        if (m_Fuel < 1) {
            baseSpeed *= 0.25;
        }
        const int speed = AltitudeHelper::GetSpeedByAltitude(
            baseSpeed * (static_cast<double>(m_SpeedAreaFactor) / 100.0), runtime::Win64CvttDoubleToInt32(GetPos().z)
        );
        if (m_Speed != speed) {
            m_Speed = speed;
            runtime::raw_atomic::StoreI32(&m_SomethingChanged, 1);
        }

        if (!HavePassengers()) {
            ++m_EmptyTick;
            if (m_EmptyTick == 120U) {
                SaveInfo();
            }
            CAirShipManager::Inst().DestoryAirShip(m_nId, ASDR_EMPTY);
            return;
        }
        m_EmptyTick = 0;
    }

    if (runtime::raw_atomic::CompareExchangeI32(&m_SomethingChanged, 0, 1) == 1) {
        SendAirShipInfo(nullptr);
    }
}

bool CAirShipForFree::IsLanded() {
    L2_GUARD_FUNCTION();
    return m_AutoInfo.type == ASIT_LANDING && m_AutoInfo.teleportIndex == -1;
}

bool CAirShipForFree::CanBeDestroyed() {
    L2_GUARD_FUNCTION();
    return !HavePassengers() && m_AutoInfo.type == ASIT_NONE;
}

bool CAirShipForRegular::IsLanded() {
    L2_GUARD_FUNCTION();
    return m_AutoInfo.stopoverTick != -1;
}

bool CAirShipForRegular::CanBeDestroyed() {
    L2_GUARD_FUNCTION();
    return !HavePassengers();
}

void CAirShipForFree::SendTeleportList(User* user) {
    L2_GUARD_FUNCTION();
    if (!IsLanded() || !IsDriver(user)) {
        return;
    }
    if (m_AutoInfo.pAirport == nullptr) {
        const FVector position = GetPos();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airport does not exist. (%d, %d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x1203,
        //     m_const->m_TypeId,
        //     static_cast<int>(m_data->m_OwnerDBID),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airport does not exist. (%d, %d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x1203,
        //     m_const->m_TypeId,
        //     static_cast<int>(m_data->m_OwnerDBID),
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        L2_LOG_ERROR(
            Log,
            "Cannot send teleport destinations for airship %u (template %d, owner %u) at (%.17g, %.17g, %.17g): current "
            "airport is missing.",
            static_cast<unsigned>(m_nId), m_const->m_TypeId, static_cast<unsigned>(m_data->m_OwnerDBID), position.x,
            position.y, position.z
        );
        return;
    }
    m_AutoInfo.pAirport->SendTeleportList(user);
}

void CAirShipForFree::_GetOff(User* user) {
    L2_GUARD_FUNCTION();
    if (m_AutoInfo.pAirport == nullptr || m_AutoInfo.pPlatform == nullptr) {
        const FVector position = GetPos();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airport does not exist. node(%d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x119c,
        //     m_AutoInfo.nodeIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airport does not exist. node(%d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x119c,
        //     m_AutoInfo.nodeIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        L2_LOG_ERROR(
            Log,
            "Cannot disembark character %u from airship %u at (%.17g, %.17g, %.17g), node %d: current airport or platform "
            "is missing.",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(m_nId), position.x, position.y, position.z,
            m_AutoInfo.nodeIndex
        );
        return;
    }

    user->GetOffAirShip(this, m_AutoInfo.pAirport->GetGetOffInfo(m_AutoInfo.pPlatform->Id()).m_Pos);
}

void CAirShipForRegular::_GetOff(User* user) {
    L2_GUARD_FUNCTION();
    CAirShipConstRegular* const regular = m_const->CastForRegular();
    if (regular == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] regular const does noe exist. (%d)",
        //     kAirShipSourceWide,
        //     0x138f,
        //     m_const->m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] regular const does noe exist. (%d)",
        //     kAirShipSourceWide,
        //     0x138f,
        //     m_const->m_TypeId)
        L2_LOG_ERROR(
            Log, "Cannot disembark character %u from airship %u: template %d has no regular-route data.",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(m_nId), m_const->m_TypeId
        );
        return;
    }

    const AirShipPathNode& node = regular->GetNode(m_AutoInfo.nodeIndex);
    CAirport* const airport = CAirShipManager::Inst().GetAirport(node.dir);
    if (airport == nullptr) {
        return;
    }

    const FVector position = airport->GetGetOffInfo(-1).m_Pos;
    user->GetOffAirShip(this, position);

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f6c, user, nullptr, nullptr, nullptr, nullptr, m_const->m_TypeId, static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), 0, 0, 0
    );
}

void CAirShip::InSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CObject* target) : m_target(target) {}

        bool CanDo(CObject* candidate) override { return candidate != m_target && candidate->IsPC(); }

    private:
        CObject* m_target;
    } filter(object);

    class ContainerFunc final : public CContainerFunc {
    public:
        explicit ContainerFunc(CObject* target) : m_target(target) {}

        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->CastUser()->InSight(m_target, false);
        }

    private:
        CObject* m_target;
    } function(object);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

void CAirShip::OutOfSight(CObject* object, bool flag) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        explicit ContainerFilter(CObject* target) : m_target(target) {}

        bool CanDo(CObject* candidate) override { return candidate != m_target && candidate->IsPC(); }

    private:
        CObject* m_target;
    } filter(object);

    class ContainerFunc final : public CContainerFunc {
    public:
        ContainerFunc(CObject* target, bool flag) : m_target(target), m_flag(flag) {}

        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->CastUser()->OutOfSight(m_target, m_flag);
        }

    private:
        CObject* m_target;
        bool m_flag;
    } function(object, flag);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

void CAirShip::KickoutAllPassengers(FVector position) {
    L2_GUARD_FUNCTION();

    class ContainerFilter final : public CContainerFilter {
    public:
        bool CanDo(CObject* object) override { return object->IsPC(); }
    } filter;

    class ContainerFunc final : public CContainerFunc {
    public:
        ContainerFunc(CAirShip* airShip, FVector position) : m_airShip(airShip), m_position(position) {}

        void Do(CObject* object) override {
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            object->CastUser()->GetOffAirShip(m_airShip, m_position);
        }

    private:
        CAirShip* m_airShip;
        FVector m_position;
    } function(this, position);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
    m_PassengerCount = 0;
}

bool CAirShip::CanGetOn(User* const user, bool) {
    // Current L2Server 0x0046ec00, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();

    unsigned int failureMessage = 0;
    if (user->IsRiding()) {
        failureMessage = SystemMessageId::ALREADY_ABOARD_ANOTHER_VEHICLE;
    } else if (user->m_data->m_bIsInCombatMode) {
        failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_IN_COMBAT;
    } else if (user->IsInDuel()) {
        failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_IN_DUEL;
    } else if (CReplyChecker::IsUserWaitingReply(user) || user->IsRegisteredAtOlympiad() ||
        user->m_PledgeGameUser.IsPlayer()) {
        failureMessage = SystemMessageId::CANNOT_BOARD_REQUIREMENTS_NOT_MET;
    } else if (user->m_data->m_StopMode != STP_STAND) {
        failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_SITTING;
    } else if (!user->IsAlive()) {
        failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_DEAD;
    } else if (user->GetController()->GetCurrentActionType() == ACT_SKILL) {
        failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_CASTING;
    } else if (user->GetPrivateStoreType() != 0 || user->IsNowTrade()) {
        failureMessage = SystemMessageId::CANNOT_BOARD_REQUIREMENTS_NOT_MET;
    } else {
        static_cast<void>(CCursedWeaponMgr::inst());
        if (user->m_nCursedWeaponClassId >= 1) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WITH_CURSED_WEAPON;
        } else if (user->IsTransformed(TransformType::Flying) != nullptr) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_TRANSFORMED;
        } else if (user->IsFrozen(true) || user->m_SkillMod.m_nBlockMoveCount != 0 || !user->m_data->m_bCanMove ||
            !user->m_SkillMod.m_bCanAct) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_UNABLE_TO_ACT;
        } else if (user->HaveSummon() || user->HavePet()) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WITH_PET_OR_SERVITOR_SUMMONED;
        } else if (user->m_pFishingController != nullptr &&
            (user->m_pFishingController->m_FishingState != FISHINGS_NONE || user->m_pFishingController->m_bIsAutoFishing)) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_FISHING;
        } else if (user->IsEquippedWithForcedItem(ST_RLHAND)) {
            failureMessage = SystemMessageId::CANNOT_BOARD_VEHICLE_WHILE_CARRYING_FLAG;
        } else if (!IsLanded()) {
            failureMessage = SystemMessageId::CANNOT_BOARD_MOVING_VEHICLE;
        } else if (m_PassengerCount <= m_const->m_MaxPassenger) {
            return true;
        } else {
            failureMessage = SystemMessageId::CANNOT_BOARD_AIRSHIP_PASSENGER_LIMIT_EXCEEDED;
        }
    }

    user->SendSystemMessage(failureMessage);
    return false;
}

bool CAirShip::GetOn(User* const user, const bool forced) {
    // Current L2Server 0x0046ef94, original AirShip.cpp owner.
    L2_GUARD_FUNCTION();
    if (!CanGetOn(user, forced) || !user->GetOnAirShip(this)) {
        return false;
    }

    if (m_const->m_Type == ASTE_REGULAR) {
        const FVector position = GetPos();
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x1f6b, user, nullptr, m_const->m_TypeId, runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), 0, 0, 0
        );
    }
    m_PassengerCount = runtime::AddInt32Wrapping(m_PassengerCount, 1);
    return true;
}

bool CAirShip::HavePassengers() {
    L2_GUARD_FUNCTION();

    class ContainerFilter final : public CContainerFilter {
    public:
        bool CanDo(CObject* object) override { return object->IsPC(); }
    } filter;

    class ContainerFunc final : public CContainerFunc {
    public:
        void Do(CObject*) override { ++m_count; }

        int m_count{};
    } function;

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
    return function.m_count > 0;
}

void CAirShipForRegular::BroadcastMovePacket() {
    L2_GUARD_FUNCTION();
    m_pController->ActionBroadcastMove(GetPos(), m_vMovingDest);
}

bool CAirShipForRegular::EnterWorld(int, int) {
    L2_GUARD_FUNCTION();
    CAirShipConstRegular* const regular = m_const->CastForRegular();
    const AirShipPathNode& node = regular->GetNode(0);
    SetInZoneID(0);
    SetDir(FAngle{node.dir});
    m_AutoInfo.type = ASIT_REGULAR;
    m_AutoInfo.nodeIndex = 0;
    m_AutoInfo.stopoverTick = -1;
    _EnterWorld(node.pos);
    return true;
}

void CAirShipForRegular::OnEnterWorld() {
    L2_GUARD_FUNCTION();
    OnNodeChanged();
    CAirShip::OnEnterWorld();
}

void CAirShipForRegular::OnNode(const AirShipPathNode& node) {
    L2_GUARD_FUNCTION();
    if (node.nodeType == ASPNT_MOVE || node.nodeType == ASPNT_INTO_AIRPORT) {
        MoveToLocation(node.pos);
    } else if (node.nodeType == ASPNT_TELEPORT) {
        TeleportToLocation(
            static_cast<int>(node.pos.x), static_cast<int>(node.pos.y), static_cast<int>(node.pos.z), GetInZoneID(), node.dir
        );
    } else {
        const FVector position = GetPos();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid nodetype(%d) on node (%d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x133b,
        //     static_cast<int>(node.nodeType),
        //     m_AutoInfo.nodeIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid nodetype(%d) on node (%d) (%d, %d, %d)",
        //     kAirShipSourceWide,
        //     0x133b,
        //     static_cast<int>(node.nodeType),
        //     m_AutoInfo.nodeIndex,
        //     static_cast<int>(position.x),
        //     static_cast<int>(position.y),
        //     static_cast<int>(position.z))
        L2_LOG_ERROR(
            Log, "Airship %u at (%.17g, %.17g, %.17g): unsupported path node type %d at index %d; no node action is issued.",
            static_cast<unsigned>(m_nId), position.x, position.y, position.z, static_cast<int>(node.nodeType),
            m_AutoInfo.nodeIndex
        );
    }
}

void CAirShipForRegular::OnNodeChanged() {
    L2_GUARD_FUNCTION();
    CAirShipConstRegular* const regular = m_const->CastForRegular();
    if (regular == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship(%d) is not regular type.",
        //     kAirShipSourceWide,
        //     0x1317,
        //     m_const->m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airship(%d) is not regular type.",
        //     kAirShipSourceWide,
        //     0x1317,
        //     m_const->m_TypeId)
        L2_LOG_ERROR(
            Log, "Cannot advance route for airship %u: template %d has no regular-route data.", static_cast<unsigned>(m_nId),
            m_const->m_TypeId
        );
        return;
    }

    m_StopDebugInfo.tick = 0;
    m_StopDebugInfo.position = GetPos();
    const AirShipPathNode& node = regular->GetNextNode(m_AutoInfo.nodeIndex);
    OnNode(node);
}

void CAirShipForRegular::SendDebugInfo(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }

    CAirShipConstRegular* const regular = m_const->CastForRegular();
    if (regular == nullptr) {
        return;
    }

    const FVector current = GetPos();
    const FVector movingDestination = m_vMovingDest;
    const FVector debugPosition = m_StopDebugInfo.position;
    const AirShipPathNode& currentNode = regular->GetNode(m_AutoInfo.nodeIndex);
    const AirShipPathNode& destinationNode = regular->GetNode(regular->GetNextIndex(m_AutoInfo.nodeIndex));

    const auto squaredDistance = [&current](const FVector& position) {
        const double dx = position.x - current.x;
        const double dy = position.y - current.y;
        const double dz = position.z - current.z;
        return dx * dx + dy * dy + dz * dz;
    };

    std::array<char16_t, 0x200> message{};
    FormatUtf16To(
        message.data(), 0x1ff,
        u"sid:%d auto_index:%d now_pos:(%d,%d,%d) "
        u"stop_sec:%d moving?:%d "
        u"now_node_pos:(%d,%d,%d:%.2f) "
        u"dest_node_pos:(%d,%d,%d:%.2f) "
        u"move_dest_pos:(%d,%d,%d:%.2f) "
        u"debug_pos:(%d,%d,%d:%.2f)",
        static_cast<int>(m_nId), m_AutoInfo.nodeIndex, static_cast<int>(current.x), static_cast<int>(current.y),
        static_cast<int>(current.z), static_cast<unsigned int>(m_StopDebugInfo.tick) >> 1, m_bMoving ? 1 : 0,
        static_cast<int>(currentNode.pos.x), static_cast<int>(currentNode.pos.y), static_cast<int>(currentNode.pos.z),
        squaredDistance(currentNode.pos), static_cast<int>(destinationNode.pos.x), static_cast<int>(destinationNode.pos.y),
        static_cast<int>(destinationNode.pos.z), squaredDistance(destinationNode.pos), static_cast<int>(movingDestination.x),
        static_cast<int>(movingDestination.y), static_cast<int>(movingDestination.z), squaredDistance(movingDestination),
        static_cast<int>(debugPosition.x), static_cast<int>(debugPosition.y), static_cast<int>(debugPosition.z),
        squaredDistance(debugPosition)
    );

    user->GetSocket()->SendSystemMessage(u"SYS", message.data());
}

void CAirShipForRegular::AutoSail() {
    L2_GUARD_FUNCTION();
    CAirShipConstRegular* const regular = m_const->CastForRegular();
    if (regular == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] airship(%d) is not regular type.",
        //     kAirShipSourceWide,
        //     0x129f,
        //     m_const->m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] airship(%d) is not regular type.",
        //     kAirShipSourceWide,
        //     0x129f,
        //     m_const->m_TypeId)
        L2_LOG_ERROR(
            Log, "Automatic sailing skipped for airship %u: template %d has no regular-route data.",
            static_cast<unsigned>(m_nId), m_const->m_TypeId
        );
        return;
    }

    const AirShipPathNode& node = regular->GetNode(m_AutoInfo.nodeIndex);
    if (node.nodeType == ASPNT_NONE) {
        return;
    }

    if (node.nodeType == ASPNT_TELEPORT) {
        m_AutoInfo.stopoverTick = runtime::AddInt32Wrapping(m_AutoInfo.stopoverTick, 1);
        if (runtime::MultiplyInt32Wrapping(m_AutoInfo.stopoverTick, 500) < 2000) {
            return;
        }
        m_AutoInfo.stopoverTick = -1;
    }

    const FVector position = GetPos();
    const double dx = position.x - node.pos.x;
    const double dy = position.y - node.pos.y;
    const double dz = position.z - node.pos.z;
    const int reachDistanceSquared = runtime::MultiplyInt32Wrapping(m_Speed, m_Speed);
    if (dx * dx + dy * dy + dz * dz <= static_cast<double>(reachDistanceSquared)) {
        m_data->SetPos(node.pos);
        if (node.nodeType == ASPNT_INTO_AIRPORT) {
            CAirport* const airport = CAirShipManager::Inst().GetAirport(node.dir);
            if (airport == nullptr) {
                const FVector current = GetPos();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] airport(%d) does not exist. (%d, %d, %d)",
                //     kAirShipSourceWide,
                //     0x1306,
                //     node.dir,
                //     static_cast<int>(current.x),
                //     static_cast<int>(current.y),
                //     static_cast<int>(current.z))
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] airport(%d) does not exist. (%d, %d, %d)",
                //     kAirShipSourceWide,
                //     0x1306,
                //     node.dir,
                //     static_cast<int>(current.x),
                //     static_cast<int>(current.y),
                //     static_cast<int>(current.z))
                L2_LOG_ERROR(
                    Log,
                    "Airship %u at (%.17g, %.17g, %.17g) reached a node for missing airport %d; airport stop is skipped and "
                    "the route advances.",
                    static_cast<unsigned>(m_nId), current.x, current.y, current.z, node.dir
                );
            } else {
                m_AutoInfo.stopoverTick = runtime::AddInt32Wrapping(m_AutoInfo.stopoverTick, 1);
                if (m_AutoInfo.stopoverTick == 0) {
                    m_SavingPos = airport->GetGetOffInfo(-1).m_Pos;
                    KickoutAllPassengers(airport->GetGetOffInfo(-1).m_Pos);
                    OnLanding(airport->m_Id, 0);
                    return;
                }

                if (runtime::MultiplyInt32Wrapping(m_AutoInfo.stopoverTick, 500) <=
                    runtime::MultiplyInt32Wrapping(airport->m_Stopover, 1000)) {
                    return;
                }
                m_AutoInfo.stopoverTick = -1;
                OnTakeOff(airport, nullptr);
            }
        }
        OnNodeChanged();
        return;
    }

    const FVector current = GetPos();
    if (!m_StopDebugInfo.position.IsEqual(current)) {
        m_StopDebugInfo.tick = 0;
        m_StopDebugInfo.position = current;
    } else {
        m_StopDebugInfo.tick = runtime::AddInt32Wrapping(m_StopDebugInfo.tick, 1);
        const std::uint32_t stopTick = static_cast<std::uint32_t>(m_StopDebugInfo.tick);
        if (stopTick % 60U == 0U) {
            const FVector now = GetPos();
            // Log.Add(
            //     LOG_ERROR,
            //     L"AIRSHIP(%d) cannot move. (%d,%d,%d) -> "
            //     L"(%d,%d,%d) now(%d,%d,%d)",
            //     m_const->m_TypeId,
            //     static_cast<int>(m_StopDebugInfo.position.x),
            //     static_cast<int>(m_StopDebugInfo.position.y),
            //     static_cast<int>(m_StopDebugInfo.position.z),
            //     static_cast<int>(node.pos.x),
            //     static_cast<int>(node.pos.y),
            //     static_cast<int>(node.pos.z),
            //     static_cast<int>(now.x),
            //     static_cast<int>(now.y),
            //     static_cast<int>(now.z))
            // L2_LOG_ERROR(Log,
            //     L"AIRSHIP(%d) cannot move. (%d,%d,%d) -> "
            //     L"(%d,%d,%d) now(%d,%d,%d)",
            //     m_const->m_TypeId,
            //     static_cast<int>(m_StopDebugInfo.position.x),
            //     static_cast<int>(m_StopDebugInfo.position.y),
            //     static_cast<int>(m_StopDebugInfo.position.z),
            //     static_cast<int>(node.pos.x),
            //     static_cast<int>(node.pos.y),
            //     static_cast<int>(node.pos.z),
            //     static_cast<int>(now.x),
            //     static_cast<int>(now.y),
            //     static_cast<int>(now.z))
            L2_LOG_ERROR(
                Log,
                "Airship %u (template %d) has not advanced for %u position checks: saved (%.17g, %.17g, %.17g), target "
                "(%.17g, %.17g, %.17g), current (%.17g, %.17g, %.17g).",
                static_cast<unsigned>(m_nId), m_const->m_TypeId, static_cast<unsigned>(stopTick), m_StopDebugInfo.position.x,
                m_StopDebugInfo.position.y, m_StopDebugInfo.position.z, node.pos.x, node.pos.y, node.pos.z, now.x, now.y,
                now.z
            );
        }
        if (stopTick % 10U == 0U) {
            const FVector now = GetPos();
            // Log.Add(
            //     LOG_ERROR,
            //     L"AIRSHIP(%d) is moving by FORCE on "
            //     L"(%d,%d,%d) -> (%d:%d,%d,%d)",
            //     m_const->m_TypeId,
            //     static_cast<int>(now.x),
            //     static_cast<int>(now.y),
            //     static_cast<int>(now.z),
            //     m_AutoInfo.nodeIndex,
            //     static_cast<int>(node.pos.x),
            //     static_cast<int>(node.pos.y),
            //     static_cast<int>(node.pos.z))
            // L2_LOG_ERROR(Log,
            //     L"AIRSHIP(%d) is moving by FORCE on "
            //     L"(%d,%d,%d) -> (%d:%d,%d,%d)",
            //     m_const->m_TypeId,
            //     static_cast<int>(now.x),
            //     static_cast<int>(now.y),
            //     static_cast<int>(now.z),
            //     m_AutoInfo.nodeIndex,
            //     static_cast<int>(node.pos.x),
            //     static_cast<int>(node.pos.y),
            //     static_cast<int>(node.pos.z))
            L2_LOG_ERROR(
                Log,
                "Retrying path node %d for airship %u (template %d) after %u unchanged-position checks: current (%.17g, "
                "%.17g, %.17g), target (%.17g, %.17g, %.17g).",
                m_AutoInfo.nodeIndex, static_cast<unsigned>(m_nId), m_const->m_TypeId, static_cast<unsigned>(stopTick),
                now.x, now.y, now.z, node.pos.x, node.pos.y, node.pos.z
            );
            OnNode(node);
            return;
        }
    }

    ++m_TickCount;
    if (m_TickCount % 10U == 0U) {
        BroadcastMovePacket();
    }
}

FVector CAirShip::GetPos() const { return m_data->GetPos(); }

FAngle CAirShip::GetDir() const { return m_data->GetDir(); }

void CAirShip::SetDir(const FAngle& direction) { m_data->SetDir(direction); }

int CAirShip::GetIndex() const { return m_data->index; }

bool CAirShip::SetMovingIntoCarrier(CMoveRelativeAction* action) {
    L2_GUARD_FUNCTION();
    return action->SetMovingIntoAirShipImpl();
}

bool CAirShip::IsDriver(User* user) const {
    L2_GUARD_FUNCTION();
    return user != nullptr && m_data->m_DriverId == user->m_nId;
}

User* CAirShip::GetDriver() {
    L2_GUARD_FUNCTION();
    if (m_data->m_DriverId == 0) {
        return nullptr;
    }
    return CSmartID(m_data->m_DriverId).GetUser();
}

FVector CAirShip::GetDetailLocation() const {
    if (!m_bMoving) {
        return GetPos();
    }

    const std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - m_nLastTick);
    std::int32_t clampedElapsed = 0;
    if (elapsed >= 0) {
        clampedElapsed = std::min(elapsed, 500);
    }
    const std::int32_t currentLiteralTime = std::max(0, std::min(clampedElapsed, static_cast<std::int32_t>(m_nLastTick)));
    const double ratio = static_cast<double>(currentLiteralTime) / 500.0;
    const FVector position = GetPos();
    return FVector{position.x + m_vMovingDirection.x * ratio, position.y + m_vMovingDirection.y * ratio,
        position.z + m_vMovingDirection.z * ratio};
}

void CAirShip::GetClosestIntersectionPoint(
    FVector& closestPoint,
    CAirShip* otherAirShip,
    CSkillInfo* skillInfo,
    CTerritory& territory,
    const FVector& source,
    const FVector& destination
) {
    // Current L2Server 0x00463fec.
    double closestDistance = 100000000.0;
    const FVector otherPosition = otherAirShip->GetPos();
    const FAngle otherDirection = otherAirShip->GetDir();
    if (!territory.IsInside(otherPosition, false)) {
        return;
    }

    const int affectRange = skillInfo->m_nAffectRange;
    const int sourceZ = static_cast<int>(source.z);
    const int otherZ = static_cast<int>(otherAirShip->GetPos().z);
    if (otherZ > sourceZ + affectRange || sourceZ - affectRange > otherZ + 750) {
        return;
    }

    const std::uint32_t yaw = static_cast<std::uint16_t>(otherDirection.Yaw);
    const double radians = static_cast<double>(yaw) * 3.141592653589793 / 32767.5;
    const FVector direction{std::cos(radians), std::sin(radians), 0.0};
    const FVector axis{0.0, 0.0, 1.0};
    const FVector left = FVector::RotateAngleAxis(&direction, -0x4000, &axis);
    const FVector right = FVector::RotateAngleAxis(&direction, 0x4000, &axis);

    const double levelZ = static_cast<double>(static_cast<int>(otherPosition.z));
    const FVector front{otherPosition.x + direction.x * 400.0, otherPosition.y + direction.y * 400.0, levelZ};
    const FVector back{otherPosition.x - direction.x * 400.0, otherPosition.y - direction.y * 400.0, levelZ};
    const FVector frontLeft{front.x + left.x * 225.0, front.y + left.y * 225.0, levelZ};
    const FVector frontRight{front.x + right.x * 225.0, front.y + right.y * 225.0, levelZ};
    const FVector backLeft{back.x + left.x * 225.0, back.y + left.y * 225.0, levelZ};
    const FVector backRight{back.x + right.x * 225.0, back.y + right.y * 225.0, levelZ};

    const FVector segmentBegin[5] = {front, frontLeft, backLeft, frontLeft, frontRight};
    const FVector segmentEnd[5] = {back, frontRight, backRight, backLeft, backRight};

    for (std::size_t index = 0; index < 5; ++index) {
        if (!IsIntersectionNew(source, destination, segmentBegin[index], segmentEnd[index])) {
            continue;
        }

        FVector lineA = back;
        FVector lineB = front;
        FVector shotEnd = destination;
        FVector shotBegin = source;
        lineA.z = 1.0;
        lineB.z = 1.0;
        shotEnd.z = 1.0;
        shotBegin.z = 1.0;

        const double shotX = shotEnd.z * shotBegin.y - shotEnd.y * shotBegin.z;
        const double shipX = lineA.z * lineB.y - lineA.y * lineB.z;
        const double shipY = lineA.x * lineB.z - lineB.x * lineA.z;
        const double shipZ = lineB.x * lineA.y - lineA.x * lineB.y;
        const double shotY = shotEnd.x * shotBegin.z - shotBegin.x * shotEnd.z;
        const double shotZ = shotBegin.x * shotEnd.y - shotEnd.x * shotBegin.y;
        const double denominator = shipY * shotX - shotY * shipX;
        if (denominator == 0.0) {
            continue;
        }

        const FVector intersection{
            (shotY * shipZ - shotZ * shipY) / denominator, (shotZ * shipX - shipZ * shotX) / denominator, 0.0};
        const double dx = source.x - intersection.x;
        const double dy = source.y - intersection.y;
        const double distance = dx * dx + dy * dy;
        if (distance < closestDistance) {
            closestPoint = intersection;
            closestDistance = distance;
        }
    }
}

FVector CAirShip::GetTargetPos(CCreature* creature, CSkillInfo* skillInfo, double time) {
    // Current L2Server 0x0046d824.
    L2_GUARD_FUNCTION();
    const FVector shipPosition = GetPos();
    const FVector actorPosition = creature->GetPos();
    const FVector shipDirection = GetNormalizedDirVector(0);
    const double forwardX = shipPosition.x + shipDirection.x * 10.0;
    const double forwardY = shipPosition.y + shipDirection.y * 10.0;
    const double side = actorPosition.y * forwardX + shipPosition.x * forwardY + actorPosition.x * shipPosition.y -
        forwardX * shipPosition.y - actorPosition.x * forwardY - actorPosition.y * shipPosition.x;
    const int angle = side < 0.0 ? -0x4000 : 0x4000;
    const FVector axis{0.0, 0.0, 1.0};
    const FVector firingDirection = FVector::RotateAngleAxis(&shipDirection, angle, &axis);

    const FAngle actorDirection{
        static_cast<int>(std::atan2(firingDirection.y, firingDirection.x) * 65535.0 / 6.283185307179586)};
    creature->m_data->SetDir(actorDirection);
    creature->CastNPC()->SendNPCInfo(nullptr, false, false);

    const double effectiveRange = static_cast<double>(skillInfo->m_nEffectiveRange);
    FVector target{actorPosition.x + effectiveRange * firingDirection.x,
        actorPosition.y + effectiveRange * firingDirection.y, actorPosition.z + effectiveRange * firingDirection.z};
    if (m_bMoving && m_Speed != 0) {
        const double seconds = time / 1000.0;
        const double speed = static_cast<double>(m_Speed);
        target.x += speed * shipDirection.x * seconds;
        target.y += speed * shipDirection.y * seconds;
        target.z += speed * shipDirection.z * seconds;
    }

    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld::Inst().GetGeo()->MoveStraightFly(
        actorPosition, target, 4096.0, target, distanceExhausted, 0.0, false, travelledDistance
    );

    CTerritory territory;
    territory.MakeRectangle(actorPosition, target, firingDirection, 400.0);
    const double radius = travelledDistance * 0.5;
    const FVector center{actorPosition.x + firingDirection.x * radius, actorPosition.y + firingDirection.y * radius,
        actorPosition.z + firingDirection.z * radius};
    FVector closestPoint{};

    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
    BROADCAST_SET_SIZE_FAST(center, static_cast<int>(radius), minX, maxX, minY, maxY, minZ, maxZ);
    g_ptlAirShipMap->Read();
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            const int lastZ = g_ptlAirShipMap->IsZDivided(x, y) ? maxZ : minZ;
            for (int z = minZ; z <= lastZ; ++z) {
                TLMIterator iterator =
                    g_ptlAirShipMap->GetIterator(static_cast<short>(x), static_cast<short>(y), static_cast<char>(z));
                while (CObject* const object = *iterator) {
                    CAirShip* const otherAirShip = __SafeCastAirShip(object);
                    if (otherAirShip != nullptr && otherAirShip != this && otherAirShip->GetInZoneID() == GetInZoneID()) {
                        const FVector otherPosition = otherAirShip->GetPos();
                        const double dx = otherPosition.x - center.x;
                        const double dy = otherPosition.y - center.y;
                        const double dz = otherPosition.z - center.z;
                        if (dx * dx + dy * dy + dz * dz <= radius * radius) {
                            GetClosestIntersectionPoint(
                                closestPoint, otherAirShip, skillInfo, territory, actorPosition, target
                            );
                        }
                    }
                    ++iterator;
                }
            }
        }
    }

    if (!closestPoint.IsEqual(FVector{})) {
        target = closestPoint;
        target.z = static_cast<double>(static_cast<int>(actorPosition.z));
    }
    return target;
}

void CAirShip::FillOutputVectorWithParts(
    std::vector<CSafePointer2<CStaticObject>>& targets, std::vector<double>& distances, FVector& position
) {
    // Current L2Server 0x0046e478.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    for (const CSmartID partId : m_AirShipParts) {
        CStaticObject* const part = partId.GetStaticObject();
        if (part == nullptr || !part->IsAirShipPart()) {
            continue;
        }

        CSafePointer2<CStaticObject> safePart(part);
        targets.push_back(safePart);
        distances.push_back(std::sqrt(position.DistCube(part->GetPos())));
    }
}

void CAirShip::MoveToLocation(int x, int y, bool broadcast) {
    L2_GUARD_FUNCTION();
    if (!CanAction()) {
        return;
    }
    const int altitude = AltitudeHelper::AdjustAltitude(static_cast<int>(GetPos().z));
    m_pController->Move(FVector{x, y, altitude}, broadcast);
}

void CAirShip::MoveToLocation(FVector position) {
    L2_GUARD_FUNCTION();
    if (m_AutoInfo.type == ASIT_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] free z is allowed to only auto mode. airship(%d)",
        //     kAirShipSourceWide,
        //     0x6f3,
        //     m_const->m_TypeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] free z is allowed to only auto mode. airship(%d)",
        //     kAirShipSourceWide,
        //     0x6f3,
        //     m_const->m_TypeId)
        L2_LOG_ERROR(
            Log,
            "Airship %u (template %d) movement to (%.17g, %.17g, %.17g) rejected: explicit altitude requires automatic "
            "mode.",
            static_cast<unsigned>(m_nId), m_const->m_TypeId, position.x, position.y, position.z
        );
        return;
    }
    m_pController->Move(position, true);
}

void CAirShip::TeleportToLocation(int x, int y, int z, unsigned int inZoneId, int direction) {
    L2_GUARD_FUNCTION();
    if (!CanAction()) {
        return;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo->GetZone(((x + 0x50000) >> 15) + 10, ((y + 0x40000) >> 15) + 10) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Try to teleport to the invalid Position. driver[%d]",
        //     kAirShipSource,
        //     0x70a,
        //     static_cast<int>(m_data->m_DriverId))
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Try to teleport to the invalid Position. driver[%d]",
        //     kAirShipSource,
        //     0x70a,
        //     static_cast<int>(m_data->m_DriverId))
        L2_LOG_ERROR(
            Log,
            "Airship %u teleport to (%d, %d, %d), instance %u rejected: destination has no geodata zone (driver object %u).",
            static_cast<unsigned>(m_nId), x, y, z, inZoneId, static_cast<unsigned>(m_data->m_DriverId)
        );
        return;
    }

    m_pController->DoNothing();
    FVector position{x, y, z};
    if (m_const->m_Type != ASTE_REGULAR) {
        position.z = static_cast<double>(AltitudeHelper::AdjustAltitude(z));
    }
    CWorld::Inst().AirShipTeleport(this, position, inZoneId, direction);
}

void CAirShip::Ascent() {
    // Current L2Server.exe 0x00465e98.
    L2_GUARD_FUNCTION();
    if (CanAction() && AltitudeHelper::CanChangeAltitude(runtime::Win64CvttDoubleToInt32(GetPos().z), AAT_ASCENT) &&
        !m_pController->IsChangingAltitude()) {
        m_pController->Ascent();
    }
}

void CAirShip::Descent() {
    // Current L2Server.exe 0x00465fa4.
    L2_GUARD_FUNCTION();
    if (CanAction() && AltitudeHelper::CanChangeAltitude(runtime::Win64CvttDoubleToInt32(GetPos().z), AAT_DESCENT) &&
        !m_pController->IsChangingAltitude()) {
        m_pController->Descent();
    }
}

void CAirShip::OnMoveToLocationPacket(User* user, const unsigned char* packet) {
    // Current L2Server.exe 0x004665a8.
    L2_GUARD_FUNCTION();
    if (!IsDriver(user)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hacking? NOT AIRSHIP DRIVER user[%s] "
        //     L"try to move airship.",
        //     L"AirShip.cpp",
        //     0xf7d,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hacking? NOT AIRSHIP DRIVER user[%s] "
        //     L"try to move airship.",
        //     L"AirShip.cpp",
        //     0xf7d,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "Airship %u movement request rejected for character '%s': requester is not the current driver (driver object
        //     %u).", static_cast<unsigned>(m_nId), user != nullptr ? WideToUtf8(std::wstring_view(user->GetName())).c_str()
        //     : "<null>", static_cast<unsigned>(m_data->m_DriverId))
        L2_LOG_ERROR(
            Log,
            "Airship %u movement request rejected for character '%s': requester is not the current driver (driver object "
            "%u).",
            static_cast<unsigned>(m_nId), user != nullptr ? Utf16ToUtf8(user->GetName()).c_str() : "<null>",
            static_cast<unsigned>(m_data->m_DriverId)
        );
        return;
    }

    int command = 0;
    const unsigned char* payload = Disassemble(packet, "d", &command);
    if (command == 0) {
        int x = 0;
        int y = 0;
        Disassemble(payload, "dd", &x, &y);
        MoveToLocation(x, y, true);
    } else if (command == 1) {
        StopMove(true);
    } else if (command == 2) {
        Ascent();
    } else if (command == 3) {
        Descent();
    } else if (command == 4) {
        int fuel = 0;
        Disassemble(payload, "d", &fuel);
        Refuel(fuel, user);
    }
}

void CAirShip::StopMove(bool) {
    L2_GUARD_FUNCTION();
    if (!m_bMoving) {
        return;
    }
    const FVector position = GetPos();
    MoveToLocation(
        static_cast<int>(position.x + m_vMovingDirection.x), static_cast<int>(position.y + m_vMovingDirection.y), true
    );
}

void CAirShip::OnCannotMoveAnymorePacket(int, int, int) {
    // Current L2Server 0x00461b48 is an intentional guard-only no-op.
    L2_GUARD_FUNCTION();
}

void CAirShip::OnValidateLocation(FVector) {
    // Current L2Server 0x00461bc4 only reads m_data.
    L2_GUARD_FUNCTION();
    (void)m_data;
}

void CAirShip::OnStartRotating(int angle, int side) {
    // Current L2Server 0x0046b4e8.
    L2_GUARD_FUNCTION();
    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x5000, GetPos(), 0x800, "cdddd", 0x7a, static_cast<int>(m_nId), angle, side, 0
    );
}

void CAirShip::OnFinishRotating(int angle) {
    // Current L2Server 0x0046b5f4.
    L2_GUARD_FUNCTION();
    m_data->SetDirByInt(angle);
    g_ptlUserMap->BroadcastToNeighborDist(this, 0x5000, GetPos(), 0x800, "cddd", 0x61, static_cast<int>(m_nId), angle, 0);
}

void CAirShip::OnAppearingPacket(User* user) {
    // Current L2Server 0x0046bee8.
    L2_GUARD_FUNCTION();
    SendAirShipInfo(user->GetSocket());
    user->SendUserInfo(nullptr, nullptr);
    CWorld::Inst().OnAppearingIntoAirShip(this, user);
}

void CAirShip::LoadCannon(CNPC* npc) {
    // Current L2Server 0x0046a75c.
    L2_GUARD_FUNCTION();
    const FVector relativePosition = npc->GetRelPos();
    const CAirShipKey key{static_cast<std::int32_t>(relativePosition.x), static_cast<std::int32_t>(relativePosition.y)};

    {
        L2_GUARD(*m_lock);
        m_AirShipCannons[key] = npc->GetSmartID();
    }
}

bool CAirShip::SetDriver(User* user, bool forced) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        const DriverHelper::AirShipDriverType result = DriverHelper::CanDrive(this, user, forced);
        if (result == DriverHelper::ASDT_FAILED) {
            return false;
        }

        StopMove(true);
        user->StopMove();
        if (result == DriverHelper::ASDT_RESET) {
            m_data->m_DriverId = 0;
        } else {
            m_data->m_DriverId = user->m_nId;
            user->SetRelPos(m_const->CastForFree()->GetDriverPos());
            user->UnequipItem(static_cast<SlotType>(7), false);
            user->UnequipItem(static_cast<SlotType>(8), false);
            user->UnequipItem(static_cast<SlotType>(14), false);
        }
        user->ParamChanged(PCB_USER | PCB_CHAR_AIRSHIP);
        SendAirShipInfo(nullptr);
    }
    return true;
}

bool CAirShip::GetOff(User* user, bool forced) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || !user->IsInAirShip()) {
        return false;
    }
    if (IsDriver(user) && !SetDriver(user, forced)) {
        return false;
    }

    if (forced) {
        FVector position = GetPos();
        position.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
        ));
        user->GetOffAirShip(this, position);
    } else {
        if (!IsLanded()) {
            return false;
        }
        _GetOff(user);
        --m_PassengerCount;
    }
    return true;
}

void CAirShip::BroadCastAirShipInfo() {
    L2_GUARD_FUNCTION();
    const FVector position = GetPos();
    const FVector steeringPosition = m_const->GetSteeringPos();
    const FVector driverPosition = m_const->GetDriverPos();
    const int steeringId = m_Steering == nullptr ? 0 : static_cast<int>(m_Steering->Id());
    const int fuel = GetFuel();
    const int maxFuel = m_const->GetMaxFuel();
    g_ptlUserMap->BroadcastToNeighbor(
        *this, 0x80000, position, "chddddddddddddddddd", 0xfe, 0x61, static_cast<int>(m_nId), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), GetDir().Yaw & 0xffff,
        static_cast<int>(m_data->m_DriverId), m_Speed, static_cast<int>(m_const->m_Speed[2]), steeringId,
        static_cast<int>(steeringPosition.x), static_cast<int>(steeringPosition.y), static_cast<int>(steeringPosition.z),
        static_cast<int>(driverPosition.x), static_cast<int>(driverPosition.y), static_cast<int>(driverPosition.z), fuel,
        maxFuel
    );
    if (m_bMoving && m_Speed > 0) {
        const FVector detail = GetDetailLocation();
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, GetPos(), "chddddddd", 0xfe, 0x66, static_cast<int>(m_nId), static_cast<int>(m_vMovingDest.x),
            static_cast<int>(m_vMovingDest.y), static_cast<int>(m_vMovingDest.z), static_cast<int>(detail.x),
            static_cast<int>(detail.y), static_cast<int>(detail.z)
        );
    }
}

void CAirShip::SendAirShipInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (IsDeleting() || m_Deleted != 0) {
        return;
    }
    if (socket == nullptr) {
        BroadCastAirShipInfo();
        return;
    }

    const FVector position = GetPos();
    const FVector steeringPosition = m_const->GetSteeringPos();
    const FVector driverPosition = m_const->GetDriverPos();
    const int steeringId = m_Steering == nullptr ? 0 : static_cast<int>(m_Steering->Id());
    socket->Send(
        "chddddddddddddddddd", 0xfe, 0x61, static_cast<int>(m_nId), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), GetDir().Yaw & 0xffff,
        static_cast<int>(m_data->m_DriverId), m_Speed, static_cast<int>(m_const->m_Speed[2]), steeringId,
        static_cast<int>(steeringPosition.x), static_cast<int>(steeringPosition.y), static_cast<int>(steeringPosition.z),
        static_cast<int>(driverPosition.x), static_cast<int>(driverPosition.y), static_cast<int>(driverPosition.z),
        GetFuel(), m_const->GetMaxFuel()
    );
    if (m_bMoving && m_Speed > 0) {
        const FVector detail = GetDetailLocation();
        socket->Send(
            "chddddddd", 0xfe, 0x66, static_cast<int>(m_nId), static_cast<int>(m_vMovingDest.x),
            static_cast<int>(m_vMovingDest.y), static_cast<int>(m_vMovingDest.z), static_cast<int>(detail.x),
            static_cast<int>(detail.y), static_cast<int>(detail.z)
        );
    }
}

void CAirShip::SeeDropItem(CItem* item, CCreature* dropper) {
    L2_GUARD_FUNCTION();
    if (item == nullptr || dropper == nullptr) {
        return;
    }

    class ContainerFilter final : public CContainerFilter {
    public:
        bool CanDo(CObject* object) override { return object->IsPC(); }
    } filter;

    class ContainerFunc final : public CContainerFunc {
    public:
        ContainerFunc(CItem* item, CCreature* dropper) : m_item(item), m_dropper(dropper) {}

        void Do(CObject* object) override {
            if (object->IsPC() && (object->IsDeleting() || object->m_Deleted != 0)) {
                return;
            }
            User* const user = object->CastUser();
            m_item->SendItemDropped(user->GetSocket(), m_dropper);
        }

    private:
        CItem* m_item;
        CCreature* m_dropper;
    } function(item, dropper);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

CAirShipReleaseVerifier::CAirShipReleaseVerifier(CAirShip* airShip) : m_pAirShip(airShip) {
    L2_ADD_REF(m_pAirShip, ObjectRefType::VerifierRetain);
    AddTimer(10000, 1);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CAirShipReleaseVerifier::TimerExpired(int) {
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
#else
    try {
#endif
        CAirShip* const airShip = m_pAirShip;
        // Capture diagnostic identity while the verifier still owns its reference.
        const std::uint32_t airShipIdForLog = airShip->m_nId;
        const bool hasAirShipTypeForLog = airShip->m_const != nullptr;
        const std::int32_t airShipTypeForLog = hasAirShipTypeForLog ? airShip->m_const->m_TypeId : 0;
        const std::int32_t remaining = L2_RELEASE(airShip, ObjectRefType::VerifierRetain);
        if (remaining > 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] ORT - AirShip Release failed. "
            //     "refcount[%d] typeid[%d]",
            //     kAirShipSource,
            //     0x2ff,
            //     airShip->RefCount(),
            //     airShip->m_const->m_TypeId)
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] ORT - AirShip Release failed. "
            //     "refcount[%d] typeid[%d]",
            //     kAirShipSource,
            //     0x2ff,
            //     airShip->RefCount(),
            //     airShip->m_const->m_TypeId)
            L2_LOG_ERROR(
                Log,
                "Airship %u: verifier release returned %d remaining references; template ID %d%s. Recording the "
                "outstanding-reference report.",
                static_cast<unsigned>(airShipIdForLog), remaining, airShipTypeForLog,
                hasAirShipTypeForLog ? "" : " (unavailable)"
            );
            airShip->ReportOrt(kAirShipSource, 0x301);
            g_ServerContext.NotReleasedAirShip.fetch_add(1, std::memory_order_seq_cst);
        }
#if defined(_MSC_VER) && defined(_WIN32)
        // The current x64 scope table stores EXCEPTION_EXECUTE_HANDLER directly
        // as filter value 1 for [00460cfd, 00460d8a).
    } __except (1) {
#else
        // POSIX has no SEH. Preserve the recoverable C++-exception boundary.
        // fatal signals remain the platform crash handler's responsibility.
    } catch (...) {
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     "Safe CAirShipReleaseVerifier Saved server... T_T")
        // L2_LOG_ERROR(Log,
        //     "Safe CAirShipReleaseVerifier Saved server... T_T")
        L2_LOG_ERROR(Log, "Exception during airship release verification; cleanup may be incomplete.");
    }
}
