#include "Shuttle.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "Door.h"
#include "Item.h"
#include "NewActionMove.h"
#include "ParserShuttle.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "SharedContainer.h"
#include "SharedFactories.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr const char* kShuttleSource = "Shuttle.cpp";

CObject* AsShuttleObject(CShuttle* shuttle) noexcept { return static_cast<CObject*>(shuttle); }

int CurrentCoordinate(double value) noexcept { return runtime::Win64CvttDoubleToInt32(value); }

} // namespace

CShuttleSmartIdManager CShuttle::s_ShuttleSmartIdManager;
volatile std::int32_t g_ShuttleCount = 0;

L2_DEFINE_OBJECT_CLASS_ID(CShuttle)

L2_DEFINE_OBJECT_CLASS_NAME(CShuttle, m_pClassName_CShuttle)

L2_DEFINE_OBJECT_ALLOCATOR(CShuttle, __GetClassName())

L2_DEFINE_OBJECT_NEW(CShuttle)

CShuttle::CShuttle(int objectId, CShuttleConst* constant) :
    m_data(nullptr),
    m_lock(nullptr),
    m_const(constant),
    m_nObjectID(objectId),
    padding_74{},
    m_pContainer(nullptr),
    m_nMoveSpeed(0),
    m_nDirection(0),
    m_nRotationSpeed(0),
    m_bIsInWorld(false),
    padding_bd{},
    m_index(0),
    m_nLastWaitTick(0),
    m_State(STS_MOVE),
    padding_ec{} {
    L2_GUARD_FUNCTION();
    OnCreate();
}

CShuttle::CShuttle(const CShuttle& other) :
    CAbstractCarrier(other),
    m_data(other.m_data),
    m_lock(other.m_lock),
    m_const(other.m_const),
    m_nObjectID(other.m_nObjectID),
    padding_74{},
    m_pContainer(other.m_pContainer),
    m_vOrigin(other.m_vOrigin),
    m_vDestination(other.m_vDestination),
    m_nMoveSpeed(other.m_nMoveSpeed),
    m_nDirection(other.m_nDirection),
    m_nRotationSpeed(other.m_nRotationSpeed),
    m_bIsInWorld(other.m_bIsInWorld),
    padding_bd{},
    m_vNodeList(other.m_vNodeList),
    m_index(other.m_index),
    m_nLastWaitTick(other.m_nLastWaitTick),
    m_State(other.m_State),
    padding_ec{} {
    L2_GUARD_FUNCTION();
}

CShuttle::~CShuttle() {
    L2_GUARD_FUNCTION();
    CContainerDB::Inst().FreeShuttleContainer(m_pContainer);
    CShuttleFactory::Inst().Remove(this);
    runtime::raw_atomic::DecrementI32(&g_ShuttleCount);
}

L2_DEFINE_OBJECT_TYPED_RELEASE(CShuttle)

bool CShuttle::IsShuttle() { return true; }

CShuttle* CShuttle::CastShuttle() { return this; }

CObject* CShuttle::Copy() { return new CShuttle(*this); }

bool CShuttle::IsActive() { return m_data != nullptr && m_data->m_bIsActive; }

void CShuttle::OnCreate() {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe CShuttle::OnCreate@0x00a5ca08 performs the
    // function-local CShuttleFactory construction at this exact first-use
    // point before Add. The portable composition call publishes the same
    // data/constant/index mappings before exposing the factory instance.
    l2server::L2ServerSharedFactories::InitializeShuttle();
    CShuttleFactory& factory = CShuttleFactory::Inst();
    m_data = factory.Add(this);
    m_nId = CShuttle::s_ShuttleSmartIdManager.RegisterObject(
        // 4,
        SCLS_SHUTTLE, static_cast<std::uint32_t>(m_data->index), this
    );
    m_pContainer = CContainerDB::Inst().AllocShuttleContainer(static_cast<int>(m_nId));
    factory.PublishIndexId(m_data->index, m_nId);
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    m_data->m_bIsActive = false;
}

void CShuttle::Delete() {
    L2_GUARD_FUNCTION();
    LeaveWorld();
    const std::int32_t deleteCount = runtime::raw_atomic::IncrementI32(&m_nDeleteCount);
    if (deleteCount < 2) {
        m_bDeleting = true;
        static_cast<void>(new CShuttleReleaseVerifier(this));
        SetDelete();
        L2_RELEASE(this, ObjectRefType::CreateDelete);
        CShuttle::s_ShuttleSmartIdManager.DeleteObject(m_nId);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid CShuttle::Delete",
    //     L"Shuttle.cpp",
    //     0xca)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid CShuttle::Delete",
    //     L"Shuttle.cpp",
    //     0xca)
    L2_LOG_ERROR(
        Log, "Duplicate deletion of shuttle %u: delete count is %d; further deletion is skipped.",
        static_cast<unsigned>(m_nId), deleteCount
    );
    runtime::process::ReportCallStack(2, false);
}

void CShuttle::LeaveWorld() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().LeaveWorld(this);
}

FVector CShuttle::GetAdjustPos(double distance, FVector position) {
    L2_GUARD_FUNCTION();
    const FVector current = GetPos();
    const double remainingSquared = (m_vDestination.x - current.x) * (m_vDestination.x - current.x) +
        (m_vDestination.y - current.y) * (m_vDestination.y - current.y) +
        (m_vDestination.z - current.z) * (m_vDestination.z - current.z);
    if (distance * distance <= remainingSquared) {
        const double x = static_cast<double>(CurrentCoordinate(position.x));
        const double y = static_cast<double>(CurrentCoordinate(position.y));
        const double z = static_cast<double>(CurrentCoordinate(position.z));
        const double length = std::sqrt(x * x + y * y + z * z);
        if (length != 0.0) {
            position = FVector{x / length * distance, y / length * distance, z / length * distance};
        }
    }
    return FVector{current.x + position.x, current.y + position.y, current.z + position.z};
}

void CShuttle::StartWait() {
    L2_GUARD_FUNCTION();
    m_nMoveSpeed = 0;
    m_State = STS_WAIT;
    m_nLastWaitTick = runtime::GetTickCount();
}

bool CShuttle::HasArrived() const {
    const FVector current = GetPos();
    return (current.x - m_vDestination.x) * (current.x - m_vDestination.x) +
        (current.y - m_vDestination.y) * (current.y - m_vDestination.y) +
        (current.z - m_vDestination.z) * (current.z - m_vDestination.z) <
        16.0;
}

void CShuttle::SetNextNode() {
    L2_GUARD_FUNCTION();
    if (m_index < 0 || static_cast<std::size_t>(m_index) >= m_vNodeList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid node(%d) of regular shuttle path",
        //     m_index)
        // L2_LOG_ERROR(Log,
        //     L"Invalid node(%d) of regular shuttle path",
        //     m_index)
        L2_LOG_ERROR(
            Log,
            "Shuttle %u has invalid current node %d for a %zu-node path; next-node advancement and wrapping still proceed.",
            static_cast<unsigned>(m_nId), m_index, static_cast<std::size_t>(m_vNodeList.size())
        );
    }
    ++m_index;
    if (static_cast<std::size_t>(m_index) >= m_vNodeList.size()) {
        m_index = 0;
    }
}

bool CShuttle::AddOuterDoor(int nodeIndex, const char16_t* doorName) {
    L2_GUARD_FUNCTION();
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= m_vNodeList.size() ||
        runtime::CheckedVectorAt(m_vNodeList, static_cast<std::size_t>(nodeIndex)).m_pOuterDoor != nullptr) {
        return false;
    }
    CDoor* const door = g_DoorDB.GetDoor(doorName, 0);
    if (door == nullptr) {
        return false;
    }
    runtime::CheckedVectorAt(m_vNodeList, static_cast<std::size_t>(nodeIndex)).AddOuterDoor(door);
    return true;
}

const CShuttlePathNode& CShuttle::GetCurrentNode() const {
    L2_GUARD_FUNCTION();
    if (m_index < 0 || static_cast<std::size_t>(m_index) >= m_vNodeList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid node(%d) of regular shuttle path",
        //     m_index)
        // L2_LOG_ERROR(Log,
        //     L"Invalid node(%d) of regular shuttle path",
        //     m_index)
        L2_LOG_ERROR(
            Log, "Shuttle %u has invalid current node %d for a %zu-node path; returning an empty node.",
            static_cast<unsigned>(m_nId), m_index, static_cast<std::size_t>(m_vNodeList.size())
        );
        static const CShuttlePathNode empty;
        return empty;
    }
    return runtime::CheckedVectorAt(m_vNodeList, static_cast<std::size_t>(m_index));
}

const CShuttlePathNode& CShuttle::GetNode(int nodeIndex) const {
    L2_GUARD_FUNCTION();
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= m_vNodeList.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid node(%d) of regular shuttle path [GetNode]",
        //     nodeIndex)
        // L2_LOG_ERROR(Log,
        //     L"Invalid node(%d) of regular shuttle path [GetNode]",
        //     nodeIndex)
        L2_LOG_ERROR(
            Log, "Shuttle %u requested node %d from a %zu-node path; returning an empty node.", static_cast<unsigned>(m_nId),
            nodeIndex, static_cast<std::size_t>(m_vNodeList.size())
        );
        static const CShuttlePathNode empty;
        return empty;
    }
    return runtime::CheckedVectorAt(m_vNodeList, static_cast<std::size_t>(nodeIndex));
}

void CShuttle::SendShuttleInfo(CUserSocket* socket, bool includeCurrentState, int nodeIndex) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> doorPacket{};
    int packetSize = 0;
    const CShuttlePathNode& stateNode = GetNode(nodeIndex);
    for (const InnerDoor& door : m_const->m_vInnerDoorList) {
        packetSize += Assemble(
            doorPacket.data() + packetSize, static_cast<int>(doorPacket.size()) - packetSize, "dddddddddd", door.id,
            CurrentCoordinate(door.open_pos.x), CurrentCoordinate(door.open_pos.y), CurrentCoordinate(door.open_pos.z),
            CurrentCoordinate(door.close_pos.x), CurrentCoordinate(door.close_pos.y), CurrentCoordinate(door.close_pos.z),
            CurrentCoordinate(door.safe_pos.x), CurrentCoordinate(door.safe_pos.y), CurrentCoordinate(door.safe_pos.z)
        );

        bool belongsToNode = false;
        if (includeCurrentState) {
            belongsToNode = std::find(stateNode.m_vInnerDoorList.begin(), stateNode.m_vInnerDoorList.end(), door.id) !=
                stateNode.m_vInnerDoorList.end();
        }
        const bool opened = belongsToNode && m_State == STS_WAIT;
        packetSize += Assemble(
            doorPacket.data() + packetSize, static_cast<int>(doorPacket.size()) - packetSize, "dd", opened, belongsToNode
        );
    }

    const FVector position = GetPos();
    const auto sendBase = [&](auto&& sender) {
        sender(
            "chdddddddb", 0xfe, 0xcb, m_nObjectID, CurrentCoordinate(position.x), CurrentCoordinate(position.y),
            CurrentCoordinate(position.z), m_nDirection, m_const->m_nId, static_cast<int>(m_const->m_vInnerDoorList.size()),
            packetSize, doorPacket.data()
        );
    };

    if (socket == nullptr) {
        sendBase([&](const char* format, auto... values) {
            g_ptlUserMap->BroadcastToNeighbor(*this, 0x80000, position, format, values...);
        });
        if (m_nMoveSpeed > 0) {
            g_ptlUserMap->BroadcastToNeighbor(
                *this, 0x80000, position, "chdddddd", 0xfe, 0xce, m_nObjectID, m_nMoveSpeed, m_nRotationSpeed,
                CurrentCoordinate(m_vDestination.x), CurrentCoordinate(m_vDestination.y), CurrentCoordinate(m_vDestination.z)
            );
        }
        return;
    }

    sendBase([&](const char* format, auto... values) { socket->Send(format, values...); });
    if (m_nMoveSpeed > 0) {
        socket->Send(
            "chdddddd", 0xfe, 0xce, m_nObjectID, m_nMoveSpeed, m_nRotationSpeed, CurrentCoordinate(m_vDestination.x),
            CurrentCoordinate(m_vDestination.y), CurrentCoordinate(m_vDestination.z)
        );
    }
}

void CShuttle::StartMove() {
    L2_GUARD_FUNCTION();
    const CShuttlePathNode& node = GetCurrentNode();
    m_vDestination = node.m_vPos;
    m_nMoveSpeed = node.m_nSpeed;
    m_State = STS_MOVE;
}

void CShuttle::OpenStationDoor() {
    L2_GUARD_FUNCTION();
    CDoor* const door = GetCurrentNode().m_pOuterDoor;
    if (door != nullptr) {
        door->Open(false, false);
    }
}

void CShuttle::CloseStationDoor() {
    L2_GUARD_FUNCTION();
    const CShuttlePathNode& node = GetCurrentNode();
    CDoor* const door = node.m_pOuterDoor;
    if (door == nullptr) {
        return;
    }
    door->Close(false, false);
    const FVector doorPosition = door->GetPos();
    if (CContainerDB::Inst().GetWorldContainer(
            CurrentCoordinate(doorPosition.x), CurrentCoordinate(doorPosition.y), CurrentCoordinate(doorPosition.z)
        ) != nullptr) {
        CWorld::Inst().ShuttleKickFromDoor(door, node.m_vBanPos);
    }
}

void CShuttle::EnterWorld() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().EnterWorld(this, GetPos());
    StartMove();
}

void CShuttle::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (!IsActive()) {
        return;
    }
    if (m_State == STS_MOVE) {
        if (!HasArrived()) {
            if (m_nMoveSpeed < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"shuttle %d not moving!",
                //     static_cast<int>(m_nId))
                // L2_LOG_ERROR(Log,
                //     L"shuttle %d not moving!",
                //     static_cast<int>(m_nId))
                L2_LOG_ERROR(
                    Log,
                    "Shuttle %u has move speed %d before arrival at node %d; restoring speed from the current path node.",
                    static_cast<unsigned>(m_nId), m_nMoveSpeed, m_index
                );
                m_nMoveSpeed = GetCurrentNode().m_nSpeed;
            }
            const FVector current = GetPos();
            FVector delta{m_vDestination.x - current.x, m_vDestination.y - current.y, m_vDestination.z - current.z};
            const FVector next = GetAdjustPos(static_cast<double>(m_nMoveSpeed) * 666.0 / 1000.0, delta);
            CWorld::Inst().ShuttleMove(this, next);
        } else {
            OpenStationDoor();
            StartWait();
            SendShuttleInfo(nullptr, true, m_index);
        }
    } else if (m_State == STS_WAIT) {
        const std::uint32_t elapsed = runtime::GetTickCount() - m_nLastWaitTick;
        if (GetCurrentNode().m_nWaitTime < static_cast<std::int32_t>(elapsed)) {
            const int previousNode = m_index;
            CloseStationDoor();
            SetNextNode();
            StartMove();
            SendShuttleInfo(nullptr, true, previousNode);
        }
    }
    AddTimer(666, 0);
}

void CShuttle::AddPathNode(const CShuttlePathNode& node) {
    L2_GUARD_FUNCTION();
    m_vNodeList.push_back(node);
}

FVector CShuttle::GetPos() const { return m_data->GetPos(); }

FVector CShuttle::ConvRelToAbs(FVector position) const {
    L2_GUARD_FUNCTION();
    const int yaw = static_cast<int>(static_cast<std::uint16_t>(GetDir().Yaw));
    const FVector axis{0.0, 0.0, 1.0};
    FVector rotated = FVector::RotateAngleAxis(&position, yaw + 0x4000, &axis);
    const FVector current = GetPos();
    rotated.x += current.x;
    rotated.y += current.y;
    rotated.z += current.z;
    return rotated;
}

FVector CShuttle::ConvAbsToRel(FVector position) const {
    L2_GUARD_FUNCTION();
    const FVector current = GetPos();
    position.x -= current.x;
    position.y -= current.y;
    position.z -= current.z;
    const int yaw = static_cast<int>(static_cast<std::uint16_t>(GetDir().Yaw));
    const FVector axis{0.0, 0.0, 1.0};
    return FVector::RotateAngleAxis(&position, -0x4000 - yaw, &axis);
}

FAngle CShuttle::GetDir() const { return m_data->GetDir(); }

void CShuttle::SetDir(const FAngle& direction) { m_data->SetDir(direction); }

int CShuttle::GetIndex() const { return m_data->index; }

bool CShuttle::SetMovingIntoCarrier(CMoveRelativeAction* action) {
    L2_GUARD_FUNCTION();
    return action->SetMovingIntoShuttleImpl();
}

void CShuttle::InSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class Filter final : public CContainerFilter {
    public:
        explicit Filter(CObject* target) : target_(target) {}
        bool CanDo(CObject* candidate) override { return candidate != target_ && candidate->IsPC(); }

    private:
        CObject* target_;
    } filter(object);

    class Function final : public CContainerFunc {
    public:
        explicit Function(CObject* target) : target_(target) {}
        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->CastUser()->InSight(target_, false);
        }

    private:
        CObject* target_;
    } function(object);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

void CShuttle::OutOfSight(CObject* object, bool flag) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class Filter final : public CContainerFilter {
    public:
        explicit Filter(CObject* target) : target_(target) {}
        bool CanDo(CObject* candidate) override { return candidate != target_ && candidate->IsPC(); }

    private:
        CObject* target_;
    } filter(object);

    class Function final : public CContainerFunc {
    public:
        Function(CObject* target, bool flag) : target_(target), flag_(flag) {}
        void Do(CObject* candidate) override {
            if (candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) {
                return;
            }
            candidate->CastUser()->OutOfSight(target_, flag_);
        }

    private:
        CObject* target_;
        bool flag_;
    } function(object, flag);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

void CShuttle::SeeDropItem(CItem* item, CCreature* dropper) {
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

FAngle CShuttle::GetMovingDir() const { return FAngle{m_nDirection}; }

void CShuttleConst::AddInnerDoor(InnerDoor innerDoor) {
    L2_GUARD_FUNCTION();

    m_vInnerDoorList.push_back(innerDoor);
}

void CShuttleConst::AddInnerDoor(int id, ::FVector openPos, ::FVector closePos, ::FVector safePos) {
    L2_GUARD_FUNCTION();

    const InnerDoor innerDoor{
        id,
        ::FVector(
            runtime::Win64CvttDoubleToInt32(openPos.x), runtime::Win64CvttDoubleToInt32(openPos.y),
            runtime::Win64CvttDoubleToInt32(openPos.z)
        ),
        ::FVector(
            runtime::Win64CvttDoubleToInt32(closePos.x), runtime::Win64CvttDoubleToInt32(closePos.y),
            runtime::Win64CvttDoubleToInt32(closePos.z)
        ),
        ::FVector(
            runtime::Win64CvttDoubleToInt32(safePos.x), runtime::Win64CvttDoubleToInt32(safePos.y),
            runtime::Win64CvttDoubleToInt32(safePos.z)
        ),
    };
    m_vInnerDoorList.push_back(innerDoor);
}

CShuttlePathNode::CShuttlePathNode() {
    L2_GUARD_FUNCTION();

    m_Type = MOVE;
    m_nWaitTime = 0;
    m_pOuterDoor = nullptr;
    m_vInnerDoorList.clear();
    m_nSpeed = 0;
}

CShuttlePathNode::~CShuttlePathNode() { m_pOuterDoor = nullptr; }

void CShuttlePathNode::AddOuterDoor(CDoor* pDoor) {
    L2_GUARD_FUNCTION();

    m_pOuterDoor = pDoor;
}

CShuttleManager::CShuttleManager() {}

CShuttleManager::~CShuttleManager() = default;

CShuttleManager& CShuttleManager::Inst() {
    static CShuttleManager manager;
    return manager;
}

bool CShuttleManager::DeleteShuttle(int objectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ShuttleMap.find(objectId);
    if (found == m_ShuttleMap.end() || found->second == nullptr) {
        return false;
    }
    found->second->Delete();
    m_ShuttleMap.erase(found);
    return true;
}

void CShuttleManager::DeleteAllShuttle() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (const auto& entry : m_ShuttleMap) {
        if (entry.second != nullptr) {
            entry.second->Delete();
        }
    }
    m_ShuttleMap.clear();
}

CShuttle* CShuttleManager::GetShuttle(int objectId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ShuttleMap.find(objectId);
    return found == m_ShuttleMap.end() ? nullptr : found->second;
}

CShuttleConst* CShuttleManager::GetShuttleConst(int constId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ShuttleConstMap.find(constId);
    return found == m_ShuttleConstMap.end() ? nullptr : found->second;
}

void CShuttleManager::AllShuttleEnterWorld() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (const auto& entry : m_ShuttleMap) {
        CShuttle* const shuttle = entry.second;
        if (shuttle == nullptr) {
            continue;
        }
        const FVector position = shuttle->GetCurrentNode().m_vPos;
        shuttle->m_data->SetPos(position);
        shuttle->m_vDestination = position;
        shuttle->m_vOrigin = position;
        shuttle->EnterWorld();
    }
}

CShuttleConst* CShuttleManager::CreateShuttleConst(int constId, double radius) {
    L2_GUARD_FUNCTION();
    if (m_ShuttleConstMap.find(constId) != m_ShuttleConstMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d] CreateShuttleConst failed. const already exists.",
        //     constId)
        // L2_LOG_ERROR(Log,
        //     L"[%d] CreateShuttleConst failed. const already exists.",
        //     constId)
        L2_LOG_ERROR(
            Log, "Cannot create shuttle definition %d: the ID already exists; the earlier definition is retained.", constId
        );
        return nullptr;
    }
    CShuttleConst* const constant = new (std::nothrow) CShuttleConst{};
    if (constant == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CreateShuttleConst failed. no memory?")
        // L2_LOG_ERROR(Log,
        //     L"CreateShuttleConst failed. no memory?")
        L2_LOG_ERROR(Log, "Cannot allocate shuttle definition %d; creation fails.", constId);
        return nullptr;
    }
    constant->m_nId = constId;
    constant->m_dRadius = radius;
    m_ShuttleConstMap.emplace(constId, constant);
    return constant;
}

void CShuttleManager::Load() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\elevatordata.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/elevatordata.txt";
#endif
    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Shuttle.cpp",
        //     0x375,
        //     L"..\\Script\\ArmorEnchantBonusData.txt")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"Shuttle.cpp",
        //     0x375,
        //     L"..\\Script\\ArmorEnchantBonusData.txt")
        L2_LOG_ERROR(Log, "Cannot read elevatordata.txt; shuttle loading stops.");
        return;
    }

    CParserForShuttle parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForShuttle::create failed",
        //     L"Shuttle.cpp",
        //     0x37d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForShuttle::create failed",
        //     L"Shuttle.cpp",
        //     0x37d)
        L2_LOG_ERROR(Log, "Cannot initialize the elevatordata.txt parser; shuttle loading stops.");
        return;
    }
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Shuttle file failed after line[%d]",
        //     L"Shuttle.cpp",
        //     0x388,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Shuttle file failed after line[%d]",
        //     L"Shuttle.cpp",
        //     0x388,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse elevatordata.txt at script line %lld; already inserted shuttle data is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

CShuttle* CShuttleManager::CreateShuttle(int objectId, int constId) {
    L2_GUARD_FUNCTION();
    const auto constant = m_ShuttleConstMap.find(constId);
    if (constant == m_ShuttleConstMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CreateShuttle failed. no such const.")
        // L2_LOG_ERROR(Log,
        //     L"CreateShuttle failed. no such const.")
        L2_LOG_ERROR(Log, "Cannot create shuttle object %d: definition %d does not exist.", objectId, constId);
        return nullptr;
    }
    if (m_ShuttleMap.find(objectId) != m_ShuttleMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"id [%d] shuttle already exists!",
        //     objectId)
        // L2_LOG_ERROR(Log,
        //     L"id [%d] shuttle already exists!",
        //     objectId)
        L2_LOG_ERROR(
            Log, "Cannot create shuttle object %d: the object ID already exists; the earlier shuttle is retained.", objectId
        );
        return nullptr;
    }
    CShuttle* const shuttle = new CShuttle(objectId, constant->second);
    if (shuttle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CreateShuttle failed. no memory?")
        // L2_LOG_ERROR(Log,
        //     L"CreateShuttle failed. no memory?")
        L2_LOG_ERROR(Log, "Cannot allocate shuttle object %d from definition %d; creation fails.", objectId, constId);
        return nullptr;
    }
    m_ShuttleMap.emplace(objectId, shuttle);
    return shuttle;
}

void CShuttleManager::AddPathNode(int objectId, int x, int y, int z) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_ShuttleMap.find(objectId);
    if (found == m_ShuttleMap.end() || found->second == nullptr) {
        return;
    }
    CShuttlePathNode node;
    node.m_Type = CShuttlePathNode::MOVE;
    node.m_vPos = FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    node.m_nWaitTime = 1000;
    found->second->AddPathNode(node);
}

CShuttleReleaseVerifier::CShuttleReleaseVerifier(CShuttle* shuttle) : m_pShuttle(shuttle) {
    L2_ADD_REF(AsShuttleObject(m_pShuttle), ObjectRefType::VerifierRetain);
    AddTimer(10000, 1);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CShuttleReleaseVerifier::TimerExpired(int) {
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
#else
    try {
#endif
        CObject* const shuttle = AsShuttleObject(m_pShuttle);
        // Capture diagnostic identity before releasing the verifier reference.
        const std::uint32_t shuttleIdForLog = m_pShuttle->m_nId;
        const std::int32_t remaining = L2_RELEASE(shuttle, ObjectRefType::VerifierRetain);
        if (remaining > 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] ORT - Shuttle Release failed. "
            //     "refcount[%d] id[%d]",
            //     kShuttleSource,
            //     0x31,
            //     shuttle->RefCount(),
            //     m_pShuttle->m_nId)
            // L2_LOG_ERROR(Log,
            //     "[%s][%d] ORT - Shuttle Release failed. "
            //     "refcount[%d] id[%d]",
            //     kShuttleSource,
            //     0x31,
            //     shuttle->RefCount(),
            //     m_pShuttle->m_nId)
            L2_LOG_ERROR(
                Log,
                "Shuttle %u still has %d references after releasing the verifier reference; reporting reference owners.",
                static_cast<unsigned>(shuttleIdForLog), remaining
            );
            shuttle->ReportOrt(kShuttleSource, 0x33);
        }
#if defined(_MSC_VER) && defined(_WIN32)
        // The current x64 scope table stores EXCEPTION_EXECUTE_HANDLER directly
        // as filter value 1 for [00a5af69, 00a5afe1).
    } __except (1) {
#else
        // POSIX has no SEH. Preserve the recoverable C++-exception boundary.
        // fatal signals remain the platform crash handler's responsibility.
    } catch (...) {
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     "Safe CShuttleReleaseVerifier Saved server... T_T")
        // L2_LOG_ERROR(Log,
        //     "Safe CShuttleReleaseVerifier Saved server... T_T")
        L2_LOG_ERROR(Log, "Exception during shuttle release verification; cleanup may be incomplete.");
    }
}
