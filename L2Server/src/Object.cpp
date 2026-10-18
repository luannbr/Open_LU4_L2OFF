#include "Object.h"
#include "Alliance.h"
#include "Boss.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CGeneralObject.h"
#include "Creature.h"
#include "GameEventSystem.h"
#include "InstantZone.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "SharedContainer.h"
#include "Shuttle.h"
#include "StaticObject.h"
#include "User.h"
#include "UserSocket.h"
#include "Vehicle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

double ArrayPVPPenalty[121]{};
double ArrayExpPenalty[121]{};
double ArrayPVPHighLevelPenalty[121]{};
double ArrayLevelPenaltyModifier[239]{};

// Current CRT slots 7673/7674 are compiled by the original Object.cpp owner in
// this order: publish the SmartID manager before constructing preCalculate.
CGeneralObjectSmartIdManager g_GeneralObjectManager;

CPreCalculate::CPreCalculate() {
    // Current L2Server 0x008ca1c8, original Object.cpp owner.
    for (int index = 0; index < 121; ++index) {
        ArrayPVPPenalty[index] = index < 5 ? 1.0 : std::pow(1.2, static_cast<double>(index - 5));
        ArrayExpPenalty[index] = std::pow(2.0, static_cast<double>(std::max(index - 5, 0)));
        ArrayPVPHighLevelPenalty[index] = index < 3 ? 1.0 : std::pow(1.2, static_cast<double>(index - 3));
    }
    for (int levelDifference = -119; levelDifference <= 119; ++levelDifference) {
        const int cube = levelDifference * levelDifference * levelDifference;
        const double value = 300.0 / (std::pow(static_cast<double>(std::max(cube, 1)), 1.12) + 270.0);
        ArrayLevelPenaltyModifier[levelDifference + 119] = std::min(value, 1.0);
    }
}

CPreCalculate preCalculate;

namespace {


CInstantZone* ResolveInstantZone(std::uint32_t sid) noexcept {
    // Current CObject::GetInstantZone at 008cb6e8 reaches the guarded
    // CGeneralObject::GetObjectW wrapper at 008cb2dc, instead of a product
    // composition lookup.
    CObject* const candidate = CGeneralObject::GetObjectW(sid);
    if (!candidate || !candidate->IsInZone()) {
        return nullptr;
    }
    return candidate->CastInZone();
}

} // namespace

void GeneralObjectGarbageCollect() {
    // Current L2Server.exe 0x008cba44, physical owner Object.obj.
    g_GeneralObjectManager.GarbageCollect();
}

CGeneralObject::CGeneralObject() {
    L2_GUARD_FUNCTION();
    CGeneralObjectFactory& factory = CGeneralObjectFactory::Inst();
    m_data = factory.Add(this);
}

CGeneralObject::CGeneralObject(const CGeneralObject& other) : CObject(other) {
    L2_GUARD_FUNCTION();
    CGeneralObjectFactory& factory = CGeneralObjectFactory::Inst();
    m_data = factory.Add(this);
    m_data->CopyFrom(other.m_data);
}

CGeneralObject::~CGeneralObject() { CGeneralObjectFactory::Inst().Remove(this); }

L2_DEFINE_OBJECT_CLASS_ID(CGeneralObject)

L2_DEFINE_OBJECT_CLASS_NAME(CGeneralObject, m_pClassName_CGeneralObject)

L2_DEFINE_OBJECT_ALLOCATOR(CGeneralObject, __GetClassName())

L2_DEFINE_OBJECT_NEW(CGeneralObject)

L2_DEFINE_OBJECT_TYPED_RELEASE(CGeneralObject)

int CGeneralObject::GetIndex() const { return m_data->index; }

CObject* CGeneralObject::Copy() { return nullptr; }

void CGeneralObject::OnCreate() {
    L2_GUARD_FUNCTION();
    m_nId = g_GeneralObjectManager.RegisterObject(
        // 14,
        SCLS_GENERAL_OBJECT, static_cast<std::uint32_t>(m_data->index), this
    );
    CGeneralObjectFactory::Inst().PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
}

void CGeneralObject::Delete() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::IncrementI32(&m_nDeleteCount) == 1) {
        SetDelete();
        g_GeneralObjectManager.DeleteObject(m_nId);
        L2_RELEASE(this, ObjectRefType::CreateDelete);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid CGeneralObject::Delete",
    //     L"Object.cpp",
    //     0x29d)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid CGeneralObject::Delete",
    //     L"Object.cpp",
    //     0x29d)
    L2_LOG_ERROR(
        Log,
        "General object %u deletion ignored: incremented delete counter is not 1; reporting the stack without repeating "
        "removal or release.",
        static_cast<unsigned>(m_nId)
    );
    runtime::process::ReportCallStack(2, false);
}

CGeneralObject* CGeneralObject::GetObjectW(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();
    return g_GeneralObjectManager.GetObjectW(smartId);
}


CSmartID::CSmartID(CObject* object) noexcept : sid_(object ? object->m_nId : 0) {}

CSmartID& CSmartID::operator=(CObject* object) noexcept {
    sid_ = object ? object->m_nId : 0;
    return *this;
}

CObject* CSmartID::GetObjectW() const {
    // Current 0x00aeb4d0 has its own dispatch and returns quietly for SID 0.
    // CObject::GetObjectW instead reports SID 0 and also accepts class 15.
    if (sid_ == 0) {
        return nullptr;
    }

    // use PDB-recorded SmartID names. Dispatch is unchanged.
    // const std::uint32_t classGroup = sid_ >> 27U
    const std::uint32_t classGroup = sid_ >> CLASS_SHIFT;
    switch (classGroup) {
        // case 4:
        case SCLS_SHUTTLE:
            return CShuttle::s_ShuttleSmartIdManager.GetObjectW(sid_);
        // case 5:
        case SCLS_FAKE_NPC:
        // case 9:
        case SCLS_CREATURE:
            return g_CreatureSmartIdManager.GetObjectW(sid_);
        // case 6:
        case SCLS_FAKE_ITEM:
        // case 8:
        case SCLS_ITEM:
            return reinterpret_cast<CObject*>(ItemSmartIdManager()->GetObjectW(sid_));
        // case 7:
        case SCLS_AIR_SHIP:
            return reinterpret_cast<CObject*>(g_AirShipSmartIdManager.GetObjectW(sid_));
        // case 10:
        case SCLS_VEHICLE:
            return CVehicle::GetObjectW(sid_);
        // case 11:
        case SCLS_STATIC_OBJECT:
            return CStaticObject::GetObjectW(sid_);
        // case 12:
        case SCLS_PLEDGE:
            return CPledge::GetObjectW(sid_);
        // case 13:
        case SCLS_PARTY:
            return g_PartySmartIdManager.GetObjectW(sid_);
        // case 14:
        case SCLS_GENERAL_OBJECT:
            return CGeneralObject::GetObjectW(sid_);
        default:
            // L2_LOG_ERROR(
            //     Log, L"[%s][%d] What class sid[%x] cls[%d] -_- ", L"..\\Shared\\ThreadSafeLibrary.cpp", 0xb3, sid_,
            //     static_cast<int>(classGroup)
            // )
            L2_LOG_ERROR(
                Log,
                "Smart ID 0x%08x has unsupported class group %u for object lookup; reporting the stack and returning no "
                "object.",
                sid_, classGroup
            );
            runtime::process::ReportCallStack(2, false);
            return nullptr;
    }
}

CWorldObject* CSmartID::GetWorldObject() const {
    if (sid_ == 0) {
        return nullptr;
    }

    // use PDB-recorded SmartID names. Dispatch is unchanged.
    // const std::uint32_t classGroup = sid_ >> 27U
    const std::uint32_t classGroup = sid_ >> CLASS_SHIFT;
    switch (classGroup) {
        // case 4:
        case SCLS_SHUTTLE:
            return CShuttle::s_ShuttleSmartIdManager.GetObjectW(sid_);
        // case 5:
        case SCLS_FAKE_NPC:
        // case 9:
        case SCLS_CREATURE:
            return g_CreatureSmartIdManager.GetObjectW(sid_);
        // case 6:
        case SCLS_FAKE_ITEM:
        // case 8:
        case SCLS_ITEM:
            return reinterpret_cast<CWorldObject*>(ItemSmartIdManager()->GetObjectW(sid_));
        // case 7:
        case SCLS_AIR_SHIP:
            return reinterpret_cast<CWorldObject*>(g_AirShipSmartIdManager.GetObjectW(sid_));
        // case 10:
        case SCLS_VEHICLE:
            return CVehicle::GetObjectW(sid_);
        // case 11:
        case SCLS_STATIC_OBJECT:
            return CStaticObject::GetObjectW(sid_);
        // case 12:
        case SCLS_PLEDGE:
        // case 13:
        case SCLS_PARTY:
        // case 14:
        case SCLS_GENERAL_OBJECT:
            return nullptr;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] What class sid[%x] cls[%d] -_- ",
            //     L"..\\Shared\\ThreadSafeLibrary.cpp",
            //     0x114,
            //     sid_,
            //     static_cast<int>(classGroup))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] What class sid[%x] cls[%d] -_- ",
            //     L"..\\Shared\\ThreadSafeLibrary.cpp",
            //     0x114,
            //     sid_,
            //     static_cast<int>(classGroup))
            L2_LOG_ERROR(
                Log,
                "Smart ID 0x%08x has unsupported class group %u for world-object lookup; reporting the stack and returning "
                "no object.",
                sid_, classGroup
            );
            runtime::process::ReportCallStack(2, false);
            return nullptr;
    }
}

CVehicle* CSmartID::GetVehicle() const {
    // if (sid_ == 0 || (sid_ & 0xf8000000U) != 0x50000000U) {
    if (sid_ == 0 || (sid_ & CLASS_MASK) != (SCLS_VEHICLE << CLASS_SHIFT)) {
        return nullptr;
    }
    return CVehicle::GetObjectW(sid_);
}

CAirShip* CSmartID::GetAirShip() const {
    // if (sid_ == 0 || (sid_ & 0xf8000000U) != 0x38000000U) {
    if (sid_ == 0 || (sid_ & CLASS_MASK) != (SCLS_AIR_SHIP << CLASS_SHIFT)) {
        return nullptr;
    }
    return g_AirShipSmartIdManager.GetObjectW(sid_);
}

CShuttle* CSmartID::GetShuttle() const {
    // if (sid_ == 0 || (sid_ & 0xf8000000U) != 0x20000000U) {
    if (sid_ == 0 || (sid_ & CLASS_MASK) != (SCLS_SHUTTLE << CLASS_SHIFT)) {
        return nullptr;
    }
    return CShuttle::s_ShuttleSmartIdManager.GetObjectW(sid_);
}

std::uint32_t CSmartID::GetId() const {
    CObject* const object = GetObjectW();
    return object ? object->m_nId : 0;
}

CCreature* CSmartID::GetCreature() const {
    if (sid_ == 0) {
        return nullptr;
    }
    // const std::uint32_t classGroup = sid_ >> 27U
    // if (classGroup != 5U && classGroup != 9U) {
    const std::uint32_t classGroup = sid_ >> CLASS_SHIFT;
    if (classGroup != SCLS_FAKE_NPC && classGroup != SCLS_CREATURE) {
        return nullptr;
    }
    return g_CreatureSmartIdManager.GetObjectW(sid_);
}

CNPC* CSmartID::GetNPC() const {
    // Current L2Server 0x00aeb684, original CSmartID owner.
    CCreature* const creature = GetCreature();
    if (creature == nullptr || !creature->IsNPC()) {
        return nullptr;
    }
    return creature->CastNPC();
}

User* CSmartID::GetUser() const {
    CCreature* const creature = GetCreature();
    if (creature == nullptr || !creature->IsPC()) {
        return nullptr;
    }
    return creature->CastUser();
}

CItem* CSmartID::GetItem() const {
    if (sid_ == 0) {
        return nullptr;
    }
    // const std::uint32_t classGroup = sid_ >> 27U
    // if (classGroup != 6U && classGroup != 8U) {
    const std::uint32_t classGroup = sid_ >> CLASS_SHIFT;
    if (classGroup != SCLS_FAKE_ITEM && classGroup != SCLS_ITEM) {
        return nullptr;
    }
    return ItemSmartIdManager()->GetObjectW(sid_);
}

CStaticObject* CSmartID::GetStaticObject() const {
    if (sid_ == 0 ||
        // (sid_ & 0xf8000000U) != 0x58000000U) {
        (sid_ & CLASS_MASK) != (SCLS_STATIC_OBJECT << CLASS_SHIFT)) {
        return nullptr;
    }
    return CStaticObject::GetObjectW(sid_);
}

bool CSmartID::IsValid() const {
    // Current 0x00aeb858: zero is an ordinary miss. Each supported class
    // goes directly to its manager, without CObject::GetObjectW's guard.
    if (sid_ == 0) {
        return false;
    }

    // const std::uint32_t classGroup = sid_ >> 27U
    const std::uint32_t classGroup = sid_ >> CLASS_SHIFT;
    switch (classGroup) {
        // case 4:
        case SCLS_SHUTTLE:
            return CShuttle::s_ShuttleSmartIdManager.GetObjectW(sid_) != nullptr;
        // case 5:
        // case 9:
        case SCLS_FAKE_NPC:
        case SCLS_CREATURE:
            return g_CreatureSmartIdManager.GetObjectW(sid_) != nullptr;
        // case 6:
        // case 8:
        case SCLS_FAKE_ITEM:
        case SCLS_ITEM:
            return ItemSmartIdManager()->GetObjectW(sid_) != nullptr;
        // case 7:
        case SCLS_AIR_SHIP:
            return g_AirShipSmartIdManager.GetObjectW(sid_) != nullptr;
        // case 10:
        case SCLS_VEHICLE:
            return CVehicle::GetObjectW(sid_) != nullptr;
        // case 11:
        case SCLS_STATIC_OBJECT:
            return CStaticObject::GetObjectW(sid_) != nullptr;
        // case 12:
        case SCLS_PLEDGE:
            return CPledge::GetObjectW(sid_) != nullptr;
        // case 13:
        case SCLS_PARTY:
            return g_PartySmartIdManager.GetObjectW(sid_) != nullptr;
        // case 14:
        case SCLS_GENERAL_OBJECT:
            return CGeneralObject::GetObjectW(sid_) != nullptr;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] What class sid[%x] cls[%d] -_- ",
            //     L"..\\Shared\\ThreadSafeLibrary.cpp",
            //     0x134,
            //     sid_,
            //     static_cast<int>(classGroup))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] What class sid[%x] cls[%d] -_- ",
            //     L"..\\Shared\\ThreadSafeLibrary.cpp",
            //     0x134,
            //     sid_,
            //     static_cast<int>(classGroup))
            L2_LOG_ERROR(
                Log,
                "Smart ID 0x%08x has unsupported class group %u for validity checking; reporting the stack and returning "
                "false.",
                sid_, classGroup
            );
            runtime::process::ReportCallStack(2, false);
            return false;
    }
}

std::uint32_t CSmartID::Sid() const noexcept { return sid_; }

CObject::CObject() {
    L2_GUARD_FUNCTION();
    m_nId = static_cast<std::uint32_t>(runtime::raw_atomic::AddI32(&m_nObjectCount, 1));
    m_nClassId = -1;
    m_nDeleteCount = 0;
    m_Deleted = 0;
    m_bDeleting = false;
    nDBID = 0;
}

CObject::CObject(const CObject& other) : ::CIOObject(other) {
    L2_GUARD_FUNCTION();
    m_nId = static_cast<std::uint32_t>(runtime::raw_atomic::AddI32(&m_nObjectCount, 1));
    m_nDeleteCount = 0;
    m_nClassId = other.m_nClassId;
    nDBID = 0;
    m_Deleted = other.m_Deleted;
    m_bDeleting = other.m_bDeleting || other.m_Deleted != 0;
}

CObject::~CObject() = default;

L2_DEFINE_OBJECT_CLASS_ID(CObject)

L2_DEFINE_OBJECT_CLASS_NAME(CObject, m_pClassName_CObject)

L2_DEFINE_OBJECT_ALLOCATOR(CObject, __GetClassName())

L2_DEFINE_OBJECT_NEW(CObject)

void CObject::operator delete(void* allocation, bool) noexcept {
    FreeFailedMemoryClass(allocation);
}

void CObject::operator delete(void* allocation) noexcept {
    FreeFailedMemoryClass(allocation);
}

void CObject::operator delete(CObject*, std::destroying_delete_t) noexcept {
    runtime::process::ReportForbiddenDelete(L"CObject", __WFILE__, __LINE__);
}

// Exact current 007285cc -> 007276ec is diagnostic and returning.
L2_DEFINE_OBJECT_TYPED_RELEASE(CObject)

CSmartID CObject::GetSmartID() { return CSmartID(this); }

void CObject::SetDelete() {
    L2_GUARD_FUNCTION();
    m_Deleted = 1;
}

FVector CObject::GetNormalizedDirVector(int angle) const { return GetNormalizedDirVector(GetDir(), angle); }

FVector CObject::GetNormalizedDirVector(const FAngle& direction, int angle) {
    const std::uint32_t yaw = static_cast<std::uint16_t>(direction.Yaw);
    const std::int32_t angleOffset = runtime::Win64CvttDoubleToInt32((static_cast<double>(angle) / -360.0) * 65535.0);
    const std::uint32_t rotatedYaw = (yaw - static_cast<std::uint32_t>(angleOffset)) & 0xffffU;
    const double radians = static_cast<double>(rotatedYaw) * 3.141592653589793 / 32767.5;
    return FVector{
        std::cos(radians),
        std::sin(radians),
        0.0,
    };
}

int CObject::GetInstantZoneIndex() {
    L2_GUARD_FUNCTION();
    CInstantZone* const zone = GetInstantZone();
    CPrivateContainer* const container = zone ? zone->GetContainer() : nullptr;
    return container ? container->m_nInstantMapIndex : -1;
}

CObject* CObject::GetObjectW(std::uint32_t sid) {
    L2_GUARD_FUNCTION();

    // const std::uint32_t classGroup = sid >> 27
    const std::uint32_t classGroup = sid >> CLASS_SHIFT;
    switch (classGroup) {
        // case 4:
        case SCLS_SHUTTLE:
            // Current 008cb57a calls the shared 1,000-slot lookup body directly
            // with the CShuttle manager storage at 20c68030.
            return reinterpret_cast<CObject*>(CShuttle::s_ShuttleSmartIdManager.GetObjectW(sid));
        // case 5:
        // case 9:
        case SCLS_FAKE_NPC:
        case SCLS_CREATURE:
            // Current 008cb5b9 reaches the one
            // CSmartIdManager<CCreature,120000,16> specialization.
            return reinterpret_cast<CObject*>(g_CreatureSmartIdManager.GetObjectW(sid));
        // case 6:
        // case 8:
        case SCLS_FAKE_ITEM:
        case SCLS_ITEM:
            // Current 008cb5d0/008cb5f5 call the Item manager accessor and then
            // the one CSmartIdManager<CItem,900000,16> lookup body.
            return reinterpret_cast<CObject*>(ItemSmartIdManager()->GetObjectW(sid));
        // case 7:
        case SCLS_AIR_SHIP:
            // Current 008cb5a8 calls that same lookup body directly with the
            // distinct CAirShip manager storage at 01e5aff0.
            return reinterpret_cast<CObject*>(g_AirShipSmartIdManager.GetObjectW(sid));
        // case 10:
        case SCLS_VEHICLE:
            return reinterpret_cast<CObject*>(CVehicle::GetObjectW(sid));
        // case 11:
        case SCLS_STATIC_OBJECT:
            // Current 008cb60f calls class group 11 directly to the one
            // CStaticObject specialization.
            return CStaticObject::GetObjectW(sid);
        // case 12:
        case SCLS_PLEDGE:
            // Current 008cb625 reaches the dedicated
            // CSmartIdManager<CPledge,10002,16> lookup body.
            return CPledge::GetObjectW(sid);
        // case 13:
        case SCLS_PARTY:
            // Current 008cb63b reaches the dedicated
            // CSmartIdManager<CParty,10003,16> specialization.
            return g_PartySmartIdManager.GetObjectW(sid);
        // case 14:
        case SCLS_GENERAL_OBJECT:
            // Current 008cb654 routes class group 14 directly to the one
            // CGeneralObject manager. Unlike the still-unpublished families this
            // specialization no longer needs a portable composition callback.
            return CGeneralObject::GetObjectW(sid);
        // case 15:
        case SCLS_ALLIANCE:
            // Current 008cb66a reaches the dedicated
            // CSmartIdManager<CAlliance,5000,16> specialization.
            return CAlliance::GetObjectW(sid);
        default:
            if (sid != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[Object.cpp][0xda] What class sid[%x] cls[%u] -_- ",
                //     sid,
                //     classGroup)
                // L2_LOG_ERROR(Log,
                //     "[Object.cpp][0xda] What class sid[%x] cls[%u] -_- ",
                //     sid,
                //     classGroup)
                L2_LOG_ERROR(
                    Log,
                    "Object lookup rejected smart ID 0x%08x with unsupported class group %u; reporting the stack and "
                    "returning no object.",
                    sid, classGroup
                );
            }
            runtime::process::ReportCallStack(2, false);
            return nullptr;
    }
}

bool CObject::SetVisible(bool) { return false; }

bool CObject::IsVisible() { return true; }

bool CObject::CanBeSeenBy(CObject&) { return IsVisible(); }

bool CObject::SetTargetable(bool, bool) { return false; }

bool CObject::IsTargetable() { return false; }

bool CObject::SetShowNametag(bool) { return false; }

bool CObject::IsShowNametag() { return false; }

int CObject::GetContainerIndex() { return 0; }

double CObject::GetCollisionRadius() const { return 0.0; }

int CObject::GetClassId() const { return m_nClassId; }

CInstantZone* CObject::GetInstantZone() {
    L2_GUARD_FUNCTION();
    if (IsInRealWorld()) {
        return nullptr;
    }

    CInstantZone* const zone = ResolveInstantZone(GetInZoneID());
    if (zone) {
        return zone;
    }

    if (IsNPC()) {
        SetDelete();
    }
    SetInZoneID(0);
    if (IsPC()) {
        // Current 008cb6e8 calls User vslot +0x8a8, which is the real
        // User::TakeOffItems body at 00b7f1ac, without a second null gate.
        CastUser()->TakeOffItems();
    }
    return nullptr;
}

unsigned int CObject::GetInZoneID() const { return m_nInstantZoneID; }

void CObject::SetInZoneID(unsigned int inZoneId) {
    L2_GUARD_FUNCTION();
    m_nInstantZoneID = inZoneId;
    m_nNextInstantZoneID = 0;

    const CInstantZone* const zone = ResolveInstantZone(inZoneId);
    SetInstantZoneTypeID(zone ? zone->m_nTypeId : 0);
}

bool CObject::IsInRealWorld() const { return m_nInstantZoneID == 0; }

bool CObject::IsInInZone(unsigned int inZoneId) { return m_nInstantZoneID == inZoneId; }

unsigned int CObject::GetNextInZoneID() const { return m_nNextInstantZoneID; }

void CObject::SetNextInZoneID(unsigned int inZoneId) { m_nNextInstantZoneID = inZoneId; }

void CObject::SetInstantZoneTypeID(int instantZoneTypeId) { m_nInstantZoneTypeID = instantZoneTypeId; }

int CObject::Level() const { return 0; }

FAngle CObject::GetDir() const { return FAngle{}; }

#define L2SERVER_COBJECT_FALSE(Method)                                                                                      \
    bool CObject::Method() { return false; }
#define L2SERVER_COBJECT_FALSE_CONST(Method)                                                                                \
    bool CObject::Method() const { return false; }
#define L2SERVER_COBJECT_NULL(Type, Method)                                                                                 \
    Type* CObject::Method() { return nullptr; }

bool CObject::IsObject() { return true; }

L2SERVER_COBJECT_FALSE(IsVehicle)
L2SERVER_COBJECT_FALSE(IsAirShip)
L2SERVER_COBJECT_FALSE(IsShuttle)
L2SERVER_COBJECT_FALSE(IsAbstractCarrier)
L2SERVER_COBJECT_NULL(CVehicle, CastVehicle)
L2SERVER_COBJECT_NULL(CAirShip, CastAirShip)
L2SERVER_COBJECT_NULL(CShuttle, CastShuttle)
L2SERVER_COBJECT_NULL(CAbstractCarrier, CastAbstractCarrier)
L2SERVER_COBJECT_FALSE(IsWorldObject)
L2SERVER_COBJECT_FALSE(IsCreature)
L2SERVER_COBJECT_FALSE_CONST(IsPC)
L2SERVER_COBJECT_FALSE(IsNPC)
L2SERVER_COBJECT_FALSE(IsPet)
L2SERVER_COBJECT_FALSE_CONST(IsSummon)
L2SERVER_COBJECT_FALSE_CONST(IsSummonOrPet)
L2SERVER_COBJECT_FALSE(IsMerchant)
L2SERVER_COBJECT_FALSE(IsBoss)
L2SERVER_COBJECT_FALSE(IsZzoldagu)
L2SERVER_COBJECT_FALSE(IsTrap)
L2SERVER_COBJECT_FALSE(IsWorldTrap)
L2SERVER_COBJECT_FALSE(IsPCTrap)
L2SERVER_COBJECT_FALSE_CONST(IsPCOrSummon)
L2SERVER_COBJECT_FALSE_CONST(IsPCOrPCRelatedNPC)
L2SERVER_COBJECT_FALSE_CONST(IsDoppelganger)
L2SERVER_COBJECT_FALSE_CONST(IsTeleporter)
L2SERVER_COBJECT_FALSE_CONST(IsCollection)
L2SERVER_COBJECT_NULL(CWorldObject, CastWorldObject)

CCreature* CObject::CastCreature() {
    L2_GUARD_FUNCTION();
    return nullptr;
}

L2SERVER_COBJECT_NULL(CSummon, CastSummon)
L2SERVER_COBJECT_NULL(CPet, CastPet)
L2SERVER_COBJECT_NULL(User, CastUser)
L2SERVER_COBJECT_NULL(CNPC, CastNPC)
L2SERVER_COBJECT_NULL(CMerchant, CastMerchant)
L2SERVER_COBJECT_NULL(CBoss, CastBoss)
L2SERVER_COBJECT_NULL(CTrap, CastTrap)
L2SERVER_COBJECT_NULL(CWorldTrap, CastWorldTrap)
L2SERVER_COBJECT_NULL(CPCTrap, CastPCTrap)
L2SERVER_COBJECT_NULL(CDoppelganger, CastDoppelganger)
L2SERVER_COBJECT_NULL(CTeleporter, CastTeleporter)
L2SERVER_COBJECT_NULL(CCollection, CastCollection)
L2SERVER_COBJECT_NULL(User, GetUserOrMaster)
L2SERVER_COBJECT_FALSE_CONST(IsItem)
L2SERVER_COBJECT_FALSE_CONST(IsWeapon)
L2SERVER_COBJECT_FALSE_CONST(IsArmor)
L2SERVER_COBJECT_FALSE_CONST(IsAccessary)
L2SERVER_COBJECT_FALSE_CONST(IsQuestItem)
L2SERVER_COBJECT_FALSE_CONST(IsAsset)
L2SERVER_COBJECT_FALSE_CONST(IsEtcItem)
L2SERVER_COBJECT_FALSE_CONST(IsArtifact)
L2SERVER_COBJECT_NULL(CItem, CastItem)
L2SERVER_COBJECT_FALSE(IsStaticObject)
L2SERVER_COBJECT_FALSE(IsDoor)
L2SERVER_COBJECT_FALSE(IsHolyThing)
L2SERVER_COBJECT_FALSE(IsBattleCamp)
L2SERVER_COBJECT_FALSE(IsSignBoard)
L2SERVER_COBJECT_FALSE(IsObserverRemains)
L2SERVER_COBJECT_FALSE(IsChair)
L2SERVER_COBJECT_FALSE(IsControlTower)
L2SERVER_COBJECT_FALSE(IsEventController)
L2SERVER_COBJECT_FALSE(IsTeamBattleFlag)
L2SERVER_COBJECT_FALSE(IsAirShipPart)
L2SERVER_COBJECT_FALSE(IsResidenceDecoVisible)
L2SERVER_COBJECT_FALSE(IsEventMatch)
L2SERVER_COBJECT_FALSE(IsFortressFlagpole)
L2SERVER_COBJECT_FALSE(IsOwnThing)
L2SERVER_COBJECT_FALSE(IsAirShipSteering)
L2SERVER_COBJECT_FALSE(IsAdvanceBase)
L2SERVER_COBJECT_FALSE(IsContributeData)
L2SERVER_COBJECT_FALSE(IsMPCC)
L2SERVER_COBJECT_FALSE(IsInZone)
L2SERVER_COBJECT_FALSE(IsDuel)
L2SERVER_COBJECT_FALSE(IsStatue)
L2SERVER_COBJECT_NULL(CStaticObject, CastStaticObject)
L2SERVER_COBJECT_NULL(CDoor, CastDoor)
L2SERVER_COBJECT_NULL(CHolyThing, CastHolyThing)
L2SERVER_COBJECT_NULL(CBattleCamp, CastBattleCamp)
L2SERVER_COBJECT_NULL(CSignBoard, CastSignBoard)
L2SERVER_COBJECT_NULL(CObserverRemains, CastObserverRemains)
L2SERVER_COBJECT_NULL(CChair, CastChair)
L2SERVER_COBJECT_NULL(CControlTower, CastControlTower)
L2SERVER_COBJECT_NULL(CEventController, CastEventController)
L2SERVER_COBJECT_NULL(CTeamBattleFlag, CastTeamBattleFlag)
L2SERVER_COBJECT_NULL(CAirShipPart, CastAirShipPart)
L2SERVER_COBJECT_NULL(CResidenceDecoVisible, CastResidenceDecoVisible)
L2SERVER_COBJECT_NULL(CEventMatch, CastEventMatch)
L2SERVER_COBJECT_NULL(CFortressFlagpole, CastFortressFlagpole)
L2SERVER_COBJECT_NULL(CAirShipSteering, CastAirShipSteering)
L2SERVER_COBJECT_NULL(COwnThing, CastOwnThing)
L2SERVER_COBJECT_NULL(CContributeData, CastContributeData)
L2SERVER_COBJECT_NULL(CMultiPartyCommandChannel, CastMPCC)
L2SERVER_COBJECT_NULL(CInstantZone, CastInZone)
L2SERVER_COBJECT_NULL(CDuel, CastDuel)
L2SERVER_COBJECT_NULL(CStatue, CastStatue)
L2SERVER_COBJECT_FALSE(IsInVehicle)
L2SERVER_COBJECT_FALSE(IsInAirShip)
L2SERVER_COBJECT_FALSE(IsInShuttle)

unsigned int CObject::GetMyAirShipID() { return 0; }

bool CObject::IsRealEnemyTo(CCreature*) { return false; }

bool CObject::IsEnemyTo(CCreature*) { return false; }

bool CObject::IsPvpTo(CCreature*) { return false; }

bool CObject::IsMakeAttackerGuilty(CCreature*, bool) { return false; }

bool CObject::IsMakeAttackerChao(CCreature*) { return false; }

L2SERVER_COBJECT_FALSE(IsAlive)
L2SERVER_COBJECT_FALSE(IsMovable)
L2SERVER_COBJECT_FALSE(IsFriend)
L2SERVER_COBJECT_FALSE(IsRidingOnGround)
L2SERVER_COBJECT_FALSE(IsRidingWolf)
L2SERVER_COBJECT_FALSE_CONST(IsRidingWyvern)
L2SERVER_COBJECT_FALSE(IsRidingStrider)
L2SERVER_COBJECT_FALSE(IsRiding)

#undef L2SERVER_COBJECT_FALSE
#undef L2SERVER_COBJECT_FALSE_CONST
#undef L2SERVER_COBJECT_NULL

void CObject::InSight(CObject*, bool) {}

void CObject::OutOfSight(CObject*, bool) {}

FVector CObject::GetMeleeAttackPosition(CCreature*) { return FVector{}; }

const char16_t* CObject::GetName() const {
    L2_GUARD_FUNCTION();
    return u"Object";
}

unsigned int CObject::GetDBID() const { return nDBID; }

void CObject::SetDBID(unsigned int dbId) { nDBID = dbId; }

void CObject::SendDB(const char* format, ...) const { static_cast<void>(format); }

bool CObject::PostLoad() { return true; }

bool L2Common::IsAliveCreature(CObject* object) {
    // Current L2Server 0x008c960c.
    return object != nullptr && object->IsCreature() && object->IsAlive();
}

bool L2Common::IsAliveCreature2(CObject* object) {
    // Current L2Server 0x008c9644. Unlike IsAliveCreature, this helper
    // tests the shared alive value directly and does not add
    // the active-object condition.
    if (object == nullptr || !object->IsCreature()) {
        return false;
    }
    CCreature* const creature = object->CastCreature();
    return creature != nullptr && creature->m_data->m_bIsAlive == 1;
}

bool L2Common::IsAliveNPC(CObject* object) {
    // Current L2Server 0x008c959c.
    return object != nullptr && object->IsNPC() && object->IsAlive();
}

bool L2Common::IsAlivePC(CObject* object) {
    // Current L2Server 0x008c96c4.
    return object != nullptr && object->IsPC() && object->IsAlive();
}

bool L2Common::IsDeadNPC(CObject* object) {
    // Current L2Server 0x008c95d4.
    return object != nullptr && object->IsNPC() && !object->IsAlive();
}

bool L2Common::IsDeadCreature(CObject* object) {
    // Current L2Server 0x008c968c.
    return object != nullptr && object->IsCreature() && !object->IsAlive();
}

namespace {

const std::array<double, 121>& ExpPenaltyTable() {
    // CPreCalculate@008ca1c8 fills current ArrayExpPenalty[0..120] with
    // pow(2.0, max(index - 5, 0)). Keeping the table behind its process
    // lifetime accessor keeps the exact values without exposing an
    // invented public owner for the otherwise anonymous current storage.
    static const std::array<double, 121> table = [] {
        std::array<double, 121> result{};
        double value = 1.0;
        for (std::size_t index = 0; index < result.size(); ++index) {
            if (index > 5) {
                value *= 2.0;
            }
            result[index] = value;
        }
        return result;
    }();
    return table;
}

void ProcessPledgeHuntPoint(User* user, std::int64_t experience, std::int32_t timestamp) {
    if (!g_bUsePledgeBonus || user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t unlockBits = UINT32_C(0) - static_cast<std::uint32_t>(timestamp);
    std::int32_t unlockTime = 0;
    std::memcpy(&unlockTime, &unlockBits, sizeof(unlockTime));
    if (timestamp <= 0 && unlockTime >= now) {
        return;
    }

    const int rate = CPledgeBonusSetting::Inst().GetExpToHuntpointRate();
    pledge->ProcessBonusPoint(user, 1, static_cast<int>(experience / rate), static_cast<int>(pledge->GetPledgeLevel()), now);
}

std::int64_t NonNegativeDifference(std::int64_t value, std::int64_t first, std::int64_t second) {
    const std::int64_t result = runtime::SubtractInt64Wrapping(runtime::SubtractInt64Wrapping(value, first), second);
    return std::max<std::int64_t>(result, 0);
}

std::int64_t NonNegativeDifferenceWithFloatingDeduction(std::int64_t value, std::int64_t first, double second) {
    const std::int64_t integerDifference = runtime::SubtractInt64Wrapping(value, first);
    return std::max<std::int64_t>(runtime::Win64CvttDoubleToInt64(static_cast<double>(integerDifference) - second), 0);
}

void SendExpSpRewardPacket(
    User* user,
    std::int64_t experience,
    double baseExperience,
    std::int64_t pcCafeExpBonus,
    std::int64_t sp,
    double baseSp,
    std::int64_t pcCafeSpBonus,
    double eventRate,
    bool combineFloatingPcCafeDeduction
) {
    const std::int64_t eventBaseExperience = runtime::Win64CvttDoubleToInt64(
        static_cast<double>(runtime::Win64CvttDoubleToInt64(baseExperience)) * eventRate
    );
    const std::int64_t eventBaseSp = runtime::Win64CvttDoubleToInt64(baseSp * eventRate);
    const double floatingEventPcCafeExperience = static_cast<double>(pcCafeExpBonus) * eventRate;
    const double floatingEventPcCafeSp = static_cast<double>(pcCafeSpBonus) * eventRate;
    const std::int64_t eventPcCafeExperience = runtime::Win64CvttDoubleToInt64(floatingEventPcCafeExperience);
    const std::int64_t eventPcCafeSp = runtime::Win64CvttDoubleToInt64(floatingEventPcCafeSp);

    const std::int64_t extraExperience = combineFloatingPcCafeDeduction
        ? NonNegativeDifferenceWithFloatingDeduction(experience, eventBaseExperience, floatingEventPcCafeExperience)
        : NonNegativeDifference(experience, eventBaseExperience, eventPcCafeExperience);
    const std::int64_t extraSp = combineFloatingPcCafeDeduction
        ? NonNegativeDifferenceWithFloatingDeduction(sp, eventBaseSp, floatingEventPcCafeSp)
        : NonNegativeDifference(sp, eventBaseSp, eventPcCafeSp);

    user->GetSocket()->Send(
        "chccQcQcQcQ", 0x62, SystemMessageId::OBTAINED_XP_AND_SP_WITH_BONUSES, 4, 6,
        runtime::SubtractInt64Wrapping(experience, eventPcCafeExperience), 6, extraExperience, 6,
        runtime::SubtractInt64Wrapping(sp, eventPcCafeSp), 6, extraSp
    );
    if (pcCafeExpBonus > 0) {
        user->GetSocket()->Send("chccQ", 0x62, SystemMessageId::BONUS_PC_CAFE_XP_GAINED, 1, 6, eventPcCafeExperience);
    }
}

void SendExpSpRewardPacketWithBaselines(
    User* user,
    std::int64_t experience,
    std::int64_t baseExperience,
    std::int64_t penalizedPcCafeExperience,
    double unpenalizedPcCafeExperience,
    std::int64_t sp,
    std::int64_t baseSp,
    std::int64_t penalizedPcCafeSp,
    double unpenalizedPcCafeSp,
    bool sendPcCafePacket
) {
    user->GetSocket()->Send(
        "chccQcQcQcQ", 0x62, SystemMessageId::OBTAINED_XP_AND_SP_WITH_BONUSES, 4, 6,
        runtime::SubtractInt64Wrapping(experience, penalizedPcCafeExperience), 6,
        NonNegativeDifferenceWithFloatingDeduction(experience, baseExperience, unpenalizedPcCafeExperience), 6,
        runtime::SubtractInt64Wrapping(sp, penalizedPcCafeSp), 6,
        NonNegativeDifferenceWithFloatingDeduction(sp, baseSp, unpenalizedPcCafeSp)
    );
    if (sendPcCafePacket) {
        user->GetSocket()->Send("chccQ", 0x62, SystemMessageId::BONUS_PC_CAFE_XP_GAINED, 1, 6, penalizedPcCafeExperience);
    }
}

} // namespace

CContributeData::CContributeData() :
    m_GreatContributor(0),
    m_PersonalGreatContributor(0),
    m_PersonalGreatContributorLevel(0),
    m_PersonalGreatContribute(0),
    m_nMPCCRouting(0),
    m_dTotalDamage(0.0),
    m_nSubCountributeUserCount(-1) {
    L2_GUARD_FUNCTION();
    CGeneralObject::OnCreate();
}

CContributeData::~CContributeData() = default;

CContributeDataForOne::CContributeDataForOne(User* user) {
    // Current L2Server 0x008ce8fc.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        m_GreatContributor = user->Id();
        m_Contributors.insert(m_GreatContributor);
    }
}

CContributeDataForOne::~CContributeDataForOne() = default;

CContributeDataForEvent::CContributeDataForEvent(User* user) {
    // Current L2Server 0x008cea14, original Object.cpp owner.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        m_GreatContributor = user->GetParty() == nullptr ? user->Id() : user->m_data->m_nPartyId;
        m_Contributors.insert(m_GreatContributor);
    }
}

CContributeDataForEvent::~CContributeDataForEvent() = default;

bool CContributeDataForEvent::PickableIn15Sec(CCreature* creature) {
    // Current L2Server 0x008c92fc, original Object.cpp owner.
    L2_GUARD_FUNCTION();
    if (creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        User* const master = pet->GetMaster();
        if (master != nullptr) {
            creature = master;
        }
    }

    if (creature->Id() == m_GreatContributor) {
        return true;
    }
    if (!creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->m_nPartySid == m_GreatContributor;
}

CContributeDataForUserDie::CContributeDataForUserDie(User* user) : m_nPledgeID(0), padding_10c{} {
    // Current L2Server 0x008ceb18, original Object.cpp owner.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        m_GreatContributor = user->GetParty() == nullptr ? user->Id() : user->m_data->m_nPartyId;
        m_nPledgeID = user->m_nPledgeSid;
        m_Contributors.insert(m_GreatContributor);
    }
}

CContributeDataForUserDie::~CContributeDataForUserDie() = default;

bool CContributeDataForUserDie::PickableIn15Sec(CCreature* creature) {
    // Current L2Server 0x008c9400, original Object.cpp owner.
    L2_GUARD_FUNCTION();
    if (creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        User* const master = pet->GetMaster();
        if (master != nullptr) {
            creature = master;
        }
    }

    if (creature->Id() == m_GreatContributor) {
        return true;
    }
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user->m_nPartySid == m_GreatContributor) {
            return true;
        }
    }
    if (!creature->IsPC() || m_nPledgeID == 0) {
        return false;
    }
    User* const user = creature->CastUser();
    return user->m_nPledgeSid == m_nPledgeID;
}

void CContributeData::TimerExpired(int id [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    L2_GUARD(m_Sect);
    if (m_Deleted == 0) {
        Delete();
    } else {
        runtime::process::ReportCallStack(2, false);
    }
}

bool CContributeData::IsContributeData() { return true; }

CContributeData* CContributeData::CastContributeData() { return this; }

bool CContributeData::PickableIn5Sec(CCreature* creature) {
    L2_GUARD_FUNCTION();

    if (creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        User* const master = pet->GetMaster();
        if (master != nullptr) {
            creature = master;
        }
    }

    if (creature->GetSmartID().Sid() == m_GreatContributor) {
        return true;
    }
    if (creature->IsPC()) {
        if (creature->CastUser()->m_nPartySid == m_GreatContributor) {
            return true;
        }
    }
    if (!creature->IsPC()) {
        return false;
    }
    if (creature->CastUser()->GetMPCC() == nullptr) {
        return false;
    }
    return creature->CastUser()->GetMPCC()->GetSmartID().Sid() == m_GreatContributor;
}

bool CContributeData::PickableIn15Sec(CCreature* creature) {
    L2_GUARD_FUNCTION();

    if (creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        User* const master = pet->GetMaster();
        if (master != nullptr) {
            creature = master;
        }
    }

    const std::uint32_t creatureSid = creature->GetSmartID().Sid();
    const std::uint32_t partySid = creature->IsPC() ? creature->CastUser()->m_nPartySid : 0;

    L2_GUARD(m_Sect);
    return m_Contributors.find(creatureSid) != m_Contributors.end() || m_Contributors.find(partySid) != m_Contributors.end();
}

void CContributeData::SetSubContributeUserCountForBuilder(int count) {
    L2_GUARD_FUNCTION();
    m_nSubCountributeUserCount = std::max(count, 1);
}

void CContributeData::SetMPCCRouting(std::uint32_t partySid) { m_nMPCCRouting = partySid; }

std::uint32_t CContributeData::GetMPCCRouting() { return m_nMPCCRouting; }

int CContributeData::AddUltimateSkillPoint(CCreature* contributor [[maybe_unused]], CCreature* target [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    return 0;
}

void CContributeData::GetBonusItemByLuckyStat(User* user, CCreature* victim, std::int64_t experience) {
    // Current Object.cpp member at 0x008c953c.
    if (user == nullptr || victim == nullptr || !user->ItemBonusByLuckyStat(experience) || !victim->IsNPC()) {
        return;
    }
    CNPC* const npc = victim->CastNPC();
    if (npc != nullptr) {
        npc->LuckyItemToInventory(user);
    }
}

double CContributeData::CalcExpBonus(double value, double multiplier, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return 0.0;
    }
    return (static_cast<double>(creature->m_SkillDurationMod.m_ExpBonusPer) * value * multiplier) / 100.0 +
        (static_cast<double>(creature->m_SkillDurationMod.m_ExpBonusDiff) * value) / 100.0;
}

double CContributeData::CalcSpBonus(double value, double multiplier, CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (!creature) {
        return 0.0;
    }
    return (static_cast<double>(creature->m_SkillDurationMod.m_ExpBonusPer) * value * multiplier) / 100.0 +
        (static_cast<double>(creature->m_SkillDurationMod.m_ExpBonusDiff) * value) / 100.0;
}

double CContributeData::CalcSubJobExpAdjustment(double value, double multiplier, User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return 0.0;
    }
    return (static_cast<double>(user->m_SkillMod.m_SubJobExpAdjustmentPer) * value * multiplier) / 100.0 +
        (static_cast<double>(user->m_SkillMod.m_SubJobExpAdjustmentDiff) * value) / 100.0;
}

double CContributeData::CalcSubJobSpAdjustment(double value, double multiplier, User* user) {
    L2_GUARD_FUNCTION();
    if (!user) {
        return 0.0;
    }
    return (static_cast<double>(user->m_SkillMod.m_SubJobExpAdjustmentPer) * value * multiplier) / 100.0 +
        (static_cast<double>(user->m_SkillMod.m_SubJobExpAdjustmentDiff) * value) / 100.0;
}

bool CContributeData::CheckSubContirbutorForDamages(User* user, double contribution) {
    // Current L2Server 0x008c88e8. Keep the shipped misspelling in the
    // public symbol.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    double thresholdPercent = 0.0;
    switch (user->GetClassCategory()) {
        case CC_TANKER:
            thresholdPercent = 3.0;
            break;
        case CC_HEALER:
            thresholdPercent = 2.0;
            break;
        case CC_ATTACKER:
            thresholdPercent = 5.0;
            break;
        default:
            return false;
    }
    return m_dTotalDamage * thresholdPercent / 100.0 < contribution;
}

bool CContributeData::CheckSubContirbutorForSkills(User* user, int contribution) {
    // Current L2Server 0x008c89fc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    switch (user->GetClassCategory()) {
        case CC_TANKER:
            return contribution >= 50;
        case CC_HEALER:
            return contribution >= 30;
        case CC_ATTACKER:
            return contribution >= 70;
        default:
            return false;
    }
}

void CContributeData::AdjustChinaFatigueSystem(
    CCreature* creature,
    std::int64_t& experience,
    std::int64_t& sp,
    int& raidPoint,
    std::int64_t& petExperience,
    std::int64_t& petSp
) {
    // Current L2Server 0x008c9114.
    L2_GUARD_FUNCTION();
    if (g_nChinaFatigueSystemType <= 1 || creature == nullptr || !creature->IsPCOrSummon()) {
        return;
    }

    User* const user = creature->GetUserOrMaster();
    if (user == nullptr) {
        return;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr || !socket->IsApplyChinaFatigueSystem()) {
        return;
    }

    const int playTime = socket->GetPlayTime_china();
    const int fullThreshold = g_ChinaFullFatigueTime * 3600;
    if (playTime > fullThreshold) {
        experience = 0;
        sp = 0;
        raidPoint = 0;
        petExperience = 0;
        petSp = 0;
        return;
    }

    const int halfThreshold = g_ChinaHalfFatigueTime * 3600;
    if (playTime > halfThreshold) {
        experience /= 2;
        sp /= 2;
        raidPoint /= 2;
        petExperience /= 2;
        petSp /= 2;
    }
}

void CContributeData::AdjustChinaFatigueSystem(CCreature* creature, std::int64_t& experience, std::int64_t& sp) {
    // Current L2Server 0x008ca784.
    L2_GUARD_FUNCTION();
    int ignored = 0;
    std::int64_t ignored64 = 0;
    AdjustChinaFatigueSystem(creature, experience, sp, ignored, ignored64, ignored64);
}

void CContributeData::AdjustChinaFatigueSystem(CCreature* creature, double& first, double& second, double& third) {
    // Current L2Server 0x008ca870. The target writes the first and third
    // references back, but leaves the second double untouched.
    L2_GUARD_FUNCTION();
    std::int64_t firstIntegral = runtime::Win64CvttDoubleToInt64(first);
    std::int64_t secondIntegral = runtime::Win64CvttDoubleToInt64(second);
    int thirdIntegral = runtime::Win64CvttDoubleToInt32(third);
    std::int64_t ignored = 0;
    AdjustChinaFatigueSystem(creature, firstIntegral, secondIntegral, thirdIntegral, ignored, ignored);
    first = static_cast<double>(firstIntegral);
    third = static_cast<double>(thirdIntegral);
}

std::int64_t CContributeData::GetAdjustedExp(
    double value, double multiplier, User* user, CCreature* creature, double flatBonus, ExpPointType type, bool forceSummon
) {
    // Current L2Server 0x008cec2c.
    L2_GUARD_FUNCTION();

    double eventRate = g_dExpSPGetRate;
    std::vector<CEvent*> events;
    CEventManager::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
    for (CEvent* const event : events) {
        if (event != nullptr && event->InEventTime()) {
            eventRate += static_cast<CEvent_EconomyControlEvent*>(event)->GetExpSpGetRate(user->Level()) - 1.0;
            break;
        }
    }

    double skillBonus = 0.0;
    if (type == EXP_TYPE) {
        skillBonus = CalcExpBonus(value, multiplier, user);
        if (!forceSummon) {
            skillBonus += CalcExpBonus(value, multiplier, creature);
        }
    } else if (type == SP_TYPE) {
        skillBonus = CalcSpBonus(value, multiplier, user);
        if (!forceSummon) {
            skillBonus += CalcSpBonus(value, multiplier, creature);
        }
    }

    double subJobAdjustment = 0.0;
    // This target branch is taken only for the force-summon
    // route. The older donor source has the same unusual control flow.
    if (forceSummon && user->m_data->m_nSubJobId != 0) {
        if (type == EXP_TYPE) {
            subJobAdjustment = CalcSubJobExpAdjustment(value, multiplier, user);
        } else if (type == SP_TYPE) {
            subJobAdjustment = CalcSubJobSpAdjustment(value, multiplier, user);
        }
    }

    // Current 0x008cee4b truncates the combined reward before applying
    // the event rate. 0x008cee5e truncates the scaled result again.
    const std::int64_t integralReward =
        runtime::Win64CvttDoubleToInt64(value * multiplier + flatBonus + subJobAdjustment + skillBonus);
    return runtime::Win64CvttDoubleToInt64(static_cast<double>(integralReward) * eventRate);
}

void CContributeData::CalculateExpSpBonus(
    std::int64_t& expBonus,
    std::int64_t& spBonus,
    std::int64_t& pcCafeExpBonus,
    std::int64_t& pcCafeSpBonus,
    double& eventRate,
    double baseExp,
    double baseSp,
    User* user,
    CCreature* creature,
    std::uint32_t forceSummon
) {
    // Current L2Server 0x008ceebc.
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        if (creature != nullptr && (creature->IsSummonOrPet() || forceSummon != 0)) {
            CSummon* const summon = creature->CastSummon();
            User* const master = summon->GetMaster();
            if (master != nullptr) {
                expBonus =
                    runtime::Win64CvttDoubleToInt64(master->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * baseExp);
                spBonus = runtime::Win64CvttDoubleToInt64(master->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * baseSp);

                std::vector<CEvent*> events;
                CEventManager::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
                for (CEvent* const event : events) {
                    if (event != nullptr && event->InEventTime()) {
                        eventRate += static_cast<CEvent_EconomyControlEvent*>(event)->GetExpSpGetRate(master->Level()) - 1.0;
                        break;
                    }
                }
            }
        }
    } else {
        expBonus = runtime::Win64CvttDoubleToInt64(user->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * baseExp);
        spBonus = runtime::Win64CvttDoubleToInt64(user->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * baseSp);

        std::vector<CEvent*> events;
        CEventManager::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
        for (CEvent* const event : events) {
            if (event != nullptr && event->InEventTime()) {
                eventRate += static_cast<CEvent_EconomyControlEvent*>(event)->GetExpSpGetRate(creature->Level()) - 1.0;
                break;
            }
        }
    }

    if (creature != nullptr) {
        expBonus = runtime::AddInt64Wrapping(
            expBonus, runtime::Win64CvttDoubleToInt64(creature->GetUserPremiumExpBonus() * baseExp)
        );
        spBonus = runtime::AddInt64Wrapping(
            spBonus, runtime::Win64CvttDoubleToInt64(creature->GetUserPremiumSpBonus() * baseSp)
        );
    }

    // The current body assumes both pointers are valid at this point.
    if (creature->IsPC() && user->m_data->m_bPCCafeUser) {
        const double pcCafeBonus = creature->GetPCCafeUserExpSPBonus();
        pcCafeExpBonus = runtime::Win64CvttDoubleToInt64(pcCafeBonus * baseExp);
        expBonus = runtime::AddInt64Wrapping(expBonus, pcCafeExpBonus);
        pcCafeSpBonus = runtime::Win64CvttDoubleToInt64(pcCafeBonus * baseSp);
        spBonus = runtime::AddInt64Wrapping(spBonus, pcCafeSpBonus);
    }
}

void CContributeData::DistributeSubExpSp(CCreature* creature) {
    // Current L2Server 0x008cd5e0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    SidContributeMap damageContributes;
    GetContributeInfos(damageContributes);

    std::set<std::uint32_t> eligibleUsers;
    for (const auto& entry : damageContributes) {
        CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(entry.first);
        if (contributor == nullptr) {
            continue;
        }
        User* const user = contributor->CastUser();
        if (CheckSubContirbutorForDamages(user, entry.second)) {
            eligibleUsers.insert(entry.first);
        }
    }

    std::map<std::uint32_t, std::int32_t> skillContributes;
    GetSubContributeInfos(skillContributes);
    for (const auto& entry : skillContributes) {
        CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(entry.first);
        if (contributor == nullptr) {
            continue;
        }
        User* const user = contributor->CastUser();
        if (CheckSubContirbutorForSkills(user, entry.second)) {
            eligibleUsers.insert(entry.first);
        }
    }

    if (eligibleUsers.empty()) {
        return;
    }

    int contributorCount = static_cast<int>(eligibleUsers.size());
    if (m_nSubCountributeUserCount != -1) {
        contributorCount = m_nSubCountributeUserCount;
    }
    const int factorIndex = std::min(contributorCount - 1, 13);
    static constexpr double kPartyFactors[14] = {
        1.0,
        2.0,
        3.0,
        4.0,
        5.0,
        6.0,
        7.0,
        8.0,
        8.7,
        9.4,
        10.1,
        10.8,
        11.4,
        12.0,
    };

    const int victimLevel = creature->m_data->m_nLevel;
    const double factor = kPartyFactors[factorIndex];
    for (const std::uint32_t sid : eligibleUsers) {
        CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(sid);
        if (contributor == nullptr) {
            continue;
        }
        User* const user = contributor->CastUser();
        if (user == nullptr || static_cast<std::uint32_t>(user->m_data->m_nLevel - victimLevel + 5) >= 11U) {
            continue;
        }

        const std::int64_t previousExp = user->m_data->m_nExp;
        const std::int64_t exp = runtime::Win64CvttDoubleToInt64(
            ((((static_cast<double>(victimLevel) * static_cast<double>(victimLevel)) * creature->m_data->m_dAcquireExpRate) /
                 10.5) *
                factor) /
            static_cast<double>(contributorCount)
        );
        const std::int64_t sp = runtime::Win64CvttDoubleToInt64(
            (((creature->m_data->m_dAcquireSP * 3.5) / 10.5) * factor) / static_cast<double>(contributorCount)
        );

        const std::int64_t newExp = user->ExpInc(exp, false);
        const std::int64_t newSp = user->SPInc(sp);
        user->AccumulateExpForItem(exp);
        user->m_nParamChanged |= std::int64_t{PCB_USER};
        user->GetSocket()->Send(
            "chccQcQ", 0x62, SystemMessageId::RAID_CONTRIBUTION_XP_AND_SP_GAINED, 2, 6, newExp - previousExp, 6, newSp
        );
    }
}

void CContributeData::AccumulateContribute(CCreature* contributor, double contribution) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);

    const std::uint32_t sid = contributor->GetSmartID().Sid();
    const auto found = m_Contributes.find(sid);
    if (found == m_Contributes.end()) {
        m_Contributes.insert(std::make_pair(sid, contribution));
    } else {
        contribution += found->second;
        found->second = contribution;
    }

    const std::int32_t integralContribution = runtime::Win64CvttDoubleToInt32(contribution);
    if (m_PersonalGreatContribute < integralContribution) {
        m_PersonalGreatContribute = integralContribution;
        m_PersonalGreatContributor = sid;
        m_PersonalGreatContributorLevel = contributor->m_data->m_nLevel;
    }
}

void CContributeData::AccumulateSubContribute(CCreature* contributor, int contribution) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);

    const std::uint32_t sid = contributor->GetSmartID().Sid();
    const auto found = m_SubContributes.find(sid);
    if (found == m_SubContributes.end()) {
        m_SubContributes.insert(std::make_pair(sid, contribution));
    } else {
        found->second = runtime::AddInt32Wrapping(found->second, contribution);
    }
}

void CContributeData::AccumulateEventContribute(CCreature* contributor, int contribution) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);

    const std::uint32_t sid = contributor->GetSmartID().Sid();
    const double value = static_cast<double>(contribution);
    const auto found = m_EventContributes.find(sid);
    if (found == m_EventContributes.end()) {
        m_EventContributes.insert(std::make_pair(sid, value));
    } else {
        found->second = value + found->second;
    }
}

std::uint32_t CContributeData::GetGreatEventContributorID() {
    L2_GUARD_FUNCTION();

    std::int32_t maximum = -1;
    std::uint32_t contributorSid = 0;
    L2_GUARD(m_Sect);
    while (!m_EventContributes.empty()) {
        for (const auto& contribution : m_EventContributes) {
            if (static_cast<double>(maximum) < contribution.second) {
                maximum = runtime::Win64CvttDoubleToInt32(contribution.second);
                contributorSid = contribution.first;
            }
        }

        CCreature* const object = g_CreatureSmartIdManager.GetObjectW(contributorSid);
        if (object && object->IsPC() && object->CastUser()) {
            break;
        }

        m_EventContributes.erase(contributorSid);
        contributorSid = UINT32_MAX;
        maximum = 0;
    }
    return contributorSid;
}

void CContributeData::Clear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);
    m_Contributors.clear();
}

void CContributeData::SetMPCCMasterPartyRouting() {
    L2_GUARD_FUNCTION();
    if (m_nMPCCRouting == 0) {
        return;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(m_nMPCCRouting);
    if (!object || !object->IsMPCC()) {
        return;
    }

    CMultiPartyCommandChannel* const channel = static_cast<CMultiPartyCommandChannel*>(object);
    CParty* const masterParty = channel->GetMasterParty();
    if (!masterParty) {
        return;
    }

    L2_GUARD(m_Sect);
    m_GreatContributor = masterParty->GetSmartID().Sid();
    m_Contributors.clear();
    m_Contributors.insert(m_GreatContributor);
}

double CContributeData::GetContributeInfos(std::map<std::uint32_t, double>& contributes) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);

    double total = 0.0;
    for (const auto& contribution : m_Contributes) {
        total += contribution.second;
        contributes.insert(contribution);
    }
    m_dTotalDamage = total;
    return total;
}

void CContributeData::GetSubContributeInfos(std::map<std::uint32_t, std::int32_t>& contributes) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Sect);

    for (const auto& contribution : m_SubContributes) {
        contributes.insert(contribution);
    }
}

void CContributeData::ArrangePartyContribute(std::map<CParty*, double>& contributes, CParty* party, double contribution) {
    L2_GUARD_FUNCTION();

    const auto found = contributes.find(party);
    if (found == contributes.end()) {
        contributes.insert(std::make_pair(party, contribution));
    } else {
        contributes[party] = found->second + contribution;
    }
}

void CContributeData::ArrangeMPCCContribute(
    std::map<CMultiPartyCommandChannel*, double>& contributes, CMultiPartyCommandChannel* channel, double contribution
) {
    L2_GUARD_FUNCTION();

    const auto found = contributes.find(channel);
    if (found == contributes.end()) {
        contributes.insert(std::make_pair(channel, contribution));
    } else {
        contributes[channel] = found->second + contribution;
    }
}

namespace {

struct ContributeTargets {
    CCreature* direct = nullptr;
    std::vector<User*> members;
    int divisor = 0;
};

ContributeTargets ResolveContributeTargets(std::uint32_t sid) {
    ContributeTargets targets;

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(sid);
    if (creature != nullptr && (creature->IsPC() || creature->IsSummonOrPet())) {
        targets.direct = creature;
        return targets;
    }

    CParty* const party = g_PartySmartIdManager.GetObjectW(sid);
    if (party != nullptr) {
        party->GetAllMember(targets.members);
        targets.divisor = party->GetMemberCount();
        return targets;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(sid);
    if (object == nullptr || !object->IsMPCC()) {
        return targets;
    }

    CMultiPartyCommandChannel* const channel = object->CastMPCC();
    if (channel != nullptr) {
        channel->GetAllMember(targets.members);
        targets.divisor = channel->GetMemberCount();
    }
    return targets;
}

template <typename TGetter>
double AveragePremiumBonus(const ContributeTargets& targets, TGetter getter) {
    if (targets.direct != nullptr) {
        return getter(targets.direct);
    }
    if (targets.divisor <= 0) {
        return 0.0;
    }

    double total = 0.0;
    for (User* const member : targets.members) {
        if (member != nullptr && member->IsPC()) {
            total += getter(static_cast<CCreature*>(member));
        }
    }
    return total / static_cast<double>(targets.divisor);
}

template <typename TGetter>
int AverageRuneBonus(const ContributeTargets& targets, TGetter getter) {
    if (targets.direct != nullptr) {
        return getter(targets.direct);
    }
    if (targets.divisor <= 0) {
        return 100;
    }

    std::int32_t total = 0;
    for (User* const member : targets.members) {
        const int value = member != nullptr && member->IsPC() ? getter(static_cast<CCreature*>(member)) : 100;
        total = runtime::AddInt32Wrapping(total, value);
    }
    return total / targets.divisor;
}

} // namespace

double CContributeData::GetContributesPremiumDropBonus() {
    // Current L2Server 0x008cdd44 selects the great-contributor owner,
    // then averages the user bonus over a party or command channel.
    const ContributeTargets targets = ResolveContributeTargets(m_GreatContributor);
    return AveragePremiumBonus(targets, [](CCreature* creature) { return creature->GetUserPremiumDropBonus(); });
}

double CContributeData::GetContributesPremiumVitalItemDropBonus() {
    // Current L2Server 0x008cdf98.
    const ContributeTargets targets = ResolveContributeTargets(m_GreatContributor);
    return AveragePremiumBonus(targets, [](CCreature* creature) { return creature->GetUserPremiumVitalItemDropBonus(); });
}

int CContributeData::GetContributesDropRateByRune(int runeType) {
    // Current L2Server 0x008ce1ec. Invalid kept members contribute the
    // neutral 100 value and are still included in the owner's member count.
    const ContributeTargets targets = ResolveContributeTargets(m_GreatContributor);
    return AverageRuneBonus(targets, [runeType](CCreature* creature) { return creature->GetUserDropRateByRune(runeType); });
}

int CContributeData::GetContributesDropAmountByRune(int runeType) {
    // Current L2Server 0x008ce44c.
    const ContributeTargets targets = ResolveContributeTargets(m_GreatContributor);
    return AverageRuneBonus(targets, [runeType](CCreature* creature) {
        return creature->GetUserDropAmountByRune(runeType);
    });
}

double CContributeData::GetExpPenalty(CCreature* creature, int targetLevel) {
    // Current L2Server 0x008c882c.
    if (creature == nullptr) {
        return 1.0;
    }

    const int creatureLevel = creature->Level();
    const int levelDifference = creatureLevel - targetLevel;
    int index = std::max(levelDifference, 0);
    if (index > 120) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"level diff is weird. %d %d",
        //     creatureLevel,
        //     targetLevel)
        // L2_LOG_ERROR(Log,
        //     L"level diff is weird. %d %d",
        //     creatureLevel,
        //     targetLevel)
        L2_LOG_ERROR(
            Log,
            "Experience-penalty level difference exceeds 120 (creature level %d, target level %d); table index clamped to "
            "120 before remaining penalty checks.",
            creatureLevel, targetLevel
        );
        index = 120;
    }
    if (g_bDoNotUseInClassic_ExpPenalty) {
        return 1.0;
    }
    if (creature->IsPC() && levelDifference > 10) {
        return 9223372036854775808.0;
    }
    return ExpPenaltyTable()[static_cast<std::size_t>(index)];
}

void CContributeData::DistributeToOneContributor(
    MPCCContributeMap& mpccContributes,
    PartyContributeMap& partyContributes,
    double& greatestContribution,
    int contributorSid,
    double contribution,
    CCreature* victim,
    double totalContribution,
    double baseExp,
    double baseSp,
    double raidPoint,
    CCreature* killer,
    int damagedSeconds,
    int contributionCount,
    std::uint32_t tick
) {
    // Current L2Server 0x008cf244.
    L2_GUARD_FUNCTION();
    if (victim == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CCreature* pDeadCreature] is nullptr",
        //     kObjectSourceFile,
        //     0xe4b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CCreature* pDeadCreature] is nullptr",
        //     kObjectSourceFile,
        //     0xe4b)
        L2_LOG_ERROR(
            Log,
            "Cannot distribute rewards to contributor smart ID 0x%08x: defeated creature is missing; contributor processing "
            "skipped.",
            static_cast<unsigned>(contributorSid)
        );
        return;
    }

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(contributorSid));
    if (contributor == nullptr) {
        return;
    }

    User* user = nullptr;
    std::uint32_t ownerSid = 0;
    if (contributor->IsPC()) {
        user = contributor->CastUser();
        if (user != nullptr) {
            if (!victim->IsInRange(user, 1500.0)) {
                return;
            }
            CParty* const party = user->GetParty();
            if (party != nullptr) {
                CMultiPartyCommandChannel* const channel = victim->IsBoss() ? user->GetMPCC() : nullptr;
                if (channel != nullptr) {
                    ArrangeMPCCContribute(mpccContributes, channel, contribution);
                } else {
                    ArrangePartyContribute(partyContributes, party, contribution);
                }
                return;
            }
        }
    }
    if (user == nullptr && contributor->IsNPC()) {
        CNPC* const npc =
            contributor->IsSummonOrPet() ? static_cast<CNPC*>(contributor->CastSummon()) : contributor->CastNPC();
        user = npc != nullptr ? npc->GetMaster() : nullptr;
        if (user == nullptr) {
            return;
        }
        ownerSid = user->GetSmartID().Sid();
        if (!victim->IsInRange(user, 1500.0)) {
            return;
        }
        CParty* const party = user->GetParty();
        if (party != nullptr) {
            CMultiPartyCommandChannel* const channel = victim->IsNPC() && victim->IsBoss() ? user->GetMPCC() : nullptr;
            if (channel != nullptr) {
                ArrangeMPCCContribute(mpccContributes, channel, contribution);
            } else {
                ArrangePartyContribute(partyContributes, party, contribution);
            }
            return;
        }
    }

    if (greatestContribution <= contribution && contribution != greatestContribution) {
        greatestContribution = contribution;
        m_GreatContributor = contributor->IsNPC() ? ownerSid : contributor->GetSmartID().Sid();
    }
    {
        L2_GUARD(m_Sect);
        m_Contributors.insert(contributor->IsNPC() ? ownerSid : contributor->GetSmartID().Sid());
    }

    if (totalContribution < 1.0) {
        return;
    }

    const double ratio = contribution / totalContribution;
    double experience = ratio * baseExp;
    double sp = ratio * baseSp;
    double raidReward = ratio * raidPoint;
    if (runtime::Win64CvttDoubleToInt64(experience) < 0 || !victim->IsInRange(contributor, 1500.0)) {
        return;
    }

    const double penalty = victim->IsPledgeBossNPC() ? 1.0 : GetExpPenalty(contributor, victim->Level());
    sp /= penalty;
    raidReward = penalty == 9223372036854775808.0 ? 0.0 : raidReward / penalty;
    experience = experience / penalty * victim->m_data->m_dOverHitBonus;

    if (victim->m_data->m_dOverHitBonus > 1.0 && user != nullptr) {
        CCreature::SendMagicAttckInfo(user, victim, MagicAttackInfo::OverHit);
        user->GetSocket()->SendSystemMessage(SystemMessageId::OVERHIT);
    }
    if (victim->IsBoss() && user != nullptr && (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2)) {
        CBoss* const boss = victim->CastBoss();
        user->WriteNoblessAchievement(WIN_RAID, boss->GetNPCId() - 1000000);
    }
    if (!contributor->IsAlive() || (user != nullptr && user->m_bDominionTransformed)) {
        return;
    }

    AdjustChinaFatigueSystem(contributor, experience, sp, raidReward);

    const double vitalityMultiplier = user != nullptr && !victim->IsBoss() ? user->GetVitalityExpBonus() : 1.0;
    std::int64_t expBonus = 0;
    std::int64_t spBonus = 0;
    std::int64_t pcCafeExpBonus = 0;
    std::int64_t pcCafeSpBonus = 0;
    double eventRate = g_dExpSPGetRate;
    CalculateExpSpBonus(
        expBonus, spBonus, pcCafeExpBonus, pcCafeSpBonus, eventRate, experience, sp, user, contributor, ownerSid
    );

    CPet* const pet = user != nullptr ? user->GetPet() : nullptr;
    CSummon* const summon = user != nullptr ? user->GetOneSummon(User::SUMMON_SINGLE) : nullptr;

    std::int64_t awardedExperience = 0;
    std::int64_t awardedSp = 0;
    std::int64_t petExperience = 0;
    std::int64_t summonExperience = 0;
    int vitalityConsumed = 0;

    if (user != nullptr && (pet != nullptr || summon != nullptr)) {
        double petRatio = 0.0;
        double summonRatio = 0.0;
        if (pet != nullptr) {
            petRatio = g_PetManager.SummonExpRatio(100 - pet->m_data->m_nMasterExpRatio, user->Level(), pet->Level());
        }
        if (summon != nullptr) {
            summonRatio =
                g_PetManager.SummonExpRatio(100 - summon->m_data->m_nMasterExpRatio, user->Level(), summon->Level());
        }
        const double masterRatio = std::max(100.0 - (summonRatio + petRatio), 0.0);

        const double masterBaseExperience = masterRatio * experience / 100.0;
        const double masterBaseSp = masterRatio * sp / 100.0;
        awardedExperience = GetAdjustedExp(
            masterBaseExperience, vitalityMultiplier, user, summon, masterRatio * static_cast<double>(expBonus) / 100.0,
            EXP_TYPE, true
        );
        awardedSp = GetAdjustedExp(
            masterBaseSp, vitalityMultiplier, user, summon, masterRatio * static_cast<double>(spBonus) / 100.0, SP_TYPE, true
        );

        const std::int64_t previousExperience = user->m_data->m_nExp;
        const std::int64_t experienceDelta = user->ExpInc(awardedExperience, false) - previousExperience;
        user->AccumulateExpForItem(awardedExperience);
        GetBonusItemByLuckyStat(user, victim, experienceDelta);
        awardedSp = user->SPInc(awardedSp);
        AddUltimateSkillPoint(user, victim);
        user->SetMPReward(victim, killer);

        if (pet != nullptr && pet->IsAlive()) {
            petExperience = GetAdjustedExp(
                petRatio * experience / 100.0, vitalityMultiplier, user, pet->CastCreature(),
                petRatio * static_cast<double>(expBonus) / 100.0, EXP_TYPE, false
            );
            const std::int64_t petSp = GetAdjustedExp(
                petRatio * sp / 100.0, vitalityMultiplier, user, pet->CastCreature(),
                petRatio * static_cast<double>(spBonus) / 100.0, SP_TYPE, false
            );
            {
                L2_GUARD(*pet->m_lock);
                pet->ExpInc(petExperience, false);
                pet->SPInc(petSp);
            }
            pet->SetMPReward(victim, killer);
        }

        if (summon != nullptr && summon->IsAlive()) {
            summonExperience = GetAdjustedExp(
                summonRatio * experience / 100.0, vitalityMultiplier, user, summon->CastCreature(),
                // Current 0x008cf244 uses the pet ratio for both summon
                // bonus portions. Preserve this shipped asymmetry.
                petRatio * static_cast<double>(expBonus) / 100.0, EXP_TYPE, false
            );
            const std::int64_t summonSp = GetAdjustedExp(
                summonRatio * sp / 100.0, vitalityMultiplier, user, summon->CastCreature(),
                petRatio * static_cast<double>(spBonus) / 100.0, SP_TYPE, false
            );
            {
                L2_GUARD(*summon->m_lock);
                summon->ExpInc(summonExperience, false);
                summon->SPInc(summonSp);
            }
            summon->SetMPReward(victim, killer);
        }

        SendExpSpRewardPacket(
            user, experienceDelta, masterBaseExperience, pcCafeExpBonus, awardedSp, masterBaseSp, pcCafeSpBonus, eventRate,
            false
        );
        if (!user->m_bVotePeaceZone) {
            user->CheckVoteSystemGetExp();
        }
        ProcessPledgeHuntPoint(user, experienceDelta, user->tPledgeBonusTimeStamp[PledgeBonus::HUNT_REWARD_RECEIVE]);

        if (victim->IsBoss() && raidPoint > 0.0) {
            user->AddBossRelatedBonus(*victim, runtime::Win64CvttDoubleToInt32(std::max(eventRate * raidReward, 1.0)));
        }
        if (awardedExperience > 0 || petExperience > 0 || summonExperience > 0) {
            const int before = user->CastUser()->GetVitalPoint();
            user->VitalityConsume(victim, 1, 1.0);
            vitalityConsumed = before - user->CastUser()->GetVitalPoint();
        }
    } else {
        User* rewardUser = contributor->GetUserOrMaster();
        if (ownerSid != 0) {
            CCreature* const owner = g_CreatureSmartIdManager.GetObjectW(ownerSid);
            rewardUser = owner != nullptr && owner->IsPC() ? owner->CastUser() : nullptr;
        }

        if (rewardUser == nullptr) {
            const std::int64_t integralVitalityExperience =
                runtime::Win64CvttDoubleToInt64(vitalityMultiplier * experience);
            awardedExperience =
                runtime::Win64CvttDoubleToInt64(static_cast<double>(integralVitalityExperience) * eventRate);
            awardedSp = runtime::Win64CvttDoubleToInt64(vitalityMultiplier * sp * eventRate);
        } else {
            awardedExperience = GetAdjustedExp(
                experience, vitalityMultiplier, rewardUser, nullptr, static_cast<double>(expBonus), EXP_TYPE, true
            );
            awardedSp =
                GetAdjustedExp(sp, vitalityMultiplier, rewardUser, nullptr, static_cast<double>(spBonus), SP_TYPE, true);
        }

        const int raidPointAward = runtime::Win64CvttDoubleToInt32(std::max(eventRate * raidReward, 1.0));
        std::int64_t experienceDelta = awardedExperience;
        if (contributor->IsPCOrSummon()) {
            const std::int64_t previousExperience = contributor->m_data->m_nExp;
            experienceDelta = contributor->ExpInc(awardedExperience, false) - previousExperience;
            contributor->SetMPReward(victim, killer);
            if (contributor->IsPC()) {
                User* const contributorUser = contributor->CastUser();
                if (contributorUser != nullptr) {
                    contributorUser->AccumulateExpForItem(awardedExperience);
                }
            }
        }
        awardedSp = contributor->SPInc(awardedSp);
        AddUltimateSkillPoint(contributor, victim);

        if (user != nullptr) {
            GetBonusItemByLuckyStat(user, victim, experienceDelta);
            SendExpSpRewardPacket(
                user, experienceDelta, experience, pcCafeExpBonus, awardedSp, sp, pcCafeSpBonus, eventRate, true
            );
            if (!user->m_bVotePeaceZone) {
                user->CheckVoteSystemGetExp();
            }
            ProcessPledgeHuntPoint(user, experienceDelta, user->tPledgeBonusTimeStamp[PledgeBonus::HUNT_REWARD_RECEIVE]);
        }

        if (rewardUser != nullptr && victim->IsBoss() && raidPoint > 0.0) {
            rewardUser->AddBossRelatedBonus(*victim, raidPointAward);
        }
        if (rewardUser != nullptr && awardedExperience > 0) {
            const int before = rewardUser->CastUser()->GetVitalPoint();
            rewardUser->VitalityConsume(victim, 1, 1.0);
            vitalityConsumed = before - rewardUser->CastUser()->GetVitalPoint();
        }
    }

    if (contributor->CanSoulCollectByExp(awardedExperience)) {
        contributor->IncreaseSoulCount(1, static_cast<int>(victim->GetSmartID().Sid()));
    }
    if (user == nullptr && contributor->IsPet()) {
        CPet* const contributorPet = contributor->CastPet();
        if (contributorPet != nullptr) {
            contributorPet->SendSummonInfo(true, false);
        }
    } else if (user != nullptr) {
        user->m_nParamChanged |= PCB_USER;
    }
    if (pet != nullptr) {
        pet->SendSummonInfo(true, false);
    }

    if (victim->IsNPC() && user != nullptr) {
        CLogFormatter formatter;
        formatter.WriteLog2(
            0x458, user, victim, awardedExperience, user->m_data->m_nExp, victim->m_data->m_nLevel, damagedSeconds,
            contributionCount, tick, 0
        );
        formatter.WriteLog2(0x465, user, victim, awardedSp, user->m_data->m_nSP, 0, 0, 0, 0, 0);
        formatter.WriteLog2(0x466, user, victim, vitalityConsumed, user->CastUser()->GetVitalPoint(), 0, 0, 0, 0, 0);
    }
    if (user != nullptr) {
        user->IncrementMonsterBookKillCount(victim);
        if (victim->IsNPC()) {
            if (awardedExperience > 0) {
                user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING_ANY, 0, 1);
            }
            if (static_cast<std::uint32_t>(user->m_data->m_nLevel - victim->m_data->m_nLevel + 10) < 21U) {
                if (user->m_data->m_nWorldId == g_nWorldId) {
                    CNPC* const npc = victim->CastNPC();
                    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING, npc->GetNPCId() - 1000000, 1);
                } else {
                    CNPC* const npc = victim->CastNPC();
                    user->PledgeMissionCheckAndAddCountInRaid(PledgeMission::PMGT_W_HUNTING, npc->GetNPCId() - 1000000, 1);
                }
            }
        }
    }
}

void CContributeData::DistributeMPCC(
    CCreature* victim,
    MPCCContributeMap& mpccContributes,
    SidContributeMap& directContributes,
    double& greatestContribution,
    double totalContribution,
    double baseExp,
    double baseSp,
    double raidPoint,
    std::uint32_t tick,
    int damagedSeconds,
    int contributionCount
) {
    // Current L2Server 0x008d0724.
    L2_GUARD_FUNCTION();

    int killerMasterSid = 0;
    int totalMemberCount = 0;
    for (const auto& mpccContribution : mpccContributes) {
        CMultiPartyCommandChannel* const channel = mpccContribution.first;
        if (channel == nullptr) {
            continue;
        }

        std::vector<User*> members;
        channel->GetAllMemberWhoCanGetExpSp(members, victim);
        const int maximumLevel = channel->GetMaxLevelOfAllMember();
        const int memberCount = static_cast<int>(members.size());
        if (memberCount < 1) {
            continue;
        }

        totalMemberCount = runtime::AddInt32Wrapping(totalMemberCount, memberCount);
        {
            L2_GUARD(m_Sect);
            for (User* const member : members) {
                if (member != nullptr) {
                    m_Contributors.insert(member->GetSmartID().Sid());
                }
            }
        }

        const double channelContribution = mpccContribution.second;
        if (greatestContribution <= channelContribution && channelContribution != greatestContribution) {
            greatestContribution = channelContribution;
            m_GreatContributor = channel->GetSmartID().Sid();
            User* const master = channel->GetMaster();
            if (master != nullptr) {
                killerMasterSid = static_cast<int>(master->GetSmartID().Sid());
            }
        }

        if (totalContribution < 1.0) {
            continue;
        }
        const double contributionRatio = channelContribution / totalContribution;
        const double channelExp = contributionRatio * baseExp;
        const double channelSp = contributionRatio * baseSp;
        if (runtime::Win64CvttDoubleToInt64(channelExp) < 0) {
            continue;
        }

        double totalLevelWeight = 0.0;
        std::vector<std::pair<User*, double>> eligibleMembers;
        for (User* const member : members) {
            if (member == nullptr) {
                continue;
            }
            const int level = member->Level();
            const double levelWeight = static_cast<double>(level) * static_cast<double>(level);
            totalLevelWeight += levelWeight;
            if (member->IsAlive() && victim->IsInRange(member, 2800.0) &&
                maximumLevel - member->Level() < g_MAX_LEVELDIFF_MPCC_GIVEN_EXP) {
                eligibleMembers.push_back(std::make_pair(member, levelWeight));
            }
        }

        const std::size_t partySize = members.size();
        double partyMultiplier = g_dPartyExpSPGetRate * (partySize < 8 ? g_PartyBonus[partySize] : 2.0);
        if (g_nPartyLevelDiffExpPenaltyIndex > 0 && eligibleMembers.size() == 1) {
            partyMultiplier = 1.0;
        }

        for (const auto& eligible : eligibleMembers) {
            User* const user = eligible.first;
            if (user == nullptr) {
                continue;
            }

            double memberRatio = eligible.second * partyMultiplier / totalLevelWeight;
            if (g_nPartyLevelDiffExpPenaltyIndex > 0) {
                memberRatio = partyMultiplier / static_cast<double>(memberCount);
            }

            const std::int64_t originalUserExp = runtime::Win64CvttDoubleToInt64(memberRatio * channelExp);
            const std::int64_t originalUserSp = runtime::Win64CvttDoubleToInt64(memberRatio * channelSp);
            const int originalRaidPoint = runtime::Win64CvttDoubleToInt32(memberRatio * contributionRatio * raidPoint);

            if (victim->IsBoss() && (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2) &&
                !user->m_bIsObserving) {
                CBoss* const boss = victim->CastBoss();
                user->WriteNoblessAchievement(WIN_RAID, boss->GetNPCId() - 1000000);
            }
            if (user->m_data->m_bIsAlive == 0 || user->m_bIsObserving) {
                continue;
            }

            CPet* const pet = user->GetPet();
            CSummon* const singleSummon = user->GetOneSummon(User::SUMMON_SINGLE);
            double eventRate = g_dExpSPGetRate;
            {
                std::vector<CEvent*> events;
                CEventManager::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
                for (CEvent* const event : events) {
                    if (event != nullptr && event->InEventTime()) {
                        eventRate += static_cast<CEvent_EconomyControlEvent*>(event)->GetExpSpGetRate(user->Level()) - 1.0;
                        break;
                    }
                }
            }
            const double vitalityMultiplier = user->GetVitalityExpBonus();

            std::int64_t remainingUserExp = originalUserExp;
            std::int64_t remainingUserSp = originalUserSp;
            std::int64_t petExperience = 0;
            std::int64_t summonExperience = 0;
            const std::array<CSummon*, 2> summons = {
                pet != nullptr ? pet->CastSummon() : nullptr,
                singleSummon,
            };
            for (std::size_t summonIndex = 0; summonIndex < summons.size(); ++summonIndex) {
                CSummon* const summon = summons[summonIndex];
                if (summon == nullptr || !summon->IsAlive() || !user->IsInRange(summon, 1500.0)) {
                    continue;
                }

                int directContribution = 0;
                const auto direct = directContributes.find(summon->GetSmartID().Sid());
                if (direct != directContributes.end()) {
                    directContribution = runtime::Win64CvttDoubleToInt32(direct->second);
                }

                double masterRatio = 0.0;
                if (summon->m_data->m_nMasterExpRatio < 0) {
                    masterRatio = 1.0 - std::min(static_cast<double>(directContribution) / channelContribution, 1.0);
                } else {
                    masterRatio = g_PetManager.MasterExpRatio(
                                      100 - summon->m_data->m_nMasterExpRatio, user->Level(), summon->Level()
                                  ) /
                        100.0;
                }

                const double summonRatio = 1.0 - masterRatio;
                const std::int64_t rawSummonExp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(originalUserExp) * summonRatio);
                const std::int64_t rawSummonSp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(originalUserSp) * summonRatio);
                remainingUserExp = runtime::SubtractInt64Wrapping(remainingUserExp, rawSummonExp);
                remainingUserSp = runtime::SubtractInt64Wrapping(remainingUserSp, rawSummonSp);

                const double summonPenalty = GetExpPenalty(summon, victim->Level());
                std::int64_t summonBaseExp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(rawSummonExp) / summonPenalty);
                std::int64_t summonBaseSp = static_cast<std::int64_t>(
                    runtime::Win64CvttDoubleToInt32(static_cast<double>(rawSummonSp) / summonPenalty)
                );

                User* const summonMaster = summon->GetMaster();
                std::int64_t summonExpBonus = 0;
                std::int64_t summonSpBonus = 0;
                if (summonMaster != nullptr) {
                    summonExpBonus = runtime::AddInt64Wrapping(
                        runtime::Win64CvttDoubleToInt64(
                            summonMaster->GetUserPremiumExpBonus() * static_cast<double>(summonBaseExp)
                        ),
                        runtime::Win64CvttDoubleToInt64(
                            summonMaster->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * static_cast<double>(summonBaseExp)
                        )
                    );
                    summonSpBonus = runtime::AddInt64Wrapping(
                        runtime::Win64CvttDoubleToInt64(
                            summonMaster->GetUserPremiumSpBonus() * static_cast<double>(summonBaseSp)
                        ),
                        runtime::Win64CvttDoubleToInt64(
                            summonMaster->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * static_cast<double>(summonBaseSp)
                        )
                    );
                }

                if (!victim->IsBoss()) {
                    summonBaseExp =
                        runtime::Win64CvttDoubleToInt64(static_cast<double>(summonBaseExp) * vitalityMultiplier);
                    summonBaseSp =
                        runtime::Win64CvttDoubleToInt64(static_cast<double>(summonBaseSp) * vitalityMultiplier);
                }

                const std::int64_t summonExpInput = runtime::Win64CvttDoubleToInt64(
                    static_cast<double>(runtime::AddInt64Wrapping(summonBaseExp, summonExpBonus)) * eventRate
                );
                const std::int64_t summonSpInput = runtime::Win64CvttDoubleToInt64(
                    static_cast<double>(runtime::AddInt64Wrapping(summonBaseSp, summonSpBonus)) * eventRate
                );
                std::int64_t awardedSummonExp = GetAdjustedExp(
                    static_cast<double>(summonExpInput), vitalityMultiplier, user, summon,
                    static_cast<double>(summonExpBonus), EXP_TYPE, false
                );
                std::int64_t awardedSummonSp = GetAdjustedExp(
                    static_cast<double>(summonSpInput), vitalityMultiplier, user, summon, static_cast<double>(summonSpBonus),
                    SP_TYPE, false
                );
                AdjustChinaFatigueSystem(summonMaster, awardedSummonExp, awardedSummonSp);

                {
                    L2_GUARD(*summon->m_lock);
                    summon->ExpInc(awardedSummonExp, false);
                    summon->SPInc(awardedSummonSp);
                }
                summon->SetMPReward(victim, summon);
                summon->SendSummonInfo(true, false);
                if (summonIndex == 0) {
                    petExperience = awardedSummonExp;
                } else {
                    summonExperience = awardedSummonExp;
                }
            }

            const double userPenalty = GetExpPenalty(user, victim->Level());
            std::int64_t userBaseExp =
                runtime::Win64CvttDoubleToInt64(static_cast<double>(remainingUserExp) / userPenalty);
            std::int64_t userBaseSp = runtime::Win64CvttDoubleToInt64(static_cast<double>(remainingUserSp) / userPenalty);
            double adjustedRaidPoint =
                userPenalty == 9223372036854775808.0 ? 0.0 : static_cast<double>(originalRaidPoint) / userPenalty;

            std::int64_t userExpBonus = 0;
            std::int64_t userSpBonus = 0;
            std::int64_t pcCafeExpBonus = 0;
            std::int64_t pcCafeSpBonus = 0;
            if (user->IsPC()) {
                userExpBonus = runtime::AddInt64Wrapping(
                    runtime::Win64CvttDoubleToInt64(user->GetUserPremiumExpBonus() * static_cast<double>(userBaseExp)),
                    runtime::Win64CvttDoubleToInt64(
                        user->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * static_cast<double>(userBaseExp)
                    )
                );
                userSpBonus = runtime::AddInt64Wrapping(
                    runtime::Win64CvttDoubleToInt64(user->GetUserPremiumSpBonus() * static_cast<double>(userBaseSp)),
                    runtime::Win64CvttDoubleToInt64(
                        user->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * static_cast<double>(userBaseSp)
                    )
                );
                if (user->m_data->m_bPCCafeUser) {
                    pcCafeExpBonus = runtime::Win64CvttDoubleToInt64(
                        user->GetPCCafeUserExpSPBonus() * static_cast<double>(userBaseExp)
                    );
                    pcCafeSpBonus = runtime::Win64CvttDoubleToInt64(
                        user->GetPCCafeUserExpSPBonus() * static_cast<double>(userBaseSp)
                    );
                    userExpBonus = runtime::AddInt64Wrapping(userExpBonus, pcCafeExpBonus);
                    userSpBonus = runtime::AddInt64Wrapping(userSpBonus, pcCafeSpBonus);
                }
                user->IncrementMonsterBookKillCount(victim);
            }

            std::int64_t packetBaseExp = runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseExp) * eventRate);
            std::int64_t packetBaseSp = runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseSp) * eventRate);
            if (!victim->IsBoss()) {
                userBaseExp = runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseExp) * vitalityMultiplier);
                userBaseSp = runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseSp) * vitalityMultiplier);
            }

            std::int64_t awardedExperience = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(runtime::AddInt64Wrapping(userBaseExp, userExpBonus)) * eventRate
            );
            std::int64_t awardedSp = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(runtime::AddInt64Wrapping(userBaseSp, userSpBonus)) * eventRate
            );
            int awardedRaidPoint = runtime::Win64CvttDoubleToInt32(std::max(adjustedRaidPoint * eventRate, 1.0));
            AdjustChinaFatigueSystem(user, awardedExperience, awardedSp, awardedRaidPoint, packetBaseExp, packetBaseSp);

            user->ExpInc(awardedExperience, false);
            user->SPInc(awardedSp);
            AddUltimateSkillPoint(user, victim);
            user->SetMPReward(victim, user);
            user->m_nParamChanged |= PCB_USER;
            user->AccumulateExpForItem(awardedExperience);

            const double floatingEventPcCafeExp = static_cast<double>(pcCafeExpBonus) * eventRate;
            const double floatingEventPcCafeSp = static_cast<double>(pcCafeSpBonus) * eventRate;
            const std::int64_t eventPcCafeExp = runtime::Win64CvttDoubleToInt64(floatingEventPcCafeExp);
            const std::int64_t eventPcCafeSp = runtime::Win64CvttDoubleToInt64(floatingEventPcCafeSp);
            SendExpSpRewardPacketWithBaselines(
                user, awardedExperience, packetBaseExp, eventPcCafeExp, floatingEventPcCafeExp, awardedSp, packetBaseSp,
                eventPcCafeSp, floatingEventPcCafeSp, pcCafeExpBonus > 0
            );
            if (!user->m_bVotePeaceZone) {
                user->CheckVoteSystemGetExp();
            }
            ProcessPledgeHuntPoint(user, awardedExperience, user->tPledgeBonusTimeStamp[PledgeBonus::HUNT_REWARD_RECEIVE]);

            if (victim->IsBoss() && raidPoint > 0.0) {
                user->AddBossPoint(victim->m_data->m_nClassID, awardedRaidPoint);
            }

            int vitalityConsumed = 0;
            if (awardedExperience > 0 || petExperience > 0 || summonExperience > 0) {
                const int before = user->CastUser()->GetVitalPoint();
                user->VitalityConsume(victim, memberCount, 1.0);
                vitalityConsumed = before - user->CastUser()->GetVitalPoint();
            }

            CLogFormatter formatter;
            formatter.WriteLog2(
                0x458, user, victim, awardedExperience, user->m_data->m_nExp, victim->m_data->m_nLevel, damagedSeconds,
                contributionCount, tick, -1
            );
            formatter.WriteLog2(0x465, user, victim, awardedSp, user->m_data->m_nSP, 0, 0, 0, 0, 0);
            formatter.WriteLog2(0x466, user, victim, vitalityConsumed, user->CastUser()->GetVitalPoint(), 0, 0, 0, 0, 0);
            if (victim->IsNPC()) {
                if (awardedExperience > 0) {
                    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING_ANY, 0, 1);
                }
                if (static_cast<std::uint32_t>(user->m_data->m_nLevel - victim->m_data->m_nLevel + 10) < 21U) {
                    if (user->m_data->m_nWorldId == g_nWorldId) {
                        CNPC* const npc = victim->CastNPC();
                        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING, npc->GetNPCId() - 1000000, 1);
                    } else {
                        CNPC* const npc = victim->CastNPC();
                        user->PledgeMissionCheckAndAddCountInRaid(
                            PledgeMission::PMGT_W_HUNTING, npc->GetNPCId() - 1000000, 1
                        );
                    }
                }
            }
        }
    }

    if (victim->IsBoss() && killerMasterSid != 0) {
        victim->CastNPC()->m_sidKillerMaster = static_cast<std::uint32_t>(killerMasterSid);
        victim->CastNPC()->m_nKillerMemberCount = totalMemberCount;

        if (g_bSetMPCCRoutingOfContribute) {
            const std::uint32_t previousRouting = victim->CastNPC()->m_pContribute->GetMPCCRouting();
            if (previousRouting != m_GreatContributor) {
                CMultiPartyCommandChannel* const previous =
                    static_cast<CMultiPartyCommandChannel*>(CGeneralObject::GetObjectW(previousRouting));
                if (previous != nullptr) {
                    previous->ResetMasterPartyRouting();
                }
                CMultiPartyCommandChannel* const current =
                    static_cast<CMultiPartyCommandChannel*>(CGeneralObject::GetObjectW(m_GreatContributor));
                if (current != nullptr) {
                    current->SetMasterPartyRouting(victim->CastNPC());
                }
            }
        }
    }
}

void CContributeData::Distribute(CCreature* victim, CCreature* killer) {
    // Current L2Server 0x008d1988.
    L2_GUARD_FUNCTION();
    if (victim == nullptr) {
        return;
    }

    if (victim->IsPC()) {
        User* const victimUser = victim->CastUser();
        if (victimUser == nullptr) {
            return;
        }

        {
            L2_GUARD(m_Sect);
            CParty* const party = victimUser->GetParty();
            m_GreatContributor = party != nullptr ? party->GetSmartID().Sid() : victimUser->GetSmartID().Sid();
            m_Contributors.insert(m_GreatContributor);
        }

        if (killer != nullptr && killer->IsPC()) {
            User* const killerUser = killer->CastUser();
            if (killerUser != nullptr) {
                AddUltimateSkillPoint(killerUser, victimUser);
            }
        }
        return;
    }

    if (victim->IsNPC()) {
        User* master = nullptr;
        if (victim->IsSummonOrPet()) {
            CSummon* const summon = victim->CastSummon();
            if (summon == nullptr) {
                return;
            }
            master = summon->GetMaster();
            if (master == nullptr) {
                return;
            }
        } else {
            CNPC* const npc = victim->CastNPC();
            master = npc != nullptr ? npc->GetMaster() : nullptr;
        }

        if (master != nullptr) {
            L2_GUARD(m_Sect);
            m_GreatContributor = master->GetSmartID().Sid();
            m_Contributors.insert(m_GreatContributor);
            return;
        }
    }

    const int victimLevel = victim->m_data->m_nLevel;
    const double level = static_cast<double>(victimLevel);
    const double baseExp =
        static_cast<double>(runtime::MultiplyInt32Wrapping(victimLevel, victimLevel)) * victim->m_data->m_dAcquireExpRate;
    double baseSp = victim->m_data->m_dAcquireSP * level * level;
    if (victim->m_data->m_dAcquireSP > 0.0 && baseSp >= 0.0 && baseSp < 1.0) {
        baseSp = 1.0;
    }
    const double raidPoint = static_cast<double>(victim->m_data->m_nAcquireRaidBossPoint);

    SidContributeMap directContributes;
    const double totalContribution = GetContributeInfos(directContributes);
    PartyContributeMap partyContributes;
    MPCCContributeMap mpccContributes;
    const std::uint32_t tick = runtime::GetTickCount();
    const int damagedSeconds = victim->m_dwDamagedTick == 0 ? 0 : static_cast<int>((tick - victim->m_dwDamagedTick) / 1000U);
    const int contributionCount = static_cast<int>(directContributes.size());

    double greatestContribution = 0.0;
    for (const auto& directContribution : directContributes) {
        DistributeToOneContributor(
            mpccContributes, partyContributes, greatestContribution, static_cast<int>(directContribution.first),
            directContribution.second, victim, totalContribution, baseExp, baseSp, raidPoint, killer, damagedSeconds,
            contributionCount, tick
        );
    }

    std::uint32_t killerMasterSid = 0;
    int killerMemberCount = 0;
    if (victim->IsBoss() && killer != nullptr && killer->IsPC()) {
        killerMasterSid = killer->GetSmartID().Sid();
        killerMemberCount = 1;
    }

    for (const auto& partyContribution : partyContributes) {
        CParty* const party = partyContribution.first;
        if (party == nullptr) {
            continue;
        }

        std::vector<User*> members;
        party->GetAllMemberWhoCanGetExpSp(members, victim);
        const int memberCount = static_cast<int>(members.size());
        if (memberCount < 1) {
            continue;
        }

        const double contribution = partyContribution.second;
        if (greatestContribution <= contribution && contribution != greatestContribution) {
            greatestContribution = contribution;
            m_GreatContributor = party->GetSmartID().Sid();
            User* const partyMaster = party->GetMaster();
            if (partyMaster != nullptr) {
                killerMasterSid = partyMaster->GetSmartID().Sid();
                killerMemberCount = party->GetMemberCount();
            }
        }

        {
            L2_GUARD(m_Sect);
            for (User* const member : members) {
                if (member != nullptr) {
                    m_Contributors.insert(member->GetSmartID().Sid());
                }
            }
        }

        if (totalContribution < 1.0) {
            continue;
        }
        const double contributionRatio = contribution / totalContribution;
        const std::int64_t partyExpTarget = runtime::Win64CvttDoubleToInt64(contributionRatio * baseExp);
        if (partyExpTarget < 0) {
            continue;
        }

        int maximumLevel = 0;
        bool dominionTransformed = false;
        for (User* const member : members) {
            if (member == nullptr) {
                continue;
            }
            maximumLevel = std::max(maximumLevel, member->Level());
            if (member->IsDominionTransformed()) {
                dominionTransformed = true;
            }
        }
        if (dominionTransformed) {
            continue;
        }

        std::array<double, 7> levelWeights{};
        double totalLevelWeight = 0.0;
        std::vector<unsigned int> eligibleIndices;
        for (int index = 0; index < memberCount; ++index) {
            User* const member = runtime::CheckedVectorAt(members, static_cast<std::size_t>(index));
            if (member == nullptr) {
                continue;
            }
            const int memberLevel = member->Level();
            const double weight = static_cast<double>(memberLevel) * static_cast<double>(memberLevel);
            levelWeights[static_cast<std::size_t>(index)] = weight;
            totalLevelWeight += weight;
            if (member->IsAlive() && victim->IsInRange(member, 1500.0) &&
                maximumLevel - memberLevel < g_MAX_LEVELDIFF_PARTY_GIVEN_EXP) {
                eligibleIndices.push_back(static_cast<unsigned int>(index));
            }
        }
        if (eligibleIndices.empty()) {
            continue;
        }

        const std::size_t eligibleCount = eligibleIndices.size();
        double partyMultiplier = 1.0;
        if (eligibleCount < 8) {
            partyMultiplier = g_dPartyExpSPGetRate * g_PartyBonus[eligibleCount];
        }
        if (g_nPartyLevelDiffExpPenaltyIndex > 0 && eligibleCount == 1) {
            partyMultiplier = 1.0;
        }

        std::array<std::int64_t, 7> memberExp{};
        std::array<std::int64_t, 7> memberSp{};
        std::array<int, 7> memberRaidPoint{};
        std::int64_t distributedExp = 0;
        std::int64_t distributedSp = 0;
        int distributedRaidPoint = 0;
        for (int index = 0; index < memberCount; ++index) {
            double memberRatio = partyMultiplier * levelWeights[static_cast<std::size_t>(index)] / totalLevelWeight;
            if (g_nPartyLevelDiffExpPenaltyIndex > 0) {
                memberRatio = partyMultiplier / static_cast<double>(memberCount);
            }

            memberExp[static_cast<std::size_t>(index)] =
                runtime::Win64CvttDoubleToInt64(memberRatio * contributionRatio * baseExp);
            memberSp[static_cast<std::size_t>(index)] =
                runtime::Win64CvttDoubleToInt64(memberRatio * contributionRatio * baseSp);
            memberRaidPoint[static_cast<std::size_t>(index)] =
                runtime::Win64CvttDoubleToInt32(memberRatio * contributionRatio * raidPoint);
            distributedExp = runtime::AddInt64Wrapping(distributedExp, memberExp[static_cast<std::size_t>(index)]);
            distributedSp = runtime::AddInt64Wrapping(distributedSp, memberSp[static_cast<std::size_t>(index)]);
            distributedRaidPoint =
                runtime::AddInt32Wrapping(distributedRaidPoint, memberRaidPoint[static_cast<std::size_t>(index)]);
        }

        std::int64_t remainder = partyExpTarget - distributedExp;
        while (remainder > 0) {
            const int index = std::rand() % memberCount;
            ++memberExp[static_cast<std::size_t>(index)];
            --remainder;
        }
        const std::int64_t partySpTarget = runtime::Win64CvttDoubleToInt64(contributionRatio * baseSp);
        remainder = partySpTarget - distributedSp;
        while (remainder > 0) {
            const int index = std::rand() % memberCount;
            ++memberSp[static_cast<std::size_t>(index)];
            --remainder;
        }
        const int partyRaidTarget = runtime::Win64CvttDoubleToInt32(contributionRatio * raidPoint);
        int raidRemainder = partyRaidTarget - distributedRaidPoint;
        while (raidRemainder > 0) {
            const int index = std::rand() % memberCount;
            ++memberRaidPoint[static_cast<std::size_t>(index)];
            --raidRemainder;
        }

        for (const unsigned int memberIndex : eligibleIndices) {
            User* const user = runtime::CheckedVectorAt(members, memberIndex);
            if (user == nullptr) {
                continue;
            }

            if (victim->IsBoss() && (user->m_data->m_nNoblessType == 1 || user->m_data->m_nNoblessType == 2) &&
                !user->m_bIsObserving) {
                CBoss* const boss = victim->CastBoss();
                user->WriteNoblessAchievement(WIN_RAID, boss->GetNPCId() - 1000000);
            }
            if (user->m_data->m_bIsAlive == 0 || user->m_bIsObserving) {
                continue;
            }

            CPet* const pet = user->GetPet();
            CSummon* const singleSummon = user->GetOneSummon(User::SUMMON_SINGLE);
            double vitalityMultiplier = 1.0;
            if (user->IsPC() && !victim->IsBoss()) {
                vitalityMultiplier = user->CastUser()->GetVitalityExpBonus();
            }
            const int levelDifference = maximumLevel - user->Level();

            std::int64_t remainingUserExp = memberExp[memberIndex];
            std::int64_t remainingUserSp = memberSp[memberIndex];
            std::int64_t lastSummonExperience = 0;
            const std::array<CSummon*, 2> summons = {
                pet != nullptr ? pet->CastSummon() : nullptr,
                singleSummon,
            };
            for (std::size_t summonIndex = 0; summonIndex < summons.size(); ++summonIndex) {
                CSummon* const summon = summons[summonIndex];
                if (summon == nullptr || !summon->IsAlive() || !user->IsInRange(summon, 1500.0)) {
                    continue;
                }

                int directContribution = 0;
                const auto direct = directContributes.find(summon->GetSmartID().Sid());
                if (direct != directContributes.end()) {
                    directContribution = runtime::Win64CvttDoubleToInt32(direct->second);
                }

                double summonRatio = 0.0;
                if (summon->m_data->m_nMasterExpRatio < 0) {
                    const double perMemberPartyContribution = contribution / static_cast<double>(eligibleCount);
                    if (perMemberPartyContribution <= 0.0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] party contribute error[%d]",
                        //     "Object.cpp",
                        //     0x64f,
                        //     perMemberPartyContribution)
                        // L2_LOG_ERROR(Log,
                        //     L"[%S][%d] party contribute error[%d]",
                        //     "Object.cpp",
                        //     0x64f,
                        //     perMemberPartyContribution)
                        L2_LOG_ERROR(
                            Log,
                            "Party reward contribution per member is %.17g for character %u; summon experience-ratio "
                            "division still proceeds with this nonpositive value.",
                            perMemberPartyContribution, static_cast<unsigned>(user->GetDBID())
                        );
                    }
                    summonRatio = std::min(static_cast<double>(directContribution) / perMemberPartyContribution, 1.0);
                    summonRatio = 1.0 - (1.0 - summonRatio);
                } else {
                    const double masterRatio =
                        g_PetManager.MasterExpRatio(100 - summon->m_data->m_nMasterExpRatio, user->Level(), summon->Level());
                    summonRatio = (100.0 - masterRatio) / 100.0;
                }

                const std::int64_t rawSummonExp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(memberExp[memberIndex]) * summonRatio);
                const std::int64_t rawSummonSp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(memberSp[memberIndex]) * summonRatio);
                remainingUserExp = runtime::SubtractInt64Wrapping(remainingUserExp, rawSummonExp);
                if (remainingUserExp < 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] negative amount of exp error!! [%d]",
                    //     "Object.cpp",
                    //     0x65f,
                    //     remainingUserExp)
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d] negative amount of exp error!! [%d]",
                    //     "Object.cpp",
                    //     0x65f,
                    //     remainingUserExp)
                    L2_LOG_ERROR(
                        Log,
                        "Character %u experience remainder became %lld after summon allocation; clamping player experience "
                        "to zero.",
                        static_cast<unsigned>(user->GetDBID()), static_cast<long long>(remainingUserExp)
                    );
                    remainingUserExp = 0;
                }
                remainingUserSp = runtime::SubtractInt64Wrapping(remainingUserSp, rawSummonSp);
                if (remainingUserSp < 0) {
                    remainingUserSp = 0;
                }

                const double summonPenalty = GetExpPenalty(summon, victimLevel);
                const std::int64_t summonBaseExp = runtime::Win64CvttDoubleToInt64(
                    static_cast<double>(rawSummonExp) / summonPenalty * victim->m_data->m_dOverHitBonus
                );
                const std::int64_t summonBaseSp =
                    runtime::Win64CvttDoubleToInt64(static_cast<double>(rawSummonSp) / summonPenalty);

                std::int64_t summonExpBonus = 0;
                std::int64_t summonSpBonus = 0;
                std::int64_t pcCafeSummonExp = 0;
                std::int64_t pcCafeSummonSp = 0;
                User* const summonMaster = summon->GetMaster();
                if (summonMaster != nullptr) {
                    summonExpBonus = runtime::Win64CvttDoubleToInt64(
                        summonMaster->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * static_cast<double>(summonBaseExp)
                    );
                    summonSpBonus = runtime::Win64CvttDoubleToInt64(
                        summonMaster->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * static_cast<double>(summonBaseSp)
                    );
                    if (summonMaster->m_data->m_bPCCafeUser) {
                        pcCafeSummonExp = runtime::Win64CvttDoubleToInt64(
                            summonMaster->GetPCCafeUserExpSPBonus() * static_cast<double>(summonBaseExp)
                        );
                        pcCafeSummonSp = runtime::Win64CvttDoubleToInt64(
                            summonMaster->GetPCCafeUserExpSPBonus() * static_cast<double>(summonBaseSp)
                        );
                    }
                }
                summonExpBonus = runtime::AddInt64Wrapping(
                    summonExpBonus,
                    runtime::Win64CvttDoubleToInt64(user->GetUserPremiumExpBonus() * static_cast<double>(summonBaseExp))
                );
                summonSpBonus = runtime::AddInt64Wrapping(
                    summonSpBonus,
                    runtime::Win64CvttDoubleToInt64(user->GetUserPremiumSpBonus() * static_cast<double>(summonBaseSp))
                );

                std::int64_t awardedSummonExp = runtime::AddInt64Wrapping(
                    GetAdjustedExp(
                        static_cast<double>(summonBaseExp), vitalityMultiplier, user, summon,
                        static_cast<double>(summonExpBonus), EXP_TYPE, false
                    ),
                    pcCafeSummonExp
                );
                std::int64_t awardedSummonSp = runtime::AddInt64Wrapping(
                    GetAdjustedExp(
                        static_cast<double>(summonBaseSp), vitalityMultiplier, user, summon,
                        static_cast<double>(summonSpBonus), SP_TYPE, false
                    ),
                    pcCafeSummonSp
                );
                AdjustChinaFatigueSystem(user, awardedSummonExp, awardedSummonSp);
                awardedSummonExp = std::max<std::int64_t>(awardedSummonExp, 0);
                awardedSummonSp = std::max<std::int64_t>(awardedSummonSp, 0);

                {
                    L2_GUARD(*summon->m_lock);
                    summon->ExpInc(awardedSummonExp, false);
                    summon->SPInc(awardedSummonSp);
                }
                summon->SetMPReward(victim, killer);
                summon->SendSummonInfo(true, false);
                lastSummonExperience = awardedSummonExp;
            }

            const double expPenalty = GetExpPenalty(user, victim->Level());
            std::int64_t userBaseSp = runtime::Win64CvttDoubleToInt64(static_cast<double>(remainingUserSp) / expPenalty);
            double adjustedRaidPoint =
                expPenalty == 9223372036854775808.0 ? 0.0 : static_cast<double>(memberRaidPoint[memberIndex]) / expPenalty;
            std::int64_t userBaseExp = runtime::Win64CvttDoubleToInt64(
                static_cast<double>(remainingUserExp) / expPenalty * victim->m_data->m_dOverHitBonus
            );

            if (victim->m_data->m_dOverHitBonus > 1.0 && user->IsPC()) {
                User* const overhitUser = user->CastUser();
                CCreature::SendMagicAttckInfo(overhitUser, victim, MagicAttackInfo::OverHit);
                overhitUser->GetSocket()->Send(
                    "chccdcd", 0x62, SystemMessageId::OVERHIT, 2, 1, overhitUser->GetSmartID().Sid(), 1,
                    victim->GetSmartID().Sid()
                );
            }

            double eventRate = g_dExpSPGetRate;
            std::int64_t expBonus = 0;
            std::int64_t spBonus = 0;
            std::int64_t pcCafeExpBonus = 0;
            std::int64_t pcCafeSpBonus = 0;
            if (user->IsPC()) {
                User* const bonusUser = user->CastUser();
                expBonus = runtime::AddInt64Wrapping(
                    runtime::Win64CvttDoubleToInt64(
                        bonusUser->GetUserPremiumExpBonus() * static_cast<double>(userBaseExp)
                    ),
                    runtime::Win64CvttDoubleToInt64(
                        bonusUser->m_SkillDurationMod.m_dExpBonusPercent / 100.0 * static_cast<double>(userBaseExp)
                    )
                );
                spBonus = runtime::AddInt64Wrapping(
                    runtime::Win64CvttDoubleToInt64(bonusUser->GetUserPremiumSpBonus() * static_cast<double>(userBaseSp)),
                    runtime::Win64CvttDoubleToInt64(
                        bonusUser->m_SkillDurationMod.m_dSPBonusPercent / 100.0 * static_cast<double>(userBaseSp)
                    )
                );
                if (bonusUser->m_data->m_bPCCafeUser) {
                    pcCafeExpBonus = runtime::Win64CvttDoubleToInt64(
                        bonusUser->GetPCCafeUserExpSPBonus() * static_cast<double>(userBaseExp)
                    );
                    pcCafeSpBonus = runtime::Win64CvttDoubleToInt64(
                        bonusUser->GetPCCafeUserExpSPBonus() * static_cast<double>(userBaseSp)
                    );
                    expBonus = runtime::AddInt64Wrapping(expBonus, pcCafeExpBonus);
                    spBonus = runtime::AddInt64Wrapping(spBonus, pcCafeSpBonus);
                }
                std::vector<CEvent*> events;
                CEventManager::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
                for (CEvent* const event : events) {
                    if (event != nullptr && event->InEventTime()) {
                        eventRate +=
                            static_cast<CEvent_EconomyControlEvent*>(event)->GetExpSpGetRate(bonusUser->Level()) - 1.0;
                        break;
                    }
                }
                bonusUser->IncrementMonsterBookKillCount(victim);
            }

            const int levelRateIndex = std::min(levelDifference, 9);
            const double levelRate = LEVELDIFF_EXP_RATE_origin[g_nPartyLevelDiffExpPenaltyIndex][levelRateIndex];
            std::int64_t packetBaseExp =
                runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseExp) * eventRate * levelRate);
            std::int64_t packetBaseSp =
                runtime::Win64CvttDoubleToInt64(static_cast<double>(userBaseSp) * eventRate * levelRate);

            std::int64_t awardedExperience = GetAdjustedExp(
                static_cast<double>(userBaseExp), vitalityMultiplier, user, nullptr, static_cast<double>(expBonus), EXP_TYPE,
                true
            );
            std::int64_t awardedSp = GetAdjustedExp(
                static_cast<double>(userBaseSp), vitalityMultiplier, user, nullptr, static_cast<double>(spBonus), SP_TYPE,
                true
            );
            int awardedRaidPoint = runtime::Win64CvttDoubleToInt32(std::max(eventRate * adjustedRaidPoint, 1.0));
            AdjustChinaFatigueSystem(user, awardedExperience, awardedSp, awardedRaidPoint, packetBaseExp, packetBaseSp);

            awardedExperience = runtime::Win64CvttDoubleToInt64(static_cast<double>(awardedExperience) * levelRate);
            awardedSp = runtime::Win64CvttDoubleToInt64(static_cast<double>(awardedSp) * levelRate);
            if (user->CanSoulCollectByExp(awardedExperience)) {
                user->IncreaseSoulCount(1, static_cast<int>(victim->GetSmartID().Sid()));
            }
            if (awardedExperience < 0) {
                awardedExperience = 0;
                packetBaseExp = 0;
            }
            if (awardedSp < 0) {
                awardedSp = 0;
                packetBaseSp = 0;
            }

            std::int64_t experienceDelta = 0;
            {
                L2_GUARD(*user->m_lock);
                const std::int64_t previousExperience = user->m_data->m_nExp;
                experienceDelta = user->ExpInc(awardedExperience, false) - previousExperience;
                GetBonusItemByLuckyStat(user, victim, awardedExperience);
                awardedSp = user->SPInc(awardedSp);
            }
            user->AccumulateExpForItem(awardedExperience);
            AddUltimateSkillPoint(user, victim);
            user->m_nParamChanged |= PCB_USER;
            user->SetMPReward(victim, killer);

            const double unpenalizedEventPcCafeExp = static_cast<double>(pcCafeExpBonus) * eventRate;
            const double unpenalizedEventPcCafeSp = static_cast<double>(pcCafeSpBonus) * eventRate;
            const std::int64_t eventPcCafeExp = runtime::Win64CvttDoubleToInt64(unpenalizedEventPcCafeExp * levelRate);
            const std::int64_t eventPcCafeSp = runtime::Win64CvttDoubleToInt64(unpenalizedEventPcCafeSp * levelRate);
            SendExpSpRewardPacketWithBaselines(
                user, experienceDelta, packetBaseExp, eventPcCafeExp, unpenalizedEventPcCafeExp, awardedSp, packetBaseSp,
                eventPcCafeSp, unpenalizedEventPcCafeSp, pcCafeExpBonus > 0
            );
            if (!user->m_bVotePeaceZone) {
                user->CheckVoteSystemGetExp();
            }
            ProcessPledgeHuntPoint(user, experienceDelta, user->tPledgeBonusTimeStamp[PledgeBonus::HUNT_REWARD_RECEIVE]);

            if (victim->IsBoss() && raidPoint > 0.0) {
                user->AddBossRelatedBonus(*victim, awardedRaidPoint);
            }

            int vitalityConsumed = 0;
            if (awardedExperience > 0 || lastSummonExperience > 0) {
                const int before = user->CastUser()->GetVitalPoint();
                user->CastUser()->VitalityConsume(victim, static_cast<int>(eligibleCount), partyMultiplier);
                vitalityConsumed = before - user->CastUser()->GetVitalPoint();
            }

            if (victim->IsNPC()) {
                CLogFormatter formatter;
                formatter.WriteLog2(
                    0x458, user, victim, awardedExperience, user->m_data->m_nExp, victimLevel, damagedSeconds,
                    contributionCount, tick, static_cast<std::int64_t>(eligibleCount)
                );
                formatter.WriteLog2(0x465, user, victim, awardedSp, user->m_data->m_nSP, 0, 0, 0, 0, 0);
                formatter.WriteLog2(0x466, user, victim, vitalityConsumed, user->CastUser()->GetVitalPoint(), 0, 0, 0, 0, 0);
            }
            if (victim->IsBoss() && user->IsPC()) {
                User* const bossLogUser = user->CastUser();
                if (bossLogUser != nullptr) {
                    CLogFormatter formatter;
                    formatter.WriteLog2(
                        0x1fa4, user, victim, runtime::Win64CvttDoubleToInt64(expPenalty),
                        runtime::Win64CvttDoubleToInt64(victim->m_data->m_dOverHitBonus),
                        runtime::Win64CvttDoubleToInt64(bossLogUser->m_SkillDurationMod.m_dExpBonusPercent),
                        pcCafeExpBonus, runtime::Win64CvttDoubleToInt64(eventRate),
                        runtime::Win64CvttDoubleToInt64(vitalityMultiplier), 0
                    );
                }
            }
            if (victim->IsNPC()) {
                if (awardedExperience > 0) {
                    user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING_ANY, 0, 1);
                }
                if (static_cast<std::uint32_t>(user->m_data->m_nLevel - victim->m_data->m_nLevel + 10) < 21U) {
                    if (user->m_data->m_nWorldId == g_nWorldId) {
                        CNPC* const npc = victim->CastNPC();
                        user->PledgeMissionCheckAndAddCount(PledgeMission::PMGT_HUNTING, npc->GetNPCId() - 1000000, 1);
                    } else {
                        CNPC* const npc = victim->CastNPC();
                        user->PledgeMissionCheckAndAddCountInRaid(
                            PledgeMission::PMGT_W_HUNTING, npc->GetNPCId() - 1000000, 1
                        );
                    }
                }
            }
        }
    }

    if (victim->IsBoss() && killerMasterSid != 0) {
        victim->CastNPC()->m_sidKillerMaster = killerMasterSid;
        victim->CastNPC()->m_nKillerMemberCount = killerMemberCount;
    }

    DistributeMPCC(
        victim, mpccContributes, directContributes, greatestContribution, totalContribution, baseExp, baseSp, raidPoint,
        tick, damagedSeconds, contributionCount
    );
    if (m_nMPCCRouting != 0) {
        SetMPCCMasterPartyRouting();
    }
}
