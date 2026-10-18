#include "L2NPC/src/DesirePQ.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/Config.h"
#include "L2NPC/src/NPC.h"
#include "L2NPC/src/NPCEventFactory.h"
#include "L2NPC/src/NpcEvent.h"
#include "L2NPC/src/User.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Runtime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

thread_local int g_tlMaxDesireSize = 0;


namespace {

void DeleteReverseHate(CNPC* npc, const CDesire& desire) {
    if (desire.type == DESIRE_ATTACK && g_nMaxChaseCount > 0 && npc) {
        ReverseHateManager::inst().DeleteHate(
            static_cast<std::uint32_t>(desire.targetId), static_cast<std::uint32_t>(npc->Id())
        );
    }
}

void AppendNormalDesireHeader(std::u16string& output, int count) {
    std::array<char, 64> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), "Normal Desire [%d]: \r\n", count);
    if (length > 0) {
        output +=
            Utf8ToUtf16(std::string_view(buffer.data(), std::min(static_cast<std::size_t>(length), buffer.size() - 1)));
    }
}

} // namespace

CDesirePQ::CDesirePQ(CYieldLock& desireLock) : m_DesireLock(&desireLock), m_AbsoluteDesire(DESIRE_NONE) {}

CDesirePQ::~CDesirePQ() {
    // Current L2NPC.exe 0x004264a8 clears the owner before the base vector
    // releases its storage.
    m_pNPC = nullptr;
}

int CDesirePQ::AddValue(const CDesire& desire) {
    L2_GUARD_FUNCTION();

    int topChanged = 0;
    return AddValue(desire, topChanged);
}

int CDesirePQ::AddValue(const CDesire& desire, int& topChanged) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    return _AddValue(desire, topChanged);
}

int CDesirePQ::_AddValue(const CDesire& desire, int& topChanged) {
    L2_GUARD_FUNCTION();

    CDesire oldTop;
    if (m_pNPC && (m_pNPC->m_data->m_bTeleporting || m_pNPC->m_bDesireFreeze)) {
        topChanged = 0;
        return 0;
    }

    if (desire.value == -1.0F) {
        m_bSetAbsoluteDesire = true;
        m_AbsoluteDesire = desire;
        m_AbsoluteDesire.absolute = true;
        topChanged = 1;
        return 1;
    }

    if (!c.empty()) {
        oldTop = c.front();
    }

    _It found = c.begin();
    for (; found != c.end(); ++found) {
        if (*found == desire) {
            break;
        }
    }

    if (found == c.end()) {
        c.push_back(desire);
        const int queueSize = static_cast<int>(c.size());
        if (g_tlMaxDesireSize < queueSize) {
            g_tlMaxDesireSize = queueSize;
            if (queueSize % 50 == 0) {
                std::wstring npcName;
                if (m_pNPC) {
                    const char16_t* const name = m_pNPC->GetName();
                    if (name) {
                        npcName = Utf16ToWide(std::u16string_view(name));
                    }
                }
                // Log.Add(
                //     LOG_ERROR,
                //     L"Too large desire size[%d] in npc[%s], "
                //     L"recent desire[%d]",
                //     queueSize,
                //     npcName.c_str(),
                //     desire.type)
                // Log.Add(LOG_WARN,
                //     "[NPC actions] Queue reached %d entries for NPC '%s' (object %u), the largest seen by this thread.
                //     action type %d with priority %.9g was retained.", queueSize, WideToUtf8(npcName).c_str(), m_pNPC ?
                //     static_cast<unsigned int>(m_pNPC->Id()) : 0U, desire.type, static_cast<double>(desire.value))
                L2_LOG_WARN(
                    Log,
                    "[NPC actions] Queue reached %d entries for NPC '%s' (object %u), the largest seen by this thread; "
                    "action type %d with priority %.9g was retained.",
                    queueSize, WideToUtf8(npcName).c_str(), m_pNPC ? static_cast<unsigned int>(m_pNPC->Id()) : 0U,
                    desire.type, static_cast<double>(desire.value)
                );
                // ReportScriptContext(LOG_ERROR)
                ReportScriptContext(LOG_WARN);
            }
        }
    } else {
        found->value += desire.value;
        found->force = desire.force;
        if (found->value > MAX_DESIRE) {
            found->value = MAX_DESIRE;
        }
    }

    if (c.size() > 1) {
        RebuildHeapTargetOrder();
    }
    topChanged = oldTop != c.front();
    return 1;
}

int CDesirePQ::SetValue(const CDesire& desire) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (c.empty()) {
        return 0;
    }

    for (_It current = c.begin(); current != c.end(); ++current) {
        if (*current == desire) {
            current->value = desire.value;
            current->m_MoveSuperPointContext = desire.m_MoveSuperPointContext;
            if (c.size() > 1) {
                RebuildHeapTargetOrder();
            }
            return 1;
        }
    }
    return 0;
}

int CDesirePQ::BoostUp(const CDesire& desire) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    return _BoostUp(desire);
}

int CDesirePQ::_BoostUp(const CDesire& desire) {
    L2_GUARD_FUNCTION();

    for (_It current = c.begin(); current != c.end(); ++current) {
        if (*current == desire) {
            current->value += current->boost;
            current->boosted = true;
            ++current->boostCount;
            if (c.size() > 1) {
                RebuildHeapTargetOrder();
            }
            return 1;
        }
    }
    return 0;
}

int CDesirePQ::_BoostDown(const CDesire& desire) {
    L2_GUARD_FUNCTION();

    for (_It current = c.begin(); current != c.end(); ++current) {
        if (*current == desire) {
            current->value -= current->boost;
            current->boosted = false;
            if (c.size() > 1) {
                RebuildHeapTargetOrder();
            }
            return 1;
        }
    }
    return 0;
}

int CDesirePQ::RemoveKey(const CDesire& desire, bool removeHate) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    return _RemoveKey(desire, removeHate);
}

int CDesirePQ::_RemoveKey(const CDesire& desire, bool removeHate) {
    L2_GUARD_FUNCTION();

    if (desire.absolute) {
        if (m_AbsoluteDesire == desire) {
            m_bSetAbsoluteDesire = false;
        }
        return 0;
    }

    for (_It current = c.begin(); current != c.end(); ++current) {
        // The shipping loop calls CDesire::operator!= and erases only when
        // that comparison returns false.
        if (*current != desire) {
            continue;
        }
        if (removeHate) {
            DeleteReverseHate(m_pNPC, *current);
        }
        c.erase(current);
        if (c.size() > 1) {
            RebuildHeapTargetOrder();
        }
        return 1;
    }
    return 0;
}

int CDesirePQ::_GetDesire(const CDesire& desire, CDesire*& found) {
    L2_GUARD_FUNCTION();

    for (_It current = c.begin(); current != c.end(); ++current) {
        if (*current == desire) {
            found = &*current;
            return 1;
        }
    }
    return 0;
}

float CDesirePQ::_GetMaxValue(PhysicalState type) {
    L2_GUARD_FUNCTION();

    float maximum = 0.0F;
    for (const CDesire& desire : c) {
        if (desire.type == static_cast<int>(type) && maximum < desire.value) {
            maximum = desire.value;
        }
    }
    return maximum;
}

bool CDesirePQ::_IsDQEmpty() const {
    L2_GUARD_FUNCTION();

    if (m_bSetAbsoluteDesire) {
        return false;
    }
    return c.empty();
}

bool CDesirePQ::IsDQEmpty() {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    return _IsDQEmpty();
}

CDesire& CDesirePQ::_GetTopDesire() {
    L2_GUARD_FUNCTION();

    return m_bSetAbsoluteDesire ? m_AbsoluteDesire : c.front();
}

CDesire& CDesirePQ::GetTopDesire() {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    return _GetTopDesire();
}

int CDesirePQ::RandomizeDesire(PhysicalState type) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (c.empty()) {
        return 0;
    }

    std::vector<int> desireValues;
    std::vector<int> randomDesireValues;
    for (const CDesire& desire : c) {
        if (desire.type == static_cast<int>(type)) {
            desireValues.push_back(runtime::Win64CvttFloatToInt32(desire.value));
        }
    }

    int count = static_cast<int>(desireValues.size());
    while (count != 0) {
        const int selected = Random(1000) % count;
        runtime::CheckedVectorAt(desireValues, static_cast<std::size_t>(selected)) =
            runtime::CheckedVectorAt(desireValues, static_cast<std::size_t>(count - 1));
        --count;
        randomDesireValues.push_back(runtime::CheckedVectorAt(desireValues, static_cast<std::size_t>(selected)));
    }

    std::size_t randomIndex = 0;
    for (CDesire& desire : c) {
        if (desire.type == static_cast<int>(type)) {
            desire.value = static_cast<float>(runtime::CheckedVectorAt(randomDesireValues, randomIndex++));
        }
    }

    if (c.size() > 1) {
        RebuildHeapTargetOrder();
    }
    return 1;
}

int CDesirePQ::RemoveAllDesire(int type) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (m_AbsoluteDesire.type == type) {
        m_bSetAbsoluteDesire = false;
    }
    if (c.empty()) {
        return 0;
    }

    for (;;) {
        _It current = c.begin();
        for (; current != c.end(); ++current) {
            if (current->type != type) {
                continue;
            }
            DeleteReverseHate(m_pNPC, *current);
            if (current->IsInterruptable()) {
                break;
            }
        }
        if (current == c.end()) {
            if (c.size() > 1) {
                RebuildHeapTargetOrder();
            }
            return 1;
        }
        c.erase(current);
    }
}

int CDesirePQ::RemoveAll() {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    m_bSetAbsoluteDesire = false;

    for (;;) {
        _It current = c.begin();
        for (; current != c.end(); ++current) {
            if (current->type != DESIRE_DECAYING) {
                break;
            }
        }
        if (current == c.end()) {
            if (c.size() > 1) {
                RebuildHeapTargetOrder();
            }
            return 1;
        }
        DeleteReverseHate(m_pNPC, *current);
        c.erase(current);
    }
}

int CDesirePQ::ClearDesire() {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (g_nMaxChaseCount > 0) {
        for (const CDesire& desire : c) {
            if (desire.type == DESIRE_ATTACK && m_pNPC) {
                ReverseHateManager::inst().DeleteHate(
                    static_cast<std::uint32_t>(desire.targetId), static_cast<std::uint32_t>(m_pNPC->Id())
                );
            }
        }
    }
    c.clear();
    return 1;
}

int CDesirePQ::Decay() {
    L2_GUARD_FUNCTION();

    bool emptyAfterDecay = false;
    {
        L2_GUARD(*m_DesireLock);
        if (c.empty()) {
            return 0;
        }

        for (_It current = c.begin(); current != c.end();) {
            float decay = m_pNPC->m_AIParam.GetDesireDecayValue(current->type);
            if (g_nUseChasePenalty != 0 && current->type == DESIRE_ATTACK && current->levelDifference > 10) {
                CCreature* const target = CCreature::GetObjectW(static_cast<std::uint32_t>(current->targetId));
                if (target &&
                    runtime::Int32FromBits(
                        runtime::GetTickCount() - static_cast<std::uint32_t>(target->m_data->m_nLastAttackTick)
                    ) > 3500) {
                    decay *= 30.0F;
                }
            }

            current->value -= decay;
            if (current->value > 0.0F) {
                ++current;
                continue;
            }

            DeleteReverseHate(m_pNPC, *current);
            current = c.erase(current);
        }

        if (c.size() > 1) {
            RebuildHeapTargetOrder();
        }
        emptyAfterDecay = c.empty();
    }

    return emptyAfterDecay ? SendNoDesireEvent() : 0;
}

void CDesirePQ::Report(std::u16string& output, PhysicalState physicalState) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (m_bSetAbsoluteDesire) {
        output += u"Absolute Desire : \r\n";
        m_AbsoluteDesire.Report(output);
    }

    AppendNormalDesireHeader(output, static_cast<int>(c.size()));
    int reported = 0;
    for (const CDesire& desire : c) {
        if (physicalState == DESIRE_NONE || desire.type == static_cast<int>(physicalState)) {
            desire.Report(output);
            ++reported;
            if (reported > 29) {
                break;
            }
        }
    }
}

int CDesirePQ::GetQueueSize() const {
    L2_GUARD_FUNCTION();

    return static_cast<int>(c.size());
}

int CDesirePQ::SetActive(int active) {
    L2_GUARD_FUNCTION();

    const int previous = m_bActive;
    m_bActive = active;
    return previous;
}

int CDesirePQ::SendNoDesireEvent() {
    L2_GUARD_FUNCTION();

    if (!m_pNPC) {
        return 0;
    }

    CNPCEvent* const event = CNPCEventFactory::CreateNoDesireEvent(m_pNPC);
    if (!event) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Event failed",
        //     __WFILE__,
        //     0x34)
        // Log.Add(LOG_ERROR,
        //     "[NPC actions] Cannot create the no-action event for NPC %u. AI callback not delivered.",
        //     static_cast<unsigned int>(m_pNPC->Id()))
        L2_LOG_ERROR(
            Log, "[NPC actions] Cannot create the no-action event for NPC %u; AI callback not delivered.",
            static_cast<unsigned int>(m_pNPC->Id())
        );
        return 0;
    }

    const int handled = m_pNPC->HandleEvent(event);
    L2_RELEASE(event, ObjectRefType::CreateDelete);
    return handled != 0;
}

bool CDesirePQ::ResetAbsoluteDesire() {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    const bool wasSet = m_bSetAbsoluteDesire;
    if (m_bSetAbsoluteDesire) {
        m_bSetAbsoluteDesire = false;
    }
    return wasSet;
}

bool CDesirePQ::RemoveTopDesire(int type) {
    L2_GUARD_FUNCTION();

    L2_GUARD(*m_DesireLock);
    if (m_AbsoluteDesire.type == type) {
        m_bSetAbsoluteDesire = false;
    }

    for (_It current = c.begin(); current != c.end(); ++current) {
        if (current->type != type) {
            continue;
        }
        DeleteReverseHate(m_pNPC, *current);
        if (current->IsInterruptable()) {
            c.erase(current);
        }
        return true;
    }
    return false;
}
