#include "AirShipAction.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "ThreadLocalWorldMap.h"
#include "WorldCollision.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

FVector AddVector(const FVector& left, const FVector& right) {
    return FVector(left.x + right.x, left.y + right.y, left.z + right.z);
}

FVector SubtractVector(const FVector& left, const FVector& right) {
    return FVector(left.x - right.x, left.y - right.y, left.z - right.z);
}


} // namespace

int AltitudeHelper::AdjustAltitude(int orgAltitude) {
    L2_GUARD_FUNCTION();

    int altitude = (orgAltitude / ALTITUDE_DELTA) * ALTITUDE_DELTA;
    if (altitude < -1000) {
        altitude = -1000;
    }
    if (altitude > 6000) {
        altitude = 6000;
    }
    return altitude;
}

int AltitudeHelper::ChangeAltitude(int orgAltitude, AltitudeActionType action) {
    L2_GUARD_FUNCTION();

    const std::int32_t delta = runtime::MultiplyInt32Wrapping(static_cast<std::int32_t>(action), ALTITUDE_DELTA);
    return AdjustAltitude(runtime::AddInt32Wrapping(orgAltitude, delta));
}

bool AltitudeHelper::CanChangeAltitude(int orgAltitude, AltitudeActionType action) {
    L2_GUARD_FUNCTION();

    if (action == AAT_ASCENT) {
        return runtime::AddInt32Wrapping(orgAltitude, ALTITUDE_DELTA) <= 6000;
    }
    if (action == AAT_DESCENT) {
        return runtime::AddInt32Wrapping(orgAltitude, -ALTITUDE_DELTA) >= -1000;
    }
    return false;
}

int AltitudeHelper::GetSpeedByAltitude(double orgSpeed, int altitude) {
    L2_GUARD_FUNCTION();

    if (altitude < L1_ALTITUDE) {
        orgSpeed *= 1.1;
    }
    return runtime::Win64CvttDoubleToInt32(orgSpeed);
}

// The original shares one "mov eax, 1; ret" body for these three slots: doing
// nothing always goes on. Returning SAR_STOP made DoPendingAction recurse until
// the stack overflowed whenever a ship had nothing pending (for example after
// DoNothing, or a move to its current position).
SHIP_ACTION_RESULT CShipNothingAction::OnStart() { return SAR_GO; }

SHIP_ACTION_RESULT CShipNothingAction::OnTick() { return SAR_GO; }

SHIP_ACTION_RESULT CShipNothingAction::OnStopMove(bool) { return SAR_GO; }

void CShipNothingAction::OnEnd(bool) {}

CShipMoveAction::CShipMoveAction() :
    m_nTick(100),
    m_bArrived(true),
    m_bResetDest(false),
    m_bMoving(false),
    m_bIsBroadCast(false),
    m_StartTime(0),
    m_MoveTickCount(0),
    m_AltitudeAction(AAT_NONE) {}

CShipMoveAction& CShipMoveAction::operator=(const CShipMoveAction& other) {
    if (this != &other) {
        m_spShip = other.m_spShip.GetObjectW();
        m_DestPos = other.m_DestPos;
        m_vMidDest = other.m_vMidDest;
        m_nTick = other.m_nTick;
        m_bArrived = other.m_bArrived;
        m_bResetDest = other.m_bResetDest;
        m_bMoving = other.m_bMoving;
        m_bIsBroadCast = other.m_bIsBroadCast;
        m_StartTime = other.m_StartTime;
        m_MoveTickCount = other.m_MoveTickCount;
        m_AltitudeAction = other.m_AltitudeAction;
    }
    return *this;
}

CShipActionData::CShipActionData() : m_Nothing(), m_ActionType(ACT_SHIP_NOTHING), padding_6c{} {}

void CShipAction::AddTimer(int delayMilliseconds) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship != nullptr && ship->m_pController != nullptr) {
        ship->m_pController->AddTimerEx(delayMilliseconds);
    }
}

void CShipMoveAction::CalcMidDest(NaviInfo& navigation) {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_SHIPACTION__MOVE_CALC);

    CAirShip* const ship = m_spShip.GetAirShip();
    const double speed = ship == nullptr ? 0.0 : static_cast<double>(ship->m_Speed);
    if (ship == nullptr || speed <= 0.5) {
        navigation.midNaviPoint = navigation.startNaviPoint;
        navigation.distPassed = 0.0;
        navigation.nextTick = 100;
        navigation.blocked = true;
        navigation.bArrived = true;
        return;
    }

    bool distanceExhausted = false;
    CGeoData* const geo = CWorld::Inst().GetGeo();
    const bool moved = geo->MoveStraightFly(
        navigation.startNaviPoint, navigation.endNaviPoint, speed * 500.0 / 1000.0, navigation.midNaviPoint,
        distanceExhausted, navigation.height, false, navigation.distPassed
    );

    int nextTick = 500;
    if (!moved || !distanceExhausted) {
        const double distance = SubtractVector(navigation.midNaviPoint, navigation.startNaviPoint).Size();
        nextTick = static_cast<int>(distance * 1000.0 / speed);
        if (nextTick >= 501) {
            nextTick = 500;
        }
        if (nextTick < 100) {
            nextTick = 100;
        }
    }

    navigation.bArrived = !distanceExhausted || nextTick < 450;
    navigation.nextTick = nextTick;
    navigation.blocked = !moved;
}

SHIP_ACTION_RESULT CShipMoveAction::OnStart() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_SHIPACTION__MOVE_ONSTART);
    m_StartTime = runtime::GetTickCount();
    m_MoveTickCount = 0;
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship == nullptr) {
        return SAR_STOP;
    }
    if (m_DestPos.IsEqual(ship->GetPos())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AIRSHIP move to same pos(%d, %d, %d)",
        //     static_cast<int>(m_DestPos.x),
        //     static_cast<int>(m_DestPos.y),
        //     static_cast<int>(m_DestPos.z))
        // L2_LOG_ERROR(Log,
        //     L"AIRSHIP move to same pos(%d, %d, %d)",
        //     static_cast<int>(m_DestPos.x),
        //     static_cast<int>(m_DestPos.y),
        //     static_cast<int>(m_DestPos.z))
        // Noise: Moving to the current position is an ordinary stop/no-op request, not an error.
        ship->StopMove(false);
        return SAR_STOP;
    }
    m_bArrived = false;
    return SetMoving();
}

SHIP_ACTION_RESULT CShipMoveAction::OnTick() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_SHIPACTION__MOVE_ONTICK);
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return SAR_STOP;
    }

    ++m_MoveTickCount;
    const bool moved = CWorld::Inst().AirShipMove(ship, m_vMidDest);
    if (m_bResetDest) {
        m_bArrived = false;
    }
    if (m_bArrived) {
        return SAR_STOP;
    }
    if (moved) {
        return SetMoving();
    }
    ship->m_pController->ActionBroadcastStopMove(true);
    return SAR_STOP;
}

SHIP_ACTION_RESULT CShipMoveAction::OnStopMove(bool) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship != nullptr) {
        ship->m_pController->ActionBroadcastStopMove(true);
    }
    return SAR_STOP;
}

void CShipMoveAction::OnEnd(bool force) {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_SHIPACTION__MOVE_ONEND);
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship == nullptr) {
        return;
    }
    if (!force) {
        CWorld::Inst().AirShipMove(ship, ship->GetDetailLocation());
    }
    ship->m_bMoving = false;
    ship->m_vMovingDirection = FVector{};
    ship->m_nLastTick = runtime::GetTickCount();
    ship->m_nMoveTick = 0;
}

SHIP_ACTION_RESULT CShipMoveAction::SetMoving() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_SHIPACTION__MOVE_SETMOVING);
    CAirShip* const ship = m_spShip.GetAirShip();
    if (ship == nullptr || ship->m_const == nullptr || ship->m_const->m_CollisionList.empty()) {
        return SAR_STOP;
    }

    const FVector current = ship->GetPos();
    const double radians = std::atan2(m_DestPos.y - current.y, m_DestPos.x - current.x);
    const std::uint32_t heading = static_cast<std::uint32_t>(radians * 65535.0 / 6.283185307179586);
    const FVector axis{0.0, 0.0, 1.0};

    std::vector<NaviInfo> candidates;
    candidates.reserve(ship->m_const->m_CollisionList.size());
    std::size_t selectedIndex = 0;
    double selectedDistance = 10000.0;
    for (const CollisionData& collision : ship->m_const->m_CollisionList) {
        const FVector rotated = FVector::RotateAngleAxis(&collision.position, static_cast<int>(heading & 0xffffU), &axis);
        NaviInfo navigation{};
        navigation.startNaviPoint = AddVector(current, rotated);
        navigation.endNaviPoint = AddVector(m_DestPos, rotated);
        navigation.nextTick = 100;
        navigation.blocked = true;
        navigation.bArrived = true;
        CalcMidDest(navigation);
        candidates.push_back(navigation);

        const double distance = std::max(0.0, navigation.distPassed);
        if (selectedDistance - distance > static_cast<double>(1.0e-5F)) {
            selectedDistance = distance;
            selectedIndex = candidates.size() - 1;
        }
    }

    const NaviInfo& selected = candidates[selectedIndex];
    m_bArrived = selected.bArrived;
    m_nTick = selected.nextTick;
    m_vMidDest = AddVector(current, SubtractVector(selected.midNaviPoint, selected.startNaviPoint));

    const bool planeCollision = g_WorldPlaneCollision.CheckCollision(
        current, m_vMidDest, selected.radius, m_vMidDest, static_cast<int>(ship->GetInZoneID()), false
    );
    if (selected.blocked || planeCollision) {
        m_DestPos = m_vMidDest;
        m_bArrived = true;
        m_bResetDest = true;
    }

    if (m_bResetDest) {
        if (current.DistCube(m_DestPos) <= 0.1) {
            ship->StopMove(false);
        } else {
            m_nTick = std::min(500, m_nTick);
            if (m_bIsBroadCast) {
                ship->m_pController->ActionBroadcastMove(current, m_DestPos);
                ship->m_vMovingDest = m_DestPos;
            }
        }
        m_bResetDest = false;
    }

    const double oneTickDistance = static_cast<double>(ship->m_Speed) * 500.0 / 1000.0;
    if (current.DistCube(m_DestPos) < oneTickDistance * oneTickDistance) {
        m_bArrived = true;
    }

    if (m_bIsBroadCast) {
        const FVector direction = SubtractVector(m_vMidDest, current);
        if (static_cast<int>(direction.x) != 0 || static_cast<int>(direction.y) != 0) {
            ship->SetDir(FAngle{static_cast<std::int32_t>(heading & 0xffffU)});
        }
        ship->m_bMoving = true;
        ship->m_vMovingDirection = direction;
        ship->m_nLastTick = runtime::GetTickCount();
        ship->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    }
    AddTimer(m_nTick);
    return SAR_GO;
}

CShipController::__async_arg_Move::__async_arg_Move() : __a2(false) {}

CShipController::__async_arg_Move::__async_arg_Move(FVector destination, bool broadcast) :
    __a1(destination), __a2(broadcast) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CShipController, __async_arg_Move)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CShipController, __async_arg_Ascent)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CShipController, __async_arg_Descent)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CShipController, __async_arg_DoNothing)

CShipController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired() : __a1(0) {}

CShipController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired(int actionNumber) : __a1(actionNumber) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CShipController, __async_arg_AsyncTimerExpired)

L2_DEFINE_OBJECT_CLASS_ID(CShipController)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CShipController)

L2_DEFINE_OBJECT_ALLOCATOR(CShipController, __GetClassName())

L2_DEFINE_OBJECT_NEW(CShipController)

CShipController::CShipController(CAirShip* airShip) :
    m_Action{},
    m_ForFreeAirShip(airShip != nullptr && airShip->m_const != nullptr && airShip->m_const->m_Type != ASTE_REGULAR),
    padding_1739{},
    m_nActionNumber(0),
    m_spMyShip(airShip) {
    L2_GUARD_FUNCTION();
    for (CShipActionData& action : m_Action) {
        action.m_ActionType = ACT_SHIP_NOTHING;
        action.m_Nothing.m_spShip = airShip;
        action.m_Move.m_spShip = airShip;
    }
    Serializer.SetOwnerName(u"AirShip");
}

CShipController::~CShipController() { m_spMyShip = static_cast<CObject*>(nullptr); }

L2_DEFINE_OBJECT_RELEASE(CShipController)

void CShipController::AddTimerEx(std::int64_t delayMilliseconds) {
    L2_GUARD_FUNCTION();
    AddTimer(delayMilliseconds, ++m_nActionNumber);
}

CShipAction* CShipController::GetCurrentAction() {
    if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_NOTHING) {
        return &m_Action[AO_CURRENT_ACTION].m_Nothing;
    }
    if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_MOVE) {
        return &m_Action[AO_CURRENT_ACTION].m_Move;
    }
    return nullptr;
}

bool CShipController::IsChangingAltitude() {
    L2_GUARD_FUNCTION();
    return m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_MOVE &&
        m_Action[AO_CURRENT_ACTION].m_Move.m_AltitudeAction != AAT_NONE;
}

void CShipController::SetAsMove(ActionOrder order, FVector destination, bool broadcast) {
    CShipActionData& action = m_Action[static_cast<int>(order)];
    action.m_ActionType = ACT_SHIP_MOVE;
    action.m_Move.m_spShip = m_spMyShip.GetObjectW();
    action.m_Move.m_bResetDest = true;
    action.m_Move.m_DestPos = destination;
    action.m_Move.m_AltitudeAction = AAT_NONE;
    action.m_Move.m_bIsBroadCast = broadcast;
}

void CShipController::DoPendingAction(int depth) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    CShipAction* const current = GetCurrentAction();
    current->OnEnd(true);

    m_Action[AO_CURRENT_ACTION].m_ActionType = m_Action[AO_PENDING_ACTION].m_ActionType;
    if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_NOTHING) {
        m_Action[AO_CURRENT_ACTION].m_Nothing.m_spShip = m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip.GetObjectW();
    } else if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_MOVE) {
        m_Action[AO_CURRENT_ACTION].m_Move = m_Action[AO_PENDING_ACTION].m_Move;
    }

    m_Action[AO_PENDING_ACTION].m_ActionType = ACT_SHIP_NOTHING;
    m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
    CShipAction* const next = GetCurrentAction();
    if (next->OnStart() == SAR_STOP) {
        if (depth > 1) {
            m_Action[AO_PENDING_ACTION].m_ActionType = ACT_SHIP_NOTHING;
            m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
        }
        DoPendingAction(runtime::AddInt32Wrapping(depth, 1));
    }
}

void CShipController::DoFirstAction() {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    CShipAction* const current = GetCurrentAction();
    if (current != nullptr && current->OnStart() == SAR_STOP) {
        DoPendingAction(0);
    }
}

void CShipController::ActionBroadcastStopMove(bool currentPosition) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr) {
        return;
    }
    const FVector position = currentPosition ? ship->GetPos() : ship->GetDetailLocation();
    g_ptlUserMap->BroadcastToNeighborDist(
        ship, 0x80000, position, 0x1000, "chddddd", 0xfe, 0x67, static_cast<int>(ship->m_nId), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), static_cast<int>(ship->GetDir().Yaw & 0xffff)
    );
}

void CShipController::ActionBroadcastMove(FVector source, FVector destination) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr) {
        return;
    }
    g_ptlUserMap->BroadcastToNeighborDist(
        ship, 0x80000, ship->GetPos(), 0x1000, "chddddddd", 0xfe, 0x66, static_cast<int>(ship->m_nId),
        static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z),
        static_cast<int>(source.x), static_cast<int>(source.y), static_cast<int>(source.z)
    );
}

void CShipController::RunMove(CShipController* owner, CExecutionArgumentPad* argument) { owner->__async_Move(argument); }

void CShipController::RunAscent(CShipController* owner, CExecutionArgumentPad* argument) { owner->__async_Ascent(argument); }

void CShipController::RunDescent(CShipController* owner, CExecutionArgumentPad* argument) {
    owner->__async_Descent(argument);
}

void CShipController::RunDoNothing(CShipController* owner, CExecutionArgumentPad* argument) {
    owner->__async_DoNothing(argument);
}

void CShipController::RunAsyncTimerExpired(CShipController* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTimerExpired(argument);
}

void CShipController::Move(FVector destination, bool broadcast) {
    __async_arg_Move argument(destination, broadcast);
    Serializer.AddExecutable(this, &CShipController::RunMove, &argument, L"Move");
}

void CShipController::Ascent() {
    __async_arg_Ascent argument;
    Serializer.AddExecutable(this, &CShipController::RunAscent, &argument, L"Ascent");
}

void CShipController::Descent() {
    __async_arg_Descent argument;
    Serializer.AddExecutable(this, &CShipController::RunDescent, &argument, L"Descent");
}

void CShipController::DoNothing() {
    __async_arg_DoNothing argument;
    Serializer.AddExecutable(this, &CShipController::RunDoNothing, &argument, L"DoNothing");
}

void CShipController::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    __async_arg_AsyncTimerExpired argument(id);
    Serializer.AddExecutable(this, &CShipController::RunAsyncTimerExpired, &argument, L"AsyncTimerExpired");
}

void CShipController::__async_AsyncTimerExpired(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    std::int32_t actionNumber = 0;
    std::memcpy(&actionNumber, argument->Buffer + 0x08, sizeof(actionNumber));
    if (m_nActionNumber != actionNumber) {
        return;
    }
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        m_Action[AO_CURRENT_ACTION].m_ActionType = ACT_SHIP_NOTHING;
        m_Action[AO_CURRENT_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
        return;
    }
    CShipAction* const current = GetCurrentAction();
    if (current != nullptr && current->OnTick() == SAR_STOP) {
        DoPendingAction(0);
    }
}

void CShipController::__async_Move(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    FVector destination;
    bool broadcast = false;
    std::memcpy(&destination, argument->Buffer + 0x08, sizeof(destination));
    std::memcpy(&broadcast, argument->Buffer + 0x20, sizeof(broadcast));
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }

    const ShipActionType currentType = m_Action[AO_CURRENT_ACTION].m_ActionType;
    if (currentType == ACT_SHIP_MOVE && m_ForFreeAirShip) {
        CShipMoveAction& move = m_Action[AO_CURRENT_ACTION].m_Move;
        destination.z = static_cast<double>(static_cast<int>(move.m_DestPos.z));
        move.m_bResetDest = true;
        move.m_DestPos = destination;
        move.m_bIsBroadCast = true;
        return;
    }

    CShipAction* const current = GetCurrentAction();
    if (current != nullptr) {
        current->OnEnd(false);
    }
    SetAsMove(AO_CURRENT_ACTION, destination, broadcast);
    m_Action[AO_PENDING_ACTION].m_ActionType = ACT_SHIP_NOTHING;
    m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
    DoFirstAction();
}

void CShipController::__async_Ascent(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    const bool startsFromNothing = m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_NOTHING;
    if (startsFromNothing) {
        SetAsMove(AO_CURRENT_ACTION, ship->GetPos(), true);
    }
    if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_MOVE) {
        CShipMoveAction& move = m_Action[AO_CURRENT_ACTION].m_Move;
        move.m_AltitudeAction = AAT_ASCENT;
        move.m_bResetDest = true;
        move.m_DestPos.z =
            static_cast<double>(AltitudeHelper::ChangeAltitude(static_cast<int>(move.m_DestPos.z), AAT_ASCENT));
        if (startsFromNothing) {
            m_Action[AO_PENDING_ACTION].m_ActionType = ACT_SHIP_NOTHING;
            m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
            DoFirstAction();
        }
    }
}

void CShipController::__async_Descent(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    const bool startsFromNothing = m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_NOTHING;
    if (startsFromNothing) {
        SetAsMove(AO_CURRENT_ACTION, ship->GetPos(), true);
    }
    if (m_Action[AO_CURRENT_ACTION].m_ActionType == ACT_SHIP_MOVE) {
        CShipMoveAction& move = m_Action[AO_CURRENT_ACTION].m_Move;
        move.m_AltitudeAction = AAT_DESCENT;
        move.m_bResetDest = true;
        move.m_DestPos.z =
            static_cast<double>(AltitudeHelper::ChangeAltitude(static_cast<int>(move.m_DestPos.z), AAT_DESCENT));
        if (startsFromNothing) {
            m_Action[AO_PENDING_ACTION].m_ActionType = ACT_SHIP_NOTHING;
            m_Action[AO_PENDING_ACTION].m_Nothing.m_spShip = m_spMyShip.GetObjectW();
            DoFirstAction();
        }
    }
}

void CShipController::__async_DoNothing(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    CAirShip* const ship = m_spMyShip.GetAirShip();
    if (ship == nullptr || !ship->m_data->m_bIsInWorld) {
        return;
    }
    CShipAction* const current = GetCurrentAction();
    if (current != nullptr) {
        current->OnEnd(false);
    }
    for (CShipActionData& action : m_Action) {
        action.m_ActionType = ACT_SHIP_NOTHING;
        action.m_Nothing.m_spShip = m_spMyShip.GetObjectW();
    }
    DoFirstAction();
    ship->StopMove(false);
    ActionBroadcastStopMove(true);
}
