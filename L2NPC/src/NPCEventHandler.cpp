#include "NPCEventHandler.h"

#include "L2NPC/src/Config.h"
#include "NpcEvent.h"
#include "NpcMakerEx.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "ScriptBinCode.h"
#include "ScriptEngine.h"
#include "Shared/src/Log.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/YieldLock.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>


namespace {
// Current L2NPC.exe 0x00da9e68: InterlockedIncrement/Exchange operate on
// this raw LONG, while the threshold/modulo paths use plain
// aligned reads.
volatile std::int32_t g_nNPCEventCount = 0;

struct EventCountRecord {
    int npcClassId = 0;
    int eventId = 0;
    std::int32_t count = 0;
};

// Current globals are contiguous in this exact order:
// round 0x02789f00, map 0x02789f08, lock 0x02789f28.
int g_npcEventReportRound = 0;
std::map<std::pair<int, int>, std::int32_t> g_npcEventReport;
CYieldLock g_npcEventReportLock;

} // namespace

CNPCEventHandler::CNPCEventHandler(CNPCEventHandler* superEventHandler, CAIParameter* parameter, CAIProperty* property) {
    L2_GUARD_FUNCTION();
    m_pSuperEventHandler = superEventHandler;
    m_pParameter = parameter;
    m_pProperty = property;
}

CNPCEventHandler::CNPCEventHandler(const CNPCEventHandler& other) :
    m_sAIClassName(other.m_sAIClassName),
    m_pSuperEventHandler(other.m_pSuperEventHandler),
    m_mapEventIdToScriptCode(other.m_mapEventIdToScriptCode),
    m_pParameter(other.m_pParameter),
    m_pProperty(other.m_pProperty) {}

CNPCEventHandler& CNPCEventHandler::operator=(const CNPCEventHandler& other) {
    if (this == &other) {
        return *this;
    }
    m_sAIClassName = other.m_sAIClassName;
    m_pSuperEventHandler = other.m_pSuperEventHandler;
    m_mapEventIdToScriptCode = other.m_mapEventIdToScriptCode;
    m_pParameter = other.m_pParameter;
    m_pProperty = other.m_pProperty;
    return *this;
}

CNPCEventHandler::~CNPCEventHandler() = default;

int CNPCEventHandler::AddEventHandler(int eventId, CScriptBinCode* scriptBinCode) {
    L2_GUARD_FUNCTION();
    const auto inserted = m_mapEventIdToScriptCode.emplace(eventId, scriptBinCode);
    if (!inserted.second) {
        const std::string aiClassName = Utf16ToUtf8(std::u16string_view(GetAIClassName()));
        // Log.Add(LOG_ERROR,
        //                L"Same Event Handler (id = %d) "
        //                L"exists in a class [%s]",
        //                eventId, aiClassName.c_str())
        // Log.Add(LOG_ERROR,
        //     "[AI load] Ignoring duplicate event handler %d in script '%s'. Keeping the existing handler.",
        //     eventId, WideToUtf8(aiClassName).c_str())
        L2_LOG_ERROR(
            Log, "[AI load] Ignoring duplicate event handler %d in script '%s'; keeping the existing handler.", eventId,
            aiClassName.c_str()
        );
        return 0;
    }
    g_ScriptCodeInfo.AddInfo(this, eventId, scriptBinCode);
    return 1;
}

void CNPCEventHandler::SuperEventHandler(CNPCEventHandler* superEventHandler) { m_pSuperEventHandler = superEventHandler; }

void CNPCEventHandler::SetAIClassName(const char16_t* value) {
    L2_GUARD_FUNCTION();
    m_sAIClassName.assign(std::u16string_view(value));
}

const char16_t* CNPCEventHandler::GetAIClassName() const { return m_sAIClassName.c_str(); }

int CNPCEventHandler::HandleEvent(ScriptEvent* event, CAIParameter* parameter) {
    L2_GUARD_FUNCTION();
    if (!event) {
        return -1;
    }

    CheckEventCount(event);

    const auto found = m_mapEventIdToScriptCode.find(event->m_nEventId);
    const bool hasLocalCode = found != m_mapEventIdToScriptCode.end() && found->second;
    if (!hasLocalCode) {
        return m_pSuperEventHandler ? m_pSuperEventHandler->HandleEvent(event, parameter, m_pProperty) : 0;
    }

    return CScriptEngine::RunNPCScript(event, this, found->second, parameter, m_pProperty);
}

int CNPCEventHandler::HandleEvent(ScriptEvent* event, CAIParameter* parameter, CAIProperty* property) {
    L2_GUARD_FUNCTION();
    if (!event) {
        return -1;
    }

    CheckEventCount(event);

    const auto found = m_mapEventIdToScriptCode.find(event->m_nEventId);
    const bool hasLocalCode = found != m_mapEventIdToScriptCode.end() && found->second;
    if (!hasLocalCode) {
        return m_pSuperEventHandler ? m_pSuperEventHandler->HandleEvent(event, parameter, property) : 0;
    }

    return CScriptEngine::RunNPCScript(event, this, found->second, parameter, property);
}

void CNPCEventHandler::CheckEventCount(ScriptEvent* event) {
    L2_GUARD_FUNCTION();

    runtime::raw_atomic::IncrementI32(&g_nNPCEventCount);
    if (runtime::raw_atomic::LoadI32Relaxed(&g_nNPCEventCount) > 1000000000) {
        const std::string selfName = Utf16ToUtf8(std::u16string_view(event->GetMySelfName()));
        const std::int32_t count = runtime::raw_atomic::LoadI32Relaxed(&g_nNPCEventCount);
        // Log.Add(LOG_ERROR,
        //                L"[CountLog] Npc Event Count Log Reset "
        //                L"By Exceeding Max [%d] npc [%s] event [%d] ",
        //                count, selfName.c_str(), event->m_nEventId)
        // Log.Add(LOG_NORMAL,
        //     "[AI statistics] Resetting event counter %d after exceeding 1000000000. Current NPC '%s', event %d.",
        //     count, WideToUtf8(selfName).c_str(), event->m_nEventId)
        // L2_LOG_INFO(Log,
        //     "[AI statistics] Resetting event counter %d after exceeding 1000000000. Current NPC '%s', event %d.",
        //     count, WideToUtf8(selfName).c_str(), event->m_nEventId)
        L2_LOG_INFO(
            Log,
            "[AI statistics] Resetting event counter %d after exceeding 1000000000; current event owner '%s', event %d.",
            count, selfName.c_str(), event->m_nEventId
        );
        runtime::raw_atomic::ExchangeI32(&g_nNPCEventCount, 0);
    }

    if (::g_bSetReportNpcEventCount && event && event->GetMySelfID() != 0) {
        const int eventId = event->m_nEventId;
        const int npcClassId = event->GetMySelfID();
        L2_GUARD(g_npcEventReportLock);
        const std::pair<int, int> key{npcClassId, eventId};
        if (g_npcEventReport.find(key) == g_npcEventReport.end()) {
            g_npcEventReport[key] = 0;
        } else {
            g_npcEventReport[key] = runtime::AddInt32Wrapping(g_npcEventReport[key], 1);
        }
    }

    const std::int32_t current = runtime::raw_atomic::LoadI32Relaxed(&g_nNPCEventCount);
    if (current % 10000000 == 1) {
        const std::string selfName = Utf16ToUtf8(std::u16string_view(event->GetMySelfName()));
        const std::int32_t reportedCount = runtime::raw_atomic::LoadI32Relaxed(&g_nNPCEventCount);
        // Log.Add(LOG_ERROR,
        //                L"[CountLog] Npc Event Count [%d] "
        //                L"npc [%s] event [%d] ",
        //                reportedCount, selfName.c_str(), event->m_nEventId)
        // Log.Add(LOG_NORMAL,
        //     "[AI statistics] Event counter %d. Current NPC '%s', event %d.",
        //     reportedCount, WideToUtf8(selfName).c_str(), event->m_nEventId)
        // L2_LOG_INFO(Log,
        //     "[AI statistics] Event counter %d. Current NPC '%s', event %d.",
        //     reportedCount, WideToUtf8(selfName).c_str(), event->m_nEventId)
        L2_LOG_INFO(
            Log, "[AI statistics] Event counter %d; current event owner '%s', event %d.", reportedCount,
            selfName.c_str(), event->m_nEventId
        );
        if (::g_bSetReportNpcEventCount) {
            L2_GUARD(g_npcEventReportLock);
            std::vector<EventCountRecord, std::allocator<EventCountRecord>> records;
            records.reserve(g_npcEventReport.size());
            for (const auto& entry : g_npcEventReport) {
                records.push_back(EventCountRecord{entry.first.first, entry.first.second, entry.second});
            }
            std::stable_sort(
                records.begin(), records.end(),
                [](const EventCountRecord& left, const EventCountRecord& right) { return left.count > right.count; }
            );
            int reported = 0;
            for (const EventCountRecord& record : records) {
                // Log.Add(LOG_ERROR,
                //                L"[EventSort] NPCClassID [%d] "
                //                L"EventID [%d] Count [%d] ",
                //                record.npcClassId, record.eventId, record.count)
                // Log.Add(LOG_NORMAL,
                //     "[AI statistics] NPC class %d, event %d: recorded counter %d.",
                //     record.npcClassId, record.eventId, record.count)
                L2_LOG_INFO(
                    Log, "[AI statistics] NPC class %d, event %d: recorded counter %d.", record.npcClassId, record.eventId,
                    record.count
                );
                ++reported;
                if (reported > 100) {
                    break;
                }
            }

            ++g_npcEventReportRound;
            if (g_npcEventReportRound == 2) {
                g_npcEventReport.clear();
                g_npcEventReportRound = 0;
            }
        }
    }
}

CNPCEventHandler* GetSuperEventHandler(const CNPCEventHandler* handler) {
    return handler ? handler->m_pSuperEventHandler : nullptr;
}

const CScriptBinCode* FindEventHandlerCode(const CNPCEventHandler* handler, int eventId, const CNPCEventHandler** owner) {
    const CNPCEventHandler* current = handler;
    while (current) {
        const auto found = current->m_mapEventIdToScriptCode.find(eventId);
        if (found != current->m_mapEventIdToScriptCode.end() && found->second) {
            if (owner) {
                *owner = current;
            }
            return found->second;
        }
        current = current->m_pSuperEventHandler;
    }
    if (owner) {
        *owner = nullptr;
    }
    return nullptr;
}
