#include "ParamProperty.h"

#include "Desire.h"
#include "L2Core/src/inc/Guard.h"
#include "NPCServer.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/ThreadManager.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <string_view>


RWLock CTelPosListCacheManager::m_lock;
std::map<TelPosList*, bool> CTelPosListCacheManager::m_mapCached;

RWLock CBuySellListCacheManager::m_lock;
std::map<CBuySellListCacheManager::map_key, bool> CBuySellListCacheManager::m_mapCached;

CAIParameter::CAIParameter() : m_pSuper(nullptr), DesireDecayCache{}, DesireBoostCache{} {}

CAIProperty::CAIProperty() : m_pSuper(nullptr) {}

AIConstantData CAIParameter::GetData(const char16_t* varName) {
    // Current L2NPC.exe 0x005084e4.
    L2_GUARD_FUNCTION();
    if (!varName) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] var name is nul",
        //     L"ParamProperty.cpp",
        //     0xbd)
        // Log.Add(LOG_ERROR,
        //     "[AI parameter] Cannot look up a parameter without a name, returning the default value.")
        L2_LOG_ERROR(Log, "[AI parameter] Cannot look up a parameter without a name; returning the default value.");
        ReportCallStack(LOG_ERROR, false);
        return {};
    }

    const std::u16string key{std::u16string_view(varName)};
    const auto found = m_mapData.find(key);
    if (found != m_mapData.end()) {
        return found->second;
    }
    return m_pSuper ? m_pSuper->GetData(varName) : AIConstantData{};
}

AIConstantData CAIProperty::GetData(char16_t* varName) {
    // Current L2NPC.exe 0x00508804.
    L2_GUARD_FUNCTION();
    const std::u16string key{std::u16string_view(varName)};
    const auto found = m_mapData.find(key);
    if (found != m_mapData.end()) {
        return found->second;
    }
    return m_pSuper ? m_pSuper->GetData(varName) : AIConstantData{};
}

int CAIParameter::GetSize() { return static_cast<int>(m_mapData.size()); }

void CAIParameter::Copy(const CAIParameter& other) {
    m_pSuper = other.m_pSuper;
    for (const auto& entry : other.m_mapData) {
        m_mapData.insert(entry);
    }
    for (auto& entry : DesireDecayCache) {
        entry = {};
    }
    for (auto& entry : DesireBoostCache) {
        entry = {};
    }
}

bool CTelPosListCacheManager::IsCached(TelPosList* list) {
    // Current L2NPC.exe 0x005080c8.
    L2_GUARD_FUNCTION();
    bool cached;
    {
        L2_READ_GUARD(m_lock);
        const auto found = m_mapCached.find(list);
        cached = found != m_mapCached.end() && found->second;
    }
    return cached;
}

bool CTelPosListCacheManager::Cache(TelPosList* list, int npcId, int userId) {
    // Current L2NPC.exe 0x00508cb0.
    L2_GUARD_FUNCTION();
    if (!list) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Undefined telPosList[%I64d]",
        //     L"ParamProperty.cpp",
        //     0x48,
        //     0LL)
        // Log.Add(LOG_ERROR,
        //     "[NPC teleport] Cannot cache a missing destination list for NPC %d, viewer %d. Nothing sent.",
        //     npcId, userId)
        L2_LOG_ERROR(
            Log, "[NPC teleport] Cannot cache a missing destination list for NPC %d, viewer %d; nothing sent.", npcId, userId
        );
        return false;
    }

    // Current L2NPC.exe keeps this stack buffer uninitialized. Only the
    // assembled prefix is sent, and assembly failure suppresses the send.
    std::array<char, 0x5000> packet;
    int packetLength = 0;
    bool failed = false;
    std::size_t failedPositions = 0;
    std::size_t failedResidences = 0;

    {
        L2_WRITE_GUARD(m_lock);
        bool& cached = m_mapCached[list];
        if (!cached) {
            for (const TelPos& position : *list) {
                const int residenceCount = static_cast<int>(position.vResidenceIdList.size());
                const int assembled = packet_helper_detail::AssemblePacket(
                    packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "dSddddd",
                    position.nameIndex, position.pszName, position.x, position.y, position.z, position.price, residenceCount
                );
                if (assembled < 1) {
                    // Noise: Encoding failures are summarized once for this list after traversal. Repeated per-entry
                    // messages do not add a distinct cause. Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] caching teleport "
                    //     L"list failed. too many pos.",
                    //     L"ParamProperty.cpp",
                    //     0x7f)
                    ++failedPositions;
                    failed = true;
                    continue;
                }

                packetLength += assembled;
                for (int residenceId : position.vResidenceIdList) {
                    const int residenceBytes = packet_helper_detail::AssemblePacket(
                        packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "d", residenceId
                    );
                    if (residenceBytes < 1) {
                        // Noise: Encoding failures are summarized once for this list after traversal. Repeated per-entry
                        // messages do not add a distinct cause. Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] caching "
                        //     L"teleport2 list failed. "
                        //     L"too many pos.",
                        //     L"ParamProperty.cpp",
                        //     0x78)
                        ++failedResidences;
                        failed = true;
                    } else {
                        packetLength += residenceBytes;
                    }
                }
            }
            m_mapCached[list] = true;
        } else {
            failed = true;
        }
    }

    if (failed) {
        if (failedPositions != 0 || failedResidences != 0) {
            // Log.Add(LOG_ERROR,
            //     "[NPC teleport] Cannot encode destination list %p for NPC %d, viewer %d: %zu position records and %zu
            //     residence IDs failed. %d of %zu bytes used. No update sent. The list was marked cached locally.",
            //     static_cast<void*>(list), npcId, userId, failedPositions, failedResidences, packetLength, packet.size())
            L2_LOG_ERROR(
                Log,
                "[NPC teleport] Cannot encode destination list %p for NPC %d, viewer %d: %zu position records and %zu "
                "residence IDs failed; %d of %zu bytes used. No update sent; the list was marked cached locally.",
                static_cast<void*>(list), npcId, userId, failedPositions, failedResidences, packetLength, packet.size()
            );
        }
        return false;
    }

    g_ns.Send(
        "cQdbdd", 0x07, static_cast<long long>(reinterpret_cast<std::uintptr_t>(list)), static_cast<int>(list->size()),
        packetLength, packet.data(), npcId, userId
    );
    if (npcId != 0 || userId != 0) {
        return true;
    }

    // Noise: TeleportFStr and NoblessTeleport pre-cache with zero NPC/viewer IDs. The list update was already
    // sent, retain the original false return without a false failure message. Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] caching teleport list "
    //     L"failed. NPCID & USERID is zero.",
    //     L"ParamProperty.cpp",
    //     0x92)
    return false;
}

bool CBuySellListCacheManager::IsCached(map_key key) {
    // Current L2NPC.exe 0x005081d0.
    L2_GUARD_FUNCTION();
    bool cached;
    {
        L2_READ_GUARD(m_lock);
        const auto found = m_mapCached.find(key);
        cached = found != m_mapCached.end() && found->second;
    }
    return cached;
}

bool CBuySellListCacheManager::Cache(map_key key, CPacketHelperEx<20480>& packet) {
    // Current L2NPC.exe 0x00508938.
    L2_GUARD_FUNCTION();
    if (!key.m_SecondaryKey) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] buysell list is null",
        //     L"ParamProperty.cpp",
        //     0x189)
        // Log.Add(LOG_ERROR,
        //     "[NPC shop] Cannot encode a buy/sell list: list pointer is null, returning failure.")
        L2_LOG_ERROR(Log, "[NPC shop] Cannot encode a buy/sell list: list pointer is null; returning failure.");
        return false;
    }

    {
        L2_WRITE_GUARD(m_lock);
        // The current executable has a function-static iterator named `it`.
        // It is initialized from the first invocation's key and reused.
        static auto found = m_mapCached.find(key);
        if (found != m_mapCached.end() && found->second) {
            return false;
        }

        BuySellList* const list = found != m_mapCached.end() ? found->first.m_SecondaryKey : key.m_SecondaryKey;
        if (list->empty()) {
            return false;
        }

        // The two branches use different members. Current
        // machine code loads [key+0] for an existing false entry and [key+4]
        // for a new entry (0x00508a90 and 0x00508b4a respectively).
        const int cacheScope = found != m_mapCached.end() ? key.m_PrimaryKey.m_PrimaryKey : key.m_PrimaryKey.m_SecondaryKey;
        packet.Assemble(
            "dQd", cacheScope, static_cast<long long>(reinterpret_cast<std::uintptr_t>(list)), static_cast<int>(list->size())
        );
        for (const BuySell& entry : *list) {
            packet.Assemble(
                "dQfQ", entry.id, static_cast<long long>(entry.price), static_cast<double>(entry.rate),
                static_cast<long long>(entry.count)
            );
        }

        if (found == m_mapCached.end()) {
            m_mapCached.emplace(key, true);
        }
    }
    return true;
}

float CAIParameter::GetDesireDecayValue(PhysicalState state) {
    // Current L2NPC.exe 0x00508628.
    L2_GUARD_FUNCTION();
    static constexpr const char16_t* kNames[] = {
        u"IdleDesire_DecayRatio",
        u"MoveAround_DecayRatio",
        u"DoNothing_DecayRatio",
        u"Attack_DecayRatio",
        u"Chase_DecayRatio",
        u"Flee_DecayRatio",
        u"GetItem_DecayRatio",
        u"Follow_DecayRatio",
        u"Decaying_DecayRatio",
        u"MoveToWayPoint_DecayRatio",
        u"UseSkill_DecayRatio",
        u"MoveTo_DecayRatio",
        u"EffectAction_DecayRatio",
        u"EffectAction2_DecayRatio",
        u"PetDefaultAction_DecayRatio",
        u"MoveToTarget_DecayRatio",
        u"MoveSuperPoint_DecayRatio",
        u"MoveFreeway_DecayRatio",
        u"MoveFormation_DecayRatio",
    };
    const int index = static_cast<int>(state);

    auto& cached = DesireDecayCache[index];
    if (!cached.Cached) {
        float value = 0.0F;
        if (index < static_cast<int>(std::size(kNames))) {
            cached.Cached = true;
            const AIConstantData data = GetData(kNames[index]);
            if (data.type) {
                value = data.data.floatData;
            }
            cached.Value = value;
        }
        return value;
    }
    return cached.Value;
}

float CAIParameter::GetDesireBoostValue(PhysicalState state) {
    // Current L2NPC.exe 0x00508710.
    L2_GUARD_FUNCTION();
    static constexpr const char16_t* kNames[] = {
        u"IdleDesire_BoostValue",
        u"MoveAround_BoostValue",
        u"DoNothing_BoostValue",
        u"Attack_BoostValue",
        u"Chase_BoostValue",
        u"Flee_BoostValue",
        u"GetItem_BoostValue",
        u"Follow_BoostValue",
        u"Decaying_BoostValue",
        u"MoveToWayPoint_BoostValue",
        u"UseSkill_BoostValue",
        u"MoveTo_BoostValue",
        u"EffectAction_BoostValue",
        u"EffectAction2_BoostValue",
        u"PetDefaultAction_BoostValue",
        u"MoveToTarget_BoostValue",
        u"MoveSuperPoint_BoostValue",
        u"MoveFreeway_BoostValue",
        u"MoveFormation_BoostValue",
    };
    const int index = static_cast<int>(state);

    auto& cached = DesireBoostCache[index];
    if (!cached.Cached) {
        float value = 0.0F;
        if (index < static_cast<int>(std::size(kNames))) {
            cached.Cached = true;
            const AIConstantData data = GetData(kNames[index]);
            if (data.type) {
                value = data.data.floatData;
            }
            cached.Value = value;
        }
        return value;
    }
    return cached.Value;
}
