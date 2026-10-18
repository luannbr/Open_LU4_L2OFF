#include "Vehicle.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TextRuntime.h"

#include "CWorld.h"
#include "Item.h"
#include "NewActionMove.h"
#include "ObjectDB.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"
#include "VehicleDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {


volatile std::int32_t g_VehicleObjectCount = 0;


void CopyVehicleName(char16_t (&destination)[128], const char16_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    std::size_t length = 0;
    while (source[length] != u'\0') {
        ++length;
    }
    if (length > 127) {
        source = u"";
        length = 0;
    }
    std::copy_n(source, length, destination);
    destination[length] = u'\0';
}

FVector AddVehicleVector(const FVector& left, const FVector& right) {
    return FVector{left.x + right.x, left.y + right.y, left.z + right.z};
}

} // namespace

CVehicleSmartIdManager g_VehicleSmartIdManager;

L2_DEFINE_OBJECT_CLASS_ID(CVehicleStationInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CVehicleStationInfo)

L2_DEFINE_OBJECT_ALLOCATOR(CVehicleStationInfo, __GetClassName())

L2_DEFINE_OBJECT_NEW(CVehicleStationInfo)

CVehicleStationInfo::CVehicleStationInfo() : m_nStationID(0), m_alignment54(0) {}

CVehicleStationInfo::~CVehicleStationInfo() = default;

L2_DEFINE_OBJECT_RELEASE(CVehicleStationInfo)

void CVehicleStationInfo::AddActionInfo(VehicleActionInfo* action) { m_vAction.push_back(action); }

L2_DEFINE_OBJECT_CLASS_ID(CVehicleController)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CVehicleController)

L2_DEFINE_OBJECT_ALLOCATOR(CVehicleController, __GetClassName())

L2_DEFINE_OBJECT_NEW(CVehicleController)

CVehicleController::CVehicleController() :
    m_pVehicle(nullptr),
    m_pCaptain(nullptr),
    m_nCaptainID(0),
    m_nCurrentStation(0),
    m_nCurrentAction(0),
    m_alignment4c(0),
    m_nLastErrorCode(0) {
    L2_GUARD_FUNCTION();
}

CVehicleController::~CVehicleController() = default;

L2_DEFINE_OBJECT_RELEASE(CVehicleController)

void CVehicleController::AddStation(CVehicleStationInfo* station) {
    L2_GUARD_FUNCTION();
    m_vStation.push_back(station);
}

void CVehicleController::SetVehicle(CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    m_pVehicle = vehicle;
    m_nCurrentStation = static_cast<int>(m_vStation.size()) - 1;
}

CVehicle* CVehicleController::GetVehicle() const noexcept { return m_pVehicle; }

void CVehicleController::SetCaptainId(int captainId, FVector relativePosition) {
    m_nCaptainID = captainId;
    m_vCaptainRelativePos = relativePosition;
}

void CVehicleController::SetBroadcastPoint(FVector first, FVector second) {
    L2_GUARD_FUNCTION();
    m_vBroadcastPoint.push_back(first);
    if (!first.IsEqual(second)) {
        m_vBroadcastPoint.push_back(second);
    }
}

int CVehicleController::GetNextStation() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(static_cast<std::int64_t>(m_nCurrentStation + 1) % static_cast<std::int64_t>(m_vStation.size()));
}

void CVehicleController::Start() {
    L2_GUARD_FUNCTION();
    m_pVehicle->SetMoveSpeed(0);
    std::optional<FVector> currentStorage;
    std::optional<FVector> destinationStorage;
    {
        L2_GUARD(m_ControllerLock);
        m_nCurrentStation = GetNextStation();
        const FVector& current = currentStorage.emplace(m_vStation[static_cast<std::size_t>(m_nCurrentStation)]->m_vPoint);
        const FVector& destination =
            destinationStorage.emplace(m_vStation[static_cast<std::size_t>(GetNextStation())]->m_vPoint);
        m_pVehicle->SetPos(current);
        m_pVehicle->SetOrigin(current);
        m_pVehicle->SetDestination(destination);
        m_nCurrentAction = 0;
    }
    AddTimer(0, 0);
}

bool CVehicleController::ReadyToDepartureAction() {
    L2_GUARD_FUNCTION();
    CVehicleStationInfo* const station = m_vStation[static_cast<std::size_t>(m_nCurrentStation)];
    if (station == nullptr || station->m_nStationID == 0) {
        return false;
    }
    if (static_cast<std::size_t>(m_nCurrentAction) < station->m_vAction.size() &&
        station->m_vAction[static_cast<std::size_t>(m_nCurrentAction)]->m_eType == VATE_DEPARTURE) {
        return true;
    }
    for (std::size_t index = 0; index < station->m_vAction.size(); ++index) {
        if (station->m_vAction[index]->m_eType == VATE_DEPARTURE) {
            m_nCurrentAction = static_cast<int>(index);
            return true;
        }
    }
    return false;
}

void CVehicleController::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    bool replayAction = id == REPLAY_ACTION;
    std::optional<FVector> positionStorage;
    L2_GUARD(m_ControllerLock);

    while (true) {
        if (m_pVehicle == nullptr || !m_pVehicle->m_bIsDriving) {
            return;
        }
        CVehicleStationInfo* const station = m_vStation[static_cast<std::size_t>(m_nCurrentStation)];
        if (station == nullptr) {
            return;
        }
        if (replayAction) {
            --m_nCurrentAction;
        }
        if (m_nCurrentAction < 0 || static_cast<std::size_t>(m_nCurrentAction) >= station->m_vAction.size()) {
            return;
        }

        VehicleActionInfo* const action = station->m_vAction[static_cast<std::size_t>(m_nCurrentAction++)];
        if (action == nullptr) {
            return;
        }

        const FVector& position = positionStorage.emplace(m_pVehicle->GetPos());
        switch (action->m_eType) {
            case VATE_MESSAGE:
                break;
            case VATE_DEPARTURE: {
                const int nextStation = GetNextStation();
                const int nextStationId = m_vStation[static_cast<std::size_t>(nextStation)]->m_nStationID;
                if (nextStationId > 0 && !g_VehicleDB.ReserveStation(nextStationId)) {
                    AddTimer(10000, REPLAY_ACTION);
                    return;
                }
                if (nextStationId > 0) {
                    replayAction = false;
                }
                if (station->m_nStationID > 0) {
                    g_VehicleDB.ReleaseStation(station->m_nStationID);
                }
                m_pVehicle->OnStart();
                m_pVehicle->SetMoveSpeed(action->m_nMovingSpeed);
                m_pVehicle->SetRotationSpeed(action->m_nRotatingSpeed);
                m_pVehicle->SendVehicleInfo(nullptr);
                break;
            }
            case VATE_CHECK_TICKET:
                if (action->m_nTicketID != 0) {
                    m_pVehicle->KickIllegal(action->m_nTicketID, action->m_vBanningPos);
                }
                m_pVehicle->InformVehicleStart(true);
                break;
            case VATE_WAIT:
                break;
            case VATE_SET_KICK_POS:
                m_pVehicle->InformVehicleStart(false);
                m_pVehicle->m_vKickPos = action->m_vBanningPos;
                break;
            case VATE_PLAY_SOUND: {
                FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                    const FVector userPosition = user->GetPos();
                    const double dx = userPosition.x - position.x;
                    const double dy = userPosition.y - position.y;
                    if (dx * dx + dy * dy < 1073741824.0) {
                        const std::wstring sound = target_utf16::ToHostCodeUnits(action->m_sFileName);
                        user->PlaySound(
                            sound.c_str(), 1, static_cast<int>(m_pVehicle->Id()),
                            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                            runtime::Win64CvttDoubleToInt32(position.z)
                        );
                    }
                });
                break;
            }
            default:
                return;
        }

        if (action->m_nNextActionTime < 0) {
            positionStorage.reset();
            continue;
        }
        if (action->m_nNextActionTime < 80) {
            positionStorage.reset();
            continue;
        }
        AddTimer(action->m_nNextActionTime, 0);
        return;
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CVehicle)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CVehicle)

L2_DEFINE_OBJECT_ALLOCATOR(CVehicle, __GetClassName())

L2_DEFINE_OBJECT_NEW(CVehicle)

CVehicle::CVehicle() :
    m_pController(nullptr),
    m_pContainer(nullptr),
    m_MoveSpeed(0),
    m_Direction(0),
    m_RotationSpeed(0),
    m_bIsDriving(false),
    m_bWaitingStart(false),
    m_bWaitingStop(false),
    padding_bf{},
    m_Name{} {
    L2_GUARD_FUNCTION();
    const int index = runtime::raw_atomic::IncrementI32(&g_VehicleObjectCount);
    m_nId = g_VehicleSmartIdManager.RegisterObject(
        // 10,
        SCLS_VEHICLE, static_cast<std::uint32_t>(index), this
    );
}

CVehicle::CVehicle(const CVehicle& other) :
    CAbstractCarrier(other),
    m_pController(other.m_pController),
    m_pContainer(nullptr),
    m_Pos(other.m_Pos),
    m_Origin(other.m_Origin),
    m_Destination(other.m_Destination),
    m_MoveSpeed(0),
    m_Direction(0),
    m_RotationSpeed(0),
    m_bIsDriving(false),
    m_bWaitingStart(false),
    m_bWaitingStop(false),
    padding_bf{},
    m_Name{} {
    L2_GUARD_FUNCTION();
    const int index = runtime::raw_atomic::IncrementI32(&g_VehicleObjectCount);
    m_nId = g_VehicleSmartIdManager.RegisterObject(
        // 10,
        SCLS_VEHICLE, static_cast<std::uint32_t>(index), this
    );
}

L2_DEFINE_OBJECT_RELEASE(CVehicle)

CVehicle* CVehicle::GetObjectW(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    return g_VehicleSmartIdManager.GetObjectW(sid);
}

CVehicle* CVehicle::FindObject(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    CVehicle* const vehicle = GetObjectW(sid);
    if (vehicle != nullptr) {
        L2_ADD_REF(vehicle, ObjectRefType::FindObject);
    }
    return vehicle;
}

bool CVehicle::IsVehicle() {
    // Current CVehicle vtable +0x118 targets the 0x004c0008 true thunk.
    return true;
}

CVehicle* CVehicle::CastVehicle() {
    // Current CVehicle vtable +0x138 targets the 0x006ed9c4 identity thunk.
    return this;
}

::CSPointer<CVehicle> CVehicle::FindObjectSP(std::uint32_t sid) {
    L2_GUARD_FUNCTION();
    CSPointer<CVehicle> vehicle;
    vehicle.SetObject(GetObjectW(sid), ObjectRefType::FindObject);
    return vehicle;
}

bool CVehicle::IsEnemyTo(CCreature*) { return false; }

bool CVehicle::IsAlive() { return true; }

void CVehicle::InSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class Filter final : public CContainerFilter {
    public:
        bool CanDo(CObject* candidate) override { return candidate->IsPC(); }
    } filter;

    class Function final : public CContainerFunc {
    public:
        explicit Function(CObject* object) : object_(object) {}

        void Do(CObject* candidate) override {
            if ((candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) ||
                (object_->IsPC() && (object_->IsDeleting() || object_->m_Deleted != 0))) {
                return;
            }
            candidate->CastUser()->InSight(object_, false);
        }

    private:
        CObject* object_;
    } function(object);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

void CVehicle::OutOfSight(CObject* object, bool flag) {
    L2_GUARD_FUNCTION();
    if (object == nullptr || object == this) {
        return;
    }

    class Filter final : public CContainerFilter {
    public:
        bool CanDo(CObject* candidate) override { return candidate->IsPC(); }
    } filter;

    class Function final : public CContainerFunc {
    public:
        Function(CObject* object, bool flag) : object_(object), flag_(flag) {}

        void Do(CObject* candidate) override {
            if ((candidate->IsPC() && (candidate->IsDeleting() || candidate->m_Deleted != 0)) ||
                (object_->IsPC() && (object_->IsDeleting() || object_->m_Deleted != 0))) {
                return;
            }
            candidate->CastUser()->OutOfSight(object_, flag_);
        }

    private:
        CObject* object_;
        bool flag_;
    } function(object, flag);

    FOR_ALL_OBJECT_IN_CONTAINER__INSTANT(m_pContainer, filter, function);
}

CObject* CVehicle::Copy() { return new CVehicle(*this); }

FVector CVehicle::ConvRelToAbs(FVector position) const {
    L2_GUARD_FUNCTION();
    const FVector axis{0.0, 0.0, 1.0};
    FVector rotated = FVector::RotateAngleAxis(&position, m_Direction + 0x4000, &axis);
    rotated.x += m_Pos.x;
    rotated.y += m_Pos.y;
    rotated.z += m_Pos.z;
    return rotated;
}

FVector CVehicle::ConvAbsToRel(FVector position) const {
    L2_GUARD_FUNCTION();
    const FVector translated{position.x - m_Pos.x, position.y - m_Pos.y, position.z - m_Pos.z};
    const FVector axis{0.0, 0.0, 1.0};
    return FVector::RotateAngleAxis(&translated, -0x4000 - m_Direction, &axis);
}

FVector CVehicle::GetPos() const { return m_Pos; }

FAngle CVehicle::GetDir() const {
    FAngle result;
    result.Yaw = m_Direction;
    return result;
}

int CVehicle::GetIndex() const { return m_pContainer->m_Data.m_nIndex; }

void CVehicle::SetPos(const FVector& position) { m_Pos = position; }

bool CVehicle::SetMovingIntoCarrier(CMoveRelativeAction* action) {
    L2_GUARD_FUNCTION();
    return action->SetMovingIntoVehicleImpl();
}

int CVehicle::GetNewDirection(const FVector& position, int elapsedMilliseconds) {
    L2_GUARD_FUNCTION();
    const int maximumDelta = runtime::Win64CvttDoubleToInt32(
        static_cast<double>(m_RotationSpeed) * static_cast<double>(elapsedMilliseconds) / 1000.0
    );
    const int desired =
        runtime::Win64CvttDoubleToInt32(std::atan2(position.y, position.x) * 65535.0 / 6.283185307) & 0xffff;
    int delta = desired - m_Direction;
    if (delta > 0x8000) {
        delta -= 0x10000;
    }
    if (delta < -0x7fff) {
        delta += 0x10000;
    }
    if (maximumDelta < delta) {
        return m_Direction + maximumDelta;
    }
    if (delta < -maximumDelta) {
        return m_Direction - maximumDelta;
    }
    return desired;
}

FVector CVehicle::GetAdjustPos(double distance, FVector position) {
    L2_GUARD_FUNCTION();
    const FVector current = GetPos();
    const double dx = m_Destination.x - current.x;
    const double dy = m_Destination.y - current.y;
    if (distance * distance <= dx * dx + dy * dy) {
        const double radians = static_cast<double>(m_Direction) * 6.283185307 / 65535.0;
        position = FVector{std::cos(radians) * distance, std::sin(radians) * distance, 0.0};
    }
    return AddVehicleVector(GetPos(), position);
}

void CVehicle::CalcDir() {
    const double radians = std::atan2(m_Destination.y - m_Origin.y, m_Destination.x - m_Origin.x);
    m_Direction = runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.283185307179586) & 0xffff;
}

bool CVehicle::HasArrived() {
    const FVector current = GetPos();
    const double dx = current.x - m_Destination.x;
    const double dy = current.y - m_Destination.y;
    return dx * dx + dy * dy < 100.0;
}

void CVehicle::KickTransformer() {
    L2_GUARD_FUNCTION();
    CWorld::Inst().VehicleKickTransformer(this);
}

void CVehicle::InformVehicleStart(bool start) {
    L2_GUARD_FUNCTION();
    CWorld::Inst().VehicleInformVehicleStart(this, start);
}

void CVehicle::KickIllegal(int index, FVector position) {
    L2_GUARD_FUNCTION();
    CWorld::Inst().VehicleKickIllegal(this, index, position);
}

void CVehicle::OnStart() {
    L2_GUARD_FUNCTION();
    if (!m_vKickPos.IsEqual(FVector{})) {
        KickTransformer();
    }
}

bool CVehicle::QuickDeparture() {
    L2_GUARD_FUNCTION();
    if (m_pController != nullptr && m_pController->ReadyToDepartureAction()) {
        m_pController->TimerExpired(0);
        return true;
    }
    return false;
}

void CVehicle::StartDrive() {
    L2_GUARD_FUNCTION();
    m_bIsDriving = false;
    m_bWaitingStart = true;
    m_bWaitingStop = false;
    m_pContainer = CContainerDB::Inst().AllocVehicleContainer(static_cast<int>(m_nId));
    AddTimer(666, 0);
}

void CVehicle::ProcessStopDrive() {
    L2_GUARD_FUNCTION();
    static_cast<void>(GetPos());
    CCreatureFactory& factory = CCreatureFactory::Inst();
    int index = m_pContainer->m_Data.m_nCreatureFirst;
    while (index != -1) {
        const std::uint32_t smartId = static_cast<std::uint32_t>(factory.GetIdByIndex(index));
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(smartId);
        if (creature == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid creature at CVehicle::StopDrive()")
            // L2_LOG_ERROR(Log,
            //     L"invalid creature at CVehicle::StopDrive()")
            L2_LOG_ERROR(
                Log,
                "Vehicle %u stop-drive found no creature for factory index %d, smart ID %u; passenger processing stops.",
                static_cast<unsigned>(m_nId), index, static_cast<unsigned>(smartId)
            );
            break;
        }

        L2_ADD_REF(creature, ObjectRefType::FindObject);
        if (!creature->m_data->m_bIsActive) {
            // Log.Add(LOG_ERROR, L"inactive creature in vehicle!")
            // L2_LOG_ERROR(Log, L"inactive creature in vehicle!")
            L2_LOG_ERROR(
                Log,
                "Vehicle %u contains inactive creature %u at factory index %d; this iteration does not advance the "
                "passenger index.",
                static_cast<unsigned>(m_nId), static_cast<unsigned>(smartId), index
            );
        } else {
            const FVector creaturePosition = creature->GetPos();
            CWorld::Inst().GetOffVehicle(creature, this, creaturePosition, true);
            index = factory.GetNextIndex(index);
        }
        L2_RELEASE(creature, ObjectRefType::FindObject);
    }

    CWorld::Inst().VehicleLeaveWorld(this);
    CContainerDB::Inst().FreeVehicleContainer(m_pContainer);
}

void CVehicle::StopDrive() {
    L2_GUARD_FUNCTION();
    m_bWaitingStart = false;
    m_bWaitingStop = true;
    if (m_pContainer != nullptr) {
        ProcessStopDrive();
    }
}

void CVehicle::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (!m_bIsDriving) {
        if (m_bWaitingStart) {
            m_bIsDriving = true;
            m_bWaitingStart = false;
            m_vKickPos = GetPos();
            m_pController->Start();
            CalcDir();
            CWorld::Inst().VehicleEnterWorld(this, GetPos());
            AddTimer(666, 0);
        }
        return;
    }

    if (m_bWaitingStop) {
        m_bWaitingStop = false;
        m_bIsDriving = false;
        return;
    }

    if (m_MoveSpeed == 0) {
        AddTimer(666, 0);
        return;
    }

    const FVector current = GetPos();
    const FVector relative{m_Destination.x - current.x, m_Destination.y - current.y, m_Destination.z - current.z};
    const int direction = GetNewDirection(relative, 666);
    if (direction != m_Direction) {
        m_Direction = direction;
    }
    const FVector adjusted = GetAdjustPos(static_cast<double>(m_MoveSpeed) * 666.0 / 1000.0, relative);
    CWorld::Inst().VehicleMove(this, adjusted);
    if (HasArrived()) {
        if (m_pController == nullptr) {
            m_MoveSpeed = 0;
        } else {
            m_pController->Start();
        }
        SendVehicleInfo(nullptr);
    }
    AddTimer(666, 0);
}

void CVehicle::SeeDropItem(CItem* item, CCreature* dropper) {
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
            if (m_dropper->IsPC() && (m_dropper->IsDeleting() || m_dropper->m_Deleted != 0)) {
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

void CVehicle::SendVehicleInfo(CUserSocket* socket) {
    // Current L2Server.exe 0x00c44080.
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);

    if (socket == nullptr) {
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, position, "cddddd", 0x60, static_cast<int>(m_nId), x, y, z, m_Direction
        );
        if (m_MoveSpeed > 0) {
            g_ptlUserMap->BroadcastToNeighbor(
                *this, 0x80000, position, "cdddddd", 0x6c, static_cast<int>(m_nId), m_MoveSpeed, m_RotationSpeed,
                runtime::Win64CvttDoubleToInt32(m_Destination.x), runtime::Win64CvttDoubleToInt32(m_Destination.y),
                runtime::Win64CvttDoubleToInt32(m_Destination.z)
            );
        }
        return;
    }

    socket->Send("cddddd", 0x60, static_cast<int>(m_nId), x, y, z, m_Direction);
    if (m_MoveSpeed > 0) {
        socket->Send(
            "cdddddd", 0x6c, static_cast<int>(m_nId), m_MoveSpeed, m_RotationSpeed,
            runtime::Win64CvttDoubleToInt32(m_Destination.x), runtime::Win64CvttDoubleToInt32(m_Destination.y),
            runtime::Win64CvttDoubleToInt32(m_Destination.z)
        );
    }
}

CVehicleDB::CVehicleDB() : m_nStationReserved{} {
    L2_GUARD_FUNCTION();
    m_VehicleIDMap.clear();
    ClearStation();
}

void CVehicleDB::AddVehicle(const char16_t* name, CVehicle* vehicle) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_VehicleMapSect);
        CopyVehicleName(vehicle->m_Name, name);
        m_VehicleIDMap.emplace(static_cast<int>(vehicle->Id()), vehicle);
    }
}

bool CVehicleDB::ReserveStation(int stationId) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(stationId - 1) >= 100U) {
        return false;
    }
    return runtime::raw_atomic::CompareExchangeI32(&m_nStationReserved[stationId], 1, 0) == 0;
}

bool CVehicleDB::ReleaseStation(int stationId) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(stationId - 1) >= 100U) {
        return false;
    }
    return runtime::raw_atomic::CompareExchangeI32(&m_nStationReserved[stationId], 0, 1) == 1;
}

void CVehicleDB::ClearStation() {
    L2_GUARD_FUNCTION();
    std::memset(m_nStationReserved, 0, 400);
}

void CVehicleDB::Load() {
    L2_GUARD_FUNCTION();
    CParserForVehicle parser;
#if defined(_WIN32)
    constexpr const wchar_t* path = L"..\\Script\\vehicledata.txt";
#else
    constexpr const wchar_t* path = L"../script/vehicledata.txt";
#endif
    if (!parser.Create(path)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForVehicle::create failed",
        //     L"Vehicle.cpp",
        //     0x3f8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForVehicle::create failed",
        //     L"Vehicle.cpp",
        //     0x3f8)
        L2_LOG_ERROR(Log, "Cannot initialize the vehicledata.txt parser or input; vehicle loading stops.");
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing VehicleData file failed after line[%d]",
        //     L"Vehicle.cpp",
        //     0x402,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing VehicleData file failed after line[%d]",
        //     L"Vehicle.cpp",
        //     0x402,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse vehicledata.txt at script line %lld; already inserted vehicle data is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

CVehicle* CVehicleDB::FindVehicle(int vehicleId) {
    L2_GUARD_FUNCTION();
    return CVehicle::FindObject(static_cast<std::uint32_t>(vehicleId));
}

void CVehicleDB::StartDrive() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_VehicleMapSect);
        for (const auto& entry : m_VehicleIDMap) {
            entry.second->StartDrive();
        }
    }
}

void CVehicleDB::QuickDeparture() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_VehicleMapSect);
        for (const auto& entry : m_VehicleIDMap) {
            entry.second->QuickDeparture();
        }
    }
}

void CVehicleDB::EraseAllVehicle() {
    L2_GUARD_FUNCTION();
    ::std::vector<CVehicle*> vehicles;
    {
        L2_GUARD(m_VehicleMapSect);
        for (const auto& entry : m_VehicleIDMap) {
            vehicles.push_back(entry.second);
            L2_ADD_REF(entry.second, ObjectRefType::Anonymous);
        }
    }

    for (CVehicle* vehicle : vehicles) {
        vehicle->StopDrive();
        L2_RELEASE(vehicle, ObjectRefType::Anonymous);
    }

    {
        L2_GUARD(m_VehicleMapSect);
        m_VehicleIDMap.clear();
    }
    ClearStation();
}

void CVehicleDB::Reload() {
    L2_GUARD_FUNCTION();
    EraseAllVehicle();
    Load();
    StartDrive();
}

CVehicleDB g_VehicleDB;
