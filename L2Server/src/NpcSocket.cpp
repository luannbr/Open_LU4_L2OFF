#include "NpcSocket.h"
#include "Agit.h"
#include "AgitHtml.h"
#include "AirShip.h"
#include "Area.h"
#include "AtomicJobServerSide.h"
#include "AuctionAgit.h"
#include "BlockUpset.h"
#include "Boss.h"
#include "BuySell.h"
#include "BuySellListManager.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "Castle.h"
#include "Cleft.h"
#include "CommissionTrade.h"
#include "ControlTower.h"
#include "Creature.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "Dominion.h"
#include "Door.h"
#include "DynamicQuest.h"
#include "EventController.h"
#include "EventMaker.h"
#include "EventMatch.h"
#include "FieldCycle.h"
#include "Fortress.h"
#include "GameEventSystem.h"
#include "HTMLCache.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "L2Time.h"
#include "MakerEvent.h"
#include "Manor.h"
#include "MiniGameAgit.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "NewAction.h"
#include "NpcServer.h"
#include "NpcSocketEx.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "PVPMatch.h"
#include "Packet.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Portable/RuntimeTimingTrace.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Quest.h"
#include "QuestManager.h"
#include "Relay.h"
#include "ReservationNPCSpawnTimer.h"
#include "Residence.h"
#include "RestrictArea.h"
#include "SerializedMessage.h"
#include "Server.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/ProcessServices.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "SiegeAgit.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "SkillEnchant.h"
#include "TeamBattleAgit.h"
#include "TeleportManager.h"
#include "ThreadLocalWorldMap.h"
#include "Trap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/socket.h>
#endif

namespace {

int g_nNpcRemovalProgress = 0;

void ReleaseNamedGate(void*& handle) noexcept {
#if defined(_WIN32)
    if (handle != nullptr) {
        ::ReleaseMutex(static_cast<HANDLE>(handle));
    }
#endif
    ::runtime::CloseNamedInstanceGate(handle);
}


User* FindUserBySid(std::int32_t sid) {
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sid));
    return creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
}

CNPC* FindNpcBySid(std::int32_t sid) {
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sid));
    return creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
}

CCreature* FindCreatureBySid(std::int32_t sid) {
    return g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sid));
}

CCreature* FindCreatureByIndex(std::int32_t index) {
    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (index < 0 || index >= creatures.GetSize()) {
        return nullptr;
    }
    return g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatures.GetIdByIndex(index)));
}


std::int32_t NpcSocketRandomOneTo(const std::int32_t maximum) {
    if (maximum < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nHigh [%d] in Random",
        //     maximum)
        L2_LOG_ERROR(Log, "[NPC random] Upper bound %d must be positive. Returning fallback value 1.", maximum);
        return 1;
    }
    return runtime::Win64CvttDoubleToInt32(::genrand64_real2() * static_cast<double>(maximum) + 1.0);
}

struct NpcInstantTeleportTerritoryContext {
    std::int32_t targetX;
    std::int32_t targetY;
    std::int32_t targetZ;
    std::int32_t targetHeading;
    std::int32_t condition;
    std::int32_t flags;
    std::int32_t bottom;
    std::int32_t top;
    std::vector<CTerritory*> territories;
    std::vector<CTerritory*> bannedTerritories;
};


} // namespace

bool IsInTerritory(
    std::vector<CTerritory*> territories, std::vector<CTerritory*> bannedTerritories, const FVector position
) {
    // Current L2Server 0x008b1d94.
    L2_GUARD_FUNCTION();
    for (CTerritory* const territory : bannedTerritories) {
        if (territory->IsInside(position, true)) {
            return false;
        }
    }

    if (territories.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     " GetGerritorySize()<=0 at file[%s], line[%d]",
        //     kNpcSocketSource,
        //     0xe1c)
        // Noise: replace shipping coordinates with the failed territory check.
        L2_LOG_ERROR(
            Log, "[Territory] Position (%.17g, %.17g, %.17g) rejected: the allowed territory list is empty.", position.x,
            position.y, position.z
        );
        return false;
    }
    for (CTerritory* const territory : territories) {
        if (territory->IsInside(position, true)) {
            return true;
        }
    }
    return false;
}

bool DisassembleTerritoryInfo(
    const unsigned char* packet, std::vector<CTerritory*>& territories, std::vector<CTerritory*>& bannedTerritories
) {
    // Current L2Server 0x008b80bc.
    L2_GUARD_FUNCTION();
    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    bannedTerritories.clear();
    for (std::int32_t index = 0; index < count; ++index) {
        bannedTerritories.push_back(CTerritory::DisassembleTerritoryInfo(&packet));
    }

    count = 0;
    packet = ::Disassemble(packet, "d", &count);
    territories.clear();
    for (std::int32_t index = 0; index < count; ++index) {
        territories.push_back(CTerritory::DisassembleTerritoryInfo(&packet));
    }
    return true;
}

namespace {

// This adapter borrows the source on Windows. On other hosts the caller owns
// storage for the duration of the native-wide call. Preserve nullptr distinctly.
const wchar_t* TargetUtf16ToHostWide(const char16_t* value, std::wstring& storage) {
    if (!value) {
        return nullptr;
    }
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(value);
#else
    storage = ::Utf16ToWide(std::u16string_view(value));
    return storage.c_str();
#endif
}

char16_t* DuplicateTargetString(const char16_t* value) {
    const std::size_t length = std::char_traits<char16_t>::length(value);
    auto* const copy = static_cast<char16_t*>(std::malloc((length + 1U) * sizeof(char16_t)));
    if (copy != nullptr) {
        std::memcpy(copy, value, (length + 1U) * sizeof(char16_t));
    }
    return copy;
}

template <std::size_t Capacity, typename... Arguments>
int FormatTargetText(
    std::array<char16_t, Capacity>& destination, const std::u16string_view format, const Arguments&... arguments
) {
    return FormatUtf16To(destination.data(), destination.size(), format, arguments...);
}

template <std::size_t Capacity>
void AppendTargetText(std::array<char16_t, Capacity>& destination, const char16_t* source) {
    const std::size_t destinationLength = std::char_traits<char16_t>::length(destination.data());
    const std::size_t sourceLength = std::char_traits<char16_t>::length(source);
    if (sourceLength >= Capacity - destinationLength) {
        destination[0] = u'\0';
        return;
    }
    std::char_traits<char16_t>::copy(destination.data() + destinationLength, source, sourceLength + 1);
}

} // namespace

// Current CRT slots 6591/6592 keep the physical NpcSocket.cpp order
// g_UserNotifier, then g_EnterWorldSerializer. Keep each definition beside
// its original method family so construction and reverse teardown keep it.
CUserNotifier g_UserNotifier;

CUserNotifier::CUserNotifier() : i(0) {}

CUserNotifier::~CUserNotifier() = default;

void CUserNotifier::TimerExpired(int) {
    // Current L2Server 0x008b1494.
    L2_GUARD_FUNCTION();
    const bool morePending = [&] {
        L2_GUARD(m_lock);

        int sent = 0;
        while (static_cast<std::uint64_t>(m_user.size()) > static_cast<std::uint64_t>(static_cast<std::int64_t>(i))) {
            g_npcserver.Send("cddd", 0x30, m_user[static_cast<std::size_t>(i)], 0, 1);
            ++i;
            ++sent;
            if (sent >= 0x32) {
                break;
            }
        }

        return static_cast<std::uint64_t>(static_cast<std::int64_t>(i)) < static_cast<std::uint64_t>(m_user.size());
    }();
    if (morePending) {
        AddTimer(200, 0);
    }
}

void CUserNotifier::Notify() {
    // Current L2Server 0x008b86e8.
    {
        L2_GUARD(m_lock);
        m_user.clear();
        i = 0;

        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, { m_user.push_back(user->Id()); });
    }
    AddTimer(100, 0);
}

CL2ServerProtector& CL2ServerProtector::inst() {
    static CL2ServerProtector protector;
    return protector;
}

void CL2ServerProtector::BeginProtection() {
    // Current L2Server 0x007f543c.
    if (::runtime::raw_atomic::AddI32(&m_nProtectionCount, 1) != 1) {
        return;
    }

    std::string diagnostic;
    const runtime::InstanceGateResult result = ::runtime::AcquireNamedInstanceGate(
        false, u"L2Mutex_Protector_090720", "l2npc_process_L2Mutex_Protector_090720.lock", m_hMutex, diagnostic
    );
    if (result == runtime::InstanceGateResult::Error) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2Mutex_Protector_090720 error: %d\n",
        //     NativeProcessError())
        // Noise: use the gate's operation-specific diagnostic instead of a later ambient error value.
        L2_LOG_ERROR(
            Log, "[NPC protection] Could not acquire L2Mutex_Protector_090720: %s. Processing continues.", diagnostic.c_str()
        );
    }
    if (result == runtime::InstanceGateResult::AlreadyRunning) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2Mutex_Protector_090720 already exists")
        L2_LOG_ERROR(
            Log,
            "[NPC protection] L2Mutex_Protector_090720 is already in use. "
            "This request did not acquire the protection gate; processing continues."
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        // report one protection transition, and do not claim
        // gate acquisition succeeded after the error reported above.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] L2Server is under protection mode!!!")
        if (result != runtime::InstanceGateResult::Error) {
            L2_LOG_INFO(
                Log,
                "[NPC protection] L2Server entered protection mode; the protection gate is held until pending protection "
                "requests finish."
            );
        }
    }
}

void CL2ServerProtector::EndProtection() {
    // Current L2Server 0x007f54f8.
    if (::runtime::raw_atomic::AddI32(&m_nProtectionCount, -1) != 0) {
        return;
    }

    ReleaseNamedGate(m_hMutex);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    // one informational record for the completed transition.
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] L2Server is released from protection mode!!!")
    L2_LOG_INFO(Log, "[NPC protection] L2Server left protection mode; no protection requests remain.");
}

CProtectionRequest::CProtectionRequest() { CL2ServerProtector::inst().BeginProtection(); }

CProtectionRequest::~CProtectionRequest() { CL2ServerProtector::inst().EndProtection(); }

CEnterWorldSerializer g_EnterWorldSerializer;

CEnterWorldSerializer::CEnterWorldSerializer() :
    m_Started(0), m_MakerFinished(0), m_InitSpawnCount(0) {}

CEnterWorldSerializer::~CEnterWorldSerializer() = default;

int CEnterWorldSerializer::GetModifiedHeight(int x, int y, int z) {
    // Current L2Server 0x0089a360.
    L2_GUARD_FUNCTION();
    const int ground = CWorld::Inst().GetGeo()->FindGround(x, y, z + 100);
    if (ground != 0 && ground < z + 100 && z - 100 < ground) {
        return ground;
    }
    return z;
}

void CEnterWorldSerializer::Push(
    int index, int x, int y, int z, int dir, int setDefaultParam, int hp, int mp, unsigned int inzoneSid
) {
    // Current L2Server 0x008b6dd4.
    L2_GUARD_FUNCTION();
    if (::runtime::raw_atomic::CompareExchangeI32(&m_Started, 1, 0) == 0) {
        AddTimer(100, 0);
    }

    {
        L2_GUARD(m_Sect);
        m_queue.push_back(NpcEnterWorldData{index, x, y, z, dir, setDefaultParam, hp, mp, inzoneSid});
    }
    ::runtime::raw_atomic::AddI32(&NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize, 1);
}

void CEnterWorldSerializer::ClearDataQueue() {
    // Current L2Server 0x008afcc4. The shrinking-size loop is intentional:
    // the current machine clears only while the running index remains below
    // the queue's new size.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Sect);
        int cleared = 0;
        while (static_cast<std::size_t>(cleared) < m_queue.size()) {
            m_queue.pop_front();
            ::runtime::raw_atomic::AddI32(&NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize, -1);
            ++cleared;
        }
    }
}

void CEnterWorldSerializer::TimerExpired(int) {
    // Current L2Server 0x008b3d10.
    L2_GUARD_FUNCTION();
    const int limit = g_nMaxSpawnNumPerOneTick;
    std::vector<NpcEnterWorldData> pending;
    pending.reserve(static_cast<std::size_t>(limit));

    {
        L2_GUARD(m_Sect);
        for (int count = 0; !m_queue.empty() && count < limit; ++count) {
            pending.push_back(m_queue.front());
            m_queue.pop_front();
            ::runtime::raw_atomic::AddI32(&NpcSpawnInfo::Inst()->Server_EnterWorldQueueSize, -1);
        }
    }

    for (const NpcEnterWorldData& data : pending) {
        CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(data.index);
        if (!creature || !creature->IsNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "npc null at file[%s], line[%d]",
            //     kNpcSocketSource,
            //     0x26e)
            // Noise: omit shipping coordinates. Report the queue entry and actual rejection.
            // L2_LOG_ERROR(Log, "npc null at file[%s], line[%d]", kNpcSocketSource, 0x26e)
            L2_LOG_ERROR(
                Log, "[NPC spawn] Skipping queued entry %d: creature is missing or is not an NPC.",
                static_cast<int>(data.index)
            );
            continue;
        }

        CNPC* const npc = creature->CastNPC();
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] [CNPC* pNPC = spNPC->CastNPC().] is nullptr",
            //     L"NpcSocket.cpp",
            //     0x277)
            // Noise: omit shipping coordinates. Retain the failed conversion.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] [CNPC* pNPC = spNPC->CastNPC(),] is nullptr", L"NpcSocket.cpp", 0x277)
            L2_LOG_ERROR(Log, "[NPC spawn] Skipping queued entry %d: CastNPC returned null.", static_cast<int>(data.index));
            continue;
        }
        if (!npc->IsNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CastNPC Fail",
            //     L"NpcSocket.cpp",
            //     0x27d)
            // Noise: omit shipping coordinates. State the failed NPC type check.
            // L2_LOG_ERROR(Log, L"[%s][%d] CastNPC Fail", L"NpcSocket.cpp", 0x27d)
            L2_LOG_ERROR(
                Log, "[NPC spawn] Skipping queued entry %d: object returned by CastNPC failed IsNPC.",
                static_cast<int>(data.index)
            );
            continue;
        }

        int x = data.x;
        int y = data.y;
        int z = data.z;
        const int originalZ = data.z;
        if (!CWorld::Inst().IsValidPosition(x, y)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] cannot spawn at (%d, %d) . Invalid position at file[%s], line[%d]",
            //     npc->GetName(),
            //     x,
            //     y,
            //     L"NpcSocket.cpp",
            //     0x284)
            // Noise: replace shipping coordinates with the rejected spawn entry.
            // L2_LOG_ERROR(Log,
            //     "[NPC spawn] Skipping queued entry %d for '%ls': position (%d, %d) is outside valid world bounds.",
            //     static_cast<int>(data.index), npc->GetName(), x, y)
            L2_LOG_ERROR(
                Log, u"[NPC spawn] Skipping queued entry %d for '%ls': position (%d, %d) is outside valid world bounds.",
                static_cast<int>(data.index), npc->GetName(), x, y
            );
            continue;
        }

        CAirShip* airShip = nullptr;
        if (static_cast<int>(npc->m_data->m_moveType) != CMT_FLY) {
            if (npc->m_nAirShipServerID == 0) {
                z = GetModifiedHeight(x, y, z);
            } else {
                airShip = CSmartID(npc->m_nAirShipServerID).GetAirShip();
                if (airShip == nullptr) {
                    z = GetModifiedHeight(x, y, z);
                } else {
                    const FVector relativePosition{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
                    npc->SetRelPos(relativePosition);
                    const FVector absolutePosition = airShip->ConvRelToAbs(relativePosition);
                    x = runtime::Win64CvttDoubleToInt32(absolutePosition.x);
                    y = runtime::Win64CvttDoubleToInt32(absolutePosition.y);
                    z = runtime::Win64CvttDoubleToInt32(absolutePosition.z);
                }
            }
        }

        const FVector position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
        if (static_cast<int>(npc->m_data->m_nClass) == 0x56 &&
            !CTreeLoc::UseTree(static_cast<int>(npc->Id()), position, static_cast<int>(npc->GetInZoneID()))) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"cannot spawn xmas tree . Near xmas tree exist[%s], line[%d]",
            //     L"NpcSocket.cpp",
            //     0x2c6)
            // Noise: omit shipping coordinates. Include the rejected tree location.
            // L2_LOG_ERROR(Log,
            //     L"cannot spawn xmas tree , near xmas tree exist[%s], line[%d]", L"NpcSocket.cpp", 0x2c6)
            L2_LOG_ERROR(
                Log,
                "[NPC spawn] Christmas tree placement rejected for NPC %u at (%d, %d, %d), instance %u. Skipping this spawn "
                "entry.",
                npc->Id(), x, y, z, npc->GetInZoneID()
            );
            continue;
        }

        const std::int32_t zDifference =
            static_cast<std::int32_t>(static_cast<std::uint32_t>(z) - static_cast<std::uint32_t>(originalZ));
        const int absoluteDifference = runtime::AbsoluteInt32Wrapping(zDifference);
        if (absoluteDifference > 299) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] z diff[%d] npc id[%d] orig z[%d], current z[%d]",
            //     L"NpcSocket.cpp",
            //     0x2cf,
            //     absoluteDifference,
            //     npc->GetClassId(),
            //     originalZ,
            //     z)
            // Noise: omit shipping coordinates. Distinguish NPC class from object ID.
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] z diff[%d] npc id[%d] orig z[%d], current z[%d]",
            //     L"NpcSocket.cpp", 0x2cf, absoluteDifference, npc->GetClassId(), originalZ, z)
            L2_LOG_ERROR(
                Log,
                "[NPC spawn] Height changed by %d for NPC class %d (queue index %d): original Z=%d, adjusted Z=%d. "
                "Continuing with the adjusted position.",
                absoluteDifference, npc->GetClassId(), static_cast<int>(data.index), originalZ, z
            );
        }

        npc->m_data->SetPos(position);
        npc->m_data->SetDirByInt(data.dir);
        if (!npc->IsBoss() && !npc->IsPet()) {
            npc->DeleteAllItemsInInventory();
        }
        if (npc->GetMaster() == nullptr) {
            npc->m_data->m_SummonType = NST_NOTHING;
        }

        int setDefaultParam = data.nSetDefaultParam;
        int hp = data.nHP;
        int mp = data.nMP;
        if (npc->IsSummonOrPet()) {
            CSummon* const summon = npc->CastSummon();
            if (!summon->IsNewSummon()) {
                setDefaultParam = 1;
                hp = runtime::Win64CvttDoubleToInt32(npc->m_data->m_dHP + 0.5);
                mp = runtime::Win64CvttDoubleToInt32(npc->m_data->m_dMP + 0.5);
            }
        }

        npc->m_data->m_bDecayComplete = false;
        npc->EnterWorld(setDefaultParam != 0, hp, mp, data.inzoneSID);
        if (airShip != nullptr) {
            airShip->LoadCannon(npc);
        }
        if (npc->IsMerchant()) {
            CLogFormatter formatter;
            formatter.WriteLog1_1(0x81, npc, nullptr, nullptr, nullptr, nullptr, npc->GetClassId(), 0, 0, 0, 0, 0, 0);
        }
    }

    AddTimer(200, 0);
}

CNpcRemover::CNpcRemover() {
    // Current L2Server 0x008b9b88.
    m_nStartTick = runtime::GetTickCount();
    i = 0;
    ClearList.clear();

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_NPCMap, npc, {
        if (!npc->IsDeleting() && npc->m_nNpcDeleteCount == 0) {
            L2_ADD_REF(npc, ObjectRefType::EventRetain);
            ClearList.push_back(npc);
        }
    });
    m_nTotalCount = static_cast<int>(ClearList.size());

    for (int position = 0; position < m_nTotalCount; ++position) {
        int selected = 1;
        if (m_nTotalCount < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"nHigh [%d] in Random",
            //     m_nTotalCount)
            L2_LOG_ERROR(
                Log, "[NPC cleanup] Invalid shuffle upper bound %d. Continuing with selected value 1.", m_nTotalCount
            );
        } else {
            selected = runtime::Win64CvttDoubleToInt32(::genrand64_real1() * static_cast<double>(m_nTotalCount) + 1.0);
        }

        if (ClearList[static_cast<std::size_t>(position)] == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"npc null at file[%S], line[%d]",
            //     kNpcSocketSource,
            //     0x2f4e)
            // Noise: replace shipping coordinates with the affected removal-list position.
            L2_LOG_ERROR(
                Log, "[NPC cleanup] Null entry at position %d before shuffling the removal list. Shuffling continues.",
                position
            );
        }
        const std::size_t selectedIndex = static_cast<std::size_t>(selected - 1);
        if (ClearList[selectedIndex] == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"npc null at file[%S], line[%d]",
            //     kNpcSocketSource,
            //     0x2f54)
            // Noise: replace shipping coordinates with the affected removal-list position.
            L2_LOG_ERROR(
                Log, "[NPC cleanup] Null entry at selected index %zu while shuffling the removal list. Shuffling continues.",
                selectedIndex
            );
        }
        std::swap(ClearList[static_cast<std::size_t>(position)], ClearList[selectedIndex]);
    }

    for (std::size_t position = 0; position < ClearList.size(); ++position) {
        if (ClearList[position] == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"npc null at file[%S], line[%d]",
            //     kNpcSocketSource,
            //     0x2f60)
            // Noise: replace shipping coordinates with the affected removal-list position.
            L2_LOG_ERROR(Log, "[NPC cleanup] Null entry remains at index %zu after shuffling the removal list.", position);
        }
    }
}

CNpcRemover::~CNpcRemover() = default;

void CNpcRemover::TimerExpired(int) {
    // Current L2Server 0x008b9f9c.
    L2_GUARD_FUNCTION();
    int processed = 0;
    for (;;) {
        if (static_cast<std::uint64_t>(ClearList.size()) <= static_cast<std::uint64_t>(static_cast<std::int64_t>(i))) {
            const int elapsedSeconds = static_cast<std::int32_t>(runtime::GetTickCount() - m_nStartTick) / 1000;
            // Log.Add(
            //     LOG_ERROR,
            //     L"NPC Removing DONE!!! Total Count[%d] Time[%ds]",
            //     m_nTotalCount,
            //     elapsedSeconds)
            // Noise: normal completion is informational. Report the list size, not a verified deletion count.
            L2_LOG_INFO(
                Log, "[NPC cleanup] Finished processing the removal list: %d entries in %d seconds.", m_nTotalCount,
                elapsedSeconds
            );
            // Log.Add(
            //     LOG_ERROR,
            //     L"NPC Removing DONE!!! Total Count[%d] Time[%ds]",
            //     m_nTotalCount,
            //     elapsedSeconds)
            // Noise: duplicate completion message. The summary above reports the same values.
            // Log.Add(
            //     LOG_ERROR,
            //     L"NPC Removing DONE!!! Total Count[%d] Time[%ds]",
            //     m_nTotalCount,
            //     elapsedSeconds)
            // Noise: duplicate completion message. The summary above reports the same values.
            L2_RELEASE(this, ObjectRefType::CreateDelete);
            return;
        }

        CNPC* const npc = ClearList[static_cast<std::size_t>(i)];
        if (!npc->IsRootNPC()) {
            npc->OnNpcSocketClose();
            L2_RELEASE(npc, ObjectRefType::EventRetain);
        }
        const int randomValue = std::rand();
        if (randomValue == (randomValue / 10) * 10) {
            ++g_nNpcRemovalProgress;
            // Log.Add(LOG_NORMAL, L"NPC Removing!!!  %d")
            // Noise: random progress samples add no reliable count and the original format lacks its argument.
            // L2_LOG_INFO(Log, L"NPC Removing!!!  %d")
        }

        ++processed;
        if (g_nMaxDespawnNumPerOneTick < processed) {
            ++i;
            AddTimer(100, 0);
            return;
        }
        ++i;
    }
}

bool NpcRegisterTelPosListPacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c05c0.
    L2_GUARD_FUNCTION();
    std::int64_t listId = 0;
    std::int32_t count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "Qd", &listId, &count);

    auto* const positions = new std::vector<TelPos>;
    {
        L2_GUARD(s_mapTelPosListSect);
        // Noise: Per-list lock entry does not report an operator event. List lookup failures remain visible.
        // Log.Add(
        //     LOG_NORMAL,
        //     "NpcRegisterTelPosListPacket() - "
        //     "s_mapTelPosListSect.Enter() %d",
        //     listId)
        s_mapTelPosList[listId] = positions;

        for (std::int32_t index = 0; index < count; ++index) {
            std::array<char16_t, 1000> name{};
            std::int32_t nameIndex = 0;
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::int32_t z = 0;
            std::int32_t price = 0;
            std::int32_t residenceCount = 0;
            cursor = ::Disassemble(cursor, "dSddddd", &nameIndex, 2000, name.data(), &x, &y, &z, &price, &residenceCount);

            TelPos position;
            position.m_nNameIndex = nameIndex;
            position.m_pszName = DuplicateTargetString(name.data());
            position.m_nX = x;
            position.m_nY = y;
            position.m_nZ = z;
            position.m_nPrice = price;
            CEvent* const baseEvent = CEventManager::inst().GetEvent(GAME_EVENT::ET_CORETIME_TELEPORT);
            if (baseEvent != nullptr) {
                position.m_nCoretimePrice = static_cast<CEvent_CoretimeTeleport*>(baseEvent)->ConvertToCoretimePrice(price);
            }
            for (std::int32_t residence = 0; residence < residenceCount; ++residence) {
                std::int32_t residenceId = 0;
                cursor = ::Disassemble(cursor, "d", &residenceId);
                position.m_vResidenceIdList.push_back(residenceId);
            }
            positions->push_back(position);
        }

        // Noise: Per-list lock exit adds routine chatter without confirming a useful result.
        // Log.Add(
        //     LOG_NORMAL,
        //     "NpcRegisterTelPosListPacket() - "
        //     "s_mapTelPosListSect.Leave()")
    }

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    ::Disassemble(cursor, "dd", &npcSid, &userSid);
    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    if (npc == nullptr || user == nullptr) {
        return false;
    }

    {
        L2_GUARD(s_mapTelPosListSect);
        const auto found = s_mapTelPosList.find(listId);
        if (found == s_mapTelPosList.end()) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "NpcShowTelposList() - s_mapTelPosList.find(%d) failed.")
            // Log.Add(LOG_WARN,
            //     "[NPC teleport] Cannot display destination list %lld for user %d: list not registered.",
            //     static_cast<long long>(listId), userSid)
            L2_LOG_WARN(
                Log, "[NPC teleport] Cannot display destination list %lld for user %d: list not registered.",
                static_cast<long long>(listId), userSid
            );
            return false;
        }
    }

    std::u16string html = u"<html><body><br><br>";
    std::array<char16_t, 1000> link{};
    std::int32_t index = 0;
    for (const TelPos& position : *s_mapTelPosList[listId]) {
        FormatUtf16To(
            link.data(), link.size(),
            u"<a action=\"bypass -h show_radar?id=%lld&index=%d\">"
            u"<fstring>%d</fstring></a><br1>",
            static_cast<long long>(listId), index++, position.m_nNameIndex
        );
        html.append(link.data());
    }
    html.append(u"<br></body></html>");
    user->ShowHTML(nullptr, html.c_str(), 0, HTML::LINK, true);
    return false;
}

bool NpcTeleportPacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bec04.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userIndex = 0;
    std::int64_t listId = 0;
    std::array<char16_t, 1024> title1{};
    std::array<char16_t, 1024> title2{};
    std::array<char16_t, 1024> title3{};
    std::array<char16_t, 1024> title4{};
    std::int32_t forceCommon = 0;
    std::int32_t requestValue = 0;
    std::int32_t formatKind = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddQSSSSddd", &npcSid, &userIndex, &listId, 0x800, title1.data(), 0x800, title2.data(), 0x800, title3.data(),
        0x800, title4.data(), &forceCommon, &requestValue, &formatKind
    );

    std::array<char16_t, 1024> priceText{};
    std::int32_t priceFStringId = 0;
    std::array<std::array<char16_t, 256>, 5> priceParameters{};
    if (formatKind == 0) {
        ::Disassemble(cursor, "S", 0x800, priceText.data());
    } else {
        ::Disassemble(
            cursor, "dSSSSS", &priceFStringId, 0x200, priceParameters[0].data(), 0x200, priceParameters[1].data(), 0x200,
            priceParameters[2].data(), 0x200, priceParameters[3].data(), 0x200, priceParameters[4].data()
        );
    }

    CSPointer<CCreature> userObject = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!userObject || !userObject->IsPC()) {
        // Log.Add(LOG_ERROR, L"Creature IsValid or not PC")
        L2_LOG_ERROR(
            Log,
            "[teleport] Ignoring list %lld from NPC %d: creature at user index %d "
            "is missing or is not a player. No HTML is sent.",
            static_cast<long long>(listId), npcSid, userIndex
        );
        return false;
    }
    User* const user = userObject->CastUser();

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* npc = nullptr;
    if (npcObject != nullptr && npcObject->IsNPC()) {
        npc = npcObject->CastNPC();
    }
    if (npc == nullptr || !npc->IsTeleporter()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPC is NULL or NPC is not Teleporter UserName[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "[teleport] Cannot display list %lld for user '%ls' (index %d): "
        //     "NPC %d is missing or is not a teleporter. No HTML is sent.",
        //     static_cast<long long>(listId), user->GetName(), userIndex, npcSid)
        L2_LOG_ERROR(
            Log,
            u"[teleport] Cannot display list %lld for user '%ls' (index %d): "
            u"NPC %d is missing or is not a teleporter. No HTML is sent.",
            static_cast<long long>(listId), user->GetName(), userIndex, npcSid
        );
        return false;
    }

    CTeleporter* const teleporter = npc->CastTeleporter();
    TeleporterType teleporterType = teleporter->CanTeleport(user);
    if (teleporterType == TELEPORTER_NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"TeleporterType NONE UserName[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "[teleport] Cannot display list %lld for user '%ls' (index %d) from NPC %d: "
        //     "CanTeleport returned TELEPORTER_NONE. No HTML is sent.",
        //     static_cast<long long>(listId), user->GetName(), userIndex, npcSid)
        L2_LOG_ERROR(
            Log,
            u"[teleport] Cannot display list %lld for user '%ls' (index %d) from NPC %d: "
            u"CanTeleport returned TELEPORTER_NONE. No HTML is sent.",
            static_cast<long long>(listId), user->GetName(), userIndex, npcSid
        );
        return false;
    }
    if (forceCommon == 1) {
        teleporterType = TELEPORTER_COMMON;
    }

    std::optional<std::array<char16_t, 0x2801>> htmlStorage;
    std::optional<std::list<TelPos>> orderedPositionsStorage;
    std::optional<std::list<std::int32_t>> orderedIndicesStorage;
    int htmlLength = 0;
    enum class TeleportListResult { Missing, TooLarge, Ready };
    const TeleportListResult listResult = [&] {
        L2_GUARD(s_mapTelPosListSect);
        const auto found = s_mapTelPosList.find(listId);
        if (found == s_mapTelPosList.end()) {
            return TeleportListResult::Missing;
        }

        std::vector<TelPos>* const positions = s_mapTelPosList[listId];
        auto& html = htmlStorage.emplace();
        htmlLength = FormatTargetText(html, u"<html><body>&$556;<br><br>");

        auto& orderedPositions = orderedPositionsStorage.emplace();
        auto& orderedIndices = orderedIndicesStorage.emplace();
        const int questListId =
            QuestManager::inst().FindFirstTelList(teleporter->GetClassId(), user->GetSelectedQuestZoneID());
        std::int32_t positionIndex = 0;
        for (const TelPos& position : *positions) {
            if (position.m_nNameIndex == questListId) {
                orderedPositions.push_front(position);
                orderedIndices.push_front(positionIndex);
            } else {
                orderedPositions.push_back(position);
                orderedIndices.push_back(positionIndex);
            }
            ++positionIndex;
        }

        for (const TelPos& position : orderedPositions) {
            const auto indexPosition = orderedIndices.begin();
            const std::int32_t originalIndex = *indexPosition;
            orderedIndices.erase(indexPosition);

            const char16_t* const icon = position.m_nNameIndex == questListId ? u"QUEST" : u"teleport";
            std::int32_t price = 0;
            if (teleporterType == TELEPORTER_COMMON) {
                price = position.m_nPrice;
            } else if (teleporterType == TELEPORTER_CORETIME) {
                price = position.m_nCoretimePrice;
            }

            std::array<char16_t, 1000> row{};
            int rowLength = 0;
            if (formatKind == 0) {
                const char16_t* const positionName = position.m_pszName;
                if (price < 1) {
                    rowLength = FormatTargetText(
                        row,
                        u"<button align=left icon=%s action=\"bypass -h "
                        u"teleport_%I64d_%d_%d_%d_%d\" "
                        u"msg=\"811;S;%s\">%s</button>",
                        icon, static_cast<long long>(listId), originalIndex, requestValue, npcSid,
                        static_cast<int>(teleporterType), positionName, positionName
                    );
                } else {
                    rowLength = FormatTargetText(
                        row,
                        u"<button align=left icon=%s action=\"bypass -h "
                        u"teleport_%I64d_%d_%d_%d_%d\" "
                        u"msg=\"811;S;%s\">%s - %d %s </button>",
                        icon, static_cast<long long>(listId), originalIndex, requestValue, npcSid,
                        static_cast<int>(teleporterType), positionName, positionName, price, priceText.data()
                    );
                }
            } else if (price < 1) {
                rowLength = FormatTargetText(
                    row,
                    u"<button align=left icon=%s action=\"bypass -h "
                    u"teleport_%I64d_%d_%d_%d_%d\" "
                    u"msg=\"811;F;%d\"><fstring>%d</fstring></button>",
                    icon, static_cast<long long>(listId), originalIndex, requestValue, npcSid,
                    static_cast<int>(teleporterType), position.m_nNameIndex, position.m_nNameIndex
                );
            } else {
                rowLength = FormatTargetText(
                    row,
                    u"<button align=left icon=%s action=\"bypass -h "
                    u"teleport_%I64d_%d_%d_%d_%d\" "
                    u"msg=\"811;F;%d\"><fstring>%d</fstring> - %d "
                    u"<fstring p1=\"%s\" p2=\"%s\" p3=\"%s\" "
                    u"p4=\"%s\" p5=\"%s\">%d</fstring> </button>",
                    icon, static_cast<long long>(listId), originalIndex, requestValue, npcSid,
                    static_cast<int>(teleporterType), position.m_nNameIndex, position.m_nNameIndex, price,
                    priceParameters[0].data(), priceParameters[1].data(), priceParameters[2].data(),
                    priceParameters[3].data(), priceParameters[4].data(), priceFStringId
                );
            }

            htmlLength += rowLength;
            if (htmlLength >= 0x2800) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Too large html for teleport list",
                //     L"NpcSocket.cpp",
                //     0x5f5)
                // Noise: shipping source coordinates are replaced by the logger's basename.
                L2_LOG_ERROR(
                    Log,
                    "[teleport] Cannot display list %lld for user %u from NPC %d: "
                    "HTML length reached %d at entry %d (limit 10240). No HTML is sent.",
                    static_cast<long long>(listId), static_cast<unsigned>(user->Id()), npcSid, htmlLength, originalIndex
                );
                return TeleportListResult::TooLarge;
            }
            AppendTargetText(html, row.data());
        }

        return TeleportListResult::Ready;
    }();
    if (listResult == TeleportListResult::Missing) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"s_mapTelPosList find fail UserName[%s]",
        //     user->GetName())
        L2_LOG_ERROR(
            Log,
            "[teleport] Cannot display list %lld for user %u from NPC %d: "
            "the list is missing. No HTML is sent.",
            static_cast<long long>(listId), static_cast<unsigned>(user->Id()), npcSid
        );
        return false;
    }
    if (listResult == TeleportListResult::TooLarge) {
        return false;
    }
    auto& html = *htmlStorage;
    std::array<char16_t, 1000> footer{};
    const int footerLength = FormatTargetText(footer, u"<br></body></html>");
    if (htmlLength + footerLength < 0x2800) {
        AppendTargetText(html, footer.data());
        TalkChannelCache::inst().mChannels[5].Cache(user->Id(), static_cast<std::uint32_t>(npcSid), listId);
        user->ShowHTML(nullptr, html.data(), 0, HTML::LINK, true);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too large html for teleport list",
        //     L"NpcSocket.cpp",
        //     0x608)
        // Noise: shipping source coordinates are replaced by the logger's basename.
        L2_LOG_ERROR(
            Log,
            "[teleport] Cannot display list %lld for user %u from NPC %d: "
            "HTML length %d plus footer length %d reaches the limit of 10240. No HTML is sent.",
            static_cast<long long>(listId), static_cast<unsigned>(user->Id()), npcSid, htmlLength, footerLength
        );
    }
    return false;
}

NpcSocket::NpcSocket(std::uint64_t socket) : CSocket(socket, 0x1000) {
    L2_GUARD_FUNCTION();
    bMustLogDataTransmissionLog = false;
    m_pPacketTable = reinterpret_cast<PacketFunc*>(gPacketTableFromNpc.GetData());
}

NpcSocket::~NpcSocket() { L2_GUARD_FUNCTION(); }

L2_DEFINE_OBJECT_CLASS_ID(NpcSocket)

L2_DEFINE_OBJECT_CLASS_NAME(NpcSocket, m_pClassName_NpcSocket)

L2_DEFINE_OBJECT_ALLOCATOR(NpcSocket, __GetClassName())

L2_DEFINE_OBJECT_NEW(NpcSocket)

CSocket* NpcSocket::Allocate(std::uint64_t socket) {
    L2_GUARD_FUNCTION();
    return new NpcSocket(socket);
}

L2_DEFINE_OBJECT_ADD_REF(NpcSocket)

L2_DEFINE_OBJECT_RELEASE(NpcSocket)

void NpcSocket::OnCreate() {
    L2_GUARD_FUNCTION();
    Read(0);
    AddTimer(600000, 0);
}

void NpcSocket::OnRead() {
    L2_GUARD_FUNCTION();

    std::int32_t packetOffset = 0;
    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    if (m_Mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    while (packetOffset < readSize) {
        if (m_Mode == SocketMode::SM_READ_LEN) {
            if (readSize < packetOffset + 2) {
                Read(static_cast<std::uint32_t>(readSize - packetOffset));
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_nPacketLen = static_cast<std::int32_t>(
                               static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                           ) -
                2;
            if (m_nPacketLen < 1 || m_nPacketLen > 0x4ffe) {
                // Log.Add(
                //     LOG_ERROR,
                //     "npcsocket %x: bad packet size %d",
                //     LegacyNpcSocketSymbolForLog(),
                //     m_nPacketLen)
                L2_LOG_ERROR(
                    Log,
                    "[NPC connection] Invalid packet body length %d (allowed 1..20478 bytes, "
                    "excluding the two-byte header). Closing the connection.",
                    m_nPacketLen
                );
                m_Mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SocketMode::SM_READ_BODY;
            packetOffset += 2;
        } else if (m_Mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }

        if (readSize < packetOffset + m_nPacketLen) {
            Read(static_cast<std::uint32_t>(readSize - packetOffset));
            return;
        }

        ++m_nPacketCount;
        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (opcode == 0xffU) {
            // Log.Add(
            //     LOG_ERROR,
            //     "npcsocket: unknown protocol %d",
            //     static_cast<int>(opcode))
            L2_LOG_ERROR(
                Log, "[NPC connection] Unsupported opcode 0x%02X in a %d-byte packet body. Closing the connection.",
                static_cast<unsigned>(opcode), m_nPacketLen
            );
            m_Mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        CNPCPacket* const packet = new CNPCPacket;
        packet->m_pSocket = this;
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = reinterpret_cast<NpcPacketCallback>(m_pPacketTable[opcode]);

        if (opcode < 0xfeU) {
            // count the full frame, including its length prefix.
            // CPacketStat<312, 254>::Inst().Accumulate(
            //     static_cast<int>(opcode), m_nPacketLen)
            CPacketStat<312, 254>::Inst().Accumulate(static_cast<int>(opcode), m_nPacketLen + 2);
        } else {
            const std::uint8_t low = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset + 1)];
            const std::uint8_t high = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset + 2)];
            // extended and ordinary counters use the same
            // full-frame unit. Keep the extended opcode bytes in the size.
            // CPacketStat<312, 254>::Inst().Accumulate(
            //     static_cast<int>(opcode) + static_cast<int>(low) +
            //         static_cast<int>(high) * 0x100,
            //     m_nPacketLen - 2)
            CPacketStat<312, 254>::Inst().Accumulate(
                static_cast<int>(opcode) + static_cast<int>(low) + static_cast<int>(high) * 0x100, m_nPacketLen + 2
            );
        }

        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_nPacketLen;
        m_Mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void NpcSocket::OnClose() {
    // Current L2Server 0x008c0058.
    L2_GUARD_FUNCTION();

    void* npcRemovingGate = nullptr;
    std::string diagnostic;
    const runtime::InstanceGateResult gateResult = ::runtime::AcquireNamedInstanceGate(
        false, u"Global\\_L2SERVER_NPC_REMOVING_", "l2server_Global__L2SERVER_NPC_REMOVING_.lock", npcRemovingGate,
        diagnostic
    );
    if (gateResult == runtime::InstanceGateResult::AlreadyRunning) {
#if defined(_WIN32)
        ::MessageBoxW(nullptr, L"hNPCRemoving HANDLE is Duplicated", L"Error", 0);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"HANDLE hNPCRemoving = CreateMutex(0, TRUE, Global\\_L2SERVER_NPC_REMOVING_),")
        L2_LOG_ERROR(
            Log, "[NPC cleanup] The NPC-removal gate is already in use. Connection cleanup continues without acquiring it."
        );
    } else if (gateResult == runtime::InstanceGateResult::Error) {
        // Portable diagnostic: this existing failure path previously continued without reporting its cause.
        L2_LOG_ERROR(
            Log, "[NPC cleanup] Could not acquire the NPC-removal gate: %s. Connection cleanup continues.",
            diagnostic.c_str()
        );
    }

    m_Mode = SocketMode::SM_CLOSE;
    bMustLogDataTransmissionLog = true;
    g_npcserver.RemoveEx(m_Socket);

    // The current Win64 image uses InterlockedExchange on only the low
    // DWORD of the 64-bit source field, then sign-extends that result.
    const std::int32_t oldSocketLow =
        ::runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_Socket), 0);
    const std::uint64_t oldSocket = static_cast<std::uint64_t>(static_cast<std::int64_t>(oldSocketLow));

    g_PetManager.DeleteAll();
    BuySellListCache::Inst().ClearAll();
    NpcPosEventMap::inst().OnNpcSocketClosed();
    DoorEventMap::inst().OnNpcSocketClosed();
    AreaStatusMap::inst().OnNpcSocketClosed();

    CNpcRemover* const remover = new CNpcRemover;
    remover->AddTimer(0, 0);

    // correct the recovered socket-map owner to match the
    // current EXE. At 0x008c01ee/0x008c01f5, Server::Remove receives
    // g_npcserver (0x02fd4380), the same owner used by RemoveEx above.
    // g_server.Remove(oldSocket)
    g_npcserver.Remove(oldSocket);
    g_EnterWorldSerializer.ClearDataQueue();
    g_UniqNPCDB.DeleteAll();
    g_PledgeTimer.SetAllMakerFinished(false);
    CPVPMatchSystem::inst().OnNpcServerClosed();

    // Log.Add(
    //     LOG_WARN,
    //     "npc socket close %x(%x)  error(%d)",
    //     oldSocket,
    //     reinterpret_cast<std::uintptr_t>(this),
    //     NativeProcessError())
    // Log.Add(LOG_WARN,
    //     "[NPC] Connection closed: socket 0x%016llx, system error %d. NPC state has been cleared.",
    //     static_cast<unsigned long long>(oldSocket), NativeProcessError())
    // NPC removal runs asynchronously. This log does not
    // establish the peer's reason for closing the connection.
    // L2_LOG_WARN(Log,
    //     "[NPC] Connection closed: socket 0x%016llx, system error %d. NPC state has been cleared.",
    //     static_cast<unsigned long long>(oldSocket), NativeProcessError())
    // identify the peer and print the socket handle in decimal.
    // L2_LOG_WARN(Log,
    //     "[NPC] Connection closed: socket 0x%016llx. Connection state cleared. NPC removal scheduled.",
    //     static_cast<unsigned long long>(oldSocket))
    L2_LOG_WARN(
        Log, "[NPC] Connection closed: peer=L2NPC socket=%llu. Connection state cleared; NPC removal scheduled.",
        static_cast<unsigned long long>(oldSocket)
    );

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, user, {
        // Log.Add(LOG_WARN, L"%d", user->GetDBID())
        // Noise: one bare character ID per GM adds no diagnosis beyond the connection-close summary.
        // L2_LOG_WARN(Log, L"%d", user->GetDBID())
        user->GetSocket()->SendSystemMessage(u"SYS", u"NPC Socket is closed.");
    });

    // disconnect counters are informational. Preserve the
    // recovered report order and contents, without classifying traffic as errors.
    // CPacketStat<200, 200>::Inst().Report(LOG_ERROR)
    // CPacketStat<312, 254>::Inst().Report(LOG_ERROR)
    // CPacketStat<200, 200>::Inst().ReportRank(
    //     LOG_ERROR,
    //     CPacketStat<200, 200>::PRED_CNT)
    // CPacketStat<312, 254>::Inst().ReportRank(
    //     LOG_ERROR,
    //     CPacketStat<312, 254>::PRED_CNT)
    // describe the named packet rows and their full-frame units.
    // L2_LOG_INFO(Log,
    //     "[traffic] NPC connection closed. Reporting accumulated packet counters. Entry numbers are opcodes. Extended
    //     entries are extended opcodes. Counts and bytes are traffic statistics.")
    L2_LOG_INFO(
        Log,
        "[traffic] L2NPC connection closed; reporting accumulated packet counters with callback names and opcodes. Frame "
        "bytes include packet headers; TCP/IP overhead excluded."
    );
    CPacketStat<200, 200>::Inst().Report(LOG_NORMAL);
    CPacketStat<312, 254>::Inst().Report(LOG_NORMAL);
    CPacketStat<200, 200>::Inst().ReportRank(LOG_NORMAL, CPacketStat<200, 200>::PRED_CNT);
    CPacketStat<312, 254>::Inst().ReportRank(LOG_NORMAL, CPacketStat<312, 254>::PRED_CNT);
    bMustLogDataTransmissionLog = false;

    ReleaseNamedGate(npcRemovingGate);
}

void NpcSocket::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (!bMustLogDataTransmissionLog) {
        // CPacketStat<200, 200>::Inst().ReportRank(
        //     LOG_ERROR,
        //     CPacketStat<200, 200>::PRED_CNT)
        CPacketStat<200, 200>::Inst().ReportRank(LOG_NORMAL, CPacketStat<200, 200>::PRED_CNT);
        // CPacketStat<312, 254>::Inst().ReportRank(
        //     LOG_ERROR,
        //     CPacketStat<312, 254>::PRED_CNT)
        CPacketStat<312, 254>::Inst().ReportRank(LOG_NORMAL, CPacketStat<312, 254>::PRED_CNT);
        CPacketStat<200, 200>::Inst().Reset();
        CPacketStat<312, 254>::Inst().Reset();
        // CSendNPCInfoStat::Inst().Report(LOG_ERROR)
        CSendNPCInfoStat::Inst().Report(LOG_NORMAL);
        CSendNPCInfoStat::Inst().Reset();
    }
    AddTimer(600000, 0);
}

bool NpcSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Write to invalid npc socket.",
        //     L"NpcSocket.cpp",
        //     0x64)
        // Noise: the logger supplies the source basename without shipping line numbers.
        L2_LOG_ERROR(Log, "[NPC connection] Write check failed: the socket handle is invalid.");
        return false;
    }
    if (m_nPendingWrite.load(std::memory_order_relaxed) <= 0x1000000) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"npcsocket pending write > %d KB, Close!",
    //     0x4000)
    L2_LOG_ERROR(
        Log,
        "[NPC connection] Write check failed: pending output exceeded 16 MiB. "
        "Reporting the call stack and packet statistics."
    );
    runtime::process::ReportCallStack(LOG_ERROR, false);
    CPacketStat<200, 200>::Inst().Report(LOG_ERROR);
    CPacketStat<312, 254>::Inst().Report(LOG_ERROR);
    return false;
}

void NpcSocket::OnSending(std::uint8_t opcode, std::uint16_t extendedOpcode, int packetSize) const {
    L2_GUARD_FUNCTION();
    static_cast<void>(extendedOpcode);
    CPacketStat<200, 200>::Inst().Accumulate(static_cast<int>(opcode), packetSize);
}

bool NpcSocialAction(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089f3f8, NC_PROTOCOL 0x2c.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t npcIndex;
    std::int32_t targetSid;
    std::int32_t targetIndex;
    std::int32_t actionId;
    std::int32_t delay;
    ::Disassemble(packet, "dddddd", &npcSid, &npcIndex, &targetSid, &targetIndex, &actionId, &delay);

    static_cast<void>(npcIndex);
    static_cast<void>(targetSid);
    static_cast<void>(targetIndex);
    static_cast<void>(delay);
    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && npc->IsActive()) {
        npc->SocialAction(actionId, 0x800, false);
    }
    return false;
}

bool NpcSocialAction2(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089f540, NC_PROTOCOL 0x2d.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t npcIndex;
    std::int32_t targetSid;
    std::int32_t targetIndex;
    std::int32_t actionId;
    std::int32_t delay;
    std::int32_t distance;
    ::Disassemble(packet, "ddddddd", &npcSid, &npcIndex, &targetSid, &targetIndex, &actionId, &delay, &distance);

    static_cast<void>(npcIndex);
    static_cast<void>(targetSid);
    static_cast<void>(targetIndex);
    static_cast<void>(delay);
    if (distance > 0x2000) {
        distance = 0x2000;
    }
    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && npc->IsActive()) {
        npc->SocialAction(actionId, distance, true);
    }
    return false;
}

bool NpcDespawn(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089f698, NC_PROTOCOL 0x2e.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid;
    std::int32_t ignoredNpcIndex;
    ::Disassemble(packet, "dd", &npcSid, &ignoredNpcIndex);
    static_cast<void>(ignoredNpcIndex);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        return false;
    }

    if (npc->IsWorldTrap() && npc->IsInRealWorld()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"WorldTrap should not despawn")
        L2_LOG_ERROR(
            Log,
            "[NPC lifecycle] Despawn requested for world trap SID %d in the real world. "
            "Processing continues through the remaining despawn checks.",
            npcSid
        );
    }

    if (npc->GetAliveHour() > 0) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Cannot NPC[%s] Despawn! Already Set AliveHour : %d",
        //     npc->GetName(),
        //     npc->GetAliveHour())
        // L2_LOG_INFO(Log,
        //     "[NPC lifecycle] Despawn request ignored for '%ls': AliveHour is %d (must be zero or negative for this
        //     request).", npc->GetName(), npc->GetAliveHour())
        L2_LOG_INFO(
            Log,
            u"[NPC lifecycle] Despawn request ignored for '%ls': AliveHour is %d (must be zero or negative for this "
            u"request).",
            npc->GetName(), npc->GetAliveHour()
        );
        return false;
    }

    if (npc->IsSummon()) {
        User* const master = npc->GetMaster();
        if (master != nullptr) {
            const int summonIndex = master->GetSummonIndex(static_cast<int>(npc->Id()));
            if (npc->IsSummon()) {
                db.RequestDeleteSummon(*master, summonIndex, nullptr);
                npc->CastSummon()->DeleteInfectedSkills(nullptr);
            }
            master->UnlinkSummon(npc->CastSummon());
        }
    }

    npc->Despawn(true, true);
    if (npcSid == 0x0f5314 || npcSid == 0x0f5326 || npcSid == 0x0f76e0 || npcSid == 0x0fc71b || npcSid == 0x0fc876) {
        RelayClient::Inst()->SendDespawnNPCPacket(npcSid);
    }
    return false;
}

bool NpcPongForCastleSiege(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b400, NC_PROTOCOL 0x2f.
    L2_GUARD_FUNCTION();

    std::int32_t lastPongTime;
    std::int32_t allNpcLoaded;
    ::Disassemble(packet, "dd", &lastPongTime, &allNpcLoaded);
    g_PledgeTimer.SetLastPongTimeAndState(lastPongTime, allNpcLoaded);
    return false;
}

bool NpcSocket::NpcWhisper(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    char16_t text[1024];
    ::Disassemble(packet, "ddS", &npcSid, &userSid, 0x800, text);

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    if (npc != nullptr && user != nullptr) {
        user->GetSocket()->Send(
            "cddddS", 0x30, runtime::Int32FromBits(npc->m_nId), SPT_TELL, npc->GetClassId(), -1, text
        );
    }
    return false;
}

bool NpcSocket::NpcSetAbilityDropItem(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t ability = 0;
    ::Disassemble(packet, "dd", &npcSid, &ability);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        npc->m_bCanDropItem = ability != 0;
    }
    return false;
}

bool NpcSocket::NpcWhisperFStr(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t stringId;
    char16_t text1[256];
    char16_t text2[256];
    char16_t text3[256];
    char16_t text4[256];
    char16_t text5[256];
    ::Disassemble(
        packet, "dddSSSSS", &npcSid, &userSid, &stringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4, 0x200,
        text5
    );

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    if (npc != nullptr && user != nullptr) {
        user->GetSocket()->Send(
            "cddddSSSSS", 0x30, runtime::Int32FromBits(npc->m_nId), SPT_TELL, npc->GetClassId(), stringId, text1, text2,
            text3, text4, text5
        );
    }
    return false;
}

bool NpcSocket::NpcIsToggleSkillOnOff(NpcSocket* socket, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t creatureSid;
    std::int64_t skillNameId;
    ::Disassemble(packet, "ddQ", &npcSid, &creatureSid, &skillNameId);

    CNPC* const npc = FindNpcBySid(npcSid);
    CCreature* const creature = FindCreatureBySid(creatureSid);
    if (npc != nullptr && creature != nullptr) {
        std::uint64_t skillNameBits;
        static_assert(sizeof(skillNameBits) == sizeof(skillNameId));
        std::memcpy(&skillNameBits, &skillNameId, sizeof(skillNameBits));
        const int skillId = runtime::Int32FromBits(static_cast<std::uint32_t>(skillNameBits >> 32U));
        const int level = static_cast<int>((skillNameBits >> 16U) & 0xffffU);
        const int subLevel = static_cast<int>(skillNameBits & 0xffffU);
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), creature);
        if (skill == nullptr || (skill->m_OperateType != SKILL_TOGGLE && skill->m_OperateType != SKILL_GROUP_TOGGLE)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid skill_id[%d] skill_level[%d]",
            //     L"NpcSocket.cpp",
            //     0x2a59,
            //     skillId,
            //     level)
            // Noise: replace shipping coordinates with request context and the actual outcome.
            L2_LOG_ERROR(
                Log,
                "[NPC skills] Cannot answer toggle-state request from NPC %d for creature %d: "
                "skill %d, level %d, sublevel %d is missing or is not a toggle skill. No reply is sent.",
                npcSid, creatureSid, skillId, level, subLevel
            );
        } else {
            socket->Send(
                "cddd", 0x97, runtime::Int32FromBits(npc->m_nId), runtime::Int32FromBits(creature->m_nId),
                creature->IsToggleSkillOnOff(skillId) ? 1 : 0
            );
        }
    }
    return false;
}

bool NpcSocket::NpcClientEffectInfo(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t effectType = 0;
    std::int32_t userSid = 0;
    std::int32_t argument0 = 0;
    std::int32_t argument1 = 0;
    ::Disassemble(packet, "dddd", &effectType, &userSid, &argument0, &argument1);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->GetSocket()->Send("chddd", 0xfe, 0xc3, effectType, argument0, argument1);
    }
    return false;
}

bool NpcSocket::NpcChangeName(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t nameKind = 0;
    std::int32_t stringId = 0;
    char16_t name[32];
    ::Disassemble(packet, "dddS", &npcSid, &nameKind, &stringId, 0x32, name);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        std::wstring hostName;
        const wchar_t* const convertedName = TargetUtf16ToHostWide(name, hostName);
        if (nameKind == 0) {
            if (stringId == -1) {
                npc->SetNickName(name);
            } else {
                npc->SetNickNameFStr(stringId, name);
            }
        } else if (nameKind == 1) {
            if (stringId == -1) {
                npc->SetMasterName(WideToUtf16(convertedName).c_str());
            } else {
                npc->SetMasterNameFStr(stringId, convertedName);
            }
        }
        npc->SendMyInfo();
    }
    return false;
}

bool NpcSocket::NpcObserveEventMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a59dc, NC_PROTOCOL 0xd8.
    L2_GUARD_FUNCTION();

    std::int32_t userSid;
    std::int32_t matchId = -1;
    ::Disassemble(packet, "dd", &userSid, &matchId);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        CEventMatchUserCommandHandler::AddObserverByNPC(user, matchId);
    }
    return false;
}

bool NpcSocket::NpcTeleportTo(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t sourceSid = 0;
    std::int32_t destinationSid = 0;
    std::int32_t instantZoneSid = 0;
    ::Disassemble(packet, "dddd", &npcSid, &sourceSid, &destinationSid, &instantZoneSid);
    (void)npcSid;

    CCreature* const source = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(sourceSid));
    CCreature* const destination = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(destinationSid));
    if (source == nullptr || destination == nullptr) {
        return false;
    }

    if (instantZoneSid == 0) {
        if (source->GetInZoneID() == destination->GetInZoneID()) {
            const FVector position = destination->GetPos();
            source->StopMove();
            source->TeleportToLocation(
                static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
                destination->GetInZoneID(), 0x10000, TeleportSourceType::NpcTeleportTo, nullptr, 0
            );
        }
        return false;
    }

    [[maybe_unused]] const FVector destinationPosition = destination->GetPos();
    source->StopMove();
    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSid));
    CInstantZone* instantZone = nullptr;
    if (object != nullptr && object->IsInZone()) {
        instantZone = object->CastInZone();
    }
    if (instantZone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "PledgeUnion NOT InzoneID[%d], Target[%s], To[%s]",
        //     instantZoneSid,
        //     source->GetName(),
        //     destination->GetName())
        // Log.Add(LOG_ERROR,
        //     L"[NPC teleport] Cannot teleport \"%ls\" to \"%ls\": instance %d is unavailable.",
        //     source->GetName(), destination->GetName(), instantZoneSid)
        // L2_LOG_ERROR(Log,
        //     L"[NPC teleport] Cannot teleport \"%ls\" to \"%ls\": instance %d is unavailable.",
        //     source->GetName(), destination->GetName(), instantZoneSid)
        // L2_LOG_ERROR(Log,
        //     "[NPC teleport] Cannot teleport '%ls' toward '%ls': instance SID %d did not resolve to an instance. "
        //     "The source's movement was stopped before this lookup.",
        //     source->GetName(), destination->GetName(), instantZoneSid)
        L2_LOG_ERROR(
            Log,
            u"[NPC teleport] Cannot teleport '%ls' toward '%ls': instance SID %d did not resolve to an instance. "
            u"The source's movement was stopped before this lookup.",
            source->GetName(), destination->GetName(), instantZoneSid
        );
        return false;
    }

    CNPC* const npc = source->CastNPC();
    if (npc != nullptr) {
        const FVector position = instantZone->GetConst()->GetStartPos(0);
        npc->m_data->SetPos(position);
        instantZone->EnterZone(npc, false);
        npc->TeleportToLocation(
            static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
            static_cast<unsigned int>(instantZoneSid), 0x10000, TeleportSourceType::NpcTeleportTo, nullptr, 0
        );
    }
    return false;
}

bool NpcSocket::NpcChangeState(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t state;
    ::Disassemble(packet, "dd", &npcSid, &state);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        npc->State(state);
        const FVector position = npc->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(npc, 0x5000, position, 0xc00, "chdd", 0xfe, 0xbf, npcSid, state);
    }
    return false;
}

bool NpcSocket::NpcShowChannelingEffect(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t skillId;
    std::int32_t duration;
    ::Disassemble(packet, "ddd", &npcSid, &skillId, &duration);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        const FVector position = npc->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(npc, 0x5000, position, 0xc00, "chddd", 0xfe, 0xe0, npcSid, skillId, duration);
    }
    return false;
}

bool NpcSocket::NpcChangeDir(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid = 0;
    std::int32_t targetSid = 0;
    std::int32_t direction = 0;
    ::Disassemble(packet, "ddd", &creatureSid, &targetSid, &direction);

    CCreature* const creature = FindCreatureBySid(creatureSid);
    if (creature != nullptr) {
        creature->DoNothing();
        CCreature* const target = FindCreatureBySid(targetSid);
        if (target == nullptr) {
            creature->m_data->SetDirByInt(direction);
        } else {
            creature->TurnDirToTarget(*target);
        }
        creature->ValidateLocation();
    }
    return false;
}

bool NpcSocket::NpcIncreaseNPCLog(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a61a4.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    std::uint8_t userLogType = 0;
    std::int32_t npcId = 0;
    std::int32_t maximum = 0;
    ::Disassemble(packet, "ddcdd", &userSid, &questId, &userLogType, &npcId, &maximum);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SendQuestNPCLogList(questId, nullptr);
        db.SaveQuestNPCLog(user, questId);
    }
    return false;
}

bool NpcSocket::NpcDynamicQuest(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a62ec, NC_PROTOCOL 0xec.
    L2_GUARD_FUNCTION();

    std::uint8_t action = 0;
    std::int32_t userSid = 0;
    std::int32_t dynamicQuestId = 0;
    std::int32_t campaignId = 0;
    std::uint32_t goalId = 0;
    std::int32_t pointType = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "cdddddd", &action, &userSid, &dynamicQuestId, &campaignId, &goalId, &pointType, &point);

    if (action == 2U) {
        User* const user = FindUserBySid(userSid);
        if (user != nullptr) {
            std::pair<int, int> ids(dynamicQuestId, campaignId);
            DynamicQuestManager::Inst()->OnAddPoint(
                *user, ids, goalId, static_cast<DynamicQuestType::PointType>(pointType), point
            );
        }
    } else if (action == 3U) {
        DynamicQuestManager::Inst()->InitNPCMaker();
    }
    return false;
}

bool NpcSocket::FindRandomUser(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x008c106c.
    L2_GUARD_FUNCTION();
    CEvent* const event = CEventManager::inst().GetEvent(GAME_EVENT::ET_XMAS_SANTA);
    if (event == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"XMASEvent object doesn't exist",
        //     "NpcSocket.cpp",
        //     0x2b75)
        // Noise: remove unused source arguments. Describe the operation that could not start.
        L2_LOG_ERROR(Log, "[Events] Cannot start random-user selection: the Christmas Santa event was not found.");
    } else {
        event->StartGetRandomUser();
    }
    return false;
}

bool InsertEventRanking(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c0e34.
    L2_GUARD_FUNCTION();
    std::int32_t eventId = 0;
    std::int32_t userSid = 0;
    std::int32_t score = 0;
    ::Disassemble(packet, "ddd", &eventId, &userSid, &score);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]InsertEventRanking cannot find user",
        //     "NpcSocket.cpp",
        //     0x2b35)
        // Noise: replace shipping coordinates with the rejected ranking entry.
        L2_LOG_ERROR(
            Log, "[Events] Cannot insert ranking entry for event %d, score %d: user SID %d was not found.", eventId, score,
            userSid
        );
        return false;
    }

    CEvent* const baseEvent = CEventManager::inst().GetEvent(GAME_EVENT::ET_WORLD);
    if (baseEvent != nullptr) {
        auto* const worldEvent = static_cast<CEvent_World*>(baseEvent);
        const std::int32_t now = runtime::crt::Time32Now();
        const char16_t* const userName = user->GetName();
        const int round = worldEvent->GetRoundNo(now);
        db.RequestInsertEventRanking(eventId, round, userName, score);
    }
    return false;
}

bool NpcSocket::GetEventScheduledTime(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c0fa4.
    L2_GUARD_FUNCTION();
    std::int32_t eventId = 0;
    ::Disassemble(packet, "d", &eventId);
    CEvent* const baseEvent = CEventManager::inst().GetEvent(GAME_EVENT::ET_WORLD, eventId);
    if (baseEvent != nullptr) {
        static_cast<CEvent_World*>(baseEvent)->SendScheduledTime();
    }
    return false;
}

bool NpcSocket::RequestEventInfo(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x008c1144.
    L2_GUARD_FUNCTION();
    CEventManager::inst().SendEventInfoToNPC();
    return false;
}

bool NpcSocket::RegisterOlympiadMaker(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089d560.
    L2_GUARD_FUNCTION();
    std::int32_t instantZoneTypeId = 0;
    ::Disassemble(packet, "d", &instantZoneTypeId);
    if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        WOlympiadService::inst().RegisterOlympiadMaker(instantZoneTypeId);
    }
    return false;
}

bool NpcSocket::CreatureInfoUpdated(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid = 0;
    std::int32_t updateType = 0;
    ::Disassemble(packet, "dd", &creatureSid, &updateType);

    CCreature* const creature = FindCreatureBySid(creatureSid);
    if (creature != nullptr) {
        if (updateType == 1 || updateType == 2) {
            creature->SendMyInfo();
        } else if (updateType == 3) {
            creature->ValidateLocation();
        }
    }
    return false;
}

bool NpcSocket::NpcStartScenePlayer(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a50a0, NC_PROTOCOL 0xca.
    L2_GUARD_FUNCTION();

    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(int sceneId, int npcSid, int argument) : m_sceneId(sceneId), m_npcSid(npcSid), m_argument(argument) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                object->CastUser()->StartScenePlay(m_sceneId, m_argument, m_npcSid);
            }
            return true;
        }

    private:
        int m_sceneId;
        int m_npcSid;
        int m_argument;
    };

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t sceneId = 0;
    std::int32_t argument = 0;
    std::int32_t target = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "ddddd", &npcSid, &userSid, &sceneId, &argument, &target);

    std::int32_t distance = 0;
    std::int32_t minZ = 0;
    std::int32_t maxZ = 0;
    if (target == 2) {
        ::Disassemble(cursor, "ddd", &distance, &minZ, &maxZ);
        CNPC* const npc = FindNpcBySid(npcSid);
        if (npc != nullptr) {
            const FVector position = npc->GetPos();
            CTLMFilterDistBetweenZ filter(
                static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(distance), minZ, maxZ
            );
            TLMFunc function(sceneId, npcSid, argument);
            FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
        }
    } else {
        User* const user = FindUserBySid(userSid);
        if (user != nullptr) {
            user->OnNpcStartScenePlayerPacket(sceneId, argument, target, distance, minZ, maxZ, npcSid);
        }
    }
    return false;
}

bool NpcRegisterBuySellListPacket(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcSocket::NpcShowBuySellPagePacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b6f80, NC_PROTOCOL 0xd7.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    std::int64_t sellListId = 0;
    std::int64_t buyBanListId = 0;
    double ratio = 0.0;
    const unsigned char* cursor = ::Disassemble(packet, "ddQQf", &userSid, &npcSid, &sellListId, &buyBanListId, &ratio);

    // Current L2Server.exe 0x008b708d resolves the first 0xd7 field through
    // CSharedFactory<CCreature>::FindObjectSP because L2NPC sends the shared
    // creature index here, not the creature SmartID.
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!userCreature || !userCreature->IsPC()) {
        return false;
    }
    User* const user = userCreature->CastUser();
    if (ratio > 9.999999747378752e-06) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][item error] invalid sell rate, npc id(%d), ratio(%f)",
        //     L"NpcSocket.cpp",
        //     0x6ba,
        //     npcSid,
        //     ratio)
        // Noise: shipping coordinates are replaced by the source basename and request context.
        L2_LOG_ERROR(
            Log,
            "[NPC shop] Buy/sell page rejected for user index %d from NPC %d: "
            "sell ratio %.17g exceeds the allowed threshold %.17g.",
            userSid, npcSid, ratio, 9.999999747378752e-06
        );
        return false;
    }

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr || npc->m_data == nullptr) {
        return false;
    }

    std::int32_t listCount = 0;
    cursor = ::Disassemble(cursor, "d", &listCount);
    for (std::int32_t listIndex = 0; listIndex < listCount; ++listIndex) {
        std::int32_t zoneId = 0;
        std::int64_t listId = 0;
        std::int32_t itemCount = 0;
        cursor = ::Disassemble(cursor, "dQd", &zoneId, &listId, &itemCount);

        auto* const list = new BuySellListCache::List;
        for (std::int32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
            BuySell item{};
            cursor = ::Disassemble(cursor, "dQfQ", &item.m_nItemClassId, &item.m_nPrice, &item.m_dStockRate, &item.m_nStock);
            if (item.m_nItemClassId == -1) {
                // Log.Add(
                //     LOG_ERROR,
                //     "Undefined item id(%d)",
                //     -1)
                L2_LOG_ERROR(
                    Log,
                    "[NPC shop] Buy/sell list %lld, zone %d, item index %d contains class ID -1. "
                    "Replacing it with 0 and continuing to load the list.",
                    static_cast<long long>(listId), zoneId, itemIndex
                );
                item.m_nItemClassId = 0;
            }
            if (item.m_dStockRate <= 0.0 || item.m_dStockRate > 1.0 || item.m_dStockRate < ::genrand64_real1()) {
                item.m_nCurrentStock = 0;
            } else {
                item.m_nCurrentStock = item.m_nStock;
            }
            item.m_nStockTimestamp = runtime::crt::Time32Now();
            list->push_back(item);
        }

        BuySellListCache::Key key{npc->m_data->m_nResidenceID, zoneId, listId};
        static_cast<void>(BuySellListCache::Inst().Register(key, list));
    }

    return user->OnNpcShowBuySellPagePacket(*npc, sellListId, buyBanListId, ratio);
}

bool NpcShowSellPreviewPagePacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b747c, NC_PROTOCOL 0x61.
    L2_GUARD_FUNCTION();

    std::int32_t userIndex = 0;
    std::int32_t npcIndex = 0;
    std::int64_t buySellListId = 0;
    std::array<char16_t, 1024> resultPage{};
    const unsigned char* cursor =
        ::Disassemble(packet, "ddQS", &userIndex, &npcIndex, &buySellListId, 0x800, resultPage.data());

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!userCreature || !userCreature->IsPC()) {
        return false;
    }
    User* const user = userCreature->CastUser();

    CSPointer<CCreature> npc = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!npc || npc->m_data == nullptr) {
        return false;
    }

    std::int32_t listCount = 0;
    cursor = ::Disassemble(cursor, "d", &listCount);
    if (listCount == 1) {
        std::int32_t zoneId = 0;
        std::int64_t listId = 0;
        std::int32_t itemCount = 0;
        cursor = ::Disassemble(cursor, "dQd", &zoneId, &listId, &itemCount);

        auto* const list = new BuySellListCache::List;
        for (std::int32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
            BuySell item{};
            cursor = ::Disassemble(cursor, "dQfQ", &item.m_nItemClassId, &item.m_nPrice, &item.m_dStockRate, &item.m_nStock);
            if (item.m_nItemClassId == -1) {
                // Log.Add(
                //     LOG_ERROR,
                //     "Undefined item id(%d)",
                //     -1)
                L2_LOG_ERROR(
                    Log,
                    "[NPC shop] Sell-preview list %lld, zone %d, item index %d contains class ID -1. "
                    "Replacing it with 0 and continuing to load the list.",
                    static_cast<long long>(listId), zoneId, itemIndex
                );
                item.m_nItemClassId = 0;
            }
            if (item.m_dStockRate <= 0.0 || item.m_dStockRate > 1.0 || item.m_dStockRate < ::genrand64_real1()) {
                item.m_nCurrentStock = 0;
            } else {
                item.m_nCurrentStock = item.m_nStock;
            }
            item.m_nStockTimestamp = runtime::crt::Time32Now();
            list->push_back(item);
        }

        const BuySellListCache::Key key{npc->m_data->m_nResidenceID, zoneId, listId};
        static_cast<void>(BuySellListCache::Inst().Register(key, list));
    }

    const BuySellListCache::Key key{npc->m_data->m_nResidenceID, static_cast<int>(npc->GetInZoneID()), buySellListId};
    BuySellListCache::List* const list = BuySellListCache::Inst().GetList(key);
    if (list == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not a cached buy-sell list[%I64d]",
        //     L"NpcSocket.cpp",
        //     0x774,
        //     buySellListId)
        // Noise: omit shipping coordinates. Identify the missing list and rejected operation.
        L2_LOG_ERROR(
            Log, "[NPC shop] Cannot display the sell preview: buy/sell list %lld is not cached.",
            static_cast<long long>(buySellListId)
        );
        return false;
    }

    CBuy* buy = nullptr;
    {
        GuardHelper previewGuard(L"NpcShowSellPreviewPagePacket 1", CThreadManager::GetThreadID());
        {
            std::optional<GuardHelper> economyGuardStorage;
            {
                L2_GUARD(*user->m_lock);
                economyGuardStorage.emplace(L"NpcShowSellPreviewPagePacket 1-0", CThreadManager::GetThreadID());
                buy = new CBuy(buySellListId, npc->m_data->m_nResidenceID);
                buy->SetCreatureData(npc.get());
                user->ReleaseEconomy();
                user->m_pEconomy = buy;
                buy->SetResultPage(resultPage.data());
                L2_ADD_REF(buy, ObjectRefType::Anonymous);
            }
        }

        std::array<char, 0x2030> itemPacket;
        std::uint32_t itemPacketLength;
        std::int16_t assembledItemCount;
        {
            GuardHelper itemListGuard(L"NpcShowSellPreviewPagePacket 1-1", CThreadManager::GetThreadID());
            itemPacketLength = 0;
            assembledItemCount = 0;
            for (BuySell& entry : *list) {
                bool include = false;
                if (entry.m_dStockRate == 0.0 || entry.m_nCurrentStock > 0) {
                    include = true;
                } else {
                    const std::int32_t now = runtime::crt::Time32Now();
                    const std::int32_t elapsed = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(now) - static_cast<std::uint32_t>(entry.m_nStockTimestamp)
                    );
                    if (elapsed < 0xe11) {
                        include = true;
                    } else {
                        if (entry.m_dStockRate <= 0.0 || entry.m_dStockRate > 1.0 ||
                            entry.m_dStockRate < ::genrand64_real1()) {
                            entry.m_nCurrentStock = 0;
                        } else {
                            entry.m_nCurrentStock = entry.m_nStock;
                        }
                        entry.m_nStockTimestamp = now;
                        include = entry.m_nCurrentStock != 0;
                    }
                }
                if (!include) {
                    continue;
                }

                CObject* const object = g_ObjectDB.GetObjectW(entry.m_nItemClassId, false);
                if (object == nullptr || reinterpret_cast<std::uintptr_t>(object) == UINT64_C(0xcdcdcdcdcdcdcdcd)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Undefined item(%d)",
                    //     L"NpcSocket.cpp",
                    //     0x7b9,
                    //     entry.m_nItemClassId)
                    // Noise: omit shipping coordinates. Retain the list, item and skipped action.
                    L2_LOG_ERROR(
                        Log,
                        "[NPC shop] Skipping item class %d in sell-preview list %lld: "
                        "the object lookup returned null or the uninitialized-memory sentinel.",
                        entry.m_nItemClassId, static_cast<long long>(buySellListId)
                    );
                    continue;
                }
                if (!object->IsItem()) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Not a item(%d)",
                    //     L"NpcSocket.cpp",
                    //     0x7be,
                    //     entry.m_nItemClassId)
                    // Noise: omit shipping coordinates. Retain the list, item and skipped action.
                    L2_LOG_ERROR(
                        Log,
                        "[NPC shop] Skipping item class %d in sell-preview list %lld: "
                        "the resolved object is not an item.",
                        entry.m_nItemClassId, static_cast<long long>(buySellListId)
                    );
                    continue;
                }

                CItem* const item = static_cast<CItem*>(object);
                if (static_cast<int>(item->m_const->GetCrystalType()) > static_cast<int>(user->GetMaxCrystal()) ||
                    !user->CanUseItem(item)) {
                    continue;
                }

                const std::int64_t previewPrice = item->GetPreviewPrice();
                {
                    GuardHelper itemAssembleGuard(L"NpcShowSellPreviewPagePacket 1-1-2", CThreadManager::GetThreadID());
                    const int itemType = static_cast<int>(item->m_data->m_Type);
                    if (itemType < 0 || itemType >= 2 ||
                        (static_cast<std::uint64_t>(item->m_data->m_Slot) & UINT64_C(0x40)) != 0) {
                        continue;
                    }

                    const int assembled = ::Assemble(
                        itemPacket.data() + itemPacketLength, 0x2000 - static_cast<int>(itemPacketLength), "dhQQ",
                        item->m_data->m_nClassID, static_cast<int>(static_cast<std::int16_t>(itemType)),
                        item->m_data->m_Slot, previewPrice
                    );
                    if (assembled > 0) {
                        itemPacketLength += static_cast<std::uint32_t>(assembled);
                        assembledItemCount = static_cast<std::int16_t>(assembledItemCount + 1);
                    }
                }
            }
        }

        CUserSocket* const userSocket = user->GetSocket();
        const std::int64_t adenaAmount = user->m_inv.GetAdenaAmount();
        {
            GuardHelper sendGuard(L"NpcShowSellPreviewPagePacket 1-2", CThreadManager::GetThreadID());
            if (userSocket != nullptr) {
                const std::int32_t buyToken = static_cast<std::int32_t>(
                    reinterpret_cast<std::intptr_t>(buy) / static_cast<std::intptr_t>(sizeof(CBuy))
                );
                userSocket->Send(
                    "cdQdhb", 0xf5, npc->m_data->index, adenaAmount, buyToken, static_cast<int>(assembledItemCount),
                    itemPacketLength, itemPacket.data()
                );
            }
        }
    }

    L2_RELEASE(buy, ObjectRefType::Anonymous);
    return false;
}

bool NpcGetSubJobList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ad6d8, NC_PROTOCOL 0x62.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    ::Disassemble(packet, "dddd", &npcSid, &userSid, &value1, &value2);

    CSPointer<CCreature> npc = CCreatureFactory::Inst().FindObjectSP(npcSid);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!npc || !npc->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [get subjob list]")
        L2_LOG_ERROR(
            Log,
            "[Subclass] List request rejected: NPC index %d or user index %d is missing or has the wrong creature type.",
            npcSid, userSid
        );
        return false;
    }

    db.RequestGetSubJobListForNpc(userCreature->CastUser(), npcSid, userSid, value1, value2);
    return false;
}

bool NpcCreateSubJob(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ad974, NC_PROTOCOL 0x63.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t classId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &classId);

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [create/change/update subjob]")
        L2_LOG_ERROR(
            Log, "[Subclass] Creation rejected for class %d: user index %d is missing or is not a player.", classId, userSid
        );
        return false;
    }
    return userCreature->CastUser()->OnNpcCreateSubJobPacket(npcSid, classId);
}

bool NpcChangeSubJob(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008adb54, NC_PROTOCOL 0x64.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t classId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &classId);

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [create/change/update subjob]")
        L2_LOG_ERROR(
            Log, "[Subclass] Change rejected for class %d: user index %d is missing or is not a player.", classId, userSid
        );
        return false;
    }
    return userCreature->CastUser()->OnNpcChangeSubJobPacket(npcSid, classId);
}

bool NpcRenewSubJob(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008add34, NC_PROTOCOL 0x65.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t subJobId = 0;
    std::int32_t newClassId = 0;
    std::int32_t renewType = 0;
    ::Disassemble(packet, "ddddd", &npcSid, &userSid, &subJobId, &newClassId, &renewType);

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [create/change/update subjob]")
        L2_LOG_ERROR(
            Log,
            "[Subclass] Renewal rejected for subjob %d, new class %d, renewal type %d: "
            "user index %d is missing or is not a player.",
            subJobId, newClassId, renewType, userSid
        );
        return false;
    }

    User* const user = userCreature->CastUser();
    if (renewType == 1) {
        if (subJobId != user->m_data->m_nSubJobId) {
            return false;
        }
        if (user->m_data->m_DualClassID > 0 && user->m_data->m_DualClassID != subJobId) {
            return false;
        }
    }
    return user->OnNpcRenewSubJobPacket(npcSid, subJobId, newClassId, renewType);
}

bool NpcRegisterCastleSiege(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcUnregisterCastleSiege(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcCheckSiege(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcSetCastleSiegeTime(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcSetTaxRate(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089a868, NC_PROTOCOL 0x0f.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::uint32_t taxRate = 0;
    ::Disassemble(packet, "dd", &residenceId, &taxRate);
    if (taxRate > 100U) {
        taxRate = 0;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request SetTaxRate but castle is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     0x923,
        //     residenceId)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(Log, "[residence] Cannot set tax rate: residence %d was not found. No rate is changed.", residenceId);
    } else {
        residence->SetTaxRates(static_cast<int>(taxRate), static_cast<CastleType::Enum>(residence->m_data->m_ResidenceType));
    }
    return false;
}

bool NpcRegisterAgit(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089acc4, NC_PROTOCOL 0x10.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t npcMakerId = 0;
    ::Disassemble(packet, "dd", &residenceId, &npcMakerId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request RegisterAgit but Agit is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     0xad1,
        //     residenceId)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log, "[residence] Cannot register NPC maker %d for clan hall %d: residence was not found.", npcMakerId,
            residenceId
        );
    } else {
        L2_ADD_REF(residence, ObjectRefType::Anonymous);
        {
            L2_WRITE_GUARD(*residence);
            if (!residence->IsWorldAgit()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] NPC Server request RegisterAgit but class is not agit.",
                //     L"NpcSocket.cpp",
                //     0xac9)
                // Noise: replace shipping coordinates with operation context.
                L2_LOG_ERROR(
                    Log,
                    "[residence] Cannot register NPC maker %d for residence %d: "
                    "the residence is not a world clan hall.",
                    npcMakerId, residenceId
                );
            } else if (residence->m_data != nullptr) {
                residence->m_data->m_nResidenceID = npcMakerId;
            }
        }
        L2_RELEASE(residence, ObjectRefType::Anonymous);
    }
    return false;
}

bool NpcSetResidenceTaxRate(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"depleted function is called. NpcSetResidenceTaxRate")
    L2_LOG_ERROR(
        Log,
        "[residence] Received NpcSetResidenceTaxRate: this handler performs no operation. "
        "No tax rate is changed."
    );
    return false;
}

bool NpcMoveTaxToResidenceBank(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ba340, NC_PROTOCOL 0x4b.
    L2_GUARD_FUNCTION();

    std::int32_t creatureIndex = 0;
    std::int32_t residenceId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "ddQ", &creatureIndex, &residenceId, &amount);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "user null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0x9ae)
        // Noise: replace shipping coordinates with the rejected request's identifiers.
        L2_LOG_ERROR(
            Log,
            "[residence] Tax transfer rejected for residence %d, amount %lld: "
            "creature at index %d is missing or is not a player.",
            residenceId, static_cast<long long>(amount), creatureIndex
        );
        return false;
    }
    if (amount < 1) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcSetResidenceTaxRate but castle is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     0xa23,
        //     residenceId)
        // Noise: omit shipping coordinates and correct the operation named by the old message.
        L2_LOG_ERROR(
            Log,
            "[residence] Tax transfer rejected for creature index %d, amount %lld: "
            "residence %d was not found.",
            creatureIndex, static_cast<long long>(amount), residenceId
        );
        return false;
    }
    if (residence->m_data->m_nTaxIncome < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcMoveTaxToResidenceBank invalid amount.tax[%I64d] request[%I64d]",
        //     L"NpcSocket.cpp",
        //     0x9bb,
        //     residence->m_data->m_nTaxIncome,
        //     amount)
        // Noise: replace shipping coordinates with residence and transfer context.
        L2_LOG_ERROR(
            Log, "[residence] Tax transfer rejected for residence %d: requested %lld exceeds tax income %lld.", residenceId,
            static_cast<long long>(amount), static_cast<long long>(residence->m_data->m_nTaxIncome)
        );
        return false;
    }

    if (!creature->IsPC()) {
        if (!creature->AddItemToInventory(0x39, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{})) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] NPC Server request NpcMoveTaxToResidenceBank invalid amount.tax[%I64d] request[%I64d]",
            //     L"NpcSocket.cpp",
            //     0xa0a,
            //     residence->m_data->m_nTaxIncome,
            //     amount)
            // Noise: replace shipping coordinates and the misleading amount diagnosis.
            L2_LOG_ERROR(
                Log,
                "[residence] Tax transfer stopped for residence %d, creature index %d: "
                "adding %lld adena to inventory failed (tax income %lld).",
                residenceId, creatureIndex, static_cast<long long>(amount),
                static_cast<long long>(residence->m_data->m_nTaxIncome)
            );
            return false;
        }
    } else {
        User* const user = creature->CastUser();
        if (runtime::AddInt64Wrapping(user->m_inv.GetAdenaAmount(), amount) > 999999999999LL) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_POSSESSION_LIMIT_EXCEEDED);
            return false;
        }

        ItemManip request(0, 0x39, amount, 0, 0, 0, true);
        {
            L2_GUARD(user->m_ManipulateQueueLock);
            CPacketHelperEx<20480> etcPacket;
            User::etcMsg message =
                user->SaveEtcMsg(false, false, residenceId, 1, amount, std::u16string(), std::u16string(), 0);
            User::ManipulateQueue queue = user->SaveManipulateQueue(
                ManipulateType::MANI_TAX_BANK, 0, 1, &request, nullptr, 0, etcPacket, false, ItemGetReason::Etc, message
            );
            const User::ManiItemResultType result = user->_SetManiItemJob(queue);
            if (result == User::MRT_JOBMAP) {
                user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
                return false;
            }
            if (result == User::MRT_JOBNOTMAP) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"NpcSocket.cpp",
                //     0x9fa)
                // Noise: normal deferral while another item operation is pending. No failure.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"NpcSocket.cpp",
                //     0x9fa)
                return false;
            }
            if (result != User::MRT_NOJOB) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                //     L"NpcSocket.cpp",
                //     0x9ff)
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
                //     L"NpcSocket.cpp",
                //     0x9ff)
                L2_LOG_ERROR(
                    Log, "[NPC items] Unexpected queue result %d for character %u; request handling stopped.",
                    static_cast<int>(result), static_cast<unsigned int>(user->GetDBID())
                );
                return false;
            }
            if (!user->_HandleItemManipulation(queue)) {
                return false;
            }
        }
    }

    static_cast<void>(residence->ChangeTaxIncome(amount));
    residence->SaveIncome(residence->IsCastle(), 10);

    User* const user = creature->CastUser();
    if (user != nullptr) {
        const FVector position = user->GetPos();
        std::wstring accountNameStorage;
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        //     std::int64_t{0x3b7},
        //     static_cast<std::int64_t>(user->GetDBID()),
        //     static_cast<std::int64_t>(user->GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(residenceId),
        //     runtime::NegateInt64Wrapping(amount),
        //     residence->m_data->m_nTaxIncome,
        //     user->GetName(),
        //     TargetUtf16ToHostWide(
        //         user->m_sAccountName.data(),
        //         accountNameStorage))
        Log.AddT(
            LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
            std::int64_t{0x3b7}, static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(residenceId), runtime::NegateInt64Wrapping(amount), residence->m_data->m_nTaxIncome,
            user->GetName(), TargetUtf16ToHostWide(user->m_sAccountName.data(), accountNameStorage)
        );
    }
    return false;
}

bool NpcMoveMoneyToResidenceBank(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bae04, NC_PROTOCOL 0x4c.
    L2_GUARD_FUNCTION();

    std::int32_t creatureIndex = 0;
    std::int32_t residenceId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "ddQ", &creatureIndex, &residenceId, &amount);
    if (amount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcMoveMoneyToResidenceBank invalid amount.tax[%d] request[%I64d]",
        //     L"NpcSocket.cpp",
        //     0xa3b,
        //     0,
        //     amount)
        // Noise: replace shipping coordinates and the constant placeholder tax value.
        L2_LOG_ERROR(
            Log,
            "[residence] Bank deposit rejected for residence %d, creature index %d: "
            "amount %lld must be positive.",
            residenceId, creatureIndex, static_cast<long long>(amount)
        );
        return false;
    }

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "user null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0xa43)
        // Noise: replace shipping coordinates with the rejected request's identifiers.
        L2_LOG_ERROR(
            Log,
            "[residence] Bank deposit rejected for residence %d, amount %lld: "
            "creature at index %d is missing or is not a player.",
            residenceId, static_cast<long long>(amount), creatureIndex
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcMoveMoneyToResidenceBank but castle is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     0xaa9,
        //     residenceId)
        // Noise: replace shipping coordinates with the rejected request's identifiers.
        L2_LOG_ERROR(
            Log,
            "[residence] Bank deposit rejected for creature index %d, amount %lld: "
            "residence %d was not found.",
            creatureIndex, static_cast<long long>(amount), residenceId
        );
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }

    ItemManip request(0, 0x39, runtime::NegateInt64Wrapping(amount), 0, 0, 0, true);
    L2_GUARD(user->m_ManipulateQueueLock);
    CPacketHelperEx<20480> etcPacket;
    User::etcMsg message = user->SaveEtcMsg(false, false, residenceId, 1, amount, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_TAX_BANK, 0, 1, &request, nullptr, 0, etcPacket, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0xa9c)
        // Noise: normal deferral while another item operation is pending. No failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0xa9c)
        return false;
    }
    if (result != User::MRT_NOJOB) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0xaa1)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0xaa1)
        L2_LOG_ERROR(
            Log, "[NPC items] Unexpected queue result %d for character %u; request handling stopped.",
            static_cast<int>(result), static_cast<unsigned int>(user->GetDBID())
        );
        return false;
    }

    const std::int64_t inventoryAmount = creature->CastUser()->m_inv.GetAdenaAmount();
    if (inventoryAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcMoveMoneyToResidenceBank invalid amount.tax[%I64d] inv[%I64d]
        //     request[%I64d]", L"NpcSocket.cpp", 0xa6e, residence->m_data->m_nTaxIncome,
        //     creature->CastUser()->m_inv.GetAdenaAmount(),
        //     amount)
        // Noise: replace shipping coordinates with the failed balance check. Use its captured value.
        L2_LOG_ERROR(
            Log,
            "[residence] Bank deposit rejected for residence %d, creature index %d: "
            "requested %lld adena exceeds inventory balance %lld (tax income %lld).",
            residenceId, creatureIndex, static_cast<long long>(amount), static_cast<long long>(inventoryAmount),
            static_cast<long long>(residence->m_data->m_nTaxIncome)
        );
        return false;
    }
    if (!residence->DepositToSafe(amount)) {
        CUserSocket* const userSocket = creature->CastUser()->GetSocket();
        if (userSocket != nullptr) {
            userSocket->SendSystemMessage(SystemMessageId::CASTLE_VAULT_ADENA_LIMIT_EXCEEDED);
        }
        return false;
    }
    if (!user->_HandleItemManipulation(queue)) {
        return false;
    }

    residence->SaveIncome(residence->IsCastle(), 0x0b);
    User* const logUser = creature->CastUser();
    const FVector position = logUser->GetPos();
    std::wstring accountNameStorage;
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
    //     std::int64_t{0x3b7},
    //     static_cast<std::int64_t>(logUser->GetDBID()),
    //     static_cast<std::int64_t>(logUser->GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(logUser->m_data->m_Race),
    //     static_cast<std::int64_t>(logUser->m_data->m_Sex),
    //     static_cast<std::int64_t>(logUser->m_data->m_nClass),
    //     static_cast<std::int64_t>(logUser->m_data->m_nLevel),
    //     static_cast<std::int64_t>(residenceId),
    //     amount,
    //     residence->m_data->m_nTaxIncome,
    //     logUser->GetName(),
    //     TargetUtf16ToHostWide(
    //         logUser->m_sAccountName.data(),
    //         accountNameStorage))
    Log.AddT(
        LOG_BOTH, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,%s,%s,,,",
        std::int64_t{0x3b7}, static_cast<std::int64_t>(logUser->GetDBID()),
        static_cast<std::int64_t>(logUser->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(logUser->m_data->m_Race), static_cast<std::int64_t>(logUser->m_data->m_Sex),
        static_cast<std::int64_t>(logUser->m_data->m_nClass), static_cast<std::int64_t>(logUser->m_data->m_nLevel),
        static_cast<std::int64_t>(residenceId), amount, residence->m_data->m_nTaxIncome, logUser->GetName(),
        TargetUtf16ToHostWide(logUser->m_sAccountName.data(), accountNameStorage)
    );
    return false;
}

bool NpcSetResidenceTaxRateEach(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089aa30, NC_PROTOCOL 0x4e.
    L2_GUARD_FUNCTION();

    std::int32_t sourceResidenceId = 0;
    std::int32_t residenceId = 0;
    std::uint32_t taxRate = 0;
    ::Disassemble(packet, "ddd", &sourceResidenceId, &residenceId, &taxRate);
    if (taxRate > 100U) {
        taxRate = 0;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsCastle()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcSetResidenceTaxRate but castle is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     residence == nullptr ? 0x973 : 0x96e,
        //     sourceResidenceId)
        // Noise: replace shipping line variants with an explicit rejection reason.
        L2_LOG_ERROR(
            Log,
            "[residence] Child tax-rate request from residence %d rejected: target residence %d %s. "
            "No rate is changed.",
            sourceResidenceId, residenceId, residence == nullptr ? "was not found" : "is not a castle"
        );
        return false;
    }

    residence->SetChildTaxRates(
        static_cast<std::int32_t>(taxRate), static_cast<CastleType::Enum>(residence->m_data->m_ResidenceType)
    );
    return false;
}

bool NpcSetVaultEmpty(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089ab84, NC_PROTOCOL 0x4f.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::uint32_t type = 0;
    ::Disassemble(packet, "dd", &residenceId, &type);
    if (type - 1U >= 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error. invalid type.[%d]",
        //     "NpcSocket.cpp",
        //     0x989,
        //     type)
        // Noise: replace shipping coordinates with the rejected reset request and accepted range.
        L2_LOG_ERROR(
            Log, "[residence] Vault reset rejected for residence %d: type %u must be 1 or 2.", residenceId,
            static_cast<unsigned>(type)
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request NpcSetVaultEmpty but castle is not exist.[%d]",
        //     L"NpcSocket.cpp",
        //     0x994,
        //     residenceId)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log, "[residence] Vault reset rejected for type %u: residence %d was not found.", static_cast<unsigned>(type),
            residenceId
        );
        return false;
    }

    residence->ResetVault(static_cast<std::int32_t>(type));
    return false;
}

// Current L2Server 0x0089cde4. Physical key-function owner NpcSocket.obj.
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreature, __async_arg_LookNeighborByNPC)

// Current L2Server 0x0089cdfc. Physical key-function owner NpcSocket.obj.
L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CCreature, __async_arg_LookItems)

bool NpcRequestLookNeighbor(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a0188, NC_PROTOCOL 0x50.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t range;
    ::Disassemble(packet, "dd", &npcSid, &range);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        CCreature::__async_arg_LookNeighborByNPC argument;
        argument.__a1 = range;
        npc->m_LookNeighborSerializer.AddExecutable(
            npc, [](CCreature* owner, CExecutionArgumentPad* pad) { owner->__async_LookNeighborByNPC(pad); }, &argument,
            L"LookNeighborByNPC"
        );
    }
    return false;
}

bool NpcBroadcastScriptEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a02b8, NC_PROTOCOL 0x51.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid;
    std::int32_t argument1;
    std::int32_t argument2;
    std::int32_t argument3;
    std::uint32_t radius;
    std::int32_t conditional = 0;
    ::Disassemble(packet, "dddddd", &npcSid, &argument1, &argument2, &argument3, &radius, &conditional);

    CCreature* const object = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* npc = nullptr;
    if (object != nullptr && object->IsNPC()) {
        npc = object->CastNPC();
    }
    if (npc == nullptr) {
        return false;
    }
    if (conditional == 0) {
        return npc->OnNpcBroadcastScriptEvent(argument1, argument2, argument3, radius);
    }
    return npc->OnNpcBroadcastScriptEventCond(argument1, argument2, argument3, radius);
}

bool NpcEnchantSkillSuggestion(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcCreatePrivates(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcCreateOnePrivate(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b161c, NC_PROTOCOL 0x1e.
    L2_GUARD_FUNCTION();

    std::int32_t bossId;
    std::int32_t npcClassId;
    std::int32_t privateId;
    void* spawnDefine;
    char16_t aiName[104];
    std::int32_t weightPoint;
    std::int32_t respawnTime;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::int32_t direction = 0;
    std::int32_t makerId;
    std::int32_t makerParam1;
    std::int32_t makerParam2;
    std::int32_t makerParam3;
    std::int32_t fixedPosition;
    std::int32_t inZoneSid;
    std::int32_t droidMasterCharSid = 0;
    ::Disassemble(
        packet, "dddpSddddddddddddd", &bossId, &npcClassId, &privateId, &spawnDefine, 200, aiName, &weightPoint,
        &respawnTime, &x, &y, &z, &direction, &makerId, &makerParam1, &makerParam2, &makerParam3, &fixedPosition, &inZoneSid,
        &droidMasterCharSid
    );

    std::wstring hostAiNameStorage;
    const wchar_t* const hostAiName = TargetUtf16ToHostWide(aiName, hostAiNameStorage);

    if (CBoss* const boss = g_UniqNPCDB.GetBossNPC(inZoneSid, npcClassId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"boss is already spawned. [%d][%d][%s][%s]",
        //     bossId,
        //     npcClassId,
        //     boss->GetName(),
        //     hostAiName)
        // L2_LOG_ERROR(Log,
        //     "[NPC spawn] CreateOnePrivate requested boss ID %d, class %d, but boss '%ls' is already registered. "
        //     "Requested AI '%ls'. Creation processing continues.",
        //     bossId,
        //     npcClassId,
        //     boss->GetName(),
        //     hostAiName)
        L2_LOG_ERROR(
            Log,
            u"[NPC spawn] CreateOnePrivate requested boss ID %d, class %d, but boss '%ls' is already registered. "
            u"Requested AI '%ls'; creation processing continues.",
            bossId, npcClassId, boss->GetName(), hostAiName
        );
    }

    const FVector originalPosition{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    if (npcClassId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid npc id [%d] at file[%s], line[%d]",
        //     npcClassId,
        //     "NpcSocket.cpp",
        //     0xcf4)
        // Noise: replace shipping coordinates with the request rejection reason.
        L2_LOG_ERROR(Log, "[NPC spawn] CreateOnePrivate rejected: NPC class ID %d is negative.", npcClassId);
        return false;
    }

    if (inZoneSid != 0) {
        CObject* const object = CSmartID(static_cast<std::uint32_t>(inZoneSid)).GetObjectW();
        CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (instantZone == nullptr || instantZone->IsDeleting() || instantZone->m_Deleted != 0) {
            return false;
        }
    }

    CNPC* const npc = g_ObjectDB.CreateNPC(npcClassId, true);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "There is no NPC id [%d] at file[%s], line[%d]",
        //     npcClassId,
        //     "NpcSocket.cpp",
        //     0xd0d)
        // Noise: replace shipping coordinates. A null creation result does not establish its cause.
        L2_LOG_ERROR(Log, "[NPC spawn] CreateOnePrivate stopped: CreateNPC returned null for class ID %d.", npcClassId);
        return false;
    }

    std::array<char, 0x4fb0> parameterBuffer;
    int parameterCount = 0;
    int bufferLength = 0;
    for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            parameterBuffer.data() + bufferLength, 0x4f9c - bufferLength, "SS", parameter.first.c_str(),
            parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateOnePrivate [%s]: too long ai parameters",
            //     L"NpcSocket.cpp",
            //     0xd22,
            //     npc->GetName())
            // Noise: replace shipping coordinates with the failed assembly position.
            // L2_LOG_ERROR(Log,
            //     "[NPC spawn] CreateOnePrivate stopped for '%ls' (class %d): "
            //     "AI parameter %d could not be assembled with %d bytes remaining.",
            //     npc->GetName(), npcClassId, parameterCount, 0x4f9c - bufferLength)
            L2_LOG_ERROR(
                Log,
                u"[NPC spawn] CreateOnePrivate stopped for '%ls' (class %d): "
                u"AI parameter %d could not be assembled with %d bytes remaining.",
                npc->GetName(), npcClassId, parameterCount, 0x4f9c - bufferLength
            );
            return false;
        }
        bufferLength += assembled;
    }

    if (fixedPosition == 0) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            const int randomX = std::rand() % 300;
            const int randomY = std::rand() % 300;
            const int offsetX = randomX - (randomX >= 150 ? 100 : 200);
            const int offsetY = randomY - (randomY >= 150 ? 100 : 200);
            const std::int32_t candidateX =
                runtime::Int32FromBits(static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(offsetX));
            const std::int32_t candidateY =
                runtime::Int32FromBits(static_cast<std::uint32_t>(y) + static_cast<std::uint32_t>(offsetY));
            const FVector candidatePosition{
                static_cast<double>(candidateX), static_cast<double>(candidateY), static_cast<double>(z)};
            const double deltaX = candidatePosition.x - originalPosition.x;
            const double deltaY = candidatePosition.y - originalPosition.y;
            const double deltaZ = candidatePosition.z - originalPosition.z;
            const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
            if (CWorld::Inst().IsValidPosition(candidateX, candidateY) &&
                CWorld::Inst().GetGeo()->MoveStraightTest(originalPosition, candidatePosition, distance, true, 0)) {
                x = candidateX;
                y = candidateY;
                z = runtime::Win64CvttDoubleToInt32(candidatePosition.z);
                break;
            }
        }
    }

    if (npc->m_data->m_moveType != CMT_FLY) {
        z = CWorld::Inst().GetGeo()->FindGround(x, y, z);
    }

    npc->m_data->m_BossID = bossId;
    npc->m_data->m_nWeightPoint = weightPoint;
    npc->m_data->m_nRespawnTime = respawnTime;
    npc->m_data->m_nMakerParam1 = makerParam1;
    npc->m_data->m_nMakerParam2 = makerParam2;
    npc->m_data->m_nMakerParam3 = makerParam3;
    npc->SetPrivateID(privateId);

    if (droidMasterCharSid != 0) {
        CSPointer<User> user = g_UserMap.FindObject(static_cast<std::uint32_t>(droidMasterCharSid));
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][DroidNPC] fail to find user in g_Usermap.FindObject(%d)",
            //     L"NpcSocket.cpp",
            //     0xd69,
            //     droidMasterCharSid)
            // Noise: replace shipping coordinates with the missing owner and actual continuation.
            L2_LOG_ERROR(
                Log,
                "[NPC spawn] Droid owner %d was not found for NPC class %d. "
                "Creation continues without assigning droid ownership from this request.",
                droidMasterCharSid, npcClassId
            );
        } else {
            const std::uint32_t characterDatabaseId = user->GetDBID();
            const std::uint32_t accountId = user->GetAccountID();
            npc->m_isDroidNPC = true;
            npc->m_nDroidNPCAccountID = accountId;
            npc->m_nDroidNPCCharDBID = characterDatabaseId;
        }
    }

    NpcServer::NPCCreateParam createParameter{};
    createParameter.pNPC = npc;
    createParameter.pDefine = spawnDefine;
    createParameter.makerId = makerId;
    createParameter.pos = FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    createParameter.dir = direction;
    createParameter.aiName = const_cast<wchar_t*>(hostAiName);
    createParameter.paramCount = parameterCount;
    createParameter.bufLen = bufferLength;
    createParameter.pBuf = parameterBuffer.data();
    createParameter.inzoneSID = inZoneSid;
    g_npcserver.SendNPCCreatePacket(createParameter);
    npc->OnCreated();
    runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_CreatePrivate);
    return false;
}

bool NpcInstantTeleport(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089e494, NC_PROTOCOL 0x1f.
    L2_GUARD_FUNCTION();

    std::int32_t creatureSid = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t heading = 0;
    std::int32_t itemClassId = 0;
    std::int64_t itemAmount = 0;
    std::int32_t sourceType = 0;
    std::int32_t requesterSid = 0;
    std::int32_t requesterClassId = 0;
    std::uint8_t targetIsUser = 0;
    char16_t targetName[25]{};
    std::int32_t oldX = 0;
    std::int32_t oldY = 0;
    std::int32_t oldZ = 0;
    std::int32_t inZoneId = 0;
    ::Disassemble(
        packet, "ddddddQdddcSdddd", &creatureSid, &x, &y, &z, &heading, &itemClassId, &itemAmount, &sourceType,
        &requesterSid, &requesterClassId, &targetIsUser, 0x32, targetName, &oldX, &oldY, &oldZ, &inZoneId
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (creature == nullptr) {
        std::wstring hostTargetNameStorage;
        const wchar_t* const hostTargetName = TargetUtf16ToHostWide(targetName, hostTargetNameStorage);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Invalid Creature[%d:%s(%s)] "
        //     L"(%d, %d, %d) -> (%d, %d, %d) type:%d "
        //     L"requester[%d:%d]",
        //     "NpcSocket.cpp",
        //     0xdd4,
        //     creatureSid,
        //     targetIsUser == 1
        //         ? L"User"
        //         : L"NPC",
        //     hostTargetName,
        //     oldX,
        //     oldY,
        //     oldZ,
        //     x,
        //     y,
        //     z,
        //     sourceType,
        //     requesterSid,
        //     requesterClassId)
        // Noise: replace shipping coordinates with the failed teleport lookup. Retain request details.
        L2_LOG_ERROR(
            Log,
            "[teleport] Target SID %d (%ls '%ls') was not found. "
            "Requested position (%d, %d, %d) -> (%d, %d, %d), source type %d, "
            "requester SID %d, requester class %d. Teleport request rejected.",
            creatureSid, targetIsUser == 1 ? L"User" : L"NPC", hostTargetName, oldX, oldY, oldZ, x, y, z, sourceType,
            requesterSid, requesterClassId
        );
        return false;
    }

    const int classId = creature->m_data->m_nClassID;
    if (classId == 0xfbbc9 || classId == 0xfbbd6) {
        const FVector position = creature->GetPos();
        Log.Add(
            LOG_AUDIT,
            u"%I64d,,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,,,,,,,,,,,,",
            static_cast<std::int64_t>(0x588), static_cast<std::int64_t>(classId - 1000000),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<std::int64_t>(x),
            static_cast<std::int64_t>(y), static_cast<std::int64_t>(z)
        );
    }

    if (itemClassId > 0 && itemAmount > 0) {
        if (creature->IsPC()) {
            FVector destination{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
            creature->CastUser()->TeleportToLocationWithItem(destination, heading, itemClassId, itemAmount);
        }
        return false;
    }

    if (inZoneId != 0 && (!creature->IsPC() || static_cast<std::int32_t>(creature->GetInZoneID()) != inZoneId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     "pCreature is not User or inzone not equal [%d]/[%d]",
        //     static_cast<int>(creature->GetInZoneID()),
        //     inZoneId)
        // Previous: L2_LOG_ERROR(Log,
        //     "pCreature is not User or inzone not equal [%d]/[%d]",
        //     static_cast<int>(creature->GetInZoneID()), inZoneId)
        L2_LOG_ERROR(
            Log,
            "[NPC teleport] Teleport rejected: the creature is not a player "
            "or its instance does not match (current %d, requested %d).",
            static_cast<int>(creature->GetInZoneID()), inZoneId
        );
        return false;
    }

    creature->TeleportToLocation(
        x, y, z, creature->GetInZoneID(), heading, static_cast<TeleportSourceType::Enum>(sourceType), nullptr, 0
    );
    return false;
}

bool NpcInstantTeleportInMyTerritory(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b8234, NC_PROTOCOL 0x20.
    L2_GUARD_FUNCTION();

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(const NpcInstantTeleportTerritoryContext& context) : context_(&context) {}

        bool CanDo(CObject* object, double) override {
            User* user = object->GetUserOrMaster();
            if (user == nullptr || object->IsDeletingObject() || object->m_Deleted != 0) {
                return true;
            }

            user = object->GetUserOrMaster();
            CParty* const party = user->GetParty();
            if (context_->condition == 1) {
                if (party == nullptr || context_->flags != static_cast<std::int32_t>(party->m_nId)) {
                    return true;
                }
            } else if (context_->condition == 2 && party != nullptr &&
                context_->flags == static_cast<std::int32_t>(party->m_nId)) {
                return true;
            }

            if (!IsInTerritory(context_->territories, context_->bannedTerritories, user->m_data->GetPos())) {
                return true;
            }

            std::int32_t destinationX = context_->targetX;
            std::int32_t destinationY = context_->targetY;
            std::int32_t destinationZ = context_->targetZ;
            for (std::int32_t attempt = 0; attempt < 5; ++attempt) {
                destinationX = runtime::AddInt32Wrapping(
                    runtime::SubtractInt32Wrapping(context_->targetX, context_->targetHeading),
                    NpcSocketRandomOneTo(runtime::AddInt32Wrapping(context_->targetHeading, context_->targetHeading))
                );
                destinationY = runtime::AddInt32Wrapping(
                    runtime::SubtractInt32Wrapping(context_->targetY, context_->targetHeading),
                    NpcSocketRandomOneTo(runtime::AddInt32Wrapping(context_->targetHeading, context_->targetHeading))
                );

                destinationZ = CWorld::Inst().GetGeo()->FindGround(
                    destinationX, destinationY, runtime::AddInt32Wrapping(context_->targetZ, context_->targetHeading)
                );
                CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
                    (runtime::AddInt32Wrapping(destinationX, 0x50000) >> 15) + 10,
                    (runtime::AddInt32Wrapping(destinationY, 0x40000) >> 15) + 10
                );
                if (zone != nullptr && destinationZ < runtime::AddInt32Wrapping(context_->targetZ, 100) &&
                    runtime::AddInt32Wrapping(context_->targetZ, -100) < destinationZ) {
                    break;
                }

                if (attempt == 4) {
                    destinationX = context_->targetX;
                    destinationY = context_->targetY;
                    destinationZ = context_->targetZ;
                }
            }

            if (context_->bottom <= runtime::Win64CvttDoubleToInt32(user->m_data->GetPos().z) &&
                runtime::Win64CvttDoubleToInt32(user->m_data->GetPos().z) <= context_->top) {
                FAngle direction = user->GetDir();
                direction.Yaw = static_cast<std::uint16_t>(direction.Yaw);
                user->TeleportToLocation(
                    destinationX, destinationY, destinationZ, user->GetInZoneID(), direction.Yaw,
                    TeleportSourceType::NpcInstantTeleportInMyTerritory, nullptr, 0
                );
            }
            return true;
        }

    private:
        const NpcInstantTeleportTerritoryContext* context_;
    };

    std::int32_t npcIndex = 0;
    NpcInstantTeleportTerritoryContext context{};
    std::int32_t west = 0;
    std::int32_t north = 0;
    std::int32_t east = 0;
    std::int32_t south = 0;
    const unsigned char* const territoryPacket = ::Disassemble(
        packet, "ddddddddddddd", &npcIndex, &context.targetX, &context.targetY, &context.targetZ, &context.targetHeading,
        &context.condition, &context.flags, &west, &north, &context.bottom, &east, &south, &context.top
    );

    CSPointer<CCreature> source = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    DisassembleTerritoryInfo(territoryPacket, context.territories, context.bannedTerritories);
    if (!source) {
        return false;
    }

    CTLMFilterRange filter(static_cast<int>(source->GetInZoneID()), west, east, north, south);
    TLMFunc visitor(context);
    FOR_ALL_NEIGHBOR_CREATURE_RANGE(&filter, &visitor);

    for (CTerritory* const territory : context.bannedTerritories) {
        territory->DeleteTerritory();
    }
    for (CTerritory* const territory : context.territories) {
        territory->DeleteTerritory();
    }
    return false;
}

bool NpcKillMe(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a98a8, NC_PROTOCOL 0x21.
    L2_GUARD_FUNCTION();

    std::int32_t summonIndex;
    std::int32_t notifyMaster;
    ::Disassemble(packet, "dd", &summonIndex, &notifyMaster);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(summonIndex);
    if (creature && creature->m_data->m_bIsAlive != 0 && creature->IsSummonOrPet()) {
        CSummon* const summon = creature->CastSummon();
        User* const master = summon->GetMaster();
        if (master != nullptr) {
            const int databaseIndex = summon->IsPet() ? static_cast<int>(summon->GetDBID()) : summon->m_nIndexForMaster;
            summon->DeleteInfectedSkills(nullptr);
            db.RequestDeleteSummon(*master, databaseIndex, nullptr);
            if (notifyMaster != 0) {
                master->GetSocket()->SendSystemMessage(SystemMessageId::SERVITOR_DISAPPEARED_SUMMON_TIME_EXPIRED);
            }
            if (!summon->IsPet()) {
                master->UnlinkSummon(summon);
            } else {
                master->m_data->m_pPet.id = -1;
                master->m_data->m_pPet.index = -1;
            }
        }
        summon->Delete();
    }
    return false;
}

bool NpcPledgeLevelUp(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a9afc, NC_PROTOCOL 0x22.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t pledgeDatabaseId;
    std::int32_t pledgeLevel;
    ::Disassemble(packet, "dddd", &npcIndex, &userIndex, &pledgeDatabaseId, &pledgeLevel);
    static_cast<void>(npcIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!creature || !creature->IsPC() || creature->m_data->m_bIsAlive == 0) {
        return false;
    }

    User* const user = creature->CastUser();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || static_cast<std::int32_t>(pledge->GetDbId()) != pledgeDatabaseId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_LEVEL_INCREASE_FAILED);
        return false;
    }

    pledge->SetPledgeLevel(static_cast<unsigned int>(pledgeLevel), true);
    pledge->UpdateInfoWindowPacket();

    if (pledgeLevel == 4) {
        {
            L2_GUARD(*user->m_lock);
            user->AcquireSkill(0xf7, 1, 0, false, true);
            user->AcquireSkill(0x3bb3, 1, 0, false, true);
            user->AcquireSkill(0x3bb4, 1, 0, false, true);
            user->AcquireSkill(0x3bb5, 1, 0, false, true);
        }
        user->ValidateChanged(std::int64_t{VB_MAX});
    } else if (pledgeLevel == 5) {
        {
            L2_GUARD(*user->m_lock);
            user->AcquireSkill(0x4a5a, 1, 0, false, true);
            user->AcquireSkill(0x4a5b, 1, 0, false, true);
        }
        user->ValidateChanged(std::int64_t{VB_MAX});
    }

    user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_LEVEL_INCREASED);
    return false;
}

bool NpcViewSiegeList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a9e0c, NC_PROTOCOL 0x24.
    L2_GUARD_FUNCTION();

    std::int32_t userIndex;
    std::int32_t ignoredNpcIndex;
    std::uint32_t residenceId;
    char16_t pledgeName[256]{};
    ::Disassemble(packet, "dddS", &userIndex, &ignoredNpcIndex, &residenceId, 0x200, pledgeName);
    static_cast<void>(ignoredNpcIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC() && creature->m_data->m_bIsAlive != 0) {
        db.RequestViewSiegeList(creature->CastUser(), residenceId, pledgeName);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid castle master[%d]",
        //     userIndex)
        L2_LOG_ERROR(
            Log, "[Siege] List request for residence %u rejected: user index %d is missing, is not a player, or is dead.",
            static_cast<unsigned>(residenceId), userIndex
        );
    }
    return false;
}

bool NpcApproveBattle(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aa020, NC_PROTOCOL 0x25.
    L2_GUARD_FUNCTION();

    std::int32_t userIndex;
    std::int32_t ignoredNpcIndex;
    std::uint32_t residenceId;
    char16_t pledgeName[256]{};
    ::Disassemble(packet, "dddS", &userIndex, &ignoredNpcIndex, &residenceId, 0x200, pledgeName);
    static_cast<void>(ignoredNpcIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC() && creature->m_data->m_bIsAlive != 0) {
        db.RequestApproveBattle(creature->CastUser(), residenceId, pledgeName);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid castle master[%d]",
        //     userIndex)
        L2_LOG_ERROR(
            Log, "[Siege] Battle approval for residence %u rejected: user index %d is missing, is not a player, or is dead.",
            static_cast<unsigned>(residenceId), userIndex
        );
    }
    return false;
}

bool NpcCastleGateOpenClose(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a789c, NC_PROTOCOL 0x26.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    std::int32_t closeGate;
    ::Disassemble(packet, "dd", &residenceId, &closeGate);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid castle[%d]",
        //     residenceId)
        L2_LOG_ERROR(
            Log, "[Doors] Cannot change castle gate state: residence %d was not found (close request %d).", residenceId,
            closeGate
        );
    } else if (!residence->IsCastle() || !residence->IsUnderSiege()) {
        for (CDoor* const door : residence->m_DoorList) {
            if (closeGate == 0) {
                door->Open(false, false);
            } else {
                door->Close(false, false);
            }
        }
    }
    return false;
}

bool NpcCastleGateOpenClose2(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b228, NC_PROTOCOL 0x3c.
    L2_GUARD_FUNCTION();

    char16_t doorName[50]{};
    std::int32_t open;
    std::int32_t inZoneId;
    ::Disassemble(packet, "Sdd", 100, doorName, &open, &inZoneId);

    std::wstring hostDoorName;
    const wchar_t* const name = TargetUtf16ToHostWide(doorName, hostDoorName);
    CDoor* const door = g_DoorDB.GetDoor(doorName, static_cast<std::uint32_t>(inZoneId));
    if (door == nullptr) {
        CInstantZone* instantZone = nullptr;
        CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(inZoneId));
        if (object != nullptr && object->IsInZone()) {
            instantZone = object->CastInZone();
        }
        if (instantZone == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"NC_CASTLE_GATE_OPEN_CLOSE2 invalid door[%s] open[%d] inzone_id[%d]",
            //     name,
            //     open,
            //     inZoneId)
            L2_LOG_ERROR(
                Log,
                "[Doors] Open/close request rejected: door '%ls' was not found "
                "(request value %d, instance %d; no instance object resolved).",
                name, open, inZoneId
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"NC_CASTLE_GATE_OPEN_CLOSE2 invalid door[%s] open[%d] inzone_id[%d][%d]",
            //     name,
            //     open,
            //     inZoneId,
            //     instantZone->m_nTypeId)
            L2_LOG_ERROR(
                Log,
                "[Doors] Open/close request rejected: door '%ls' was not found "
                "(request value %d, instance %d, instance type %d).",
                name, open, inZoneId, instantZone->m_nTypeId
            );
        }
        return false;
    }

    CResidence* const residence = door->m_pOwningResidence;
    if (residence != nullptr && residence->IsCastle() && residence->IsUnderSiege()) {
        return false;
    }
    if (open == 0) {
        door->Open(false, false);
    } else {
        door->Close(false, false);
    }
    return false;
}

bool InzoneDoorBreakable(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b108, NC_PROTOCOL 0xc3.
    L2_GUARD_FUNCTION();

    char16_t doorName[50]{};
    std::uint32_t inZoneId = 0;
    ::Disassemble(packet, "Sd", 100, doorName, &inZoneId);

    std::wstring hostDoorName;
    const wchar_t* const name = TargetUtf16ToHostWide(doorName, hostDoorName);
    CDoor* const door = g_DoorDB.GetDoor(doorName, inZoneId);
    if (door == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NC_INZONE_DOOR_BREAKABLE invalid door[%s]",
        //     name)
        L2_LOG_ERROR(
            Log, "[Doors] Cannot make door '%ls' breakable: it was not found in instance %u.", name,
            static_cast<unsigned>(inZoneId)
        );
    } else {
        door->SetBreakable(true);
    }
    return false;
}

bool NpcShowSystemMessage(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089eecc, NC_PROTOCOL 0x27.
    L2_GUARD_FUNCTION();

    std::int32_t first;
    std::int32_t userSid;
    std::int32_t third;
    std::int32_t mode;
    const unsigned char* cursor = ::Disassemble(packet, "dddd", &first, &userSid, &third, &mode);

    if (first == -1 && userSid == -1 && third == -1) {
        std::int32_t payloadSize;
        cursor = ::Disassemble(cursor, "d", &payloadSize);
        CUserBroadcaster::inst().BroadcastToAllUser("cb", 0x62, payloadSize, cursor);
        return false;
    }

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        return false;
    }

    if (mode == 1) {
        std::int16_t messageId;
        ::Disassemble(cursor, "h", &messageId);
        user->GetSocket()->SendSystemMessage(messageId);
    } else if (mode == 2) {
        char16_t message[0x5000];
        ::Disassemble(cursor, "S", 0xa000, message);
        user->GetSocket()->Send("chccS", 0x62, SystemMessageId::NPC_SYSTEM_MESSAGE_TEXT, 1, 0, message);
    }
    return false;
}

bool NpcShowSystemMessageFStr(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089f0fc, NC_PROTOCOL 0x28.
    L2_GUARD_FUNCTION();

    std::int32_t first;
    std::int32_t userSid;
    std::int32_t third;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &first, &userSid, &third);

    User* user = nullptr;
    if (first != -1 || userSid != -1 || third != -1) {
        user = FindUserBySid(userSid);
        if (user == nullptr) {
            return false;
        }
    }

    std::int32_t stringId;
    char16_t text1[256];
    char16_t text2[256];
    char16_t text3[256];
    char16_t text4[256];
    char16_t text5[256];
    ::Disassemble(cursor, "dSSSSS", &stringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4, 0x200, text5);

    if (user == nullptr) {
        CUserBroadcaster::inst().BroadcastToAllUser(
            "chccdSSSSS", 0x62, 0x7c3, 1, 0x0e, stringId, text1, text2, text3, text4, text5
        );
    } else {
        user->GetSocket()->Send(
            "chccdSSSSS", 0x62, SystemMessageId::NPC_SYSTEM_MESSAGE_TEXT, 1, 0x0e, stringId, text1, text2, text3, text4,
            text5
        );
    }
    return false;
}

bool NpcBroadcastSystemMessage(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b1fd4, NC_PROTOCOL 0x29.
    L2_GUARD_FUNCTION();

    std::int32_t creatureSid;
    std::int32_t ignored;
    std::int32_t radius;
    std::int32_t mode;
    const unsigned char* cursor = ::Disassemble(packet, "dddd", &creatureSid, &ignored, &radius, &mode);
    static_cast<void>(ignored);

    CCreature* const creature = FindCreatureBySid(creatureSid);
    if (creature == nullptr) {
        return false;
    }

    if (mode == 1) {
        std::int32_t rawMessageId = 0;
        ::Disassemble(cursor, "d", &rawMessageId);
        const std::int32_t messageId = static_cast<std::int16_t>(rawMessageId);
        if (radius == 0) {
            CUserBroadcaster::inst().BroadcastToAllUserEx("chc", 0x62, messageId, 0);
        } else {
            g_ptlUserMap->BroadcastToNeighborDist(creature, 0xf000, creature->GetPos(), radius, "chc", 0x62, messageId, 0);
        }
    } else if (mode == 2) {
        char16_t message[0x5000];
        ::Disassemble(cursor, "S", 0xa000, message);
        if (radius == 0) {
            CUserBroadcaster::inst().BroadcastToAllUserEx("chccS", 0x62, 0x7c3, 1, 0, message);
        } else {
            g_ptlUserMap->BroadcastToNeighborDist(
                creature, 0xf000, creature->GetPos(), radius, "chccS", 0x62, 0x7c3, 1, 0, message
            );
        }
    }
    return false;
}

bool NpcBroadcastSystemMessageFStr(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b2260, NC_PROTOCOL 0x2a.
    L2_GUARD_FUNCTION();

    std::int32_t creatureSid;
    std::int32_t ignored;
    std::int32_t radius;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &creatureSid, &ignored, &radius);
    static_cast<void>(ignored);

    CCreature* const creature = FindCreatureBySid(creatureSid);
    if (creature == nullptr) {
        return false;
    }

    std::int32_t stringId;
    char16_t text1[256];
    char16_t text2[256];
    char16_t text3[256];
    char16_t text4[256];
    char16_t text5[256];
    ::Disassemble(cursor, "dSSSSS", &stringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4, 0x200, text5);

    if (radius == 0) {
        CUserBroadcaster::inst().BroadcastToAllUserEx(
            "chccdSSSSS", 0x62, 0x7c3, 1, 0x0e, stringId, text1, text2, text3, text4, text5
        );
    } else {
        g_ptlUserMap->BroadcastToNeighborDist(
            creature, 0xf000, creature->GetPos(), radius, "chccdSSSSS", 0x62, 0x7c3, 1, 0x0e, stringId, text1, text2, text3,
            text4, text5
        );
    }
    return false;
}

bool NpcDummy(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid packet index[%d]",
    //     0xff)
    L2_LOG_ERROR(
        Log,
        "[NPC protocol] NpcDummy invoked; the packet is ignored. "
        "This handler does not decode an opcode."
    );
    return false;
}

bool NpcVersionPacket(NpcSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x008b8930, NC_PROTOCOL 0x00.
    L2_GUARD_FUNCTION();

    std::int32_t npcProtocolVersion = 0;
    std::int32_t npcSharedFactoryVersion = 0;
    std::int32_t npcSharedDataSize = 0;
    ::Disassemble(packet, "ddd", &npcProtocolVersion, &npcSharedFactoryVersion, &npcSharedDataSize);
    if (npcProtocolVersion == -1) {
        return false;
    }

    char revisionTag[80]{};
    std::int32_t l2ServerProtocolVersion = 0;
    std::int32_t l2ServerSharedFactoryVersion = 0;
#if defined(_MSC_VER)
    ::sscanf_s(
        "$Revision: #272 $", "%s #%d", revisionTag, static_cast<unsigned>(sizeof(revisionTag)), &l2ServerProtocolVersion
    );
    ::sscanf_s(
        "$Revision: #289 $", "%s #%d", revisionTag, static_cast<unsigned>(sizeof(revisionTag)), &l2ServerSharedFactoryVersion
    );
#else
    std::sscanf("$Revision: #272 $", "%79s #%d", revisionTag, &l2ServerProtocolVersion);
    std::sscanf("$Revision: #289 $", "%79s #%d", revisionTag, &l2ServerSharedFactoryVersion);
#endif

    const bool protocolMatches = npcProtocolVersion == l2ServerProtocolVersion;
    const bool sharedFactoryMatches = npcSharedFactoryVersion == l2ServerSharedFactoryVersion;
    const bool sharedDataMatches = npcSharedDataSize == 0x4b88;

    // The target reparses the protocol revision immediately before sending.
#if defined(_MSC_VER)
    ::sscanf_s(
        "$Revision: #272 $", "%s #%d", revisionTag, static_cast<unsigned>(sizeof(revisionTag)), &l2ServerProtocolVersion
    );
#else
    std::sscanf("$Revision: #272 $", "%79s #%d", revisionTag, &l2ServerProtocolVersion);
#endif

    socket->Send(
        "ccccddddd", 0, protocolMatches, sharedFactoryMatches, sharedDataMatches, l2ServerProtocolVersion, g_nServerId,
        g_L2ServerType, l2ServerSharedFactoryVersion, 0x4b88
    );
    g_L2Time.NpcPingStart();

    if (protocolMatches && sharedFactoryMatches && sharedDataMatches) {
        g_UserNotifier.Notify();
        return false;
    }

    if (!protocolMatches) {
        std::array<char16_t, 0x200> message{};
        FormatTargetText(
            message,
            // Original: L"[MISMATCH] NPCProtocol NPC[%d] L2SERVER[%d] server type[%d]",
            u"NPC protocol mismatch: L2NPC version %d, L2Server version %d (server type %d).", npcProtocolVersion,
            l2ServerProtocolVersion, g_L2ServerType
        );
        // Log.Add(LOG_ERROR, message.data())
        // Log.Add(LOG_ERROR, L"%ls", message.data())
        L2_LOG_ERROR(Log, u"%ls", message.data());
        // Noise: The error is already logged above. Only Windows needs a modal copy.
        // PopUpWindow(message.data())
#if defined(_WIN32)
        PopUpWindow(L"%ls", Utf16ToWide(message.data()).c_str());
#endif
    }
    if (!sharedFactoryMatches) {
        std::array<char16_t, 0x200> message{};
        FormatTargetText(
            message,
            // Original: L"[MISMATCH] SharedFactoryVersion NPC[%d] L2SERVER[%d] server type[%d]",
            u"Shared factory version mismatch: L2NPC version %d, L2Server version %d (server type %d).",
            npcSharedFactoryVersion, l2ServerSharedFactoryVersion, g_L2ServerType
        );
        // Log.Add(LOG_ERROR, message.data())
        // Log.Add(LOG_ERROR, L"%ls", message.data())
        L2_LOG_ERROR(Log, u"%ls", message.data());
        // Noise: The error is already logged above. Only Windows needs a modal copy.
        // PopUpWindow(message.data())
#if defined(_WIN32)
        PopUpWindow(L"%ls", Utf16ToWide(message.data()).c_str());
#endif
    }
    if (!sharedDataMatches) {
        std::array<char16_t, 0x200> message{};
        FormatTargetText(
            message,
            // Original: L"[MISMATCH] Shared Data Size NPC[%d] L2SERVER[%d] server type[%d]",
            u"Shared data size mismatch: L2NPC %d bytes, L2Server %d bytes (server type %d).", npcSharedDataSize, 0x4b88,
            g_L2ServerType
        );
        // Log.Add(LOG_ERROR, message.data())
        // Log.Add(LOG_ERROR, L"%ls", message.data())
        L2_LOG_ERROR(Log, u"%ls", message.data());
        // Noise: The error is already logged above. Only Windows needs a modal copy.
        // PopUpWindow(message.data())
#if defined(_WIN32)
        PopUpWindow(L"%ls", Utf16ToWide(message.data()).c_str());
#endif
    }
    return true;
}

bool NpcCreatePacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b6698, NC_PROTOCOL 0x01.
    L2_GUARD_FUNCTION();

    char16_t npcName[256]{};
    void* spawnDefine = nullptr;
    std::int32_t pledgeId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t direction = 0;
    std::int32_t hp = 0;
    std::int32_t mp = 0;
    std::int32_t databaseValue = 0;
    std::int32_t inZoneId = 0;
    std::int32_t airShipSid = 0;
    std::uint8_t fakeNpc = 0;
    std::int32_t fakeNpcSuicideTime = 0;

    const unsigned char* cursor = ::Disassemble(
        packet, "Spddddddddddcd", 0x200, npcName, &spawnDefine, &pledgeId, &x, &y, &z, &direction, &hp, &mp, &databaseValue,
        &inZoneId, &airShipSid, &fakeNpc, &fakeNpcSuicideTime
    );

    std::int32_t makerParam1 = 0;
    std::int32_t makerParam2 = 0;
    std::int32_t makerParam3 = 0;
    cursor = ::Disassemble(cursor, "ddd", &makerParam1, &makerParam2, &makerParam3);

    std::uint8_t droidNpc = 0;
    std::int32_t droidAccountId = 0;
    std::int32_t droidCharacterDatabaseId = 0;
    cursor = ::Disassemble(cursor, "cdd", &droidNpc, &droidAccountId, &droidCharacterDatabaseId);

    std::uint8_t decoNpcType = 0;
    std::int32_t residenceOrPledgeId = 0;
    std::int32_t agitSlot = 0;
    cursor = ::Disassemble(cursor, "cdd", &decoNpcType, &residenceOrPledgeId, &agitSlot);

    std::int32_t aliveHour = 0;
    ::Disassemble(cursor, "d", &aliveHour);

    const int npcClassId = g_ObjectDB.GetClassIdFromName(npcName, false);
    if (npcClassId < 0) {
        std::wstring hostNpcName;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid npc name [%s] [pledgeId:%d][x:%d][y:%d][z:%d][dir:%d][hp:%d][mp:%d]",
        //     L"NpcSocket.cpp",
        //     0x163,
        //     TargetUtf16ToHostWide(npcName, hostNpcName),
        //     pledgeId,
        //     x,
        //     y,
        //     z,
        //     direction,
        //     hp,
        //     mp)
        // Noise: replace shipping coordinates with the failed lookup and rejected action.
        L2_LOG_ERROR(
            Log,
            "[NPC spawn] Creation rejected: name '%ls' did not resolve to a valid class ID. "
            "Pledge %d, position (%d, %d, %d), direction %d, HP %d, MP %d.",
            TargetUtf16ToHostWide(npcName, hostNpcName), pledgeId, x, y, z, direction, hp, mp
        );
        return false;
    }

    if (inZoneId != 0) {
        CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(inZoneId));
        CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
        if (instantZone == nullptr || instantZone->IsDeleting() || instantZone->m_Deleted != 0) {
            return false;
        }
    }

    CNPC* npc = nullptr;
    {
        LagAlert lagAlert(5000, "NpcCreatePacket", 0x177);
        npc = g_ObjectDB.CreateNPC(npcClassId, fakeNpc == 0);
    }

    if (npc == nullptr) {
        std::wstring hostNpcName;
        // Log.Add(
        //     LOG_ERROR,
        //     L"There is no NPC named [%s] at file[%s], line[%d]",
        //     TargetUtf16ToHostWide(npcName, hostNpcName),
        //     L"NpcSocket.cpp",
        //     0x185)
        // Noise: replace shipping coordinates. Creation failure does not prove a missing name.
        L2_LOG_ERROR(
            Log, "[NPC spawn] NpcCreatePacket stopped: CreateNPC returned null for '%ls' (class %d).",
            TargetUtf16ToHostWide(npcName, hostNpcName), npcClassId
        );
        return false;
    }

    if (pledgeId != 0) {
        npc->m_data->SetPledgeID(static_cast<std::uint32_t>(pledgeId));
    }

    const int aiClassId = fakeNpc == 0 ? npc->m_nClassId : 0x0f9061;
    const wchar_t* const aiName = g_ObjectDB.GetAI(aiClassId);
    if (aiName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "AI does not specified [clsid=%d]",
        //     npc->m_nClassId)
        // Previous: L2_LOG_ERROR(Log,
        //     "AI does not specified [clsid=%d]", npc->m_nClassId)
        L2_LOG_ERROR(Log, "[NPC spawn] NPC class %d has no AI name; creation is rejected.", npc->m_nClassId);
        return false;
    }

    std::array<char, 0x2400> parameterBuffer;
    int parameterCount = 0;
    int bufferLength = 0;
    for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            parameterBuffer.data() + bufferLength, static_cast<int>(parameterBuffer.size()) - bufferLength, "SS",
            parameter.first.c_str(), parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    npc->m_data->m_nDBValue = databaseValue;
    npc->m_data->m_nMakerParam1 = makerParam1;
    npc->m_data->m_nMakerParam2 = makerParam2;
    npc->m_data->m_nMakerParam3 = makerParam3;
    npc->m_nAirShipServerID = static_cast<std::uint32_t>(airShipSid);
    npc->m_nFakeNpcSuicideTime = fakeNpcSuicideTime;
    npc->SetDroidNPCInfo(
        droidNpc == 1, static_cast<std::uint32_t>(droidAccountId), static_cast<std::uint32_t>(droidCharacterDatabaseId)
    );

    if (decoNpcType == 1) {
        CResidence* const residence = g_ResidenceDB.GetResidence(residenceOrPledgeId);
        if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Deco Npc is created in none residence : %d",
            //     residenceOrPledgeId)
            L2_LOG_ERROR(
                Log,
                "[NPC decoration] Creation rejected for NPC class %d: residence %d was not found. "
                "Deleting the newly created NPC.",
                npcClassId, residenceOrPledgeId
            );
            npc->Delete();
            return false;
        }
        if (!residence->SetDecoNpc(agitSlot, static_cast<int>(npc->Id()))) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Set Deco Npc is failed : %d, %d",
            //     residenceOrPledgeId,
            //     agitSlot)
            L2_LOG_ERROR(
                Log,
                "[NPC decoration] SetDecoNpc failed for residence %d, slot %d. "
                "Deleting the newly created NPC.",
                residenceOrPledgeId, agitSlot
            );
            npc->Delete();
            return false;
        }
        npc->SetDecoNpcInfo(NpcTypeForDeco::DecoNpcInRealWorld, residenceOrPledgeId, agitSlot);
    } else if (decoNpcType == 2) {
        if (!CInstantAgitManager::inst().OnSetDecoNpcFromNPCServer(
                residenceOrPledgeId, agitSlot, static_cast<int>(npc->Id())
            )) {
            npc->Delete();
            return false;
        }
        npc->SetDecoNpcInfo(NpcTypeForDeco::DecoNpcInInstantAgit, residenceOrPledgeId, agitSlot);
    }

    npc->SetAliveHour(aliveHour);
    if (aliveHour > 0) {
        const std::int32_t reservationDate = CReservationNPCSpawnTimer::GetReservationDate(aliveHour);
        CReservationNPCSpawnTimer::inst().RegisterReservationInfo(
            RESERVATION_NPC_DESPAWN, npc->Id(), reservationDate, FVector{}, 0, 0, aliveHour
        );
    }

    NpcServer::NPCCreateParam createParameter{};
    createParameter.pNPC = npc;
    createParameter.pDefine = spawnDefine;
    createParameter.pos = FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    createParameter.dir = direction;
    createParameter.hp = hp;
    createParameter.mp = mp;
    createParameter.aiName = const_cast<wchar_t*>(aiName);
    createParameter.paramCount = parameterCount;
    createParameter.bufLen = bufferLength;
    createParameter.pBuf = parameterBuffer.data();
    createParameter.airshipSID = airShipSid;
    createParameter.inzoneSID = inZoneId;
    g_npcserver.SendNPCCreatePacket(createParameter);
    npc->OnCreated();
    return false;
}

void NpcEnterWorld_Do(
    const int index,
    FVector* const position,
    const int dir,
    const int setDefaultParam,
    const int hp,
    const int mp,
    const int inzoneSid
) {
    // Current L2Server 0x008b6f18.
    g_EnterWorldSerializer.Push(
        index, static_cast<int>(position->x), static_cast<int>(position->y), static_cast<int>(position->z), dir,
        setDefaultParam, hp, mp, static_cast<unsigned int>(inzoneSid)
    );
    ::runtime::raw_atomic::IncrementI32(&g_nNPCCount);
}

bool NpcEnterWorldPacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b8c80, NC_PROTOCOL 0x02.
    L2_GUARD_FUNCTION();

    std::int32_t index = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t dir = 0;
    std::int32_t setDefaultParam = 0;
    std::int32_t hp = 0;
    std::int32_t mp = 0;
    std::int32_t inzoneSid = 0;
    ::Disassemble(packet, "ddddddddd", &index, &x, &y, &z, &dir, &setDefaultParam, &hp, &mp, &inzoneSid);
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(index);
    if (creature && creature->IsNPC()) {
        CSPointer<CNPC> npc(creature->CastNPC(), ObjectRefType::FunctionLocal);
        if (npc) {
            if (npc->IsDroidNPC()) {
                const std::uint32_t accountId = npc->m_nDroidNPCAccountID;
                const std::uint32_t characterDatabaseId = npc->m_nDroidNPCCharDBID;
                const FVector position(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
                if (npc->m_isDroidNPC) {
                    npc->m_vDroidSpawnPos = position;
                    npc->m_nDroidSpawnDir = dir;
                    npc->m_nDroidSpawnDefaultParam = setDefaultParam;
                    npc->m_nDroidSpawnDefaultHP = hp;
                    npc->m_nDroidSpawnDefaultMP = mp;
                    npc->m_nDroidSpawnInZoneID = inzoneSid;
                }
                db.RequestLoadDroidNPCAppearance(
                    static_cast<DROIDNPC_COMMAND::DROIDNPC_COMMAND_TYPE>(0), npc->m_nId, accountId, characterDatabaseId
                );
                return false;
            }

            if (npc->IsInRealWorld()) {
                const int classId = npc->GetClassId();
                const int npcClass = npc->GetClass();
                if (classId == 0x0f5314 || classId == 0x0f5326 || classId == 0x0f76e0 || classId == 0x0fc71b ||
                    classId == 0x0fc876 || npcClass == 0x51) {
                    RelayClient::Inst()->SendSpawnNPCPacket(npc->GetClassId());
                }
            }
        }
    }

    FVector position(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
    NpcEnterWorld_Do(index, &position, dir, setDefaultParam, hp, mp, inzoneSid);
    return false;
}

using NpcPacketCallback = bool (*)(NpcSocket*, const unsigned char*);

FixedSizeCallbackMap<255, NpcPacketCallback> gPacketTableFromNpc(&NpcDummy, u"NC_PROTOCOL");

L2_BIND_PACKET(kBindNpcVersionPacket, gPacketTableFromNpc, 0x00, &NpcVersionPacket);

L2_BIND_PACKET(kBindNpcCreatePacket, gPacketTableFromNpc, 0x01, &NpcCreatePacket);

L2_BIND_PACKET(kBindNpcEnterWorldPacket, gPacketTableFromNpc, 0x02, &NpcEnterWorldPacket);

L2_BIND_PACKET(kBindNpcMovePacket, gPacketTableFromNpc, 0x03, &NpcMovePacket);

L2_BIND_PACKET(kBindNpcSayPacket, gPacketTableFromNpc, 0x04, &NpcSayPacket);

L2_BIND_PACKET(kBindNpcSayFStringPacket, gPacketTableFromNpc, 0x05, &NpcSayFStringPacket);

L2_BIND_PACKET(kBindNpcAttackPacket, gPacketTableFromNpc, 0x06, &NpcAttackPacket);

L2_BIND_PACKET(kBindNpcRegisterTelPosListPacket, gPacketTableFromNpc, 0x07, &NpcRegisterTelPosListPacket);

L2_BIND_PACKET(kBindNpcTeleportPacket, gPacketTableFromNpc, 0x08, &NpcTeleportPacket);

L2_BIND_PACKET(kBindNpcRegisterBuySellListPacket, gPacketTableFromNpc, 0x09, &NpcRegisterBuySellListPacket);

L2_BIND_PACKET(kBindNpcDummy0a, gPacketTableFromNpc, 0x0a, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummy0b, gPacketTableFromNpc, 0x0b, &NpcDummy);

L2_BIND_PACKET(kBindNpcRegisterCastleSiege, gPacketTableFromNpc, 0x0c, &NpcRegisterCastleSiege);

L2_BIND_PACKET(kBindNpcUnregisterCastleSiege, gPacketTableFromNpc, 0x0d, &NpcUnregisterCastleSiege);

L2_BIND_PACKET(kBindNpcSetCastleSiegeTime, gPacketTableFromNpc, 0x0e, &NpcSetCastleSiegeTime);

L2_BIND_PACKET(kBindNpcSetTaxRate, gPacketTableFromNpc, 0x0f, &NpcSetTaxRate);

L2_BIND_PACKET(kBindNpcRegisterAgit, gPacketTableFromNpc, 0x10, &NpcRegisterAgit);

L2_BIND_PACKET(kBindNpcShowHTMLString, gPacketTableFromNpc, 0x11, &NpcShowHTMLString);

L2_BIND_PACKET(kBindNpcChangeMoveType, gPacketTableFromNpc, 0x12, &NpcChangeMoveType);

L2_BIND_PACKET(kBindNpcEquipItem, gPacketTableFromNpc, 0x13, &NpcEquipItem);

L2_BIND_PACKET(kBindNpcMoveToPawn, gPacketTableFromNpc, 0x14, &NpcMoveToPawn);

L2_BIND_PACKET(kBindNpcShowSkillList, gPacketTableFromNpc, 0x15, &NpcShowSkillList);

L2_BIND_PACKET(kBindNpcGrowSkillSuggestion, gPacketTableFromNpc, 0x16, &NpcGrowSkillSuggestion);

L2_BIND_PACKET(kBindNpcGetItem, gPacketTableFromNpc, 0x17, &NpcSocket::NpcGetItem);

L2_BIND_PACKET(kBindNpcClassChange, gPacketTableFromNpc, 0x18, &NpcClassChange);

L2_BIND_PACKET(kBindNpcSetWeightPoint, gPacketTableFromNpc, 0x19, &NpcSetWeightPoint);

L2_BIND_PACKET(kBindNpcLeaveWorld, gPacketTableFromNpc, 0x1a, &NpcLeaveWorld);

L2_BIND_PACKET(kBindNpcUseSkill, gPacketTableFromNpc, 0x1b, &NpcUseSkill);

L2_BIND_PACKET(kBindNpcUseItem, gPacketTableFromNpc, 0x1c, &NpcUseItem);

L2_BIND_PACKET(kBindNpcCreatePrivates, gPacketTableFromNpc, 0x1d, &NpcCreatePrivates);

L2_BIND_PACKET(kBindNpcCreateOnePrivate, gPacketTableFromNpc, 0x1e, &NpcCreateOnePrivate);

L2_BIND_PACKET(kBindNpcInstantTeleport, gPacketTableFromNpc, 0x1f, &NpcInstantTeleport);

L2_BIND_PACKET(kBindNpcInstantTeleportInMyTerritory, gPacketTableFromNpc, 0x20, &NpcInstantTeleportInMyTerritory);

L2_BIND_PACKET(kBindNpcKillMe, gPacketTableFromNpc, 0x21, &NpcKillMe);

L2_BIND_PACKET(kBindNpcPledgeLevelUp, gPacketTableFromNpc, 0x22, &NpcPledgeLevelUp);

L2_BIND_PACKET(kBindNpcPing, gPacketTableFromNpc, 0x23, &NpcPing);

L2_BIND_PACKET(kBindNpcViewSiegeList, gPacketTableFromNpc, 0x24, &NpcViewSiegeList);

L2_BIND_PACKET(kBindNpcApproveBattle, gPacketTableFromNpc, 0x25, &NpcApproveBattle);

L2_BIND_PACKET(kBindNpcCastleGateOpenClose, gPacketTableFromNpc, 0x26, &NpcCastleGateOpenClose);

L2_BIND_PACKET(kBindNpcShowSystemMessage, gPacketTableFromNpc, 0x27, &NpcShowSystemMessage);

L2_BIND_PACKET(kBindNpcShowSystemMessageFStr, gPacketTableFromNpc, 0x28, &NpcShowSystemMessageFStr);

L2_BIND_PACKET(kBindNpcBroadcastSystemMessage, gPacketTableFromNpc, 0x29, &NpcBroadcastSystemMessage);

L2_BIND_PACKET(kBindNpcBroadcastSystemMessageFStr, gPacketTableFromNpc, 0x2a, &NpcBroadcastSystemMessageFStr);

L2_BIND_PACKET(kBindNpcCheckSiege, gPacketTableFromNpc, 0x2b, &NpcCheckSiege);

L2_BIND_PACKET(kBindNpcSocialAction, gPacketTableFromNpc, 0x2c, &NpcSocialAction);

L2_BIND_PACKET(kBindNpcSocialAction2, gPacketTableFromNpc, 0x2d, &NpcSocialAction2);

L2_BIND_PACKET(kBindNpcDespawn, gPacketTableFromNpc, 0x2e, &NpcDespawn);

L2_BIND_PACKET(kBindNpcPongForCastleSiege, gPacketTableFromNpc, 0x2f, &NpcPongForCastleSiege);

L2_BIND_PACKET(kBindNpcSound, gPacketTableFromNpc, 0x30, &NpcSound);

L2_BIND_PACKET(kBindNpcCastleBanishOthers, gPacketTableFromNpc, 0x31, &NpcCastleBanishOthers);

L2_BIND_PACKET(kBindNpcCastleHpRegen, gPacketTableFromNpc, 0x32, &NpcCastleHpRegen);

L2_BIND_PACKET(kBindNpcCastleMpRegen, gPacketTableFromNpc, 0x33, &NpcCastleMpRegen);

L2_BIND_PACKET(kBindNpcChangeStopType, gPacketTableFromNpc, 0x34, &NpcChangeStopType);

L2_BIND_PACKET(kBindShowTutorialPage, gPacketTableFromNpc, 0x35, &ShowTutorialPage);

L2_BIND_PACKET(kBindShowNewTutorialPage, gPacketTableFromNpc, 0xf7, &ShowNewTutorialPage);

L2_BIND_PACKET(kBindShowTutorialMark, gPacketTableFromNpc, 0x36, &ShowTutorialMark);

L2_BIND_PACKET(kBindCloseTutorialHTML, gPacketTableFromNpc, 0x37, &CloseTutorialHTML);

L2_BIND_PACKET(kBindEnableTutorialEvent, gPacketTableFromNpc, 0x38, &EnableTutorialEvent);

L2_BIND_PACKET(kBindNpcDummy39, gPacketTableFromNpc, 0x39, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummy3a, gPacketTableFromNpc, 0x3a, &NpcDummy);

L2_BIND_PACKET(kBindRadarControl, gPacketTableFromNpc, 0x3b, &RadarControl);

L2_BIND_PACKET(kBindNpcCastleGateOpenClose2, gPacketTableFromNpc, 0x3c, &NpcCastleGateOpenClose2);

L2_BIND_PACKET(kBindNpcAtomicDelGiveItemParam, gPacketTableFromNpc, 0x3d, &NpcAtomicDelGiveItemParam);

L2_BIND_PACKET(kBindNpcEarthquake, gPacketTableFromNpc, 0x3e, &NpcEarthquake);

L2_BIND_PACKET(kBindNpcSpecialCamera, gPacketTableFromNpc, 0x3f, &NpcSpecialCamera);

L2_BIND_PACKET(kBindNpcSpecialCamera2, gPacketTableFromNpc, 0x40, &NpcSpecialCamera2);

L2_BIND_PACKET(kBindNpcNormalCamera, gPacketTableFromNpc, 0x41, &NpcNormalCamera);

L2_BIND_PACKET(kBindNpcAgitAuction, gPacketTableFromNpc, 0x42, &NpcAgitAuction);

L2_BIND_PACKET(kBindNpcOpenSiegeInfo, gPacketTableFromNpc, 0x43, &NpcOpenSiegeInfo);

L2_BIND_PACKET(kBindNpcSetDoorHpLevel, gPacketTableFromNpc, 0x44, &NpcSetDoorHpLevel);

L2_BIND_PACKET(kBindNpcGetDoorHpLevel, gPacketTableFromNpc, 0x45, &NpcGetDoorHpLevel);

L2_BIND_PACKET(kBindNpcSetControlTowerLevel, gPacketTableFromNpc, 0x46, &NpcSetControlTowerLevel);

L2_BIND_PACKET(kBindNpcGetControlTowerLevel, gPacketTableFromNpc, 0x47, &NpcGetControlTowerLevel);

L2_BIND_PACKET(kBindNpcAgitTeleportLevel, gPacketTableFromNpc, 0x48, &NpcAgitTeleportLevel);

L2_BIND_PACKET(kBindNpcMusicEffect, gPacketTableFromNpc, 0x49, &NpcMusicEffect);

L2_BIND_PACKET(kBindNpcSetResidenceTaxRate, gPacketTableFromNpc, 0x4a, &NpcSetResidenceTaxRate);

L2_BIND_PACKET(kBindNpcMoveTaxToResidenceBank, gPacketTableFromNpc, 0x4b, &NpcMoveTaxToResidenceBank);

L2_BIND_PACKET(kBindNpcMoveMoneyToResidenceBank, gPacketTableFromNpc, 0x4c, &NpcMoveMoneyToResidenceBank);

L2_BIND_PACKET(kBindNpcStaticObjectAttackPacket, gPacketTableFromNpc, 0x4d, &NpcStaticObjectAttackPacket);

L2_BIND_PACKET(kBindNpcSetResidenceTaxRateEach, gPacketTableFromNpc, 0x4e, &NpcSetResidenceTaxRateEach);

L2_BIND_PACKET(kBindNpcSetVaultEmpty, gPacketTableFromNpc, 0x4f, &NpcSetVaultEmpty);

L2_BIND_PACKET(kBindNpcRequestLookNeighbor, gPacketTableFromNpc, 0x50, &NpcRequestLookNeighbor);

L2_BIND_PACKET(kBindNpcBroadcastScriptEvent, gPacketTableFromNpc, 0x51, &NpcBroadcastScriptEvent);

L2_BIND_PACKET(kBindNpcLottoBuyTicket, gPacketTableFromNpc, 0x52, &NpcLottoBuyTicket);

L2_BIND_PACKET(kBindNpcSuicide, gPacketTableFromNpc, 0x53, &NpcSuicide);

L2_BIND_PACKET(kBindNpcSystemMessage, gPacketTableFromNpc, 0x54, &NpcSystemMessage);

L2_BIND_PACKET(kBindNpcRegisterTeamBattle, gPacketTableFromNpc, 0x55, &NpcRegisterTeamBattle);

L2_BIND_PACKET(kBindNpcQueryTeamBattleNpc, gPacketTableFromNpc, 0x56, &NpcQueryTeamBattleNpc);

L2_BIND_PACKET(kBindNpcHennaItemListOpenForEquip, gPacketTableFromNpc, 0x57, &NpcHennaItemListOpenForEquip);

L2_BIND_PACKET(kBindNpcHennaEquipListOpenForUnequip, gPacketTableFromNpc, 0x58, &NpcHennaEquipListOpenForUnequip);

L2_BIND_PACKET(kBindNpcQueryTeamBattlePledge, gPacketTableFromNpc, 0x59, &NpcQueryTeamBattlePledge);

L2_BIND_PACKET(kBindNpcShowManorMenu, gPacketTableFromNpc, 0x5a, &NpcShowManorMenu);

L2_BIND_PACKET(kBindNpcSetNextSeedInfo, gPacketTableFromNpc, 0x5b, &NpcSetNextSeedInfo);

L2_BIND_PACKET(kBindNpcSetNextProcureInfo, gPacketTableFromNpc, 0x5c, &NpcSetNextProcureInfo);

L2_BIND_PACKET(kBindNpcShowSellSeedList, gPacketTableFromNpc, 0x5d, &NpcShowSellSeedList);

L2_BIND_PACKET(kBindNpcShowProcureCropList, gPacketTableFromNpc, 0x5e, &NpcShowProcureCropList);

L2_BIND_PACKET(kBindNpcAreaSetOnOff, gPacketTableFromNpc, 0x5f, &NpcAreaSetOnOff);

L2_BIND_PACKET(kBindNpcStopMove, gPacketTableFromNpc, 0x60, &NpcStopMove);

L2_BIND_PACKET(kBindNpcShowSellPreviewPage, gPacketTableFromNpc, 0x61, &NpcShowSellPreviewPagePacket);

L2_BIND_PACKET(kBindNpcGetSubJobList, gPacketTableFromNpc, 0x62, &NpcGetSubJobList);

L2_BIND_PACKET(kBindNpcCreateSubJob, gPacketTableFromNpc, 0x63, &NpcCreateSubJob);

L2_BIND_PACKET(kBindNpcChangeSubJob, gPacketTableFromNpc, 0x64, &NpcChangeSubJob);

L2_BIND_PACKET(kBindNpcRenewSubJob, gPacketTableFromNpc, 0x65, &NpcRenewSubJob);

L2_BIND_PACKET(kBindNpcShowDirection, gPacketTableFromNpc, 0x66, &NpcShowDirection);

L2_BIND_PACKET(kBindNpcShout, gPacketTableFromNpc, 0x67, &NpcShout);

L2_BIND_PACKET(kBindNpcShoutFStringPacket, gPacketTableFromNpc, 0x68, &NpcShoutFStringPacket);

L2_BIND_PACKET(kBindNpcShowMultisellPage, gPacketTableFromNpc, 0x69, &NpcShowMultisellPage);

L2_BIND_PACKET(kBindNpcGetBattleRoyalPledgeList, gPacketTableFromNpc, 0x6a, &NpcGetBattleRoyalPledgeList);

L2_BIND_PACKET(kBindNpcJoinTimeAttack, gPacketTableFromNpc, 0x6b, &NpcJoinTimeAttack);

L2_BIND_PACKET(kBindNpcLeaveTimeAttack, gPacketTableFromNpc, 0x6c, &NpcLeaveTimeAttack);

L2_BIND_PACKET(kBindNpcGiveContribution, gPacketTableFromNpc, 0x6d, &NpcGiveContribution);

L2_BIND_PACKET(kBindNpcBroadcastToParty, gPacketTableFromNpc, 0x6e, &NpcBroadcastToParty);

L2_BIND_PACKET(kBindNpcShowTerritoryInfo, gPacketTableFromNpc, 0x6f, &NpcShowTerritoryInfo);

L2_BIND_PACKET(kBindNpcShowEnchantSkillList, gPacketTableFromNpc, 0x70, &NpcShowEnchantSkillList);

L2_BIND_PACKET(kBindNpcEnchantSkillSuggestion, gPacketTableFromNpc, 0x71, &NpcEnchantSkillSuggestion);

L2_BIND_PACKET(kBindNpcShowQuestInfo, gPacketTableFromNpc, 0x72, &NpcShowQuestInfo);

L2_BIND_PACKET(kBindNpcShowQuestMark, gPacketTableFromNpc, 0x73, &NpcShowQuestMark);

L2_BIND_PACKET(kBindNpcManorFunc, gPacketTableFromNpc, 0x74, &NpcManorFunc);

L2_BIND_PACKET(kBindNpcUseSoulSpiritShot, gPacketTableFromNpc, 0x75, &NpcUseSoulSpiritShot);

L2_BIND_PACKET(kBindNpcLookItems, gPacketTableFromNpc, 0x76, &NpcLookItems);

L2_BIND_PACKET(kBindNpcShowTelposList, gPacketTableFromNpc, 0x77, &NpcShowTelposList);

L2_BIND_PACKET(kBindNpcAnnouncePacket, gPacketTableFromNpc, 0x78, &NpcAnnouncePacket);

L2_BIND_PACKET(kBindNpcOlympiadFunc, gPacketTableFromNpc, 0x79, &NpcOlympiadFunc);

L2_BIND_PACKET(kBindSendBufferToUser, gPacketTableFromNpc, 0x7a, &SendBufferToUser);

L2_BIND_PACKET(kBindDeletePreviousOlympiadPoint, gPacketTableFromNpc, 0x7b, &DeletePreviousOlympiadPoint);

L2_BIND_PACKET(kBindNpcSetEnchantOfWeapon, gPacketTableFromNpc, 0x7c, &NpcSetEnchantOfWeapon);

L2_BIND_PACKET(kBindNpcUpdatePCCafePoint, gPacketTableFromNpc, 0x7d, &NpcUpdatePCCafePoint);

L2_BIND_PACKET(kBindNpcGiveItemByPCCafePoint, gPacketTableFromNpc, 0x7e, &NpcGiveItemByPCCafePoint);

L2_BIND_PACKET(kBindNpcInstantTeleportMPCC, gPacketTableFromNpc, 0x7f, &NpcInstantTeleportMPCC);

L2_BIND_PACKET(kBindNpcSetMPCCMasterPartyRouting, gPacketTableFromNpc, 0x80, &NpcSetMPCCMasterPartyRouting);

L2_BIND_PACKET(kBindDeleteOlympiadTradePoint, gPacketTableFromNpc, 0x81, &DeleteOlympiadTradePoint);

L2_BIND_PACKET(kBindNpcMakerFinished, gPacketTableFromNpc, 0x82, &NpcMakerFinished);

L2_BIND_PACKET(kBindNpcRegMakerEvent, gPacketTableFromNpc, 0x83, &NpcRegMakerEvent);

L2_BIND_PACKET(kBindNpcMiniGameAgitFunc, gPacketTableFromNpc, 0x84, &NpcMiniGameAgitFunc);

L2_BIND_PACKET(kBindNpcShowScreenMessage, gPacketTableFromNpc, 0x85, &NpcShowScreenMessage);

L2_BIND_PACKET(kBindNpcShowScreenFStrMessage, gPacketTableFromNpc, 0x86, &NpcShowScreenFStrMessage);

L2_BIND_PACKET(kBindNpcBroadcastScreenMessage, gPacketTableFromNpc, 0x87, &NpcBroadcastScreenMessage);

L2_BIND_PACKET(kBindNpcBroadcastScreenFStrMessage, gPacketTableFromNpc, 0x88, &NpcBroadcastScreenFStrMessage);

L2_BIND_PACKET(kBindNpcSetParam, gPacketTableFromNpc, 0x89, &NpcSetParam);

L2_BIND_PACKET(kBindNpcCheckCursedUser, gPacketTableFromNpc, 0x8a, &NpcCheckCursedUser);

L2_BIND_PACKET(kBindNpcShowFishingRanking, gPacketTableFromNpc, 0x8b, &NpcShowFishingRanking);

L2_BIND_PACKET(kBindNpcGiveFishingPrize, gPacketTableFromNpc, 0x8c, &NpcGiveFishingPrize);

L2_BIND_PACKET(kBindNpcShowCursedWeaponStatus, gPacketTableFromNpc, 0x8d, &NpcShowCursedWeaponStatus);

L2_BIND_PACKET(kBindNpcSpecialCamera3, gPacketTableFromNpc, 0x8e, &NpcSpecialCamera3);

L2_BIND_PACKET(kBindNpcSpecialCameraZLimit, gPacketTableFromNpc, 0x8f, &NpcSpecialCameraZLimit);

L2_BIND_PACKET(kBindNpcTBAgitCheckMemberRegisterStatus, gPacketTableFromNpc, 0x90, &NpcTBAgitCheckMemberRegisterStatus);

L2_BIND_PACKET(kBindNpcAuctionAgitGetCostInfo, gPacketTableFromNpc, 0x91, &NpcAuctionAgitGetCostInfo);

L2_BIND_PACKET(kBindAddOlympiadTradePoint, gPacketTableFromNpc, 0x92, &AddOlympiadTradePoint);

L2_BIND_PACKET(kBindNpcAddPCSocial, gPacketTableFromNpc, 0x93, &NpcAddPCSocial);

L2_BIND_PACKET(kBindShowVariationMakeWindow, gPacketTableFromNpc, 0x94, &ShowVariationMakeWindow);

L2_BIND_PACKET(kBindShowVariationCancelWindow, gPacketTableFromNpc, 0x95, &ShowVariationCancelWindow);

L2_BIND_PACKET(kBindNPCPlayAnimation, gPacketTableFromNpc, 0x96, &NPCPlayAnimation);

L2_BIND_PACKET(kBindNPCTeleportToUser, gPacketTableFromNpc, 0x97, &NPCTeleportToUser);

L2_BIND_PACKET(kBindNPCUnequipWeapon, gPacketTableFromNpc, 0x98, &NPCUnequipWeapon);

L2_BIND_PACKET(kBindNPCGiveEventItem, gPacketTableFromNpc, 0x99, &NPCGiveEventItem);

L2_BIND_PACKET(kBindNPCSetTrapWorldVisible, gPacketTableFromNpc, 0x9a, &NPCSetTrapWorldVisible);

L2_BIND_PACKET(kBindNPCActivateWorldTrap, gPacketTableFromNpc, 0x9b, &NPCActivateWorldTrap);

L2_BIND_PACKET(kBindNPCSetVisible, gPacketTableFromNpc, 0x9c, &NPCSetVisible);

L2_BIND_PACKET(kBindNPCPlayScene, gPacketTableFromNpc, 0x9d, &NPCPlayScene);

L2_BIND_PACKET(kBindNpcFortressEvent, gPacketTableFromNpc, 0x9e, &NpcFortressEvent);

L2_BIND_PACKET(kBindNPCFieldCycle, gPacketTableFromNpc, 0x9f, &NPCFieldCycle);

L2_BIND_PACKET(kBindNPCShowEnchantSkillListDrawer, gPacketTableFromNpc, 0xa0, &NPCShowEnchantSkillListDrawer);

L2_BIND_PACKET(kBindNpcInstantZoneEvent, gPacketTableFromNpc, 0xa1, &NpcInstantZoneEvent);

L2_BIND_PACKET(kBindNpcShowChangePledgeNameUI, gPacketTableFromNpc, 0xa2, &NpcShowChangePledgeNameUI);

L2_BIND_PACKET(kBindNpcSetStaticMeshStatus, gPacketTableFromNpc, 0xa3, &NpcSetStaticMeshStatus);

L2_BIND_PACKET(kBindShowBaseAttributeCancelWindow, gPacketTableFromNpc, 0xa4, &ShowBaseAttributeCancelWindow);

L2_BIND_PACKET(kBindNpcAreaSetBannedTerritoryOnOff, gPacketTableFromNpc, 0xa5, &NpcAreaSetBannedTerritoryOnOff);

L2_BIND_PACKET(kBindGiveItemByCastleSiegeDefence, gPacketTableFromNpc, 0xa6, &GiveItemByCastleSiegeDefence);

L2_BIND_PACKET(kBindNpcCreatePVPMatch, gPacketTableFromNpc, 0xa7, &NpcCreatePVPMatch);

L2_BIND_PACKET(kBindNpcRegisterPVPMatch, gPacketTableFromNpc, 0xa8, &NpcRegisterPVPMatch);

L2_BIND_PACKET(kBindNpcUnregisterPVPMatch, gPacketTableFromNpc, 0xa9, &NpcUnregisterPVPMatch);

L2_BIND_PACKET(kBindNpcStartPVPMatch, gPacketTableFromNpc, 0xaa, &NpcStartPVPMatch);

L2_BIND_PACKET(kBindNpcEndPVPMatch, gPacketTableFromNpc, 0xab, &NpcEndPVPMatch);

L2_BIND_PACKET(kBindNpcCheckRegisterParty, gPacketTableFromNpc, 0xac, &NpcCheckRegisterParty);

L2_BIND_PACKET(kBindNpcRegisterResurrectionTower, gPacketTableFromNpc, 0xad, &NpcRegisterResurrectionTower);

L2_BIND_PACKET(kBindNpcDispel, gPacketTableFromNpc, 0xae, &NpcDispel);

L2_BIND_PACKET(kBindNpcRegisterTeleporterType, gPacketTableFromNpc, 0xaf, &NpcRegisterTeleporterType);

L2_BIND_PACKET(kBindNpcAbsoluteReturn, gPacketTableFromNpc, 0xb0, &NpcAbsoluteReturn);

L2_BIND_PACKET(kBindNpcShowPremiumItemList, gPacketTableFromNpc, 0xb1, &NpcShowPremiumItemList);

L2_BIND_PACKET(kBindNpcCheckRegisterUser, gPacketTableFromNpc, 0xb2, &NpcCheckRegisterUser);

L2_BIND_PACKET(kBindNpcIsPVPMatching, gPacketTableFromNpc, 0xb3, &NpcIsPVPMatching);

L2_BIND_PACKET(kBindNpcRegisterUserPVPMatch, gPacketTableFromNpc, 0xb4, &NpcRegisterUserPVPMatch);

L2_BIND_PACKET(kBindNpcUnregisterUserPVPMatch, gPacketTableFromNpc, 0xb5, &NpcUnregisterUserPVPMatch);

L2_BIND_PACKET(kBindNpcAddKillPointUserPVPMatch, gPacketTableFromNpc, 0xb6, &NpcAddKillPointUserPVPMatch);

L2_BIND_PACKET(kBindNpcGetRankUserPVPMatch, gPacketTableFromNpc, 0xb7, &NpcGetRankUserPVPMatch);

L2_BIND_PACKET(kBindNpcRegisterUserResurrectionTower, gPacketTableFromNpc, 0xb8, &NpcRegisterUserResurrectionTower);

L2_BIND_PACKET(kBindNpcSetRimPoint, gPacketTableFromNpc, 0xb9, &NpcSetRimPoint);

L2_BIND_PACKET(kBindNpcChangeStatus, gPacketTableFromNpc, 0xba, &NpcChangeStatus);

L2_BIND_PACKET(kBindSendUIEvent, gPacketTableFromNpc, 0xbb, &SendUIEvent);

L2_BIND_PACKET(kBindSendUIEventFStr, gPacketTableFromNpc, 0xbc, &SendUIEventFStr);

L2_BIND_PACKET(kBindNpcRegisterDominionSiege, gPacketTableFromNpc, 0xbd, &NpcRegisterDominionSiege);

L2_BIND_PACKET(kBindNpcDeclareLord, gPacketTableFromNpc, 0xbe, &NpcDeclareLord);

L2_BIND_PACKET(kBindNpcGetRegistrySize, gPacketTableFromNpc, 0xbf, &NpcGetRegistrySize);

L2_BIND_PACKET(kBindNpcSaveGetbirthdayGiftTime, gPacketTableFromNpc, 0xc0, &NpcSaveGetbirthdayGiftTime);

L2_BIND_PACKET(kBindNpcSaveGet5YearGiftTime, gPacketTableFromNpc, 0xc1, &NpcSaveGet5YearGiftTime);

L2_BIND_PACKET(kBindNpcBlockUpset, gPacketTableFromNpc, 0xc2, &NpcSocket::NpcBlockUpset);

L2_BIND_PACKET(kBindInzoneDoorBreakable, gPacketTableFromNpc, 0xc3, &InzoneDoorBreakable);

L2_BIND_PACKET(kBindNpcCleftManagerEnter, gPacketTableFromNpc, 0xc4, &NpcSocket::NpcCleftManagerEnter);

L2_BIND_PACKET(kBindNpcCleftUserEnter, gPacketTableFromNpc, 0xc5, &NpcSocket::NpcCleftUserEnter);

L2_BIND_PACKET(kBindNpcCleftCenterDestroyed, gPacketTableFromNpc, 0xc6, &NpcSocket::NpcCleftCenterDestroyed);

L2_BIND_PACKET(kBindNpcFieldCyclePointLock, gPacketTableFromNpc, 0xc7, &NPCFieldCyclePointLock);

L2_BIND_PACKET(kBindCreatureInfoUpdated, gPacketTableFromNpc, 0xc8, &NpcSocket::CreatureInfoUpdated);

L2_BIND_PACKET(kBindNpcDominionScriptEvent, gPacketTableFromNpc, 0xc9, &NpcDominionScriptEvent);

L2_BIND_PACKET(kBindNpcStartScenePlayer, gPacketTableFromNpc, 0xca, &NpcSocket::NpcStartScenePlayer);

L2_BIND_PACKET(kBindNpcSetDailyQuestFlag, gPacketTableFromNpc, 0xcb, &NpcSetDailyQuestFlag);

L2_BIND_PACKET(kBindNpcSetSkillAll, gPacketTableFromNpc, 0xcc, &NpcSetSkillAll);

L2_BIND_PACKET(kBindReplyNpcPing, gPacketTableFromNpc, 0xcd, &ReplyNpcPing);

L2_BIND_PACKET(kBindNpcSetXMasEventState, gPacketTableFromNpc, 0xce, &NpcSetXMasEventState);

L2_BIND_PACKET(kBindNpcEventManagetEnter, gPacketTableFromNpc, 0xcf, &NpcSocket::NpcEventManagetEnter);

L2_BIND_PACKET(kBindNpcSetYearCatchEventState, gPacketTableFromNpc, 0xd0, &NpcSetYearCatchEventState);

L2_BIND_PACKET(kBindNpcYearCatchEventManagerEnter, gPacketTableFromNpc, 0xd1, &NpcSocket::NpcYearCatchEventManagerEnter);

L2_BIND_PACKET(kBindNpcShowInTerritory, gPacketTableFromNpc, 0xd2, &NpcSocket::NpcShowInTerritory);

L2_BIND_PACKET(kBindNpcSetAbilityDropItem, gPacketTableFromNpc, 0xd3, &NpcSocket::NpcSetAbilityDropItem);

L2_BIND_PACKET(kBindNpcWhisper, gPacketTableFromNpc, 0xd4, &NpcSocket::NpcWhisper);

L2_BIND_PACKET(kBindNpcWhisperFStr, gPacketTableFromNpc, 0xd5, &NpcSocket::NpcWhisperFStr);

L2_BIND_PACKET(kBindNpcIsToggleSkillOnOff, gPacketTableFromNpc, 0xd6, &NpcSocket::NpcIsToggleSkillOnOff);

L2_BIND_PACKET(kBindNpcShowBuySellPage, gPacketTableFromNpc, 0xd7, &NpcSocket::NpcShowBuySellPagePacket);

L2_BIND_PACKET(kBindNpcObserveEventMatch, gPacketTableFromNpc, 0xd8, &NpcSocket::NpcObserveEventMatch);

L2_BIND_PACKET(kBindInsertEventRanking, gPacketTableFromNpc, 0xd9, &InsertEventRanking);

L2_BIND_PACKET(kBindNpcTeleportTo, gPacketTableFromNpc, 0xda, &NpcSocket::NpcTeleportTo);

L2_BIND_PACKET(kBindNpcChangeState, gPacketTableFromNpc, 0xdb, &NpcSocket::NpcChangeState);

L2_BIND_PACKET(kBindNpcFollow, gPacketTableFromNpc, 0xdc, &NpcSocket::NpcFollow);

L2_BIND_PACKET(kBindNpcClientEffectInfo, gPacketTableFromNpc, 0xdd, &NpcSocket::NpcClientEffectInfo);

L2_BIND_PACKET(kBindNpcChangeName, gPacketTableFromNpc, 0xde, &NpcSocket::NpcChangeName);

L2_BIND_PACKET(kBindNpcChangeDir, gPacketTableFromNpc, 0xdf, &NpcSocket::NpcChangeDir);

L2_BIND_PACKET(kBindNpcIncreaseNPCLog, gPacketTableFromNpc, 0xe0, &NpcSocket::NpcIncreaseNPCLog);

L2_BIND_PACKET(kBindNpcGetEventScheduledTime, gPacketTableFromNpc, 0xe1, &NpcSocket::GetEventScheduledTime);

L2_BIND_PACKET(kBindNpcFindRandomUser, gPacketTableFromNpc, 0xe2, &NpcSocket::FindRandomUser);

L2_BIND_PACKET(kBindNpcRequestEventInfo, gPacketTableFromNpc, 0xe3, &NpcSocket::RequestEventInfo);

L2_BIND_PACKET(kBindNpcRegisterOlympiadMaker, gPacketTableFromNpc, 0xe4, &NpcSocket::RegisterOlympiadMaker);

L2_BIND_PACKET(kBindNpcDummyE5, gPacketTableFromNpc, 0xe5, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummyE6, gPacketTableFromNpc, 0xe6, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummyE7, gPacketTableFromNpc, 0xe7, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummyE8, gPacketTableFromNpc, 0xe8, &NpcDummy);

L2_BIND_PACKET(kBindNpcDummyE9, gPacketTableFromNpc, 0xe9, &NpcDummy);

L2_BIND_PACKET(kBindNpcShowChannelingEffect, gPacketTableFromNpc, 0xea, &NpcSocket::NpcShowChannelingEffect);

L2_BIND_PACKET(kBindNpcRegisterAIStatus, gPacketTableFromNpc, 0xeb, &NpcRegisterAIStatus);

L2_BIND_PACKET(kBindNpcDynamicQuest, gPacketTableFromNpc, 0xec, &NpcSocket::NpcDynamicQuest);

L2_BIND_PACKET(kBindNpcSetWeeklyQuestFlag, gPacketTableFromNpc, 0xed, &NpcSetWeeklyQuestFlag);

L2_BIND_PACKET(kBindNpcSetCalledPledgeDBId, gPacketTableFromNpc, 0xee, &NpcSocket::NpcSetCalledPledgeDBId);

L2_BIND_PACKET(kBindNpcGetPledgeRaidRewardRate, gPacketTableFromNpc, 0xef, &NpcSocket::NpcGetPledgeRaidRewardRate);

L2_BIND_PACKET(kBindNpcInstantAgitPacket, gPacketTableFromNpc, 0xf0, &NpcSocket::NpcInstantAgitPacket);

L2_BIND_PACKET(kBindNpcCallToChangeClass, gPacketTableFromNpc, 0xf1, &NpcSocket::NpcCallToChangeClass);

L2_BIND_PACKET(kBindShowCommission, gPacketTableFromNpc, 0xf2, &NpcSocket::ShowCommission);

L2_BIND_PACKET(kBindNpcChangeToAwakenedClass, gPacketTableFromNpc, 0xf3, &NpcSocket::NpcChangeToAwakenedClass);

L2_BIND_PACKET(kBindNpcCreateOneAnother, gPacketTableFromNpc, 0xf4, &NpcCreateOneAnother);

L2_BIND_PACKET(kBindBroadcastUIEventFStr, gPacketTableFromNpc, 0xf5, &BroadcastUIEventFStr);

L2_BIND_PACKET(kBindBroadcastUIEventFStrInTerritory, gPacketTableFromNpc, 0xf6, &BroadcastUIEventFStrInTerritory);

L2_BIND_PACKET(kBindSendUSMEvent, gPacketTableFromNpc, 0xf8, &SendUSMEvent);

L2_BIND_PACKET(kBindBroadcastUSMEvent, gPacketTableFromNpc, 0xf9, &BroadcastUSMEvent);

L2_BIND_PACKET(kBindNpcShowStatPage, gPacketTableFromNpc, 0xfa, &NpcSocket::ShowStatPage);

L2_BIND_PACKET(kBindNpcBroadcastSound, gPacketTableFromNpc, 0xfb, &NpcBroadcastSound);

L2_BIND_PACKET(kBindRequestTotalSkillEnchantCount, gPacketTableFromNpc, 0xfc, &NpcSocket::RequestTotalSkillEnchantCount);

L2_BIND_PACKET(kBindNpcSunRise, gPacketTableFromNpc, 0xfd, &NpcSunRise);

L2_BIND_PACKET(kBindNpcExSocket, gPacketTableFromNpc, 0xfe, &NpcExSocket);

bool NpcSpecialCamera2(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcStaticObjectAttackPacket(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] deprecated packet!",
    //     L"NpcSocket.cpp",
    //     0x172c)
    // Noise: replace shipping coordinates with the packet name and actual outcome.
    L2_LOG_ERROR(Log, "[NPC protocol] Ignoring deprecated NpcStaticObjectAttackPacket; no action is performed.");
    return false;
}

bool NpcShowManorMenu(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x0089bdcc, NC_PROTOCOL 0x5a.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Function '%s' is deprecated.",
    //     L"NpcSocket.cpp",
    //     0x1926,
    //     L"NpcShowManorMenu")
    // Noise: replace shipping coordinates with the packet name and actual outcome.
    L2_LOG_ERROR(Log, "[NPC protocol] Ignoring deprecated NpcShowManorMenu; no menu is sent.");
    return false;
}

bool NpcSetNextSeedInfo(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ac498, NC_PROTOCOL 0x5b.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t residenceId = 0;
    std::int32_t manorId = 0;
    std::int32_t seedId = 0;
    std::int64_t count = 0;
    std::int64_t price = 0;
    ::Disassemble(packet, "dddddQQ", &npcIndex, &userIndex, &residenceId, &manorId, &seedId, &count, &price);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [NpcSetNextSeedInfo]")
        L2_LOG_ERROR(
            Log,
            "[Manor] Seed update rejected for residence %d, manor %d: "
            "NPC index %d or user index %d is missing or has the wrong creature type.",
            residenceId, manorId, npcIndex, userIndex
        );
        return false;
    }

    if (seedId < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
        return false;
    }
    if (count < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_QUANTITY);
        return false;
    }
    if (price < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find residence in [NpcSetNextSeedInfo] [%d][%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Manor] Seed update rejected: residence %d was not found (NPC residence %d).", residenceId,
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }
    if (!residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [NpcSetNextSeedInfo] [%d][%d] manorid[%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID,
        //     manorId)
        L2_LOG_ERROR(
            Log, "[Manor] Seed update rejected: residence %d (NPC residence %d) does not have manor %d.", residenceId,
            npcCreature->m_data->m_nResidenceID, manorId
        );
        return false;
    }

    residence->SetNextSeedInfo(npcIndex, userIndex, residenceId, manorId, seedId, count, price);
    return false;
}

bool NpcSetNextProcureInfo(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008acb0c, NC_PROTOCOL 0x5c.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t residenceId = 0;
    std::int32_t manorId = 0;
    std::int32_t cropId = 0;
    std::int64_t count = 0;
    std::int64_t price = 0;
    std::int32_t type = 0;
    ::Disassemble(packet, "dddddQQd", &npcIndex, &userIndex, &residenceId, &manorId, &cropId, &count, &price, &type);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [NpcSetNextProcureInfo]")
        L2_LOG_ERROR(
            Log,
            "[Manor] Crop procurement update rejected for residence %d, manor %d: "
            "NPC index %d or user index %d is missing or has the wrong creature type.",
            residenceId, manorId, npcIndex, userIndex
        );
        return false;
    }

    if (cropId < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_INFORMATION);
        return false;
    }
    if (price < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CROP_QUANTITY);
        return false;
    }
    if (count < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_CROP_PRICE);
        return false;
    }
    if (type < 0) {
        userCreature->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_TYPE);
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find residence in [NpcSetNextProcureInfo] [%d][%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Manor] Crop procurement update rejected: residence %d was not found (NPC residence %d).", residenceId,
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }
    if (!residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [NpcSetNextProcureInfo] [%d][%d] manorid[%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID,
        //     manorId)
        L2_LOG_ERROR(
            Log, "[Manor] Crop procurement update rejected: residence %d (NPC residence %d) does not have manor %d.",
            residenceId, npcCreature->m_data->m_nResidenceID, manorId
        );
        return false;
    }

    residence->SetNextCropInfo(npcIndex, userIndex, residenceId, manorId, cropId, count, price, type);
    return false;
}

bool NpcShowSellSeedList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ad24c, NC_PROTOCOL 0x5d.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t residenceId = 0;
    std::int32_t manorId = 0;
    ::Disassemble(packet, "dddd", &npcIndex, &userIndex, &residenceId, &manorId);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [NpcShowSellSeedList]")
        L2_LOG_ERROR(
            Log,
            "[Manor] Cannot show seed list for residence %d, manor %d: "
            "NPC index %d or user index %d is missing or has the wrong creature type.",
            residenceId, manorId, npcIndex, userIndex
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot cannot find residence in [NpcShowSellSeedList] [%d][%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Manor] Cannot show seed list: residence %d was not found (NPC residence %d).", residenceId,
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }
    if (!residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find manor in [NpcShowSellSeedList] [%d][%d] manorid[%d]",
        //     residenceId,
        //     npcCreature->m_data->m_nResidenceID,
        //     manorId)
        L2_LOG_ERROR(
            Log, "[Manor] Cannot show seed list: residence %d (NPC residence %d) does not have manor %d.", residenceId,
            npcCreature->m_data->m_nResidenceID, manorId
        );
        return false;
    }

    residence->ShowSellSeedList(*npcCreature->CastNPC(), *userCreature->CastUser(), residenceId, manorId);
    return false;
}

bool NpcShowProcureCropList(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x0089be0c, NC_PROTOCOL 0x5e.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] Function NpcShowProcureCropList is deprecated. See NpcManorFunc.",
    //     "NpcSocket.cpp",
    //     0x1a05)
    // Noise: replace shipping coordinates with the ignored operation and its replacement.
    L2_LOG_ERROR(Log, "[NPC protocol] Ignoring deprecated NpcShowProcureCropList; no list is sent. Use NpcManorFunc.");
    return false;
}

bool NpcAreaSetOnOff(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089d060, NC_PROTOCOL 0x5f.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 0x5000> areaName{};
    std::int32_t on = 0;
    std::int32_t inZoneId = 0;
    ::Disassemble(packet, "Sdd", 0xa000, areaName.data(), &on, &inZoneId);

    CArea* const area = CAreaDB::inst().FindArea(areaName.data());
    if (area == nullptr) {
        std::wstring hostAreaName;
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid area[%s]",
        //     TargetUtf16ToHostWide(areaName.data(), hostAreaName))
        L2_LOG_ERROR(
            Log, "[Areas] Cannot change area state: '%ls' was not found (requested on=%d, instance %d).",
            TargetUtf16ToHostWide(areaName.data(), hostAreaName), on, inZoneId
        );
        return false;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(inZoneId));
    CInstantZone* const inZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
    if (inZone == nullptr) {
        area->SetOnOff(on == 1);
    } else {
        inZone->SetAreaOnOff(area, on == 1);
    }
    return false;
}

bool NpcShowTerritoryInfo(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcGiveFishingPrize(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcShowFishingRanking(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcShowDirection(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t first;
    std::int32_t second;
    char16_t direction[1000];
    std::int32_t third;
    std::int32_t fourth;
    std::int32_t fifth;
    ::Disassemble(packet, "ddSddd", &first, &second, 2000, direction, &third, &fourth, &fifth);
    return false;
}

bool ReplyNpcPing(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    const std::uint32_t replyTime = runtime::GetTickCount();
    std::int32_t sendTime;
    ::Disassemble(packet, "d", &sendTime);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NPC Ping] SendTime [%d] ReplyTime [%d] ",
    //     sendTime,
    //     runtime::Int32FromBits(replyTime))
    // Noise: a ping reply is informational. Ticks are unsigned millisecond counters, not wall-clock times.
    L2_LOG_INFO(
        Log, "[NPC ping] Reply received: sent tick %u, received tick %u, elapsed %u ms.",
        static_cast<unsigned>(static_cast<std::uint32_t>(sendTime)), static_cast<unsigned>(replyTime),
        static_cast<unsigned>(replyTime - static_cast<std::uint32_t>(sendTime))
    );
    return false;
}

bool NpcMovePacket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid = 0;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    ::Disassemble(packet, "dddd", &creatureSid, &x, &y, &z);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
    if (npc != nullptr) {
        if (runtime::runtime_timing::Matches(npc)) {
            runtime::runtime_timing::Record(
                "npc_move_ingress", npc, nullptr,
                "destination=%d,%d,%d mode=%d environment=%d "
                "base_speed=%d effective_speed=%.6f",
                x, y, z, static_cast<int>(npc->m_data->m_MoveMode), static_cast<int>(npc->m_data->m_Environment),
                npc->m_data->m_nMoveTypeSpeed, npc->m_data->m_dParam[VALIDATE::SPEED]
            );
        }
        npc->OnNpcMovePacket(x, y, z);
    }
    return false;
}

bool NpcSocket::NpcFollow(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t targetSid = 0;
    std::int32_t followType = 0;
    std::int32_t first = 0;
    std::int32_t second = 0;
    ::Disassemble(packet, "ddddd", &npcSid, &targetSid, &followType, &first, &second);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        return false;
    }
    return npc->OnNpcFollow(targetSid, static_cast<char>(followType), first, second);
}

bool NpcShowHTMLString(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a8588, NC_PROTOCOL 0x11.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t userIndex;
    char16_t html[10000];
    ::Disassemble(packet, "ddS", &npcIndex, &userIndex, 20000, html);

    static_cast<void>(npcIndex);
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        creature->CastUser()->ShowHTML(nullptr, SanitizeUtf16(html).c_str(), 0, HTML::LINK, true);
    }
    return false;
}

bool NpcChangeMoveType(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a875c, NC_PROTOCOL 0x12.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t moveType;
    std::int32_t reason;
    ::Disassemble(packet, "ddd", &npcIndex, &moveType, &reason);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (creature && creature->IsNPC()) {
        CNPC* const npc = creature->CastNPC();
        if (runtime::runtime_timing::Matches(npc)) {
            runtime::runtime_timing::Record(
                "npc_move_mode_ingress", npc, nullptr,
                "current=%d requested=%d reason=%d environment=%d "
                "base_speed=%d effective_speed=%.6f",
                static_cast<int>(npc->m_data->m_MoveMode), moveType, reason, static_cast<int>(npc->m_data->m_Environment),
                npc->m_data->m_nMoveTypeSpeed, npc->m_data->m_dParam[VALIDATE::SPEED]
            );
        }
        npc->ChangeMoveType(static_cast<MoveType>(moveType), static_cast<unsigned int>(reason));
    }
    return false;
}

bool NpcEquipItem(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a88f0, NC_PROTOCOL 0x13.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t itemClassId;
    ::Disassemble(packet, "dd", &npcIndex, &itemClassId);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (creature && creature->IsNPC()) {
        creature->CastNPC()->EquipItemByClassID(itemClassId);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Invalid NPC index[%d], ItemClassID[%d]",
        //     "NpcSocket.cpp",
        //     0xb18,
        //     npcIndex,
        //     itemClassId)
        // Noise: replace shipping coordinates with the rejected equipment request.
        L2_LOG_ERROR(
            Log, "[NPC items] Cannot equip item class %d: NPC index %d is missing or is not an NPC.", itemClassId, npcIndex
        );
    }
    return false;
}

bool NpcMoveToPawn(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t sourceIndex;
    std::int32_t targetSid;
    std::int32_t targetIndex;
    ::Disassemble(packet, "ddd", &sourceIndex, &targetSid, &targetIndex);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(targetIndex) != targetSid) {
        return false;
    }

    CSPointer<CCreature> source = creatures.FindObjectSP(sourceIndex);
    if (!source || !source->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc index[%d]",
        //     L"NpcSocket.cpp",
        //     0xb31,
        //     sourceIndex)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log,
            "[NPC movement] MoveToPawn rejected: source index %d is missing or is not an NPC "
            "(target index %d, SID %d).",
            sourceIndex, targetIndex, targetSid
        );
        return false;
    }

    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
    if (target != nullptr) {
        source->Action(target, false, false);
    }
    return false;
}

bool NpcShowSkillList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bcc24, NC_PROTOCOL 0x15.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t userSid;
    std::int32_t userIndex;
    char16_t unusedText[1024];
    EtcSkillAcquireType acquireType;
    ::Disassemble(packet, "dddSd", &npcIndex, &userSid, &userIndex, 0x800, unusedText, &acquireType);
    static_cast<void>(unusedText);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(userIndex) != userSid) {
        return false;
    }

    CSPointer<CCreature> userCreature = creatures.FindObjectSP(userIndex);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0xb5f,
        //     userIndex)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log,
            "[NPC skills] Cannot show skill list from NPC index %d: user index %d "
            "is missing or is not a player. No list is sent.",
            npcIndex, userIndex
        );
        return false;
    }

    CSPointer<CCreature> npcCreature = creatures.FindObjectSP(npcIndex);
    if (!npcCreature || !npcCreature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc index[%d]",
        //     L"NpcSocket.cpp",
        //     0xb66,
        //     npcIndex)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log,
            "[NPC skills] Cannot show skill list for user index %d: NPC index %d "
            "is missing or is not an NPC. No list is sent.",
            userIndex, npcIndex
        );
        return false;
    }

    User* const user = userCreature->CastUser();
    CNPC* const npc = npcCreature->CastNPC();
    TalkChannelCache::inst().mChannels[0].Cache(user->Id(), npc->Id(), 0);
    user->SendSkillAcquireList(acquireType, false);
    return false;
}

bool NpcGrowSkillSuggestion(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a8d38, NC_PROTOCOL 0x16.
    L2_GUARD_FUNCTION();

    std::int32_t unusedNpcIndex;
    std::int32_t userSid;
    std::int32_t userIndex;
    std::int64_t skillNameId;
    std::int32_t unusedArgument0;
    std::int32_t unusedArgument1;
    char16_t unusedText[1024];
    EtcSkillAcquireType acquireType;
    ::Disassemble(
        packet, "dddQddSd", &unusedNpcIndex, &userSid, &userIndex, &skillNameId, &unusedArgument0, &unusedArgument1, 0x800,
        unusedText, &acquireType
    );

    static_cast<void>(unusedNpcIndex);
    static_cast<void>(unusedArgument0);
    static_cast<void>(unusedArgument1);
    static_cast<void>(unusedText);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(userIndex) != userSid) {
        return false;
    }

    CSPointer<CCreature> creature = creatures.FindObjectSP(userIndex);
    if (!creature || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0xbab,
        //     userIndex)
        // Noise: replace shipping coordinates with operation context.
        L2_LOG_ERROR(
            Log, "[NPC skills] GrowSkillSuggestion rejected: user index %d is missing or is not a player.", userIndex
        );
        return false;
    }

    User* const user = creature->CastUser();
    std::uint64_t packedSkill;
    static_assert(sizeof(packedSkill) == sizeof(skillNameId));
    std::memcpy(&packedSkill, &skillNameId, sizeof(packedSkill));
    const int skillId = runtime::Int32FromBits(static_cast<std::uint32_t>(packedSkill >> 32U));
    const int skillLevel = static_cast<int>((packedSkill >> 16U) & UINT64_C(0xffff));
    const int skillSubLevel = static_cast<int>(packedSkill & UINT64_C(0xffff));

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(skillId, skillLevel, skillSubLevel), user);
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid skill_id[%d] skill_level[%d]",
        //     L"NpcSocket.cpp",
        //     0xbb9,
        //     skillId,
        //     skillLevel)
        // Noise: replace shipping coordinates with the complete skill key and request context.
        L2_LOG_ERROR(
            Log,
            "[NPC skills] GrowSkillSuggestion rejected for user index %d: "
            "skill %d, level %d, sublevel %d was not found.",
            userIndex, skillId, skillLevel, skillSubLevel
        );
        return false;
    }

    if (skillSubLevel != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NpcGrowSkillSuggestion() invalid sublevel id[%d] level[%d], sublev[%d]",
        //     L"NpcSocket.cpp",
        //     0xbc1,
        //     skillId,
        //     skillLevel,
        //     skillSubLevel)
        // Noise: remove stray source arguments that did not match the original format.
        L2_LOG_ERROR(
            Log,
            "[NPC skills] GrowSkillSuggestion for user index %d contains nonzero sublevel: "
            "skill %d, level %d, sublevel %d. Processing continues.",
            userIndex, skillId, skillLevel, skillSubLevel
        );
    }

    SkillAcquireInfo* const acquireInfo = acquireType == ESAT_NORMAL
        ? g_SkillAcquireDB.GetSkillAcquireInfo(user->m_data->m_nClass, skillId, skillLevel)
        : g_SkillAcquireDB.GetEtcSkillAcquireInfo(acquireType, skillId, skillLevel);
    if (acquireInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hacking? user[%s] skill_id[%d] skill_level[%d] type[%d]",
        //     L"NpcSocket.cpp",
        //     0xbce,
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     static_cast<int>(acquireType))
        // Noise: replace shipping coordinates and unsupported hacking speculation with the lookup failure.
        // L2_LOG_ERROR(Log,
        //     "[NPC skills] GrowSkillSuggestion rejected for user '%ls': "
        //     "no acquisition data for skill %d, level %d, acquisition type %d. No reply is sent.",
        //     user->GetName(),
        //     skillId,
        //     skillLevel,
        //     static_cast<int>(acquireType))
        L2_LOG_ERROR(
            Log,
            u"[NPC skills] GrowSkillSuggestion rejected for user '%ls': "
            u"no acquisition data for skill %d, level %d, acquisition type %d. No reply is sent.",
            user->GetName(), skillId, skillLevel, static_cast<int>(acquireType)
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (acquireInfo->vNeededItem.empty()) {
        userSocket->Send("cddQdd", 0x91, skillId, skillLevel, acquireInfo->nLevelUP_SP, static_cast<int>(acquireType), 0);
        return false;
    }

    std::array<char, 0x5000> itemPacket;
    int packetLength = 0;
    for (std::size_t index = 0; index < acquireInfo->vNeededItem.size(); ++index) {
        CObject* const object = g_ObjectDB.GetObjectW(runtime::CheckedVectorAt(acquireInfo->vNeededItem, index), false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid item",
            //     L"NpcSocket.cpp",
            //     0xbe0)
            // Noise: replace shipping coordinates with the missing requirement and skill key.
            L2_LOG_ERROR(
                Log,
                "[NPC skills] GrowSkillSuggestion rejected for user index %d: "
                "required item %d at index %zu was not found for skill %d, level %d. No reply is sent.",
                userIndex, static_cast<int>(runtime::CheckedVectorAt(acquireInfo->vNeededItem, index)), index, skillId, skillLevel
            );
            return false;
        }

        CItem* const item = static_cast<CItem*>(object);
        packetLength += ::Assemble(
            itemPacket.data() + packetLength, 0x5000 - packetLength, "ddQQ", static_cast<int>(item->m_data->m_Type),
            item->m_data->m_nClassID, runtime::CheckedVectorAt(acquireInfo->vNeededItemCount, index), item->m_data->m_Slot
        );
    }

    userSocket->Send(
        "cddQddb", 0x91, skillId, skillLevel, acquireInfo->nLevelUP_SP, static_cast<int>(acquireType),
        static_cast<int>(acquireInfo->vNeededItem.size()), packetLength, itemPacket.data()
    );
    return false;
}

bool NpcSayFStringPacket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t option;
    std::int32_t creatureSid;
    std::int32_t stringId;
    char16_t text1[256];
    char16_t text2[256];
    char16_t text3[256];
    char16_t text4[256];
    char16_t text5[256];
    std::int32_t value1;
    std::int32_t value2;
    std::int32_t value3;
    std::int32_t value4;
    std::int32_t value5;
    ::Disassemble(
        packet, "dddSSSSSddddd", &option, &creatureSid, &stringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4,
        0x200, text5, &value1, &value2, &value3, &value4, &value5
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (creature != nullptr && creature->IsNPC()) {
        std::wstring hostText1;
        std::wstring hostText2;
        std::wstring hostText3;
        std::wstring hostText4;
        std::wstring hostText5;
        wchar_t* const argument1 = const_cast<wchar_t*>(TargetUtf16ToHostWide(text1, hostText1));
        wchar_t* const argument2 = const_cast<wchar_t*>(TargetUtf16ToHostWide(text2, hostText2));
        wchar_t* const argument3 = const_cast<wchar_t*>(TargetUtf16ToHostWide(text3, hostText3));
        wchar_t* const argument4 = const_cast<wchar_t*>(TargetUtf16ToHostWide(text4, hostText4));
        wchar_t* const argument5 = const_cast<wchar_t*>(TargetUtf16ToHostWide(text5, hostText5));
        if (option == 1) {
            creature->CastNPC()->SayFStr(
                stringId, argument1, argument2, argument3, argument4, argument5, value1, value2, value3, value4, value5
            );
        } else if (option == 2) {
            creature->CastNPC()->ShoutFStr(
                stringId, argument1, argument2, argument3, argument4, argument5, 0x4000, value1, value2, value3, value4,
                value5
            );
        }
    }
    return false;
}

bool NpcShoutFStringPacket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &creatureSid);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (creature != nullptr && creature->IsNPC()) {
        std::int32_t stringId;
        char16_t text1[256];
        char16_t text2[256];
        char16_t text3[256];
        char16_t text4[256];
        char16_t text5[256];
        std::int32_t radius;
        std::int32_t value1;
        std::int32_t value2;
        std::int32_t value3;
        std::int32_t value4;
        std::int32_t value5;
        ::Disassemble(
            cursor, "dSSSSSdddddd", &stringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4, 0x200, text5, &radius,
            &value1, &value2, &value3, &value4, &value5
        );

        CNPC* const npc = creature->CastNPC();
        std::wstring hostText1;
        std::wstring hostText2;
        std::wstring hostText3;
        std::wstring hostText4;
        std::wstring hostText5;
        npc->ShoutFStr(
            stringId, const_cast<wchar_t*>(TargetUtf16ToHostWide(text1, hostText1)),
            const_cast<wchar_t*>(TargetUtf16ToHostWide(text2, hostText2)),
            const_cast<wchar_t*>(TargetUtf16ToHostWide(text3, hostText3)),
            const_cast<wchar_t*>(TargetUtf16ToHostWide(text4, hostText4)),
            const_cast<wchar_t*>(TargetUtf16ToHostWide(text5, hostText5)), radius, value1, value2, value3, value4, value5
        );
    }
    return false;
}

bool NpcShowMultisellPage(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b9064.
    L2_GUARD_FUNCTION();
    std::int32_t userIndex;
    std::int32_t npcIndexOrEventListId;
    std::int32_t selectorOrListId;
    ::Disassemble(packet, "ddd", &userIndex, &npcIndexOrEventListId, &selectorOrListId);

    CCreature* const creature = FindCreatureByIndex(userIndex);
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (selectorOrListId == -1) {
        CEventMakerManager::inst().ShowMultisell(npcIndexOrEventListId, user);
    } else {
        user->ShowMultiSell(npcIndexOrEventListId, selectorOrListId);
    }
    return false;
}

bool NpcPing(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t requestTick;
    std::int32_t npcReplyTick;
    std::int32_t userSid;
    std::int32_t pendingWrite;
    ::Disassemble(packet, "dddd", &requestTick, &npcReplyTick, &userSid, &pendingWrite);

    const std::int32_t requestDuration =
        runtime::Int32FromBits(static_cast<std::uint32_t>(npcReplyTick) - static_cast<std::uint32_t>(requestTick));
    const std::uint32_t replyTick = runtime::GetTickCount();
    const std::int32_t replyDuration = runtime::Int32FromBits(replyTick - static_cast<std::uint32_t>(npcReplyTick));
    std::array<char16_t, 1000> status{};
    FormatUtf16To(
        status.data(), status.size() - 1,
        u"[NPCPING] main->npc : %dms,  npc->main : %dms, "
        u"pending write(npc->main) : %dbytes",
        requestDuration, replyDuration, pendingWrite
    );
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[NPCPING][%s][%d] %s",
    //     L"NpcSocket.cpp",
    //     0xfe9,
    //     status.data())
    // Noise: omit shipping coordinates and the duplicated NPCPING prefix.
    // Log the measurements directly. The legacy status buffer remains the client-facing message.
    L2_LOG_INFO(
        Log, "[NPC ping] User SID %d: server to NPC %d ms, NPC to server %d ms, pending NPC output %d bytes.", userSid,
        requestDuration, replyDuration, pendingWrite
    );

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        char16_t context[] = u"SYS";
        user->GetSocket()->SendSystemMessage(context, status.data());
    }
    return false;
}

bool NpcShowQuestMark(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcId;
    std::int32_t userSid;
    std::int32_t questId;
    std::int32_t markType;
    ::Disassemble(packet, "dddd", &npcId, &userSid, &questId, &markType);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->GetSocket()->Send("chdd", 0xfe, 0x21, questId, markType);
    }
    return false;
}

bool NpcManorFunc(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a0838, NC_PROTOCOL 0x74.
    L2_GUARD_FUNCTION();
    std::uint8_t function;
    std::int32_t npcId;
    std::int32_t userId;
    std::int32_t manorId;
    std::int32_t time;
    std::int32_t cropId;
    ::Disassemble(packet, "cddddd", &function, &npcId, &userId, &manorId, &time, &cropId);

    CManorFuncHandler handler(function, npcId, userId, manorId, time, cropId);
    if (handler.PreCheck()) {
        handler.Execute();
    }
    return false;
}

bool NpcUseSoulSpiritShot(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ae284, NC_PROTOCOL 0x75.
    L2_GUARD_FUNCTION();

    std::int32_t creatureIndex;
    std::int32_t shotType;
    const unsigned char* const payload = ::Disassemble(packet, "dd", &creatureIndex, &shotType);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature) {
        return false;
    }

    SkillUseInfo skill;
    if (shotType == 0) {
        std::int32_t attackBonus;
        ::Disassemble(payload, "d", &attackBonus);
        if (creature->m_data->m_nSoulShot != 0) {
            return false;
        }

        creature->m_data->m_nSoulShot = 1;
        creature->m_data->m_dSoulShotAttackBonusPer = static_cast<double>(attackBonus) / 100.0;
        skill.m_skillID = 0x7f7;
        skill.m_skillLevel = 1;
    } else if (shotType == 1) {
        std::int32_t attackBonus;
        std::int32_t speedBonus;
        std::int32_t healBonus;
        ::Disassemble(payload, "ddd", &attackBonus, &speedBonus, &healBonus);
        if (creature->m_data->m_bSpiritShot) {
            return false;
        }

        creature->m_data->m_bSpiritShot = true;
        creature->m_data->m_dSpiritShotAttackBonusPer = static_cast<double>(attackBonus) / 100.0;
        creature->m_data->m_dSpiritShotSpeedBonusPer = static_cast<double>(speedBonus) / 100.0;
        creature->m_dSpiritShotHealBonus = static_cast<double>(healBonus);
        skill.m_skillID = 0x7ff;
        skill.m_skillLevel = 1;
    } else {
        return false;
    }

    const int parameter1 = 0;
    const int parameter2 = 0;
    const int parameter3 = 0;
    const int parameter4 = 0;
    const int parameter5 = 0;
    const int parameter6 = 0;
    DefaultIOBuffer* const buffer = creature->MakePacketForMagicSkillUse(
        parameter1, creature.get(), skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
    );
    g_ptlUserMap->BroadcastToNeighborDist(creature.get(), 0x5000, creature->GetPos(), 0x1000, buffer);
    buffer->Release();
    return false;
}

bool NpcSetSkillAll(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aea30, NC_PROTOCOL 0xcc.
    L2_GUARD_FUNCTION();

    std::int32_t creatureIndex = 0;
    std::int32_t type = 0;
    std::uint32_t classOrAcquireType = 0;
    ::Disassemble(packet, "ddd", &creatureIndex, &type, &classOrAcquireType);

    CCreature* const creature = FindCreatureByIndex(creatureIndex);
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    CPacketHelperEx<20480> deletePacket;
    int deleteCount = 0;
    std::uint32_t acquireType = classOrAcquireType;
    if (type == 2) {
        if (classOrAcquireType > 0x8dU) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid etc setskillall! talker %d: class %d",
            //     creatureIndex,
            //     static_cast<std::int32_t>(classOrAcquireType))
            L2_LOG_ERROR(
                Log,
                "[NPC skills] SetSkillAll rejected for creature index %d: acquisition value %u exceeds 141 for operation "
                "type 2.",
                creatureIndex, static_cast<unsigned>(classOrAcquireType)
            );
            return false;
        }

        User* const user = creature->CastUser();
        if (user != nullptr) {
            {
                L2_GUARD(creature->m_AcquiredSkillsSect);
                L2_GUARD(user->m_AcquiredHonorsSkillsLock);
                for (const auto& entry : creature->m_AcquiredSkills) {
                    const AcquiredSkillInfo& acquired = entry.second;
                    const CSkillInfo* const skill =
                        g_SkillDB.GetSkillInfo(CSkillKey(acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel), user);
                    if (!skill->m_bSaveToDB && acquired.m_bSaveToDB) {
                        deletePacket.Assemble("d", acquired.m_nId);
                        ++deleteCount;
                    }
                }
                creature->_DeleteAllAcquireSkills(true);
                creature->_AcquireSkills(g_SkillAcquireDB.GetAcquirableRaceSkills(user->m_data->m_Race), true, false);
                user->_AcquireAllHonorsSkill();
            }
        }
    } else {
        if (type != 1) {
            if (type != 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid setskillall type! talker %d: type %d",
                //     creatureIndex,
                //     type)
                L2_LOG_ERROR(
                    Log, "[NPC skills] SetSkillAll rejected for creature index %d: operation type %d must be 0, 1 or 2.",
                    creatureIndex, type
                );
                return false;
            }
            classOrAcquireType = static_cast<std::uint32_t>(creature->m_data->m_nClass);
        }
        deleteCount = 0;
        acquireType = 0;
        if (classOrAcquireType > 0xbfU) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid class setskillall! talker %d: class %d",
            //     creatureIndex,
            //     static_cast<std::int32_t>(classOrAcquireType))
            L2_LOG_ERROR(
                Log, "[NPC skills] SetSkillAll rejected for creature index %d: class ID %u exceeds 191.", creatureIndex,
                static_cast<unsigned>(classOrAcquireType)
            );
            return false;
        }
    }

    const auto& skills = g_SkillAcquireDB.GetAcquirableSkills(
        static_cast<EtcSkillAcquireType>(acquireType), static_cast<ClassType>(classOrAcquireType)
    );
    std::optional<CPacketHelperEx<20480>> savePacketStorage;
    int saveCount = 0;
    {
        L2_GUARD(*creature->m_lock);
        creature->DisableSetItemSkill();
        creature->DisableNewSetItemSkill();
        const auto& pledgeSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_PLEDGE);
        const auto& subPledgeSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_SUB_PLEDGE);
        auto& savePacket = savePacketStorage.emplace();
        {
            L2_GUARD(creature->m_AcquiredSkillsSect);
            creature->_AcquireSkills(skills, false, false);
            if (type == 2) {
                for (const SkillAcquireInfo* const skill : skills) {
                    if (skill != nullptr && !skill->bAutoGet) {
                        savePacket.Assemble("dddd", skill->nSkillID, skill->nSkillLevel, 0, 1);
                        ++saveCount;
                    }
                }
            } else {
                for (const auto& entry : creature->m_AcquiredSkills) {
                    const AcquiredSkillInfo& acquired = entry.second;
                    bool pledgeSkill = false;
                    for (const SkillAcquireInfo* const skill : pledgeSkills) {
                        if (skill->nSkillID == acquired.m_nId) {
                            pledgeSkill = true;
                        }
                    }
                    for (const SkillAcquireInfo* const skill : subPledgeSkills) {
                        if (skill->nSkillID == acquired.m_nId) {
                            pledgeSkill = true;
                        }
                    }
                    if (acquired.m_bSaveToDB && !pledgeSkill) {
                        savePacket.Assemble("dddd", acquired.m_nId, acquired.m_nLevel, acquired.m_nSubLevel, 1);
                        ++saveCount;
                    }
                }
            }

            if (creature->IsPC()) {
                creature->CastUser()->_OnRuneItemsChange();
            }
        }
        creature->ValidateSetItem(ST_MAX);
        creature->ValidateNewSetItem();
        creature->ValidateShapeShiftedSetItem();
    }
    auto& savePacket = *savePacketStorage;

    if (!creature->IsPC()) {
        creature->ValidateChanged(std::int64_t{VB_MAX});
    } else {
        creature->CastUser()->ValidateChanged(std::int64_t{VB_MAX});
    }
    db.RequestDeleteAndSaveSkillList(creature->CastUser(), deleteCount, &deletePacket, saveCount, &savePacket);
    return false;
}

bool NpcLookItems(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a0964, NC_PROTOCOL 0x76.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid;
    std::int32_t range;
    std::int64_t maxCount;
    std::int32_t itemClassId;
    ::Disassemble(packet, "ddQd", &npcSid, &range, &maxCount, &itemClassId);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        CCreature::__async_arg_LookItems argument;
        argument.__a1 = range;
        argument.__a2 = maxCount;
        argument.__a3 = itemClassId;
        npc->m_LookNeighborSerializer.AddExecutable(
            npc, [](CCreature* owner, CExecutionArgumentPad* pad) { owner->__async_LookItems(pad); }, &argument, L"LookItems"
        );
    }
    return false;
}

bool NpcShowTelposList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b4910, NC_PROTOCOL 0x77.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int64_t listId = 0;
    ::Disassemble(packet, "ddQ", &npcSid, &userSid, &listId);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"User is NULL")
        L2_LOG_ERROR(
            Log, "[teleport] Cannot show position list %lld from NPC SID %d: user SID %d was not found.",
            static_cast<long long>(listId), npcSid, userSid
        );
        return false;
    }

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPC is NULL UserName[%s]",
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     "[teleport] Cannot show position list %lld for user '%ls' (SID %d): NPC SID %d was not found.",
        //     static_cast<long long>(listId), user->GetName(), userSid, npcSid)
        L2_LOG_ERROR(
            Log, u"[teleport] Cannot show position list %lld for user '%ls' (SID %d): NPC SID %d was not found.",
            static_cast<long long>(listId), user->GetName(), userSid, npcSid
        );
        return false;
    }

    std::array<char16_t, 0x2800> html;
    {
        L2_GUARD(s_mapTelPosListSect);
        const auto found = s_mapTelPosList.find(listId);
        if (found == s_mapTelPosList.end()) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "NpcShowTelposList() - s_mapTelPosList.find(%d) failed.")
            // Log.Add(LOG_WARN,
            //     "[NPC teleport] Cannot display destination list %lld for user %d: list not registered.",
            //     static_cast<long long>(listId), userSid)
            L2_LOG_WARN(
                Log, "[NPC teleport] Cannot display destination list %lld for user %d: list not registered.",
                static_cast<long long>(listId), userSid
            );
            return false;
        }

        std::vector<TelPos>* const positions = s_mapTelPosList[listId];
        std::array<char16_t, 1000> link;
        FormatTargetText(html, u"<html><body><br><br>");
        std::int32_t index = 0;
        for (const TelPos& position : *positions) {
            FormatTargetText(
                link,
                u"<button icon=teleport align=left action=\"bypass -h "
                u"show_radar?id=%I64d&index=%d\"><fstring>%d</fstring>"
                u"</button>",
                static_cast<long long>(listId), index, position.m_nNameIndex
            );
            AppendTargetText(html, link.data());
            ++index;
        }
    }

    AppendTargetText(html, u"<br></body></html>");
    user->ShowHTML(nullptr, html.data(), 0, HTML::LINK, true);
    return false;
}

bool NpcAnnouncePacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008afe84, NC_PROTOCOL 0x78.
    L2_GUARD_FUNCTION();

    std::int32_t messageId = -1;
    const unsigned char* const payload = ::Disassemble(packet, "d", &messageId);
    if (messageId == -1) {
        std::array<char16_t, 0x5000> message;
        ::Disassemble(payload, "S", 0xa000, message.data());
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, user, {
            user->GetSocket()->Send("cddSdS", 0x4a, 0, 10, u"", -1, message.data());
        });
        return false;
    }

    std::array<char16_t, 0x100> argument1;
    std::array<char16_t, 0x100> argument2;
    std::array<char16_t, 0x100> argument3;
    std::array<char16_t, 0x100> argument4;
    std::array<char16_t, 0x100> argument5;
    ::Disassemble(
        payload, "SSSSS", 0x200, argument1.data(), 0x200, argument2.data(), 0x200, argument3.data(), 0x200, argument4.data(),
        0x200, argument5.data()
    );
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, user, {
        user->GetSocket()->Send(
            "cddSdSSSSS", 0x4a, 0, 10, u"", messageId, argument1.data(), argument2.data(), argument3.data(),
            argument4.data(), argument5.data()
        );
    });
    return false;
}

bool NpcOlympiadFunc(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ae64c, NC_PROTOCOL 0x79.
    L2_GUARD_FUNCTION();

    std::uint8_t functionId = 0;
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "cddd", &functionId, &npcIndex, &userIndex, &point);

    if (functionId > 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid func id: %d, %d",
        //     L"NpcSocket.cpp",
        //     0x1ca0,
        //     static_cast<int>(functionId),
        //     npcIndex)
        // Noise: replace shipping coordinates with the rejected operation and participants.
        L2_LOG_ERROR(
            Log, "[Olympiad] NPC request rejected: function %u exceeds 7 (NPC index %d, user index %d).",
            static_cast<unsigned>(functionId), npcIndex, userIndex
        );
        return false;
    }

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!creature || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]invalid user index: %d",
        //     L"NpcSocket.cpp",
        //     0x1ca7,
        //     userIndex)
        // Noise: replace shipping coordinates with the rejected operation and participants.
        L2_LOG_ERROR(
            Log, "[Olympiad] Function %u from NPC index %d rejected: user index %d is missing or is not a player.",
            static_cast<unsigned>(functionId), npcIndex, userIndex
        );
        return false;
    }

    CSPointer<User> user(creature->CastUser(), ObjectRefType::FunctionChain);
    if (functionId == 1) {
        if (!WOlympiadStuff::IsTalkableDistance(user, npcIndex)) {
            return false;
        }
        WOlympiadServiceProxy::inst().StartMatchMaking(user);
        return false;
    }

    if (functionId == 3) {
        if (!WOlympiadServiceConfig::inst().IsHost()) {
            WOlympiadServiceProxy::inst().StopMatchMaking(user, false);
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NpcSocket.cpp",
        //     0x1cbc)
        // Noise: identify the unsupported operation instead of shipping coordinates and punctuation.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] Matchmaking cancellation is not implemented on the host "
            "(NPC index %d, user index %d). Request ignored.",
            npcIndex, userIndex
        );
        return false;
    }

    if (functionId == 7) {
        if (!WOlympiadServiceConfig::inst().IsHost()) {
            WOlympiadServiceProxy::inst().ResetOlympiadPoint(user, point);
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NpcSocket.cpp",
        //     0x1cc7)
        // Noise: identify the unsupported operation instead of shipping coordinates and punctuation.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] Point reset is not implemented on the host "
            "(NPC index %d, user index %d, points %d). Request ignored.",
            npcIndex, userIndex, point
        );
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
    //     L"NpcSocket.cpp",
    //     0x1cd0)
    // Noise: identify the unsupported operation instead of shipping coordinates and punctuation.
    L2_LOG_ERROR(
        Log,
        "[Olympiad] NPC function %u is not implemented (NPC index %d, user index %d, points %d). "
        "Request ignored.",
        static_cast<unsigned>(functionId), npcIndex, userIndex, point
    );
    return false;
}

bool NpcSuicide(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid;
    std::int32_t reason;
    ::Disassemble(packet, "dd", &creatureSid, &reason);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcSuicide failed. cannot find npc nType[%d]",
        //     L"NpcSocket.cpp",
        //     0x1797,
        //     reason)
        // Noise: replace shipping coordinates with the missing target and requested reason.
        L2_LOG_ERROR(
            Log, "[NPC commands] Suicide request ignored: creature SID %d was not found (reason %d).", creatureSid, reason
        );
    } else {
        creature->Suicide(DAMAGE_SUICIDE, reason);
    }
    return false;
}

bool NpcSystemMessage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    char16_t message[1000];
    ::Disassemble(packet, "dS", &userSid, 2000, message);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    if (creature != nullptr && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->SendSystemMessage(u"NPC", message);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcSystemMessage, cannot find user.",
        //     L"NpcSocket.cpp",
        //     0x17ac)
        // Noise: replace shipping coordinates with the recipient and delivery outcome.
        L2_LOG_ERROR(
            Log, "[NPC messages] System message was not sent: recipient SID %d is missing or is not a player.", userSid
        );
    }
    return false;
}

bool NpcStopMove(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a0634, NC_PROTOCOL 0x60.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    ::Disassemble(packet, "d", &npcSid);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        npc->StopMove();
    }
    return false;
}

bool NpcChangeStopType(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089fa2c, NC_PROTOCOL 0x34.
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid = 0;
    std::int32_t stopType;
    std::int32_t delayTime = 0;
    ::Disassemble(packet, "ddd", &creatureSid, &stopType, &delayTime);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid sid[%x]",
        //     L"NpcSocket.cpp",
        //     0x12f2,
        //     creatureSid)
        // Noise: replace shipping coordinates with the rejected command's parameters.
        L2_LOG_ERROR(
            Log,
            "[NPC movement] ChangeStopType rejected: creature SID %d was not found "
            "(stop type %d, delay %d).",
            creatureSid, stopType, delayTime
        );
        return false;
    }

    if (stopType == STP_SIT) {
        creature->GetController()->Sit(delayTime, nullptr);
    } else if (stopType == STP_STAND) {
        creature->GetController()->Stand(delayTime);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid stoptype[%d]",
        //     L"NpcSocket.cpp",
        //     0x12ff,
        //     stopType)
        // Noise: replace shipping coordinates with the rejected command and supported values.
        L2_LOG_ERROR(
            Log,
            "[NPC movement] ChangeStopType rejected for creature SID %d: stop type %d "
            "is neither sit nor stand (delay %d).",
            creatureSid, stopType, delayTime
        );
    }
    return false;
}

bool NpcShowQuestInfo(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcId;
    std::int32_t userSid;
    std::int32_t userIndex;
    ::Disassemble(packet, "ddd", &npcId, &userSid, &userIndex);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->GetSocket()->Send("ch", 0xfe, 0x20);
    }
    return false;
}

bool NpcSocket::ShowStatPage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    ::Disassemble(packet, "d", &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->GetSocket()->Send("chd", 0xfe, 0x10a, -2);
    }
    return false;
}

bool NpcSocket::RequestTotalSkillEnchantCount(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a6770, NC_PROTOCOL 0xfc.
    L2_GUARD_FUNCTION();

    std::int32_t requestId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &requestId, &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SendTotalSkillEnchantCountToNpcServer(requestId);
    }
    return false;
}

bool SendBufferToUser(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    std::int32_t length;
    const unsigned char* const cursor = ::Disassemble(packet, "dd", &userSid, &length);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] no user",
        //     L"NpcSocket.cpp",
        //     0x1cea)
        // Noise: replace shipping coordinates with the failed delivery context.
        L2_LOG_ERROR(
            Log, "[NPC messages] Buffer was not forwarded: user SID %d was not found (requested length %d).", userSid, length
        );
    } else {
        user->GetSocket()->Send("b", length, cursor);
    }
    return false;
}

bool DeletePreviousOlympiadPoint(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a0cf8, NC_PROTOCOL 0x7b.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t previousOlympiadPoint = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &previousOlympiadPoint);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DeletePreviousOlympiadPoint Failed. "
        //     L"User sid[%d] is null",
        //     L"NpcSocket.cpp",
        //     0x1cfe,
        //     userSid)
        // Noise: replace shipping coordinates with request identifiers and the actual outcome.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] DeletePreviousOlympiadPoint from NPC SID %d was not submitted to the database: "
            "user SID %d was not found (requested points %d).",
            npcSid, userSid, previousOlympiadPoint
        );
    } else {
        DxDeletePreviousOlympiadPoint request(static_cast<int>(user->GetDBID()), previousOlympiadPoint, npcSid, userSid);
        db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x2c, static_cast<int>(sizeof(request)), request);
    }
    return false;
}

bool DeleteOlympiadTradePoint(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &point);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DeleteOlympiadTradePoint Failed. "
        //     L"User sid[%d] is null",
        //     L"NpcSocket.cpp",
        //     0x1d15,
        //     userSid)
        // Noise: replace shipping coordinates with request context.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] DeleteOlympiadTradePoint from NPC SID %d rejected: "
            "user SID %d was not found (requested points %d).",
            npcSid, userSid, point
        );
    } else if (WOlympiadServiceConfig::inst().IsHost()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NpcSocket.cpp",
        //     0x1d1c)
        // Noise: replace shipping coordinates and emphatic punctuation with the unsupported operation.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] DeleteOlympiadTradePoint is not implemented on the host: "
            "NPC SID %d, user SID %d, points %d. No deletion is performed by this handler.",
            npcSid, userSid, point
        );
    } else {
        CSPointer<User> retained(user, ObjectRefType::FunctionChain);
        if (!WOlympiadServiceProxy::inst().DeleteTradePoint(retained, point, static_cast<std::uint32_t>(npcSid))) {
            NpcServerUtil::SendDeleteOlympiadTradePointReturned(npcSid, userSid, point, 0);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]DeleteTradePoint failed: %d",
            //     L"NpcSocket.cpp",
            //     0x1d23,
            //     static_cast<int>(user->GetDBID()))
            // Noise: replace shipping coordinates with the service failure and request context.
            L2_LOG_ERROR(
                Log,
                "[Olympiad] DeleteTradePoint returned failure for character %u, NPC SID %d, points %d. "
                "A failure reply to the NPC server was attempted.",
                static_cast<unsigned>(user->GetDBID()), npcSid, point
            );
        }
    }
    return false;
}

bool AddOlympiadTradePoint(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t userSid;
    std::int32_t point;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &point);

    static_cast<void>(npcSid);
    static_cast<void>(point);
    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AddOlympiadTradePoint Failed. "
        //     L"User sid[%d] is null",
        //     L"NpcSocket.cpp",
        //     0x1d3c,
        //     userSid)
        // Noise: replace shipping coordinates with request context.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] AddOlympiadTradePoint from NPC SID %d rejected: "
            "user SID %d was not found (requested points %d).",
            npcSid, userSid, point
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"NpcSocket.cpp",
        //     0x1d41)
        // Noise: replace shipping coordinates and emphatic punctuation with the unsupported operation.
        L2_LOG_ERROR(
            Log,
            "[Olympiad] AddOlympiadTradePoint is not implemented: NPC SID %d, user SID %d, points %d. "
            "No addition is performed by this handler.",
            npcSid, userSid, point
        );
    }
    return false;
}

bool NpcAddPCSocial(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t actionId = 0;
    ::Disassemble(packet, "dd", &userSid, &actionId);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SocialAction(actionId, 0x800, false);
    }
    return false;
}

bool SendUSMEvent(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t eventId = 0;
    std::int32_t inZoneId = 0;
    ::Disassemble(packet, "ddd", &userSid, &eventId, &inZoneId);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr && (inZoneId == 0 || static_cast<std::uint32_t>(inZoneId) == user->GetInZoneID())) {
        user->SendUSMEventPacket(eventId);
    }
    return false;
}

bool BroadcastUSMEvent(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        TLMFunc(int eventId, int inZoneId) : m_nEventId(eventId), m_nInZoneId(inZoneId) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                if (m_nInZoneId == 0 || static_cast<std::uint32_t>(m_nInZoneId) == user->GetInZoneID()) {
                    user->PushSerializedMessage(new CSerializedSendUSMEventMessage(user, m_nEventId));
                }
            }
            return true;
        }

    private:
        int m_nEventId;
        int m_nInZoneId;
    };

    std::int32_t npcSid = 0;
    std::int32_t eventId = 0;
    std::int32_t distance = 0;
    std::int32_t inZoneId = 0;
    ::Disassemble(packet, "dddd", &npcSid, &eventId, &distance, &inZoneId);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && distance > 0) {
        const FVector position = npc->GetPos();
        CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(distance));
        TLMFunc function(eventId, inZoneId);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return false;
}

bool SendUIEvent(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::int32_t value3 = 0;
    std::array<char16_t, 20> text1{};
    std::array<char16_t, 20> text2{};
    std::array<char16_t, 20> text3{};
    std::array<char16_t, 1000> text4{};
    std::array<char16_t, 20> text5{};
    std::array<char16_t, 20> text6{};

    const unsigned char* const cursor = ::Disassemble(packet, "d", &userSid);
    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        ::Disassemble(
            cursor, "dddSSSSSS", &value1, &value2, &value3, 0x28, text1.data(), 0x28, text2.data(), 0x28, text3.data(), 2000,
            text4.data(), 0x28, text5.data(), 0x28, text6.data()
        );
        user->GetSocket()->Send(
            "chddddSSSSSS", 0xfe, 0x8f, userSid, value1, value2, value3, text1.data(), text2.data(), text3.data(),
            text4.data(), text5.data(), text6.data()
        );
    }
    return false;
}

bool SendUIEventFStr(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t value1 = 0;
    std::int32_t value2 = 0;
    std::int32_t value3 = 0;
    std::int32_t value4;
    std::array<char16_t, 20> shortText1{};
    std::array<char16_t, 20> shortText2{};
    std::array<char16_t, 20> shortText3{};
    std::array<char16_t, 20> shortText4{};
    std::array<char16_t, 20> shortText5{};
    std::array<char16_t, 256> longText1{};
    std::array<char16_t, 256> longText2{};
    std::array<char16_t, 256> longText3{};
    std::array<char16_t, 256> longText4{};
    std::array<char16_t, 256> longText5{};

    const unsigned char* const cursor = ::Disassemble(packet, "d", &userSid);
    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        ::Disassemble(
            cursor, "dddSSSSSdSSSSS", &value1, &value2, &value3, 0x28, shortText1.data(), 0x28, shortText2.data(), 0x28,
            shortText3.data(), 0x28, shortText4.data(), 0x28, shortText5.data(), &value4, 0x200, longText1.data(), 0x200,
            longText2.data(), 0x200, longText3.data(), 0x200, longText4.data(), 0x200, longText5.data()
        );
        user->GetSocket()->Send(
            "chddddSSSSSdSSSSS", 0xfe, 0x8f, userSid, value1, value2, value3, shortText1.data(), shortText2.data(),
            shortText3.data(), shortText4.data(), shortText5.data(), value4, longText1.data(), longText2.data(),
            longText3.data(), longText4.data(), longText5.data()
        );
    }
    return false;
}

bool BroadcastUIEventFStr(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        explicit TLMFunc(const unsigned char* packetValue) : packet(packetValue) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->PushSerializedMessage(new CSerializedSendUIEventMessage(user, packet));
            }
            return true;
        }

    private:
        const unsigned char* packet;
    };

    std::int32_t npcSid = 0;
    std::int32_t distance = 0;
    const unsigned char* const eventPacket = ::Disassemble(packet, "dd", &npcSid, &distance);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && distance > 0) {
        const FVector position = npc->GetPos();
        CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(distance));
        TLMFunc function(eventPacket);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return false;
}

bool NpcCreateOneAnother(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t npcClassId;
    char16_t aiName[104];
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::int32_t heading;
    ::Disassemble(packet, "dSdddd", &npcClassId, 200, aiName, &x, &y, &z, &heading);

    if (npcClassId > 1000000) {
        CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
        if (object != nullptr && object->IsNPC() && !object->IsSummonOrPet()) {
            std::wstring hostAiNameStorage;
            NpcServerUtil::SendRequestNpcSpawn(g_ObjectDB.GetName(npcClassId), FVector(x, y, z), heading, aiName, 0x10000);
        }
    }
    return false;
}

bool NpcSocket::NpcCleftManagerEnter(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);
    Singleton<CCleft>::inst().RegisterManagerNpcId(npcSid);
    return false;
}

bool NpcSetXMasEventState(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t buffId = 0;
    std::int32_t state = 0;
    ::Disassemble(packet, "dd", &buffId, &state);

    CEvent_XmasSanta* const event =
        static_cast<CEvent_XmasSanta*>(Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_XMAS_SANTA));
    if (event != nullptr) {
        XmasSantaEventInfo* const info = event->GetEventInfo(buffId);
        if (info != nullptr) {
            if (state == 0) {
                runtime::raw_atomic::ExchangeI32(&info->m_nOnStartTurky, 1);
                runtime::raw_atomic::ExchangeI32(&info->m_nOnStartSanta, 1);
            } else if (state == 1) {
                runtime::raw_atomic::ExchangeI32(&info->m_nOnStartTurky, 0);
                info->m_bIsActive = true;
            }
        }
    }
    return false;
}

bool NpcSocket::NpcEventManagetEnter(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);
    CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_XMAS_SANTA);
    if (event != nullptr) {
        event->m_ManagerNpcId = npcSid;
    }
    return false;
}

bool NpcSocket::NpcYearCatchEventManagerEnter(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);
    CEvent* const event = Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_YEAR_CATCH);
    if (event != nullptr) {
        event->m_ManagerNpcId = npcSid;
    }
    return false;
}

bool NpcSetYearCatchEventState(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t state = 0;
    ::Disassemble(packet, "d", &state);

    CEvent_YearCatch* const event =
        static_cast<CEvent_YearCatch*>(Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_YEAR_CATCH));
    if (event != nullptr) {
        if (state == 0) {
            event->SetOff();
        } else if (state == 1) {
            event->SetOn();
        }
    }
    return false;
}

bool NpcSocket::NpcShowInTerritory(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t inZoneId = 0;
    std::uint32_t type = 0;
    std::array<char16_t, 0x2800> territoryName;
    const unsigned char* const cursor = ::Disassemble(packet, "ddS", &inZoneId, &type, 0x5000, territoryName.data());

    CTerritory* const territory = g_NpcAnnounceDB.GetNpcPos(territoryName.data());
    if (territory == nullptr) {
        return false;
    }

    if (type == 0) {
        class CShowInTerritory1 : public CTLMFunc {
        public:
            explicit CShowInTerritory1(std::uint32_t messageId) : messageId_(messageId) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    object->CastUser()->SendSystemMessage(messageId_);
                }
                return true;
            }

        private:
            std::uint32_t messageId_;
        };

        std::uint32_t messageId = 0;
        ::Disassemble(cursor, "d", &messageId);
        CShowInTerritory1 function(messageId);
        CTLMFuncInTerritory filter(inZoneId, territory);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    } else if (type == 1) {
        class CShowInTerritory2 : public CTLMFunc {
        public:
            explicit CShowInTerritory2(const char16_t* message) : message_(message) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    object->CastUser()->GetSocket()->Send(
                        "chccS", 0x62, SystemMessageId::NPC_SYSTEM_MESSAGE_TEXT, 1, 0, message_
                    );
                }
                return true;
            }

        private:
            const char16_t* message_;
        };

        std::array<char16_t, 0x2800> message;
        ::Disassemble(cursor, "S", 0x5000, message.data());
        CShowInTerritory2 function(message.data());
        CTLMFuncInTerritory filter(inZoneId, territory);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    } else if (type == 2) {
        class CShowInTerritory2 : public CTLMFunc {
        public:
            CShowInTerritory2(
                std::uint32_t messageId,
                const char16_t* argument1,
                const char16_t* argument2,
                const char16_t* argument3,
                const char16_t* argument4,
                const char16_t* argument5
            ) :
                messageId_(messageId),
                argument1_(argument1),
                argument2_(argument2),
                argument3_(argument3),
                argument4_(argument4),
                argument5_(argument5) {}

            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    object->CastUser()->GetSocket()->Send(
                        "chccdSSSSS", 0x62, SystemMessageId::NPC_SYSTEM_MESSAGE_TEXT, 1, 0x0e, messageId_, argument1_,
                        argument2_, argument3_, argument4_, argument5_
                    );
                }
                return true;
            }

        private:
            std::uint32_t messageId_;
            const char16_t* argument1_;
            const char16_t* argument2_;
            const char16_t* argument3_;
            const char16_t* argument4_;
            const char16_t* argument5_;
        };

        std::uint32_t messageId;
        std::array<char16_t, 0x100> argument1;
        std::array<char16_t, 0x100> argument2;
        std::array<char16_t, 0x100> argument3;
        std::array<char16_t, 0x100> argument4;
        std::array<char16_t, 0x100> argument5;
        ::Disassemble(
            cursor, "dSSSSS", &messageId, 0x200, argument1.data(), 0x200, argument2.data(), 0x200, argument3.data(), 0x200,
            argument4.data(), 0x200, argument5.data()
        );
        CShowInTerritory2 function(
            messageId, argument1.data(), argument2.data(), argument3.data(), argument4.data(), argument5.data()
        );
        CTLMFuncInTerritory filter(inZoneId, territory);
        FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);
    }
    return false;
}

bool BroadcastUIEventFStrInTerritory(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        explicit TLMFunc(const unsigned char* packetValue) : packet(packetValue) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->PushSerializedMessage(new CSerializedSendUIEventMessage(user, packet));
            }
            return true;
        }

    private:
        const unsigned char* packet;
    };

    std::int32_t inZoneId = 0;
    std::array<char16_t, 0x2800> territoryName{};
    const unsigned char* const eventPacket = ::Disassemble(packet, "dS", &inZoneId, 0x5000, territoryName.data());

    CTerritory* const territory = g_NpcAnnounceDB.GetNpcPos(territoryName.data());
    if (territory == nullptr) {
        return false;
    }

    TLMFunc function(eventPacket);
    CTLMFilterTerritory filter(inZoneId, territory);
    // Current L2Server.exe 0x008a1fd0 calls the user-distance wrapper
    // directly. The territory condition remains owned by the filter.
    FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    return false;
}

bool NpcShowScreenMessage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t mode = 0;
    std::int32_t userSid = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &mode, &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        std::int32_t messageId = -1;
        std::int32_t value1;
        std::int32_t value2;
        std::int32_t value3;
        std::int32_t value4;
        std::int32_t value5;
        std::int32_t value6;
        std::int32_t value7;
        std::int32_t value8;
        std::array<char16_t, 2000> message{};
        cursor = ::Disassemble(cursor, "dddddddd", &value1, &value2, &value3, &value4, &value5, &value6, &value7, &value8);
        if (mode == 0) {
            ::Disassemble(cursor, "d", &messageId);
        } else if (mode == 1) {
            ::Disassemble(cursor, "S", 4000, message.data());
        }
        user->GetSocket()->Send(
            "chdddddddddddS", 0xfe, 0x39, mode, messageId, value1, value2, value3, value4, value5, value6, value7, value8,
            -1, message.data()
        );
    }
    return false;
}

bool NpcShowScreenFStrMessage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t mode = 0;
    std::int32_t userSid = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &mode, &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        std::int32_t messageId = -1;
        std::int32_t fStringId = -1;
        std::int32_t value1;
        std::int32_t value2;
        std::int32_t value3;
        std::int32_t value4;
        std::int32_t value5;
        std::int32_t value6;
        std::int32_t value7;
        std::int32_t value8;
        char16_t text1[0x100];
        char16_t text2[0x100];
        char16_t text3[0x100];
        char16_t text4[0x100];
        char16_t text5[0x100];
        cursor = ::Disassemble(cursor, "dddddddd", &value1, &value2, &value3, &value4, &value5, &value6, &value7, &value8);
        if (mode == 0) {
            ::Disassemble(cursor, "d", &messageId);
        } else if (mode == 1) {
            ::Disassemble(
                cursor, "dSSSSS", &fStringId, 0x200, text1, 0x200, text2, 0x200, text3, 0x200, text4, 0x200, text5
            );
        }
        user->GetSocket()->Send(
            "chdddddddddddSSSSS", 0xfe, 0x39, mode, messageId, value1, value2, value3, value4, value5, value6, value7,
            value8, fStringId, text1, text2, text3, text4, text5
        );
    }
    return false;
}

bool NpcBroadcastScreenMessage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        TLMFunc(const std::int32_t* values, std::int32_t mode, std::int32_t messageId, const char16_t* message) :
            m_pValues(values), m_nMode(mode), m_nMessageId(messageId), m_pMessage(message) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->GetSocket()->Send(
                    "chdddddddddddS", 0xfe, 0x39, m_nMode, m_nMessageId, m_pValues[0], m_pValues[1], m_pValues[2],
                    m_pValues[3], m_pValues[4], m_pValues[5], m_pValues[6], m_pValues[7], -1, m_pMessage
                );
            }
            return true;
        }

    private:
        const std::int32_t* m_pValues;
        std::int32_t m_nMode;
        std::int32_t m_nMessageId;
        const char16_t* m_pMessage;
    };

    std::int32_t mode = 0;
    std::int32_t npcSid = 0;
    std::int32_t radius = 0;
    std::int32_t messageId = -1;
    std::int32_t values[8];
    std::array<char16_t, 2000> message{};
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &mode, &npcSid, &radius);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && radius > 0) {
        cursor = ::Disassemble(
            cursor, "dddddddd", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6],
            &values[7]
        );
        if (mode == 0) {
            ::Disassemble(cursor, "d", &messageId);
        } else if (mode == 1) {
            ::Disassemble(cursor, "S", 4000, message.data());
        }

        const FVector position = npc->GetPos();
        CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(radius));
        TLMFunc function(values, mode, messageId, message.data());
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return false;
}

bool NpcBroadcastScreenFStrMessage(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    struct MessageData {
        std::int32_t values[8];
        std::int32_t messageId;
        std::int32_t fStringId;
        char16_t text1[0x100];
        char16_t text2[0x100];
        char16_t text3[0x100];
        char16_t text4[0x100];
        char16_t text5[0x100];
    };

    class TLMFunc : public CTLMFunc {
    public:
        TLMFunc(const MessageData* data, std::int32_t mode, const char16_t* message) :
            m_pData(data), m_nMode(mode), m_pMessage(message) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->GetSocket()->Send(
                    "chdddddddddddSSSSS", 0xfe, 0x39, m_nMode, m_pData->messageId, m_pData->values[0], m_pData->values[1],
                    m_pData->values[2], m_pData->values[3], m_pData->values[4], m_pData->values[5], m_pData->values[6],
                    m_pData->values[7], m_pData->fStringId, m_pData->text1, m_pData->text2, m_pData->text3, m_pData->text4,
                    m_pData->text5
                );
            }
            return true;
        }

    private:
        const MessageData* m_pData;
        std::int32_t m_nMode;
        [[maybe_unused]] const char16_t* m_pMessage;
    };

    std::int32_t mode = 0;
    std::int32_t npcSid = 0;
    std::int32_t radius = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &mode, &npcSid, &radius);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && radius >= 0) {
        std::array<char16_t, 2000> message{};
        MessageData data;
        data.messageId = -1;
        data.fStringId = -1;
        cursor = ::Disassemble(
            cursor, "dddddddd", &data.values[0], &data.values[1], &data.values[2], &data.values[3], &data.values[4],
            &data.values[5], &data.values[6], &data.values[7]
        );
        if (mode == 0) {
            ::Disassemble(cursor, "d", &data.messageId);
        } else if (mode == 1) {
            ::Disassemble(
                cursor, "dSSSSS", &data.fStringId, 0x200, data.text1, 0x200, data.text2, 0x200, data.text3, 0x200,
                data.text4, 0x200, data.text5
            );
        }

        if (radius == 0) {
            CUserBroadcaster::inst().BroadcastToAllUserEx(
                "chdddddddddddSSSSS", 0xfe, 0x39, mode, data.messageId, data.values[0], data.values[1], data.values[2],
                data.values[3], data.values[4], data.values[5], data.values[6], data.values[7], data.fStringId, data.text1,
                data.text2, data.text3, data.text4, data.text5
            );
        } else {
            const FVector position = npc->GetPos();
            CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(radius));
            TLMFunc function(&data, mode, message.data());
            FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
        }
    }
    return false;
}

bool NpcSetParam(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcServerId = 0;
    std::int32_t parameterType = 0;
    double value = 0.0;
    ::Disassemble(packet, "ddf", &npcServerId, &parameterType, &value);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
    return npc != nullptr ? npc->OnNpcSetParam(static_cast<CHAR_PARAM_TYPE>(parameterType), value) : false;
}

bool NpcCheckCursedUser(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &npcSid, &userSid);

    std::int32_t result = 2;
    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcCheckCursedUser Failed. User sid[%d] is null",
        //     L"NpcSocket.cpp",
        //     0x2039,
        //     userSid)
        // Noise: replace shipping coordinates with request context and the fallback result.
        L2_LOG_ERROR(
            Log,
            "[Cursed weapons] Cannot check user SID %d for NPC SID %d: user was not found. "
            "The reply will use result 2.",
            userSid, npcSid
        );
    } else {
        static_cast<void>(CCursedWeaponMgr::inst());
        result = user->m_nCursedWeaponClassId > 0 ? 1 : 0;
    }

    g_npcserver.Send("cddd", 0x58, npcSid, userSid, result);
    return false;
}

bool NpcShowCursedWeaponStatus(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t itemClassId = 0;
    ::Disassemble(packet, "dd", &userSid, &itemClassId);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcShowCursedWeaponStatus Failed. "
        //     L"User sid[%d] is null",
        //     L"NpcSocket.cpp",
        //     0x2059,
        //     userSid)
        // Noise: replace shipping coordinates with the requested weapon and recipient.
        L2_LOG_ERROR(
            Log,
            "[Cursed weapons] Cannot show status for item class %d: user SID %d was not found. "
            "No status is displayed.",
            itemClassId, userSid
        );
    } else {
        CCursedWeaponMgr::inst().ShowCursedWeaponStatus(user, itemClassId);
    }
    return false;
}

bool NpcTBAgitCheckMemberRegisterStatus(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b0a18, NC_PROTOCOL 0x90.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t residenceId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &residenceId);

    CCreature* const npc = FindCreatureBySid(npcSid);
    User* const user = FindUserBySid(userSid);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* worldAgit = nullptr;
    if (residence != nullptr && residence->IsWorldAgit()) {
        worldAgit = residence->CastWorldAgit();
    }

    if (npc != nullptr && user != nullptr && worldAgit != nullptr) {
        std::int32_t result = 0;
        if (worldAgit->IsTeamBattleAgit() && user->GetPledge() != nullptr) {
            const std::uint32_t userDatabaseId = user->GetDBID();
            CTeamBattleAgit* const teamBattleAgit = worldAgit->CastTeamBattleAgit();
            if (teamBattleAgit->IsMasterEntryMember(userDatabaseId)) {
                result = 1;
            } else {
                const std::uint32_t secondUserDatabaseId = user->GetDBID();
                CPledge* const pledge = user->GetPledge();
                const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
                CTeamBattleAgit* const secondTeamBattleAgit = worldAgit->CastTeamBattleAgit();
                if (secondTeamBattleAgit->IsBattleRoyalEntryMember(pledgeDatabaseId, secondUserDatabaseId)) {
                    result = 1;
                }
            }
        }

        g_npcserver.Send("cddd", 0x5d, npcSid, userSid, result);
    }
    return false;
}

bool NpcAuctionAgitGetCostInfo(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089fdac, NC_PROTOCOL 0x91.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t residenceId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &residenceId);

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);

    CAuctionAgit* auctionAgit = nullptr;
    CWorldAgit* worldAgit = nullptr;
    if (residence != nullptr) {
        // Current calls the CWorldAgit slots directly for this residence
        // without first calling IsWorldAgit/CastWorldAgit.
        worldAgit = static_cast<CWorldAgit*>(residence);
        auctionAgit = worldAgit->CastAuctionAgit();
    }

    if (npc != nullptr && user != nullptr && auctionAgit != nullptr && worldAgit->IsAuctionAgit() &&
        auctionAgit->GetNextCost() > 0) {
        g_npcserver.Send(
            "cddQd", 0x5f, static_cast<std::int32_t>(npc->m_nId), static_cast<std::int32_t>(user->m_nId),
            residence->m_data->m_nAgitLease, auctionAgit->GetNextCost()
        );
    }
    return false;
}

bool NpcSocket::NpcGetItem(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a9380, NC_PROTOCOL 0x17.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    std::int32_t itemIndex;
    ::Disassemble(packet, "dd", &npcIndex, &itemIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!creature || !creature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc",
        //     L"NpcSocket.cpp",
        //     0xc0d)
        // Noise: replace shipping coordinates with the failed pickup request.
        L2_LOG_ERROR(
            Log, "[NPC items] GetItem rejected for item index %d: NPC index %d is missing or is not an NPC.", itemIndex,
            npcIndex
        );
        return false;
    }

    CNPC* const npc = creature->CastNPC();
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     "item null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0xc15)
        // Noise: replace shipping coordinates with the failed pickup request.
        L2_LOG_ERROR(
            Log, "[NPC items] GetItem rejected for NPC index %d: item index %d was not found.", npcIndex, itemIndex
        );
        return false;
    }

    npc->Action(item->CastItem(), false, false);
    return false;
}

bool NpcClassChange(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a6ecc, NC_PROTOCOL 0x18.
    L2_GUARD_FUNCTION();

    std::int32_t creatureSid;
    std::int32_t unusedCreatureIndex;
    std::int32_t level;
    std::int32_t classType;
    ::Disassemble(packet, "dddd", &creatureSid, &unusedCreatureIndex, &level, &classType);
    static_cast<void>(unusedCreatureIndex);

    CSPointer<CCreature> creature = CCreature::FindObjectSP(static_cast<std::uint32_t>(creatureSid));
    if (!creature || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (runtime::raw_atomic::CompareExchangeI32(&user->m_lSaveCharacter, 1, 0) != 0) {
        return false;
    }

    creature->ChangeClass(static_cast<ClassType>(classType), level);
    DxSetClass request(
        static_cast<int>(creature->GetDBID()), creature->m_data->m_nSubJobId, static_cast<int>(creature->m_data->m_nClass)
    );
    db.RequestTPacketProcess(creature->CastUser()->m_data->m_nWorldId, 0x23, static_cast<int>(sizeof(request)), request);
    db.SendSaveCharacterInfo(creature->CastUser(), false, true);
    return false;
}

bool NpcSetWeightPoint(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a7290, NC_PROTOCOL 0x19.
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid;
    std::int32_t creatureIndex;
    std::int32_t weightPoint;
    ::Disassemble(packet, "ddd", &creatureSid, &creatureIndex, &weightPoint);

    static_cast<void>(creatureIndex);
    CSPointer<CCreature> creature = CCreature::FindObjectSP(static_cast<std::uint32_t>(creatureSid));
    if (creature) {
        {
            L2_GUARD(*creature->m_lock);
            creature->m_data->m_nWeightPoint = weightPoint;
        }
    }
    return false;
}

bool NpcLeaveWorld(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a965c, NC_PROTOCOL 0x1a.
    L2_GUARD_FUNCTION();

    std::int32_t npcIndex;
    ::Disassemble(packet, "d", &npcIndex);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetData(npcIndex)->m_bIsUser) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] not a npc",
        //     L"NpcSocket.cpp",
        //     0xc6b)
        // Noise: replace shipping coordinates with the exact guard that rejected the request.
        L2_LOG_ERROR(
            Log, "[NPC lifecycle] LeaveWorld rejected for index %d: shared creature data marks it as a user.", npcIndex
        );
        return false;
    }

    CSPointer<CCreature> creature = creatures.FindObjectSP(npcIndex);
    if (!creature || !creature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] not a creature",
        //     L"NpcSocket.cpp",
        //     0xc72)
        // Noise: replace shipping coordinates with the missing or incompatible target.
        L2_LOG_ERROR(
            Log, "[NPC lifecycle] LeaveWorld rejected for index %d: creature is missing or is not an NPC.", npcIndex
        );
        return false;
    }

    creature->Delete();
    return false;
}

bool NPCTeleportToUser(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t targetUserSid = 0;
    std::int32_t sourceUserSid = 0;
    ::Disassemble(packet, "ddd", &npcSid, &targetUserSid, &sourceUserSid);

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const targetUser = FindUserBySid(targetUserSid);
    User* const sourceUser = FindUserBySid(sourceUserSid);
    User::CallPCResult result = User::CPR_ETC;
    if (!L2Common::IsAliveNPC(npc) || !L2Common::IsAlivePC(targetUser) || !L2Common::IsAlivePC(sourceUser)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCTeleportToUser. Failed. src or dest is not AlivePC. %d %d",
        //     targetUserSid,
        //     sourceUserSid)
        L2_LOG_ERROR(
            Log,
            "[teleport] Teleport-to-user request rejected: target user SID %d, source user SID %d, "
            "or NPC SID %d failed its alive/type check.",
            targetUserSid, sourceUserSid, npcSid
        );
    } else if (User::IsCallPCEnable(sourceUser, targetUser, false, result)) {
        const FVector position = sourceUser->GetPos();
        targetUser->StopMove();
        targetUser->TeleportToLocation(
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), sourceUser->GetInZoneID(), 0x10000,
            TeleportSourceType::NPCTeleportToUser, nullptr, 0
        );
    }

    g_npcserver.Send("cdddd", 0x62, npcSid, targetUserSid, sourceUserSid, result);
    return false;
}

bool ShowVariationMakeWindow(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &userSid, &npcSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr && user->IsPC()) {
        CNPC* const npc = FindNpcBySid(npcSid);
        if (npc != nullptr && npc->IsNPC() && user->IsTalkableDistance(npc)) {
            TalkChannelCache::inst().mChannels[8].Cache(user->Id(), npc->Id(), 0);
            user->GetSocket()->Send("ch", 0xfe, 0x52);
        }
    }
    return false;
}

bool ShowVariationCancelWindow(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &userSid, &npcSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr && user->IsPC()) {
        CNPC* const npc = FindNpcBySid(npcSid);
        if (npc != nullptr && npc->IsNPC() && user->IsTalkableDistance(npc)) {
            TalkChannelCache::inst().mChannels[8].Cache(user->Id(), npc->Id(), 0);
            user->GetSocket()->Send("ch", 0xfe, 0x53);
        }
    }
    return false;
}

bool NpcInstantZoneEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089c588, NC_PROTOCOL 0xa1.
    L2_GUARD_FUNCTION();
    CInstantZoneNpcAIEventHandler::Receive(packet);
    return false;
}

bool NpcShowChangePledgeNameUI(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t pledgeId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &pledgeId);
    static_cast<void>(pledgeId);

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    if (npc == nullptr || user == nullptr) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    CSharedPledgeData* const pledgeData = pledge != nullptr ? pledge->SharedData() : nullptr;
    if (pledgeData == nullptr || pledgeData->m_nPrevServer == 0 || !user->IsPledgeMaster()) {
        return false;
    }

    TalkChannelCache::inst().mChannels[10].Cache(user->Id(), npc->Id(), 0);
    user->GetSocket()->Send("chddS", 0xfe, 0x6a, 1, 0, pledgeData->m_sOriginPledgeName);
    return false;
}

bool NpcSetStaticMeshStatus(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a396c, NC_PROTOCOL 0xa3.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::array<char16_t, 0x100> doorName{};
    std::int32_t targetable = 0;
    std::int32_t stealth = 0;
    std::uint32_t inZoneId = 0;
    ::Disassemble(packet, "dSddd", &userSid, 0x200, doorName.data(), &targetable, &stealth, &inZoneId);

    CCreature* const creature = FindCreatureBySid(userSid);
    std::wstring hostDoorName;
    CDoor* const door = g_DoorDB.GetDoor(doorName.data(), inZoneId);
    if (door == nullptr) {
        return false;
    }

    if (userSid >= 0 && creature != nullptr && creature->IsPC()) {
        door->SetTargetable(targetable != 0, true);
        door->m_data->m_nStealth = stealth;

        CUserSocket* userSocket = nullptr;
        if (creature->CastUser() != nullptr) {
            userSocket = creature->CastUser()->GetSocket();
        }
        door->SendDoorStatus(userSocket);

        if (door->m_pEventController != nullptr && door->m_data->m_nStealth > 0) {
            door->m_pEventController->SetOnOff(!door->m_data->m_bIsOpen);
        }
    } else {
        door->SendDoorStatus(nullptr);
    }
    return false;
}

bool ShowBaseAttributeCancelWindow(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &userSid, &npcSid);

    User* const user = FindUserBySid(userSid);
    if (user == nullptr || !user->IsPC()) {
        return false;
    }
    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr || !npc->IsNPC() || !user->IsTalkableDistance(npc)) {
        return false;
    }

    TalkChannelCache::inst().mChannels[11].Cache(user->Id(), npc->Id(), 0);
    std::array<char, 0x5000> itemPacket{};
    int packetLength = 0;
    int itemCount = 0;
    const bool hadItems = [&] {
        L2_READ_GUARD(user->m_inv);
        CItem* item = user->m_inv.GetFirstItemByEnchantedAttribute();
        if (item == nullptr) {
            return false;
        }

        do {
            const int assembled = ::Assemble(
                itemPacket.data() + packetLength, 0x5000 - packetLength, "dQ", item->Id(),
                item->GetBaseAttributeCancelCommision()
            );
            if (assembled > 0) {
                packetLength += assembled;
                ++itemCount;
            }
            item = user->m_inv.GetNextItemByEnchantedAttribute(item->m_data->index);
        } while (item != nullptr);
        return true;
    }();
    if (!hadItems) {
        user->GetSocket()->Send("chd", 0xfe, 0x75, 0);
        return false;
    }

    user->GetSocket()->Send("chdb", 0xfe, 0x75, itemCount, packetLength, itemPacket.data());
    return false;
}

bool NpcAreaSetBannedTerritoryOnOff(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089d1e8, NC_PROTOCOL 0xa5.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 0x5000> areaName{};
    std::int32_t on = 0;
    std::int32_t index = 0;
    std::uint32_t inZoneId = 0;
    ::Disassemble(packet, "Sddd", 0xa000, areaName.data(), &on, &index, &inZoneId);

    CArea* const area = CAreaDB::inst().FindArea(areaName.data());
    if (area == nullptr) {
        std::wstring hostAreaName;
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid area[%s]",
        //     TargetUtf16ToHostWide(
        //         areaName.data(),
        //         hostAreaName))
        L2_LOG_ERROR(
            Log,
            "[Areas] Cannot change banned-territory state: area '%ls' was not found "
            "(territory index %d, requested on=%d, instance %u).",
            TargetUtf16ToHostWide(areaName.data(), hostAreaName), index, on, static_cast<unsigned>(inZoneId)
        );
        return false;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(inZoneId);
    CInstantZone* const inZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
    if (inZone == nullptr) {
        area->SetBannedTerritoryOnOff(index, on == 1);
    } else {
        inZone->SetBannedTerritoryOnOff(area, index, on == 1);
    }
    return false;
}

bool GiveItemByCastleSiegeDefence(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bc108, NC_PROTOCOL 0xa6.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t pledgeDbId = 0;
    std::int32_t requestedDefenceCount = 0;
    std::int32_t itemClassId = 0;
    std::int32_t enchant = 0;
    std::int64_t itemAmount = 0;
    ::Disassemble(
        packet, "ddddddQ", &npcSid, &userSid, &pledgeDbId, &requestedDefenceCount, &itemClassId, &enchant, &itemAmount
    );

    if (requestedDefenceCount <= 0) {
        return false;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (npc == nullptr || user == nullptr) {
        return false;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || !user->IsPledgeMaster() || static_cast<int>(pledge->GetDbId()) != pledgeDbId) {
        return false;
    }

    if (pledge->GetCastleSiegeDefenceCount() < requestedDefenceCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"TRY to decrease siege defence count more than have. [%d][%d][%d]",
        //     pledgeDbId,
        //     pledge->GetCastleSiegeDefenceCount(),
        //     requestedDefenceCount)
        L2_LOG_ERROR(
            Log, "[Siege] Defence-count exchange rejected for pledge %d: available count %d, requested count %d.",
            pledgeDbId, pledge->GetCastleSiegeDefenceCount(), requestedDefenceCount
        );
        return false;
    }

    std::array<ItemManip, 100> requests{};
    requests[0].mClassId = itemClassId;
    requests[0].mCount = itemAmount;
    requests[0].mEnchanted = enchant;

    EtcManiGiveItemByCastleSiegeCount manipulation(
        static_cast<int>(user->Id()), static_cast<int>(user->GetDBID()), pledgeDbId,
        pledge->GetCastleSiegeDefenceCount() - requestedDefenceCount, npcSid
    );
    CPacketHelperEx<20480> etcPacket;
    manipulation.Assemble(etcPacket);

    L2_GUARD(user->m_ManipulateQueueLock);
    User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_GIVE_CASTLE_ITEM, 0, 1, requests.data(), nullptr, 1, etcPacket, false, ItemGetReason::Etc,
        message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        static_cast<void>(user->_HandleItemManipulation(queue));
    } else if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0x2180)
        // Noise: normal deferral while another item operation is pending. No failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0x2180)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0x2185)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0x2185)
        L2_LOG_ERROR(
            Log, "[NPC items] Unexpected queue result %d for character %u; request handling stopped.",
            static_cast<int>(result), static_cast<unsigned int>(user->GetDBID())
        );
    }
    return false;
}

bool NpcCreatePVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b4fb0, NC_PROTOCOL 0xa7.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t matchType = PVPMT_MAX;
    ::Disassemble(packet, "dd", &managerNpcId, &matchType);

    if (FindNpcBySid(managerNpcId) != nullptr && CPVPMatchSystem::IsValidPvpMatch(matchType)) {
        CPVPMatchSystem::inst().CreateMatch(
            static_cast<std::uint32_t>(managerNpcId), static_cast<PVP_MATCH_TYPE>(matchType)
        );
    }
    return false;
}

bool NpcRegisterPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b50f0, NC_PROTOCOL 0xa8.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t partyId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "ddd", &managerNpcId, &partyId, &userSid);

    CPVPMatchSystem& matches = CPVPMatchSystem::inst();
    const PVPMatchResult result =
        matches.RegisterParty(static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(partyId));
    char registeredParties[256];
    registeredParties[0] = 0;
    std::memset(registeredParties + 1, 0, sizeof(registeredParties) - 1);
    const int packetLength = matches.GetRegisteredPartiesPacket(
        static_cast<std::uint32_t>(managerNpcId), registeredParties, static_cast<int>(sizeof(registeredParties))
    );
    g_npcserver.Send(
        "cdddb", 0x70, managerNpcId, userSid, static_cast<std::int32_t>(result), packetLength, registeredParties
    );
    return false;
}

bool NpcUnregisterPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b52b4, NC_PROTOCOL 0xa9.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t partyId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "ddd", &managerNpcId, &partyId, &userSid);

    if (FindNpcBySid(managerNpcId) != nullptr) {
        CPVPMatchSystem& matches = CPVPMatchSystem::inst();
        matches.UnregisterParty(static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(partyId));
        char registeredParties[256];
        registeredParties[0] = 0;
        std::memset(registeredParties + 1, 0, sizeof(registeredParties) - 1);
        const int packetLength = matches.GetRegisteredPartiesPacket(
            static_cast<std::uint32_t>(managerNpcId), registeredParties, static_cast<int>(sizeof(registeredParties))
        );
        g_npcserver.Send("cddb", 0x71, managerNpcId, userSid, packetLength, registeredParties);
    }
    return false;
}

bool NpcStartPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b54b4, NC_PROTOCOL 0xaa.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t bluePartyId = 0;
    std::int32_t redPartyId = 0;
    ::Disassemble(packet, "ddd", &managerNpcId, &bluePartyId, &redPartyId);

    const bool started = CPVPMatchSystem::inst().StartMatch(
        static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(bluePartyId),
        static_cast<std::uint32_t>(redPartyId)
    );
    g_npcserver.Send("cdddd", 0x72, managerNpcId, bluePartyId, redPartyId, started ? 1 : 0);
    return false;
}

bool NpcEndPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b55f4, NC_PROTOCOL 0xab.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    ::Disassemble(packet, "d", &managerNpcId);

    CPVPMatchSystem& matches = CPVPMatchSystem::inst();
    matches.EndMatch(static_cast<std::uint32_t>(managerNpcId));
    char registeredParties[256];
    registeredParties[0] = 0;
    std::memset(registeredParties + 1, 0, sizeof(registeredParties) - 1);
    const int packetLength = matches.GetRegisteredPartiesPacket(
        static_cast<std::uint32_t>(managerNpcId), registeredParties, static_cast<int>(sizeof(registeredParties))
    );
    g_npcserver.Send("cdb", 0x73, managerNpcId, packetLength, registeredParties);
    return false;
}

bool NpcCheckRegisterParty(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5778, NC_PROTOCOL 0xac.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t partyCount = 0;
    std::int32_t partyId = 0;
    char results[128];
    std::memset(results, 0, sizeof(results));
    const unsigned char* cursor = ::Disassemble(packet, "dd", &managerNpcId, &partyCount);

    int resultsLength = 0;
    for (int index = 0; index < partyCount; ++index) {
        cursor = ::Disassemble(cursor, "d", &partyId);
        const int result = CPVPMatchSystem::inst().CheckRegisterParty(
            static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(partyId)
        );
        resultsLength +=
            ::Assemble(results + resultsLength, static_cast<int>(sizeof(results)) - resultsLength, "dd", partyId, result);
    }
    g_npcserver.Send("cddb", 0x74, managerNpcId, partyCount, resultsLength, results);
    return false;
}

bool NpcRegisterResurrectionTower(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5914, NC_PROTOCOL 0xad.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t team = 0;
    std::int32_t towerObjectId = 0;
    ::Disassemble(packet, "ddd", &managerNpcId, &team, &towerObjectId);

    // The current callback passes these first two lanes in wire order even
    // though the target CPVPMatchSystem body interprets its second lane as
    // the manager-NPC id, then swaps them in the reply. Preserve that
    // shipped mismatch literally.
    const bool registered = CPVPMatchSystem::inst().RegisterResurrectionTower(
        static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(team), static_cast<std::uint32_t>(towerObjectId)
    );
    g_npcserver.Send("cddd", 0x75, team, managerNpcId, registered ? 1 : 0);
    return false;
}

bool NpcCheckRegisterUser(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5b74, NC_PROTOCOL 0xb2.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    ::Disassemble(packet, "dd", &managerNpcId, &userDatabaseId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr) {
        const int result = CPVPMatchSystem::inst().CheckRegisterUser(
            static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(userDatabaseId)
        );
        g_npcserver.Send("cddd", 0x77, managerNpcId, static_cast<std::int32_t>(user->Id()), result);
    }
    return false;
}

bool NpcIsPVPMatching(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5cf0, NC_PROTOCOL 0xb3.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    ::Disassemble(packet, "dd", &managerNpcId, &userDatabaseId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr) {
        const bool matching = CPVPMatchSystem::inst().IsCrataeCubeUser(*user);
        g_npcserver.Send("cddd", 0x78, managerNpcId, static_cast<std::int32_t>(user->Id()), matching ? 1 : 0);
    }
    return false;
}

bool NpcRegisterUserPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5e6c, NC_PROTOCOL 0xb4.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    ::Disassemble(packet, "dd", &managerNpcId, &userDatabaseId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr) {
        const int result = CPVPMatchSystem::inst().RegisterUser(
            static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(userDatabaseId)
        );
        g_npcserver.Send("cddd", 0x79, managerNpcId, static_cast<std::int32_t>(user->Id()), result);
    }
    return false;
}

bool NpcUnregisterUserPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5fe8, NC_PROTOCOL 0xb5.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    ::Disassemble(packet, "dd", &managerNpcId, &userDatabaseId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr && FindNpcBySid(managerNpcId) != nullptr) {
        CPVPMatchSystem::inst().UnregisterUser(static_cast<std::uint32_t>(managerNpcId), *user, 0);
        g_npcserver.Send("cdd", 0x7a, managerNpcId, static_cast<std::int32_t>(user->Id()));
    }
    return false;
}

bool NpcGetRankUserPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b61a4, NC_PROTOCOL 0xb7.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    ::Disassemble(packet, "dd", &managerNpcId, &userDatabaseId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr) {
        const int rank = CPVPMatchSystem::inst().GetRankUserPVPMatch(
            static_cast<std::uint32_t>(managerNpcId), static_cast<std::uint32_t>(userDatabaseId)
        );
        g_npcserver.Send("cddd", 0x7d, managerNpcId, static_cast<std::int32_t>(user->Id()), rank);
    }
    return false;
}

bool NpcAddKillPointUserPVPMatch(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b6320, NC_PROTOCOL 0xb6.
    L2_GUARD_FUNCTION();
    std::int32_t managerNpcId = 0;
    std::int32_t userDatabaseId = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "ddd", &managerNpcId, &userDatabaseId, &point);
    static_cast<void>(managerNpcId);

    User* user = nullptr;
    {
        CSPointer<User> found = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
        user = found.get();
    }
    if (user != nullptr) {
        CPVPMatch* const match = CPVPMatchSystem::inst().GetMatchByUser(*user);
        if (match != nullptr) {
            match->AddKillPoint(user, point);
        }
    }
    return false;
}

bool NpcRegisterUserResurrectionTower(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b5a50, NC_PROTOCOL 0xb8.
    L2_GUARD_FUNCTION();
    std::int32_t userDatabaseId = 0;
    std::int32_t managerNpcId = 0;
    ::Disassemble(packet, "dd", &userDatabaseId, &managerNpcId);

    const bool registered = CPVPMatchSystem::inst().RegisterResurrectionNPC(
        static_cast<std::uint32_t>(userDatabaseId), static_cast<std::uint32_t>(managerNpcId)
    );
    g_npcserver.Send("cddd", 0x7b, managerNpcId, userDatabaseId, registered ? 1 : 0);
    return false;
}

bool NpcDispel(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t targetSid = 0;
    std::int32_t abnormalSlot = 0;
    ::Disassemble(packet, "ddd", &npcSid, &targetSid, &abnormalSlot);

    CNPC* const npc = FindNpcBySid(npcSid);
    return npc != nullptr ? npc->OnNpcDispel(targetSid, static_cast<AbnormalStatusSlotType>(abnormalSlot)) : false;
}

bool NpcRegisterTeleporterType(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t teleporterType = 0;
    std::int32_t parameter = 0;
    ::Disassemble(packet, "ddd", &npcSid, &teleporterType, &parameter);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && npc->IsTeleporter()) {
        CTeleporter* const teleporter = npc->CastTeleporter();
        teleporter->SetType(static_cast<TeleporterType>(teleporterType), parameter);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] Teleporter Name [%s] teleporterType[%d] / param[%d]",
        //     teleporter->GetName(),
        //     teleporterType,
        //     parameter)
        // describe successful mode registration as information.
        // SetType and its invalid-type handling remain unchanged.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] Teleporter Name [%s] teleporterType[%d] / param[%d]",
        //     teleporter->GetName(),
        //     teleporterType,
        //     parameter)
        const wchar_t* mode = nullptr;
        switch (teleporterType) {
            // SetType accepts NONE as well. It does not clear existing modes.
            case TELEPORTER_NONE:
                mode = L"none (existing modes retained)";
                break;
            case TELEPORTER_COMMON:
                mode = L"ordinary";
                break;
            case TELEPORTER_CORETIME:
                mode = L"coretime event";
                break;
            case TELEPORTER_FREE:
                mode = L"free for clanless characters";
                break;
            default:
                break;
        }
        if (mode != nullptr) {
            // L2_LOG_INFO(Log,
            //     L"[teleporter] NPC '%ls' (object ID %u): registered mode '%ls' (type %d). %ls=%d.",
            //     teleporter->GetName(),
            //     static_cast<unsigned int>(npcSid),
            //     mode,
            //     teleporterType,
            //     teleporterType == TELEPORTER_FREE
            //         ? L"maximum character level (inclusive)"
            //         : L"unused parameter",
            //     parameter)
            L2_LOG_INFO(
                Log, u"[teleporter] NPC '%ls' (object ID %u): registered mode '%ls' (type %d); %ls=%d.",
                teleporter->GetName(), static_cast<unsigned int>(npcSid), mode, teleporterType,
                teleporterType == TELEPORTER_FREE ? L"maximum character level (inclusive)" : L"unused parameter", parameter
            );
        } else {
            // L2_LOG_WARN(Log,
            //     L"[teleporter] NPC '%ls' (object ID %u): registration requested with unrecognized mode %d, parameter %d.
            //     see SetType diagnostics for rejected values.", teleporter->GetName(), static_cast<unsigned int>(npcSid),
            //     teleporterType,
            //     parameter)
            L2_LOG_WARN(
                Log,
                u"[teleporter] NPC '%ls' (object ID %u): registration requested with unrecognized mode %d, parameter %d; "
                u"see SetType diagnostics for rejected values.",
                teleporter->GetName(), static_cast<unsigned int>(npcSid), teleporterType, parameter
            );
        }
    }
    return false;
}

bool NpcAbsoluteReturn(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a3c64, NC_PROTOCOL 0xb0.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t mode = 0;
    ::Disassemble(packet, "ddddd", &npcSid, &x, &y, &z, &mode);

    CNPC* const npc = FindNpcBySid(npcSid);
    return npc != nullptr ? npc->OnNpcAbsoluteReturn(x, y, z, mode) : false;
}

bool NpcSetRimPoint(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a4824, NC_PROTOCOL 0xb9.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t pointType = 0;
    std::int32_t instantZoneSid = 0;
    const std::int32_t now = runtime::crt::Time32Now();
    const std::time_t hostNow = static_cast<std::time_t>(now);
    const std::tm* const local = std::localtime(&hostNow);
    const int weekDay = local->tm_wday;
    const int minuteOfDay = local->tm_hour * 60 + local->tm_min;

    ::Disassemble(packet, "ddd", &userSid, &pointType, &instantZoneSid);
    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        return false;
    }

    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneSid));
    CInstantZone* const instantZone = object != nullptr && object->IsInZone() ? object->CastInZone() : nullptr;
    if (instantZone != nullptr) {
        CInstantZoneConst* const instantZoneConst = instantZone->GetConst();
        const int resetTime = instantZoneConst->GetLatesetResetBindingDay(weekDay, minuteOfDay) * 60;
        db.SetRimPoint(*user, pointType, now, resetTime);
    }
    return false;
}

bool NpcRegisterDominionSiege(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a4b14, NC_PROTOCOL 0xbd.
    L2_GUARD_FUNCTION();
    std::int32_t dominionId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &dominionId, &userSid);

    if (g_ResidenceDB.GetDominion(dominionId) == nullptr) {
        return false;
    }
    if (FindUserBySid(userSid) == nullptr) {
        return false;
    }
    CDominionManager::Inst()->SendDominionRegistry(userSid, dominionId);
    return false;
}

bool NpcDeclareLord(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a4c5c, NC_PROTOCOL 0xbe.
    L2_GUARD_FUNCTION();
    std::int32_t requestId = 0;
    std::int32_t dominionId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "ddd", &requestId, &dominionId, &userSid);

    if (g_ResidenceDB.GetDominion(dominionId) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find dominion - %d : NpcRegisterDominionSiege",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "[Dominion] DeclareLord request %d for user SID %d rejected: dominion %d was not found.", requestId,
            userSid, dominionId
        );
        return false;
    }

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find user - %d : NpcRegisterDominionSiege",
        //     userSid)
        L2_LOG_ERROR(
            Log, "[Dominion] DeclareLord request %d for dominion %d rejected: user SID %d was not found.", requestId,
            dominionId, userSid
        );
        return false;
    }

    g_npcserver.Send("cdddd", 0x84, requestId, dominionId, userSid, user->DeclareLord());
    return false;
}

bool NpcGetRegistrySize(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a4dec, NC_PROTOCOL 0xbf.
    L2_GUARD_FUNCTION();
    std::int32_t requestId = 0;
    std::int32_t dominionId = 0;
    std::int32_t userSid = 0;
    std::int32_t registryType = 0;
    ::Disassemble(packet, "dddd", &requestId, &dominionId, &userSid, &registryType);

    CDominion* const dominion = g_ResidenceDB.GetDominion(dominionId);
    if (dominion == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find dominion - %d : NpcRegisterDominionSiege",
        //     dominionId)
        L2_LOG_ERROR(
            Log, "[Dominion] Registry-size request %d, type %d rejected: dominion %d was not found.", requestId,
            registryType, dominionId
        );
        return false;
    }

    if (FindUserBySid(userSid) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find user - %d : NpcRegisterDominionSiege",
        //     userSid)
        L2_LOG_ERROR(
            Log, "[Dominion] Registry-size request %d for dominion %d rejected: user SID %d was not found.", requestId,
            dominionId, userSid
        );
        return false;
    }

    std::int32_t registrySize = 0;
    if (dominion->m_pSiegeEntry != nullptr) {
        registrySize = static_cast<std::int32_t>(
            registryType != 0 ? dominion->m_pSiegeEntry->m_vPledgeList.size() : dominion->m_pSiegeEntry->m_vUserList.size()
        );
    }
    g_npcserver.Send("cddddd", 0x85, requestId, dominionId, userSid, registryType, registrySize);
    return false;
}

bool NpcSocket::NpcSetCalledPledgeDBId(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008af164, NC_PROTOCOL 0xee.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t pledgeDbId = 0;
    ::Disassemble(packet, "dd", &npcIndex, &pledgeDbId);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!creature || !creature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc",
        //     L"NpcSocket.cpp",
        //     0x2bec)
        // Noise: replace shipping coordinates with the rejected assignment.
        L2_LOG_ERROR(
            Log, "[NPC pledge] Cannot assign pledge DB ID %d: NPC index %d is missing or is not an NPC.", pledgeDbId,
            npcIndex
        );
        return false;
    }

    CNPC* const npc = creature->CastNPC();
    if (npc != nullptr && npc->IsBoss()) {
        CBoss* const boss = npc->CastBoss();
        if (boss != nullptr) {
            CPledge* const pledge = CPledge::FindPledge(g_nWorldId, pledgeDbId).get();
            if (pledge != nullptr) {
                boss->m_nPledgeID = pledgeDbId;
            }
        }
    }
    return false;
}

bool NpcSocket::NpcGetPledgeRaidRewardRate(NpcSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x008af3b0, NC_PROTOCOL 0xef.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    ::Disassemble(packet, "d", &npcIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!creature || !creature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid npc",
        //     L"NpcSocket.cpp",
        //     0x2c10)
        // Noise: replace shipping coordinates with the failed request.
        L2_LOG_ERROR(
            Log, "[NPC pledge] Cannot answer raid reward-rate request: NPC index %d is missing or is not an NPC.", npcIndex
        );
        return false;
    }

    CNPC* const npc = creature->CastNPC();
    if (npc == nullptr || !npc->IsPledgeBossNPC()) {
        return false;
    }
    CBoss* const boss = npc->CastBoss();
    if (boss == nullptr) {
        return false;
    }

    CPledge* const pledge = CPledge::FindPledge(g_nWorldId, boss->GetPledgeId()).get();
    if (pledge != nullptr) {
        const int neighborPledgeMembers = boss->GetNeighborPledgeMember();
        const int pledgeMembers = pledge->GetDbMemberCount();
        socket->Send("cddd", 0xae, static_cast<std::int32_t>(npc->Id()), neighborPledgeMembers, pledgeMembers);
    }
    return false;
}

bool NpcSocket::NpcInstantAgitPacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008af640, NC_PROTOCOL 0xf0.
    L2_GUARD_FUNCTION();
    char action = 0;
    std::int32_t requestSid = 0;
    std::int32_t userSidOrPledgeId = 0;
    std::int32_t value = 0;
    ::Disassemble(packet, "cddd", &action, &requestSid, &userSidOrPledgeId, &value);

    if (action > '\r') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"NpcSocket.cpp",
        //     0x2c34,
        //     static_cast<int>(action))
        // Noise: replace shipping coordinates with the rejected action and request context.
        L2_LOG_ERROR(
            Log,
            "[Instant clan hall] Request %d rejected: action %d exceeds 13 "
            "(user SID or pledge ID %d, value %d).",
            requestSid, static_cast<int>(action), userSidOrPledgeId, value
        );
        return false;
    }

    CInstantAgitManager& manager = CInstantAgitManager::inst();
    if (action == 1) {
        manager.TryAddInstantAgitDeco(requestSid, userSidOrPledgeId, value);
        return false;
    }
    if (action == 12) {
        manager.EnterAgitManager(requestSid);
        return false;
    }

    User* const user = FindUserBySid(userSidOrPledgeId);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid User",
        //     L"NpcSocket.cpp",
        //     0x2c47)
        // Noise: replace shipping coordinates with the missing request participant.
        L2_LOG_ERROR(
            Log, "[Instant clan hall] Action %d from request %d rejected: user SID %d was not found.",
            static_cast<int>(action), requestSid, userSidOrPledgeId
        );
        return false;
    }

    CSPointer<CCreature> npc = CCreatureFactory::Inst().FindObjectSP(requestSid);
    if (!npc || !npc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid NPC",
        //     L"NpcSocket.cpp",
        //     0x2c4e)
        // Noise: replace shipping coordinates with the missing request participant.
        L2_LOG_ERROR(
            Log,
            "[Instant clan hall] Action %d for user SID %d rejected: request NPC index %d "
            "is missing or is not an NPC.",
            static_cast<int>(action), userSidOrPledgeId, requestSid
        );
        return false;
    }

    manager.NpcPacketHandler(action, requestSid, *user, value);
    return false;
}

bool NpcSocket::NpcCallToChangeClass(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a64c0, NC_PROTOCOL 0xf1.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::uint32_t classId = 0;
    std::int32_t argument = 0;
    ::Disassemble(packet, "ddd", &userSid, &classId, &argument);

    if (classId >= 0xc0U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"user [%d] class id is invalid [%d]",
        //     userSid,
        //     static_cast<std::int32_t>(classId))
        L2_LOG_ERROR(
            Log, "[Class change] Request rejected for user SID %d: class ID %u exceeds 191.", userSid,
            static_cast<unsigned>(classId)
        );
        return false;
    }

    User* const user = FindUserBySid(userSid);
    if (user != nullptr && user->m_data->m_TrainingState != 1 && (!g_bDualBillingSystem || user->IsBuilder())) {
        user->GetSocket()->Send("chddd", 0xfe, 0xfe, static_cast<std::int32_t>(classId), argument, 1);
    }
    return false;
}

bool NpcSocket::ShowCommission(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008be308, NC_PROTOCOL 0xf2.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t talkNpcIndex = 0;
    ::Disassemble(packet, "dd", &userSid, &talkNpcIndex);

    CCreature* const npcObject = FindCreatureByIndex(talkNpcIndex);
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowCommission Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        L2_LOG_ERROR(
            Log, "[Commission] Cannot open the shop for user SID %d: NPC index %d did not resolve to an NPC.", userSid,
            talkNpcIndex
        );
        return false;
    }

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        return false;
    }
    if (!user->IsTalkableDistance(npc)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowCommission Error too distant")
        L2_LOG_ERROR(
            Log, "[Commission] Cannot open the shop for user SID %d: NPC index %d is outside talking distance.", userSid,
            talkNpcIndex
        );
        return false;
    }

    std::int32_t enabled = 0;
    if (CCommissionTradeManager::Inst().CheckCommissionJob(Commission::ShowNPC)) {
        TalkChannelCache::inst().mChannels[13].Cache(user->Id(), npc->Id(), 0);
        enabled = 1;
    }
    user->GetSocket()->Send("chd", 0xfe, 0xf2, enabled);
    return false;
}

bool NpcSocket::NpcChangeToAwakenedClass(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008be58c, NC_PROTOCOL 0xf3.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t awakenNpcId = 0;
    std::int32_t awakenClassId = 0;
    ::Disassemble(packet, "ddd", &userSid, &awakenNpcId, &awakenClassId);

    User* const user = FindUserBySid(userSid);
    if ((awakenClassId >= 0x8b && awakenClassId <= 0x92) || awakenClassId == 0xbe || awakenClassId == 0xbf) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Change Class!! (To Awakened Class [%d])",
        //     L"NpcSocket.cpp",
        //     0x2cba,
        //     awakenClassId)
        // Noise: replace shipping coordinates and emphatic punctuation with request context.
        L2_LOG_ERROR(
            Log,
            "[Class change] Request rejected for user SID %d from awaken NPC %d: "
            "target class %d is excluded by this handler.",
            userSid, awakenNpcId, awakenClassId
        );
        return false;
    }

    if (user != nullptr && user->CanCallToChangeClass()) {
        user->m_nAwakenClassID = awakenClassId;
        user->m_nAwakenNpcID = awakenNpcId;
        user->UpdateMentorByLevelUp(true, 0);
        TalkChannelCache::inst().mChannels[15].Cache(user->Id(), static_cast<std::uint32_t>(awakenNpcId), 0);
        user->GetSocket()->Send("chd", 0xfe, 0xff, awakenClassId);
    }
    return false;
}

bool NpcSocket::NpcBlockUpset(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a3e80, NC_PROTOCOL 0xc2.
    L2_GUARD_FUNCTION();
    std::int32_t action = 0;
    const unsigned char* const payload = ::Disassemble(packet, "d", &action);
    CBlockUpsetManager& manager = Singleton<CBlockUpsetManager>::inst();

    switch (action) {
        case 0: {
            std::int32_t npcSid = 0;
            std::int32_t stage = 0;
            ::Disassemble(payload, "dd", &npcSid, &stage);
            if (FindNpcBySid(npcSid) != nullptr) {
                manager.RegisterManagerNpcId(stage, npcSid);
            }
            break;
        }
        case 1: {
            std::int32_t unused0 = 0;
            std::int32_t unused1 = 0;
            std::int32_t unused2 = 0;
            std::int32_t userSid = 0;
            std::int32_t point = 0;
            ::Disassemble(payload, "ddddd", &unused0, &unused1, &unused2, &userSid, &point);
            User* const user = FindUserBySid(userSid);
            if (user != nullptr) {
                manager.AddBlockUpsetPoint(*user, point);
            }
            break;
        }
        case 2: {
            std::int32_t unused = 0;
            std::int32_t stage = 0;
            std::int32_t userSid = 0;
            ::Disassemble(payload, "ddd", &unused, &stage, &userSid);
            User* const user = FindUserBySid(userSid);
            if (user != nullptr) {
                manager.EnterBlockUpsetPlayer(stage, *user);
            }
            break;
        }
        case 3: {
            std::int32_t unused = 0;
            std::int32_t stage = 0;
            std::int32_t amount = 0;
            ::Disassemble(payload, "ddd", &unused, &stage, &amount);
            manager.ChangeBlockUpsetNpcAmount(stage, amount);
            break;
        }
        case 4: {
            std::int32_t npcSid = 0;
            std::int32_t stage = 0;
            ::Disassemble(payload, "dd", &npcSid, &stage);
            manager.AddBlockUpsetNpc(stage, npcSid);
            break;
        }
        case 5: {
            std::int32_t userSid = 0;
            std::int32_t stage = 0;
            std::int32_t color = 0;
            ::Disassemble(payload, "ddd", &userSid, &stage, &color);
            manager.ChangeBlockColor(stage, userSid, color);
            break;
        }
        default:
            break;
    }
    return false;
}

bool NpcSocket::NpcCleftUserEnter(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a41c4, NC_PROTOCOL 0xc5.
    L2_GUARD_FUNCTION();
    std::int32_t unused = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &unused, &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        Singleton<CCleft>::inst().EnterCleftPlayer(*user);
    }
    return false;
}

bool NpcSocket::NpcCleftCenterDestroyed(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a42c4, NC_PROTOCOL 0xc6.
    L2_GUARD_FUNCTION();
    std::int32_t unused = 0;
    std::int32_t towerType = 0;
    std::int32_t userSid = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "dddd", &unused, &towerType, &userSid, &point);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        Singleton<CCleft>::inst().TowerDestroyed(*user, towerType, point);
    }
    return false;
}

bool NpcDominionScriptEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089d458, NC_PROTOCOL 0xc9.
    L2_GUARD_FUNCTION();
    std::int32_t eventType = 0;
    std::int32_t firstArgument = 0;
    std::int32_t secondArgument = 0;
    ::Disassemble(packet, "ddd", &eventType, &firstArgument, &secondArgument);
    CDominionManager::Inst()->SendDominionScriptEvent(eventType, firstArgument, secondArgument);
    return false;
}

bool NpcShowPremiumItemList(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &npcSid, &userSid);

    CNPC* const npc = FindNpcBySid(npcSid);
    User* const user = FindUserBySid(userSid);
    if (npc != nullptr && user != nullptr) {
        TalkChannelCache::inst().mChannels[12].Cache(user->Id(), npc->Id(), 0);
        user->ShowPremiumItemList();
    }
    return false;
}

bool NPCUnequipWeapon(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        npc->UnequipWeapon(false);
    }
    return false;
}

bool NPCGiveEventItem(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a31ac.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t deleteItemClassId = 0;
    std::int64_t deleteItemAmount = 0;
    std::int32_t addItemClassId = 0;
    std::int64_t addItemAmount = 0;
    std::int32_t addItemEnchant = 0;
    std::int32_t timeGap = 0;
    std::int32_t eventType = 0;
    std::int32_t eventVariable = 0;
    ::Disassemble(
        packet, "ddQdQdddd", &userSid, &deleteItemClassId, &deleteItemAmount, &addItemClassId, &addItemAmount,
        &addItemEnchant, &timeGap, &eventType, &eventVariable
    );

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->GiveEventItem(
            deleteItemClassId, deleteItemAmount, addItemClassId, addItemAmount, addItemEnchant, timeGap, eventType,
            eventVariable
        );
    }
    return false;
}

bool NPCSetTrapWorldVisible(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t trapClassId = 0;
    std::int32_t visible = 1;
    ::Disassemble(packet, "ddd", &npcSid, &trapClassId, &visible);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCSetTrapWorldVisible. WorldTrap npcSID = %d, "
        //     L"ClassId = %d doesn't exist",
        //     npcSid,
        //     trapClassId)
        L2_LOG_ERROR(
            Log, "[World traps] Visibility request rejected: NPC SID %d was not found (trap class %d).", npcSid, trapClassId
        );
        return false;
    }

    CWorldTrap* const trap = g_WorldTrapDB.GetWorldTrap(static_cast<int>(npc->GetInZoneID()), trapClassId);
    if (trap == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCSetTrapWorldVisible. WorldTrap npcSID = %d, "
        //     L"ClassId = %d doesn't registered",
        //     npcSid,
        //     trapClassId)
        L2_LOG_ERROR(
            Log,
            "[World traps] Visibility request from NPC SID %d rejected: trap class %d is not registered in its instance.",
            npcSid, trapClassId
        );
    } else {
        trap->SetVisible(visible != 0);
    }
    return false;
}

bool NPCActivateWorldTrap(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t targetSid = 0;
    std::int32_t trapClassId = 0;
    std::int32_t activate = 1;
    ::Disassemble(packet, "dddd", &npcSid, &targetSid, &trapClassId, &activate);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCActivateWorldTrap. WorldTrap npcSID = %d, "
        //     L"ClassId = %d doesn't exist",
        //     npcSid,
        //     trapClassId)
        L2_LOG_ERROR(
            Log, "[World traps] Activation/defuse request rejected: NPC SID %d was not found (trap class %d).", npcSid,
            trapClassId
        );
        return false;
    }

    CCreature* const target = FindCreatureBySid(targetSid);
    CWorldTrap* const trap = g_WorldTrapDB.GetWorldTrap(static_cast<int>(npc->GetInZoneID()), trapClassId);
    if (trap == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCActivateWorldTrap. WorldTrap npcSID = %d, "
        //     L"ClassId = %d doesn't registered",
        //     npcSid,
        //     trapClassId)
        L2_LOG_ERROR(
            Log,
            "[World traps] Activation/defuse request from NPC SID %d rejected: trap class %d is not registered in its "
            "instance.",
            npcSid, trapClassId
        );
    } else if (activate == 1) {
        trap->Activate(target);
    } else {
        trap->Defuse(target);
    }
    return false;
}

bool NPCSetVisible(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t visible = 1;
    ::Disassemble(packet, "dd", &npcSid, &visible);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPCSetVisible. npcSID = %d doesn't exist",
        //     npcSid)
        L2_LOG_ERROR(Log, "[NPC visibility] Cannot apply visibility value %d: NPC SID %d was not found.", visible, npcSid);
    } else {
        npc->SetVisible(visible != 0);
    }
    return false;
}

bool NpcSaveGet5YearGiftTime(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    ::Disassemble(packet, "d", &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SaveGet5YearGiftTime();
    }
    return false;
}

bool NpcSaveGetbirthdayGiftTime(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    ::Disassemble(packet, "d", &userSid);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SaveGetBirthdayGiftTime();
    }
    return false;
}

bool NpcSetDailyQuestFlag(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    ::Disassemble(packet, "dd", &userSid, &questId);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SetDailyQuest(questId, true);
        user->SendCompletedDailyQuest(questId);
    }
    return false;
}

bool NpcSetWeeklyQuestFlag(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    ::Disassemble(packet, "dd", &userSid, &questId);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        user->SetDailyQuest(questId, true);
    }
    return false;
}

bool NpcSunRise(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a45ec, NC_PROTOCOL 0xfd.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::int32_t mode = 0;
    ::Disassemble(packet, "dd", &userSid, &mode);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        static_cast<void>(runtime::raw_atomic::ExchangeI32(&user->m_nSunRiseForce, 1));
        if (mode == 0) {
            user->GetSocket()->Send("c", 0x13);
        } else if (mode == 1) {
            user->GetSocket()->Send("c", 0x12);
        }
    }
    return false;
}

bool NpcChangeStatus(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t statusFlags = 0;
    ::Disassemble(packet, "dd", &npcIndex, &statusFlags);

    std::int32_t changedFlags = 0;
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (creature && creature->IsNPC()) {
        CNPC* const npc = creature->CastNPC();
        for (std::int32_t index = 0; index < 3; ++index) {
            const std::int32_t bit = 1 << index;
            const bool enabled = (statusFlags & bit) > 0;
            bool changed = false;
            if (index == 0) {
                changed = npc->SetTargetable(enabled, true);
            } else if (index == 1) {
                changed = npc->SetShowNametag(enabled);
            }

            if (changed) {
                changedFlags &= bit;
            }
        }
    }

    g_npcserver.Send("cdd", 0x82, npcIndex, changedFlags);
    return false;
}

bool NPCFieldCycle(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t action = 0;
    std::int32_t fieldCycleId = 0;
    std::int32_t value = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddd", &action, &fieldCycleId, &value);

    if (action == 0) {
        std::int32_t reason = 0;
        std::int32_t creatureSid = 0;
        ::Disassemble(cursor, "dd", &reason, &creatureSid);
        CCreature* const creature = FindCreatureBySid(creatureSid);
        g_FieldCycleSystem.AddPoint(fieldCycleId, value, static_cast<FieldCycleReason::Value>(reason), creature, nullptr);
    } else if (action == 1) {
        g_FieldCycleSystem.SetPoint(fieldCycleId, value, FieldCycleReason::ByNpcSetPoint, nullptr);
    } else if (action == 2) {
        std::int32_t reason = 0;
        std::int32_t creatureSid = 0;
        ::Disassemble(cursor, "dd", &reason, &creatureSid);
        CCreature* const creature = FindCreatureBySid(creatureSid);
        g_FieldCycleSystem.ChangeStep(fieldCycleId, value, static_cast<FieldCycleReason::Value>(reason), creature, nullptr);
    } else if (action == 3) {
        std::int32_t reason = 0;
        std::int32_t creatureSid = 0;
        ::Disassemble(cursor, "dd", &reason, &creatureSid);
        static_cast<void>(FindCreatureBySid(creatureSid));
        g_FieldCycleSystem.SetMissionResult(fieldCycleId, value != 0, static_cast<FieldCycleReason::Value>(reason));
    }
    return false;
}

bool NpcShout(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureSid;
    std::int32_t creatureIndex;
    char16_t message[1024];
    std::int32_t radius;
    ::Disassemble(packet, "ddSd", &creatureSid, &creatureIndex, 0x800, message, &radius);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(creatureIndex) == creatureSid) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureSid));
        if (creature != nullptr && creature->IsNPC()) {
            CNPC* const npc = creature->CastNPC();
            npc->Shout(message, radius);
        }
    }
    return false;
}

bool NpcSound(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a79c4, NC_PROTOCOL 0x30.
    L2_GUARD_FUNCTION();
    char16_t soundName[1000];
    std::int32_t userIndex;
    std::int32_t value1;
    std::int32_t value2;
    std::int32_t value3;
    std::int32_t value4;
    std::int32_t value5;
    std::int32_t mode;
    const unsigned char* const cursor =
        ::Disassemble(packet, "Sddddddd", 2000, soundName, &userIndex, &value1, &value2, &value3, &value4, &value5, &mode);

    CCreature* const creature = FindCreatureByIndex(userIndex);
    if (creature != nullptr && creature->IsPC()) {
        if (mode == 0) {
            User* const user = creature->CastUser();
            std::wstring hostSoundName;
            user->PlaySound(TargetUtf16ToHostWide(soundName, hostSoundName), value1, value2, value3, value4, value5);
        } else {
            std::int32_t value6;
            ::Disassemble(cursor, "d", &value6);
            if (mode == 2) {
                User* const user = creature->CastUser();
                std::wstring hostSoundName;
                user->PlayVoice(soundName, value1, value2, value3, value4, value5, value6, true);
            } else if (mode == 3) {
                User* const user = creature->CastUser();
                std::wstring hostSoundName;
                user->PlayVoice(soundName, value1, value2, value3, value4, value5, value6, false);
            }
        }
    }
    return false;
}

bool NpcCastleBanishOthers(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b5f8, NC_PROTOCOL 0x31.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    ::Disassemble(packet, "d", &residenceId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NpcCastleBanishOthers  invalid parameter[residence_id:%d]",
        //     residenceId)
        L2_LOG_ERROR(Log, "[Residence] Cannot expel non-clan members: residence %d was not found.", residenceId);
    } else {
        residence->ExpelNonPledgeMember();
    }
    return false;
}

bool NpcCastleHpRegen(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b6d8, NC_PROTOCOL 0x32.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    std::int32_t productId;
    ::Disassemble(packet, "dd", &residenceId, &productId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsWorldAgit()) {
        const std::int32_t duration =
            productId != 0 ? runtime::AddInt32Wrapping(runtime::crt::Time32Now(), INT32_C(0x93a80)) : 0;
        db.RequestInstallResidenceDeco(*residence, 0, productId, duration);
    }
    return false;
}

bool NpcCastleMpRegen(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b7d8, NC_PROTOCOL 0x33.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    std::int32_t productId;
    ::Disassemble(packet, "dd", &residenceId, &productId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsWorldAgit()) {
        const std::int32_t duration =
            productId != 0 ? runtime::AddInt32Wrapping(runtime::crt::Time32Now(), INT32_C(0x93a80)) : 0;
        db.RequestInstallResidenceDeco(*residence, 1, productId, duration);
    }
    return false;
}

bool NpcBroadcastSound(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        explicit TLMFunc(const unsigned char* packetValue) : packet(packetValue) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->PushSerializedMessage(new CSerializedSendVoiceEventMessage(user, packet));
            }
            return true;
        }

    private:
        const unsigned char* packet;
    };

    std::int32_t npcSid;
    std::int32_t distance;
    const unsigned char* const voicePacket = ::Disassemble(packet, "dd", &npcSid, &distance);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr && distance > 0) {
        const FVector position = npc->GetPos();
        CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(distance));
        TLMFunc function(voicePacket);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return false;
}

bool NPCPlayScene(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userIndex;
    std::int32_t sceneId;
    ::Disassemble(packet, "dd", &userIndex, &sceneId);

    CCreature* const creature = FindCreatureByIndex(userIndex);
    if (creature != nullptr && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->PlayScene(sceneId);
    }
    return false;
}

bool NpcFortressEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089c420.
    L2_GUARD_FUNCTION();
    std::int32_t residenceId = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &residenceId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsFortress()) {
        CFortress* const fortress = residence->CastFortress();
        if (fortress != nullptr) {
            fortress->RecieveNpcEvent(cursor);
        }
    }
    return false;
}

bool NpcSayPacket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t option;
    std::int32_t npcIndex;
    char16_t message[1024];
    ::Disassemble(packet, "ddS", &option, &npcIndex, 0x800, message);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (creature && creature->IsNPC()) {
        if (option == 1) {
            CNPC* const npc = creature->CastNPC();
            npc->Say(message);
        } else if (option == 2) {
            CNPC* const npc = creature->CastNPC();
            npc->Shout(message, 0x4000);
        }
    }
    return false;
}

bool NpcAttackPacket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t attackerIndex;
    std::int32_t targetId;
    std::int32_t attackMoveType;
    std::int32_t forceAttack;
    ::Disassemble(packet, "dddd", &attackerIndex, &targetId, &attackMoveType, &forceAttack);

    CSPointer<CCreature> attacker = CCreatureFactory::Inst().FindObjectSP(attackerIndex);
    if (attacker && attacker->IsNPC()) {
        CNPC* const npc = attacker->CastNPC();
        npc->OnNpcAttackPacket(targetId, static_cast<NpcAttackMoveType>(attackMoveType), forceAttack);
    }
    return false;
}

bool NpcUseSkill(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a7478, NC_PROTOCOL 0x1b.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t npcIndex;
    std::int32_t targetId;
    std::int64_t skillNameId;
    std::int32_t attackMoveType;
    std::int32_t forceAttack;
    std::int32_t followType;
    std::int32_t followData1;
    ::Disassemble(
        packet, "dddQdddd", &npcSid, &npcIndex, &targetId, &skillNameId, &attackMoveType, &forceAttack, &followType,
        &followData1
    );

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(npcIndex) != npcSid) {
        return false;
    }

    CSPointer<CCreature> creature;
    creature.SetObject(g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid)), ObjectRefType::ObjectIndex);
    if (creature && creature->IsNPC()) {
        CNPC* const npc = creature->CastNPC();
        npc->OnNpcUseSkillPacket(
            targetId, skillNameId, static_cast<NpcAttackMoveType>(attackMoveType), forceAttack, followType, followData1
        );
    }
    return false;
}

bool NpcUseItem(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a76e8, NC_PROTOCOL 0x1c.
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    std::int32_t userIndex;
    std::int32_t itemServerId;
    ::Disassemble(packet, "ddd", &userSid, &userIndex, &itemServerId);

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatures.GetIdByIndex(userIndex) != userSid) {
        return false;
    }

    CSPointer<CCreature> creature;
    creature.SetObject(g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid)), ObjectRefType::ObjectIndex);
    if (creature) {
        User* const user = static_cast<User*>(creature.get());
        CItem* const item = user->m_inv.GetByServerID(itemServerId);
        if (item != nullptr) {
            static_cast<void>(user->UseItem(*item, 0, false, 0));
        }
    }
    return false;
}

bool ShowNewTutorialPage(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aa4f4, NC_PROTOCOL 0xf7.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    char16_t html[256];
    ::Disassemble(packet, "ddS", &npcIndex, &userIndex, 0x1fe, html);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->GetSocket()->Send(
            "cdS", 0xa6,
            // 2,
            TUTORIAL_NEW, html
        );
        if (user->IsBuilder()) {
            user->GetSocket()->SendSystemMessage(u"New Tutorial HTML", html);
        }
    }
    return false;
}

bool ShowTutorialPage(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aa234, NC_PROTOCOL 0x35.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    char16_t fileName[256];
    ::Disassemble(packet, "ddS", &npcIndex, &userIndex, 0x1fe, fileName);

    static_cast<void>(npcIndex);
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        std::u16string html;
        if (g_HTMLCache.GetHTMLFile(fileName, html, user->m_data->m_CountryCode)) {
            user->GetSocket()->Send(
                "cdS", 0xa6,
                // 1,
                TUTORIAL_OLD, html.c_str()
            );
            if (user->IsBuilder()) {
                user->GetSocket()->SendSystemMessage(u"Tutorial HTML", fileName);
            }
        }
    }
    return false;
}

bool ShowTutorialMark(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aa784, NC_PROTOCOL 0x36.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t markType;
    std::int32_t markId;
    ::Disassemble(packet, "dddd", &npcIndex, &userIndex, &markType, &markId);

    static_cast<void>(npcIndex);
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        // if (static_cast<std::uint32_t>(markType) < 2U) {
        if (static_cast<std::uint32_t>(markType) < static_cast<std::uint32_t>(TutorialIcon::TIT_MAX)) {
            creature->CastUser()->GetSocket()->Send(
                "ccd", 0xa7, static_cast<std::int32_t>(static_cast<std::int8_t>(markType)), markId
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid question mark type[%d]",
            //     markType)
            L2_LOG_ERROR(
                Log, "[Tutorial] Mark %d was not sent to user index %d: type %d is outside the range 0..%d.", markId,
                userIndex, markType, static_cast<int>(TutorialIcon::TIT_MAX) - 1
            );
        }
    }
    return false;
}

bool CloseTutorialHTML(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aa9c0, NC_PROTOCOL 0x37.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    ::Disassemble(packet, "dd", &npcIndex, &userIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->GetSocket()->Send("c", 0xa9);
    }
    return false;
}

bool EnableTutorialEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aab8c, NC_PROTOCOL 0x38.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t eventId;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &eventId);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->m_TutorialEventSet = eventId;
        user->GetSocket()->Send("cd", 0xa8, eventId);
    }
    return false;
}

bool RadarControl(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008aad6c, NC_PROTOCOL 0x3b.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t action;
    std::int32_t radarType;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "ddddddd", &npcIndex, &userIndex, &action, &radarType, &x, &y, &z);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        User* const user = creature->CastUser();
        user->GetSocket()->Send("cddddd", 0xf1, action, radarType, x, y, z);
    }
    return false;
}

void LogNpcAtomicDelGiveItemResult(User* user, const int relatedQuestId, const int flag) {
    // Current L2Server 0x0089cebc, original NpcSocket.cpp helper.
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user is null. quest[%d]",
        //     L"NpcSocket.cpp",
        //     0x1420,
        //     relatedQuestId)
        // Noise: replace shipping coordinates with the missing audit record's context.
        L2_LOG_ERROR(
            Log, "[NPC items] Cannot record atomic item-exchange result for quest %d, flag %d: user is null.",
            relatedQuestId, flag
        );
        return;
    }

    const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    const int memoState = user->m_data->GetMemoState(relatedQuestId, 0);
    std::wstring accountNameStorage;
    const wchar_t* const accountName = TargetUtf16ToHostWide(user->m_sAccountName.data(), accountNameStorage);

    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%s,%s,,,",
    //     static_cast<std::int64_t>(0x135),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(user->m_data->m_Race),
    //     static_cast<std::int64_t>(user->m_data->m_Sex),
    //     static_cast<std::int64_t>(user->m_data->m_nClass),
    //     static_cast<std::int64_t>(user->m_data->m_nLevel),
    //     static_cast<std::int64_t>(relatedQuestId),
    //     static_cast<std::int64_t>(memoState),
    //     static_cast<std::int64_t>(flag),
    //     user->GetName(),
    //     accountName)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,,,",
        static_cast<std::int64_t>(0x135), static_cast<std::int64_t>(user->GetDBID()),
        static_cast<std::int64_t>(user->GetAccountID()), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
        static_cast<std::int64_t>(z), static_cast<std::int64_t>(user->m_data->m_Race),
        static_cast<std::int64_t>(user->m_data->m_Sex), static_cast<std::int64_t>(user->m_data->m_nClass),
        static_cast<std::int64_t>(user->m_data->m_nLevel), static_cast<std::int64_t>(relatedQuestId),
        static_cast<std::int64_t>(memoState), static_cast<std::int64_t>(flag), user->GetName(), accountName
    );
}

bool NpcAtomicDelGiveItemParam(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bb8f0, NC_PROTOCOL 0x3d.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::int32_t userIndex = 0;
    std::int32_t eventId = 0;
    std::int32_t addItemJobCheck = 0;
    std::int32_t etcManipulationCount = 0;
    std::int32_t itemManipulationCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "dddddd", &userSid, &userIndex, &eventId, &addItemJobCheck, &etcManipulationCount, &itemManipulationCount
    );

    if (CCreatureFactory::Inst().GetIdByIndex(userIndex) != userSid) {
        return false;
    }

    if (itemManipulationCount < 1 && etcManipulationCount < 1) {
        AtomicJobList* const atomicJob = AtomicJobList::CreateAtomicJobList(cursor, userSid, false);
        if (atomicJob != nullptr) {
            if (!atomicJob->CanDo()) {
                atomicJob->Dump();
            } else {
                static_cast<void>(atomicJob->Do());
            }
            atomicJob->Destroy();
        }
        return false;
    }

    if (itemManipulationCount >= 0x65) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Too many manipulation from NPC, count=[%d]",
        //     L"NpcSocket.cpp",
        //     0x1446,
        //     itemManipulationCount)
        // Noise: replace shipping coordinates with the request identity and actual limit.
        L2_LOG_ERROR(
            Log,
            "[NPC items] Atomic exchange rejected for user SID %d, event %d: "
            "%d item operations exceeds the limit of 100.",
            userSid, eventId, itemManipulationCount
        );
        return false;
    }

    User* const user = FindUserBySid(userSid);
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NpcAtomicDelGiveItem failed. invalid user serverID[%d] "
        //     L"nEventId[%d]",
        //     userSid,
        //     eventId)
        L2_LOG_ERROR(Log, "[NPC items] Atomic exchange rejected: user SID %d was not found (event %d).", userSid, eventId);
        return false;
    }

    CPacketHelperEx<20480> etcManipulationPacket;
    for (int index = 0; index < etcManipulationCount; ++index) {
        std::int16_t type = 0;
        cursor = ::Disassemble(cursor, "h", &type);
        EtcMani* const manipulation = Singleton<EtcManiFactory>::inst().CreateObject(static_cast<ETC_MANI_TYPE>(type));
        if (manipulation != nullptr) {
            cursor = manipulation->Disassemble(cursor);
            static_cast<void>(manipulation->Assemble(etcManipulationPacket));
            manipulation->Destroy();
        }
    }

    std::array<ItemManip, 100> itemManipulations{};
    for (int index = 0; index < itemManipulationCount; ++index) {
        ItemManip& manipulation = itemManipulations[index];
        cursor = manipulation.Disassemble(cursor);

        if (manipulation.mCount > 0 && manipulation.mEnchanted == 0) {
            CObject* const object = g_ObjectDB.GetObjectW(manipulation.mClassId, false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr) {
                manipulation.mEnchanted = static_cast<int>(item->m_const->m_nPrice);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"NpcSocket.cpp",
                //     0x1474,
                //     manipulation.mClassId)
                // Noise: replace shipping coordinates with the failed lookup and continued processing.
                L2_LOG_ERROR(
                    Log,
                    "[NPC items] Atomic exchange for user SID %d, event %d: "
                    "item class %d at operation index %d did not resolve to an item. "
                    "Processing continues without copying its price into the operation.",
                    userSid, eventId, manipulation.mClassId, index
                );
            }
        }

        if (g_dAiAdenaRate > 0.0 && manipulation.mRelatedQuestID > 0 && manipulation.mClassId == 0x39 &&
            manipulation.mCount > 0) {
            manipulation.mCount = static_cast<std::int64_t>(
                runtime::Win64CvttDoubleToInt32(static_cast<double>(manipulation.mCount) * g_dAiAdenaRate)
            );
        }
    }

    AtomicJobList* const atomicJob = AtomicJobList::CreateAtomicJobList(cursor, userSid, addItemJobCheck != 0);
    if (atomicJob != nullptr && !atomicJob->CanDo()) {
        // Current 0x008bb8f0 dumps but does not destroy this
        // rejected list on the non-queued branch.
        atomicJob->Dump();
        return false;
    }

    L2_GUARD(user->m_ManipulateQueueLock);
    CQuestSystem::LogQuestItem(*user, itemManipulationCount, itemManipulations.data());
    User::etcMsg message = user->SaveEtcMsg(false, false, eventId, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_GIVE_NPC_ITEM, eventId, itemManipulationCount, itemManipulations.data(), atomicJob,
        etcManipulationCount, etcManipulationPacket, false, ItemGetReason::Etc, message
    );
    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (!user->_HandleItemManipulation(queue)) {
            int auditFlag = 0;
            if (itemManipulationCount < 1 || eventId == 1 || eventId == 9) {
                user->SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_ITEM_INVENTORY_QUANTITY_LIMIT_EXCEEDED);
                auditFlag = 1;
            } else {
                user->SendSystemMessage(SystemMessageId::CANNOT_COMPLETE_QUEST_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
            }
            LogNpcAtomicDelGiveItemResult(user, itemManipulations[0].mRelatedQuestID, auditFlag);
            if (atomicJob != nullptr) {
                atomicJob->Dump();
                atomicJob->Destroy();
            }
            for (int index = 0; index < itemManipulationCount; ++index) {
                itemManipulations[index].Dump();
            }
        } else {
            CQuestSystem::LogQuestItem(*user, itemManipulationCount, itemManipulations.data());
        }
        if (user->GetTrade() != nullptr) {
            user->TradeCancel(false);
        }
    } else if (result == User::MRT_JOBMAP) {
        if (atomicJob != nullptr) {
            atomicJob->Dump();
            atomicJob->Destroy();
        }
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0x14db)
        // Noise: normal deferral while another item operation is pending. No failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"NpcSocket.cpp",
        //     0x14db)
    } else {
        if (atomicJob != nullptr) {
            atomicJob->Dump();
            atomicJob->Destroy();
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0x14e6)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"NpcSocket.cpp",
        //     0x14e6)
        L2_LOG_ERROR(
            Log, "[NPC items] Unexpected queue result %d for character %u; request handling stopped.",
            static_cast<int>(result), static_cast<unsigned int>(user->GetDBID())
        );
    }
    return false;
}

bool NpcNormalCamera(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userIndex;
    ::Disassemble(packet, "d", &userIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        creature->CastUser()->GetSocket()->Send("c", 0xd7);
    }
    return false;
}

bool NpcAgitAuction(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureIndex;
    std::int32_t mode;
    ::Disassemble(packet, "dd", &creatureIndex, &mode);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature || !creature->IsPC()) {
        return false;
    }

    if (mode == 0) {
        CAgitHTML html(creature->CastUser()->GetSocket());
        html.AgitMaid();
    } else if (mode == 1) {
        CAgitHTML html(creature->CastUser()->GetSocket());
        html.AgitDealer();
    }
    return false;
}

bool NpcOpenSiegeInfo(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089ff78, NC_PROTOCOL 0x43.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t residenceId;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &residenceId);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !userCreature || !npcCreature->IsNPC() || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "User null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0x166d)
        // Noise: replace shipping coordinates. Either participant can fail this guard.
        L2_LOG_ERROR(
            Log,
            "[Siege] Cannot open information for residence %d: NPC index %d or user index %d "
            "is missing or has the wrong creature type.",
            residenceId, npcIndex, userIndex
        );
        return false;
    }

    CNPC* const npc = npcCreature->CastNPC();
    User* const user = userCreature->CastUser();
    CUserSocket* const userSocket = user->GetSocket();
    if (userSocket == nullptr) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        return false;
    }

    g_ResidenceDB.SetNowTalkingMessenger(user, npc);
    CCastle* const castle = residence->CastCastle();
    CSiegeAgit* siegeAgit = nullptr;
    if (residence->IsWorldAgit()) {
        siegeAgit = residence->CastWorldAgit()->CastSiegeAgit();
    }

    if (castle != nullptr) {
        castle->SendCastleSiegeInfo(userSocket, user);
    } else if (siegeAgit != nullptr) {
        siegeAgit->SendCastleSiegeInfo(userSocket, user);
    }
    return false;
}

bool NpcSetDoorHpLevel(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089ba60, NC_PROTOCOL 0x44.
    L2_GUARD_FUNCTION();
    std::int32_t unused;
    char16_t doorName[128];
    std::int32_t hpPercent;
    std::int32_t inZoneId;
    ::Disassemble(packet, "dSdd", &unused, 0x100, doorName, &hpPercent, &inZoneId);

    std::wstring hostDoorName;
    CDoor* const door = g_DoorDB.GetDoor(doorName, static_cast<std::uint32_t>(inZoneId));
    if (door == nullptr) {
        return false;
    }

    CResidence* const residence = door->m_pOwningResidence;
    if (residence != nullptr && residence->IsCastle() && residence->IsUnderSiege()) {
        return false;
    }

    const double maxHp = static_cast<double>(hpPercent) * door->m_dBaseHp / 100.0;
    door->m_data->m_dMaxHP = maxHp;
    door->SetHP(maxHp);
    db.RequestSaveDoorStatus(door);
    return false;
}

bool NpcGetDoorHpLevel(NpcSocket* const socket, const unsigned char* packet) {
    // Current L2Server 0x0089bbc4, NC_PROTOCOL 0x45.
    L2_GUARD_FUNCTION();
    std::int32_t requestId;
    std::int32_t replyId;
    char16_t doorName[128];
    std::int32_t inZoneId;
    ::Disassemble(packet, "ddSd", &requestId, &replyId, 0x100, doorName, &inZoneId);

    std::wstring hostDoorName;
    CDoor* const door = g_DoorDB.GetDoor(doorName, static_cast<std::uint32_t>(inZoneId));
    if (door != nullptr) {
        socket->Send(
            "cdddd", 0x39, requestId, replyId,
            runtime::Win64CvttDoubleToInt32(door->m_data->m_dMaxHP / door->m_dBaseHp * 100.0)
        );
    }
    return false;
}

bool NpcSetControlTowerLevel(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bfce4, NC_PROTOCOL 0x46.
    L2_GUARD_FUNCTION();
    std::int32_t unused;
    char16_t towerName[128];
    std::int32_t controlLevel;
    ::Disassemble(packet, "dSd", &unused, 0x100, towerName, &controlLevel);

    CControlTower* const tower = CControlTowerDB::Inst().GetControlTower(towerName);
    if (tower != nullptr) {
        tower->m_nControlLevel = controlLevel;
        db.RequestSaveControlTowerStatus(tower);
    }
    return false;
}

bool NpcGetControlTowerLevel(NpcSocket* const socket, const unsigned char* packet) {
    // Current L2Server 0x008bfe10, NC_PROTOCOL 0x47.
    L2_GUARD_FUNCTION();
    std::int32_t requestId;
    std::int32_t replyId;
    char16_t towerName[128];
    ::Disassemble(packet, "ddS", &requestId, &replyId, 0x100, towerName);

    CControlTower* const tower = CControlTowerDB::Inst().GetControlTower(towerName);
    if (tower != nullptr) {
        socket->Send("cddd", 0x3a, requestId, replyId, tower->m_nControlLevel);
    }
    return false;
}

bool NpcAgitTeleportLevel(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089b8dc, NC_PROTOCOL 0x48.
    L2_GUARD_FUNCTION();
    std::int32_t residenceId;
    std::int32_t productId;
    ::Disassemble(packet, "dd", &residenceId, &productId);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsWorldAgit()) {
        const std::int32_t duration =
            productId != 0 ? runtime::AddInt32Wrapping(runtime::crt::Time32Now(), INT32_C(0x93a80)) : 0;
        db.RequestInstallResidenceDeco(*residence, 3, productId, duration);
    }
    return false;
}

bool NpcMusicEffect(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b297c, NC_PROTOCOL 0x49.
    L2_GUARD_FUNCTION();
    std::int32_t creatureIndex;
    std::int32_t radius;
    char16_t musicName[1024];
    ::Disassemble(packet, "ddS", &creatureIndex, &radius, 0x800, musicName);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature) {
        return false;
    }

    if (radius == 0) {
        CUserBroadcaster::inst().BroadcastToAllUserEx("cdSdddddd", 0x9e, 1, musicName, 0, 0, 0, 0, 0, 0);
    } else {
        g_ptlUserMap->BroadcastToNeighborRect(
            creature.get(), 0xf000, creature->GetPos(), radius, "cdSdddddd", 0x9e, 1, musicName, 0, 0, 0, 0, 0, 0
        );
    }
    return false;
}

bool NpcGiveContribution(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t contribution;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &contribution);

    CSPointer<CCreature> npc = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!npc || !npc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "npc null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0x1b7c)
        // Noise: replace shipping coordinates with the rejected contribution request.
        L2_LOG_ERROR(
            Log,
            "[NPC contribution] Cannot apply contribution %d from user index %d: "
            "NPC index %d is missing or is not an NPC.",
            contribution, userIndex, npcIndex
        );
        return false;
    }

    CSPointer<CCreature> user = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!user || !user->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "user null at file[%s], line[%d]",
        //     "NpcSocket.cpp",
        //     0x1b82)
        // Noise: replace shipping coordinates with the rejected contribution request.
        L2_LOG_ERROR(
            Log,
            "[NPC contribution] Cannot apply contribution %d to NPC index %d: "
            "user index %d is missing or is not a player.",
            contribution, npcIndex, userIndex
        );
        return false;
    }

    npc->AccumulateContribute(user.get(), static_cast<double>(contribution));
    return false;
}

bool NpcSpecialCamera(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureIndex;
    std::int32_t argument0;
    std::int32_t argument1;
    std::int32_t argument2;
    std::int32_t argument3;
    std::int32_t argument4;
    std::int32_t argument5;
    std::int32_t argument6;
    std::int32_t argument7;
    std::int32_t argument8;
    std::int32_t argument9;
    std::int32_t argument10;
    ::Disassemble(
        packet, "dddddddddddd", &creatureIndex, &argument0, &argument1, &argument2, &argument3, &argument4, &argument5,
        &argument6, &argument7, &argument8, &argument9, &argument10
    );

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (creature) {
        const FVector position = creature->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            creature.get(), 0xf000, position, argument4, "cddddddddddd", 0xd6, runtime::Int32FromBits(creature->m_nId),
            argument0, argument1, argument2, argument3, argument5, argument6, argument7, argument8, argument9, argument10
        );
    }
    return false;
}

bool NpcSpecialCamera3(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFilter : public CTLMFilterDist {
    public:
        TLMFilter(int inZoneId, const FVector& position, double distance) : CTLMFilterDist(inZoneId, position, distance) {}

        bool CanDo(CObject* object) override { return CTLMFilterDist::CanDo(object) && object->IsNPC(); }
    };

    class TLMFunc : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<CSafePointer2<CCreature>>* creatures) : v(creatures) {}

        bool CanDo(CObject* object, double) override {
            v->push_back(CSafePointer2<CCreature>(object->CastCreature()));
            return true;
        }

    private:
        std::vector<CSafePointer2<CCreature>>* v;
    };

    std::int32_t creatureIndex;
    std::int32_t argument0;
    std::int32_t argument1;
    std::int32_t argument2;
    std::int32_t argument3;
    std::int32_t argument4;
    std::int32_t argument5;
    std::int32_t argument6;
    std::int32_t argument7;
    std::int32_t argument8;
    std::int32_t argument9;
    std::int32_t argument10;
    ::Disassemble(
        packet, "dddddddddddd", &creatureIndex, &argument0, &argument1, &argument2, &argument3, &argument4, &argument5,
        &argument6, &argument7, &argument8, &argument9, &argument10
    );

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature) {
        return false;
    }

    const FVector position = creature->GetPos();
    std::vector<CSafePointer2<CCreature>> creatures;
    if (argument4 > 20000) {
        argument4 = 0x1000;
    }

    const int inZoneId = static_cast<int>(creature->GetInZoneID());
    TLMFilter npcFilter(inZoneId, position, static_cast<double>(argument4));
    TLMFunc collectNpcs(&creatures);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&npcFilter, &collectNpcs);

    for (int index = 0; index < static_cast<int>(creatures.size()); ++index) {
        CSPointer<CCreature> neighbor = runtime::CheckedVectorAt(creatures, index).FindObjectSP();
        if (neighbor && neighbor->IsNPC()) {
            neighbor->CastNPC()->SendNPCInfoForSpecialCamera3(nullptr, position, argument4);
        }
    }

    {
        class TLMFunc : public CTLMFunc {
        public:
            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    object->CastUser()->GetController()->DoNothing(true);
                }
                return true;
            }
        };

        CTLMFilterDist userFilter(inZoneId, position, static_cast<double>(argument4));
        TLMFunc resetUserActions;
        FOR_ALL_NEIGHBOR_USER_DIST(&userFilter, &resetUserActions);
    }

    g_ptlUserMap->BroadcastToNeighborDist(
        creature.get(), 0xf000, position, argument4, "cddddddddddd", 0xd6, runtime::Int32FromBits(creature->m_nId),
        argument0, argument1, argument2, argument3, argument5, argument6, argument7, argument8, argument9, argument10
    );
    return false;
}

bool NpcSpecialCameraZLimit(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    class TLMFunc : public CTLMFunc {
    public:
        explicit TLMFunc(std::vector<CSafePointer2<CCreature>>* creatures) : v(creatures) {}

        bool CanDo(CObject* object, double) override {
            v->push_back(CSafePointer2<CCreature>(object->CastCreature()));
            return true;
        }

    private:
        std::vector<CSafePointer2<CCreature>>* v;
    };

    std::int32_t creatureIndex;
    std::int32_t argument0;
    std::int32_t argument1;
    std::int32_t argument2;
    std::int32_t argument3;
    std::int32_t argument4;
    std::int32_t argument5;
    std::int32_t argument6;
    std::int32_t argument7;
    std::int32_t argument8;
    std::int32_t argument9;
    std::int32_t argument10;
    std::int32_t minZ;
    std::int32_t maxZ;
    ::Disassemble(
        packet, "dddddddddddddd", &creatureIndex, &argument0, &argument1, &argument2, &argument3, &argument4, &argument5,
        &argument6, &argument7, &argument8, &argument9, &argument10, &minZ, &maxZ
    );

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (!creature) {
        return false;
    }

    const FVector position = creature->GetPos();
    std::vector<CSafePointer2<CCreature>> creatures;
    if (argument4 > 20000) {
        argument4 = 0x1000;
    }

    const int inZoneId = static_cast<int>(creature->GetInZoneID());
    CTLMFilterDistBetweenZ npcFilter(inZoneId, position, static_cast<double>(argument4), minZ, maxZ);
    TLMFunc collectNpcs(&creatures);
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&npcFilter, &collectNpcs);

    for (int index = 0; index < static_cast<int>(creatures.size()); ++index) {
        CSPointer<CCreature> neighbor = runtime::CheckedVectorAt(creatures, index).FindObjectSP();
        if (neighbor && neighbor->IsNPC()) {
            neighbor->CastNPC()->SendNPCInfoForSpecialCamera3(nullptr, position, argument4);
        }
    }

    {
        class TLMFunc : public CTLMFunc {
        public:
            bool CanDo(CObject* object, double) override {
                if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                    object->CastUser()->GetController()->DoNothing(true);
                }
                return true;
            }
        };

        CTLMFilterDistBetweenZ userFilter(inZoneId, position, static_cast<double>(argument4), minZ, maxZ);
        TLMFunc resetUserActions;
        FOR_ALL_NEIGHBOR_USER_DIST(&userFilter, &resetUserActions);
    }

    g_ptlUserMap->BroadcastToNeighborDistBetweenZ(
        *creature.get(), 0xf000, position, argument4, minZ, maxZ, "cddddddddddd", 0xd6,
        runtime::Int32FromBits(creature->m_nId), argument0, argument1, argument2, argument3, argument5, argument6,
        argument7, argument8, argument9, argument10
    );
    return false;
}

bool NpcSetEnchantOfWeapon(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t enchant;
    ::Disassemble(packet, "dd", &npcSid, &enchant);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        npc->SetEnchantOfWeapon(enchant);
    }
    return false;
}

bool NpcUpdatePCCafePoint(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a12a8, NC_PROTOCOL 0x7d.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "dd", &userSid, &point);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr) {
        DoJobWithPoint operation = DJWP_ADD;
        if (point < 0) {
            operation = DJWP_DECREASE;
        }
        user->UpdatePoint(point, PT_PCCAFE_POINT, operation, 0);
    }
    return false;
}

bool NpcGiveItemByPCCafePoint(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a13b0, NC_PROTOCOL 0x7e.
    L2_GUARD_FUNCTION();

    std::int32_t userSid = 0;
    std::int32_t point = 0;
    std::int32_t itemClassId = 0;
    std::int32_t enchanted = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "ddddQ", &userSid, &point, &itemClassId, &enchanted, &amount);

    User* const user = FindUserBySid(userSid);
    if (user != nullptr && point >= 0) {
        user->GetItemByPCCafePoint(point, itemClassId, enchanted, amount);
    }
    return false;
}

bool NpcInstantTeleportMPCC(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089e860, NC_PROTOCOL 0x7f.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t range = 0;
    std::int32_t minimumDistance = 0;
    std::int32_t maximumDistance = 0;
    std::int32_t itemClassId = 0;
    std::int64_t itemCount = 0;
    std::int32_t questId = 0;
    ::Disassemble(
        packet, "dddddddddQd", &npcSid, &userSid, &x, &y, &z, &range, &minimumDistance, &maximumDistance, &itemClassId,
        &itemCount, &questId
    );

    CCreature* const npc = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]NpcInstantTeleportMPCC cannot find npc, [%d]",
        //     "NpcSocket.cpp",
        //     0xeee,
        //     npcSid)
        // Noise: replace shipping coordinates with both participants and the rejection code.
        L2_LOG_ERROR(
            Log,
            "[teleport] Command-channel teleport rejected for user SID %d: NPC SID %d was not found. "
            "The reply will use result -1.",
            userSid, npcSid
        );
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -1);
        return false;
    }
    if (creature == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]NpcInstantTeleportMPCC cannot find user, [%d]",
        //     "NpcSocket.cpp",
        //     0xef5,
        //     userSid)
        // Noise: replace shipping coordinates with both participants and the rejection code.
        L2_LOG_ERROR(
            Log,
            "[teleport] Command-channel teleport from NPC SID %d rejected: user SID %d was not found. "
            "The reply will use result -2.",
            npcSid, userSid
        );
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -2);
        return false;
    }

    User* const user = static_cast<User*>(creature);
    CParty* const party = user->GetParty();
    if (party == nullptr) {
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -3);
        return false;
    }

    const std::uint32_t inZoneId = user->GetInZoneID();
    CMultiPartyCommandChannel* const channel = party->GetMPCC();
    if (channel == nullptr) {
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -4);
        return false;
    }

    CParty* const masterParty = channel->GetMasterParty();
    if (masterParty == nullptr) {
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -5);
        return false;
    }
    if (static_cast<std::uint32_t>(userSid) != masterParty->m_data->m_OnlineMaster.id) {
        g_npcserver.Send("cddd", 0x5a, npcSid, userSid, -6);
        return false;
    }

    const int result = channel->TeleportMPCCToLocation(
        x, y, z, range, minimumDistance, maximumDistance, itemClassId, itemCount, questId, static_cast<int>(inZoneId)
    );
    g_npcserver.Send("cddd", 0x5a, npcSid, userSid, result);
    return false;
}

bool NpcSetMPCCMasterPartyRouting(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x0089ebc8, NC_PROTOCOL 0x80.
    L2_GUARD_FUNCTION();

    std::int32_t npcSid;
    std::int32_t mpccSid;
    std::int32_t action;
    ::Disassemble(packet, "ddd", &npcSid, &mpccSid, &action);

    if (npcSid != 0 && mpccSid != 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(mpccSid));
        if (creature != nullptr && creature->IsNPC() && object != nullptr && object->IsMPCC()) {
            CNPC* const npc = creature->CastNPC();
            if (npc != nullptr) {
                CMultiPartyCommandChannel* const channel = static_cast<CMultiPartyCommandChannel*>(object);
                if (action == 1) {
                    channel->SetMasterPartyRouting(npc);
                } else if (action == 0 && npc->m_pContribute->GetMPCCRouting() == static_cast<std::uint32_t>(mpccSid)) {
                    channel->ResetMasterPartyRouting();
                }
            }
        }
    }
    return false;
}

bool NpcMiniGameAgitFunc(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008a7c1c, NC_PROTOCOL 0x84.
    L2_GUARD_FUNCTION();

    std::uint8_t function = 0;
    std::int32_t agitId = 0;
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "cdddd", &function, &agitId, &npcIndex, &userIndex, &point);

    CMiniGameAgitNpcFuncHandler handler(function, agitId, npcIndex, userIndex, point);
    if (handler.PreCheck()) {
        handler.Execute();
    }
    return false;
}

bool NpcMakerFinished(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bff68, NC_PROTOCOL 0x82.
    L2_GUARD_FUNCTION();

    std::int32_t ignored = 0;
    ::Disassemble(packet, "dd", &ignored, &g_EnterWorldSerializer.m_InitSpawnCount);
    g_PledgeTimer.SetAllMakerFinished(true);
    NpcPosEventMap::inst().MakerFinished();
    DoorEventMap::inst().MakerFinished();
    CInstantAgitManager::inst().MakerFinished();
    // Log.Add(LOG_NORMAL, L"[NpcSpawnInfo] Spawn Complete")
    L2_LOG_INFO(Log, "[NPC startup] Received maker-finished notification from L2NPC; dependent managers were notified.");
    return false;
}

bool NpcRegMakerEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bfbf8, NC_PROTOCOL 0x83.
    L2_GUARD_FUNCTION();

    std::int32_t eventType = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "d", &eventType);
    if (eventType == 0) {
        std::array<char16_t, 0x200> doorName{};
        ::Disassemble(cursor, "S", 0x400, doorName.data());
        DoorEventMap::inst().RegisterDoorEvent(doorName.data());
    }
    return false;
}

bool NpcRegisterAIStatus(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bf9e8, NC_PROTOCOL 0xeb.
    L2_GUARD_FUNCTION();

    std::int32_t action = 0;
    std::array<char16_t, 20480> name{};
    std::int32_t worldId = 0;
    ::Disassemble(packet, "dSd", &action, 0xa000, name.data(), &worldId);

    switch (action) {
        case 0:
            AreaStatusMap::inst().RegisterAreaStatus(name.data(), worldId);
            break;
        case 1:
            DoorStatusMap::inst().RegisterDoorStatus(name.data(), worldId);
            break;
        case 2:
            AreaStatusMap::inst().UnRegisterAreaStatus(name.data(), worldId);
            break;
        case 3:
            DoorStatusMap::inst().UnRegisterDoorStatus(name.data(), worldId);
            break;
        default:
            break;
    }
    return false;
}

bool NpcEarthquake(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureIndex;
    std::int32_t argument0;
    std::int32_t argument1;
    std::int32_t minXIndex;
    std::int32_t maxXIndex;
    std::int32_t minYIndex;
    std::int32_t maxYIndex;
    ::Disassemble(packet, "ddddddd", &creatureIndex, &argument0, &argument1, &minXIndex, &maxXIndex, &minYIndex, &maxYIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (creature) {
        const FVector position = creature->GetPos();
        const int maximumIndex = std::max(std::max(minXIndex, maxXIndex), std::max(minYIndex, maxYIndex));
        g_ptlUserMap->BroadcastToNeighborIndex(
            creature.get(), 0xf000, position, minXIndex, maxXIndex, minYIndex, maxYIndex, "cdddddd", 0xd3,
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), argument0, argument1, maximumIndex
        );
    }
    return false;
}

bool NpcJoinTimeAttack(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b2b98, NC_PROTOCOL 0x6b.
    L2_GUARD_FUNCTION();

    std::int32_t roomNumber = 0;
    std::int32_t partType = 0;
    std::int32_t partySid = 0;
    ::Disassemble(packet, "ddd", &roomNumber, &partType, &partySid);

    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partySid));
    if (party == nullptr) {
        return false;
    }

    {
        std::vector<std::uint32_t> memberIds;
        party->GetAllMemberId(memberIds);
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        const int z = runtime::Win64CvttDoubleToInt32(member->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(member->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(member->GetPos().x);
        std::wstring accountNameStorage;
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        //     L"%s,%s,,,",
        //     static_cast<std::int64_t>(0x137),
        //     static_cast<std::int64_t>(member->GetDBID()),
        //     static_cast<std::int64_t>(member->GetAccountID()),
        //     static_cast<std::int64_t>(x),
        //     static_cast<std::int64_t>(y),
        //     static_cast<std::int64_t>(z),
        //     static_cast<std::int64_t>(member->m_data->m_Race),
        //     static_cast<std::int64_t>(member->m_data->m_Sex),
        //     static_cast<std::int64_t>(member->m_data->m_nClass),
        //     static_cast<std::int64_t>(member->m_data->m_nLevel),
        //     static_cast<std::int64_t>(partType),
        //     static_cast<std::int64_t>(partySid),
        //     static_cast<std::int64_t>(roomNumber),
        //     static_cast<std::int64_t>(members.size()),
        //     member->GetName(),
        //     TargetUtf16ToHostWide(
        //         member->m_sAccountName.data(),
        //         accountNameStorage))
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
            u"%s,%s,,,",
            static_cast<std::int64_t>(0x137), static_cast<std::int64_t>(member->GetDBID()),
            static_cast<std::int64_t>(member->GetAccountID()), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
            static_cast<std::int64_t>(z), static_cast<std::int64_t>(member->m_data->m_Race),
            static_cast<std::int64_t>(member->m_data->m_Sex), static_cast<std::int64_t>(member->m_data->m_nClass),
            static_cast<std::int64_t>(member->m_data->m_nLevel), static_cast<std::int64_t>(partType),
            static_cast<std::int64_t>(partySid), static_cast<std::int64_t>(roomNumber),
            static_cast<std::int64_t>(members.size()), member->GetName(),
            TargetUtf16ToHostWide(member->m_sAccountName.data(), accountNameStorage)
        );
    }
    return false;
}

bool NpcLeaveTimeAttack(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b2e60, NC_PROTOCOL 0x6c.
    L2_GUARD_FUNCTION();

    std::int32_t roomNumber = 0;
    std::int32_t partType = 0;
    std::int32_t partySid = 0;
    std::int32_t parameter = 0;
    ::Disassemble(packet, "dddd", &roomNumber, &partType, &partySid, &parameter);

    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partySid));
    if (party == nullptr) {
        return false;
    }

    {
        std::vector<std::uint32_t> memberIds;
        party->GetAllMemberId(memberIds);
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        if (member != nullptr) {
            member->SetPrivateID(0);
        }
        const int z = runtime::Win64CvttDoubleToInt32(member->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(member->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(member->GetPos().x);
        std::wstring accountNameStorage;
        // Log.Add(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
        //     L"%I64d,%s,%s,,,",
        //     static_cast<std::int64_t>(0x138),
        //     static_cast<std::int64_t>(member->GetDBID()),
        //     static_cast<std::int64_t>(member->GetAccountID()),
        //     static_cast<std::int64_t>(x),
        //     static_cast<std::int64_t>(y),
        //     static_cast<std::int64_t>(z),
        //     static_cast<std::int64_t>(parameter),
        //     static_cast<std::int64_t>(member->m_data->m_Race),
        //     static_cast<std::int64_t>(member->m_data->m_Sex),
        //     static_cast<std::int64_t>(member->m_data->m_nClass),
        //     static_cast<std::int64_t>(member->m_data->m_nLevel),
        //     static_cast<std::int64_t>(partType),
        //     static_cast<std::int64_t>(partySid),
        //     static_cast<std::int64_t>(roomNumber),
        //     static_cast<std::int64_t>(members.size()),
        //     std::int64_t{0},
        //     member->GetName(),
        //     TargetUtf16ToHostWide(
        //         member->m_sAccountName.data(),
        //         accountNameStorage))
        Log.Add(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,"
            u"%I64d,%s,%s,,,",
            static_cast<std::int64_t>(0x138), static_cast<std::int64_t>(member->GetDBID()),
            static_cast<std::int64_t>(member->GetAccountID()), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
            static_cast<std::int64_t>(z), static_cast<std::int64_t>(parameter),
            static_cast<std::int64_t>(member->m_data->m_Race), static_cast<std::int64_t>(member->m_data->m_Sex),
            static_cast<std::int64_t>(member->m_data->m_nClass), static_cast<std::int64_t>(member->m_data->m_nLevel),
            static_cast<std::int64_t>(partType), static_cast<std::int64_t>(partySid), static_cast<std::int64_t>(roomNumber),
            static_cast<std::int64_t>(members.size()), std::int64_t{0}, member->GetName(),
            TargetUtf16ToHostWide(member->m_sAccountName.data(), accountNameStorage)
        );
    }
    return false;
}

bool NpcBroadcastToParty(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t partySid = 0;
    std::int32_t mode = 0;
    std::int32_t packetLength = 0;
    const unsigned char* const embeddedPacket = ::Disassemble(packet, "ddd", &partySid, &mode, &packetLength);

    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partySid));
    if (party == nullptr) {
        return false;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    if (mode == 0) {
        for (User* const member : members) {
            member->GetSocket()->Send("b", packetLength, embeddedPacket);
        }
    } else if (mode == 1) {
        User* const master = party->GetMaster();
        if (master != nullptr) {
            master->GetSocket()->Send("b", packetLength, embeddedPacket);
        }
    } else if (mode == 2) {
        User* const master = party->GetMaster();
        if (master != nullptr) {
            for (User* const member : members) {
                if (member->m_nId != master->m_nId) {
                    member->GetSocket()->Send("b", packetLength, embeddedPacket);
                }
            }
        }
    }
    return false;
}

bool NPCPlayAnimation(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t animationId;
    std::int32_t distance;
    ::Disassemble(packet, "ddd", &npcSid, &animationId, &distance);

    CNPC* const npc = FindNpcBySid(npcSid);
    if (npc != nullptr) {
        const FVector position = npc->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            npc, 0x5000, position, distance, "chdcdS", 0xfe, 0x5b, npcSid, 0, animationId, u""
        );
    }
    return false;
}

bool NPCFieldCyclePointLock(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool NpcLottoBuyTicket(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t creatureIndex;
    std::int32_t unused1;
    std::int32_t unused2;
    std::int32_t unused3;
    ::Disassemble(packet, "dddd", &creatureIndex, &unused1, &unused2, &unused3);

    static_cast<void>(unused1);
    static_cast<void>(unused2);
    static_cast<void>(unused3);
    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (creature) {
        static_cast<void>(creature->IsPC());
    }
    return false;
}

bool NpcRegisterTeamBattle(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008b03a4, NC_PROTOCOL 0x55.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t target;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &target);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [query team battle]")
        L2_LOG_ERROR(
            Log,
            "[Team battle] Registration rejected: NPC index %d or user index %d "
            "is missing or has the wrong creature type (target %d).",
            npcIndex, userIndex, target
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npcCreature->m_data->m_nResidenceID);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CTeamBattleAgit* const teamBattleAgit = worldAgit != nullptr ? worldAgit->CastTeamBattleAgit() : nullptr;
    if (teamBattleAgit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid agit[%d] in [query team battle]",
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Team battle] Registration rejected: NPC residence %d did not resolve to a team-battle clan hall.",
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }

    if (target == 1) {
        CPledge* const pledge = userCreature->CastUser()->GetPledge();
        bool alreadyRegistered = false;
        TEAM_BATTLE_COLOR color = TBC_NOT_DETERMINED;
        if (pledge != nullptr) {
            color = teamBattleAgit->GetColor(static_cast<int>(pledge->GetDbId()), alreadyRegistered);
        }
        if (alreadyRegistered || color == TBC_NOT_DETERMINED) {
            g_npcserver.Send("cdddd", 0x3f, npcIndex, userIndex, target, static_cast<int>(color));
        } else {
            db.RequestNewTeamBattleAgitPledge(
                teamBattleAgit, pledge, userCreature->CastUser(), npcCreature->CastNPC(), static_cast<int>(color), 0
            );
        }
    } else if (target == 2) {
        CPledge* const pledge = userCreature->CastUser()->GetPledge();
        int reply = 0;
        if (pledge != nullptr) {
            const int result = teamBattleAgit->TestRegisterTeamBattleEntryMember(pledge->GetDbId(), userCreature->GetDBID());
            if (result == 0) {
                db.RequestNewTeamBattleAgitMember(teamBattleAgit, userCreature->CastUser(), npcCreature->CastNPC());
                return false;
            }
            if (result == -6) {
                reply = 2;
            } else if (result == -2) {
                reply = 1;
            }
        }
        g_npcserver.Send("cdddd", 0x3f, npcIndex, userIndex, target, reply);
    } else {
        // Current 0x008b098f supplies no value for the %d.
        // Log.Add(
        //     LOG_ERROR,
        //     "invalid target[%d] in [query team battle]")
        // Log.Add(LOG_ERROR,
        //     "[NPC team battle] Query rejected: invalid target %d.", target)
        // L2_LOG_ERROR(Log,
        //     "[NPC team battle] Query rejected: invalid target %d.", target)
        // Previous: L2_LOG_ERROR(Log,
        //     "Team battle registration rejected: unsupported target %d "
        //     "(expected 1 for clan or 2 for member). NPC index %d, player "
        //     "index %d.", target, npcIndex, userIndex)
        L2_LOG_ERROR(
            Log,
            "[Team battle] Registration rejected: unsupported target %d "
            "(expected 1 for clan or 2 for member); NPC index %d, player "
            "index %d.",
            target, npcIndex, userIndex
        );
    }
    return false;
}

bool NpcQueryTeamBattleNpc(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ab538, NC_PROTOCOL 0x56.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t operation;
    std::int32_t npcType;
    ::Disassemble(packet, "dddd", &npcIndex, &userIndex, &operation, &npcType);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [query team battle]")
        L2_LOG_ERROR(
            Log,
            "[Team battle] NPC query operation %d, NPC type %d rejected: NPC index %d or user index %d "
            "is missing or has the wrong creature type.",
            operation, npcType, npcIndex, userIndex
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npcCreature->m_data->m_nResidenceID);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CTeamBattleAgit* const teamBattleAgit = worldAgit != nullptr ? worldAgit->CastTeamBattleAgit() : nullptr;
    if (teamBattleAgit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid agit[%d] in [query team battle]",
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Team battle] NPC query rejected: residence %d did not resolve to a team-battle clan hall.",
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }

    if (operation == 1) {
        CPledge* const pledge = userCreature->CastUser()->GetPledge();
        int result = 0;
        if (pledge != nullptr) {
            result = teamBattleAgit->GetNpcType(pledge->GetDbId());
        }
        g_npcserver.Send("cdddd", 0x40, npcIndex, userIndex, operation, result);
    } else if (operation == 2) {
        CPledge* const pledge = userCreature->CastUser()->GetPledge();
        int result = 0;
        if (pledge != nullptr) {
            result = teamBattleAgit->SetNpcType(pledge->GetDbId(), npcType);
            if (result > 0) {
                db.RequestSetTeamBattleNpcType(*teamBattleAgit, pledge->GetDbId(), result);
            }
        }
        g_npcserver.Send("cdddd", 0x40, npcIndex, userIndex, operation, result);
    }
    return false;
}

bool NpcShowEnchantSkillList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bd01c, NC_PROTOCOL 0x70.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userSid = 0;
    std::int32_t userIndex = 0;
    SkillEnchantCommandType command = SECT_NORMAL;
    ::Disassemble(packet, "dddd", &npcIndex, &userSid, &userIndex, &command);

    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(command);
    User* user = nullptr;
    CNPC* npc = nullptr;
    if (enchant != nullptr && enchant->CheckNpcShowEnchantSkillList(npcIndex, userSid, userIndex, user, npc)) {
        TalkChannelCache::inst().mChannels[7].Cache(user->Id(), npc->Id(), 0);
        enchant->SendAcquiredSkillList(user);
    }
    return false;
}

bool NPCShowEnchantSkillListDrawer(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bde60, NC_PROTOCOL 0xa0.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex = 0;
    std::int32_t userSid = 0;
    std::int32_t userIndex = 0;
    std::int64_t skillNameId = 0;
    SkillEnchantCommandType command = SECT_NORMAL;
    ::Disassemble(packet, "dddQd", &npcIndex, &userSid, &userIndex, &skillNameId, &command);

    SkillEnchantOperator* const enchant = Singleton<SkillEnchantFactory>::inst().CreateObject(command);
    User* user = nullptr;
    CNPC* npc = nullptr;
    if (enchant != nullptr && enchant->CheckNpcShowEnchantSkillList(npcIndex, userSid, userIndex, user, npc)) {
        TalkChannelCache::inst().mChannels[7].Cache(user->Id(), npc->Id(), 0);
        enchant->SendAcquirableSkillList(user, skillNameId);
    }
    return false;
}

bool NpcHennaItemListOpenForEquip(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bd16c, NC_PROTOCOL 0x57.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    ::Disassemble(packet, "dd", &npcIndex, &userIndex);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!npcCreature || !npcCreature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0x18a5,
        //     npcIndex)
        // Noise: replace shipping coordinates with the requested list and invalid participant.
        L2_LOG_ERROR(
            Log, "[Henna] Cannot show equip list for user index %d: NPC index %d is missing or is not an NPC.", userIndex,
            npcIndex
        );
        return false;
    }

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0x18ac,
        //     userIndex)
        // Noise: replace shipping coordinates with the requested list and invalid participant.
        L2_LOG_ERROR(
            Log, "[Henna] Cannot show equip list from NPC index %d: user index %d is missing or is not a player.", npcIndex,
            userIndex
        );
        return false;
    }

    CNPC* const npc = npcCreature->CastNPC();
    User* const user = userCreature->CastUser();
    TalkChannelCache::inst().mChannels[2].Cache(user->Id(), npc->Id(), 0);
    user->SendInvenHennaList();
    return false;
}

bool NpcHennaEquipListOpenForUnequip(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008bd4c0, NC_PROTOCOL 0x58.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    ::Disassemble(packet, "dd", &npcIndex, &userIndex);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    if (!npcCreature || !npcCreature->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0x18cc,
        //     npcIndex)
        // Noise: replace shipping coordinates with the requested list and invalid participant.
        L2_LOG_ERROR(
            Log, "[Henna] Cannot show removal list for user index %d: NPC index %d is missing or is not an NPC.", userIndex,
            npcIndex
        );
        return false;
    }

    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid creature index[%d]",
        //     L"NpcSocket.cpp",
        //     0x18d3,
        //     userIndex)
        // Noise: replace shipping coordinates with the requested list and invalid participant.
        L2_LOG_ERROR(
            Log, "[Henna] Cannot show removal list from NPC index %d: user index %d is missing or is not a player.",
            npcIndex, userIndex
        );
        return false;
    }

    CNPC* const npc = npcCreature->CastNPC();
    User* const user = userCreature->CastUser();
    TalkChannelCache::inst().mChannels[2].Cache(user->Id(), npc->Id(), 0);
    user->SendEquipedHennaListForUnequip();
    return false;
}

bool NpcQueryTeamBattlePledge(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008abf50, NC_PROTOCOL 0x59.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t queryFlag;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &queryFlag);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [query team battle]")
        L2_LOG_ERROR(
            Log,
            "[Team battle] Clan-status query %d rejected: NPC index %d or user index %d "
            "is missing or has the wrong creature type.",
            queryFlag, npcIndex, userIndex
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npcCreature->m_data->m_nResidenceID);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CTeamBattleAgit* const teamBattleAgit = worldAgit != nullptr ? worldAgit->CastTeamBattleAgit() : nullptr;
    if (teamBattleAgit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid agit[%d] in [query team battle]",
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log, "[Team battle] Clan-status query rejected: residence %d did not resolve to a team-battle clan hall.",
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }

    CPledge* const pledge = userCreature->CastUser()->GetPledge();
    int result = -1;
    if (pledge != nullptr) {
        if (pledge->IsWaitingToDismiss()) {
            userCreature->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_SIEGE_DURING_CLAN_DISSOLUTION_GRACE_PERIOD);
            return false;
        }
        if (pledge->IsWaitingTransferMaster()) {
            userCreature->CastUser()->GetSocket()->SendSystemMessage(
                SystemMessageId::CANNOT_REGISTER_CLAN_HALL_WAR_DURING_LEADER_TRANSFER
            );
            return false;
        }
        result = teamBattleAgit->GetPledgeRegisterStatusForNpcServer(pledge->GetDbId());
    }
    if (queryFlag == 0 && result == -1) {
        result = -3;
    }
    g_npcserver.Send("cddd", 0x41, npcIndex, userIndex, result);
    return false;
}

bool NpcGetBattleRoyalPledgeList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008ab9d4, NC_PROTOCOL 0x6a.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    ::Disassemble(packet, "dd", &npcIndex, &userIndex);

    CSPointer<CCreature> npcCreature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
    CSPointer<CCreature> userCreature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (!npcCreature || !npcCreature->IsNPC() || !userCreature || !userCreature->IsPC() ||
        userCreature->CastUser()->m_data->m_nWorldId != g_nWorldId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [query team battle]")
        L2_LOG_ERROR(
            Log,
            "[Team battle] Battle-royal clan-list request rejected for NPC index %d, user index %d: "
            "a participant is missing, has the wrong creature type, or the player is not in world %d.",
            npcIndex, userIndex, g_nWorldId
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(npcCreature->m_data->m_nResidenceID);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CTeamBattleAgit* const teamBattleAgit = worldAgit != nullptr ? worldAgit->CastTeamBattleAgit() : nullptr;
    if (teamBattleAgit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid agit[%d] in [get battle royal pledge list]",
        //     npcCreature->m_data->m_nResidenceID)
        L2_LOG_ERROR(
            Log,
            "[Team battle] Cannot obtain battle-royal clan list: residence %d did not resolve to a team-battle clan hall.",
            npcCreature->m_data->m_nResidenceID
        );
        return false;
    }

    std::int32_t pledgeIds[TBC_MAX] = {
        -1,
        -1,
        -1,
        -1,
        -1,
    };
    std::int32_t memberCounts[TBC_MAX] = {};
    for (int index = 0; index < TBC_MAX; ++index) {
        const int pledgeDatabaseId = teamBattleAgit->GetEntryPledgeDbId(static_cast<TEAM_BATTLE_COLOR>(index));
        if (pledgeDatabaseId == 0) {
            continue;
        }
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
        if (pledge) {
            pledgeIds[index] = pledge->GetIndex();
            memberCounts[index] = teamBattleAgit->GetBattleRoyalEntryMemberCount(pledge->GetDbId());
        }
    }

    CPledge* masterPledge = nullptr;
    {
        // Current source converts the temporary CSPointer to a borrowed
        // pointer, so the keep is released before the following reads.
        CSPointer<CPledge> masterLookup = CPledge::FindPledge(g_nWorldId, teamBattleAgit->GetMasterPledgeDbId());
        masterPledge = masterLookup.get();
    }
    int masterPledgeId = 0;
    int masterMemberCount = 0;
    if (masterPledge != nullptr) {
        masterPledgeId = masterPledge->GetIndex();
        masterMemberCount = teamBattleAgit->GetBattleRoyalEntryMemberCount(masterPledge->GetDbId());
    }

    g_npcserver.Send(
        "cdddddddddddddd", 0x4b, npcIndex, userIndex, pledgeIds[0], memberCounts[0], pledgeIds[1], memberCounts[1],
        pledgeIds[2], memberCounts[2], pledgeIds[3], memberCounts[3], pledgeIds[4], memberCounts[4], masterPledgeId,
        masterMemberCount
    );
    return false;
}
