#include "NewActionMove.h"

#include "AirShip.h"
#include "Area.h"
#include "CConfig.h"
#include "CWorld.h"
#include "ControlTower.h"
#include "Creature.h"
#include "CreatureController.h"
#include "Default.h"
#include "Door.h"
#include "Item.h"
#include "NPC.h"
#include "Path.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Portable/RuntimeTimingTrace.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "SharedContainer.h"
#include "Shuttle.h"
#include "SignBoard.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"
#include "Vehicle.h"
#include "WorldCollision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

const int CMoveActionBase::MoveTick = 800;

// Current L2Server newaction_move.obj byte 0x015d7a62. The image
// initializes collision checking and the builder command may toggle it.
bool g_CheckCollisionForNPCMove = true;
bool g_bWorldCollision = true;

CMoveActionBase::CMoveActionBase() :
    m_nTick(0), m_bMoving(false), m_bArrived(false) {}

CMoveAction::CMoveAction() :
    m_bPathFind(false),
    m_bPathFinding(false),
    m_bResetDest(false),
    m_nSkillId(0),
    m_nSkillLevel(0),
    m_nSkillSubLevel(0),
    m_bForceAttack(false),
    m_nUsedItemSID(0) {}

void CMoveActionBase::CalcMidDest(
    const FVector& destination,
    FVector targetPosition,
    double speed,
    FVector& middleDestination,
    int& tick,
    bool& arrived,
    bool& routeSuccessful,
    bool,
    CCreature* creature
) {
    L2_GUARD_FUNCTION();

    const double stepDistance = speed * 800.0 / 1000.0;
    bool distanceExhausted = false;
    if (speed <= 0.5) {
        tick = 100;
        middleDestination = destination;
    } else {
        double travelledDistance = 0.0;
        CGeoData* const geo = CWorld::Inst().GetGeo();
        int waterSurface = 0;

        if (!creature->m_bFloatingMoveMode) {
            if (static_cast<std::int32_t>(creature->m_data->m_moveType) == CMT_FLY) {
                if (!creature->IsRidingWyvern()) {
                    targetPosition.z = std::clamp(targetPosition.z, -1000.0, 6000.0);
                }
                routeSuccessful = geo->MoveStraightFly(
                    destination, targetPosition, stepDistance, middleDestination, distanceExhausted,
                    creature->GetCollisionHeight(), !creature->IsNPC(), travelledDistance
                );
            } else {
                routeSuccessful = geo->MoveStraight(
                    destination, targetPosition, stepDistance, middleDestination, distanceExhausted, travelledDistance
                );
            }

            if (CAreaDB::inst().IsUnderWater(middleDestination, waterSurface, static_cast<int>(creature->GetInZoneID()))) {
                routeSuccessful = geo->MoveStraightFloating(
                    destination, targetPosition, stepDistance, middleDestination, distanceExhausted, waterSurface
                );
                creature->m_bFloatingMoveMode = true;
            } else {
                creature->m_bFloatingMoveMode = false;
            }
        } else {
            routeSuccessful = geo->MoveStraightFloating(
                destination, targetPosition, stepDistance, middleDestination, distanceExhausted,
                creature->m_AreaMod.m_nWaterSurfaceHeight
            );
            creature->m_bFloatingMoveMode = true;
        }

        if (!routeSuccessful || !distanceExhausted) {
            const double distance = std::sqrt(destination.DistCube(middleDestination));
            tick = std::min(runtime::Win64CvttDoubleToInt32(distance * 1000.0 / speed), MoveTick);
        } else {
            tick = MoveTick;
        }
    }

    tick = std::max(tick, 100);
    if (!distanceExhausted || tick < 750) {
        arrived = true;
    }
}

bool CMoveAction::OnStart() {
    L2_GUARD_FUNCTION();

    CNewAction::OnStart();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVE_ONSTART);
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_action_start", actor, nullptr,
            "destination=%.3f,%.3f,%.3f pathfind=%d can_act=%d "
            "block_move=%d can_move=%d stop_mode=%d speed=%.6f",
            m_vDest.x, m_vDest.y, m_vDest.z, m_bPathFind ? 1 : 0, actor->m_SkillMod.m_bCanAct ? 1 : 0,
            actor->m_SkillMod.m_nBlockMoveCount, actor->m_data->m_bCanMove ? 1 : 0,
            static_cast<int>(actor->m_data->m_StopMode), actor->m_data->m_dParam[11]
        );
    }

    actor->m_spActionTarget = CSmartID();
    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove) {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_action_reject_state", actor, nullptr, "can_act=%d block_move=%d can_move=%d",
                actor->m_SkillMod.m_bCanAct ? 1 : 0, actor->m_SkillMod.m_nBlockMoveCount, actor->m_data->m_bCanMove ? 1 : 0
            );
        }
        return false;
    }

    const FVector currentPosition = actor->GetPos();
    if (actor->m_data->m_dParam[11] <= 0.0 || m_vDest == currentPosition) {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_action_reject_destination", actor, nullptr,
                "current=%.3f,%.3f,%.3f destination=%.3f,%.3f,%.3f "
                "speed=%.6f",
                currentPosition.x, currentPosition.y, currentPosition.z, m_vDest.x, m_vDest.y, m_vDest.z,
                actor->m_data->m_dParam[11]
            );
        }
        if (actor->IsPC() && actor->m_dSpeedModByWeight == 0.0) {
            actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_MOVE_INVENTORY_TOO_HEAVY);
        }
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }

    if (actor->m_data->m_StopMode != STP_SIT && actor->m_data->m_StopMode != STP_STORE) {
        m_bArrived = false;
        actor->m_data->m_MovingStatus = MSTE_MOVING;
        SetValidPendingAction(actor);
        actor->CheckShowMovePos(1, m_vDest);
        return SetMoving();
    }

    actor->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_action_reject_stop_mode", actor, nullptr, "stop_mode=%d", static_cast<int>(actor->m_data->m_StopMode)
        );
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    return false;
}

bool CMoveAction::OnStopMove() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr) {
        actor->GetController()->ActionBroadcastStopMove();
    }
    return false;
}

void CMoveAction::SetValidPendingAction(CCreature* creature) {
    L2_GUARD_FUNCTION();

    if (m_bArrived == true && !creature->m_data->m_bMoveTrouble && m_nSkillId > 0) {
        creature->GetController()->SetAsSkill(
            1, CSmartID(), m_nSkillId, m_nSkillLevel, m_nSkillSubLevel, m_bForceAttack, false,
            static_cast<unsigned int>(m_nUsedItemSID)
        );
    }
}

bool CMoveAction::SetMovingFlyingImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    bool routeSuccessful = true;
    m_bPathFinding = false;
    CalcMidDest(currentPosition, m_vDest, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);

    const bool collided = g_WorldPlaneCollision.CheckCollision(
        currentPosition, m_vMidDest, actor->GetCollisionRadius(), m_vMidDest, static_cast<int>(actor->GetInZoneID()), false
    );
    if (!routeSuccessful || collided) {
        m_vDest = m_vMidDest;
        m_bResetDest = true;
        m_bArrived = true;
    }

    if (m_bResetDest) {
        if (currentPosition.DistCube(m_vDest) <= 0.1) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
        } else {
            m_nTick = std::min(m_nTick, MoveTick);
            actor->GetController()->ActionBroadcastMove(currentPosition, m_vDest);
            actor->m_vMovingDest = m_vDest;
        }
        m_bResetDest = false;
    }

    if (std::sqrt(currentPosition.DistCube(m_vDest)) < speed * 800.0 / 1000.0) {
        m_bArrived = true;
    }

    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    if (!actor->m_vMovingDirection.IsZero()) {
        FAngle direction;
        direction.Yaw = static_cast<std::int32_t>(
            std::atan2(actor->m_vMovingDirection.y, actor->m_vMovingDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveAction::SetMovingCameraModeImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const FVector currentPosition = actor->GetPos();
    const double stepDistance = actor->m_data->m_dParam[11] * 800.0 / 1000.0;
    if (currentPosition.DistCube(m_vDest) >= 100.0) {
        FVector direction{
            m_vDest.x - currentPosition.x,
            m_vDest.y - currentPosition.y,
            m_vDest.z - currentPosition.z,
        };
        direction.NormalizeCube();
        if (stepDistance * stepDistance <= currentPosition.DistCube(m_vDest)) {
            m_bArrived = false;
            m_nTick = MoveTick;
            m_vMidDest = FVector{
                currentPosition.x + direction.x * stepDistance,
                currentPosition.y + direction.y * stepDistance,
                currentPosition.z + direction.z * stepDistance,
            };
        } else {
            m_bArrived = true;
            m_nTick =
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentPosition.DistCube(m_vDest)) / stepDistance * 800.0);
            m_vMidDest = m_vDest;
        }
    } else {
        m_vDest = currentPosition;
        m_vMidDest = m_vDest;
        m_bArrived = true;
        m_nTick = 80;
    }

    actor->GetController()->ActionBroadcastMove(currentPosition, m_vDest);
    m_bResetDest = false;
    FVector destinationDirection{
        m_vDest.x - currentPosition.x,
        m_vDest.y - currentPosition.y,
        m_vDest.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction;
        direction.Yaw = static_cast<std::int32_t>(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveAction::SetMovingRelativeImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->IsCreature()) {
        return false;
    }

    FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    if (m_bResetDest) {
        actor->GetController()->ActionBroadcastMove(currentPosition, m_vDest);
        m_bResetDest = false;
    }

    CWorldObject* carrier = nullptr;
    if (actor->IsInVehicle()) {
        const int vehicleId = CContainerDB::Inst().GetVehicleID(actor->m_data->container_index);
        carrier = g_VehicleDB.FindVehicle(vehicleId);
        if (carrier == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "vehicle null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3b6)
            // L2_LOG_ERROR(Log,
            //     "vehicle null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3b6)
            L2_LOG_ERROR(
                Log,
                "Cannot start relative movement for actor %u: vehicle %d is missing from container %d; movement setup "
                "fails.",
                static_cast<unsigned>(actor->m_nId), vehicleId, actor->m_data->container_index
            );
            return false;
        }
    } else if (actor->IsInAirShip()) {
        carrier = g_AirShipSmartIdManager.GetObjectW(actor->GetMyAirShipID());
        if (carrier == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "airship null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3c1)
            // L2_LOG_ERROR(Log,
            //     "airship null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3c1)
            L2_LOG_ERROR(
                Log,
                "Cannot start relative movement for actor %u: airship is missing for container %d; movement setup fails.",
                static_cast<unsigned>(actor->m_nId), actor->m_data->container_index
            );
            return false;
        }
    } else if (actor->IsInShuttle()) {
        const int shuttleId = CContainerDB::Inst().GetShuttleID(actor->m_data->container_index);
        carrier = CShuttle::s_ShuttleSmartIdManager.GetObjectW(static_cast<std::uint32_t>(shuttleId));
        if (carrier == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "shuttle null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3cc)
            // L2_LOG_ERROR(Log,
            //     "shuttle null (in container[%d]) at file[%s], line[%d]",
            //     actor->m_data->container_index,
            //     "newaction_move.cpp",
            //     0x3cc)
            L2_LOG_ERROR(
                Log,
                "Cannot start relative movement for actor %u: shuttle %d is missing from container %d; movement setup "
                "fails.",
                static_cast<unsigned>(actor->m_nId), shuttleId, actor->m_data->container_index
            );
            return false;
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "what is this?? (in container[%d]) at file[%s], line[%d]",
        //     actor->m_data->container_index,
        //     "newaction_move.cpp",
        //     0x3d4)
        // L2_LOG_ERROR(Log,
        //     "what is this?? (in container[%d]) at file[%s], line[%d]",
        //     actor->m_data->container_index,
        //     "newaction_move.cpp",
        //     0x3d4)
        L2_LOG_ERROR(
            Log,
            "Cannot start relative movement for actor %u: container %d has no supported transport type; movement setup "
            "fails.",
            static_cast<unsigned>(actor->m_nId), actor->m_data->container_index
        );
        return false;
    }

    const double stepDistance = speed * 800.0 / 1000.0;
    const FVector targetRelative = carrier->ConvAbsToRel(m_vDest);
    const FVector currentRelative = actor->GetRelPos();
    FVector relativeDirection{
        targetRelative.x - currentRelative.x,
        targetRelative.y - currentRelative.y,
        targetRelative.z - currentRelative.z,
    };
    if (relativeDirection.Size() >= 1.0) {
        relativeDirection.NormalizeCube();
        m_vMidRelDest = FVector{
            currentRelative.x + relativeDirection.x * stepDistance,
            currentRelative.y + relativeDirection.y * stepDistance,
            currentRelative.z + relativeDirection.z * stepDistance,
        };
    } else {
        m_vMidRelDest = targetRelative;
    }

    const double middleX = m_vMidRelDest.x - currentRelative.x;
    const double middleY = m_vMidRelDest.y - currentRelative.y;
    const double targetX = targetRelative.x - currentRelative.x;
    const double targetY = targetRelative.y - currentRelative.y;
    if (middleX * middleX + middleY * middleY < targetX * targetX + targetY * targetY) {
        m_nTick = MoveTick;
    } else {
        m_vMidRelDest = targetRelative;
        m_nTick = std::max(
            std::min(
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentRelative.DistCube(targetRelative)) * 1000.0 / speed),
                MoveTick
            ),
            200
        );
        m_bArrived = true;
    }

    m_vMidDest = carrier->ConvRelToAbs(m_vMidRelDest);
    bool blocked = false;
    CContainerIndex neighbors =
        CContainerDB::Inst().GatherNeighborContainerIndex(CContainerDB::Inst().GetContainer(actor->GetContainerIndex()));
    for (int index = 0; index < neighbors.n; ++index) {
        CContainer* const container = neighbors.container[static_cast<std::size_t>(index)];
        L2_GUARD(container->m_BlockTerritoryLock);
        for (CTerritory* const territory : container->m_lBlockTerritoryList) {
            if (territory->CheckIntersectAndGetPoint(currentPosition, m_vMidDest, true)) {
                blocked = true;
            }
        }
    }
    if (blocked) {
        m_vDest = m_vMidDest;
        m_bArrived = true;
        m_bPathFinding = false;
        m_bResetDest = true;
    }

    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveAction::SetMoving() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVE_SETMOVING);

    CCreature* const actor = m_spActor.GetCreature();
    if (actor->IsInVehicle() || actor->IsInAirShip() || actor->IsInShuttle()) {
        return SetMovingRelativeImpl();
    }
    if (static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY) {
        return SetMovingFlyingImpl();
    }
    if (actor->IsCameraMode()) {
        return SetMovingCameraModeImpl();
    }

    FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    FVector pathDestination{};
    bool routeSuccessful = true;

    if (!g_bUserPathFind || !m_bPathFind) {
        CalcMidDest(currentPosition, m_vDest, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);
    } else if (m_bPathFinding &&
        (currentPosition.x - m_vPathFindMidDest.x) * (currentPosition.x - m_vPathFindMidDest.x) +
                (currentPosition.y - m_vPathFindMidDest.y) * (currentPosition.y - m_vPathFindMidDest.y) >
            1.0) {
        actor->m_data->m_bMoveTrouble = false;
        actor->m_data->m_dwPathFindFailCount = 0;
        bool pathSegmentArrived = false;
        CalcMidDest(
            currentPosition, m_vPathFindMidDest, speed, m_vMidDest, m_nTick, pathSegmentArrived, routeSuccessful, false,
            actor
        );
    } else {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_pathfind_begin", actor, nullptr, "source=%.3f,%.3f,%.3f destination=%.3f,%.3f,%.3f", currentPosition.x,
                currentPosition.y, currentPosition.z, m_vDest.x, m_vDest.y, m_vDest.z
            );
        }
        const PathFindResult pathResult = g_pnWorld.PathFind(actor->GetPos(), m_vDest, pathDestination, 400, actor);
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_pathfind_end", actor, nullptr, "result=%d intermediate=%.3f,%.3f,%.3f", static_cast<int>(pathResult),
                pathDestination.x, pathDestination.y, pathDestination.z
            );
        }
        if (pathResult == PFR_CAN_GO) {
            CalcMidDest(actor->GetPos(), m_vDest, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);
            if (m_bPathFinding) {
                m_bResetDest = true;
            }
            m_bPathFinding = false;
            actor->m_data->m_bMoveTrouble = false;
            actor->m_data->m_dwPathFindFailCount = 0;
        } else if (pathResult == PFR_PATH_FOUND) {
            m_bPathFinding = true;
            actor->m_data->m_bMoveTrouble = false;
            actor->m_data->m_dwPathFindFailCount = 0;
            m_vPathFindMidDest = pathDestination;
            bool pathSegmentArrived = false;
            CalcMidDest(
                actor->GetPos(), pathDestination, speed, m_vMidDest, m_nTick, pathSegmentArrived, routeSuccessful, false,
                actor
            );
            m_bResetDest = true;
        } else if (pathResult == PFR_ERROR) {
            CalcMidDest(actor->GetPos(), m_vDest, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);
            m_bPathFinding = false;
            m_bResetDest = true;
            actor->m_data->m_bMoveTrouble = true;
            ++actor->m_data->m_dwPathFindFailCount;
        }
    }

    bool blocked = g_WorldPlaneCollision.CheckCollision(
        currentPosition, m_vMidDest, actor->GetCollisionRadius(), m_vMidDest, static_cast<int>(actor->GetInZoneID()), false
    );
    CContainerIndex neighbors =
        CContainerDB::Inst().GatherNeighborContainerIndex(CContainerDB::Inst().GetContainer(actor->GetContainerIndex()));
    for (int index = 0; index < neighbors.n; ++index) {
        CContainer* const container = neighbors.container[static_cast<std::size_t>(index)];
        L2_GUARD(container->m_BlockTerritoryLock);
        for (CTerritory* const territory : container->m_lBlockTerritoryList) {
            if (territory->CheckIntersectAndGetPoint(currentPosition, m_vMidDest, true)) {
                blocked = true;
            }
        }
    }
    if (!routeSuccessful || blocked) {
        m_vDest = m_vMidDest;
        m_bPathFinding = false;
        m_bResetDest = true;
        m_bArrived = true;
    }

    const FVector unclampedMiddleDestination = m_vMidDest;
    if (m_bResetDest) {
        if (currentPosition.DistCube(m_vDest) <= 0.1) {
            // actor->ActionFailed(1)
            actor->ActionFailed(AF_MOVE);
        } else {
            m_vMidDest = unclampedMiddleDestination;
            m_nTick = std::min(m_nTick, MoveTick);
            const FVector& broadcastDestination = m_bPathFinding ? pathDestination : m_vDest;
            actor->GetController()->ActionBroadcastMove(currentPosition, broadcastDestination);
            actor->m_vMovingDest = broadcastDestination;
        }
        m_bResetDest = false;
    }

    if (!m_bPathFinding && std::sqrt(currentPosition.DistCube(m_vDest)) < speed * 800.0 / 1000.0) {
        m_bArrived = true;
    }

    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    if (!actor->m_vMovingDirection.IsZero()) {
        FAngle direction;
        direction.Yaw = static_cast<std::int32_t>(
            std::atan2(actor->m_vMovingDirection.y, actor->m_vMovingDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveAction::OnTick() {
    L2_GUARD_FUNCTION();

    CNewAction::OnTick();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVE_ONTICK);
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0) {
        return false;
    }

    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_action_tick", actor, nullptr,
            "destination=%.3f,%.3f,%.3f intermediate=%.3f,%.3f,%.3f "
            "arrived=%d pathfinding=%d can_act=%d block_move=%d "
            "can_move=%d",
            m_vDest.x, m_vDest.y, m_vDest.z, m_vMidDest.x, m_vMidDest.y, m_vMidDest.z, m_bArrived ? 1 : 0,
            m_bPathFinding ? 1 : 0, actor->m_SkillMod.m_bCanAct ? 1 : 0, actor->m_SkillMod.m_nBlockMoveCount,
            actor->m_data->m_bCanMove ? 1 : 0
        );
    }

    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }

    CCreature* const target = actor->m_spTarget.GetCreature();
    if (target != nullptr && target->IsFrozen(false)) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }

    bool moved = false;
    if (actor->IsInVehicle()) {
        const int vehicleId = CContainerDB::Inst().GetVehicleID(actor->m_data->container_index);
        CVehicle* const vehicle = g_VehicleDB.FindVehicle(vehicleId);
        if (vehicle == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "vehicle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x23e)
            // L2_LOG_ERROR(Log,
            //     "vehicle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x23e)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in vehicle %d: transport lookup failed for container %d; no relative move is "
                "performed this tick.",
                static_cast<unsigned>(actor->m_nId), vehicleId, actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, vehicle, m_vMidRelDest);
            L2_RELEASE(vehicle, ObjectRefType::FindObject);
        }
    } else if (actor->IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(actor->GetMyAirShipID());
        if (airShip == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x24c)
            // L2_LOG_ERROR(Log,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x24c)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in airship: transport lookup failed for container %d; no relative move is performed "
                "this tick.",
                static_cast<unsigned>(actor->m_nId), actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, airShip, m_vMidRelDest);
        }
    } else if (actor->IsInShuttle()) {
        const int shuttleId = CContainerDB::Inst().GetShuttleID(actor->m_data->container_index);
        CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(static_cast<std::uint32_t>(shuttleId));
        if (shuttle == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "shuttle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x259)
            // L2_LOG_ERROR(Log,
            //     "shuttle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x259)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in shuttle %d: transport lookup failed for container %d; no relative move is "
                "performed this tick.",
                static_cast<unsigned>(actor->m_nId), shuttleId, actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, shuttle, m_vMidRelDest);
        }
    } else {
        const double sourceHeight = actor->m_data->GetPos().z;
        const double destinationHeight = m_vMidDest.z;
        moved = CWorld::Inst().MoveTo(actor, m_vMidDest, false);
        if (moved && destinationHeight + 100.0 < sourceHeight && actor->IsPC()) {
            actor->HeightDamage(sourceHeight - destinationHeight);
        }
    }

    if (m_bResetDest) {
        m_bArrived = false;
    }
    if (m_bArrived) {
        SetValidPendingAction(actor);
        return false;
    }
    if (!moved) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }

    actor->CheckShowMovePos(0, m_vMidDest);
    if (!SetMoving()) {
        return false;
    }
    SetValidPendingAction(actor);
    return true;
}

void CMoveAction::OnEnd(bool success) {
    L2_GUARD_FUNCTION();

    CMoveActionBase::OnEnd(success);
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVE_ONEND);
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    if (!actor->IsInVehicle() && !actor->IsInAirShip() && !actor->IsInShuttle() && !actor->IsCameraMode()) {
        if (success) {
            if (!actor->m_AreaMod.m_bIsUnderWater) {
                bool underWater = false;
                if (actor->m_AreaMod.m_bIsNearUnderWater) {
                    int waterSurface = 0;
                    underWater =
                        CAreaDB::inst().IsUnderWater(actor->GetPos(), waterSurface, static_cast<int>(actor->GetInZoneID()));
                }
                actor->m_bFloatingMoveMode = underWater;
            }
        } else {
            const double directionSquared = actor->m_vMovingDirection.DistCube(FVector{});
            const bool flying = static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY;

            if (actor->IsNPC() && flying && directionSquared < 160000.0) {
                std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - actor->m_nLastTick);
                elapsed = std::clamp(elapsed, 0, MoveTick);
                if (actor->m_nMoveTick == 0) {
                    actor->m_nMoveTick = 1;
                }
                const double fraction =
                    std::clamp(static_cast<double>(elapsed) / static_cast<double>(actor->m_nMoveTick), 0.0, 1.0);
                const FVector sourcePosition = actor->m_data->GetPos();
                FVector interpolatedPosition{
                    sourcePosition.x + fraction * actor->m_vMovingDirection.x,
                    sourcePosition.y + fraction * actor->m_vMovingDirection.y,
                    sourcePosition.z + fraction * actor->m_vMovingDirection.z,
                };

                CWorld::Inst().MoveTo(actor, interpolatedPosition, false);
            } else if (flying && directionSquared < 250000.0) {
                std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - actor->m_nLastTick);
                elapsed = std::clamp(elapsed, 0, MoveTick);
                if (actor->m_nMoveTick == 0) {
                    actor->m_nMoveTick = 1;
                }

                if (!(actor->IsPC() && actor->CastUser()->m_bRestartState)) {
                    const double fraction =
                        std::clamp(static_cast<double>(elapsed) / static_cast<double>(actor->m_nMoveTick), 0.0, 1.0);
                    const FVector sourcePosition = actor->m_data->GetPos();
                    const FVector interpolatedPosition{
                        sourcePosition.x + fraction * actor->m_vMovingDirection.x,
                        sourcePosition.y + fraction * actor->m_vMovingDirection.y,
                        sourcePosition.z + fraction * actor->m_vMovingDirection.z,
                    };

                    CWorld::Inst().MoveTo(actor, interpolatedPosition, false);
                }
            } else if (directionSquared < 90000.0) {
                std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - actor->m_nLastTick);
                elapsed = std::clamp(elapsed, 0, MoveTick);
                if (actor->m_nMoveTick == 0) {
                    actor->m_nMoveTick = 1;
                }
                const double fraction =
                    std::clamp(static_cast<double>(elapsed) / static_cast<double>(actor->m_nMoveTick), 0.0, 1.0);
                const FVector sourcePosition = actor->m_data->GetPos();
                FVector interpolatedPosition{
                    sourcePosition.x + fraction * actor->m_vMovingDirection.x,
                    sourcePosition.y + fraction * actor->m_vMovingDirection.y,
                    sourcePosition.z + fraction * actor->m_vMovingDirection.z,
                };

                bool floating = actor->m_AreaMod.m_bIsUnderWater;
                int waterSurface = actor->m_AreaMod.m_nWaterSurfaceHeight;
                if (!floating) {
                    bool enteredWater = false;
                    if (actor->m_AreaMod.m_bIsNearUnderWater) {
                        enteredWater = CAreaDB::inst().IsUnderWater(
                            interpolatedPosition, waterSurface, static_cast<int>(actor->GetInZoneID())
                        );
                    }
                    if (enteredWater) {
                        actor->m_bFloatingMoveMode = true;
                        floating = true;
                        const int ground = CWorld::Inst().GetGeo()->FindNearGround(
                            static_cast<int>(interpolatedPosition.x), static_cast<int>(interpolatedPosition.y),
                            static_cast<int>(interpolatedPosition.z)
                        );
                        interpolatedPosition.z = std::max(interpolatedPosition.z, static_cast<double>(ground));
                    } else {
                        actor->m_bFloatingMoveMode = false;
                        floating = false;
                        waterSurface = 0;
                        interpolatedPosition.z = std::max(sourcePosition.z, m_vMidDest.z);
                        if (!flying) {
                            interpolatedPosition.z = static_cast<double>(CWorld::Inst().GetGeo()->FindNearGround(
                                static_cast<int>(interpolatedPosition.x), static_cast<int>(interpolatedPosition.y),
                                static_cast<int>(interpolatedPosition.z)
                            ));
                        }
                    }
                }

                const double distance = std::sqrt(sourcePosition.DistCube(interpolatedPosition));
                if (!CWorld::Inst().GetGeo()->MoveStraightTest(
                        sourcePosition, interpolatedPosition, distance, floating, waterSurface
                    )) {
                    interpolatedPosition = sourcePosition;
                }

                if (!(actor->IsPC() && actor->CastUser()->m_bRestartState)) {
                    if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove ||
                        !actor->m_SkillMod.m_bCanAct) {
                        actor->GetController()->ActionBroadcastMove(sourcePosition, interpolatedPosition);
                    }
                    if (actor->IsPC() && GetInterruptActionType() == ACT_SKILL) {
                        actor->GetController()->ActionBroadcastMove(sourcePosition, interpolatedPosition);
                        SetInterruptActionType(ACT_MAX);
                    }
                    CWorld::Inst().MoveTo(actor, interpolatedPosition, false);
                }
            }
        }
    }

    if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
        actor->m_data->m_bMoveTrouble = false;
        actor->m_data->m_dwPathFindFailCount = 0;
    }
    actor->m_bMoving = false;
    actor->m_vMovingDirection = FVector{};
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = 0;
    actor->m_data->m_MovingStatus = MSTE_NOT_MOVING;
    m_bPathFinding = false;
}

FVector CMoveActionBase::GetTargetPos(CCreature& creature, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    if (target.IsItem()) {
        return target.CastItem()->m_data->GetPos();
    }
    if (target.IsStaticObject()) {
        return target.CastStaticObject()->GetPos();
    }
    if (target.IsCreature()) {
        return target.CastCreature()->GetDetailLocation();
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"what is target type of CMoveToTarget?")
    // L2_LOG_ERROR(Log,
    //     L"what is target type of CMoveToTarget?")
    L2_LOG_ERROR(
        Log, "Cannot resolve movement destination for target %u: unsupported object type; using actor %u current position.",
        static_cast<unsigned>(target.m_nId), static_cast<unsigned>(creature.m_nId)
    );
    return creature.GetPos();
}

void CMoveActionBase::OnEnd(bool success) {
    L2_GUARD_FUNCTION();

    CNewAction::OnEnd(success);
    CCreature* const actor = m_spActor.GetCreature();
    if (actor->m_bShowMovePosToGM) {
        CCreatureController* const controller = actor->GetController();
        const ActionType current = controller->GetCurrentActionType();
        const ActionType next = controller->GetNextActionType();
        if ((current == ACT_MOVE_TO_TARGET || current == ACT_MOVE_TO_TARGET_WAIT) && next != ACT_MOVE_TO_TARGET &&
            next != ACT_MOVE_TO_TARGET_WAIT) {
            actor->m_FollowData.Init();
            actor->ValidateMoveToTargetSpeedAndSendInfo();
        }
    }
}

FVector CMoveActionBase::GetTargetDestAsFollow(CCreature& creature, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    FVector destination = GetTargetPos(creature, target);
    const char followType = creature.m_FollowData.m_FollowType;
    if (followType == CCreature::FOLLOW_NONE_TYPE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Use GetTargetDestAsNormal.")
        // L2_LOG_ERROR(Log,
        //     L"Use GetTargetDestAsNormal.")
        L2_LOG_ERROR(
            Log,
            "Follow destination requested for actor %u without a follow mode; returning the unadjusted target %u position.",
            static_cast<unsigned>(creature.m_nId), static_cast<unsigned>(target.m_nId)
        );
        runtime::process::ReportCallStack(2, false);
        return destination;
    }

    creature.m_FollowData.m_FollowerSID = static_cast<std::int32_t>(target.m_nId);
    if (followType == CCreature::FOLLOW_DIST_TYPE || followType == CCreature::FOLLOW_ABSOLUTE_DIST_TYPE) {
        const FVector direction = target.GetNormalizedDirVector(creature.m_FollowData.m_FollowAngle);
        const double distance = static_cast<double>(creature.m_FollowData.m_FollowDistance);
        destination = FVector{
            destination.x + distance * direction.x,
            destination.y + distance * direction.y,
            destination.z + distance * direction.z,
        };
    } else if (followType == CCreature::FOLLOW_OFFSET_TYPE) {
        const FVector offset{
            static_cast<double>(creature.m_FollowData.m_FollowOffsetX),
            static_cast<double>(creature.m_FollowData.m_FollowOffsetY),
            0.0,
        };
        const FAngle targetDirection = target.GetDir();
        const int yaw = static_cast<std::uint16_t>(targetDirection.Yaw);
        const FVector verticalAxis{0.0, 0.0, 1.0};
        const FVector rotated = FVector::RotateAngleAxis(&offset, yaw, &verticalAxis);
        destination.x += rotated.x;
        destination.y += rotated.y;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    destination.z = static_cast<double>(geo->FindGround(
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z + 40.0)
    ));
    return destination;
}

bool CMoveActionBase::NeedMove(CCreature& creature, CWorldObject& target, const double distance) {
    L2_GUARD_FUNCTION();

    if (creature.m_FollowData.m_FollowType == CCreature::FOLLOW_NONE_TYPE) {
        const FVector position = creature.GetPos();
        const FVector targetPosition = GetTargetPos(creature, target);
        const double deltaX = position.x - targetPosition.x;
        const double deltaY = position.y - targetPosition.y;
        const double deltaZ = position.z - targetPosition.z;
        return distance < std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    }

    const FVector position = creature.GetDetailLocation();
    const FVector targetPosition = GetTargetDestAsFollow(creature, target);
    const double deltaX = position.x - targetPosition.x;
    const double deltaY = position.y - targetPosition.y;
    const double deltaZ = position.z - targetPosition.z;
    const double remainingDistance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) -
        (creature.GetCollisionRadius() + target.GetCollisionRadius());
    if (remainingDistance < 0.0) {
        if (creature.m_data->m_bTurnToFollowee) {
            creature.TurnDirToTarget(target);
            creature.ValidateLocation();
            if (creature.IsPC()) {
                creature.CastUser()->ParamChanged(PCB_USER | PCB_CHAR_MOVETYPE);
            } else {
                creature.MakeEnterWorldPacket();
            }
        }
        return false;
    }
    return true;
}

bool CMoveToTargetWaitAction::OnStart() {
    L2_GUARD_FUNCTION();

    CNewAction::OnStart();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    actor->m_spActionTarget = m_spTarget;
    actor->GetController()->ActionBroadcastStopMove();
    actor->GetController()->SetAsMoveToTargetWait(1, m_spTarget, m_acceptableDist);
    AddTimer(actor->m_bShowMovePosToGM ? 200 : 0x309, 0, 0);
    return true;
}

bool CMoveToTargetWaitAction::OnTargetOutOfSight(int distance) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr && actor->IsAlive() && m_spTarget.Sid() == static_cast<std::uint32_t>(distance)) {
        actor->GetController()->ActionBroadcastStopMove();
        actor->GetController()->SetAsNothing(1);
        return false;
    }
    return true;
}

bool CMoveToTargetWaitAction::OnTick() {
    L2_GUARD_FUNCTION();

    CNewAction::OnTick();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    CObject* const targetObject = m_spTarget.GetObjectW();
    if (targetObject == nullptr || !targetObject->IsCreature()) {
        actor->GetController()->SetAsNothing(1);
        return false;
    }

    CCreature* const target = targetObject->CastCreature();
    if (!NeedMove(*actor, *target, static_cast<double>(m_acceptableDist))) {
        AddTimer(0x22b, 0, 0);
        return true;
    }

    if (!actor->IsInAirShip() || !targetObject->IsInAirShip()) {
        actor->GetController()->SetAsMoveToTarget(1, m_spTarget, false, 0, 0, 0, 0, 0);
    } else if (actor->GetMyAirShipID() == targetObject->GetMyAirShipID()) {
        actor->GetController()->SetAsMoveToTargetRelative(
            1, m_spTarget, false, 0, 0, 0, 0, 0, CSmartID(actor->GetMyAirShipID())
        );
    }
    return false;
}

void CCreatureController::__async_Move(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }
    if (!actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0 || actor->m_data->m_StopMode == STP_SIT) {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_dispatch_reject_actor", actor, nullptr,
                "active=%d alive=%d stop_mode=%d current_action=%d "
                "next_action=%d",
                actor->m_data->m_bIsActive ? 1 : 0, actor->m_data->m_bIsAlive != 0 ? 1 : 0,
                static_cast<int>(actor->m_data->m_StopMode), static_cast<int>(GetCurrentActionType()),
                static_cast<int>(GetNextActionType())
            );
        }
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    runtime::raw_atomic::IncrementI32(actor->IsPC() ? &g_nUserMoveToLocationCount : &g_nNpcMoveToLocationCount);
    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove) {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_dispatch_reject_state", actor, nullptr,
                "can_act=%d block_move=%d can_move=%d current_action=%d "
                "next_action=%d",
                actor->m_SkillMod.m_bCanAct ? 1 : 0, actor->m_SkillMod.m_nBlockMoveCount, actor->m_data->m_bCanMove ? 1 : 0,
                static_cast<int>(GetCurrentActionType()), static_cast<int>(GetNextActionType())
            );
        }
        // actor->ActionFailed(1)
        actor->ActionFailed(AF_MOVE);
        return;
    }

    const ActionType currentAction = GetCurrentActionType();
    const int transition = g_ActionTable[currentAction][ACT_MOVE];
    if (transition == 0) {
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "move_dispatch_reject_transition", actor, nullptr, "current_action=%d next_action=%d transition=%d",
                static_cast<int>(currentAction), static_cast<int>(GetNextActionType()), transition
            );
        }
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    FVector destination;
    bool pathFind;
    std::memcpy(&destination, argument->Buffer + 0x08, sizeof(destination));
    std::memcpy(&pathFind, argument->Buffer + 0x20, sizeof(pathFind));
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_dispatch", actor, nullptr,
            "destination=%.3f,%.3f,%.3f pathfind=%d current_action=%d "
            "next_action=%d transition=%d",
            destination.x, destination.y, destination.z, pathFind ? 1 : 0, static_cast<int>(currentAction),
            static_cast<int>(GetNextActionType()), transition
        );
    }

    if (transition == 1) {
        if (currentAction == ACT_TALK && actor->IsPC()) {
            CCreature* const actionTarget = actor->m_spActionTarget.GetCreature();
            if (actionTarget != nullptr && !actionTarget->m_data->m_EnableMoveAfterTalk) {
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                actor->CastUser()->GetSocket()->SendSystemMessage(
                    SystemMessageId::CANNOT_MOVE_IMMEDIATELY_AFTER_STARTING_NPC_CONVERSATION
                );
                return;
            }
        }

        actor->InitCoupleAction();
        EndCurrentAction(false);
        OptionData* optionData;
        std::memcpy(&optionData, argument->Buffer + 0x28, sizeof(optionData));
        if (optionData == nullptr) {
            SetAsMove(0, destination, pathFind, false, 0, 0, 0, 0);
        } else {
            SetAsMove(
                0, destination, pathFind, optionData->m_bForceAttack, optionData->m_nSkillId, optionData->m_nSkillLevel,
                optionData->m_nSkillSubLevel, optionData->m_nUsedItemSID
            );
        }
        SetAsNothing(1);
        DoFirstAction();
        return;
    }

    if (transition == 2) {
        SetAsMove(1, destination, pathFind, false, 0, 0, 0, 0);
        // actor->ActionFailed(1)
        actor->ActionFailed(AF_MOVE);
    }
}

void CCreatureController::__async_MoveToTargetRelative(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0 || !actor->IsInAirShip()) {
        return;
    }

    if (actor->m_data->m_StopMode == 0) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    CObject* target;
    bool forceAttack;
    SkillUseInfo skillUseInfo;
    int usedItemSID;
    std::memcpy(&target, argument->Buffer + 0x08, sizeof(target));
    if (target == nullptr) {
        return;
    }
    std::memcpy(&forceAttack, argument->Buffer + 0x10, sizeof(forceAttack));
    std::memcpy(&skillUseInfo, argument->Buffer + 0x14, sizeof(skillUseInfo));
    std::memcpy(&usedItemSID, argument->Buffer + 0x2c, sizeof(usedItemSID));

    if (!actor->CanForceAttack()) {
        forceAttack = false;
    }
    if (g_nCountryCode == 3 && actor->GetSocket() != nullptr && actor->GetSocket()->m_bEconomicRestriction) {
        forceAttack = false;
    }

    const ActionType currentAction = GetCurrentActionType();
    if (skillUseInfo.m_skillID == 0 && !forceAttack &&
        (currentAction == ACT_MOVE_TO_TARGET_RELATIVE || currentAction == ACT_ATTACK) &&
        GetCurrentAction(-1)->IsTarget(target)) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    const int transition = g_ActionTable[currentAction][ACT_MOVE_TO_TARGET_RELATIVE];
    const CSmartID targetId = target->GetSmartID();
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsMoveToTargetRelative(
            0, targetId, forceAttack, skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel,
            skillUseInfo.m_castRange, usedItemSID, CSmartID(actor->GetMyAirShipID())
        );
        SetAsNothing(1);
        DoFirstAction();
        return;
    }

    if (transition == 2) {
        SetAsMoveToTargetRelative(
            1, targetId, forceAttack, skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel,
            skillUseInfo.m_castRange, usedItemSID, CSmartID(actor->GetMyAirShipID())
        );
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
}

void CCreatureController::__async_MoveToTarget(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0) {
        return;
    }

    if (actor->m_data->m_StopMode == 0) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    CObject* target;
    bool forceAttack;
    SkillUseInfo skillUseInfo;
    int usedItemSID;
    std::memcpy(&target, argument->Buffer + 0x08, sizeof(target));
    if (target == nullptr) {
        return;
    }
    std::memcpy(&forceAttack, argument->Buffer + 0x10, sizeof(forceAttack));
    std::memcpy(&skillUseInfo, argument->Buffer + 0x14, sizeof(skillUseInfo));
    std::memcpy(&usedItemSID, argument->Buffer + 0x2c, sizeof(usedItemSID));

    if (!actor->CanForceAttack()) {
        forceAttack = false;
    }
    if (g_nCountryCode == 3 && actor->GetSocket() != nullptr && actor->GetSocket()->m_bEconomicRestriction) {
        forceAttack = false;
    }

    const ActionType currentAction = GetCurrentActionType();
    if (skillUseInfo.m_skillID == 0 && !forceAttack &&
        (currentAction == ACT_MOVE_TO_TARGET || currentAction == ACT_ATTACK) && GetCurrentAction(-1)->IsTarget(target)) {
        // actor->ActionFailed(1)
        actor->ActionFailed(AF_MOVE);
        return;
    }

    const int transition = g_ActionTable[currentAction][ACT_MOVE_TO_TARGET];
    const CSmartID targetId = target->GetSmartID();
    if (transition == 1) {
        EndCurrentAction(false);
        SetAsMoveToTarget(0, targetId, forceAttack, skillUseInfo, usedItemSID);
        SetAsNothing(1);
        DoFirstAction();
        return;
    }

    if (transition == 2) {
        if (skillUseInfo.m_skillID > 0 &&
            actor->IsDoubleCasting(skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel)) {
            if (actor->m_nLaunchedSkills.load() == 1) {
                if (m_Action[0].m_spPostTarget.Sid() != 0 && m_Action[0].m_spPostTarget.Sid() != targetId.Sid()) {
                    // actor->ActionFailed(0)
                    actor->ActionFailed(AF_DEFAULT);
                    return;
                }

                int skillIndex = 0;
                CSkillAction2& skillAction = m_Action[0].GetSkillAction(skillIndex, false, -1);
                if (skillAction.m_pSkillInfoA == nullptr) {
                    // actor->ActionFailed(0)
                    actor->ActionFailed(AF_DEFAULT);
                    return;
                }
                if (skillAction.m_ActionState == ActionState::End) {
                    const double remainReuseDelay = actor->GetRemainReuseDelaySec(
                        skillUseInfo.m_skillID, skillUseInfo.m_skillLevel, skillUseInfo.m_skillSubLevel
                    );
                    if (remainReuseDelay <= 0.0) {
                        skillAction.Set(
                            m_spActor, targetId, skillUseInfo, forceAttack, forceAttack,
                            static_cast<unsigned int>(usedItemSID), skillIndex
                        );
                        skillAction.OnStart();
                    }
                    return;
                }
            }
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return;
        }

        SetAsMoveToTarget(1, targetId, forceAttack, skillUseInfo, usedItemSID);
    }
    // actor->ActionFailed(1)
    actor->ActionFailed(AF_MOVE);
}

void CMoveAction::Set(
    CSmartID actor,
    FVector destination,
    bool pathFind,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int usedItemSID
) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_vDest = destination;
    m_bPathFinding = false;
    m_bResetDest = true;
    m_bPathFind = pathFind;
    m_nSkillId = skillId;
    m_nSkillLevel = skillLevel;
    m_nSkillSubLevel = skillSubLevel;
    m_bForceAttack = forceAttack;
    m_nUsedItemSID = usedItemSID;
}

void CMoveRelativeAction::Set(CSmartID actor, CSmartID relativeWorldObject, FVector relativePosition) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spRelativeWorldObject = relativeWorldObject;
    m_vRelPos = relativePosition;
}

FVector CMoveRelativeAction::GetDestination() {
    L2_GUARD_FUNCTION();

    CWorldObject* const relativeWorldObject = m_spRelativeWorldObject.GetWorldObject();
    if (relativeWorldObject == nullptr) {
        return FVector{};
    }
    return relativeWorldObject->ConvRelToAbs(m_vRelPos);
}

void CMoveRelativeAction::OnEnd(bool success) {
    L2_GUARD_FUNCTION();

    CMoveActionBase::OnEnd(success);
    if (m_spActor.IsValid() && m_spRelativeWorldObject.IsValid()) {
        CCreature* const actor = m_spActor.GetCreature();
        actor->m_bMoving = false;
        actor->m_vMovingDirection = FVector{};
        actor->m_nLastTick = runtime::GetTickCount();
        actor->m_nMoveTick = 0;
        actor->m_data->m_MovingStatus = MSTE_NOT_MOVING;
    }
}

bool CMoveRelativeAction::OnStopMove() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr) {
        actor->GetController()->ActionBroadcastStopMove();
    }
    return false;
}

bool CMoveRelativeAction::SetMoving() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    if (actor->IsInVehicle()) {
        return SetMovingInVehicleImpl();
    }
    if (actor->IsInAirShip()) {
        return SetMovingInAirShipImpl();
    }
    if (actor->IsInShuttle()) {
        return SetMovingInShuttleImpl();
    }

    CWorldObject* const relativeWorldObject = m_spRelativeWorldObject.GetWorldObject();
    CAbstractCarrier* const carrier = relativeWorldObject == nullptr ? nullptr : relativeWorldObject->CastAbstractCarrier();
    return carrier != nullptr && carrier->SetMovingIntoCarrier(this);
}

bool CMoveRelativeAction::OnTick() {
    L2_GUARD_FUNCTION();

    CNewAction::OnTick();
    if (!m_spRelativeWorldObject.IsValid()) {
        return false;
    }

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0) {
        return false;
    }
    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }

    bool moved = false;
    if (actor->IsInVehicle()) {
        const int vehicleId = CContainerDB::Inst().GetVehicleID(actor->m_data->container_index);
        CVehicle* const vehicle = CVehicle::GetObjectW(static_cast<std::uint32_t>(vehicleId));
        if (vehicle == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "vehicle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x7fe)
            // L2_LOG_ERROR(Log,
            //     "vehicle null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x7fe)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in vehicle %d: transport lookup failed for container %d; no relative move is "
                "performed this tick.",
                static_cast<unsigned>(actor->m_nId), vehicleId, actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, vehicle, m_vMidRelDest);
        }
    } else if (actor->IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(actor->GetMyAirShipID());
        if (airShip == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x80b)
            // L2_LOG_ERROR(Log,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x80b)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in airship: transport lookup failed for container %d; no relative move is performed "
                "this tick.",
                static_cast<unsigned>(actor->m_nId), actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, airShip, m_vMidRelDest);
        }
    } else if (actor->IsInShuttle()) {
        const int shuttleId = CContainerDB::Inst().GetShuttleID(actor->m_data->container_index);
        CShuttle* const shuttle = CShuttle::s_ShuttleSmartIdManager.GetObjectW(static_cast<std::uint32_t>(shuttleId));
        if (shuttle == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x818)
            // L2_LOG_ERROR(Log,
            //     "airship null at file[%s], line[%d]",
            //     "newaction_move.cpp",
            //     0x818)
            L2_LOG_ERROR(
                Log,
                "Cannot move actor %u in shuttle %d: transport lookup failed for container %d; no relative move is "
                "performed this tick.",
                static_cast<unsigned>(actor->m_nId), shuttleId, actor->m_data->container_index
            );
        } else {
            moved = CWorld::Inst().MoveToRelatively(actor, shuttle, m_vMidRelDest);
        }
    } else {
        moved = CWorld::Inst().MoveTo(actor, m_vMidDest, false);
    }

    if (m_bArrived) {
        return false;
    }
    if (moved || !actor->IsPC()) {
        m_vDest = GetDestination();
        return SetMoving();
    }
    actor->GetController()->ActionBroadcastStopMove();
    return false;
}

bool CMoveRelativeAction::OnStart() {
    L2_GUARD_FUNCTION();

    CNewAction::OnStart();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !m_spRelativeWorldObject.IsValid()) {
        return false;
    }

    actor->m_spActionTarget = CSmartID();
    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove) {
        return false;
    }

    FVector currentPosition = actor->m_data->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    m_vDest = GetDestination();
    const double deltaX = m_vDest.x - currentPosition.x;
    const double deltaY = m_vDest.y - currentPosition.y;
    if (speed <= 0.0 || deltaX * deltaX + deltaY * deltaY < 1.0) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return false;
    }

    if (actor->m_data->m_StopMode != STP_SIT && actor->m_data->m_StopMode != STP_STORE) {
        m_bMoving = false;
        m_bArrived = false;
        actor->m_data->m_MovingStatus = MSTE_MOVING;
        return SetMoving();
    }

    if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    return false;
}

bool CMoveRelativeAction::SetMovingIntoVehicleImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    const FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    CVehicle* const vehicle = m_spRelativeWorldObject.GetVehicle();
    if (vehicle == nullptr) {
        return false;
    }

    const FVector currentRelative = vehicle->ConvAbsToRel(currentPosition);
    FVector middleDestination{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld::Inst().GetGeo()->MoveStraight(
        actor->GetPos(), m_vDest, speed * 800.0 / 1000.0, middleDestination, distanceExhausted, travelledDistance
    );

    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "cdddddd", 0x7e, static_cast<int>(actor->m_nId),
                static_cast<int>(vehicle->m_nId), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y),
                static_cast<int>(currentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "cdddddd", 0x7e, static_cast<int>(actor->m_nId), static_cast<int>(vehicle->m_nId),
                static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z),
                static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y), static_cast<int>(currentRelative.z)
            );
        }
    }

    m_bMoving = true;
    m_vMidDest = middleDestination;
    m_nTick = std::min(
        runtime::Win64CvttDoubleToInt32(std::sqrt(currentPosition.DistCube(middleDestination)) * 1000.0 / speed), MoveTick
    );
    if (!distanceExhausted || m_nTick < 750) {
        m_bArrived = true;
    }

    FVector destinationDirection{
        m_vDest.x - currentPosition.x,
        m_vDest.y - currentPosition.y,
        m_vDest.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveRelativeAction::SetMovingIntoAirShipImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    CAirShip* const airShip = m_spRelativeWorldObject.GetAirShip();
    if (airShip == nullptr) {
        return false;
    }

    const FVector currentRelative = airShip->ConvAbsToRel(currentPosition);
    FVector middleDestination{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld::Inst().GetGeo()->MoveStraight(
        actor->GetPos(), m_vDest, speed * 800.0 / 1000.0, middleDestination, distanceExhausted, travelledDistance
    );

    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "chdddddd", 0xfe, 0x6e, static_cast<int>(actor->m_nId),
                static_cast<int>(airShip->m_nId), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y),
                static_cast<int>(currentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddd", 0xfe, 0x6e, static_cast<int>(actor->m_nId), static_cast<int>(airShip->m_nId),
                static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z),
                static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y), static_cast<int>(currentRelative.z)
            );
        }
    }

    m_bMoving = true;
    m_vMidDest = middleDestination;
    m_nTick = std::min(
        runtime::Win64CvttDoubleToInt32(std::sqrt(currentPosition.DistCube(middleDestination)) * 1000.0 / speed), MoveTick
    );
    if (!distanceExhausted || m_nTick < 750) {
        m_bArrived = true;
    }

    FVector destinationDirection{
        m_vDest.x - currentPosition.x,
        m_vDest.y - currentPosition.y,
        m_vDest.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveRelativeAction::SetMovingIntoShuttleImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    const FVector currentPosition = actor->GetPos();
    const double speed = actor->m_data->m_dParam[11];
    CShuttle* const shuttle = m_spRelativeWorldObject.GetShuttle();
    if (shuttle == nullptr) {
        return false;
    }

    const FVector currentRelative = shuttle->ConvAbsToRel(currentPosition);
    FVector middleDestination{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld::Inst().GetGeo()->MoveStraight(
        actor->GetPos(), m_vDest, speed * 800.0 / 1000.0, middleDestination, distanceExhausted, travelledDistance
    );

    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "chdddddddd", 0xfe, 0xcf, static_cast<int>(actor->m_nId),
                shuttle->ObjectID(), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y),
                static_cast<int>(currentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddddd", 0xfe, 0xcf, static_cast<int>(actor->m_nId), shuttle->ObjectID(), static_cast<int>(m_vRelPos.x),
                static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x),
                static_cast<int>(currentRelative.y), static_cast<int>(currentRelative.z)
            );
        }
    }

    m_bMoving = true;
    m_vMidDest = middleDestination;
    m_nTick = std::min(
        runtime::Win64CvttDoubleToInt32(std::sqrt(currentPosition.DistCube(middleDestination)) * 1000.0 / speed), MoveTick
    );
    if (!distanceExhausted || m_nTick < 750) {
        m_bArrived = true;
    }

    FVector destinationDirection{
        m_vDest.x - currentPosition.x,
        m_vDest.y - currentPosition.y,
        m_vDest.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveRelativeAction::SetMovingInVehicleImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    const FVector currentPosition = actor->GetPos();
    const FVector destination = m_vDest;
    const double speed = actor->m_data->m_dParam[11];
    CVehicle* const vehicle = m_spRelativeWorldObject.GetVehicle();
    if (vehicle == nullptr) {
        return false;
    }

    const FVector packetCurrentRelative = vehicle->ConvAbsToRel(currentPosition);
    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "cdddddd", 0x7e, static_cast<int>(actor->m_nId),
                static_cast<int>(vehicle->m_nId), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(packetCurrentRelative.x),
                static_cast<int>(packetCurrentRelative.y), static_cast<int>(packetCurrentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "cdddddd", 0x7e, static_cast<int>(actor->m_nId), static_cast<int>(vehicle->m_nId),
                static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z),
                static_cast<int>(packetCurrentRelative.x), static_cast<int>(packetCurrentRelative.y),
                static_cast<int>(packetCurrentRelative.z)
            );
        }
    }
    m_bMoving = true;

    const double stepDistance = speed * 800.0 / 1000.0;
    const FVector targetRelative = vehicle->ConvAbsToRel(destination);
    const FVector currentRelative = actor->GetRelPos();
    FVector relativeDirection{
        targetRelative.x - currentRelative.x,
        targetRelative.y - currentRelative.y,
        targetRelative.z - currentRelative.z,
    };
    if (relativeDirection.DistCube(FVector{}) >= 1.0) {
        relativeDirection.NormalizeCube();
        m_vMidRelDest = FVector{
            currentRelative.x + relativeDirection.x * stepDistance,
            currentRelative.y + relativeDirection.y * stepDistance,
            currentRelative.z + relativeDirection.z * stepDistance,
        };
    } else {
        m_vMidRelDest = targetRelative;
    }

    const double middleX = m_vMidRelDest.x - currentRelative.x;
    const double middleY = m_vMidRelDest.y - currentRelative.y;
    const double targetX = targetRelative.x - currentRelative.x;
    const double targetY = targetRelative.y - currentRelative.y;
    if (middleX * middleX + middleY * middleY < targetX * targetX + targetY * targetY) {
        m_nTick = MoveTick;
    } else {
        m_vMidRelDest = targetRelative;
        m_nTick = std::max(
            std::min(
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentRelative.DistCube(targetRelative)) * 1000.0 / speed),
                MoveTick
            ),
            100
        );
        m_bArrived = true;
    }

    m_vMidDest = vehicle->ConvRelToAbs(m_vMidRelDest);
    FVector destinationDirection{
        destination.x - currentPosition.x,
        destination.y - currentPosition.y,
        destination.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveRelativeAction::SetMovingInAirShipImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    const FVector currentPosition = actor->GetPos();
    const FVector destination = m_vDest;
    const double speed = actor->m_data->m_dParam[11];
    CAirShip* const airShip = m_spRelativeWorldObject.GetAirShip();
    if (airShip == nullptr) {
        return false;
    }

    const FVector packetCurrentRelative = airShip->ConvAbsToRel(currentPosition);
    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "chdddddd", 0xfe, 0x6e, static_cast<int>(actor->m_nId),
                static_cast<int>(airShip->m_nId), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(packetCurrentRelative.x),
                static_cast<int>(packetCurrentRelative.y), static_cast<int>(packetCurrentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddd", 0xfe, 0x6e, static_cast<int>(actor->m_nId), static_cast<int>(airShip->m_nId),
                static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z),
                static_cast<int>(packetCurrentRelative.x), static_cast<int>(packetCurrentRelative.y),
                static_cast<int>(packetCurrentRelative.z)
            );
        }
    }
    m_bMoving = true;

    const double stepDistance = speed * 800.0 / 1000.0;
    const FVector targetRelative = airShip->ConvAbsToRel(destination);
    const FVector currentRelative = actor->GetRelPos();
    FVector relativeDirection{
        targetRelative.x - currentRelative.x,
        targetRelative.y - currentRelative.y,
        targetRelative.z - currentRelative.z,
    };
    if (relativeDirection.DistCube(FVector{}) >= 1.0) {
        relativeDirection.NormalizeCube();
        m_vMidRelDest = FVector{
            currentRelative.x + relativeDirection.x * stepDistance,
            currentRelative.y + relativeDirection.y * stepDistance,
            currentRelative.z + relativeDirection.z * stepDistance,
        };
    } else {
        m_vMidRelDest = targetRelative;
    }

    const double middleX = m_vMidRelDest.x - currentRelative.x;
    const double middleY = m_vMidRelDest.y - currentRelative.y;
    const double targetX = targetRelative.x - currentRelative.x;
    const double targetY = targetRelative.y - currentRelative.y;
    if (middleX * middleX + middleY * middleY < targetX * targetX + targetY * targetY) {
        m_nTick = MoveTick;
    } else {
        m_vMidRelDest = targetRelative;
        m_nTick = std::max(
            std::min(
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentRelative.DistCube(targetRelative)) * 1000.0 / speed),
                MoveTick
            ),
            100
        );
        m_bArrived = true;
    }

    m_vMidDest = airShip->ConvRelToAbs(m_vMidRelDest);
    FVector destinationDirection{
        destination.x - currentPosition.x,
        destination.y - currentPosition.y,
        destination.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveRelativeAction::SetMovingInShuttleImpl() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }
    const FVector currentPosition = actor->GetPos();
    const FVector destination = m_vDest;
    const double speed = actor->m_data->m_dParam[11];
    CShuttle* const shuttle = m_spRelativeWorldObject.GetShuttle();
    if (shuttle == nullptr) {
        return false;
    }

    const double stepDistance = speed * 800.0 / 1000.0;
    const FVector targetRelative = shuttle->ConvAbsToRel(destination);
    const FVector currentRelative = actor->GetRelPos();
    FVector relativeDirection{
        targetRelative.x - currentRelative.x,
        targetRelative.y - currentRelative.y,
        targetRelative.z - currentRelative.z,
    };
    if (!m_bMoving) {
        if (!actor->IsGMInvisible()) {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0xf000, currentPosition, "chdddddddd", 0xfe, 0xcf, static_cast<int>(actor->m_nId),
                shuttle->ObjectID(), static_cast<int>(m_vRelPos.x), static_cast<int>(m_vRelPos.y),
                static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x), static_cast<int>(currentRelative.y),
                static_cast<int>(currentRelative.z)
            );
        } else if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddddd", 0xfe, 0xcf, static_cast<int>(actor->m_nId), shuttle->ObjectID(), static_cast<int>(m_vRelPos.x),
                static_cast<int>(m_vRelPos.y), static_cast<int>(m_vRelPos.z), static_cast<int>(currentRelative.x),
                static_cast<int>(currentRelative.y), static_cast<int>(currentRelative.z)
            );
        }
    }
    m_bMoving = true;

    if (relativeDirection.DistCube(FVector{}) >= 1.0) {
        relativeDirection.NormalizeCube();
        m_vMidRelDest = FVector{
            currentRelative.x + relativeDirection.x * stepDistance,
            currentRelative.y + relativeDirection.y * stepDistance,
            currentRelative.z + relativeDirection.z * stepDistance,
        };
    } else {
        m_vMidRelDest = targetRelative;
    }

    const double middleX = m_vMidRelDest.x - currentRelative.x;
    const double middleY = m_vMidRelDest.y - currentRelative.y;
    const double targetX = targetRelative.x - currentRelative.x;
    const double targetY = targetRelative.y - currentRelative.y;
    if (middleX * middleX + middleY * middleY < targetX * targetX + targetY * targetY) {
        m_nTick = MoveTick;
    } else {
        m_vMidRelDest = targetRelative;
        m_nTick = std::max(
            std::min(
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentRelative.DistCube(targetRelative)) * 1000.0 / speed),
                MoveTick
            ),
            100
        );
        m_bArrived = true;
    }

    m_vMidDest = shuttle->ConvRelToAbs(m_vMidRelDest);
    FVector destinationDirection{
        destination.x - currentPosition.x,
        destination.y - currentPosition.y,
        destination.z - currentPosition.z,
    };
    if (!destinationDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(destinationDirection.y, destinationDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

void CMoveToTargetRelativeAction::Set(
    CSmartID actor,
    CSmartID target,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int skillRange,
    int usedItemSID,
    CSmartID airShip
) {
    L2_GUARD_FUNCTION();

    m_spAirShip = airShip;
    m_spActor = actor;
    m_spTarget = target;
    if (actor.GetObjectW() != nullptr && g_bIsNonPKServer) {
        forceAttack = false;
    }
    m_Data.m_bForceAttack = forceAttack;
    m_Data.m_nSkillId = skillId;
    m_Data.m_nSkillRange = skillRange;
    m_nSkillLevel = skillLevel;
    m_nSkillSubLevel = skillSubLevel;
    m_nUsedItemSID = usedItemSID;
    m_bMoving = false;
}

bool CMoveToTargetActionBase::OnTargetOutOfSight(int targetId) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr && actor->IsAlive() && m_spTarget.Sid() == static_cast<std::uint32_t>(targetId)) {
        actor->GetController()->ActionBroadcastStopMove();
        actor->GetController()->SetAsNothing(1);
        return false;
    }
    return true;
}

void CMoveToTargetActionBase::ComputeDistance(CCreature& creature, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    target.DecideDistance(&m_Data, creature.GetSmartID());
}

FVector CMoveActionBase::GetTargetDestAsNormal(CCreature& creature, CWorldObject& target, int distance) {
    L2_GUARD_FUNCTION();

    FVector destination = GetTargetPos(creature, target);
    if (creature.m_FollowData.m_FollowType >= 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Use GetTargetDestAsFollow.")
        // L2_LOG_ERROR(Log,
        //     L"Use GetTargetDestAsFollow.")
        L2_LOG_ERROR(
            Log,
            "Normal destination requested for actor %u with follow mode %d; returning the unadjusted target %u position.",
            static_cast<unsigned>(creature.m_nId), static_cast<int>(creature.m_FollowData.m_FollowType),
            static_cast<unsigned>(target.m_nId)
        );
        runtime::process::ReportCallStack(2, false);
        return destination;
    }

    const FVector originalTargetPosition = destination;
    const FVector creaturePosition = creature.m_data->GetPos();
    const FVector direction =
        FVector{
            creaturePosition.x - destination.x,
            creaturePosition.y - destination.y,
            creaturePosition.z - destination.z,
        }
            .SafeNormal();
    destination = FVector{
        destination.x + static_cast<double>(distance) * direction.x,
        destination.y + static_cast<double>(distance) * direction.y,
        destination.z + static_cast<double>(distance) * direction.z,
    };

    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (static_cast<std::int32_t>(creature.m_data->m_moveType) != CMT_FLY) {
        destination.z = static_cast<double>(geo->FindGround(
            runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
            runtime::Win64CvttDoubleToInt32(destination.z + 40.0)
        ));
    }
    if (creature.IsNPC() && target.IsCreature() &&
        !geo->CanSee(destination, originalTargetPosition, static_cast<int>(creature.GetInZoneID()), false)) {
        destination = originalTargetPosition;
    }
    return destination;
}

FVector CMoveToTargetActionBase::GetTargetDestination(CCreature& creature, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    if (target.IsItem()) {
        return CMoveActionBase::GetTargetPos(creature, target);
    }
    if (target.IsCreature()) {
        ComputeDistance(creature, target);
    }
    if (creature.m_FollowData.m_FollowType < 1) {
        return CMoveActionBase::GetTargetDestAsNormal(creature, target, m_Data.m_nDistance);
    }
    return CMoveActionBase::GetTargetDestAsFollow(creature, target);
}

void CMoveToTargetRelativeAction::OnEnd(bool success) {
    L2_GUARD_FUNCTION();

    CMoveActionBase::OnEnd(success);
    if (!m_spActor.IsValid()) {
        return;
    }

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }
    if ((actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) &&
        m_Data.m_nSkillId != 0) {
        actor->OnUseSkillFinished(
            static_cast<int>(actor->m_spTarget.Sid()), m_Data.m_nSkillId, m_nSkillLevel, m_nSkillSubLevel, 0
        );
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    actor->m_bMoving = false;
    actor->m_vMovingDirection = FVector{};
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = 0;
    actor->m_data->m_MovingStatus = MSTE_NOT_MOVING;
}

void CMoveToTargetRelativeAction::SetValidPendingAction() {
    L2_GUARD_FUNCTION();

    CWorldObject* const target = m_spTarget.GetWorldObject();
    CCreature* const actor = m_spActor.GetCreature();
    const bool targetIsCreature = target != nullptr && target->IsCreature();
    if (target == nullptr || (!targetIsCreature && !target->IsAirShipPart())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what target? pActor[%x] pTarget id[%x]",
        //     L"newaction_move.cpp",
        //     0xb05,
        //     static_cast<int>(actor->m_nId),
        //     target == nullptr ? 0 : static_cast<int>(target->m_nId))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] what target? pActor[%x] pTarget id[%x]",
        //     L"newaction_move.cpp",
        //     0xb05,
        //     static_cast<int>(actor->m_nId),
        //     target == nullptr ? 0 : static_cast<int>(target->m_nId))
        L2_LOG_ERROR(
            Log,
            "Cannot select a pending relative-move action for actor %u: target %u is missing or is neither a creature nor "
            "an airship part; setting idle action.",
            static_cast<unsigned>(actor->m_nId), target == nullptr ? 0U : static_cast<unsigned>(target->m_nId)
        );
        actor->GetController()->SetAsNothing(1);
        return;
    }

    CCreatureController* const controller = actor->GetController();
    if (!targetIsCreature) {
        if (m_Data.m_nSkillId < 1) {
            if (target->IsEnemyTo(actor)) {
                controller->SetAsAttack(1, m_spTarget, false);
            }
            return;
        }
        controller->SetAsSkill(
            1, target->GetSmartID(), m_Data.m_nSkillId, m_nSkillLevel, m_nSkillSubLevel, m_Data.m_bForceAttack, false,
            static_cast<unsigned int>(m_nUsedItemSID)
        );
        return;
    }

    if (m_Data.m_nSkillId > 0) {
        controller->SetAsSkill(
            1, m_spTarget, m_Data.m_nSkillId, m_nSkillLevel, m_nSkillSubLevel, m_Data.m_bForceAttack, false,
            static_cast<unsigned int>(m_nUsedItemSID)
        );
        return;
    }

    if (m_Data.m_bForceAttack || target->IsEnemyTo(actor)) {
        controller->SetAsAttack(1, m_spTarget, false);
        return;
    }
    if (!target->IsNPC()) {
        User* const targetUser = target->CastUser();
        if (targetUser->GetPrivateStoreType() == 0) {
            ComputeDistance(*actor, *target);
            controller->SetAsMoveToTargetWait(1, m_spTarget, m_Data.m_nAcceptableDistance);
        } else {
            controller->SetAsGeneral(1, m_spTarget, CGeneralAction::GAT_PRIVATE_STORE);
        }
        return;
    }

    const FVector actorPosition = actor->GetPos();
    const FVector targetPosition = target->GetPos();
    const int zDistance =
        std::abs(runtime::Win64CvttDoubleToInt32(actorPosition.z) - runtime::Win64CvttDoubleToInt32(targetPosition.z));
    if (zDistance < 100) {
        controller->SetAsTalk(1, m_spTarget);
    } else {
        controller->SetAsNothing(1);
    }
}

FVector CMoveToTargetRelativeAction::GetMeleeAttackPosition(CCreature* creature, CWorldObject* target) {
    L2_GUARD_FUNCTION();

    const int attackDistance = runtime::Win64CvttDoubleToInt32(
        static_cast<double>(
            runtime::Win64CvttDoubleToInt32(target->GetCollisionRadius()) +
            runtime::Win64CvttDoubleToInt32(creature->GetCollisionRadius())
        ) +
        static_cast<double>(creature->m_data->m_nAttackRange) * 0.5
    );
    const FVector creaturePosition = creature->GetPos();
    const FVector targetPosition = target->IsCreature() ? target->CastCreature()->GetDetailLocation() : target->GetPos();

    if (attackDistance < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] attack range invalid",
        //     L"newaction_move.cpp",
        //     0x9b6)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] attack range invalid",
        //     L"newaction_move.cpp",
        //     0x9b6)
        L2_LOG_ERROR(
            Log, "Computed negative melee approach distance %d for actor %u; using the actor current position.",
            attackDistance, static_cast<unsigned>(creature->m_nId)
        );
        return creaturePosition;
    }

    const FVector delta{
        creaturePosition.x - targetPosition.x,
        creaturePosition.y - targetPosition.y,
        creaturePosition.z - targetPosition.z,
    };
    if (attackDistance < 500) {
        const FVector direction = delta.SafeNormal();
        return FVector{
            targetPosition.x + static_cast<double>(attackDistance) * direction.x,
            targetPosition.y + static_cast<double>(attackDistance) * direction.y,
            targetPosition.z + static_cast<double>(attackDistance) * direction.z,
        };
    }

    const double scale =
        static_cast<double>(attackDistance) / std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    return FVector{
        targetPosition.x + delta.x * scale,
        targetPosition.y + delta.y * scale,
        targetPosition.z + delta.z * scale,
    };
}

FVector CMoveToTargetRelativeAction::GetDestination() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return FVector{};
    }
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (target == nullptr) {
        return actor->GetPos();
    }
    if (target->IsCreature() || target->IsAirShipPart()) {
        ComputeDistance(*actor, *target);
    }

    const FVector actorPosition = actor->GetPos();
    FVector targetPosition = CMoveActionBase::GetTargetPos(*actor, *target);
    const FVector direction =
        FVector{
            actorPosition.x - targetPosition.x,
            actorPosition.y - targetPosition.y,
            actorPosition.z - targetPosition.z,
        }
            .SafeNormal();
    const double distance = static_cast<double>(m_Data.m_nDistance);
    targetPosition.x += distance * direction.x;
    targetPosition.y += distance * direction.y;
    targetPosition.z += distance * direction.z;
    return targetPosition;
}

bool CMoveToTargetRelativeAction::SetMoving() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    CAirShip* const airShip = m_spAirShip.GetAirShip();
    if (actor == nullptr || target == nullptr || airShip == nullptr || !actor->IsInAirShip() ||
        m_spAirShip.Sid() != target->GetMyAirShipID()) {
        return false;
    }

    FVector currentPosition = actor->GetPos();
    const FVector targetPosition = CMoveActionBase::GetTargetPos(*actor, *target);
    const double deltaX = currentPosition.x - targetPosition.x;
    const double deltaY = currentPosition.y - targetPosition.y;
    const double deltaZ = currentPosition.z - targetPosition.z;
    const std::int32_t acceptableDistanceSquared = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(m_Data.m_nAcceptableDistance) * static_cast<std::uint32_t>(m_Data.m_nAcceptableDistance)
    );
    if (static_cast<double>(acceptableDistanceSquared) >= deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ) {
        actor->GetController()->ActionBroadcastMoveToTargetInAirShip(
            currentPosition, actor->GetRelPos(), static_cast<int>(m_spTarget.Sid()), m_Data.m_nDistance,
            actor->GetMyAirShipID()
        );
        return false;
    }

    if (!m_bMoving) {
        actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
        m_bMoving = true;
    }

    const FVector destination =
        m_MoveToTargetType == MTTT_MOVE_TO_MELEE_POINT ? GetMeleeAttackPosition(actor, target) : GetDestination();
    const double speed = actor->m_data->m_dParam[11];
    const double stepDistance = speed * 800.0 / 1000.0;
    const FVector targetRelative = airShip->ConvAbsToRel(destination);
    const FVector currentRelative = airShip->ConvAbsToRel(currentPosition);
    FVector relativeDirection{
        targetRelative.x - currentRelative.x,
        targetRelative.y - currentRelative.y,
        targetRelative.z - currentRelative.z,
    };
    if (relativeDirection.DistCube(FVector{}) >= 1.0) {
        relativeDirection.NormalizeCube();
        m_vMidRelDest = FVector{
            currentRelative.x + relativeDirection.x * stepDistance,
            currentRelative.y + relativeDirection.y * stepDistance,
            currentRelative.z + relativeDirection.z * stepDistance,
        };
    } else {
        m_vMidRelDest = targetRelative;
    }

    const double middleX = m_vMidRelDest.x - currentRelative.x;
    const double middleY = m_vMidRelDest.y - currentRelative.y;
    const double targetX = targetRelative.x - currentRelative.x;
    const double targetY = targetRelative.y - currentRelative.y;
    if (middleX * middleX + middleY * middleY < targetX * targetX + targetY * targetY) {
        m_nTick = MoveTick;
    } else {
        m_vMidRelDest = targetRelative;
        m_nTick = std::max(
            std::min(
                runtime::Win64CvttDoubleToInt32(std::sqrt(currentRelative.DistCube(targetRelative)) * 1000.0 / speed),
                MoveTick
            ),
            100
        );
        m_bArrived = true;
    }

    m_vMidDest = airShip->ConvRelToAbs(m_vMidRelDest);
    CContainerIndex neighbors =
        CContainerDB::Inst().GatherNeighborContainerIndex(CContainerDB::Inst().GetContainer(actor->GetContainerIndex()));
    for (int index = 0; index < neighbors.n; ++index) {
        CContainer* const container = neighbors.container[static_cast<std::size_t>(index)];
        L2_GUARD(container->m_BlockTerritoryLock);
        for (CTerritory* const territory : container->m_lBlockTerritoryList) {
            territory->CheckIntersectAndGetPoint(currentPosition, m_vMidDest, true);
        }
    }

    actor->m_vMovingDirection = FVector{
        m_vMidDest.x - currentPosition.x,
        m_vMidDest.y - currentPosition.y,
        m_vMidDest.z - currentPosition.z,
    };
    if (!actor->m_vMovingDirection.IsZero()) {
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(actor->m_vMovingDirection.y, actor->m_vMovingDirection.x) * 65535.0 / 6.283185307179586
        );
        actor->m_data->SetDir(direction);
    }
    actor->m_bMoving = true;
    actor->m_nLastTick = runtime::GetTickCount();
    actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
    AddTimer(m_nTick, 0, 0);
    return true;
}

bool CMoveToTargetRelativeAction::OnTick() {
    L2_GUARD_FUNCTION();

    CNewAction::OnTick();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !m_spTarget.IsValid() || !actor->m_data->m_bIsActive || actor->m_data->m_bIsAlive == 0) {
        return false;
    }

    CWorldObject* const target = static_cast<CWorldObject*>(m_spTarget.GetObjectW());
    if (target == nullptr || !actor->IsInAirShip() || m_spAirShip.Sid() != target->GetMyAirShipID()) {
        return false;
    }
    CAirShip* const airShip = m_spAirShip.GetAirShip();
    if (airShip == nullptr) {
        return false;
    }
    if (target != actor && !target->IsTargetable()) {
        actor->ChangeTarget(nullptr, CTR_DEFAULT);
        return false;
    }

    const double sourceHeight = actor->m_data->GetPos().z;
    const double destinationHeight = m_vMidDest.z;
    const bool moved = CWorld::Inst().MoveToRelatively(actor, airShip, m_vMidRelDest);
    if (moved && destinationHeight + 100.0 < sourceHeight && actor->IsPC()) {
        actor->HeightDamage(sourceHeight - destinationHeight);
    }

    if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }
    if (m_bArrived) {
        SetValidPendingAction();
        return false;
    }
    if (!moved) {
        actor->GetController()->ActionBroadcastStopMove();
        SetValidPendingAction();
        return false;
    }
    if (SetMoving()) {
        return true;
    }
    SetValidPendingAction();
    return false;
}

bool CMoveToTargetRelativeAction::OnStart() {
    L2_GUARD_FUNCTION();

    CNewAction::OnStart();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || actor->GetMorphID() != 0 || !actor->IsInAirShip() || !m_spTarget.IsValid() || !actor->IsPC()) {
        return false;
    }

    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (target == nullptr || target->IsItem() || m_spAirShip.Sid() != target->GetMyAirShipID()) {
        return false;
    }
    if (target != actor && !target->IsTargetable()) {
        actor->ChangeTarget(nullptr, CTR_DEFAULT);
        return false;
    }

    actor->m_spActionTarget = m_spTarget;
    const double speed = actor->m_data->m_dParam[11];
    if (speed > 0.0) {
        if (actor->m_data->m_StopMode == STP_SIT || actor->m_data->m_StopMode == STP_STORE) {
            actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
            return false;
        }
        if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
            return false;
        }

        SetValidPendingAction();
        m_MoveToTargetType = MTTT_MOVE_TO_PAWN;
        ComputeDistance(*actor, *target);
        const FVector actorPosition = actor->GetPos();
        const FVector targetPosition = CMoveActionBase::GetTargetPos(*actor, *target);
        const double deltaX = actorPosition.x - targetPosition.x;
        const double deltaY = actorPosition.y - targetPosition.y;
        const double deltaZ = actorPosition.z - targetPosition.z;
        const double distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        const std::int32_t acceptableDistanceSquared = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(m_Data.m_nAcceptableDistance) *
            static_cast<std::uint32_t>(m_Data.m_nAcceptableDistance)
        );
        if (static_cast<double>(acceptableDistanceSquared) < distanceSquared) {
            if (distanceSquared > 36000000.0 || m_nUsedItemSID == -2) {
                return false;
            }
            m_bMoving = false;
            m_bArrived = false;
            if (SetMoving()) {
                actor->m_data->m_MovingStatus = MSTE_MOVING;
                return true;
            }
            SetValidPendingAction();
            return false;
        }

        actor->GetController()->ActionBroadcastMoveToTargetInAirShip(
            actorPosition, actor->GetRelPos(), static_cast<int>(m_spTarget.Sid()), m_Data.m_nDistance,
            actor->GetMyAirShipID()
        );
        return false;
    }

    if (actor->m_dSpeedModByWeight == 0.0) {
        if (m_Data.m_nSkillId < 1 && !m_Data.m_bForceAttack && target->IsNPC() && !target->IsEnemyTo(actor)) {
            actor->GetController()->SetAsTalk(1, m_spTarget);
        }
        actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_MOVE_INVENTORY_TOO_HEAVY);
    }
    return false;
}

void CMoveToTargetAction::Set(
    CSmartID actor,
    CSmartID target,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int skillRange,
    int usedItemSID
) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
    CCreature* const creature = actor.GetCreature();
    if (creature == nullptr) {
        return;
    }

    forceAttack = creature->CanForceAttack() && forceAttack;
    if (g_nCountryCode == 3 && creature->GetSocket() != nullptr && creature->GetSocket()->m_bEconomicRestriction) {
        forceAttack = false;
    }
    m_Data.m_bForceAttack = forceAttack;
    m_Data.m_nSkillId = skillId;
    m_Data.m_nSkillRange = skillRange;
    m_nSkillLevel = skillLevel;
    m_nSkillSubLevel = skillSubLevel;
    m_nUsedItemSID = usedItemSID;
    m_bMoving = false;
    m_bPathFinding = false;
    m_vTargetPosOnSetMoving = FVector();
}

bool CMoveToTargetAction::CondForHiddenTarget(CWorldObject* target) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return false;
    }

    if (target != nullptr && !target->IsVisible()) {
        if (!actor->IsPC()) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return false;
        }

        if (!target->IsPC()) {
            if (target->IsCreature()) {
                if (target->CastCreature()->IsSummonOrPet()) {
                    return actor->CastUser()->IsMySummonOrPet(target->CastCreature());
                }
            }
        } else {
            User* const targetUser = target->CastUser();
            User* const actorUser = actor->CastUser();
            if (actorUser->m_nPartySid == 0 || targetUser->m_nPartySid != actorUser->m_nPartySid) {
                // actor->ActionFailed(0)
                actor->ActionFailed(AF_DEFAULT);
                return false;
            }
        }
    }
    return true;
}

bool CMoveToTargetAction::CheckValidTarget(bool followTarget) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (m_spTarget.IsValid() && actor->m_data->m_bIsActive && actor->m_data->m_bIsAlive != 0) {
        CWorldObject* const target = static_cast<CWorldObject*>(m_spTarget.GetObjectW());
        if (target != nullptr && target != actor) {
            if (followTarget || (target->IsActive() && CondForHiddenTarget(target))) {
                return true;
            }
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            actor->GetController()->ActionBroadcastStopMove();
        }
    }
    return false;
}

void CMoveToTargetAction::OnEnd(bool success) {
    L2_GUARD_FUNCTION();

    CMoveActionBase::OnEnd(success);
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVETOTARGET_ONEND);
    CCreature* const actor = m_spActor.GetCreature();

    if (!actor->IsInVehicle() && !actor->IsInAirShip() && !actor->IsInShuttle() && !actor->IsCameraMode() && !success) {
        const double directionSquared = actor->m_vMovingDirection.DistCube(FVector{});
        const bool flying = static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY;

        if ((flying && directionSquared < 250000.0) || directionSquared < 90000.0) {
            std::int32_t elapsed = static_cast<std::int32_t>(runtime::GetTickCount() - actor->m_nLastTick);
            if (flying && directionSquared < 250000.0) {
                elapsed = std::clamp(elapsed, 0, MoveTick);
            }
            if (actor->m_nMoveTick == 0) {
                actor->m_nMoveTick = 1;
            }
            const double fraction =
                std::clamp(static_cast<double>(elapsed) / static_cast<double>(actor->m_nMoveTick), 0.0, 1.0);
            const FVector sourcePosition = actor->m_data->GetPos();
            FVector position{
                sourcePosition.x + fraction * actor->m_vMovingDirection.x,
                sourcePosition.y + fraction * actor->m_vMovingDirection.y,
                sourcePosition.z + fraction * actor->m_vMovingDirection.z,
            };

            if (!(flying && directionSquared < 250000.0)) {
                bool floating = actor->m_AreaMod.m_bIsUnderWater;
                int waterSurface = actor->m_AreaMod.m_nWaterSurfaceHeight;
                if (!floating) {
                    bool enteredWater = false;
                    if (actor->m_AreaMod.m_bIsNearUnderWater) {
                        enteredWater =
                            CAreaDB::inst().IsUnderWater(position, waterSurface, static_cast<int>(actor->GetInZoneID()));
                    }
                    if (enteredWater) {
                        actor->m_bFloatingMoveMode = true;
                        floating = true;
                    } else {
                        actor->m_bFloatingMoveMode = false;
                        floating = false;
                        waterSurface = 0;
                        position.z = std::max(sourcePosition.z, m_vMidDest.z);
                    }
                }
                if (floating || !flying) {
                    const int ground = CWorld::Inst().GetGeo()->FindNearGround(
                        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
                    );
                    if (floating) {
                        position.z = std::max(position.z, static_cast<double>(ground));
                    } else {
                        position.z = static_cast<double>(ground);
                    }
                }

                const double distance = std::sqrt(sourcePosition.DistCube(position));
                if (!CWorld::Inst().GetGeo()->MoveStraightTest(sourcePosition, position, distance, floating, waterSurface)) {
                    position = sourcePosition;
                }
                if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
                    actor->GetController()->ActionBroadcastMove(sourcePosition, position);
                }
            }
            CWorld::Inst().MoveTo(actor, position, false);
        }
    }

    const bool unableToMove =
        actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct;
    if (unableToMove) {
        if (m_Data.m_nSkillId != 0) {
            actor->OnUseSkillFinished(
                static_cast<int>(actor->m_spTarget.Sid()), m_Data.m_nSkillId, m_nSkillLevel, m_nSkillSubLevel, 0
            );
        }
        actor->m_data->m_bMoveTrouble = false;
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    if (unableToMove) {
        actor->m_data->m_bMoveTrouble = false;
        actor->m_data->m_dwPathFindFailCount = 0;
    }
    actor->m_data->m_MovingStatus = MSTE_NOT_MOVING;
    actor->m_bMoving = false;
}

bool CMoveToTargetAction::OnTick() {
    L2_GUARD_FUNCTION();

    CNewAction::OnTick();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVETOTARGET_ONTICK);
    CCreature* const actor = m_spActor.GetCreature();
    if (!CheckValidTarget(actor->m_FollowData.m_FollowType != 0)) {
        return false;
    }

    if (actor->IsInVehicle() || actor->IsInAirShip() || actor->IsInShuttle()) {
        if (SetMoving()) {
            return true;
        }
        SetValidPendingAction();
        return false;
    }

    const double sourceHeight = actor->m_data->GetPos().z;
    const double destinationHeight = m_vMidDest.z;
    const bool moved = CWorld::Inst().MoveTo(actor, m_vMidDest, false);
    actor->CheckShowMovePos(0, m_vMidDest);
    if (moved && destinationHeight + 100.0 < sourceHeight && actor->IsPC()) {
        actor->HeightDamage(sourceHeight - destinationHeight);
    }

    if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
        actor->GetController()->ActionBroadcastStopMove();
        return false;
    }
    if (m_bArrived) {
        SetValidPendingAction();
        return false;
    }
    if (!moved) {
        actor->GetController()->ActionBroadcastStopMove();
        SetValidPendingAction();
        return false;
    }
    if (SetMoving()) {
        return true;
    }
    SetValidPendingAction();
    return false;
}

bool CMoveToTargetAction::OnStart() {
    L2_GUARD_FUNCTION();

    CNewAction::OnStart();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVETOTARGET_ONSTART);
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || (actor->GetMorphID() != 0 && !actor->IsSummonOrPet())) {
        return false;
    }

    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (target == nullptr || !CheckValidTarget(actor->m_FollowData.m_FollowType != 0)) {
        return false;
    }
    actor->m_spActionTarget = m_spTarget;

    if (actor->IsPC() && target->IsPC() && actor->CastUser()->m_nCoupleID != 0 && target->CastUser()->m_nCoupleID != 0) {
        if (actor->CastUser()->m_data->m_bIsFakeDeath || target->CastUser()->m_data->m_bIsFakeDeath) {
            return false;
        }

        const bool canSee =
            CWorld::Inst().GetGeo()->CanSee(actor, target->CastWorldObject(), actor->IsPC() && target->IsCreature());
        const bool floating =
            static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY || actor->m_AreaMod.m_bIsUnderWater;
        FVector targetPosition = target->GetPos();
        if (!canSee || !CWorld::Inst().CanGo(actor->GetPos(), targetPosition, 0, floating)) {
            User* const actorUser = actor->CastUser();
            User* const targetUser = target->CastUser();
            actorUser->InitCoupleParam();
            targetUser->InitCoupleParam();
            actorUser->GetSocket()->SendSystemMessage(SystemMessageId::REQUEST_FAILED_TARGET_POSITION_REQUIREMENTS_NOT_MET);
            targetUser->GetSocket()->SendSystemMessage(SystemMessageId::REQUEST_FAILED_TARGET_POSITION_REQUIREMENTS_NOT_MET);
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return false;
        }
    }

    if (actor->m_data->m_dParam[11] <= 0.0) {
        if (actor->IsPC() && actor->m_dSpeedModByWeight == 0.0) {
            if (m_Data.m_nSkillId < 1 && !m_Data.m_bForceAttack && target->IsNPC() && !target->IsEnemyTo(actor)) {
                actor->GetController()->SetAsTalk(1, m_spTarget);
            }
            actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_MOVE_INVENTORY_TOO_HEAVY);
        }
        return false;
    }
    if (actor->IsInVehicle() || actor->IsInShuttle()) {
        if (actor->IsPC()) {
            actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_RIDING_FERRY);
        }
        return false;
    }
    if (actor->IsPC() && (actor->m_data->m_StopMode == STP_SIT || actor->m_data->m_StopMode == STP_STORE)) {
        actor->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
        return false;
    }
    if (actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct) {
        return false;
    }

    SetValidPendingAction();
    m_MoveToTargetType = MTTT_MOVE_TO_PAWN;
    if (actor->m_FollowData.m_FollowType == 0 && actor->IsNPC() && actor->GetAttackRange() < 500) {
        m_MoveToTargetType = MTTT_MOVE_TO_MELEE_POINT;
    }
    actor->CheckRelaxType();
    ComputeDistance(*actor, *target);

    const FVector actorPosition = actor->GetPos();
    const FVector targetPosition = CMoveActionBase::GetTargetPos(*actor, *target);
    const bool needed = NeedMove(*actor, *target, static_cast<double>(m_Data.m_nAcceptableDistance));
    bool shouldMove = needed;
    if (!needed && !actor->IsPC()) {
        shouldMove =
            !CWorld::Inst().GetGeo()->CanSee(actorPosition, targetPosition, static_cast<int>(actor->GetInZoneID()), false);
    }
    if (!shouldMove) {
        actor->m_data->m_bMoveTrouble = false;
        actor->m_data->m_dwPathFindFailCount = 0;
        actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
        return false;
    }

    if (needed && (actorPosition.DistCube(targetPosition) > 36000000.0 || m_nUsedItemSID == -2)) {
        return false;
    }

    m_bMoving = false;
    m_bArrived = false;
    if (SetMoving()) {
        actor->m_data->m_MovingStatus = MSTE_MOVING;
        actor->CheckShowMovePos(1, m_vTargetPosOnSetMoving);
        actor->CheckShowMovePos(0, m_vMidDest);
        return true;
    }
    SetValidPendingAction();
    return false;
}

bool CMoveToTargetAction::SetMoving() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVETOTARGET_SETMOVING);

    CCreature* const actor = m_spActor.GetCreature();
    CWorldObject* const target = m_spTarget.GetWorldObject();
    if (actor == nullptr || target == nullptr) {
        return false;
    }

    actor->ValidateMoveToTargetSpeedAndSendInfo();
    FVector currentPosition = actor->m_data->GetPos();
    FVector targetPosition = CMoveActionBase::GetTargetPos(*actor, *target);
    const double speed = actor->m_data->m_dParam[11];

    if (!NeedMove(*actor, *target, static_cast<double>(m_Data.m_nAcceptableDistance))) {
        if (!actor->IsPC() &&
            !CWorld::Inst().GetGeo()->CanSee(
                currentPosition, targetPosition, static_cast<int>(actor->GetInZoneID()), false
            )) {
            // Current falls through to the ordinary move path when a
            // non-player target is within range but out of sight.
        } else {
            actor->m_data->m_bMoveTrouble = false;
            actor->m_data->m_dwPathFindFailCount = 0;
            actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
            return false;
        }
    }

    if (m_MoveToTargetType == MTTT_MOVE_TO_MELEE_POINT) {
        targetPosition = target->GetMeleeAttackPosition(actor);
    } else {
        targetPosition = GetTargetDestination(*actor, *target);
    }

    if (actor->IsInVehicle() || actor->IsInAirShip() || actor->IsInShuttle()) {
        actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
        m_nTick = MoveTick;
        AddTimer(m_nTick, 0, 0);
        return true;
    }

    bool routeSuccessful = true;
    if (static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY) {
        CalcMidDest(currentPosition, targetPosition, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(m_vMidDest.y - currentPosition.y, m_vMidDest.x - currentPosition.x) * 65535.0 / 6.283185307179586
        );
        actor->m_data->SetDir(direction);

        const bool collided = g_WorldPlaneCollision.CheckCollision(
            currentPosition, m_vMidDest, actor->GetCollisionRadius(), m_vMidDest, static_cast<int>(actor->GetInZoneID()),
            false
        );
        if (!routeSuccessful || collided) {
            actor->GetController()->ActionBroadcastMove(currentPosition, m_vMidDest);
            actor->m_vMovingDest = m_vMidDest;
            actor->m_vMovingDirection = FVector{
                m_vMidDest.x - currentPosition.x,
                m_vMidDest.y - currentPosition.y,
                m_vMidDest.z - currentPosition.z,
            };
            actor->m_bMoving = true;
            m_nTick = MoveTick;
        } else {
            if (!m_bMoving) {
                m_nTick = std::min(m_nTick, MoveTick);
                CItem* const item = m_spTarget.GetItem();
                if (item == nullptr) {
                    actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
                    actor->m_vMovingDest = target->GetPos();
                } else {
                    actor->GetController()->ActionBroadcastMove(currentPosition, targetPosition);
                    actor->m_vMovingDest = targetPosition;
                }
                m_bMoving = true;
            }
            actor->m_bMoving = true;
            actor->m_vMovingDirection = FVector{
                m_vMidDest.x - currentPosition.x,
                m_vMidDest.y - currentPosition.y,
                m_vMidDest.z - currentPosition.z,
            };
            actor->m_nLastTick = runtime::GetTickCount();
            actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
        }
        AddTimer(m_nTick, 0, 0);
        return true;
    }

    if (!actor->IsCameraMode()) {
        if (actor->IsPC()) {
            CalcMidDest(
                currentPosition, targetPosition, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor
            );
            FAngle direction{};
            direction.Yaw = runtime::Win64CvttDoubleToInt32(
                std::atan2(m_vMidDest.y - currentPosition.y, m_vMidDest.x - currentPosition.x) * 65535.0 / 6.283185307179586
            );
            actor->m_data->SetDir(direction);

            bool blocked = g_WorldPlaneCollision.CheckCollision(
                currentPosition, m_vMidDest, actor->GetCollisionRadius(), m_vMidDest, static_cast<int>(actor->GetInZoneID()),
                false
            );
            CContainerIndex neighbors = CContainerDB::Inst().GatherNeighborContainerIndex(
                CContainerDB::Inst().GetContainer(actor->GetContainerIndex())
            );
            for (int index = 0; index < neighbors.n; ++index) {
                CContainer* const container = neighbors.container[static_cast<std::size_t>(index)];
                L2_GUARD(container->m_BlockTerritoryLock);
                for (CTerritory* const territory : container->m_lBlockTerritoryList) {
                    if (territory->CheckIntersectAndGetPoint(currentPosition, m_vMidDest, true)) {
                        blocked = true;
                    }
                }
            }

            if (!routeSuccessful || blocked) {
                actor->GetController()->ActionBroadcastMove(currentPosition, m_vMidDest);
                actor->m_vMovingDest = m_vMidDest;
                actor->m_vMovingDirection = FVector{
                    m_vMidDest.x - currentPosition.x,
                    m_vMidDest.y - currentPosition.y,
                    m_vMidDest.z - currentPosition.z,
                };
                actor->m_bMoving = true;
                m_nTick = MoveTick;
            } else {
                const FVector middleDestination = m_vMidDest;
                if (!m_bMoving) {
                    m_vMidDest = middleDestination;
                    m_nTick = std::min(m_nTick, MoveTick);
                    if (!target->IsItem()) {
                        actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
                        actor->m_vMovingDest = target->GetPos();
                    } else {
                        actor->GetController()->ActionBroadcastMove(currentPosition, targetPosition);
                        actor->m_vMovingDest = targetPosition;
                    }
                    m_bMoving = true;
                }
                actor->m_bMoving = true;
                actor->m_vMovingDirection = FVector{
                    middleDestination.x - currentPosition.x,
                    middleDestination.y - currentPosition.y,
                    middleDestination.z - currentPosition.z,
                };
                actor->m_nLastTick = runtime::GetTickCount();
                actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
            }
            AddTimer(m_nTick, 0, 0);
            return true;
        }

        PathFindResult pathResult = PFR_CAN_GO;
        if (!target->IsCreature()) {
            const FVector targetWorldPosition = target->GetPos();
            pathResult = g_pnWorld.PathFind(currentPosition, targetWorldPosition, targetPosition, 1000, actor);
        } else {
            CCreature* const targetCreature = target->CastCreature();
            targetPosition = targetCreature->GetPassingPosOnSight(currentPosition, targetPosition, pathResult, actor);
        }
        if (pathResult == PFR_ERROR) {
            actor->m_data->m_bMoveTrouble = true;
            ++actor->m_data->m_dwPathFindFailCount;
        } else {
            actor->m_data->m_bMoveTrouble = false;
            actor->m_data->m_dwPathFindFailCount = 0;

            CCreature* const targetCreature = target->CastCreature();
            if (g_bMoveToLeader && targetCreature != nullptr && actor->IsValidAttackLeaderCandidate()) {
                const CCreature::Position position = actor->GetMyPositionFrom(targetCreature);
                CCreature* attackLeader =
                    g_CreatureSmartIdManager.GetObjectW(targetCreature->m_attackLeaderId[static_cast<int>(position)].load());
                if (attackLeader == actor) {
                    attackLeader = nullptr;
                }
                if (attackLeader == nullptr || !attackLeader->IsValidAttackLeaderCandidate() ||
                    actor->IsCloserThan(targetCreature, attackLeader->GetPos())) {
                    targetCreature->SetAttackLeaderRelated(actor, attackLeader);
                }
            }
        }

        CalcMidDest(currentPosition, targetPosition, speed, m_vMidDest, m_nTick, m_bArrived, routeSuccessful, false, actor);
        FAngle direction{};
        direction.Yaw = runtime::Win64CvttDoubleToInt32(
            std::atan2(m_vMidDest.y - currentPosition.y, m_vMidDest.x - currentPosition.x) * 65535.0 / 6.283185307179586
        );
        actor->m_data->SetDir(direction);

        bool blocked = false;
        if ((g_CheckCollisionForNPCMove || actor->IsPet()) && !actor->m_data->m_bPassableDoor) {
            blocked = g_WorldPlaneCollision.CheckCollision(
                currentPosition, m_vMidDest, actor->GetCollisionRadius(), m_vMidDest, static_cast<int>(actor->GetInZoneID()),
                false
            );
        }
        CContainerIndex neighbors =
            CContainerDB::Inst().GatherNeighborContainerIndex(CContainerDB::Inst().GetContainer(actor->GetContainerIndex()));
        for (int index = 0; index < neighbors.n; ++index) {
            CContainer* const container = neighbors.container[static_cast<std::size_t>(index)];
            L2_GUARD(container->m_BlockTerritoryLock);
            for (CTerritory* const territory : container->m_lBlockTerritoryList) {
                if (territory->CheckIntersectAndGetPoint(currentPosition, m_vMidDest, true)) {
                    blocked = true;
                }
            }
        }

        const FVector middleDestination = m_vMidDest;
        m_vMidDest = middleDestination;
        m_nTick = std::max(std::min(m_nTick, MoveTick), 700);
        actor->GetController()->ActionBroadcastMove(currentPosition, blocked ? m_vMidDest : targetPosition);
        actor->m_vMovingDest = m_vMidDest;
        actor->m_bMoving = true;
        actor->m_vMovingDirection = FVector{
            m_vMidDest.x - currentPosition.x,
            m_vMidDest.y - currentPosition.y,
            m_vMidDest.z - currentPosition.z,
        };
        actor->m_nMoveTick = static_cast<std::uint32_t>(m_nTick);
        actor->m_nLastTick = runtime::GetTickCount();
        m_bPathFinding = true;
        m_bMoving = true;
        AddTimer(m_nTick, 0, 0);
        return true;
    }

    const double stepDistance = speed * 800.0 / 1000.0;
    const double squaredDistance = currentPosition.DistCube(targetPosition);
    if (squaredDistance >= 10.0) {
        FVector direction{
            targetPosition.x - currentPosition.x,
            targetPosition.y - currentPosition.y,
            targetPosition.z - currentPosition.z,
        };
        direction.NormalizeCube();
        direction = FVector{
            direction.x * stepDistance,
            direction.y * stepDistance,
            direction.z * stepDistance,
        };
        if (stepDistance <= squaredDistance) {
            m_bArrived = false;
            m_nTick = MoveTick;
            m_vMidDest = FVector{
                currentPosition.x + direction.x,
                currentPosition.y + direction.y,
                currentPosition.z + direction.z,
            };
            if (!actor->m_AreaMod.m_bIsUnderWater) {
                const int ground = CWorld::Inst().GetGeo()->FindGround(
                    runtime::Win64CvttDoubleToInt32(m_vMidDest.x), runtime::Win64CvttDoubleToInt32(m_vMidDest.y),
                    runtime::Win64CvttDoubleToInt32(m_vMidDest.z)
                );
                if (std::abs(ground - runtime::Win64CvttDoubleToInt32(m_vMidDest.z)) < 100) {
                    m_bArrived = true;
                }
            }
        } else {
            m_bArrived = true;
            m_nTick = runtime::Win64CvttDoubleToInt32(std::sqrt(squaredDistance) / stepDistance * 800.0);
            m_vMidDest = targetPosition;
        }
    } else {
        m_vMidDest = currentPosition;
        m_nTick = 80;
        m_bArrived = true;
    }

    if (!target->IsItem() && !target->IsStaticObject()) {
        actor->GetController()->ActionBroadcastMoveToTarget(*target, m_Data.m_nDistance);
    } else if (!m_bMoving) {
        actor->GetController()->ActionBroadcastMove(currentPosition, targetPosition);
        actor->m_vMovingDest = targetPosition;
    }
    m_bMoving = true;
    AddTimer(m_nTick, 0, 0);
    return true;
}

void CMoveToTargetAction::SetValidPendingAction() {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NEWACTION__MOVETOTARGET_SETVALIDPENDINGACTION);

    CWorldObject* const target = m_spTarget.GetWorldObject();
    CCreature* const actor = m_spActor.GetCreature();
    CCreatureController* const controller = actor->GetController();

    if (target->IsItem()) {
        controller->SetAsGetItem(1, target->GetSmartID());
        return;
    }

    if (target->IsHolyThing() || target->IsFortressFlagpole()) {
        if (m_Data.m_nSkillId < 1) {
            controller->SetAsNothing(1);
        } else {
            controller->SetAsSkill(
                1, target->GetSmartID(), m_skilluseinfo, m_Data.m_bForceAttack, false,
                static_cast<unsigned int>(m_nUsedItemSID)
            );
        }
        return;
    }

    if (target->IsDoor()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, target->GetSmartID(), m_skilluseinfo, m_Data.m_bForceAttack, false,
                static_cast<unsigned int>(m_nUsedItemSID)
            );
            return;
        }

        CDoor* const door = target->CastDoor();
        const bool attackDoor = door->m_data->m_bBreakable && (door->m_InstantZoneKey.typeId != 0 || door->IsEnemyTo(actor));
        if (attackDoor) {
            controller->SetAsAttack(1, m_spTarget, false);
        } else {
            controller->SetAsOpenClose(1, target->GetSmartID());
        }
        return;
    }

    if (target->IsChair()) {
        controller->SetAsNothing(1);
        return;
    }

    if (target->IsBattleCamp()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, m_spTarget, m_skilluseinfo, m_Data.m_bForceAttack, false, static_cast<unsigned int>(m_nUsedItemSID)
            );
            return;
        }
        if (!m_Data.m_bForceAttack && actor->IsPC() && !target->IsEnemyTo(actor)) {
            controller->SetAsNothing(1);
            return;
        }
        if (!m_Data.m_bForceAttack && actor->IsSummonOrPet() && !target->IsEnemyTo(actor)) {
            return;
        }
        controller->SetAsAttack(1, m_spTarget, false);
        return;
    }

    if (target->IsControlTower()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, m_spTarget, m_skilluseinfo, m_Data.m_bForceAttack, false, static_cast<unsigned int>(m_nUsedItemSID)
            );
            return;
        }
        if (m_Data.m_bForceAttack) {
            controller->SetAsAttack(1, m_spTarget, false);
            return;
        }

        CControlTower* const tower = target->CastControlTower();
        CResidence* defendingResidence = nullptr;
        if (tower->m_pOwningResidence != nullptr && tower->m_pOwningResidence->IsDominion()) {
            CDominion* const dominion = tower->m_pOwningResidence->CastDominion();
            if (dominion != nullptr) {
                defendingResidence = tower->m_pOwningResidence;
            }
        }
        bool attackTower =
            tower->m_data->m_bBreakable && (tower->m_bTogglable || tower->m_eStatus != ControlTowerStatusEnum::Destroyed);
        if (attackTower && actor->IsPC()) {
            const int pledgeId = static_cast<int>(actor->m_data->m_nPledgeID);
            attackTower = pledgeId == 0 || defendingResidence == nullptr ||
                !defendingResidence->IsDefendingPledge(pledgeId, actor->m_data->m_nWorldId) ||
                tower->m_eStatus == ControlTowerStatusEnum::Destroyed;
        }
        if (attackTower) {
            controller->SetAsAttack(1, m_spTarget, false);
        } else {
            controller->SetAsNothing(1);
        }
        return;
    }

    if (target->IsCreature()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, m_spTarget, m_skilluseinfo, m_Data.m_bForceAttack, false, static_cast<unsigned int>(m_nUsedItemSID)
            );
            return;
        }

        if (actor->IsPC() && target->IsPC() && actor->CastUser()->m_nCoupleID != 0 && target->CastUser()->m_nCoupleID != 0) {
            controller->SetAsCouple(1, 0x9f6, m_spTarget);
            return;
        }
        if (actor->m_FollowData.m_FollowType > 0) {
            ComputeDistance(*actor, *target);
            controller->SetAsMoveToTargetWait(1, m_spTarget, m_Data.m_nAcceptableDistance);
            return;
        }

        if ((m_Data.m_bForceAttack || target->IsEnemyTo(actor)) && actor->CanNormalAttack()) {
            bool surely = false;
            if (actor->IsNPC()) {
                const FVector actorPosition = actor->GetPos();
                CCreature* const targetCreature = target->CastCreature();
                const double deltaX = actorPosition.x - m_vTargetPosOnSetMoving.x;
                const double deltaY = actorPosition.y - m_vTargetPosOnSetMoving.y;
                surely = CAttackAction::CheckAttackable(
                    static_cast<double>(actor->m_data->m_nAttackRange), actor->GetCollisionRadius(),
                    target->GetCollisionRadius(), deltaX * deltaX + deltaY * deltaY, actor->m_data->m_dCollisionHeight,
                    targetCreature->m_data->m_dCollisionHeight, runtime::Win64CvttDoubleToInt32(actorPosition.z),
                    runtime::Win64CvttDoubleToInt32(m_vTargetPosOnSetMoving.z), actor->GetAttackType()
                );
            }
            controller->SetAsAttack(1, m_spTarget, surely);
            return;
        }

        if (actor->IsPC() && target->IsNPC()) {
            if (target->IsEnemyTo(actor)) {
                controller->ActionBroadcastStopMove();
                return;
            }

            const int heightDifference = std::abs(
                runtime::Win64CvttDoubleToInt32(actor->GetPos().z) -
                runtime::Win64CvttDoubleToInt32(target->GetPos().z)
            );
            if (heightDifference <= 99) {
                controller->SetAsTalk(1, m_spTarget);
                return;
            }

            controller->SetAsNothing(1);
            CNPC* const npc = target->CastNPC();
            if (npc == nullptr) {
                return;
            }
            const int closeHeightDifference = std::abs(
                runtime::Win64CvttDoubleToInt32(actor->GetPos().z) - runtime::Win64CvttDoubleToInt32(npc->GetPos().z)
            );
            const int classId = npc->m_data->m_nClass;
            const bool talkTeleportClass = classId == 0x3d || classId == 0x3e || classId == 0x3f || classId == 0x40 ||
                classId == 0x41 || classId == 0x42 || classId == 0x44 || classId == 0x45 || classId == 0x46 ||
                classId == 0x4b;
            if (closeHeightDifference < 500 && talkTeleportClass) {
                const FVector npcPosition = npc->GetPos();
                const int ground = CWorld::Inst().GetGeo()->FindGround(
                    runtime::Win64CvttDoubleToInt32(npcPosition.x), runtime::Win64CvttDoubleToInt32(npcPosition.y),
                    runtime::Win64CvttDoubleToInt32(npcPosition.z)
                );
                const int groundDifference = std::abs(runtime::Win64CvttDoubleToInt32(npcPosition.z) - ground);
                if (groundDifference >= 101 && groundDifference <= 499) {
                    const FVector actorPosition = actor->GetPos();
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"jongber ttp 60204, NPC Teleport to talk z "
                    //     L"org[%d] mod[%d] name[%s], user x[%d] y[%d] "
                    //     L"z[%d]",
                    //     runtime::Win64CvttDoubleToInt32(
                    //         npcPosition.z),
                    //     ground,
                    //     npc->GetName(),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.x),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.y),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.z))
                    // L2_LOG_ERROR(Log,
                    //     L"jongber ttp 60204, NPC Teleport to talk z "
                    //     L"org[%d] mod[%d] name[%s], user x[%d] y[%d] "
                    //     L"z[%d]",
                    //     runtime::Win64CvttDoubleToInt32(
                    //         npcPosition.z),
                    //     ground,
                    //     npc->GetName(),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.x),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.y),
                    //     runtime::Win64CvttDoubleToInt32(
                    //         actorPosition.z))
                    // L2_LOG_WARN(Log,
                    //     "Correcting talk NPC '%s' altitude from %.17g to %d. Player position is (%.17g, %.17g, %.17g).",
                    //     WideToUtf8(npc->GetName()).c_str(),
                    //     npcPosition.z,
                    //     ground,
                    //     actorPosition.x,
                    //     actorPosition.y,
                    //     actorPosition.z)
                    L2_LOG_WARN(
                        Log, "Correcting talk NPC '%s' altitude from %.17g to %d; player position is (%.17g, %.17g, %.17g).",
                        Utf16ToUtf8(npc->GetName()).c_str(), npcPosition.z, ground, actorPosition.x, actorPosition.y,
                        actorPosition.z
                    );
                    npc->TeleportToLocation(
                        runtime::Win64CvttDoubleToInt32(npcPosition.x), runtime::Win64CvttDoubleToInt32(npcPosition.y),
                        ground, static_cast<std::uint16_t>(npc->GetDir().Yaw), static_cast<int>(npc->GetInZoneID()),
                        TeleportSourceType::NPCTeleportToTalk, actor, 0
                    );
                }
            }
            return;
        }

        if (actor->IsPC() && target->IsPC()) {
            if (target->CastUser()->GetPrivateStoreType() != 0) {
                controller->SetAsGeneral(1, m_spTarget, CGeneralAction::GAT_PRIVATE_STORE);
            } else {
                ComputeDistance(*actor, *target);
                controller->SetAsMoveToTargetWait(1, m_spTarget, m_Data.m_nAcceptableDistance);
            }
        }
        return;
    }

    if (target->IsSignBoard() || target->IsStatue()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, m_spTarget, m_skilluseinfo, m_Data.m_bForceAttack, false, static_cast<unsigned int>(m_nUsedItemSID)
            );
        } else if (actor->IsPC()) {
            controller->SetAsTalk(1, m_spTarget);
        }
        return;
    }

    if (target->IsOwnThing()) {
        if (m_Data.m_nSkillId > 0) {
            controller->SetAsSkill(
                1, m_spTarget, m_skilluseinfo, m_Data.m_bForceAttack, false, static_cast<unsigned int>(m_nUsedItemSID)
            );
            return;
        }
        if (m_Data.m_bForceAttack) {
            controller->SetAsAttack(1, m_spTarget, false);
            return;
        }

        controller->SetAsAttack(1, m_spTarget, false);
        if (actor->IsPC() && !target->IsEnemyTo(actor)) {
            controller->SetAsNothing(1);
            return;
        }
        if (actor->IsSummonOrPet() && !target->IsEnemyTo(actor)) {
            return;
        }
        controller->SetAsAttack(1, m_spTarget, false);
        return;
    }

    if (!target->IsAirShipSteering() && !target->IsAirShip()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] what target? pActor[%x] pTarget id[%x]",
        //     L"newaction_move.cpp",
        //     0x1028,
        //     static_cast<int>(actor->m_nId),
        //     static_cast<int>(target->m_nId))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] what target? pActor[%x] pTarget id[%x]",
        //     L"newaction_move.cpp",
        //     0x1028,
        //     static_cast<int>(actor->m_nId),
        //     static_cast<int>(target->m_nId))
        L2_LOG_ERROR(
            Log,
            "Cannot select a pending move-to-target action for actor %u and target %u: unsupported target type; no action "
            "is selected.",
            static_cast<unsigned>(actor->m_nId), static_cast<unsigned>(target->m_nId)
        );
    }
}

void CMoveToTargetAction::Set(
    CSmartID actor, CSmartID target, bool forceAttack, const SkillUseInfo& skillUseInfo, int usedItemSID
) {
    L2_GUARD_FUNCTION();

    const int skillId = skillUseInfo.m_skillID;
    const int skillLevel = skillUseInfo.m_skillLevel;
    const int skillSubLevel = skillUseInfo.m_skillSubLevel;
    const int skillRange = skillUseInfo.m_castRange;
    m_spActor = actor;
    m_spTarget = target;
    CCreature* const creature = actor.GetCreature();
    if (creature == nullptr) {
        return;
    }

    forceAttack = creature->CanForceAttack() && forceAttack;
    if (g_nCountryCode == 3 && creature->GetSocket() != nullptr && creature->GetSocket()->m_bEconomicRestriction) {
        forceAttack = false;
    }
    m_Data.m_bForceAttack = forceAttack;
    m_Data.m_nSkillId = skillId;
    m_Data.m_nSkillRange = skillRange;
    m_nSkillLevel = skillLevel;
    m_nSkillSubLevel = skillSubLevel;
    m_nUsedItemSID = usedItemSID;
    m_skilluseinfo = skillUseInfo;
    m_bMoving = false;
    m_bPathFinding = false;
    m_vTargetPosOnSetMoving = FVector();
}

void CMoveToTargetWaitAction::Set(CSmartID actor, CSmartID target, int acceptableDistance) {
    L2_GUARD_FUNCTION();

    m_spActor = actor;
    m_spTarget = target;
    m_acceptableDist = acceptableDistance;
}
