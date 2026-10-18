#include "FieldCycle.h"
#include "Shared/src/MemorySub.h"

#include "Area.h"
#include "DB.h"
#include "Door.h"
#include "FieldCycleMissionData.h"
#include "FieldCycleStuff.h"
#include "IOCPMessageServer.h"
#include "NpcServer.h"
#include "ParserFieldCycle.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <utility>

namespace {

constexpr std::int64_t kCycleNotifyPointMilliseconds = 3000;
constexpr int kTimerCycleHeartbeat = 0x3ea;
constexpr int kTimerAddPoint = 0x3eb;
constexpr int kTimerNotifyPoint = 0x3ec;

// Diagnostic display only. Preserve the numeric reason in each operator record.
const char* FieldCycleReasonName(FieldCycleReason::Value reason) noexcept {
    switch (reason) {
        case FieldCycleReason::ByPointChange:
            return "point change";
        case FieldCycleReason::ByNPCAI_1:
        case FieldCycleReason::ByNPCAI_2:
        case FieldCycleReason::ByNPCAI_3:
        case FieldCycleReason::ByNPCAI_4:
        case FieldCycleReason::ByNPCAI_5:
        case FieldCycleReason::ByNPCAI_8:
            return "NPC AI";
        case FieldCycleReason::ByBuilder:
            return "builder command";
        case FieldCycleReason::ByMultiSell:
            return "multisell";
        case FieldCycleReason::ByTimeInRange:
            return "scheduled time range";
        case FieldCycleReason::ByExpirationTime:
            return "step expiration";
        case FieldCycleReason::ByMissionLimitTime:
            return "mission time limit";
        case FieldCycleReason::ByMissionComplete:
            return "mission completion";
        case FieldCycleReason::ByUnlockAccumulate:
            return "accumulated-point unlock";
        case FieldCycleReason::ByLockExpired:
            return "point-lock expiration";
        case FieldCycleReason::ByPointTimer:
            return "point timer";
        case FieldCycleReason::ByNpcSetPoint:
            return "NPC point assignment";
        case FieldCycleReason::ByUnitTest:
            return "unit test";
        case FieldCycleReason::ByDropTime:
            return "point-drop interval";
        default:
            return "unknown";
    }
}


void WriteLOG_FIELD_MISSION_BEGIN(
    User* const user,
    const int fieldCycleId,
    const int step,
    const int questId,
    const int classId,
    const int level,
    const int previousQuestId
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x155, user, nullptr, nullptr, nullptr, nullptr, fieldCycleId, step, questId, classId, level, previousQuestId, 0, 0,
        0, 0, 0
    );
}

void WriteLOG_FIELD_MISSION_COMPLETE(
    User* const user, const int fieldCycleId, const int step, const int questId, const int completePoint
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x156, user, nullptr, nullptr, nullptr, nullptr, fieldCycleId, step, questId, completePoint, 0, 0, 0, 0, 0, 0, 0
    );
}

void WriteLOG_FIELD_CYCLE_USER_ENTER(
    User* const user, const int fieldCycleId, const int step, const int questId, const int classId, const int level
) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x153, user, nullptr, nullptr, nullptr, nullptr, fieldCycleId, step, questId, classId, level, 0, 0, 0, 0, 0, 0
    );
}

void WriteLOG_FIELD_CYCLE_USER_LEAVE(User* const user, const int fieldCycleId, const int step, const int questId) {
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x154, user, nullptr, nullptr, nullptr, nullptr, fieldCycleId, step, questId, 0, 0, 0, 0, 0, 0, 0, 0
    );
}

bool GiveQuestTo(User* const user, const int questId) {
    // Current L2Server 0x0072c418.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x696,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x696,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot assign field-cycle quest %d: the user is null; no quest is assigned.", questId);
        return false;
    }

    L2_GUARD(*user->m_lock);
    if (user->AcceptQuest(questId)) {
        user->SendQuestList(nullptr);
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[FIELD_CYCLE][%s][%d]Failed to accept quest: %d, %d",
    //     L"FieldCycle.cpp",
    //     0x69d,
    //     user->GetDBID(),
    //     questId)
    // L2_LOG_ERROR(Log,
    //     L"[FIELD_CYCLE][%s][%d]Failed to accept quest: %d, %d",
    //     L"FieldCycle.cpp",
    //     0x69d,
    //     user->GetDBID(),
    //     questId)
    L2_LOG_WARN(
        Log, "Character DBID %u did not accept field-cycle quest %d; the assignment failed.",
        static_cast<unsigned int>(user->GetDBID()), questId
    );
    return false;
}

bool IsFieldCycleInProgress(CFieldCycleSystem* const system, const int fieldCycleId) {
    // Current L2Server 0x0072c914.
    L2_GUARD_FUNCTION();
    if (fieldCycleId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x97d,
        //     L"(nFieldCycleID != 0)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x97d,
        //     L"(nFieldCycleID != 0)")
        L2_LOG_ERROR(Log, "Cannot check field-cycle mission progress: cycle ID is 0; the check returns false.");
        return false;
    }
    CFieldCycle* const fieldCycle = system->GetFieldCycle(fieldCycleId);
    return fieldCycle != nullptr && fieldCycle->IsMissionInProgress();
}


void WriteLOG_NPC_GET_STEP_POINT(
    const int point,
    const int reason,
    CCreature* const creature,
    const int fieldCycleId,
    const int currentPoint,
    const int accumulatedPoint
) {
    L2_GUARD_FUNCTION();
    CLogFormatter formatter;
    formatter.WriteLog2(0x78, creature, nullptr, fieldCycleId, reason, point, currentPoint, accumulatedPoint, 0, 0);
}

void SendBuildCommandFailureTo(CSocket* const socket) {
    L2_GUARD_FUNCTION();
    if (socket != nullptr) {
        static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", u"//field_cycle : invalid fieldID");
    }
}

DefaultIOBuffer* MakeConstBufferFrom_S_EX_FIELD_EVENT_POINT(const int point) {
    // Current L2Server.exe 0x0072bc28.
    L2_GUARD_FUNCTION();
    protocol::S_EX_FIELD_EVENT_POINT packet;
    packet.nPoint = point;
    return MakeConstBufferFromPacketEx(static_cast<char>(0xfe), 0x1b7, packet);
}

DefaultIOBuffer* MakeConstBufferFrom_S_EX_FIELD_EVENT_STEP(const int step, const int goalPoint, const int remainSecond) {
    // Current L2Server.exe 0x0072c204.
    L2_GUARD_FUNCTION();
    protocol::S_EX_FIELD_EVENT_STEP packet;
    packet.nStep = step;
    packet.nRemainSec = remainSecond;
    packet.nGoalPoint = goalPoint;
    return MakeConstBufferFromPacketEx(static_cast<char>(0xfe), 0x1b6, packet);
}

void NotifyFieldEventPointTo(User* const user, const int point) {
    // Current L2Server 0x0072c130.
    L2_GUARD_FUNCTION();
    protocol::S_EX_FIELD_EVENT_POINT packet;
    packet.nPoint = point;
    user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1b7), packet);
}

void NotifyFieldEventStepTo(User* const user, const int step, const int goalPoint, const int remainSecond) {
    // Current L2Server 0x0072cbb8.
    L2_GUARD_FUNCTION();
    protocol::S_EX_FIELD_EVENT_STEP packet;
    packet.nStep = step;
    packet.nRemainSec = remainSecond;
    packet.nGoalPoint = goalPoint;
    user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1b6), packet);
}

void NotifyFieldEventResetTo(User* const user) {
    // Current L2Server 0x0072c3c8 is the separate unguarded all-zero
    // S_EX_FIELD_EVENT_STEP producer kept by OnUserLeave.
    protocol::S_EX_FIELD_EVENT_STEP packet;
    packet.nStep = 0;
    packet.nRemainSec = 0;
    packet.nGoalPoint = 0;
    user->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1b6), packet);
}

int MissionStatus(const CFieldCycle::MissionMode& mode, int step) noexcept {
    if (step == 0) {
        return 0;
    }
    if (step < mode.firstStep) {
        return 1;
    }
    if (mode.firstStep != 0) {
        if (step == mode.successStep) {
            return 3;
        }
        if (step != mode.failStep) {
            return 2;
        }
    }
    if (step == mode.successStep) {
        return 3;
    }
    return step == mode.failStep ? 4 : 0;
}

template <typename TValue>
bool Contains(const std::vector<TValue>& values, const TValue& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void DeactivateAreas(StepInfo* const previous, const StepInfo* const current) {
    // Current L2Server 0x0072d060.
    L2_GUARD_FUNCTION();
    for (CArea* const area : previous->areaOnList) {
        if (area != nullptr && !Contains(current->areaOnList, area)) {
            area->SetOnOff(false);
        }
    }
}

void ActivateAreas(StepInfo* const current) {
    // Current L2Server 0x0072d148.
    L2_GUARD_FUNCTION();
    for (CArea* const area : current->areaOnList) {
        if (area != nullptr && !area->IsOn(0)) {
            area->SetOnOff(true);
        }
    }
}

void CloseDoors(StepInfo* const previous, const StepInfo* const current) {
    // Current L2Server 0x0072d218.
    L2_GUARD_FUNCTION();
    for (void* const rawDoor : previous->openDoorList) {
        auto* const door = static_cast<CDoor*>(rawDoor);
        if (door != nullptr && !Contains(current->openDoorList, rawDoor) && door->GetDoorOpenMethod() == DOOROPEN_CYCLE) {
            door->Close(false, true);
        }
    }
}

void OpenDoors(StepInfo* const current) {
    // Current L2Server 0x0072d30c dereferences m_data after
    // checking only the door pointer.
    L2_GUARD_FUNCTION();
    for (void* const rawDoor : current->openDoorList) {
        auto* const door = static_cast<CDoor*>(rawDoor);
        if (door != nullptr && !door->IsOpened() && door->GetDoorOpenMethod() == DOOROPEN_CYCLE) {
            door->Open(false, true);
        }
    }
}

std::pair<bool, int> IsFieldCycleInProgressOfQuest(CFieldCycleSystem* const system, const int questId) {
    // Current L2Server 0x0072d604.
    L2_GUARD_FUNCTION();
    const int fieldCycleId = FieldCycleMissionDB::inst().RetrieveFieldCycleID(questId);
    if (fieldCycleId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle of quest: %d",
        //     L"FieldCycle.cpp",
        //     0x997,
        //     questId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle of quest: %d",
        //     L"FieldCycle.cpp",
        //     0x997,
        //     questId)
        L2_LOG_ERROR(
            Log, "Cannot synchronize quest %d: no field-cycle mapping exists; the quest is treated as inactive.", questId
        );
        return std::make_pair(false, 0);
    }

    CFieldCycle* const fieldCycle = system->GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x99f,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x99f,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log, "Cannot synchronize quest %d: field cycle %d is missing; the quest is treated as inactive.", questId,
            fieldCycleId
        );
        return std::make_pair(false, fieldCycleId);
    }
    if (!fieldCycle->IsMissionInProgress()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Field cycle is not in progress: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x9a5,
        //     fieldCycleId,
        //     questId)
        // Noise: An inactive mission is an expected synchronization outcome. The caller records any cancellation through the
        // mission accounting route. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Field cycle is not in progress: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x9a5,
        //     fieldCycleId,
        //     questId)
        return std::make_pair(false, fieldCycleId);
    }
    return std::make_pair(true, fieldCycleId);
}

void SyncMissionQuestToFieldCycleUser(CFieldCycleSystem* const system, User* const user) {
    // Current L2Server 0x0072e538.
    L2_GUARD_FUNCTION();
    if (system == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9b3,
        //     L"(pSystem != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9b3,
        //     L"(pSystem != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot synchronize the user's field-cycle quest: the cycle system is null; synchronization is skipped."
        );
        return;
    }
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9b4,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9b4,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot synchronize the field-cycle quest: the user is null; synchronization is skipped.");
        return;
    }

    std::vector<int> questIds;
    user->RetrieveAcceptedQuestIDs(questIds);
    int questId = FieldCycleMissionUtil::RetrieveMissionQuestID(&FieldCycleMissionDB::inst(), questIds);
    if (questId == 0) {
        return;
    }

    auto state = IsFieldCycleInProgressOfQuest(system, questId);
    int fieldCycleId = state.second;
    if (!state.first) {
        if (!user->OnDestroyQuest(questId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0x9c9)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0x9c9)
            L2_LOG_ERROR(
                Log,
                "Cannot cancel inactive quest %d for character DBID %u during synchronization with field cycle %d; the "
                "field-cycle quest association is still cleared.",
                questId, static_cast<unsigned int>(user->GetDBID()), fieldCycleId
            );
        }
        FieldCycleStuff::Write_LOG_FIELD_MISSION_CANCEL(
            user, fieldCycleId, system->GetFieldCycleStep(fieldCycleId), questId, 0,
            FieldCycleStuff::MissionCancelReason::SyncMissionQuest
        );
        fieldCycleId = 0;
        questId = 0;
    }
    user->SetFieldCycleQuest(questId, fieldCycleId);
}

void OnUserUpdateOnExistFieldCycle(User* const user) {
    // Current L2Server 0x0073056c.
    L2_GUARD_FUNCTION();
    const int fieldCycleId = user->GetFieldCycleID();
    CFieldCycle* const fieldCycle = g_FieldCycleSystem.GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x969,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x969,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log,
            "Cannot update character DBID %u in field cycle %d: the cycle is missing; the character's current cycle ID is "
            "reset to 0.",
            static_cast<unsigned int>(user->GetDBID()), fieldCycleId
        );
        user->SetFieldCycleID(0);
        return;
    }
    if (!fieldCycle->ContainPosition(user->GetPos())) {
        fieldCycle->OnUserLeave(user);
    }
}

void OnUserFindNewFieldCycle(User* const user) {
    // Current L2Server 0x00731964.
    L2_GUARD_FUNCTION();
    CFieldCycle* const fieldCycle = g_FieldCycleSystem.FindFieldCycleByArea(user->GetPos());
    if (fieldCycle != nullptr) {
        fieldCycle->OnUserEnter(user);
    }
}

class TimerRescheduleGuard final {
public:
    TimerRescheduleGuard(CIOObject* owner, std::int64_t delay, int id) noexcept : m_owner(owner), m_delay(delay), m_id(id) {}

    ~TimerRescheduleGuard() { m_owner->AddTimer(m_delay, m_id); }

private:
    CIOObject* m_owner;
    std::int64_t m_delay;
    std::int32_t m_id;
    [[maybe_unused]] std::int32_t m_targetPadding = 0;
};


} // namespace

namespace FieldCycleConst {

std::int32_t _CYCLE_HEARTBEAT = 30000;

} // namespace FieldCycleConst

CFieldCycleSystem g_FieldCycleSystem;

StepInfo::StepInfo() = default;
StepInfo::~StepInfo() = default;

StepInfoGroup::StepInfoGroup() = default;

StepInfoGroup::~StepInfoGroup() { Reset(); }

void StepInfoGroup::Reset() {
    for (StepInfo* const info : m_stepList) {
        delete info;
    }
    m_stepList.clear();
    m_pointList.clear();
    m_maxStep = -1;
}

StepInfo* StepInfoGroup::GetStepInfo(int step) {
    static StepInfo nullStepInfo;
    if (step < 0 || step > m_maxStep) {
        return &nullStepInfo;
    }
    return runtime::CheckedVectorAt(m_stepList, static_cast<std::size_t>(step));
}

int StepInfoGroup::GetPoint(int step) const {
    if (step < 0 || step > m_maxStep || static_cast<std::size_t>(step) >= m_pointList.size()) {
        return 0;
    }
    return runtime::CheckedVectorAt(m_pointList, static_cast<std::size_t>(step));
}

int StepInfoGroup::GetGoalPoint(int step) const { return std::max(0, GetPoint(step + 1) - GetPoint(step)); }

bool StepInfoGroup::AddStep(int step, int point, StepInfo* info) {
    if (static_cast<int>(m_pointList.size()) != step) {
        return false;
    }
    m_pointList.push_back(point);
    m_stepList.push_back(info);
    ++m_maxStep;
    return true;
}

FieldCycleUser::FieldCycleUser() {}

FieldCycleUser::~FieldCycleUser() = default;

L2_DEFINE_OBJECT_RELEASE(FieldCycleUser)

void FieldCycleMissionUsers::Reset() { m_mapUsers.DeleteAllObjects(const_cast<char*>("FieldCycle.cpp"), 0x162); }

void FieldCycleMissionUsers::Register(std::uint32_t userId, const CSmartID& idUser) {
    L2_GUARD_FUNCTION();
    CSPointer<FieldCycleUser> user(new FieldCycleUser(), ObjectRefType::FunctionChain);
    L2_RELEASE(user, ObjectRefType::CreateDelete);
    m_mapUsers.Insert(static_cast<int>(userId), user.get(), const_cast<char*>("FieldCycle.cpp"), 0x16c);
    user->m_idUser = idUser;
}

bool FieldCycleMissionUsers::Unregister(std::uint32_t userId) { return m_mapUsers.DeleteObject(static_cast<int>(userId)); }

bool FieldCycleMissionUsers::IsRegistered(std::uint32_t userId) { return m_mapUsers.ExistKey(static_cast<int>(userId)); }

void FieldCycleMissionUsers::BroadcastToAll(DefaultIOBuffer* buffer) {
    // Current L2Server.exe 0x0072d788 is the physical expansion of
    // FOR_ALL_OBJECT_IN_OBJECT_MAP, including both nested guard frames.
    L2_GUARD_FUNCTION();
    FOR_ALL_OBJECT_IN_OBJECT_MAP(m_mapUsers, missionUser, {
        User* const user = missionUser->m_idUser.GetUser();
        if (user != nullptr) {
            buffer->AddRef();
            user->GetSocket()->AsyncSendConstBuffer(buffer);
        }
    });
}

void FieldCycleMissionUsers::NotifyFieldEventStepToAll(int step, int goalPoint, int remainSecond) {
    L2_GUARD_FUNCTION();
    DefaultIOBuffer* const buffer = MakeConstBufferFrom_S_EX_FIELD_EVENT_STEP(step, goalPoint, remainSecond);
    if (buffer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x199)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x199)
        L2_LOG_ERROR(
            Log,
            "Cannot prepare the field-cycle step notification (step %d, goal %d, remaining %d seconds); this broadcast is "
            "skipped.",
            step, goalPoint, remainSecond
        );
    } else {
        BroadcastToAll(buffer);
    }
}

void FieldCycleMissionUsers::NotifyFieldEventPointToAll(int point) {
    L2_GUARD_FUNCTION();
    DefaultIOBuffer* const buffer = MakeConstBufferFrom_S_EX_FIELD_EVENT_POINT(point);
    if (buffer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x1aa)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x1aa)
        L2_LOG_ERROR(
            Log, "Cannot prepare the field-cycle point notification for %d points; this broadcast is skipped.", point
        );
    } else {
        BroadcastToAll(buffer);
    }
}

CFieldCycle::CFieldCycle(int fieldCycleId) :
    m_fieldCycleID(fieldCycleId),
    m_pMissionMode(std::make_unique<MissionMode>()),
    m_pStepInfos(std::make_unique<StepInfoGroup>()),
    m_pMissionUsers(std::make_unique<FieldCycleMissionUsers>()) {
    SharedFieldCycleInfo& info = GetMyInfo();
    info.cycleID = fieldCycleId;
    info.nowPoint = 0;
    info.nowStep = 0;
    info.stepChangedTime = 0;
    info.tMissionStarted = 0;
    info.eMissionStatus = 0;
}

CFieldCycle::~CFieldCycle() {
    if (m_pMissionUsers) {
        m_pMissionUsers->Reset();
    }
    if (m_pStepInfos) {
        m_pStepInfos->Reset();
    }
}

L2_DEFINE_OBJECT_RELEASE(CFieldCycle)

SharedFieldCycleInfo& CFieldCycle::GetMyInfo() { return *SharedFieldCycleInfo::GetInfo(m_fieldCycleID); }

void CFieldCycle::BroadcastToAll(DefaultIOBuffer* const buffer) {
    // Current L2Server.exe 0x0072dbc4.
    L2_GUARD_FUNCTION();
    m_pMissionUsers->BroadcastToAll(buffer);
}

void CFieldCycle::AssembleMapInfoPacket(IPacketHelper& packet, int& count) {
    // Current L2Server 0x0072e9ac.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);

    const int step = GetMyInfo().nowStep;
    if (step < 0 || m_pStepInfos->m_maxStep < step) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]IsValidStep operation failed: %d",
        //     L"FieldCycle.cpp",
        //     0x295,
        //     step)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]IsValidStep operation failed: %d",
        //     L"FieldCycle.cpp",
        //     0x295,
        //     step)
        L2_LOG_ERROR(
            Log, "Field cycle %d: map-information step %d is outside 0..%d; the default step information is used.",
            m_fieldCycleID, step, m_pStepInfos->m_maxStep
        );
    }

    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(step);
    if (stepInfo->mapHuntingZoneID != 0) {
        packet.Assemble("dd", stepInfo->mapHuntingZoneID, stepInfo->mapStringNo);
        ++count;
    }
}

void CFieldCycle::SendDebugInfoTo(CSocket* const socket) {
    // Current L2Server 0x0072eb08.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);

    const L2DateTime stepChangedTime = L2DateTime::FromTime_t(GetMyInfo().stepChangedTime);
    const L2DateTime pointChangedTime = L2DateTime::FromTime_t(m_pointChangedTime);
    L2DateTime changeStepTime;
    const std::int32_t now = runtime::crt::Time32Now();
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);

    std::int32_t changeStepTimestamp = m_tStepChangeTime;
    if (changeStepTimestamp < 1) {
        if (stepInfo->expirationTime > 0) {
            changeStepTimestamp = runtime::AddInt32Wrapping(GetMyInfo().stepChangedTime, stepInfo->expirationTime);
            changeStepTime = L2DateTime::FromTime_t(changeStepTimestamp);
        }
    } else {
        changeStepTime = L2DateTime::FromTime_t(changeStepTimestamp);
    }

    std::array<char16_t, 1024> message{};
    if (!GetMyInfo().bIsMissionMode) {
        const int lockTimeLeft = L2DateTimeUtil::CalculateRemainSecond(now, GetMyInfo().stepChangedTime, stepInfo->lockTime);
        const int dropTimeLeft = L2DateTimeUtil::CalculateRemainSecond(now, m_pointChangedTime, stepInfo->dropTime);
        const std::wstring changeStepText = Utf16ToWide(L2DateTimeUtil::ToString(changeStepTime));
        const std::wstring pointChangedText = Utf16ToWide(L2DateTimeUtil::ToString(pointChangedTime));
        const std::wstring stepChangedText = Utf16ToWide(L2DateTimeUtil::ToString(stepChangedTime));
        const std::wstring format = WindowsWideFormatToHost(
            L"ID[%d] maxStep[%d] lockTime[%d] dropTime[%d] "
            L"nowStep[%d] nowPoint[%d] lockTimeLeft[%d] "
            L"dropTimeLeft[%d] dummyPoint[%d] stepChanged[%s] "
            L"pointChanged[%s] changeStepTime[%s] intervalTime[%d] "
            L"intervalPoint[%d] mapHuntingZone[%d] mapString[%d]"
        );
        FormatUtf16To(
            message.data(), message.size(),
            u"ID[%d] maxStep[%d] lockTime[%d] dropTime[%d] "
            u"nowStep[%d] nowPoint[%d] lockTimeLeft[%d] "
            u"dropTimeLeft[%d] dummyPoint[%d] stepChanged[%s] "
            u"pointChanged[%s] changeStepTime[%s] intervalTime[%d] "
            u"intervalPoint[%d] mapHuntingZone[%d] mapString[%d]",
            m_fieldCycleID, m_pStepInfos->m_maxStep, stepInfo->lockTime, stepInfo->dropTime, GetMyInfo().nowStep,
            GetMyInfo().nowPoint, lockTimeLeft, dropTimeLeft, m_accumulatedPoint, stepChangedText.c_str(),
            pointChangedText.c_str(), changeStepText.c_str(), stepInfo->intervalTime, stepInfo->intervalPoint,
            stepInfo->mapHuntingZoneID, stepInfo->mapStringNo
        );
    } else {
        const int missionLimitLeft =
            L2DateTimeUtil::CalculateRemainSecond(now, GetMyInfo().tMissionStarted, m_pMissionMode->limitTime);
        const std::wstring changeStepText = Utf16ToWide(L2DateTimeUtil::ToString(changeStepTime));
        const std::wstring pointChangedText = Utf16ToWide(L2DateTimeUtil::ToString(pointChangedTime));
        const std::wstring stepChangedText = Utf16ToWide(L2DateTimeUtil::ToString(stepChangedTime));
        FormatUtf16To(
            message.data(), message.size(),
            u"ID[%d] maxStep[%d] nowStep[%d] nowPoint[%d] "
            u"stepChanged[%s] pointChanged[%s] changeStepTime[%s] "
            u"MissionLimit[%d] mapHuntingZone[%d] mapString[%d]",
            m_fieldCycleID, m_pStepInfos->m_maxStep, GetMyInfo().nowStep, GetMyInfo().nowPoint, stepChangedText.c_str(),
            pointChangedText.c_str(), changeStepText.c_str(), missionLimitLeft, stepInfo->mapHuntingZoneID,
            stepInfo->mapStringNo
        );
    }

    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", message.data());
}

bool CFieldCycle::ContainPosition(const FVector& position) const {
    // Current L2Server.exe 0x0072d3e4.
    L2_GUARD_FUNCTION();
    return std::any_of(m_Areas.begin(), m_Areas.end(), [&position](CArea* area) {
        return area != nullptr && area->m_FieldTerritory.IsInside(position, false);
    });
}

bool CFieldCycle::IsMissionInProgress() const {
    // Current L2Server 0x0072c758.
    L2_GUARD_FUNCTION();
    L2_GUARD(*const_cast<CYieldLock*>(&m_cycleLock));
    return _IsMissionInProgress();
}

void CFieldCycle::_GiveMissionQuestTo(User* const user, const int currentQuestId) const {
    // Current L2Server 0x0072c5a4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x72e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x72e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Field cycle %d: cannot select a quest after quest %d because the user is null; assignment is skipped.",
            m_fieldCycleID, currentQuestId
        );
        return;
    }

    const SharedFieldCycleInfo& info = const_cast<CFieldCycle*>(this)->GetMyInfo();
    const FieldCycleMissionQuest* const quest = FieldCycleStuff::FindNextQuestWithDailyFlag(
        &FieldCycleMissionDB::inst(), info.cycleID, info.nowStep, user, currentQuestId
    );
    if (quest == nullptr || !GiveQuestTo(user, quest->m_nQuestID)) {
        return;
    }

    user->SetFieldCycleQuest(quest->m_nQuestID, info.cycleID);
    WriteLOG_FIELD_MISSION_BEGIN(
        user, info.cycleID, info.nowStep, user->GetFieldCycleQuestID(), user->GetClass(), user->Level(), currentQuestId
    );
}

void CFieldCycle::OnUserEnter(User* user) {
    // Current L2Server.exe 0x00731334. The field membership transaction is
    // completed before any client notification, matching the
    // target's ordering seen by concurrent point broadcasts.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x6af,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x6af,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Field cycle %d: cannot process entry because the user is null.", m_fieldCycleID);
        return;
    }
    if (m_pMissionUsers->IsRegistered(user->GetDBID())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x6b3)
        // Noise: The user is already registered in this cycle. The duplicate entry is ignored.
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x6b3)
        return;
    }

    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);
    if (stepInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x6bb)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x6bb)
        L2_LOG_ERROR(
            Log,
            "Field cycle %d: cannot register character DBID %u because the current step information is null; entry is "
            "skipped.",
            m_fieldCycleID, static_cast<unsigned int>(user->GetDBID())
        );
        return;
    }

    m_pMissionUsers->Register(user->GetDBID(), user->GetSmartID());
    user->SetFieldCycleID(GetMyInfo().cycleID);

    {
        L2_GUARD(m_cycleLock);
        const SharedFieldCycleInfo& info = GetMyInfo();
        const int remainSecond = L2DateTimeUtil::CalculateRemainSecond(
            runtime::crt::Time32Now(), info.tMissionStarted, m_pMissionMode->limitTime
        );
        int missionStep = 0;
        if (m_pMissionMode->firstStep != 0 && m_pMissionMode->firstStep <= info.nowStep && info.nowStep != 0 &&
            info.nowStep != m_pMissionMode->successStep && info.nowStep != m_pMissionMode->failStep) {
            missionStep = info.nowStep - m_pMissionMode->firstStep + 1;
        }

        NotifyFieldEventStepTo(user, missionStep, m_pStepInfos->GetGoalPoint(info.nowStep), remainSecond);
        NotifyFieldEventPointTo(user, info.nowPoint - m_pStepInfos->GetPoint(info.nowStep));
    }

    if (user->GetFieldCycleQuestID() != 0 && GetMyInfo().cycleID != user->GetFieldCycleIDOfQuest()) {
        const int questId = user->GetFieldCycleQuestID();
        if (!user->OnDestroyQuest(questId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]Failed to cancel quest: %d, %d",
            //     L"FieldCycle.cpp",
            //     0x6da,
            //     user->GetDBID(),
            //     questId)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]Failed to cancel quest: %d, %d",
            //     L"FieldCycle.cpp",
            //     0x6da,
            //     user->GetDBID(),
            //     questId)
            L2_LOG_ERROR(
                Log,
                "Field cycle %d: cannot cancel quest %d from another cycle for character DBID %u; the quest association is "
                "still cleared and entry processing continues.",
                m_fieldCycleID, questId, static_cast<unsigned int>(user->GetDBID())
            );
        }
        FieldCycleStuff::Write_LOG_FIELD_MISSION_CANCEL(
            user, user->GetFieldCycleIDOfQuest(), GetMyInfo().nowStep, questId, m_fieldCycleID,
            FieldCycleStuff::MissionCancelReason::WrongFieldCycleOfQuest
        );
        user->SetFieldCycleQuest(0, 0);
    }

    if (IsMissionInProgress() && user->GetFieldCycleQuestID() == 0) {
        L2_GUARD(m_cycleLock);
        _GiveMissionQuestTo(user, 0);
    }

    WriteLOG_FIELD_CYCLE_USER_ENTER(
        user, m_fieldCycleID, GetMyInfo().nowStep, user->GetFieldCycleQuestID(), user->GetClass(), user->Level()
    );
}

void CFieldCycle::OnUserLeave(User* user) {
    // Current L2Server.exe 0x0072feb0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x700,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x700,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Field cycle %d: cannot process departure because the user is null.", m_fieldCycleID);
        return;
    }

    user->SetFieldCycleID(0);
    NotifyFieldEventResetTo(user);
    m_pMissionUsers->Unregister(user->GetDBID());
    WriteLOG_FIELD_CYCLE_USER_LEAVE(user, m_fieldCycleID, GetMyInfo().nowStep, user->GetFieldCycleQuestID());
}

void CFieldCycle::OnUserTimerExpired(User* const user) {
    // Current L2Server 0x0072cd48.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x716,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x716,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Field cycle %d: cannot process the user timer because the user is null.", m_fieldCycleID);
        return;
    }
    if (!IsMissionInProgress() || user->m_nFieldCycleQuestID != 0) {
        return;
    }

    L2_GUARD(m_cycleLock);
    _GiveMissionQuestTo(user, 0);
}

FVector CFieldCycle::GetRestartPoint(FVector& position, bool chao) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);

    SharedFieldCycleInfo& info = GetMyInfo();
    if (info.nowStep == 0 || !m_pStepInfos) {
        return FVector{};
    }

    StepInfo* const step = m_pStepInfos->GetStepInfo(info.nowStep);
    RestartInfo* const restart = step != nullptr ? step->restartInfo : nullptr;
    if (restart == nullptr || restart->pTerritory == nullptr || !restart->pTerritory->IsInside(position, true)) {
        return FVector{};
    }
    return chao ? restart->chaoPoint : restart->normalPoint;
}

void CFieldCycle::PostLoad() {
    L2_GUARD_FUNCTION();
    m_pointChangedTime = runtime::crt::Time32Now();
    GetMyInfo().stepChangedTime = m_pointChangedTime;
    runtime::raw_atomic::ExchangeI32(&m_somethingChanged, 0);
    AddTimer(FieldCycleConst::_CYCLE_HEARTBEAT, kTimerCycleHeartbeat);
    if (GetMyInfo().bIsMissionMode) {
        AddTimer(kCycleNotifyPointMilliseconds, kTimerNotifyPoint);
    }
}

void CFieldCycle::LoadFromDBContinue(
    const int newStep,
    const int newPoint,
    const std::int32_t stepChangedTime,
    const std::int32_t pointChangedTime,
    const int accumulatedPoint,
    const std::int32_t missionStartedTime
) {
    // Current L2Server.exe 0x0073211c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);

    if (newStep < 0 || m_pStepInfos->m_maxStep < newStep) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]FieldCycle Initialization Failed... newStep[%d] > maxStep[%d]",
        //     L"FieldCycle.cpp",
        //     0x326,
        //     newStep,
        //     m_pStepInfos->m_maxStep)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]FieldCycle Initialization Failed... newStep[%d] > maxStep[%d]",
        //     L"FieldCycle.cpp",
        //     0x326,
        //     newStep,
        //     m_pStepInfos->m_maxStep)
        L2_LOG_ERROR(
            Log, "Field cycle %d: cannot restore DB state with step %d outside 0..%d; the current state is retained.",
            m_fieldCycleID, newStep, m_pStepInfos->m_maxStep
        );
        return;
    }

    SharedFieldCycleInfo& info = GetMyInfo();
    info.nowStep = newStep;
    info.nowPoint = newPoint;
    info.stepChangedTime = stepChangedTime;
    m_pointChangedTime = pointChangedTime;
    m_accumulatedPoint = accumulatedPoint;
    m_lockTimeLeft = false;
    m_intervalTimeLeft = false;
    info.tMissionStarted = missionStartedTime;

    const std::int32_t now = runtime::crt::Time32Now();
    StepInfo* stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
    m_tStepChangeTime = stepInfo->stepChangeTimeInRange.CalculateTime(info.stepChangedTime, now);

    if (info.bIsMissionMode) {
        info.eMissionStatus = MissionStatus(*m_pMissionMode, info.nowStep);
    }

    if (info.nowStep > 0) {
        stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
        if (stepInfo->lockTime > 0) {
            const int remainingSeconds =
                L2DateTimeUtil::CalculateRemainSecond(runtime::crt::Time32Now(), stepChangedTime, stepInfo->lockTime);
            if (remainingSeconds < 1) {
                _OnLockTimeExpired();
            } else {
                m_lockTimeLeft = true;
                AddTimer(static_cast<std::int64_t>(remainingSeconds) * 1000, info.nowStep);
            }
        }

        auto activation = [this] { _ActivateDoorAndArea(0); };
        PostLambdaMessage(activation);

        if (!m_intervalTimeLeft) {
            stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
            if (stepInfo->intervalTime > 0) {
                m_intervalTimeLeft = true;
                stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
                AddTimer(stepInfo->intervalTime, kTimerAddPoint);
            }
        }
    }
}

bool CFieldCycle::AddStep(const int step, const int point, StepInfo* const info) {
    L2_GUARD_FUNCTION();
    return m_pStepInfos != nullptr && m_pStepInfos->AddStep(step, point, info);
}

void CFieldCycle::SetMissionStartStep(const int step) {
    m_pMissionMode->firstStep = step;
    GetMyInfo().bIsMissionMode = step != 0;
}

void CFieldCycle::SetMissionLimitTime(const int seconds) { m_pMissionMode->limitTime = std::max(0, seconds); }

void CFieldCycle::SetMissionSuccessStep(const int step) { m_pMissionMode->successStep = step; }

void CFieldCycle::SetMissionFailedStep(const int step) { m_pMissionMode->failStep = step; }

void CFieldCycle::SetMissionArea(std::vector<CArea*> areas) { m_Areas = std::move(areas); }

void CFieldCycle::SetExpirationTime(const std::int32_t seconds) {
    // Current L2Server.exe 0x0072f794.
    L2_GUARD(m_cycleLock);
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);
    if (stepInfo != nullptr) {
        stepInfo->expirationTime = seconds;
    }
}

void CFieldCycle::_SaveStatus() {
    L2_GUARD_FUNCTION();
    const SharedFieldCycleInfo& info = GetMyInfo();
    db.RequestSaveFieldCycleInfo(
        m_fieldCycleID, info.nowPoint, info.nowStep, info.stepChangedTime, m_pointChangedTime, m_accumulatedPoint,
        info.tMissionStarted
    );
}

bool CFieldCycle::_IsMissionInProgress() const {
    L2_GUARD_FUNCTION();
    return const_cast<CFieldCycle*>(this)->GetMyInfo().tMissionStarted != 0;
}

bool CFieldCycle::ChangeStep(int step, FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    return _ChangeStep(step, reason, true);
}

bool CFieldCycle::ChangeNextStep() {
    // Current L2Server.exe 0x0072fcb4.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    return _ChangeNextStep(FieldCycleReason::ByBuilder);
}

void CFieldCycle::TryStepExpire(const int step, const int day, const int hour, const int minute) {
    // Current L2Server.exe 0x0072f5dc.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(step);
    if (day == stepInfo->stepChangeTime.day && hour == stepInfo->stepChangeTime.hour &&
        minute == stepInfo->stepChangeTime.min) {
        g_npcserver.Send("cddd", 0x9a, 0, m_fieldCycleID, step);
    }
}

void CFieldCycle::SetMissionResult(const bool success, const FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    SharedFieldCycleInfo& info = GetMyInfo();
    if (!info.bIsMissionMode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Not mission mode: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x4e5,
        //     m_fieldCycleID,
        //     static_cast<int>(reason))
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Not mission mode: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x4e5,
        //     m_fieldCycleID,
        //     static_cast<int>(reason))
        L2_LOG_WARN(
            Log,
            "Field cycle %d: cannot apply mission result %s (reason %s [%d]) because mission mode is disabled; the result "
            "is ignored.",
            m_fieldCycleID, success ? "success" : "failure", FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }

    const int step = success ? m_pMissionMode->successStep : m_pMissionMode->failStep;
    static_cast<void>(ChangeStep(step, reason));
}

void CFieldCycle::SetPoint(const int point, const FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    if (!m_lockTimeLeft && point >= 0) {
        SharedFieldCycleInfo& info = GetMyInfo();
        info.nowPoint = point;
        if (info.nowPoint < 0) {
            info.nowPoint = 0;
        }
        _OnPointChanged(reason);
    }
}

void CFieldCycle::_AddPoint(const int point, const FieldCycleReason::Value reason, CCreature* const creature) {
    L2_GUARD_FUNCTION();
    SharedFieldCycleInfo& info = GetMyInfo();
    if (!m_lockTimeLeft) {
        info.nowPoint = runtime::AddInt32Wrapping(info.nowPoint, point);
        if (info.nowPoint < 0) {
            info.nowPoint = 0;
        }
    } else {
        m_accumulatedPoint = runtime::AddInt32Wrapping(m_accumulatedPoint, point);
    }
    _OnPointChanged(reason);
    WriteLOG_NPC_GET_STEP_POINT(
        point, static_cast<int>(reason), creature, m_fieldCycleID, info.nowPoint, m_accumulatedPoint
    );
}

void CFieldCycle::OnUserCompleteQuest(User* const user) {
    // Current L2Server.exe 0x00732640.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x753,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x753,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Field cycle %d: cannot process quest completion because the user is null.", m_fieldCycleID);
        return;
    }

    L2_GUARD(m_cycleLock);
    const int currentQuestId = user->GetFieldCycleQuestID();
    const FieldCycleMissionQuest* const quest = FieldCycleMissionDB::inst().FindQuest(currentQuestId);

    int completePoint = 0;
    if (quest == nullptr) {
        const SharedFieldCycleInfo& info = GetMyInfo();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No quest for complete: %d, %d, %d",
        //     L"FieldCycle.cpp",
        //     0x75f,
        //     info.cycleID,
        //     info.nowStep,
        //     currentQuestId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No quest for complete: %d, %d, %d",
        //     L"FieldCycle.cpp",
        //     0x75f,
        //     info.cycleID,
        //     info.nowStep,
        //     currentQuestId)
        L2_LOG_ERROR(
            Log,
            "Field cycle %d, step %d: completed quest %d has no mission definition; no completion points are added, the "
            "quest association is cleared and next-quest selection may continue.",
            info.cycleID, info.nowStep, currentQuestId
        );
    } else {
        _AddPoint(quest->m_nCompletePoint, FieldCycleReason::ByMissionComplete, user);
        completePoint = quest->m_nCompletePoint;
    }

    const SharedFieldCycleInfo& info = GetMyInfo();
    WriteLOG_FIELD_MISSION_COMPLETE(user, m_fieldCycleID, info.nowStep, currentQuestId, completePoint);
    user->SetFieldCycleQuest(0, 0);
    if (_IsMissionInProgress()) {
        _GiveMissionQuestTo(user, currentQuestId);
    }
}

void CFieldCycle::AddPoint(const int point, const FieldCycleReason::Value reason, CCreature* const creature) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    _AddPoint(point, reason, creature);
}

bool CFieldCycle::_ChangeStep(int step, FieldCycleReason::Value reason, bool resetPoint) {
    L2_GUARD_FUNCTION();
    if (!m_pStepInfos || step < 0 || step > m_pStepInfos->m_maxStep) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x5fa)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x5fa)
        L2_LOG_ERROR(
            Log,
            "Field cycle %d: cannot apply step %d (reason %s [%d]); step definitions present: %s, maximum step: %d. The "
            "current step is retained.",
            m_fieldCycleID, step, FieldCycleReasonName(reason), static_cast<int>(reason), m_pStepInfos ? "yes" : "no",
            m_pStepInfos ? m_pStepInfos->m_maxStep : -1
        );
        return false;
    }

    SharedFieldCycleInfo& info = GetMyInfo();
    const int previousStep = info.nowStep;
    info.nowStep = step;
    _OnStepChanged(previousStep, runtime::crt::Time32Now(), resetPoint, reason);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]FieldCycle Step changed : %d, %d, %d, %d",
    //     L"FieldCycle.cpp",
    //     0x607,
    //     m_fieldCycleID,
    //     previousStep,
    //     step,
    //     static_cast<int>(reason))
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]FieldCycle Step changed : %d, %d, %d, %d",
    //     L"FieldCycle.cpp",
    //     0x607,
    //     m_fieldCycleID,
    //     previousStep,
    //     step,
    //     static_cast<int>(reason))
    L2_LOG_INFO(
        Log, "Field cycle %d: applied step %d (previous %d, reason %s [%d]).", m_fieldCycleID, step, previousStep,
        FieldCycleReasonName(reason), static_cast<int>(reason)
    );
    return true;
}

bool CFieldCycle::_ChangeNextStep(FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    if (!m_pStepInfos || !m_pMissionMode) {
        return false;
    }

    int nextStep = GetMyInfo().nowStep;
    if (nextStep == 0 || (nextStep != m_pMissionMode->successStep && nextStep != m_pMissionMode->failStep)) {
        if (nextStep >= m_pStepInfos->m_maxStep) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0x61e)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0x61e)
            L2_LOG_WARN(
                Log,
                "Field cycle %d: cannot advance from step %d at or above maximum step %d (reason %s [%d]); the current step "
                "is retained.",
                m_fieldCycleID, nextStep, m_pStepInfos->m_maxStep, FieldCycleReasonName(reason), static_cast<int>(reason)
            );
            return false;
        }
        ++nextStep;
    } else {
        nextStep = 1;
    }
    return _ChangeStep(nextStep, reason, true);
}

void CFieldCycle::_ChangePrevStep(FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    const int currentStep = GetMyInfo().nowStep;
    if (currentStep < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x630)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x630)
        L2_LOG_ERROR(
            Log, "Field cycle %d: cannot move back from step %d below 1 (reason %s [%d]); the current step is retained.",
            m_fieldCycleID, currentStep, FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }
    static_cast<void>(_ChangeStep(currentStep - 1, reason, false));
}

void CFieldCycle::_OnPointChanged(FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    m_pointChangedTime = runtime::crt::Time32Now();
    if (!m_pStepInfos) {
        return;
    }

    SharedFieldCycleInfo& info = GetMyInfo();
    if (info.nowStep < m_pStepInfos->m_maxStep && m_pStepInfos->GetPoint(info.nowStep + 1) <= info.nowPoint) {
        static_cast<void>(_ChangeNextStep(reason));
        return;
    }
    if (info.nowStep > 0 && info.nowPoint < m_pStepInfos->GetPoint(info.nowStep)) {
        _ChangePrevStep(reason);
        return;
    }
    runtime::raw_atomic::ExchangeI32(&m_somethingChanged, 1);
}

void CFieldCycle::_OnLockTimeExpired() {
    L2_GUARD_FUNCTION();
    SharedFieldCycleInfo& info = GetMyInfo();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Lock time expired. FieldCycleID [%d] Step [%d] Current Point [%d]
    //     AccumulatedPoint [%d]", L"FieldCycle.cpp", 0x496, info.cycleID, info.nowStep, info.nowPoint, m_accumulatedPoint)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Lock time expired. FieldCycleID [%d] Step [%d] Current Point [%d]
    //     AccumulatedPoint [%d]", L"FieldCycle.cpp", 0x496, info.cycleID, info.nowStep, info.nowPoint, m_accumulatedPoint)
    L2_LOG_INFO(
        Log, "Field cycle %d, step %d: point lock expired; applying %d accumulated points to %d current points.",
        info.cycleID, info.nowStep, m_accumulatedPoint, info.nowPoint
    );
    m_lockTimeLeft = false;
    info.nowPoint = std::max(0, runtime::AddInt32Wrapping(info.nowPoint, m_accumulatedPoint));
    m_accumulatedPoint = 0;
    _OnPointChanged(FieldCycleReason::ByLockExpired);
}

void CFieldCycle::_OnStepChanged(
    int previousStep, std::int32_t changedTime, bool resetPoint, FieldCycleReason::Value reason
) {
    L2_GUARD_FUNCTION();
    if (!m_pStepInfos || !m_pMissionMode) {
        return;
    }

    SharedFieldCycleInfo& info = GetMyInfo();
    if (resetPoint) {
        info.nowPoint = m_pStepInfos->GetPoint(info.nowStep);
    }
    m_accumulatedPoint = 0;
    info.stepChangedTime = changedTime;
    g_npcserver.Send("cdd", 0x65, m_fieldCycleID, info.nowStep);
    m_pointChangedTime = runtime::crt::Time32Now();

    if (info.bIsMissionMode) {
        const MissionMode& mode = *m_pMissionMode;
        if (mode.firstStep == info.nowStep) {
            info.tMissionStarted = changedTime;
        } else if (mode.firstStep == 0 || info.nowStep < mode.firstStep ||
            (info.nowStep != 0 && (info.nowStep == mode.successStep || info.nowStep == mode.failStep))) {
            info.tMissionStarted = 0;
        }
        info.eMissionStatus = MissionStatus(mode, info.nowStep);

        const int remainSecond =
            L2DateTimeUtil::CalculateRemainSecond(runtime::crt::Time32Now(), info.tMissionStarted, mode.limitTime);
        int missionStep = 0;
        if (mode.firstStep != 0 && mode.firstStep <= info.nowStep && info.nowStep != 0 && info.nowStep != mode.successStep &&
            info.nowStep != mode.failStep) {
            missionStep = info.nowStep - mode.firstStep + 1;
        }
        m_pMissionUsers->NotifyFieldEventStepToAll(missionStep, m_pStepInfos->GetGoalPoint(info.nowStep), remainSecond);
        m_pMissionUsers->NotifyFieldEventPointToAll(info.nowPoint - m_pStepInfos->GetPoint(info.nowStep));
    }

    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
    m_tStepChangeTime = stepInfo->stepChangeTimeInRange.CalculateTime(info.stepChangedTime, changedTime);
    runtime::raw_atomic::ExchangeI32(&m_somethingChanged, 0);
    _SaveStatus();

    m_lockTimeLeft = false;
    const int lockMilliseconds = stepInfo->lockTime * 1000;
    if (lockMilliseconds > 0) {
        m_lockTimeLeft = true;
        AddTimer(lockMilliseconds, info.nowStep);
    }
    if (stepInfo->intervalTime > 0 && !m_intervalTimeLeft) {
        m_intervalTimeLeft = true;
        AddTimer(stepInfo->intervalTime, kTimerAddPoint);
    }

    auto activation = [this, previousStep] { _ActivateDoorAndArea(previousStep); };
    PostLambdaMessage(activation);
    static_cast<void>(reason);
}

void CFieldCycle::_ActivateDoorAndArea(int previousStep) {
    L2_GUARD_FUNCTION();
    if (previousStep == GetMyInfo().nowStep) {
        return;
    }

    StepInfo* const previous = m_pStepInfos->GetStepInfo(previousStep);
    StepInfo* const current = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);

    DeactivateAreas(previous, current);
    CloseDoors(previous, current);
    ActivateAreas(current);
    OpenDoors(current);
}

void CFieldCycle::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_cycleLock);
    if (id < 1000) {
        if (id == GetMyInfo().nowStep) {
            _OnLockTimeExpired();
        }
        return;
    }
    if (id == kTimerCycleHeartbeat) {
        _OnTimer_CYCLE_TIMER();
    } else if (id == kTimerAddPoint) {
        _OnTimer_ADD_POINT();
    } else if (id == kTimerNotifyPoint) {
        _OnTimer_TIMER_NOTIFY_POINT();
    }
}

void CFieldCycle::_OnTimerForStepChangeTime(std::int32_t now) {
    L2_GUARD_FUNCTION();
    if (!m_pStepInfos) {
        return;
    }
    const L2DateTime localTime = L2DateTime::FromTime_t(now);
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);
    if (stepInfo->stepChangeTime.day >= 0) {
        if (!m_stepChangeEventFired) {
            if (localTime.m_eDayOfWeek != stepInfo->stepChangeTime.day ||
                localTime.m_nHour != stepInfo->stepChangeTime.hour || localTime.m_nMin != stepInfo->stepChangeTime.min) {
                return;
            }
            g_npcserver.Send("cddd", 0x9a, 0, m_fieldCycleID, GetMyInfo().nowStep);
            m_stepChangeEventFired = true;
            return;
        }
        if (std::abs(localTime.m_nMin - stepInfo->stepChangeTime.min) < 1) {
            return;
        }
    }
    m_stepChangeEventFired = false;
}

void CFieldCycle::_OnTimerForStepChangeTimeInRange(std::int32_t now) {
    L2_GUARD_FUNCTION();
    if (m_tStepChangeTime == 0) {
        return;
    }
    if (!GetMyInfo().bIsMissionMode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x5de)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x5de)
        L2_LOG_ERROR(
            Log, "Field cycle %d: scheduled step-change timestamp %d requires mission mode; this timer check is skipped.",
            m_fieldCycleID, m_tStepChangeTime
        );
        return;
    }
    if (m_tStepChangeTime <= now) {
        static_cast<void>(_ChangeNextStep(FieldCycleReason::ByTimeInRange));
    }
}

void CFieldCycle::_OnTimerForExpireTime(std::int32_t now) {
    if (!m_pStepInfos) {
        return;
    }
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);
    if (stepInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]no step info: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x57f,
        //     m_fieldCycleID,
        //     GetMyInfo().nowStep)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]no step info: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x57f,
        //     m_fieldCycleID,
        //     GetMyInfo().nowStep)
        L2_LOG_ERROR(
            Log, "Field cycle %d: step %d has null step information; the expiration check is skipped.", m_fieldCycleID,
            GetMyInfo().nowStep
        );
        return;
    }
    if (stepInfo->expirationTime <= 0) {
        return;
    }
    if (!GetMyInfo().bIsMissionMode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x585)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x585)
        L2_LOG_ERROR(
            Log, "Field cycle %d: expiration interval %d seconds requires mission mode; this expiration check is skipped.",
            m_fieldCycleID, stepInfo->expirationTime
        );
        return;
    }
    if (GetMyInfo().stepChangedTime + stepInfo->expirationTime <= now) {
        static_cast<void>(_ChangeNextStep(FieldCycleReason::ByExpirationTime));
    }
}

void CFieldCycle::_OnTimerForMissionLimitTime(std::int32_t now) {
    L2_GUARD_FUNCTION();
    if (!m_pMissionMode) {
        return;
    }
    const SharedFieldCycleInfo& info = GetMyInfo();
    const MissionMode& mode = *m_pMissionMode;
    if (!info.bIsMissionMode || mode.limitTime == 0 || info.tMissionStarted == 0 || mode.firstStep == 0 ||
        mode.firstStep > info.nowStep || info.nowStep == mode.successStep || info.nowStep == mode.failStep) {
        return;
    }
    if (L2DateTimeUtil::CalculateRemainSecond(now, info.tMissionStarted, mode.limitTime) < 1) {
        static_cast<void>(_ChangeStep(mode.failStep, FieldCycleReason::ByMissionLimitTime, true));
    }
}

void CFieldCycle::_OnTimer_ADD_POINT() {
    L2_GUARD_FUNCTION();
    if (!m_pStepInfos) {
        m_intervalTimeLeft = false;
        return;
    }
    StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(GetMyInfo().nowStep);
    if (stepInfo->intervalTime <= 0) {
        m_intervalTimeLeft = false;
        return;
    }

    if (!m_lockTimeLeft) {
        SharedFieldCycleInfo& info = GetMyInfo();
        info.nowPoint = std::max(0, info.nowPoint + stepInfo->intervalPoint);
    } else {
        m_accumulatedPoint += stepInfo->intervalPoint;
    }
    _OnPointChanged(FieldCycleReason::ByPointTimer);
    m_intervalTimeLeft = true;
    AddTimer(stepInfo->intervalTime, kTimerAddPoint);
}

void CFieldCycle::_OnTimer_CYCLE_TIMER() {
    L2_GUARD_FUNCTION();
    SharedFieldCycleInfo& info = GetMyInfo();
    if (m_pStepInfos && info.nowStep > 0) {
        const std::int32_t now = runtime::crt::Time32Now();
        const int elapsed = now - m_pointChangedTime;
        StepInfo* const stepInfo = m_pStepInfos->GetStepInfo(info.nowStep);
        const int previousStep = info.nowStep;
        if (stepInfo->dropTime > 0 && stepInfo->dropTime <= elapsed) {
            --info.nowStep;
            _OnStepChanged(previousStep, now, true, FieldCycleReason::ByDropTime);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]FieldCycleID [%d] Step changed (%d -> %d) by TimerExpired(). (Elapsed
            //     time : %d > Drop time : %d)", L"FieldCycle.cpp", 0x50a, m_fieldCycleID, previousStep, info.nowStep,
            //     elapsed,
            //     stepInfo->dropTime)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]FieldCycleID [%d] Step changed (%d -> %d) by TimerExpired(). (Elapsed
            //     time : %d > Drop time : %d)", L"FieldCycle.cpp", 0x50a, m_fieldCycleID, previousStep, info.nowStep,
            //     elapsed,
            //     stepInfo->dropTime)
            L2_LOG_INFO(
                Log,
                "Field cycle %d: step changed from %d to %d after %d elapsed seconds reached the drop interval of %d "
                "seconds.",
                m_fieldCycleID, previousStep, info.nowStep, elapsed, stepInfo->dropTime
            );
        }
    }

    const std::int32_t now = runtime::crt::Time32Now();
    _OnTimerForStepChangeTime(now);
    _OnTimerForStepChangeTimeInRange(now);
    _OnTimerForExpireTime(now);
    _OnTimerForMissionLimitTime(now);

    if (runtime::raw_atomic::CompareExchangeI32(&m_somethingChanged, 0, 1) == 1) {
        _SaveStatus();
    }
    AddTimer(FieldCycleConst::_CYCLE_HEARTBEAT, kTimerCycleHeartbeat);
}

void CFieldCycle::_OnTimer_TIMER_NOTIFY_POINT() {
    L2_GUARD_FUNCTION();
    TimerRescheduleGuard reschedule(this, kCycleNotifyPointMilliseconds, kTimerNotifyPoint);
    if (!_IsMissionInProgress() || !m_pStepInfos || !m_pMissionUsers) {
        return;
    }
    const SharedFieldCycleInfo& info = GetMyInfo();
    if (m_nPrevPointAtNotify == info.nowPoint) {
        return;
    }
    m_pMissionUsers->NotifyFieldEventPointToAll(info.nowPoint - m_pStepInfos->GetPoint(info.nowStep));
    m_nPrevPointAtNotify = info.nowPoint;
}

CFieldCycleSystem::CFieldCycleSystem() = default;
CFieldCycleSystem::~CFieldCycleSystem() = default;

void CFieldCycleSystem::Reset() {
    for (CFieldCycle* const fieldCycle : m_fieldCycleList) {
        if (fieldCycle != nullptr) {
            L2_RELEASE(fieldCycle, ObjectRefType::CreateDelete);
        }
    }
    m_fieldCycleList.clear();
    m_vMissionFields.clear();
    SharedFieldCycleInfo::GetInfo(0)->cycleID = 0;
}

void CFieldCycleSystem::PostLoad() {
    L2_GUARD_FUNCTION();
    for (CFieldCycle* const fieldCycle : m_fieldCycleList) {
        if (fieldCycle != nullptr) {
            fieldCycle->PostLoad();
        }
    }
}

void CFieldCycleSystem::RequestFieldCycleInfo() {
    // Current L2Server 0x0072c094.
    L2_GUARD_FUNCTION();
    db.RequestLoadFieldCycleInfo(-1);
}

void CFieldCycleSystem::LoadFromDBContinue(
    const int fieldCycleId,
    const int newStep,
    const int newPoint,
    const std::int32_t stepChangedTime,
    const std::int32_t pointChangedTime,
    const int accumulatedPoint,
    const std::int32_t missionStartedTime
) {
    // Current L2Server.exe 0x0073283c.
    L2_GUARD_FUNCTION();
    CFieldCycle* const fieldCycle = GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x830,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x830,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log, "Cannot restore DB state for field cycle %d: the cycle is missing; the returned state is ignored.",
            fieldCycleId
        );
        return;
    }
    fieldCycle->LoadFromDBContinue(
        newStep, newPoint, stepChangedTime, pointChangedTime, accumulatedPoint, missionStartedTime
    );
}

void CFieldCycleSystem::OnUserCompleteQuest(User* const user, const int questId) {
    // Current L2Server.exe 0x00732a78.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa70,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa70,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process completion of field-cycle quest %d: the user is null.", questId);
        return;
    }
    if (user->GetFieldCycleQuestID() != questId) {
        return;
    }

    const int fieldCycleId = user->GetFieldCycleID();
    if (fieldCycleId != 0 && fieldCycleId == user->GetFieldCycleIDOfQuest()) {
        if (CFieldCycle* const fieldCycle = GetFieldCycle(fieldCycleId); fieldCycle != nullptr) {
            fieldCycle->OnUserCompleteQuest(user);
            return;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d, %d",
        //     L"FieldCycle.cpp",
        //     0xa85,
        //     user->GetDBID(),
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d, %d",
        //     L"FieldCycle.cpp",
        //     0xa85,
        //     user->GetDBID(),
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log,
            "Cannot complete quest %d for character DBID %u: field cycle %d is missing; the field-cycle quest association "
            "is cleared without awarding completion points.",
            questId, static_cast<unsigned int>(user->GetDBID()), fieldCycleId
        );
    }
    user->SetFieldCycleQuest(0, 0);
}

void CFieldCycleSystem::Load() {
    L2_GUARD_FUNCTION();
    Reset();

#if defined(_WIN32)
    constexpr const wchar_t* kFieldCycleDataPath = L"..\\Script\\FieldCycle.txt";
#else
    constexpr const wchar_t* kFieldCycleDataPath = L"../script/FieldCycle.txt";
#endif

    CSecureStream input(kFieldCycleDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Read from %s file failed",
        //     L"FieldCycle.cpp",
        //     0x7a4,
        //     kFieldCycleDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Read from %s file failed",
        //     L"FieldCycle.cpp",
        //     0x7a4,
        //     kFieldCycleDataPath)
        L2_LOG_ERROR(
            Log, "Cannot read field-cycle definitions from '%s'; the cycle lists were reset and remain empty.",
            WideToUtf8(kFieldCycleDataPath).c_str()
        );
        return;
    }

    CParserForFieldCycle parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]CParserForFieldCycle::create failed",
        //     L"FieldCycle.cpp",
        //     0x7ad)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]CParserForFieldCycle::create failed",
        //     L"FieldCycle.cpp",
        //     0x7ad)
        L2_LOG_ERROR(
            Log,
            "Cannot create the parser for FieldCycle.txt; the cycle lists remain empty and post-load initialization is "
            "skipped."
        );
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Parsing %s file failed after line[%d]",
        //     L"FieldCycle.cpp",
        //     0x7bc,
        //     kFieldCycleDataPath,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Parsing %s file failed after line[%d]",
        //     L"FieldCycle.cpp",
        //     0x7bc,
        //     kFieldCycleDataPath,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse '%s' after lexer line %lld; %llu registered cycles are retained and post-load initialization is "
            "still performed.",
            WideToUtf8(kFieldCycleDataPath).c_str(), static_cast<long long>(parser.m_lexer.line),
            static_cast<unsigned long long>(m_fieldCycleList.size())
        );
    }
    PostLoad();
}

L2_DEFINE_OBJECT_RELEASE(CFieldCycleSystem)

CFieldCycle* CFieldCycleSystem::GetFieldCycle(int fieldCycleId) {
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || fieldCycleId > 0xff || static_cast<std::size_t>(fieldCycleId) > m_fieldCycleList.size()) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
}

void CFieldCycleSystem::SendDebugInfoTo(const int fieldCycleId, CSocket* const socket) {
    // Current L2Server.exe 0x00730254.
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        SendBuildCommandFailureTo(socket);
        return;
    }

    CFieldCycle* const fieldCycle = runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
    if (fieldCycle != nullptr) {
        fieldCycle->SendDebugInfoTo(socket);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
    //     L"FieldCycle.cpp",
    //     0x8e6,
    //     fieldCycleId)
    // L2_LOG_ERROR(Log,
    //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
    //     L"FieldCycle.cpp",
    //     0x8e6,
    //     fieldCycleId)
    L2_LOG_ERROR(
        Log, "Cannot show debug information for field cycle %d: its cycle entry is null; no cycle details are sent.",
        fieldCycleId
    );
}

void CFieldCycleSystem::SendDebugInfoTo(CSocket* const socket) {
    // Current L2Server.exe 0x00730360.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 1024> message{};
    FormatUtf16To(message.data(), message.size(), u"total fieldCycle : %d ", SharedFieldCycleInfo::GetInfo(0)->cycleID);
    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", message.data());

    for (CFieldCycle* const fieldCycle : m_fieldCycleList) {
        fieldCycle->SendDebugInfoTo(socket);
    }
}

void CFieldCycleSystem::ChangeStep(
    const int fieldCycleId, const int step, const FieldCycleReason::Value reason, CCreature*, CSocket* const socket
) {
    L2_GUARD_FUNCTION();
    if (fieldCycleId < 1 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        SendBuildCommandFailureTo(socket);
        return;
    }

    CFieldCycle* const fieldCycle = runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
    if (fieldCycle == nullptr || !fieldCycle->ChangeStep(step, reason)) {
        SendBuildCommandFailureTo(socket);
    }
}

void CFieldCycleSystem::TryStepExpire(
    const int fieldCycleId, const int step, const int day, const int hour, const int minute
) {
    // Current L2Server.exe 0x00730454.
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        return;
    }
    CFieldCycle* const fieldCycle = runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x90f,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x90f,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log,
            "Cannot check scheduled expiration for field cycle %d, step %d: the cycle entry is null; no expiration request "
            "is sent.",
            fieldCycleId, step
        );
        return;
    }
    fieldCycle->TryStepExpire(step, day, hour, minute);
}

void CFieldCycleSystem::SetMissionResult(const int fieldCycleId, const bool success, const FieldCycleReason::Value reason) {
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x88d)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0x88d)
        L2_LOG_WARN(
            Log, "Cannot set mission result %s for invalid field-cycle ID %d (reason %s [%d]); the request is ignored.",
            success ? "success" : "failure", fieldCycleId, FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }

    CFieldCycle* const fieldCycle = GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field: %d",
        //     L"FieldCycle.cpp",
        //     0x895,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field: %d",
        //     L"FieldCycle.cpp",
        //     0x895,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log,
            "Cannot set mission result %s for field cycle %d (reason %s [%d]): the cycle is missing; the request is "
            "ignored.",
            success ? "success" : "failure", fieldCycleId, FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }
    fieldCycle->SetMissionResult(success, reason);
}

void CFieldCycleSystem::SetPoint(
    const int fieldCycleId, const int point, const FieldCycleReason::Value reason, CSocket* const socket
) {
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        SendBuildCommandFailureTo(socket);
        return;
    }

    CFieldCycle* const fieldCycle = runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x847,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x847,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log,
            "Cannot set %d points for field cycle %d (reason %s [%d]): the cycle entry is null; the request is ignored.",
            point, fieldCycleId, FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }
    fieldCycle->SetPoint(point, reason);
}

void CFieldCycleSystem::AddPoint(
    const int fieldCycleId,
    const int point,
    const FieldCycleReason::Value reason,
    CCreature* const creature,
    CSocket* const socket
) {
    L2_GUARD_FUNCTION();
    if (fieldCycleId <= 0 || SharedFieldCycleInfo::GetInfo(0)->cycleID < fieldCycleId) {
        SendBuildCommandFailureTo(socket);
        return;
    }

    CFieldCycle* const fieldCycle = runtime::CheckedVectorAt(m_fieldCycleList, static_cast<std::size_t>(fieldCycleId - 1));
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x861,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid FieldCycle ID[%d]",
        //     L"FieldCycle.cpp",
        //     0x861,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log, "Cannot add %d points to field cycle %d (reason %s [%d]): the cycle entry is null; the request is ignored.",
            point, fieldCycleId, FieldCycleReasonName(reason), static_cast<int>(reason)
        );
        return;
    }
    fieldCycle->AddPoint(point, reason, creature);
}

void CFieldCycleSystem::BroadcastTo(const int fieldCycleId, DefaultIOBuffer* const buffer) {
    // Current L2Server.exe 0x0072dda8.
    L2_GUARD_FUNCTION();
    CFieldCycle* const fieldCycle = GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x8c9,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x8c9,
        //     fieldCycleId)
        L2_LOG_ERROR(
            Log, "Cannot broadcast to field cycle %d: the cycle is missing; this broadcast is skipped.", fieldCycleId
        );
        return;
    }
    if (!fieldCycle->GetMyInfo().bIsMissionMode) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Field cycle is not mission mode: %d",
        //     L"FieldCycle.cpp",
        //     0x8cf,
        //     fieldCycleId)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Field cycle is not mission mode: %d",
        //     L"FieldCycle.cpp",
        //     0x8cf,
        //     fieldCycleId)
        L2_LOG_WARN(
            Log, "Cannot broadcast to field cycle %d: mission mode is disabled; this broadcast is skipped.", fieldCycleId
        );
        return;
    }
    fieldCycle->BroadcastToAll(buffer);
}

bool CFieldCycleSystem::AddField(int fieldCycleId, CFieldCycle* fieldCycle) {
    L2_GUARD_FUNCTION();
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x7e2,
        //     L"(fieldCycle != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x7e2,
        //     L"(fieldCycle != NULL)")
        L2_LOG_ERROR(Log, "Cannot register field cycle %d: the cycle object is null.", fieldCycleId);
        return false;
    }
    if (fieldCycleId != fieldCycle->GetMyInfo().cycleID) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Field Cycle ID is invalid: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x7e6,
        //     fieldCycleId,
        //     fieldCycle->GetMyInfo().cycleID)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Field Cycle ID is invalid: %d, %d",
        //     L"FieldCycle.cpp",
        //     0x7e6,
        //     fieldCycleId,
        //     fieldCycle->GetMyInfo().cycleID)
        L2_LOG_ERROR(
            Log, "Cannot register field cycle %d: its shared-state cycle ID is %d; registration is rejected.", fieldCycleId,
            fieldCycle->GetMyInfo().cycleID
        );
        return false;
    }
    const int expectedId = static_cast<int>(m_fieldCycleList.size()) + 1;
    if (expectedId >= 0x100 || expectedId != fieldCycleId) {
        return false;
    }
    m_fieldCycleList.push_back(fieldCycle);
    if (fieldCycle->GetMyInfo().bIsMissionMode) {
        m_vMissionFields.push_back(fieldCycle);
    }
    SharedFieldCycleInfo::GetInfo(0)->cycleID = static_cast<int>(m_fieldCycleList.size());
    return true;
}

CFieldCycle* CFieldCycleSystem::FindFieldCycleByArea(const FVector& position) const {
    // Current L2Server.exe 0x0072d578.
    for (CFieldCycle* const fieldCycle : m_vMissionFields) {
        if (fieldCycle != nullptr && fieldCycle->ContainPosition(position)) {
            return fieldCycle;
        }
    }
    return nullptr;
}

FVector CFieldCycleSystem::GetRestartPoint(FVector position, bool chao) {
    L2_GUARD_FUNCTION();
    const FVector zero{};
    for (CFieldCycle* const fieldCycle : m_fieldCycleList) {
        if (fieldCycle == nullptr) {
            continue;
        }
        FVector restart = fieldCycle->GetRestartPoint(position, chao);
        if (!restart.IsEqual(zero)) {
            return restart;
        }
    }
    return zero;
}

int CFieldCycleSystem::GetFieldCount() const { return static_cast<int>(m_fieldCycleList.size()); }

void CFieldCycleSystem::SendMapInfoTo(User* user) {
    // Current L2Server 0x007300f0.
    L2_GUARD_FUNCTION();

    int count = 0;
    CPacketHelperEx<20480> packet;
    for (CFieldCycle* const fieldCycle : m_fieldCycleList) {
        fieldCycle->AssembleMapInfoPacket(packet, count);
    }

    if (count != 0 && user != nullptr) {
        CUserSocket* const socket = user->GetSocket();
        socket->Send("chdb", 0xfe, 0xa2, count, packet.GetLength(), packet.GetPacket());
    }
}

void CFieldCycleSystem::OnUserEnterWorld(User* user) {
    // Current L2Server.exe 0x00731a30.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9e4,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9e4,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process world entry for field cycles: the user is null.");
        return;
    }

    SyncMissionQuestToFieldCycleUser(this, user);
    OnUserFindNewFieldCycle(user);
}

void CFieldCycleSystem::OnUserLeaveWorld(User* user) {
    // Current L2Server.exe 0x00730684.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9f2,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0x9f2,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process world departure for field cycles: the user is null.");
        return;
    }
    if (user->GetFieldCycleID() == 0) {
        return;
    }

    CFieldCycle* const field = GetFieldCycle(user->GetFieldCycleID());
    if (field == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x9fd,
        //     user->GetFieldCycleID())
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No field cycle: %d",
        //     L"FieldCycle.cpp",
        //     0x9fd,
        //     user->GetFieldCycleID())
        L2_LOG_ERROR(
            Log,
            "Cannot process departure from field cycle %d: the cycle is missing; the character's current cycle ID is reset "
            "to 0.",
            user->GetFieldCycleID()
        );
        user->SetFieldCycleID(0);
        return;
    }
    field->OnUserLeave(user);
}

void CFieldCycleSystem::OnUserTeleport(User* user) {
    // Current L2Server.exe 0x00731b18.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa0c,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa0c,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process teleportation for field cycles: the user is null.");
        return;
    }

    if (user->GetFieldCycleID() == 0) {
        OnUserFindNewFieldCycle(user);
    } else {
        OnUserUpdateOnExistFieldCycle(user);
    }
}

void CFieldCycleSystem::OnUserTimerExpired(User* const user) {
    // Current L2Server 0x0072ce78.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa21,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycle.cpp",
        //     0xa21,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot process the user timer for field cycles: the user is null.");
        return;
    }

    if (user->m_nFieldCycleIDOfQuest != 0 && !user->IsQuestProgressCompleted(user->m_nFieldCycleQuestID) &&
        !IsFieldCycleInProgress(this, user->m_nFieldCycleIDOfQuest)) {
        if (!user->OnDestroyQuest(user->m_nFieldCycleQuestID)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0xa2d)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"FieldCycle.cpp",
            //     0xa2d)
            L2_LOG_ERROR(
                Log,
                "Cannot cancel quest %d for character DBID %u when field-cycle mission %d is not in progress; the quest "
                "association is still cleared and timer processing continues.",
                user->m_nFieldCycleQuestID, static_cast<unsigned int>(user->GetDBID()), user->m_nFieldCycleIDOfQuest
            );
        }
        FieldCycleStuff::Write_LOG_FIELD_MISSION_CANCEL(
            user, user->m_nFieldCycleIDOfQuest, GetFieldCycleStep(user->m_nFieldCycleIDOfQuest), user->m_nFieldCycleQuestID,
            user->m_nFieldCycleID, FieldCycleStuff::MissionCancelReason::MissionOver
        );
        user->SetFieldCycleQuest(0, 0);
    }

    if (user->m_nFieldCycleID == 0) {
        return;
    }
    CFieldCycle* const fieldCycle = GetFieldCycle(user->m_nFieldCycleID);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0xa42)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0xa42)
        L2_LOG_ERROR(
            Log,
            "Cannot process the timer for character DBID %u: field cycle %d is missing; the cycle ID is retained and this "
            "timer pass is skipped.",
            static_cast<unsigned int>(user->GetDBID()), user->m_nFieldCycleID
        );
        return;
    }
    fieldCycle->OnUserTimerExpired(user);
}

int CFieldCycleSystem::GetFieldCycleStep(const int fieldCycleId) {
    // Current L2Server 0x0072ca0c.
    L2_GUARD_FUNCTION();
    CFieldCycle* const fieldCycle = GetFieldCycle(fieldCycleId);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0xa63)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"FieldCycle.cpp",
        //     0xa63)
        // Noise: Cycle ID 0 means no associated cycle, including failed quest synchronization.
        if (fieldCycleId != 0) {
            L2_LOG_ERROR(
                Log, "Cannot obtain the step for field cycle %d: the cycle is missing; returning step 0.", fieldCycleId
            );
        }
        return 0;
    }
    return fieldCycle->GetMyInfo().nowStep;
}
