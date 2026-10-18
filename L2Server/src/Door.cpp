#include "Door.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "Castle.h"
#include "DB.h"
#include "EventController.h"
#include "Fortress.h"
#include "NPC.h"
#include "ParserDoor.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Relay.h"
#include "Residence.h"
#include "SSQ.h"
#include "Shared/src/MemorySub.h"
#include "SharedContainer.h"
#include "User.h"
#include "UserSocket.h"
#include "WorldCollision.h"

#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

constexpr int kDoorReportLine = 0x24;


void CopyDoorName(char16_t (&destination)[50], const char16_t (&source)[50]) noexcept {
    std::size_t length = 0;
    while (length < 50 && source[length] != u'\0') {
        ++length;
    }
    if (length == 50) {
        destination[0] = u'\0';
        return;
    }
    std::memcpy(destination, source, (length + 1) * sizeof(char16_t));
}

void RoutePlaneCollision(CDoor* door, const char16_t* doorName, bool enabled) {
    const int currentThread = ::CThreadManager::GetThreadID();
    const int threadCount = ::CThreadManager::GetThreadCount();
    if (door->GetInZoneID() == 0 || threadCount <= currentThread) {
        g_WorldPlaneCollision.SetEnabled(std::u16string(doorName), enabled);
        return;
    }
    CInstantZone* const zone = door->GetInstantZone();
    if (zone) {
        zone->SetCollisionEnabled(std::u16string(doorName), enabled);
    }
}

} // namespace

class CDoorReleaseVerifier final : public ::CIOObject {
public:
    CDoorReleaseVerifier(CDoor* door, const wchar_t* guardString) : m_pDoor(door), m_guardStr(guardString) {
        L2_ADD_REF(m_pDoor, ObjectRefType::VerifierRetain);
        AddTimer(10000, 1);
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    }

    ~CDoorReleaseVerifier() override = default;

    void TimerExpired(int) override {
        const std::int32_t remaining = L2_RELEASE(m_pDoor, ObjectRefType::VerifierRetain);
        if (remaining > 0) {
            const char16_t* const doorName = m_pDoor->m_sDoorName;
            std::size_t doorNameLength = 0;
            while (doorNameLength < 50 && doorName[doorNameLength] != u'\0') {
                ++doorNameLength;
            }
            const std::string portableDoorName = ::Utf16ToUtf8(std::u16string_view(doorName, doorNameLength));
            const std::string portableCaller = ::WideToUtf8(m_guardStr ? m_guardStr : L"");
            // Log.Add(
            //     LOG_ERROR,
            //     "[Door.cpp][0xd9] ORT - Door Release failed. "
            //     "DoorName[%s] refcount[%d] Caller is [%s]",
            //     portableDoorName.c_str(),
            //     remaining,
            //     portableCaller.c_str())
            // L2_LOG_ERROR(Log,
            //     "[Door.cpp][0xd9] ORT - Door Release failed. "
            //     "DoorName[%s] refcount[%d] Caller is [%s]",
            //     portableDoorName.c_str(),
            //     remaining,
            //     portableCaller.c_str())
            L2_LOG_ERROR(
                Log,
                "Door '%s' still has %d references after the delayed release check (caller '%s'); reporting reference "
                "owners.",
                portableDoorName.c_str(), remaining, portableCaller.c_str()
            );
            m_pDoor->ReportOrt("Door.cpp", 0xdb);
            g_ServerContext.NotReleasedDoor.fetch_add(1, std::memory_order_relaxed);
        }
    }


private:
    CDoor* m_pDoor;
    const wchar_t* m_guardStr;
};


CDoor::CDoor() :
    m_nDoorDeleteCount(0),
    m_DamageGrade(0),
    m_Height(0),
    padding_50c{},
    m_Range{},
    m_nOwningResidenceID(0),
    padding_574{},
    m_pOwningResidence(nullptr),
    m_sDoorName{},
    padding_5e4{},
    m_dPreviousHp(0.0),
    m_eDoorOpenMethod(DOOROPEN_NPC),
    m_eType(DOORT_NORMAL),
    m_eMasterOpenEvent(DOOR_ACT_NONE),
    m_eMasterCloseEvent(DOOR_ACT_NONE),
    m_pParent(nullptr),
    m_pMaster(nullptr),
    m_nCloseTime(30000),
    m_nOpenTime(30000),
    m_nRandomTime(0),
    m_dwLastSendTime(0),
    m_dBaseHp(150.0),
    m_bSiegeProof(false),
    m_bDefaultOpened(false),
    padding_66a{},
    m_InstantZoneKey{0, 0},
    m_nRemainTime(0),
    m_bNeedToLog(true),
    m_bOpenByDoorNameSkill(false),
    padding_67a{},
    m_nHpShowable(1),
    m_nInactiveTime(0),
    m_isCheckCollisiion(true),
    padding_685{},
    m_pEventController(nullptr) {
    L2_GUARD_FUNCTION();
    m_data->m_bIsOpen = false;
    m_data->m_bBreakable = false;
    m_data->m_dMaxHP = 150.0;
    SetHP(150.0);
    ValidateDamageGrade();
    m_data->m_cLevel = 7U;
    m_data->m_nStealth = 1;
    CStaticObject::OnCreate();
}

CDoor::CDoor(const CDoor& other) :
    CStaticObject(other),
    m_nDoorDeleteCount(0),
    m_DamageGrade(other.m_DamageGrade),
    m_HingePos(other.m_HingePos),
    m_Height(0),
    padding_50c{},
    m_Range{other.m_Range[0], other.m_Range[1], other.m_Range[2], other.m_Range[3]},
    m_nOwningResidenceID(other.m_nOwningResidenceID),
    padding_574{},
    m_pOwningResidence(other.m_pOwningResidence),
    m_sDoorName{},
    padding_5e4{},
    m_dPreviousHp(other.m_dPreviousHp),
    m_eDoorOpenMethod(other.m_eDoorOpenMethod),
    m_eType(DOORT_NORMAL),
    m_eMasterOpenEvent(DOOR_ACT_NONE),
    m_eMasterCloseEvent(DOOR_ACT_NONE),
    m_pParent(nullptr),
    m_pMaster(nullptr),
    m_nCloseTime(other.m_nCloseTime),
    m_nOpenTime(other.m_nOpenTime),
    m_nRandomTime(other.m_nRandomTime),
    m_dwLastSendTime(0),
    m_dBaseHp(other.m_dBaseHp),
    m_bSiegeProof(other.m_bSiegeProof),
    m_bDefaultOpened(other.m_bDefaultOpened),
    padding_66a{},
    m_InstantZoneKey(other.m_InstantZoneKey),
    m_nRemainTime(0),
    m_bNeedToLog(other.m_bNeedToLog),
    m_bOpenByDoorNameSkill(other.m_bOpenByDoorNameSkill),
    padding_67a{},
    m_nHpShowable(0),
    m_nInactiveTime(0),
    m_isCheckCollisiion(other.m_isCheckCollisiion),
    padding_685{},
    m_pEventController(nullptr) {
    L2_GUARD_FUNCTION();
    SetPos(other.GetPos());
    m_data->m_bIsOpen = other.m_data->m_bIsOpen;
    m_data->m_dMaxHP = other.m_data->m_dMaxHP;
    SetHP(other.GetHP());
    CopyDoorName(m_sDoorName, other.m_sDoorName);
    m_data->m_cLevel = other.m_data->m_cLevel;
    m_data->m_nStealth = other.m_data->m_nStealth;
    if (other.m_pEventController) {
        m_pEventController = static_cast<CEventController*>(other.m_pEventController->Copy());
    }
    CStaticObject::OnCreate();
    const bool originalTargetable = other.m_data->m_OrgTargetable;
    m_data->m_OrgTargetable = originalTargetable;
    m_data->m_bTargetable = originalTargetable;
}

CDoor::~CDoor() {
    L2_GUARD_FUNCTION();
    g_ServerContext.Door.fetch_sub(1, std::memory_order_relaxed);
    if (m_pEventController) {
        m_pEventController->Delete();
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CDoor)

L2_DEFINE_OBJECT_CLASS_NAME(CDoor, m_pClassName_CDoor)

L2_DEFINE_OBJECT_ALLOCATOR(CDoor, __GetClassName())

L2_DEFINE_OBJECT_NEW(CDoor)

MemoryObject* CDoor::AddRef(const char* callerFile, int callerLine, ObjectRefType type) {
    runtime::raw_atomic::AddI32(&m_nRefCount, 1);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    helper.m_nRefCount.fetch_add(1, std::memory_order_relaxed);
    helper.m_pAddCaller = callerFile;
    helper.m_nAddLine = callerLine;
    return this;
}

std::int32_t CDoor::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);
    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(LOG_ERROR,
        //                "[%s][%d] sub-type[%zu] sub-reference count[%d] "
        //                "is below zero!",
        //                callerFile ? callerFile : "", callerLine, typeIndex, subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //                "[%s][%d] sub-type[%zu] sub-reference count[%d] "
        //                "is below zero!",
        //                callerFile ? callerFile : "", callerLine, typeIndex, subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "Door %p has negative reference count %d for subtype %zu after release at %s:%d; reporting references and call "
            "stack.",
            static_cast<void*>(this), subtypeRemaining, typeIndex, callerFile ? callerFile : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt("Door.cpp", kDoorReportLine);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CDoor();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CDoor", callerFile, callerLine);
            ReportOrt("Door.cpp", kDoorReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CDoor));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CDoor", MemoryObjectID(), callerFile, callerLine);
        ReportOrt("Door.cpp", kDoorReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CDoor::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    const int threadId = ::CThreadManager::GetThreadID();
    static thread_local std::int32_t reportTick = 0;
    ++reportTick;
    int sequence = 1;
    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        L2_LOG_ERROR(
            Log, "[door references] Begin report %d, entry %d, for door %p (diagnostic worker %d), requested at %s:%d.",
            reportTick, sequence, static_cast<void*>(this), threadId, callerFile, callerLine
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        L2_LOG_ERROR(
            Log, "[door references] Begin report %d, entry %d, for door %p (diagnostic worker %d); caller is unknown.",
            reportTick, sequence, static_cast<void*>(this), threadId
        );
    }
    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = ::WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] "
        //     "[RefCount:%d]-[%s] last_add[%s][%d] "
        //     "last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] "
        //     "[RefCount:%d]-[%s] last_add[%s][%d] "
        //     "last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[door references] Report %d, entry %d (diagnostic worker %d): subtype %zu (%s) has %d references; last add at "
            "%s:%d, last release at %s:%d.",
            reportTick, sequence, threadId, index, typeName.c_str(), count, helper.m_pAddCaller, helper.m_nAddLine,
            helper.m_pReleaseCaller, helper.m_nReleaseLine
        );
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    // L2_LOG_ERROR(Log,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    L2_LOG_ERROR(
        Log, "[door references] End report %d, entry %d, for door %p (diagnostic worker %d).", reportTick, sequence + 1,
        static_cast<void*>(this), threadId
    );
}

void CDoor::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (m_bDeleting || m_Deleted != 0) {
        return;
    }
    if (m_eDoorOpenMethod == DOOROPEN_SKILL || m_eDoorOpenMethod == DOOROPEN_SKILL_ITEM ||
        m_eDoorOpenMethod == DOOROPEN_ITEM) {
        Close(false, false);
        return;
    }

    std::int32_t delay = 5000;
    if (m_eDoorOpenMethod == DOOROPEN_TIME || m_eDoorOpenMethod == DOOROPEN_TIME_SKILL) {
        std::int32_t randomDelay = 0;
        if (m_nRandomTime > 0) {
            randomDelay = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * static_cast<double>(m_nRandomTime) + 1.0);
        }
        if (!m_data->m_bIsOpen) {
            Open(false, false);
            delay = runtime::AddInt32Wrapping(m_nCloseTime, randomDelay);
        } else {
            Close(false, false);
            delay = runtime::AddInt32Wrapping(m_nOpenTime, randomDelay);
        }
    } else {
        OnTimerHpChanged(false);
    }
    AddTimer(delay, 0);
}

CObject* CDoor::Copy() { return new CDoor(*this); }

bool CDoor::IsDoor() { return true; }

CDoor* CDoor::CastDoor() { return this; }

bool CDoor::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (!m_data->m_bBreakable) {
        return false;
    }
    if (!creature) {
        // Log.Add(
        //     LOG_ERROR,
        //     "attacker null at file[%S], line[%d]",
        //     "Door.cpp",
        //     0x268)
        // L2_LOG_ERROR(Log,
        //     "attacker null at file[%S], line[%d]",
        //     "Door.cpp",
        //     0x268)
        L2_LOG_ERROR(Log, "Cannot determine door hostility: attacker is null; returning non-hostile.");
        return false;
    }
    CObject* const object = creature;
    if (!object->IsPC()) {
        return true;
    }
    if (!m_pOwningResidence) {
        return false;
    }

    User* const user = object->CastUser();
    const int dominionSiegeId = user->m_data->m_nDominionSiegeId;
    if (dominionSiegeId != 0) {
        int residenceDominionSiegeId = 0;
        if (m_pOwningResidence->IsCastle()) {
            residenceDominionSiegeId = m_pOwningResidence->CastCastle()->GetDominionSiegeID();
        } else if (m_pOwningResidence->IsFortress()) {
            residenceDominionSiegeId = m_pOwningResidence->CastFortress()->GetDominionSiegeID();
        }
        if (residenceDominionSiegeId != 0) {
            return residenceDominionSiegeId != dominionSiegeId;
        }
    }

    const int pledgeId = static_cast<int>(user->m_data->m_nPledgeID);
    const int worldId = user->m_data->m_nWorldId;
    if (m_pOwningResidence->IsCastle()) {
        return !m_pOwningResidence->IsDefendingPledge(pledgeId, worldId);
    }
    if (!m_pOwningResidence->IsWorldAgit() && !m_pOwningResidence->IsFortress()) {
        return false;
    }
    if (pledgeId != 0 && pledgeId == m_pOwningResidence->GetResidenceOwnerID()) {
        return false;
    }
    return true;
}

bool CDoor::IsAlive() { return !m_data->m_bIsOpen && m_DamageGrade < 6; }

void CDoor::SendDB(const char* format, ...) const {
    L2_GUARD_FUNCTION();
    const int worldId = g_nWorldId;
    runtime::process::SetCurrentWorldId(worldId);
    CDB::CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (worldId < 1) {
        SystemMail::sendNotSetThreadLocalWorldID(mailServer);
    } else {
        db.m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

void CDoor::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    DecideDistanceAttackable(data, creatureId, 100);
}

void CDoor::Delete() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::IncrementI32(&m_nDoorDeleteCount) == 1) {
        m_bDeleting = true;
        const wchar_t* caller = runtime::process::CurrentGuardCaller();
        if (!caller) {
            caller = L"callStackError";
        }
        new CDoorReleaseVerifier(this, caller);
        CStaticObject::Delete();
        L2_RELEASE(this, ObjectRefType::CreateDelete);
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid CDoor::Delete",
    //     L"Door.cpp",
    //     0xf2)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid CDoor::Delete",
    //     L"Door.cpp",
    //     0xf2)
    L2_LOG_ERROR(
        Log, "Repeated or invalid delete request for door %p; deletion is skipped for this request. Reporting call stack.",
        static_cast<void*>(this)
    );
    runtime::process::ReportCallStack(2, false);
}

double CDoor::AttackedBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    return GotDamageBy(attacker, damage);
}

double CDoor::GotDamageBy(CCreature* attacker, double damage) {
    L2_GUARD_FUNCTION();
    if (!m_data->m_bBreakable || m_data->m_bIsOpen) {
        return 0.0;
    }

    CObject* const attackerObject = attacker;
    if (m_eType == DOORT_WALL) {
        if (!attackerObject->IsSummonOrPet()) {
            return 0.0;
        }
        CSummon* const summon = attackerObject->CastSummon();
        if (summon->m_data->m_Race != RT_SIEGE_WEAPON) {
            return 0.0;
        }
    }

    const auto [oldGrade, newGrade] = [&] {
        L2_GUARD(*m_lock);
        const std::int32_t oldGrade = m_DamageGrade;
        SetHP(GetHP() - damage);
        if (GetHP() < 0.0) {
            SetHP(0.0);
        }
        ValidateDamageGrade();
        const std::int32_t newGrade = m_DamageGrade;
        return std::pair{oldGrade, newGrade};
    }();

    if (attackerObject->IsPC()) {
        User* const user = attackerObject->CastUser();
        CUserSocket* const socket = user->GetSocket();
        socket->Send(
            "chccScdcd", 0x62, SystemMessageId::CHARACTER_DEALT_DAMAGE_TO_CHARACTER, 3, 0xc, attackerObject->GetName(), 0xb,
            m_data->m_nEditorID, 1, runtime::Win64CvttDoubleToInt32(damage)
        );
    } else if (attackerObject->GetUserOrMaster() != nullptr) {
        User* const user = attackerObject->GetUserOrMaster();
        CUserSocket* const socket = user->GetSocket();
        socket->Send(
            "chccdcdcd", 0x62, SystemMessageId::CHARACTER_DEALT_DAMAGE_TO_CHARACTER, 3, 2, attackerObject->GetClassId(), 0xb,
            m_data->m_nEditorID, 1, runtime::Win64CvttDoubleToInt32(damage)
        );
    }
    SendDoorStatus(nullptr);
    if (newGrade != oldGrade) {
        ValidatePlaneCollision();
        const std::int32_t systemMessageId =
            m_eType == DOORT_WALL ? SystemMessageId::CASTLE_WALL_BROKEN : SystemMessageId::CASTLE_GATE_DESTROYED;
        if (m_pOwningResidence != nullptr && m_pOwningResidence->IsCastle()) {
            RelayClient::Inst()->SendDoorDestructPacket(m_pOwningResidence->GetResidenceID(), m_eType, newGrade);
        }
        if (newGrade == 6 && m_pOwningResidence != nullptr) {
            if (m_pOwningResidence->IsCastle()) {
                CResidence* const residence = m_pOwningResidence;
                FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                    if (user->IsInRealWorld() && residence->IsInBattleField(user->GetPos(), true)) {
                        user->GetSocket()->SendSystemMessage(systemMessageId);
                    }
                });
            } else if (m_pOwningResidence->IsFortress()) {
                CFortress* const fortress = m_pOwningResidence->CastFortress();
                if (fortress != nullptr) {
                    const std::wstring doorName = Utf16ToWide(m_sDoorName);
                    fortress->HandleDoorDestroyed(doorName.c_str());
                    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                        if (user->IsInRealWorld() && fortress->IsInBattleField(user->GetPos(), true)) {
                            user->GetSocket()->SendSystemMessage(systemMessageId);
                        }
                    });
                } else {
                    const std::wstring doorName = Utf16ToWide(m_sDoorName);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] door[%s] destroyed in invalid fortress",
                    //     L"Door.cpp",
                    //     0x317,
                    //     doorName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] door[%s] destroyed in invalid fortress",
                    //     L"Door.cpp",
                    //     0x317,
                    //     doorName.c_str())
                    L2_LOG_ERROR(
                        Log,
                        u"Door '%ls' was destroyed, but its owning residence could not be resolved as a fortress; fortress "
                        u"notification is skipped and door-status persistence is still requested.",
                        doorName.c_str()
                    );
                }
            }
        }
        db.RequestSaveDoorStatus(this);
    }
    return damage;
}

void CDoor::SetHP(double hp) {
    L2_GUARD_FUNCTION();
    m_data->m_dHP = hp;
}

double CDoor::GetMDefend() {
    L2_GUARD_FUNCTION();
    double defense = m_data->m_dMDefend;
    if (SSQSystem::inst().GetCurrentStatus() == SSQStatus::SEAL_EFFECT) {
        const SSQPart::type sealOwner = SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE);
        if (sealOwner == SSQPart::DAWN) {
            defense += defense * 0.2;
        } else if (sealOwner == SSQPart::TWILIGHT) {
            defense -= defense * 0.5;
        }
    }
    if (m_pOwningResidence != nullptr && m_pOwningResidence->IsFortress()) {
        CFortress* const fortress = m_pOwningResidence->CastFortress();
        if (fortress != nullptr && fortress->IsInSiege() && fortress->GetFacilityLevel(2) != 0) {
            defense *= 1.5;
        }
    }
    return defense;
}

double CDoor::GetPDefend() {
    L2_GUARD_FUNCTION();
    double defense = m_data->m_dPDefend;
    if (SSQSystem::inst().GetCurrentStatus() == SSQStatus::SEAL_EFFECT) {
        const SSQPart::type sealOwner = SSQSystem::inst().GetSealOwner(SSQSeal::STRIFE);
        if (sealOwner == SSQPart::DAWN) {
            defense += defense * 0.2;
        } else if (sealOwner == SSQPart::TWILIGHT) {
            defense -= defense * 0.5;
        }
    }
    if (m_pOwningResidence != nullptr && m_pOwningResidence->IsFortress()) {
        CFortress* const fortress = m_pOwningResidence->CastFortress();
        if (fortress != nullptr && fortress->IsInSiege() && fortress->GetFacilityLevel(2) != 0) {
            defense *= 1.5;
        }
    }
    return defense;
}

void CDoor::ValidatePlaneCollision() {
    L2_GUARD_FUNCTION();
    if (!m_isCheckCollisiion) {
        return;
    }
    const bool enabled = !m_data->m_bIsOpen && m_DamageGrade != 6;
    RoutePlaneCollision(this, m_sDoorName, enabled);
}

CDoorDB::CDoorDB() : m_pParser(nullptr), m_pLastDoor(nullptr) {
    L2_GUARD_FUNCTION();
    m_DoorNameMap.clear();
    m_DoorIdMap.clear();
    m_vSunriseDoors.clear();
}

CDoorDB::~CDoorDB() = default;

CDoor* CDoorDB::GetDoor(const char16_t* name, std::uint32_t inZoneId) {
    L2_GUARD_FUNCTION();
    if (inZoneId == 0) {
        const std::u16string key(name != nullptr ? name : u"");
        const auto found = m_DoorNameMap.find(key);
        if (found != m_DoorNameMap.end()) {
            return found->second;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Door[%s]",
        //     L"Door.cpp",
        //     0x484,
        //     name != nullptr ? name : L"")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Door[%s]",
        //     L"Door.cpp",
        //     0x484,
        //     name != nullptr ? name : L"")
        L2_LOG_ERROR(
            Log,
            u"No door is registered under name '%ls' in the world door registry; returning no door and reporting the call "
            u"stack.",
            name != nullptr ? key.c_str() : u"<null>"
        );
        runtime::process::ReportCallStack(2, false);
        return nullptr;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(inZoneId);
    if (object == nullptr || !object->IsInZone()) {
        return nullptr;
    }
    CInstantZone* const instantZone = object->CastInZone();
    return instantZone != nullptr ? instantZone->GetDoor(name) : nullptr;
}

CDoor* CDoorDB::GetDoor(int doorId) {
    L2_GUARD_FUNCTION();
    const auto found = m_DoorIdMap.find(doorId);
    if (found != m_DoorIdMap.end()) {
        return found->second;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] undefined (or unregistered) Door[%d]",
    //     L"Door.cpp",
    //     0x49b,
    //     doorId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] undefined (or unregistered) Door[%d]",
    //     L"Door.cpp",
    //     0x49b,
    //     doorId)
    L2_LOG_ERROR(Log, u"No door is registered with runtime ID %d; returning no door.", doorId);
    return nullptr;
}

void CDoorDB::SetDoorBreakable(const bool breakable) {
    // Current L2Server 0x006e26c8.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_DoorNameMap) {
        entry.second->SetBreakable(breakable);
    }
}

void CDoorDB::InitializeDoors() {
    // Current L2Server 0x006e27c4: despite the source name, the current
    // body visits the name map and calls the door's appended collision
    // validation slot only.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_DoorNameMap) {
        entry.second->ValidatePlaneCollision();
    }
}

void CDoorDB::SetDoorOpen(const bool opened) {
    // Current L2Server 0x006e3b58.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_DoorNameMap) {
        if (opened) {
            entry.second->Open(false, false);
        } else {
            entry.second->Close(false, false);
        }
    }
}

void CDoorDB::AddDoor(const char16_t* const name, CDoor* const door) {
    // Current L2Server 0x006e4884.
    L2_GUARD_FUNCTION();
    const FVector position = door->GetPos();
    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (CContainerDB::Inst().GetWorldContainer(x, y, z) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
        //     x,
        //     y,
        //     z,
        //     "Door.cpp",
        //     0x448)
        // L2_LOG_ERROR(Log,
        //     "Putitem failed : pos(%d, %d, %d), file[%s], line[%d]",
        //     x,
        //     y,
        //     z,
        //     "Door.cpp",
        //     0x448)
        L2_LOG_ERROR(
            Log, u"Cannot register door '%ls': no world container at (%d, %d, %d); this registration is skipped.",
            name != nullptr ? name : u"<null>", x, y, z
        );
        return;
    }

    if (door->m_eType != DOORT_PARENT) {
        door->m_data->m_bIsActive = true;
        g_PendingStaticObjects.RegisterAsPending(door);
        if (!door->m_data->m_bIsOpen && door->m_pEventController != nullptr) {
            door->m_pEventController->m_data->m_bIsOpen = door->m_data->m_nStealth >= 1;
            g_PendingStaticObjects.RegisterAsPending(door->m_pEventController);
        }
    }

    m_DoorNameMap[std::u16string(name != nullptr ? name : u"")] = door;
    m_DoorIdMap[static_cast<int>(door->Id())] = door;
    m_pLastDoor = door;
}

bool CDoorDB::LoadData(char16_t* const fileBuffer, const int fileSize, const bool firstStep) {
    // Current L2Server 0x006e2020.
    L2_GUARD_FUNCTION();
    bool result = false;
    m_pParser = new CParserForDoor;
    m_pParser->m_bFirstStep = firstStep;
    if (!m_pParser->Create(this, fileBuffer, fileSize)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] trapParser.create failed",
        //     L"Door.cpp",
        //     0x4d4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] trapParser.create failed",
        //     L"Door.cpp",
        //     0x4d4)
        L2_LOG_ERROR(
            Log, u"Cannot initialize the door/trap data parser; loading failed before parsing (first step: %ls).",
            firstStep ? L"yes" : L"no"
        );
    } else {
        if (m_pParser->yyparse() == 0) {
            result = true;
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Parsing trapdata file failed after line[%d]",
            //     L"Door.cpp",
            //     0x4df,
            //     m_pParser->m_lexer.line)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Parsing trapdata file failed after line[%d]",
            //     L"Door.cpp",
            //     0x4df,
            //     m_pParser->m_lexer.line)
            L2_LOG_ERROR(
                Log, u"Door/trap data parsing failed near line %lld (first step: %ls); loading reports failure.",
                static_cast<long long>(m_pParser->m_lexer.line) + 1, firstStep ? L"yes" : L"no"
            );
        }
    }
    delete m_pParser;
    m_pParser = nullptr;
    return result;
}

bool CDoorDB::LoadDoor(const bool firstStep) {
    // Current L2Server 0x006e4630.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kDoorDataPath = L"..\\Script\\doordata.txt";
#else
    constexpr const wchar_t* kDoorDataPath = L"../script/doordata.txt";
#endif
    CSecureStream stream(kDoorDataPath, true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from door file failed",
        //     L"Door.cpp",
        //     0x4a9)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from door file failed",
        //     L"Door.cpp",
        //     0x4a9)
        L2_LOG_ERROR(
            Log, u"Cannot read doordata.txt from the scripts directory; door loading failed (first step: %ls).",
            firstStep ? L"yes" : L"no"
        );
        return false;
    }
    return LoadData(stream.GetRawString() + 1, stream.GetRawStringLen() - 1, firstStep);
}

bool CDoorDB::LoadTrap() {
    // Current L2Server 0x006e475c.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kTrapDataPath = L"..\\Script\\TrapData.txt";
#else
    constexpr const wchar_t* kTrapDataPath = L"../script/TrapData.txt";
#endif
    CSecureStream stream(kTrapDataPath, true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Door.cpp",
        //     0x4bc)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Door.cpp",
        //     0x4bc)
        L2_LOG_ERROR(Log, u"Cannot read TrapData.txt from the scripts directory; trap loading failed.");
        return false;
    }
    return LoadData(stream.GetRawString() + 1, stream.GetRawStringLen() - 1, false);
}

void CDoorDB::StartDoorTimer() {
    // Current L2Server 0x006e39ac.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_DoorNameMap) {
        CDoor* const door = entry.second;
        if (door->m_eDoorOpenMethod == DOOROPEN_TIME || door->m_eDoorOpenMethod == DOOROPEN_TIME_SKILL) {
            std::int32_t randomDelay = 0;
            if (door->m_nRandomTime > 0) {
                randomDelay =
                    runtime::Win64CvttDoubleToInt32(::genrand64_real2() * static_cast<double>(door->m_nRandomTime) + 1.0);
            }
            const std::int32_t delay =
                runtime::AddInt32Wrapping(door->IsOpened() ? door->m_nOpenTime : door->m_nCloseTime, randomDelay);
            door->AddTimer(delay, 0);
        } else if (door->IsOpened()) {
            door->Open(false, false);
        }
        if (door->m_isCheckCollisiion) {
            door->ValidatePlaneCollision();
        }
    }
}

void CDoorDB::OnSunrise(const bool isNight) {
    // Current L2Server 0x006e44b8. The argument name reflects the current
    // caller: entering night selects every door's configured close event.
    L2_GUARD_FUNCTION();
    // entering day selects the matching open event.
    for (CDoor* const door : m_vSunriseDoors) {
        if (door != nullptr) {
            door->DoDoorAction(isNight ? door->m_eMasterCloseEvent : door->m_eMasterOpenEvent);
        }
    }
}

CDoorDB g_DoorDB;

const char16_t* CDoor::GetDoorName() const { return m_sDoorName; }

void CDoor::CalcCenterPos() {
    L2_GUARD_FUNCTION();
    std::int32_t sumX = 0;
    std::int32_t sumY = 0;
    for (const FVector& point : m_Range) {
        sumX = runtime::AddInt32Wrapping(sumX, runtime::Win64CvttDoubleToInt32(point.x));
        sumY = runtime::AddInt32Wrapping(sumY, runtime::Win64CvttDoubleToInt32(point.y));
    }
    const std::int32_t centerX = sumX / 4;
    const std::int32_t centerY = sumY / 4;
    const std::int32_t centerZ =
        runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(m_Range[0].z), m_Height / 2);
    SetPos(FVector{static_cast<double>(centerX), static_cast<double>(centerY), static_cast<double>(centerZ)});
}

void CDoor::GetTerritory(::CTerritory& territory) const {
    L2_GUARD_FUNCTION();
    for (const FVector& point : m_Range) {
        const std::int32_t z = runtime::Win64CvttDoubleToInt32(point.z);
        if (z < territory.GetBottom()) {
            territory.SetBottom(z);
        }
        territory.AddTerritoryPoint(
            runtime::Win64CvttDoubleToInt32(point.x), runtime::Win64CvttDoubleToInt32(point.y)
        );
    }
    territory.SetTop(runtime::AddInt32Wrapping(territory.GetBottom(), m_Height));
}

void CDoor::SetOwningResidence(CResidence* residence) {
    L2_GUARD_FUNCTION();
    m_pOwningResidence = residence;
    CResidenceData* const residenceData = residence->m_data;
    m_nOwningResidenceID = residenceData ? residenceData->m_nResidenceID : 0;
}

bool CDoor::CanBeOpenedBy(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (m_eDoorOpenMethod != DOOROPEN_CYCLE && m_eDoorOpenMethod == DOOROPEN_CLICK) {
        CObject* const object = creature;
        if (object->IsPC()) {
            User* const user = object->CastUser();
            if (user->HavePledgePower(PP_OPEN_AGIT_DOOR) && m_nOwningResidenceID > 0) {
                CResidence* const residence = g_ResidenceDB.GetResidence(m_nOwningResidenceID);
                if (residence == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "residence null at file[%s], line[%d]",
                    //     "Door.cpp",
                    //     0x21a)
                    // L2_LOG_ERROR(Log,
                    //     "residence null at file[%s], line[%d]",
                    //     "Door.cpp",
                    //     0x21a)
                    L2_LOG_ERROR(
                        Log, "Cannot authorize opening the door: owning residence %d was not found; opening is denied.",
                        m_nOwningResidenceID
                    );
                } else {
                    const int ownerPledgeId = residence->GetResidenceOwnerID();
                    return ownerPledgeId != 0 && ownerPledgeId == static_cast<int>(user->m_data->m_nPledgeID);
                }
            }
        }
    }
    return false;
}

void CDoor::SendDoorStatus(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    const auto [opened, damageGrade, enemy, targetable] = [&] {
        L2_GUARD(*m_lock);
        const bool opened = m_data->m_bIsOpen;
        const std::int32_t damageGrade = m_DamageGrade;
        bool enemy = false;
        if (socket) {
            User* const user = socket->GetUser();
            enemy = IsEnemyTo(user);
        }
        const bool targetable = IsTargetable();
        return std::tuple{opened, damageGrade, enemy, targetable};
    }();

    if (!socket) {
        CWorld::Inst().DoorStatusUpdate(this);
        return;
    }
    socket->Send(
        "cddddddddddd", 0x9f, m_data->m_nEditorID, m_nId, 1, targetable ? 1 : 0, m_data->m_nStealth, opened ? 0 : 1,
        enemy ? 1 : 0, runtime::Win64CvttDoubleToInt32(GetHP()), runtime::Win64CvttDoubleToInt32(m_data->m_dMaxHP),
        m_nHpShowable, damageGrade
    );
}

void CDoor::ValidateDamageGrade() {
    L2_GUARD_FUNCTION();
    double maximumHp = m_data->m_dMaxHP;
    if (maximumHp < 0.0) {
        maximumHp = 1.0;
    }
    const std::int32_t grade = runtime::Win64CvttDoubleToInt32(((maximumHp - GetHP()) * 6.0) / maximumHp);
    if (grade < 0) {
        m_DamageGrade = 0;
    } else {
        m_DamageGrade = grade < 7 ? grade : 6;
    }
}

void CDoor::OnTimerHpChanged(bool force) {
    L2_GUARD_FUNCTION();
    const double hp = GetHP();
    if (m_dPreviousHp != hp || force) {
        db.RequestSaveDoorStatus(this);
        SendDoorStatus(nullptr);
        m_dPreviousHp = hp;
    }
}

void CDoor::Open(bool direct, bool force) {
    L2_GUARD_FUNCTION();
    if (m_eDoorOpenMethod == DOOROPEN_CYCLE && !force) {
        return;
    }
    if (m_bNeedToLog) {
        const std::u16string doorName = Utf16Text(m_sDoorName).String();
        CLogFormatter formatter;
        formatter.WriteLog4(
            0xe9, nullptr, nullptr, doorName.c_str(), nullptr, nullptr, m_nOwningResidenceID, DOOR_ACT_OPEN, direct, 0, 0, 0,
            0, 0, 0, 0, 0
        );
    }
    if (m_eType == DOORT_CHILD && !direct) {
        if (m_pParent) {
            m_pParent->Open(false, force);
        }
        return;
    }
    if (GetInZoneID() == 0 || ::CThreadManager::GetThreadCount() <= ::CThreadManager::GetThreadID()) {
        if (m_isCheckCollisiion) {
            g_WorldPlaneCollision.SetEnabled(std::u16string(m_sDoorName), false);
        }
    } else {
        CInstantZone* const zone = GetInstantZone();
        if (zone != nullptr) {
            zone->SetCollisionEnabled(std::u16string(m_sDoorName), false);
        }
    }
    if ((m_eDoorOpenMethod == DOOROPEN_SKILL || m_eDoorOpenMethod == DOOROPEN_SKILL_ITEM ||
            m_eDoorOpenMethod == DOOROPEN_ITEM) &&
        IsInRealWorld()) {
        AddTimer(m_nCloseTime, 0);
    }
    CWorld::Inst().DoorOpenClose(this, true, force);
    if (m_pEventController && m_data->m_nStealth > 0) {
        m_pEventController->SetOnOff(false);
    }
}

void CDoor::Close(bool direct, bool force) {
    L2_GUARD_FUNCTION();
    if (m_eDoorOpenMethod == DOOROPEN_CYCLE && !force) {
        return;
    }
    if (m_bNeedToLog) {
        const std::u16string doorName = Utf16Text(m_sDoorName).String();
        CLogFormatter formatter;
        formatter.WriteLog4(
            0xe9, nullptr, nullptr, doorName.c_str(), nullptr, nullptr, m_nOwningResidenceID, DOOR_ACT_CLOSE, direct, 0, 0,
            0, 0, 0, 0, 0, 0
        );
    }
    if (m_eType == DOORT_CHILD && !direct) {
        if (m_pParent) {
            m_pParent->Close(false, false);
        }
        return;
    }
    if (m_DamageGrade != 6) {
        CInstantZone* const zone = GetInstantZone();
        if (zone) {
            zone->SetCollisionEnabled(std::u16string(m_sDoorName), true);
        } else if (m_isCheckCollisiion) {
            g_WorldPlaneCollision.SetEnabled(std::u16string(m_sDoorName), true);
        }
    }
    m_nRemainTime = static_cast<std::int32_t>(runtime::GetTickCount());
    CWorld::Inst().DoorOpenClose(this, false, force);
    if (m_pEventController && m_data->m_nStealth > 0) {
        m_pEventController->SetOnOff(true);
    }
}

void CDoor::DoDoorAction(DoorActionTypeEnum action) {
    L2_GUARD_FUNCTION();
    if (action == DOOR_ACT_OPEN) {
        Open(false, false);
    } else if (action == DOOR_ACT_CLOSE) {
        Close(false, false);
    }
}

void CDoor::ResetDoorState(bool forceClose, double hpRatio) {
    L2_GUARD_FUNCTION();
    m_data->m_dMaxHP = m_dBaseHp;
    if (m_data->m_bBreakable) {
        SetHP(m_dBaseHp * hpRatio);
    }
    ValidateDamageGrade();
    if (!m_bDefaultOpened || forceClose) {
        Close(false, false);
    } else {
        Open(false, false);
    }
    OnTimerHpChanged(true);
}
