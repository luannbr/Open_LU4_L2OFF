#include "TeleportManager.h"
#include "Shared/src/MemorySub.h"

#include "GameEventSystem.h"
#include "User.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <cstring>
#include <string>

namespace {
// Shipping number (metadata only): 274.
constexpr const char* kSource = "TeleportManager.cpp";
constexpr int kReportLine = 0x0b;
} // namespace

// Current L2Server.exe roots 0x00db6ad0/0x00db6b3c publish the physical
// TeleportManager.cpp globals at 0x20d87090/0x20d870b0 in this order.
std::map<std::int64_t, std::vector<TelPos>*> s_mapTelPosList;
CIOCriticalSection s_mapTelPosListSect;
std::int16_t TeleporterTypeMask[TELEPORTER_MAX] = {0, 1, 2, 4};

CTeleporter::CTeleporter() : m_teleporterFlag(0), m_alignment3d7aTo3d7c{}, m_param{} {}

CTeleporter::CTeleporter(const CTeleporter& other) : CNPC(other), m_teleporterFlag(0), m_alignment3d7aTo3d7c{}, m_param{} {
    L2_GUARD_FUNCTION();
}

CTeleporter::~CTeleporter() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CTeleporter)

std::int32_t CTeleporter::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);
    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(LOG_ERROR,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] is below zero!",
        //     callerFile ? callerFile : "", callerLine, typeIndex,
        //     subtypeRemaining)
        // Log.Add(LOG_ERROR,
        //     "[teleporter references] Subtype %llu reference count became %d after release at %s:%d. Dumping reference
        //     history.", static_cast<unsigned long long>(typeIndex), subtypeRemaining, callerFile ? callerFile :
        //     "<unspecified>", callerLine)
        L2_LOG_ERROR(
            Log,
            "[teleporter references] Subtype %llu reference count became %d after release at %s:%d. Dumping reference "
            "history.",
            static_cast<unsigned long long>(typeIndex), subtypeRemaining, callerFile ? callerFile : "<unspecified>",
            callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kSource, kReportLine);
    }
    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CTeleporter();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CTeleporter", callerFile, callerLine);
            ReportOrt(kSource, kReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CTeleporter));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CTeleporter", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kSource, kReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CTeleporter::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    const int threadId = CThreadManager::GetThreadID();
    static thread_local std::int32_t reportTick = 0;
    ++reportTick;
    int sequence = 1;
    if (callerFile) {
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId, reportTick, sequence, callerFile, callerLine)
        // Log.Add(LOG_ERROR,
        //     "[teleporter references] Report %d, thread slot %d, entry %d: begin (requested by %s:%d).",
        //     reportTick, threadId, sequence, callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "[teleporter references] Report %d, thread slot %d, entry %d: begin (requested by %s:%d).", reportTick,
            threadId, sequence, callerFile, callerLine
        );
    } else {
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId, reportTick, sequence)
        // Log.Add(LOG_ERROR,
        //     "[teleporter references] Report %d, thread slot %d, entry %d: begin (caller unavailable).",
        //     reportTick, threadId, sequence)
        L2_LOG_ERROR(
            Log, "[teleporter references] Report %d, thread slot %d, entry %d: begin (caller unavailable).", reportTick,
            threadId, sequence
        );
    }
    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] [RefCount:%d]-[%s] "
        //     "last_add[%s][%d] last_release[%s][%d]",
        //     threadId, reportTick, sequence, count, typeName.c_str(),
        //     helper.m_pAddCaller, helper.m_nAddLine,
        //     helper.m_pReleaseCaller, helper.m_nReleaseLine)
        // Log.Add(LOG_ERROR,
        //     "[teleporter references] Report %d, thread slot %d, entry %d: %s has %d references. Last add at %s:%d, last
        //     release at %s:%d.", reportTick, threadId, sequence, typeName.c_str(), count, helper.m_pAddCaller,
        //     helper.m_nAddLine, helper.m_pReleaseCaller, helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "[teleporter references] Report %d, thread slot %d, entry %d: %s has %d references; last add at %s:%d, last "
            "release at %s:%d.",
            reportTick, threadId, sequence, typeName.c_str(), count, helper.m_pAddCaller, helper.m_nAddLine,
            helper.m_pReleaseCaller, helper.m_nReleaseLine
        );
    }
    // Log.Add(LOG_ERROR, "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId, reportTick, sequence + 1)
    // Log.Add(LOG_ERROR,
    //     "[teleporter references] Report %d, thread slot %d, entry %d: end.",
    //     reportTick, threadId, sequence + 1)
    L2_LOG_ERROR(
        Log, "[teleporter references] Report %d, thread slot %d, entry %d: end.", reportTick, threadId, sequence + 1
    );
}

CObject* CTeleporter::Copy() {
    L2_GUARD_FUNCTION();
    CTeleporter* const teleporter = new CTeleporter(*this);
    teleporter->OnCreate();
    return teleporter;
}

bool CTeleporter::IsTeleporter() const { return true; }

CTeleporter* CTeleporter::CastTeleporter() { return this; }

void CTeleporter::SetType(const TeleporterType type, const int value) {
    // Current L2Server.exe 0x00ae4b14. The signed upper-bound-only check is
    // intentional: the current machine also admits negative enum values.
    L2_GUARD_FUNCTION();
    if (type < TELEPORTER_MAX) {
        {
            L2_GUARD(*m_lock);
            m_teleporterFlag = static_cast<std::int16_t>(m_teleporterFlag | TeleporterTypeMask[type]);
            m_param[type] = value;
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] NPC[%s/%d], Type[%d/%d] "
    //     L"m_teleporterFlag[%d] m_param[%d/%d/%d/%d]",
    //     L"TeleportManager.cpp",
    //     0x28,
    //     GetName(),
    //     m_data->m_nId,
    //     type,
    //     value,
    //     static_cast<int>(m_teleporterFlag),
    //     m_param[0],
    //     m_param[1],
    //     m_param[2],
    //     m_param[3])
    // Log.Add(LOG_ERROR,
    //     L"[teleporter] Ignoring type %d (value %d) for NPC \"%ls\" (shared index %d): type must be below %d. Flags %d and
    //     parameters [%d, %d, %d, %d] are unchanged.", static_cast<int>(type), value, GetName(), m_data->m_nId,
    //     static_cast<int>(TELEPORTER_MAX), static_cast<int>(m_teleporterFlag),
    //     m_param[0], m_param[1], m_param[2], m_param[3])
    // L2_LOG_ERROR(Log,
    //     L"[teleporter] Ignoring type %d (value %d) for NPC \"%ls\" (shared index %d): type must be below %d. Flags %d and
    //     parameters [%d, %d, %d, %d] are unchanged.", static_cast<int>(type), value, GetName(), m_data->m_nId,
    //     static_cast<int>(TELEPORTER_MAX), static_cast<int>(m_teleporterFlag),
    //     m_param[0], m_param[1], m_param[2], m_param[3])
    L2_LOG_ERROR(
        Log,
        u"[teleporter] Ignoring type %d (value %d) for NPC \"%ls\" (shared index %d): type must be below %d. Flags %d and "
        u"parameters [%d, %d, %d, %d] are unchanged.",
        static_cast<int>(type), value, GetName(), m_data->m_nId, static_cast<int>(TELEPORTER_MAX),
        static_cast<int>(m_teleporterFlag), m_param[0], m_param[1], m_param[2], m_param[3]
    );
}

TeleporterType CTeleporter::CanTeleport(User* const user) {
    // Current L2Server.exe 0x00ae4cbc.
    L2_GUARD_FUNCTION();
    for (int type = TELEPORTER_FREE; type > TELEPORTER_NONE; --type) {
        const std::int16_t mask = TeleporterTypeMask[type];
        if ((m_teleporterFlag & mask) != mask) {
            continue;
        }
        if (type == TELEPORTER_COMMON) {
            return TELEPORTER_COMMON;
        }
        if (type == TELEPORTER_CORETIME) {
            CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_CORETIME_TELEPORT);
            if (event != nullptr && static_cast<CEvent_CoretimeTeleport*>(event)->IsInCoretime()) {
                return TELEPORTER_CORETIME;
            }
        } else if (type == TELEPORTER_FREE && user->Level() <= m_param[TELEPORTER_FREE] && user->m_data->m_nPledgeID == 0) {
            return TELEPORTER_FREE;
        }
    }
    return TELEPORTER_NONE;
}
