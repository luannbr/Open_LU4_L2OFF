#include "NPC.h"
#include "Agit.h"
#include "AirShip.h"
#include "Alliance.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CGeneralObject.h"
#include "CWorld.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "EventMaker.h"
#include "ExDrop.h"
#include "GameEventSystem.h"
#include "InstantAgit.h"
#include "InstantZone.h"
#include "Item.h"
#include "KalieEvent.h"
#include "MakerEvent.h"
#include "Merchant.h"
#include "MonRaceData.h"
#include "MultiPartyCommandChannel.h"
#include "NewAction.h"
#include "NpcServer.h"
#include "Object.h"
#include "ObjectDB.h"
#include "PVPMatch.h"
#include "ParserExpData.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Quest.h"
#include "Relay.h"
#include "Residence.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "Trap.h"
#include "User.h"
#include "UserSocket.h"


#include "Portable/ClientProtocolRuntime.h"
#include "Portable/ProcessRuntime.h"
#include "Portable/RuntimeTimingTrace.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ShareDefine.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/WatchList.h"


#include "Shared/src/TextRuntime.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

// Original storage belongs to L2Server.obj. Its current body is not yet
// created in the portable build.
extern bool g_isPacketOptimizeMode;

std::int32_t CNPC::s_nMaxID = 0;
double CNPC::m_dAdenaDropValue1 = 90.0 / 7.0;
double CNPC::m_dAdenaDropValue2 = 15.0 * (90.0 / 7.0) + 10.0;
double CNPC::m_dItemDropValue1 = 18.0;
double CNPC::m_dItemDropValue2 = 190.0;
CNPCObjectMap g_NPCMap;
CNPC* g_TestNPC = nullptr;
std::map<std::int32_t, CNPCLoadingData> g_NPCLoadingDataMap;
CMwmrGrowingQueue<65536> g_PacketHelperPool(static_cast<int>(sizeof(CPacketHelperEx<20480>)));

namespace {

constexpr const char* kNpcSource = "NPC.cpp";

constexpr std::size_t kMaxNpcBroadcastIds = 0x139c;
thread_local std::array<std::int32_t, kMaxNpcBroadcastIds> g_npcBroadcastIds{};
thread_local std::int32_t g_npcBroadcastIdCount = 0;
thread_local std::int32_t g_npcBroadcastCandidateCount = 0;

std::int64_t PackNpcSkillParameters(int level, int subLevel, int unknown) noexcept {
    std::uint64_t packed = static_cast<std::uint64_t>(static_cast<std::int64_t>(level));
    packed = (packed << 16U) | static_cast<std::uint64_t>(static_cast<std::int64_t>(subLevel));
    packed = (packed << 16U) | static_cast<std::uint64_t>(static_cast<std::int64_t>(unknown));

    std::int64_t result = 0;
    std::memcpy(&result, &packed, sizeof(result));
    return result;
}

CObject* AsNpcObject(CNPC* npc) noexcept { return static_cast<CObject*>(npc); }

CObject* AsCreatureObject(CCreature* creature) noexcept { return static_cast<CObject*>(creature); }


std::int32_t NpcTeleportZoneCoordinate(std::int32_t coordinate, std::int32_t offset) noexcept {
    // Current x64 code performs the signed add with 32-bit wrapping and
    // then an arithmetic shift by 15 before adding the ten-cell border.
    return (runtime::AddInt32Wrapping(coordinate, offset) >> 15) + 10;
}

double ClampNpcDropUnit(double value) noexcept {
    // The current Win64 compare sequence maps NaN to the upper bound.
    if (value <= 1.0) {
        if (value < 0.0) {
            return 0.0;
        }
        if (value <= 1.0) {
            return value;
        }
    }
    return 1.0;
}

double ClampNpcDropProbability(double value) noexcept {
    // The current Win64 compare sequence maps NaN to the lower bound.
    if (0.0 <= value) {
        return 100.0 < value ? 100.0 : value;
    }
    return 0.0;
}


void LogNpcDroppedItem(CNPC& npc, CItem& item, std::int64_t count, const FVector& position) {
    if (CWatchItem* const watch = g_WatchList.CheckWatchList(WATCH_ITEM, item.m_nClassId)) {
        const std::wstring information = Utf16ToWide(
            std::u16string_view(watch->m_strInformation, target_utf16::BoundedLength(watch->m_strInformation, 256)),
            Utf16WidePolicy::PreserveInvalidOnWindows
        );
        // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
        Log.AddT(
            LOG_AUDIT,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
            u"%I64d,%I64d,,,,,,,,,,,,,",
            0x57b, npc.Id(), npc.m_nClassId, runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), information.c_str(),
            item.m_nClassId, item.Id()
        );
#else
        // Log.AddT(
        //     LOG_AUDIT,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        //     L"%I64d,%I64d,,,,,,,,,,,,,",
        //     0x57b,
        //     npc.Id(),
        //     npc.m_nClassId,
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     information.c_str(),
        //     item.m_nClassId,
        //     item.Id())
        Log.AddT(
            LOG_AUDIT,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
            u"%I64d,%I64d,,,,,,,,,,,,,",
            static_cast<long long>(0x57b), static_cast<long long>(npc.Id()), static_cast<long long>(npc.m_nClassId),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), information.c_str(),
            static_cast<long long>(item.m_nClassId), static_cast<long long>(item.Id())
        );
#endif
    }

    const wchar_t* const format = L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                                  L"%I64d,%I64d,,,,,%I64d,%I64d,%I64d,,%I64d,%I64d,,,"
                                  L"%I64d";
    // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH, format, 0x390, npc.GetClassId(), npc.GetClassId(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), npc.Level(),
        npc.IsBoss() ? 1U : 0U, item.m_data->GetEnchanted(), item.m_nClassId, count, npc.GetClassId(), npc.GetClassId(),
        item.Id()
    );
#else
    // Log.AddT(
    //     LOG_BOTH,
    //     format,
    //     0x390,
    //     npc.GetClassId(),
    //     npc.GetClassId(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     npc.Level(),
    //     npc.IsBoss() ? 1U : 0U,
    //     item.m_data->GetEnchanted(),
    //     item.m_nClassId,
    //     count,
    //     npc.GetClassId(),
    //     npc.GetClassId(),
    //     item.Id())
    Log.AddT(
        LOG_BOTH, format, static_cast<long long>(0x390), static_cast<long long>(npc.GetClassId()),
        static_cast<long long>(npc.GetClassId()), static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<long long>(npc.Level()),
        static_cast<long long>(npc.IsBoss() ? 1U : 0U), static_cast<long long>(item.m_data->GetEnchanted()),
        static_cast<long long>(item.m_nClassId), static_cast<long long>(count), static_cast<long long>(npc.GetClassId()),
        static_cast<long long>(npc.GetClassId()), static_cast<long long>(item.Id())
    );
#endif
    if (npc.IsBoss()) {
        // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
        Log.AddT(
            LOG_AUDIT, format, 0x58e, npc.GetClassId(), npc.GetClassId(), runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), npc.Level(),
            npc.IsBoss() ? 1U : 0U, item.m_data->GetEnchanted(), item.m_nClassId, count, npc.GetClassId(), npc.GetClassId(),
            item.Id()
        );
#else
        // Log.AddT(
        //     LOG_AUDIT,
        //     format,
        //     0x58e,
        //     npc.GetClassId(),
        //     npc.GetClassId(),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     npc.Level(),
        //     npc.IsBoss() ? 1U : 0U,
        //     item.m_data->GetEnchanted(),
        //     item.m_nClassId,
        //     count,
        //     npc.GetClassId(),
        //     npc.GetClassId(),
        //     item.Id())
        Log.AddT(
            LOG_AUDIT, format, static_cast<long long>(0x58e), static_cast<long long>(npc.GetClassId()),
            static_cast<long long>(npc.GetClassId()), static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<long long>(npc.Level()),
            static_cast<long long>(npc.IsBoss() ? 1U : 0U), static_cast<long long>(item.m_data->GetEnchanted()),
            static_cast<long long>(item.m_nClassId), static_cast<long long>(count), static_cast<long long>(npc.GetClassId()),
            static_cast<long long>(npc.GetClassId()), static_cast<long long>(item.Id())
        );
#endif
    }
}

bool NpcUtf16NameEquals(const char16_t (&stored)[25], std::u16string_view value) noexcept {
    std::size_t storedLength = 0;
    while (storedLength < 25 && stored[storedLength] != u'\0') {
        ++storedLength;
    }
    return std::u16string_view(stored, storedLength) == value;
}

void CopyNpcUtf16Name(char16_t (&destination)[25], std::u16string_view value) noexcept {
    std::memset(destination, 0, sizeof(destination));
    const std::size_t count = value.size() < 24 ? value.size() : 24;
    for (std::size_t index = 0; index < count; ++index) {
        destination[index] = value[index];
    }
}

} // namespace

bool SendClanDiedEvent(CNPC* const target) {
    // Current L2Server 0x008869a8, original NPC.cpp owner.
    L2_GUARD_FUNCTION();
    std::array<char, 0x4f9c> header{};
    int headerLength = ::Assemble(
        header.data(), static_cast<int>(header.size()), "dd", static_cast<int>(target->m_nId), target->GetIndex()
    );
    if (headerLength == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Buffer Overflow in SendClanDieEvent!!!",
        //     L"NPC.cpp",
        //     0xa2e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Buffer Overflow in SendClanDieEvent!!!",
        //     L"NPC.cpp",
        //     0xa2e)
        L2_LOG_ERROR(Log, "Cannot send NPC clan death event: packet header assembly failed.");
        return false;
    }

    const int clanHelpRange = target->m_data->m_nClanHelpRange;
    if (clanHelpRange <= 0) {
        return true;
    }

    class TLMFilter final : public CTLMFilterDist {
    public:
        TLMFilter(
            const int inZoneId,
            const FVector& position,
            const double distance,
            const int targetIndex,
            const int targetClassIndex
        ) :
            CTLMFilterDist(inZoneId, position, distance), targetIndex_(targetIndex), targetClassIndex_(targetClassIndex) {}

        bool CanDo(CObject* const object) override {
            if (!CTLMFilterDist::CanDo(object)) {
                return false;
            }
            CNPC* const npc = __SafeCastNpc(object);
            return npc != nullptr && npc->m_data->index != targetIndex_ &&
                Singleton<CClanHelpTable>::inst().GetFlag(targetClassIndex_, npc->m_data->m_nClassIndex) &&
                npc->CanGetNpcEvent();
        }

    private:
        int targetIndex_;
        int targetClassIndex_;
    };

    TLMFilter filter(
        static_cast<int>(target->GetInZoneID()), target->GetPos(), static_cast<double>(clanHelpRange), target->GetIndex(),
        target->m_data->m_nClassIndex
    );

    CPacketHelperEx<20480> packet;
    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(CPacketHelperEx<20480>& packet) : packet_(packet) {}

        bool CanDo(CObject* const object, double) override {
            packet_.Assemble("d", static_cast<int>(object->m_nId));
            return packet_.GetAssembledCount() < 0x80;
        }

    private:
        CPacketHelperEx<20480>& packet_;
    };

    TLMFunc append(packet);

    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &append);
    if (packet.GetAssembledCount() <= 0) {
        return true;
    }

    packet.Assemble("d", 0);
    const int appended = ::Assemble(
        header.data() + headerLength, static_cast<int>(header.size()) - headerLength, "b", packet.GetLength(),
        packet.GetPacket()
    );
    if (appended == 0) {
        return false;
    }
    headerLength += appended;
    g_npcserver.Send("cb", 0x1d, headerLength, header.data());
    return true;
}

void ComputeClanHelpTable() {
    // Current L2Server 0x00888984. The current build always computes this
    // table in LoadData. Unlike the older TotalChecker source, it has no
    // g_bComputeClanHelpTable branch and does not call SaveTable afterwards.
    L2_GUARD_FUNCTION();

    constexpr std::int32_t kFirstNpcClassId = 1'000'001;
    constexpr std::int32_t kNpcClassIdBase = 1'000'000;

    auto getRegisteredNpc = [=](const std::int32_t classId, const std::int32_t diagnosticClassId) -> CNPC* {
        const std::uint32_t registeredIndex = static_cast<std::uint32_t>(classId - kNpcClassIdBase);
        if (registeredIndex >= g_bRegisteredNpcID.size()) {
            // The current xarray implementation throws this exact text.
            throw std::out_of_range("invalid xarray<T> subscript");
        }
        if (g_bRegisteredNpcID[registeredIndex] == 0) {
            return nullptr;
        }

        CObject* const object = g_ObjectDB.GetObjectW(classId, false);
        if (object == nullptr) {
            // The second (inner) lookup reports n1 as well.
            // current 0x00888af6 loads the outer class id into the vararg.
            // Log.Add(
            //     LOG_ERROR,
            //     L"Computing ClanTable Error : npc id[%d] is invalid",
            //     diagnosticClassId)
            // L2_LOG_ERROR(Log,
            //     L"Computing ClanTable Error : npc id[%d] is invalid",
            //     diagnosticClassId)
            L2_LOG_ERROR(
                Log, "Cannot compute NPC clan table: registered class %d has no object (diagnostic class %d).", classId,
                diagnosticClassId
            );
            return nullptr;
        }
        return static_cast<CNPC*>(object);
    };

    for (std::int32_t n1 = kFirstNpcClassId; n1 <= CNPC::s_nMaxID; ++n1) {
        CNPC* const npc1 = getRegisteredNpc(n1, n1);
        if (npc1 == nullptr) {
            continue;
        }

        const std::int32_t classIndex1 = npc1->m_data->m_nClassIndex;
        const std::int32_t clanSize1 = npc1->m_data->m_nClanIdCount;

        for (std::int32_t n2 = kFirstNpcClassId; n2 <= CNPC::s_nMaxID; ++n2) {
            CNPC* const npc2 = getRegisteredNpc(n2, n1);
            if (npc2 == nullptr) {
                continue;
            }

            const std::int32_t classIndex2 = npc2->m_data->m_nClassIndex;
            if (n1 == n2 && clanSize1 == 1 && npc1->m_data->m_ClanId[0] == -1) {
                Singleton<CClanHelpTable>::inst().SetFlag(classIndex1, classIndex2, true);
                continue;
            }

            const std::int32_t clanSize2 = npc2->m_data->m_nClanIdCount;
            for (std::int32_t clanIndex1 = 0; clanIndex1 < clanSize1; ++clanIndex1) {
                const std::int32_t clanId = npc1->m_data->m_ClanId[clanIndex1];
                if (clanId == -1) {
                    continue;
                }

                for (std::int32_t clanIndex2 = 0; clanIndex2 < clanSize2; ++clanIndex2) {
                    if (clanId != npc2->m_data->m_ClanId[clanIndex2]) {
                        continue;
                    }

                    bool ignoreClan = false;
                    const auto loadingData = g_NPCLoadingDataMap.find(npc2->m_data->m_nClassID);
                    if (loadingData != g_NPCLoadingDataMap.end()) {
                        ignoreClan = loadingData->second.m_IgnoreDB.find(n1) != loadingData->second.m_IgnoreDB.end();
                    }

                    // Current 0x00888c84..0x00888c9c passes !ignoreClan to
                    // SetFlag, so an ignore entry actively clears the bit.
                    Singleton<CClanHelpTable>::inst().SetFlag(classIndex1, classIndex2, !ignoreClan);
                }
            }
        }
    }
}

// Current CRT slots 6464/6466/6467/6468/6469 keep the physical
// NPC.cpp order g_NPCMap, g_NPCLoadingDataMap, g_PacketHelperPool,
// g_UniqNPCDB, g_NPCPosDB. Keep the latter owners beside their original
// method families so construction and reverse teardown keep that order.
CUniqNPCDB g_UniqNPCDB;

CUniqNPCDB::CUniqNPCDB() = default;
CUniqNPCDB::~CUniqNPCDB() = default;

void CUniqNPCDB::DeleteAll() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_lock);
        for (auto& entry : m_UniqNPCInzoneDB) {
            entry.second.clear();
        }
        m_UniqNPCInzoneDB.clear();
    }
}

bool CUniqNPCDB::AddUniqNPC(CNPC* const npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    std::optional<std::wstring> npcNameStorage;
    {
        L2_GUARD(m_lock);
        NpcMap& uniqueNpcs = m_UniqNPCInzoneDB[static_cast<int>(npc->GetInZoneID())];
        const auto inserted = uniqueNpcs.emplace(npc->GetClassId(), npc->Id());
        if (inserted.second) {
            return true;
        }

        const std::wstring& npcName = npcNameStorage.emplace(Utf16ToWide(
            std::u16string_view(npc->m_data->Name(), target_utf16::BoundedLength(npc->m_data->Name(), 256)),
            Utf16WidePolicy::PreserveInvalidOnWindows
        ));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] [%s][%d] common npc try to be unique npc. "
        //     L"[%d] [%s]",
        //     L"NPC.cpp",
        //     0x1e95,
        //     npc->GetClassId(),
        //     npcName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] [%s][%d] common npc try to be unique npc. "
        //     L"[%d] [%s]",
        //     L"NPC.cpp",
        //     0x1e95,
        //     npc->GetClassId(),
        //     npcName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot register unique NPC %s (class %d): this class is already registered in zone %u.",
            WideToUtf8(npcName).c_str(), npc->GetClassId(), static_cast<unsigned>(npc->GetInZoneID())
        );
    }
    return false;
}

bool CUniqNPCDB::DeleteUniqNPC(CNPC* const npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    const int inZoneId = static_cast<int>(npc->GetInZoneID());
    const int npcClassId = npc->GetClassId();
    {
        L2_GUARD(m_lock);
        const auto zone = m_UniqNPCInzoneDB.find(inZoneId);
        if (zone == m_UniqNPCInzoneDB.end()) {
            if (!npc->IsBoss() && NpcServer::Inst()->Connected()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] NPC is not uniq NPC[%d],[%d]",
                //     L"NPC.cpp",
                //     0x1eb7,
                //     npcClassId,
                //     inZoneId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] NPC is not uniq NPC[%d],[%d]",
                //     L"NPC.cpp",
                //     0x1eb7,
                //     npcClassId,
                //     inZoneId)
                L2_LOG_ERROR(
                    Log, "Cannot remove unique NPC class %d: zone %d has no unique-NPC registry.", npcClassId, inZoneId
                );
                runtime::process::ReportCallStack(LOG_ERROR, false);
            }
            return false;
        }

        zone->second.erase(npcClassId);
        if (zone->second.empty()) {
            m_UniqNPCInzoneDB.erase(zone);
        }
    }
    return true;
}

std::uint32_t CUniqNPCDB::GetUniqNPCSID(const int inZoneId, const int npcClassId) {
    L2_GUARD_FUNCTION();

    const std::uint32_t sid = [&] {
        L2_GUARD(m_lock);
        std::uint32_t sid = 0;
        const auto zone = m_UniqNPCInzoneDB.find(inZoneId);
        if (zone != m_UniqNPCInzoneDB.end()) {
            const auto npc = zone->second.find(npcClassId);
            if (npc != zone->second.end()) {
                sid = npc->second;
            }
        }
        return sid;
    }();
    return sid;
}

CNPC* CUniqNPCDB::GetUniqNPC(const int inZoneId, const int npcClassId) {
    L2_GUARD_FUNCTION();

    // Current 0x00892362 calls the creature manager directly, including
    // the zero SID returned when this unique NPC has not spawned.
    CObject* const object = g_CreatureSmartIdManager.GetObjectW(GetUniqNPCSID(inZoneId, npcClassId));
    return object != nullptr && object->IsNPC() ? object->CastNPC() : nullptr;
}

CBoss* CUniqNPCDB::GetBossNPC(const int inZoneId, const int npcClassId) {
    L2_GUARD_FUNCTION();

    CNPC* const npc = GetUniqNPC(inZoneId, npcClassId);
    return npc != nullptr && npc->IsBoss() ? npc->CastBoss() : nullptr;
}

CNPCPosDB g_NPCPosDB;

bool CNPCPosDB::GetNPCPos(const int npcClassId, NPCPos& position) {
    L2_GUARD_FUNCTION();

    const auto found = m_positions.find(npcClassId);
    if (found == m_positions.end()) {
        return false;
    }
    position = found->second;
    return true;
}

bool CNPCPosDB::AddNPCPos(CNPC* const npc) {
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    NPCPos position{};
    position.nInzoneID = static_cast<int>(npc->GetInZoneID());
    position.vPos = npc->GetPos();

    const bool inserted = [&] {
        L2_GUARD(m_lock);
        const bool inserted = m_positions.emplace(npc->GetClassId(), position).second;
        return inserted;
    }();
    return inserted;
}

bool CNPC::CopyUserBuffToNPC_Do() {
    L2_GUARD_FUNCTION();

    User* const master = GetUserOrMaster();
    if (!IsDoppelganger() && (!IsSummonOrPet() || master != GetUserOrMaster())) {
        return false;
    }

    bool hadTransferredState = false;
    {
        L2_GUARD(m_InfectedSkillsSect);
        if (!m_InfectedSkillsOfMaster.empty()) {
            for (const InfectedSkillType& source : m_InfectedSkillsOfMaster) {
                InfectedSkillType infectedSkill(source);
                CSkillInfo* const skillInfo = infectedSkill.pSkillInfo;
                if (skillInfo->m_eDebuff != DB_GOOD || skillInfo->m_nTransferAbnormal != 0 ||
                    skillInfo->m_nAbnormalType == ABNS_ABILITY_CHANGE || !CanBeBuffed(skillInfo, master)) {
                    continue;
                }

                bool duplicate = false;
                for (const InfectedSkillType& existing : m_InfectedSkills) {
                    if (existing.pSkillInfo->m_nAbnormalType == skillInfo->m_nAbnormalType) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) {
                    continue;
                }

                m_InfectedSkills.push_back(infectedSkill);
                _AddAbnormalStatus(*skillInfo, nullptr, true, true, SPST_CASTING);
            }
            m_InfectedSkillsOfMaster.clear();
            hadTransferredState = true;
        }
    }

    if (hadTransferredState) {
        ValidateAllOnSIM();
        CheckAbnormalVisualEffect();
        ValidateChanged(VB_MAX);
    }
    return true;
}

bool CNPC::ProcessEnteringWorldServer(const unsigned int inZoneId, const FVector& position) {
    L2_GUARD_FUNCTION();

    CInstantZone* instantZone = nullptr;
    bool enterRealWorld = false;
    bool requiresInstantZone = false;

    if (!IsSummonOrPet()) {
        if (m_data->m_BossID != 0) {
            CGeneralObject* const owner = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(m_data->m_BossID));
            if (owner != nullptr && owner->IsNPC()) {
                if (owner->IsInRealWorld()) {
                    enterRealWorld = true;
                } else {
                    instantZone = owner->GetInstantZone();
                    requiresInstantZone = true;
                }
            }
        }
    } else {
        CSummon* const summon = CastSummon();
        User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
        if (master != nullptr) {
            if (master->IsInRealWorld()) {
                enterRealWorld = true;
            } else {
                instantZone = master->GetInstantZone();
                requiresInstantZone = true;
            }
        }
    }

    if (!enterRealWorld && !requiresInstantZone) {
        if (inZoneId == 0) {
            enterRealWorld = true;
        } else {
            requiresInstantZone = true;
            CGeneralObject* const zoneObject = CGeneralObject::GetObjectW(inZoneId);
            if (zoneObject != nullptr && zoneObject->IsInZone()) {
                // Current L2Server 0x00887e8b..0x00887e97 calls
                // CObject vslot +0x3e8 (slot 125, CastInZone) on the object
                // returned for inZoneId. GetInstantZone is a different
                // wrapper which resolves the object's own containing zone.
                instantZone = zoneObject->CastInZone();
            }
        }
    }

    if (requiresInstantZone &&
        (instantZone == nullptr || instantZone->IsDeleting() || instantZone->m_Deleted != 0 ||
            !instantZone->EnterZone(this, true))) {
        Despawn(true, false);
        return false;
    }

    if (!IsSummonOrPet()) {
        CWorld::Inst().EnterWorld(this, position, inZoneId, false, false);
        return true;
    }

    CSummon* const summon = CastSummon();
    User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
    if (master != nullptr && master->IsInAirShip()) {
        CAirShip* const airShip = g_AirShipSmartIdManager.GetObjectW(master->GetMyAirShipID());
        if (airShip != nullptr) {
            GetOnAirShip(airShip);
        }
        return true;
    }

    CWorld::Inst().EnterWorld(this, position, inZoneId, false, false);
    return true;
}

bool CNPC::GetOnAirShip(CAirShip* const airShip) {
    L2_GUARD_FUNCTION();
    if (IsInAirShip()) {
        return false;
    }

    StopMove();
    SetMyAirShipID(static_cast<unsigned int>(airShip->m_nId));
    const FVector position = airShip->GetPos();
    g_ptlUserMap->BroadcastToNeighbor(
        *this, 0xf000, position, "chddddd", 0xfe, 100, static_cast<int>(m_nId), static_cast<int>(airShip->m_nId),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    );
    return CWorld::Inst().GetOnAirShip(this, airShip);
}

bool CNPC::GetOffAirShip(CAirShip* const airShip, const FVector position) {
    L2_GUARD_FUNCTION();
    if (!IsInAirShip()) {
        return false;
    }

    StopMove();
    SetMyAirShipID(0);
    return CWorld::Inst().GetOffAirShip(this, airShip, position);
}

void CNPC::EnterWorld(const bool restoreParameters, const int hitPoints, const int manaPoints, const unsigned int inZoneId) {
    L2_GUARD_FUNCTION();

    EnterWorldImpl();
    ValidateParameters(VB_SKILLMOD, false);
    CopyUserBuffToNPC_Do();

    CPet* const initialPet = CastPet();
    if (IsPet() && initialPet != nullptr && initialPet->IsSyncLevel()) {
        initialPet->ValidateSyncLevel();
    }

    ValidateParameters(VB_MAX, true);
    CIOObject::SetTimeStamp(&m_nEnterWorldTime);

    FVector position{};
    {
        L2_GUARD(*m_lock);
        position = m_data->GetPos();
        if (!restoreParameters) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 0x0b);
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 0x0c);
            if (IsDoppelganger()) {
                m_data->SetHP(m_data->m_dBaseHP * m_dHPRate_Reserve);
                m_data->SetMP(m_data->m_dBaseMP * m_dMPRate_Reserve);
            }
        } else {
            m_data->SetHP(static_cast<double>(hitPoints));
            m_data->SetMP(static_cast<double>(manaPoints));
            if (IsPet()) {
                if (m_data->m_dBaseHP <= m_data->m_dHP && m_data->m_dHP != m_data->m_dBaseHP) {
                    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 0x0b);
                }
                if (m_data->m_dBaseMP <= m_data->m_dMP && m_data->m_dMP != m_data->m_dBaseMP) {
                    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 0x0c);
                }
            }
        }
    }

    if (m_data->m_nPledgeID != 0) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_data->m_nPledgeID));
        if (pledge) {
            m_nPledgeCrestId = static_cast<std::int32_t>(pledge->GetCrestId());
            if (CAlliance* const alliance = pledge->GetAlliance()) {
                m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                    reinterpret_cast<std::int32_t&>(m_data->m_nAllianceId),
                    static_cast<std::int32_t>(alliance->m_data->m_nDbId), 0x2f
                );
                m_nAllianceCrestId = static_cast<std::int32_t>(alliance->m_data->m_nCrestId);
            }

            if (m_data->m_bIsNPCRelatedToPledge && GetClassId() == 0x0f8d85) {
                pledge->SendSystemMessageToMembersWithNpcId(SystemMessageId::CLAN_FLAG_SUMMONED, GetClassId());
                if (User* const summoner = GetSummoner()) {
                    const char16_t* const pledgeName = pledge->GetPledgeName();
                    const std::u16string pledgeNameUtf16 =
                        pledgeName != nullptr ? std::u16string(pledgeName) : std::u16string{};
                    CLogFormatter formatter;
                    formatter.WriteLog(
                        0x0802, summoner->GetDBID(), summoner->GetAccountID(), 0, 0,
                        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                        runtime::Win64CvttDoubleToInt32(position.z), pledgeNameUtf16.c_str(), nullptr, nullptr,
                        summoner->m_data->m_Race, summoner->m_data->m_Sex, summoner->m_data->m_nClass,
                        summoner->m_data->m_nLevel, summoner->GetSocialClass(), pledge->GetDbId(), pledge->GetPledgeLevel(),
                        0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
                    );
                }
            }
        }
    }

    if (IsSummonOrPet()) {
        CSummon* const summon = CastSummon();
        User* const master = summon != nullptr ? summon->GetMaster() : nullptr;
        if (summon != nullptr && master != nullptr) {
            const FVector summonPosition = summon->GetPos();
            const FVector masterPosition = master->GetPos();
            const double radians = std::atan2(masterPosition.y - summonPosition.y, masterPosition.x - summonPosition.x);
            summon->m_data->SetDir(FAngle{runtime::Win64CvttDoubleToInt32(radians * 65535.0 / 6.283185307179586)});
            summon->CacheMasterInfo(master);
            if (CParty* const party = master->GetParty()) {
                party->AddPetWindowPacket(summon);
            }
        }

        if (IsPet()) {
            CPet* const pet = CastPet();
            if (pet->IsSyncLevel()) {
                pet->ValidateSyncLevel();
            }
        }
    }

    if (!ProcessEnteringWorldServer(inZoneId, position)) {
        return;
    }

    if (IsBoss() || m_data->m_bUnique) {
        if (!g_UniqNPCDB.AddUniqNPC(this)) {
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[NO_ERROR] AddUniqNPC is strange [%d][IsBoss=%d][bUnique=%d]",
            //     GetClassId(),
            //     IsBoss() ? 1 : 0,
            //     m_data->m_bUnique ? 1 : 0)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] AddUniqNPC is strange [%d][IsBoss=%d][bUnique=%d]",
            //     GetClassId(),
            //     IsBoss() ? 1 : 0,
            //     m_data->m_bUnique ? 1 : 0)
            L2_LOG_ERROR(
                Log, "NPC world entry continues after unique registration failed: class %d, boss=%d, unique=%d.",
                GetClassId(), IsBoss() ? 1 : 0, m_data->m_bUnique ? 1 : 0
            );
        }
        // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,,,,,", 0x0590, static_cast<std::int64_t>(Id()),
            static_cast<std::int64_t>(m_nClassId), static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z))
        );
#else
        // Log.AddT(
        //     LOG_AUDIT,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,,,,,",
        //     0x0590,
        //     static_cast<std::int64_t>(Id()),
        //     static_cast<std::int64_t>(m_nClassId),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)))
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,,,,,,,,,,,,,,,", static_cast<long long>(0x0590),
            static_cast<long long>(static_cast<std::int64_t>(Id())),
            static_cast<long long>(static_cast<std::int64_t>(m_nClassId)),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)))
        );
#endif
    }

    if (g_bEnableTeleportToNPCInternal) {
        g_NPCPosDB.AddNPCPos(this);
    }

    if (CWatchItem* const watch = g_WatchList.CheckWatchList(WATCH_NPC, m_nClassId - 1000000)) {
        const std::wstring information = Utf16ToWide(
            std::u16string_view(watch->m_strInformation, target_utf16::BoundedLength(watch->m_strInformation, 256)),
            Utf16WidePolicy::PreserveInvalidOnWindows
        );
        // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,,,,,,,,,,,,,,,", 0x057a, static_cast<std::int64_t>(Id()),
            static_cast<std::int64_t>(m_nClassId), static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), information.c_str()
        );
#else
        // Log.AddT(
        //     LOG_AUDIT,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,,,,,,,,,,,,,,,",
        //     0x057a,
        //     static_cast<std::int64_t>(Id()),
        //     static_cast<std::int64_t>(m_nClassId),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     information.c_str())
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,,,,,,,,,,,,,,,", static_cast<long long>(0x057a),
            static_cast<long long>(static_cast<std::int64_t>(Id())),
            static_cast<long long>(static_cast<std::int64_t>(m_nClassId)),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z))),
            information.c_str()
        );
#endif
    }

    const int npcClass = static_cast<int>(m_data->m_nClass);
    if (npcClass == 0x55) {
        g_MonRaceDB.CheckRaceManagerLoc(m_data->GetPos());
    } else if (npcClass == 0x78 && IsPCTrap()) {
        if (CPCTrap* const trap = CastPCTrap()) {
            trap->Activate(GetMaster());
        }
    }

    if (m_data->m_nResidenceID != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
        CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
        if (worldAgit != nullptr) {
            worldAgit->OnBossNpcEnterWorld(this);
        }
    }

    if (!restoreParameters) {
        if (m_bChargeFullHPBySkillEnterWorld) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 0x0b);
        }
        if (m_bChargeFullMPBySkillEnterWorld) {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 0x0c);
        }
    }
}

// Current L2Server 0x007f535c, class number 295.
L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CNPC)

void CNPC::SetDefaultParameters() {
    // Current L2Server 0x008844d4.
    L2_GUARD_FUNCTION();

    for (const std::int64_t packed : m_pClassConst->m_SkillList) {
        const std::uint64_t bits = static_cast<std::uint64_t>(packed);
        AcquireSkill(
            static_cast<int>(bits >> 32U), static_cast<int>((bits >> 16U) & UINT64_C(0xffff)),
            static_cast<int>(bits & UINT64_C(0xffff)), false, true
        );
    }

    {
        L2_GUARD(*m_lock);
        m_nValidateChanged = 0;
        m_nValidateExChanged = 0;
        m_data->m_nResidenceOwnerID = 0;
        m_data->m_dwSummonTime = 0;
        m_data->m_dwSummonLifeTime = 1200000;
        m_data->m_bIsCorpse = false;
        m_data->m_bIsDiss = false;
        if (m_data->m_nCorpseTime < 1) {
            m_data->m_nCorpseTime = 1;
        }

        m_data->SetAttackType(m_data->m_BaseAttackType);
        m_data->SetAttackRange(m_data->m_nBaseAttackRange);
        m_data->m_nRandomDamage = m_data->m_nBaseRandomDamage;

        constexpr VALIDATE::Enum kCopiedParameters[] = {
            VALIDATE::PATTACK,
            VALIDATE::PDEFEND,
            VALIDATE::MATTACK,
            VALIDATE::MDEFEND,
            VALIDATE::PATTACKSPEED,
            VALIDATE::MATTACKSPEED,
            VALIDATE::PCRITICAL,
            VALIDATE::PHIT,
            VALIDATE::PAVOID,
            VALIDATE::SHIELDDEFENSE,
            VALIDATE::SHIELDDEFENSERATE,
        };
        for (const VALIDATE::Enum parameter : kCopiedParameters) {
            m_data->m_nOrgParam[parameter] = m_data->m_nBaseParam[parameter];
        }

        m_data->SetGuility(0);
        m_data->m_nKarma = 0;
        m_bChargeFullHPBySkillEnterWorld = false;
        m_bChargeFullMPBySkillEnterWorld = false;
        m_data->m_OriginAbnormalResist.m_N[0] = 0;
        m_data->m_OriginAbnormalResist.m_N[1] = 0;
    }
}

CNPC::CNPC() :
    m_nParamChanged(0),
    m_bSpeedChanged(false),
    m_alignment38e1To38e8{},
    m_dHPRate_Reserve(0.0),
    m_dMPRate_Reserve(0.0),
    m_nNpcDeleteCount(0),
    m_bIsPureAncestor(true),
    m_bIsPureAncestorOrigin(true),
    m_alignment38feTo3900{},
    m_pClassConst(new CNPCClassConst()),
    m_MasterNameIndex(0),
    m_sMasterName{},
    m_bDespawnByScript(false),
    m_partialTail393f{},
    m_nPledgeCrestId(0),
    m_nAllianceCrestId(0),
    m_nAirShipServerID(0),
    m_bChargeFullHPBySkillEnterWorld(false),
    m_bChargeFullMPBySkillEnterWorld(false),
    m_bAbsoluteReturn(false),
    m_alignment394fTo3950{},
    m_nReturnedTime(0),
    m_bHadFakeItem(false),
    m_alignment3955To3958{},
    m_bCanDropItem(true),
    m_partialTail3969To396c{},
    m_NickNameIndex(0),
    m_nChestItemClassID(0),
    m_nRHandItemClassID(0),
    m_nLHandItemClassID(0),
    m_nOldRHandItemClassID(0),
    m_nOldLHandItemClassID(0),
    m_nOldChestItemClassID(0),
    m_nEnchantOfWeapon(0),
    m_State(0),
    // m_eDoppelGangerType(0),
    m_eDoppelGangerType(DoppelGangerType::NONE),
    m_bIsSummonedNpc(false),
    m_bIsSummonedFriendNpc(false),
    m_alignment3993To3998{},
    m_dLastCorpseItemChanceProb(1.0),
    m_dLastMultiItemChanceProb(1.0),
    m_dLastExItemChanceProb(1.0),
    m_bHasSummoner(false),
    m_bCanChanageDir(true),
    m_alignment39b2To39b4{},
    m_nFakeClassId(-1),
    m_nEnterWorldTime(0),
    m_nFakeNpcSuicideTime(0),
    m_sidKillerMaster(0),
    m_nKillerMemberCount(0),
    m_bSetResult(true),
    m_alignment39c9To39cc{},
    m_nEventDropProbType(0),
    m_alignment39e8To39f0{},
    m_alignment3a08To3a10{},
    m_dNpcCustomizedSizeRate(1.0),
    m_bLogToError(false),
    m_alignment3a19To3a20{},
    m_alignment3a38To3a40{},
    m_isEnterWorldPacket(1),
    m_alignment3becTo3bf0{},
    m_pac(nullptr),
    m_pav(nullptr),
    m_pat(nullptr),
    m_pad(nullptr),
    m_alignment3c28To3c30{},
    m_isProcessingDroidNPCSetting(0),
    m_isDroidNPC(false),
    m_alignment3c35To3c38{},
    m_nDroidNPCAccountID(0),
    m_nDroidNPCCharDBID(0),
    m_DroidNPCInfo{},
    m_nDroidSpawnDir(0),
    m_nDroidSpawnDefaultParam(0),
    m_nDroidSpawnDefaultHP(0),
    m_nDroidSpawnDefaultMP(0),
    m_nDroidSpawnInZoneID(0),
    m_eNpcTypeForDeco(NpcTypeForDeco::NotDecoNpc),
    m_nAgitCIDorPledgeDBID(0),
    m_nAgitSlot(0),
    m_bMonsterBookNPC(false),
    m_alignment3d71To3d74{},
    m_nAliveHour(0) {
    L2_GUARD_FUNCTION();
    m_data->m_bIsNPCRelatedToPledge = false;
    m_NPCInfoPacket.Init(2);
}

CNPC::CNPC(const CNPC& other) :
    CCreature(other),
    m_nParamChanged(0),
    m_bSpeedChanged(false),
    m_alignment38e1To38e8{},
    m_dHPRate_Reserve(other.m_dHPRate_Reserve),
    m_dMPRate_Reserve(other.m_dMPRate_Reserve),
    m_nNpcDeleteCount(0),
    m_bIsPureAncestor(false),
    m_bIsPureAncestorOrigin(false),
    m_alignment38feTo3900{},
    m_pClassConst(other.m_pClassConst),
    m_MasterNameIndex(-1),
    m_sMasterName{},
    m_bDespawnByScript(false),
    m_partialTail393f{},
    m_nPledgeCrestId(other.m_nPledgeCrestId),
    m_nAllianceCrestId(other.m_nAllianceCrestId),
    m_nAirShipServerID(other.m_nAirShipServerID),
    m_bChargeFullHPBySkillEnterWorld(false),
    m_bChargeFullMPBySkillEnterWorld(false),
    m_bAbsoluteReturn(false),
    m_alignment394fTo3950{},
    m_nReturnedTime(0),
    m_bHadFakeItem(false),
    m_alignment3955To3958{},
    m_bCanDropItem(true),
    m_partialTail3969To396c{},
    m_NickNameIndex(-1),
    m_nChestItemClassID(other.m_nChestItemClassID),
    m_nRHandItemClassID(other.m_nRHandItemClassID),
    m_nLHandItemClassID(other.m_nLHandItemClassID),
    m_nOldRHandItemClassID(0),
    m_nOldLHandItemClassID(0),
    m_nOldChestItemClassID(0),
    m_nEnchantOfWeapon(other.m_nEnchantOfWeapon),
    m_State(0),
    m_eDoppelGangerType(other.m_eDoppelGangerType),
    m_bIsSummonedNpc(other.m_bIsSummonedNpc),
    m_bIsSummonedFriendNpc(other.m_bIsSummonedFriendNpc),
    m_alignment3993To3998{},
    m_dLastCorpseItemChanceProb(0.0),
    m_dLastMultiItemChanceProb(0.0),
    m_dLastExItemChanceProb(0.0),
    m_bHasSummoner(const_cast<CNPC&>(other).HasSummoner()),
    m_bCanChanageDir(other.m_bCanChanageDir),
    m_alignment39b2To39b4{},
    m_nFakeClassId(other.m_nFakeClassId),
    // Shipping L2Server copied the adjacent fake-suicide duration here:
    // m_nEnterWorldTime(other.m_nFakeNpcSuicideTime),
    // Use the actual enter-world timestamp so a copy is internally
    // consistent before EnterWorld() refreshes it.
    m_nEnterWorldTime(other.m_nEnterWorldTime),
    m_nFakeNpcSuicideTime(other.m_nFakeNpcSuicideTime),
    m_sidKillerMaster(0),
    m_nKillerMemberCount(0),
    m_bSetResult(true),
    m_alignment39c9To39cc{},
    m_nEventDropProbType(other.m_nEventDropProbType),
    m_vExDropItem(other.m_vExDropItem),
    m_alignment39e8To39f0{},
    m_vBCCondition(other.m_vBCCondition),
    m_alignment3a08To3a10{},
    m_dNpcCustomizedSizeRate(other.m_dNpcCustomizedSizeRate > 0.0 ? other.m_dNpcCustomizedSizeRate : 1.0),
    m_bLogToError(false),
    m_alignment3a19To3a20{},
    m_vEventController(other.m_vEventController),
    m_alignment3a38To3a40{},
    m_isEnterWorldPacket(1),
    m_alignment3becTo3bf0{},
    m_pac(nullptr),
    m_pav(nullptr),
    m_pat(nullptr),
    m_pad(nullptr),
    m_alignment3c28To3c30{},
    m_isProcessingDroidNPCSetting(0),
    m_isDroidNPC(false),
    m_alignment3c35To3c38{},
    m_nDroidNPCAccountID(0),
    m_nDroidNPCCharDBID(0),
    m_DroidNPCInfo{},
    m_nDroidSpawnDir(0),
    m_nDroidSpawnDefaultParam(0),
    m_nDroidSpawnDefaultHP(0),
    m_nDroidSpawnDefaultMP(0),
    m_nDroidSpawnInZoneID(0),
    m_eNpcTypeForDeco(NpcTypeForDeco::NotDecoNpc),
    m_nAgitCIDorPledgeDBID(0),
    m_nAgitSlot(0),
    m_bMonsterBookNPC(other.m_bMonsterBookNPC),
    m_alignment3d71To3d74{},
    m_nAliveHour(0) {
    L2_GUARD_FUNCTION();
    LagAlert lagAlert(5000, "CNPC::CNPC", 0xb4);
    {
        L2_GUARD(*m_lock);
        m_data->CopyFrom(other.m_data);
    }
    m_data->m_bIsNPCRelatedToPledge = false;
    m_NPCInfoPacket.Init(2);
}

CObject* CNPC::Copy() {
    // Current L2Server 0x008931d0.
    L2_GUARD_FUNCTION();
    CNPC* const copy = new CNPC(*this);
    LagAlert lagAlert(5000, "CNPC::Copy", 0x137d);
    copy->OnCreate();
    return copy;
}

CNPC* CNPC::CopyFakeNPC() {
    // Current L2Server 0x0089309c.
    L2_GUARD_FUNCTION();
    CNPC* const copy = new CNPC(*this);
    LagAlert lagAlert(5000, "CNPC::CopyFakeNPC", 0x136d);
    copy->OnCreateFakeNPC();
    return copy;
}

CNPC::~CNPC() {
    g_ServerContext.Npc.fetch_sub(1, std::memory_order_seq_cst);
    m_pClassConst = nullptr;
}

std::int32_t CNPC::Release(
    const char* const callerFile, const int callerLine, const ObjectRefType type, const bool releaseFromTimer
) {
    static_cast<void>(releaseFromTimer);

    RefHelperStruct& helper = m_RefHelpers.m_RefData[static_cast<std::size_t>(type)];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_seq_cst) - 1;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]  sub-type[%s] sub-reference count[%d] "
        //     L"is below zero!",
        //     callerFile,
        //     callerLine,
        //     ObjectRefTypeName(type),
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log, "NPC reference count became negative: type=%s, count=%d, release caller=%s:%d",
            ::WideToUtf8(ObjectRefTypeName(type)).c_str(), static_cast<int>(subtypeRemaining),
            callerFile != nullptr ? callerFile : "(unknown)", callerLine
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     helper.m_nRefCount.load(std::memory_order_relaxed),
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[%d]-[%s]  last_add[%S][%d] last_release[%S][%d]",
        //     helper.m_nRefCount.load(std::memory_order_relaxed),
        //     ObjectRefTypeName(type),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log, "NPC reference history: type=%s, current count=%d, last add=%s:%d, last release=%s:%d",
            ::WideToUtf8(ObjectRefTypeName(type)).c_str(),
            static_cast<int>(helper.m_nRefCount.load(std::memory_order_relaxed)),
            helper.m_pAddCaller != nullptr ? helper.m_pAddCaller : "(unknown)", helper.m_nAddLine,
            helper.m_pReleaseCaller != nullptr ? helper.m_pReleaseCaller : "(unknown)", helper.m_nReleaseLine
        );
        helper.m_pReleaseCaller = callerFile;
        helper.m_nReleaseLine = callerLine;
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kNpcSource, 0x39);
    } else {
        helper.m_nReleaseLine = callerLine;
        helper.m_pReleaseCaller = callerFile;
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            DestroyReleasedObject();
        } else {
            runtime::process::ReportDeletingInTimer("CNPC", callerFile, callerLine);
            ReportOrt(kNpcSource, 0x39);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CNPC));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CNPC", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kNpcSource, 0x39);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CNPC::ReportOrt(const char* const callerFile, const int callerLine) {
    L2_GUARD_FUNCTION();

    static thread_local int totalCall = 0;
    const int threadId = CThreadManager::GetThreadID();
    int sequence = 1;
    ++totalCall;
    if (callerFile == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin "
        //     "---------------------------",
        //     threadId,
        //     totalCall,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin "
        //     "---------------------------",
        //     threadId,
        //     totalCall,
        //     sequence)
        L2_LOG_ERROR(Log, "NPC reference report begin: thread=%d, report=%d, entry=%d", threadId, totalCall, sequence);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     L"file[%S], line[%d] -------------",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // L2_LOG_ERROR(Log,
        //     L"[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     L"file[%S], line[%d] -------------",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     callerFile,
        //     callerLine)
        L2_LOG_ERROR(
            Log, "NPC reference report begin: thread=%d, report=%d, entry=%d, caller=%s:%d", threadId, totalCall, sequence,
            callerFile, callerLine
        );
    }

    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        if (helper.m_pAddCaller == nullptr || helper.m_pReleaseCaller == nullptr) {
            continue;
        }
        const std::int32_t referenceCount = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (referenceCount == 0) {
            continue;
        }

        ++sequence;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     referenceCount,
        //     ObjectRefTypeName(
        //         static_cast<ObjectRefType>(index)),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     L"[ORT Report][tid:%d][tick:%d][%d] "
        //     L"[RefCount:%d]-[%s]  last_add[%S][%d] "
        //     L"last_release[%S][%d]",
        //     threadId,
        //     totalCall,
        //     sequence,
        //     referenceCount,
        //     ObjectRefTypeName(
        //         static_cast<ObjectRefType>(index)),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "NPC reference report: thread=%d, report=%d, entry=%d, type=%s, count=%d, last add=%s:%d, last release=%s:%d",
            threadId, totalCall, sequence, ::WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index))).c_str(),
            static_cast<int>(referenceCount), helper.m_pAddCaller, helper.m_nAddLine, helper.m_pReleaseCaller,
            helper.m_nReleaseLine
        );
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end "
    //     "-----------------------------",
    //     threadId,
    //     totalCall,
    //     sequence + 1)
    // L2_LOG_ERROR(Log,
    //     "[ORT Report][tid:%d][tick:%d][%d] end "
    //     "-----------------------------",
    //     threadId,
    //     totalCall,
    //     sequence + 1)
    L2_LOG_ERROR(Log, "NPC reference report end: thread=%d, report=%d, entry=%d", threadId, totalCall, sequence + 1);
}

void CNPC::TimerExpired(int id) {
    // Current L2Server 0x0088b768. This heartbeat is also the sole live
    // driver for the race manager. Keep the owner calls and their cadence
    // intact instead of folding the work into parser publication.
    L2_GUARD_FUNCTION();
    if (m_bDeleting || m_Deleted != 0) {
        return;
    }

    CCreature::TimerExpired(id);
    L2_DECLARE_AUTO_TIMER(timer, JOB_CNPC_TIMEREXPIRED);

    // Current 0x0088b85a reads CSharedCreatureData +0x94 (m_nClass),
    // not the prototype class ID at +0x358c. The monrace manager's class
    // category is 0x55 even though its prototype class ID is 1030995.
    if (static_cast<int>(m_data->m_nClass) == 0x55 && g_MonRaceDB.CheckMonRaceKeeper(static_cast<int>(m_nId), GetIndex())) {
        g_MonRaceDB.TimerCalled();
    }

    if (m_data->m_Race == RT_MERCENARY) {
        if (m_nResidenceGuardTick % 10U == 1U && m_data->m_nResidenceID != 0) {
            CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
            const bool ownerChanged = residence == nullptr || residence->m_data == nullptr ||
                residence->m_data->m_nResidenceOwnerID != static_cast<std::uint32_t>(m_data->m_nResidenceOwnerID);
            const ResidenceState state =
                residence != nullptr && residence->m_data != nullptr ? residence->m_data->m_ResidenceState : RS_MAX;
            if (ownerChanged || state == RS_TYPE_CHANGE) {
                Despawn(false, true);
                return;
            }
        }
        if (m_nResidenceGuardTick % 100U == 1U && m_nMercenaryKey != 0) {
            db.RequestUpdateMercenary(
                static_cast<int>(m_nMercenaryKey), m_data->m_Pos, m_data->GetDir(), m_data->m_dCollisionRadius,
                m_data->m_dCollisionHeight
            );
        }
        ++m_nResidenceGuardTick;
    } else if (m_data->m_Race == RT_CASTLE_GUARD) {
        if (m_nResidenceGuardTick % 30U == 1U && m_data->m_nResidenceID != 0) {
            CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
            if (residence != nullptr && (residence->IsCastle() || residence->IsUnderSiege())) {
                const ResidenceState state = residence->m_data != nullptr ? residence->m_data->m_ResidenceState : RS_MAX;
                if (state == RS_ANNOUNCING || state == RS_TYPE_CHANGE) {
                    Despawn(false, true);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"castle[%d] guard npc[%d] is not despawned "
                    //     L"after castle siege...",
                    //     m_data->m_nResidenceID,
                    //     GetClassId())
                    // L2_LOG_ERROR(Log,
                    //     L"castle[%d] guard npc[%d] is not despawned "
                    //     L"after castle siege...",
                    //     m_data->m_nResidenceID,
                    //     GetClassId())
                    L2_LOG_WARN(
                        Log, "Requested despawn of castle %d guard NPC class %d after siege state changed.",
                        m_data->m_nResidenceID, GetClassId()
                    );
                    return;
                }
            }
        }
        ++m_nResidenceGuardTick;
    }

    if (m_data->m_bTeleporting &&
        (!m_bMoving ||
            // Shipping L2Server used the reversed expression below, so the
            // 60-second recovery never fired while time advanced normally:
            // m_nReturnedTime - runtime::crt::Time32Now() > 60
            runtime::crt::Time32Now() - m_nReturnedTime > 60)) {
        m_data->m_bTeleporting = false;
        m_SkillMod.m_bCanGetDamageHP = true;
        m_SkillMod.m_bCanGetDamageMP = true;
    }

    if (m_nPledgeContributeSaveTick != 0) {
        if (m_nPledgeContributeSaveTick % 50U == 0U) {
            {
                L2_GUARD(m_PledgeContributeSect);
                const int residenceId = m_data->m_nRulingAgit;
                db.RequestSavePledgeContribution(
                    residenceId, g_ResidenceDB.IsSiegeAgit(residenceId), false, &m_PledgeContributes
                );
                m_PledgeContributes.clear();
            }
        }
        ++m_nPledgeContributeSaveTick;
    }

    if (m_isEnterWorldPacket == 0) {
        const bool parametersChanged = m_bParamChanged.load(std::memory_order_seq_cst) != 0;
        if (!parametersChanged) {
            if (m_bSpeedChanged) {
                m_bSpeedChanged = false;
                if (!m_isDroidNPC) {
                    SendNPCInfo(nullptr, false, true);
                } else {
                    BroadcastCharInfoUpdate();
                }
            }
        } else {
            m_bParamChanged.exchange(0, std::memory_order_seq_cst);
            m_nParamChanged = 0;
            m_bSpeedChanged = false;
            if (!m_isDroidNPC) {
                SendNPCInfo(nullptr, false, false);
            } else {
                BroadcastCharInfoUpdate();
            }
        }
    }

    if (m_nFakeNpcSuicideTime > 0 && m_nFakeNpcSuicideTime < runtime::crt::Time32Now() - m_nEnterWorldTime) {
        Suicide(DAMAGE_SUICIDE, 5);
    }
}

void CNPC::Delete() {
    L2_GUARD_FUNCTION();

    if (runtime::raw_atomic::IncrementI32(&m_nNpcDeleteCount) > 1) {
        return;
    }

    if (m_eNpcTypeForDeco == NpcTypeForDeco::DecoNpcInRealWorld) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_nAgitCIDorPledgeDBID);
        if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[DECONPC]Agit is not exist at %s : %d",
            //     L"CNPC::Delete",
            //     m_nAgitCIDorPledgeDBID)
            // L2_LOG_ERROR(Log,
            //     L"[DECONPC]Agit is not exist at %s : %d",
            //     L"CNPC::Delete",
            //     m_nAgitCIDorPledgeDBID)
            L2_LOG_ERROR(
                Log, "Cannot reset decoration NPC slot in residence %d: residence is missing; NPC deletion continues.",
                m_nAgitCIDorPledgeDBID
            );
        } else {
            residence->ResetDecoNpc(m_nAgitSlot);
        }
        m_nAgitSlot = 0;
        m_eNpcTypeForDeco = NpcTypeForDeco::NotDecoNpc;
        m_nAgitCIDorPledgeDBID = 0;
    } else if (m_eNpcTypeForDeco == NpcTypeForDeco::DecoNpcInInstantAgit) {
        Singleton<CInstantAgitManager>::inst().ResetDecoNpc(m_nAgitCIDorPledgeDBID, m_nAgitSlot);
        m_nAgitSlot = 0;
        m_eNpcTypeForDeco = NpcTypeForDeco::NotDecoNpc;
        m_nAgitCIDorPledgeDBID = 0;
    }

    m_bDeleting = true;
    const wchar_t* const caller = runtime::process::CurrentGuardCaller();
    static_cast<void>(new CNPCReleaseVerifier(this, caller != nullptr ? caller : L"callStackError"));

    runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_Delete);
    if (m_data->m_BossID > 0) {
        runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_DeletePrivate);
        if (User* const summoner = GetSummoner()) {
            summoner->DeleteSummonedNpc(static_cast<int>(Id()));
        }
    }

    // Current L2Server CNPC::Delete at 0x00892da4 calls vslot
    // +0x188 (IsSummonOrPet), then vslot +0x1f8 (CastSummon). The
    // recovered IsSummon-only condition suppressed S_PET_DELETE for CPet,
    // whose IsSummon override is false.
    // if (IsSummon()) {
    if (IsSummonOrPet()) {
        CastSummon()->SendSummonDelete();
        if (User* const master = GetMaster()) {
            if (CParty* const party = master->GetParty()) {
                party->ClosePetWindowPacket(CastSummon());
            }
        }
    }

    if (m_data->m_bUnique || IsBoss()) {
        g_UniqNPCDB.DeleteUniqNPC(this);
    }
    if (m_data->m_bIsActive) {
        LeaveWorld(true);
    }

    const std::uint32_t inZoneId = m_data->m_nInstantZoneID;
    g_npcserver.Send("cdd", 0x18, static_cast<int>(Id()), inZoneId);
    if (runtime::raw_atomic::DecrementI32(&g_nNPCCount) < 0) {
        runtime::raw_atomic::ExchangeI32(&g_nNPCCount, 0);
    }

    CCreature::Delete();
    if (!IsRootNPC()) {
        g_NPCMap.DeleteObject(static_cast<int>(Id()), const_cast<char*>(kNpcSource), 0x254);
    }
    L2_RELEASE(this, ObjectRefType::CreateDelete, (inZoneId & UINT32_C(0xffffff00)) != 0);
}

void CNPC::OnCreateFakeNPC() {
    L2_GUARD_FUNCTION();
    m_NPCInfoPacket.Init(2);
    m_isEnterWorldPacket = 1;
    g_ServerContext.Npc.fetch_add(1, std::memory_order_seq_cst);
    CCreature::OnCreateFakeNPC();
    g_NPCMap.Insert(static_cast<int>(GetSmartID().Sid()), this, const_cast<char*>(kNpcSource), 0x166);
}

void CNPC::OnCreate() {
    L2_GUARD_FUNCTION();
    g_ServerContext.Npc.fetch_add(1, std::memory_order_seq_cst);
    CCreature::OnCreate();
    g_NPCMap.Insert(static_cast<int>(GetSmartID().Sid()), this, const_cast<char*>(kNpcSource), 0x174);
}

template <>
CSafePointer2<CCreature>& CSafePointer2<CCreature>::operator=(const CCreature* object) {
    L2_GUARD_FUNCTION();
    id = static_cast<int>(object->m_nId);
    index = object->GetIndex();
    return *this;
}

bool CNPC::IsNPC() { return true; }

CNPC* CNPC::CastNPC() { return this; }

bool CNPC::IsPCOrPCRelatedNPC() const { return m_bHasSummoner; }

int CNPC::GetClassId() const { return m_nFakeClassId == -1 ? m_nClassId : m_nFakeClassId; }

int CNPC::GetNPCId() { return GetClassId(); }

QuestNpcEventController* CNPC::GetEventController(const QuestNpcEventType eventType, const bool create) {
    // Current L2Server.exe 0x0088f740.
    L2_GUARD_FUNCTION();
    for (QuestNpcEventController* const controller : m_vEventController) {
        if (controller != nullptr && controller->m_eventType == eventType) {
            return controller;
        }
    }

    QuestNpcEventController* controller = nullptr;
    if (create) {
        controller = MakeEventController(eventType);
        // The current image appends the factory result even when it is null.
        m_vEventController.push_back(controller);
    }
    return controller;
}

FVector CNPC::ChoiceValidPosition(const User* user, CSkillInfo* skillInfo) {
    // Current L2Server 0x008827b8.
    L2_GUARD_FUNCTION();

    const FVector source =
        skillInfo != nullptr && skillInfo->m_TargetType == STGT_GROUND ? user->m_NonTargetSkillPos : user->m_data->m_Pos;
    FVector destination{};
    CWorld::Inst().GetValidPositionAroundPos(*user, 40, m_data->m_dCollisionRadius, source, destination);
    return destination;
}

bool CNPC::CanGetNpcEvent() { return !m_data->m_bTeleporting && !IsFakeNPC(); }

void CNPC::SetDecoNpcInfo(NpcTypeForDeco::Type type, int agitCidOrPledgeDbId, int agitSlot) {
    m_eNpcTypeForDeco = type;
    m_nAgitCIDorPledgeDBID = agitCidOrPledgeDbId;
    m_nAgitSlot = agitSlot;
}

bool CNPC::IsSummonedNpc() { return m_bIsSummonedNpc; }

bool CNPC::IsSummonedFriendNpc() { return m_bIsSummonedFriendNpc; }

bool CNPC::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    return IsEnemyToAsNpc(creature, false);
}

bool CNPC::IsEnemyToAsNpc(CCreature* creature, bool forceEnemy) {
    L2_GUARD_FUNCTION();

    if (creature == nullptr) {
        return false;
    }

    if (creature->IsNPC()) {
        if (creature->CastNPC()->HasSummoner()) {
            if (User* const summoner = creature->CastNPC()->GetSummoner()) {
                creature = summoner->CastCreature();
                if (creature == nullptr || !creature->IsPC()) {
                    return false;
                }
            }
        }

        if (creature->IsSummonOrPet()) {
            CSummon* const summon = creature->CastSummon();
            creature = summon != nullptr ? summon->GetMaster() : nullptr;
        }
    }

    if (creature == nullptr) {
        return false;
    }

    if (creature->IsNPC()) {
        if (IsFriendlyNPC() && creature->CastNPC()->IsFriendlyNPC()) {
            return false;
        }
        if (!IsFriendlyNPC() && creature->CastNPC()->IsFriendlyNPC()) {
            return true;
        }
        if (IsFriendlyNPC() && !creature->CastNPC()->IsFriendlyNPC()) {
            return true;
        }
    }

    if (m_data->m_Race == RT_FRIEND && creature->IsPC()) {
        return false;
    }

    if (!IsCastleRelatedNPC()) {
        const std::uint32_t pledgeId = m_data->m_nPledgeID;
        if (!m_data->m_bIsNPCRelatedToPledge) {
            if (pledgeId != 0 && pledgeId == creature->m_data->m_nPledgeID) {
                return false;
            }

            if (IsSummonedNpc()) {
                User* const master = GetMaster();
                if (master != nullptr && master->m_PledgeGameUser.IsPlaying()) {
                    if (creature->IsSummonOrPet()) {
                        CSummon* const summon = creature->CastSummon();
                        creature = summon != nullptr ? summon->GetMaster() : nullptr;
                    }
                    return creature != nullptr && creature->CastUser() != master;
                }
            }

            return forceEnemy || IsRealEnemyTo(creature);
        }

        return pledgeId == 0 || pledgeId != creature->m_data->m_nPledgeID;
    }

    if (creature->IsSummonOrPet()) {
        CSummon* const summon = creature->CastSummon();
        creature = summon != nullptr ? summon->GetMaster() : nullptr;
    }
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    const int residenceId = m_data->m_nResidenceID;
    if (creature->CastUser()->m_data->m_nDominionSiegeId != 0 &&
        residenceId == creature->CastUser()->m_data->m_nDominionSiegeId) {
        return false;
    }

    CPledge* const pledge = creature->CastUser()->GetPledge();
    if (pledge == nullptr) {
        return true;
    }

    bool sameResidence = false;
    const std::uint32_t castleId = pledge->GetCastleDbId();
    if (castleId != 0) {
        sameResidence = castleId == static_cast<std::uint32_t>(residenceId);
    } else if (pledge->SharedData()->m_nFortressUnderOffenceSiegeId == static_cast<std::uint32_t>(residenceId) ||
        pledge->SharedData()->m_nFortressUnderDefenceSiegeId == static_cast<std::uint32_t>(residenceId)) {
        sameResidence = true;
    }
    if (sameResidence && m_data->m_nClass != 0x7a) {
        return false;
    }

    int siegeStatus = 0;
    if (residenceId > 0 && pledge->GetCurrentCastleSiegeDbId(residenceId) == residenceId) {
        siegeStatus = pledge->GetCurrentCastleSiegeStatus(residenceId);
    } else if (pledge->SharedData()->m_nFortressUnderOffenceSiegeId == static_cast<std::uint32_t>(residenceId)) {
        siegeStatus = 1;
    } else if (pledge->SharedData()->m_nFortressUnderDefenceSiegeId == static_cast<std::uint32_t>(residenceId)) {
        siegeStatus = 2;
    }

    if (m_data->m_nClass == 0x7a) {
        if (siegeStatus == 1) {
            return false;
        }
    } else if (siegeStatus == 2) {
        return false;
    }

    return forceEnemy || m_data->m_bCanBeAttacked;
}

bool CNPC::IsRealEnemyTo(CCreature* const creature) {
    if (creature->IsPC()) {
        if (IsSummonOrPet()) {
            User* const user = creature->CastUser();
            User* const master = GetMaster();
            if (master == user) {
                return false;
            }
        }
    } else {
        if (creature->IsSummonOrPet() || creature->IsPCTrap() || creature->IsSummonedNpc()) {
            return m_data->m_bCanBeAttacked;
        }
        if (creature->IsNPC() && m_data->m_nResidenceID == creature->m_data->m_nResidenceID) {
            return false;
        }
    }
    return CCreature::IsRealEnemyTo(creature);
}

bool CNPC::IsMakeAttackerGuilty(CCreature* const attacker, const bool flag) {
    if (!flag) {
        return false;
    }
    const int race = static_cast<int>(m_data->m_Race);
    if (race == 0x19 || race == 0x1a) {
        return false;
    }
    if (m_data->m_nClassID == 0x57 || m_data->m_nClassID == 0x7a) {
        return false;
    }
    return IsEnemyTo(attacker);
}

void CNPC::SetName(const wchar_t* name) {
    L2_GUARD_FUNCTION();
    const std::u16string targetName = target_utf16::FromRecoveredWindowsWide(name);
    m_data->SetName(targetName.c_str());
}

bool CNPC::NeedToHideInfoAboutMaster() {
    L2_GUARD_FUNCTION();
    if (!IsSummonOrPet() && !IsSummonedNpc()) {
        return false;
    }
    if (GetMaster() == nullptr) {
        return false;
    }
    return GetMaster()->m_PledgeGameUser.IsPlaying();
}

double CNPC::EquipedArmorsValidate(VALIDATE::Enum type) const {
    L2_GUARD_FUNCTION();
    return static_cast<double>(m_data->m_nBaseParam[type]);
}

double CNPC::RegenBonusByMotionEx() {
    L2_GUARD_FUNCTION();
    if (m_data->m_MovingStatus != MSTE_MOVING) {
        return 1.0;
    }
    const int moveType = static_cast<int>(m_data->m_MoveMode);
    const int environment = static_cast<int>(m_data->m_Environment);
    return static_cast<double>(runtime::AddInt32Wrapping(m_pClassConst->m_RegenByME[moveType][environment], 100)) * 0.01;
}

double CNPC::HPRegenBonusByMotionEx() {
    L2_GUARD_FUNCTION();
    return RegenBonusByMotionEx();
}

double CNPC::MPRegenBonusByMotionEx() {
    L2_GUARD_FUNCTION();
    return RegenBonusByMotionEx();
}

double CNPC::CPRegenBonusByMotionEx() {
    L2_GUARD_FUNCTION();
    return RegenBonusByMotionEx();
}

bool CNPC::ValidateOrgHP() {
    L2_GUARD_FUNCTION();
    ValidateBaseHP();
    return true;
}

bool CNPC::ValidateOrgMP() {
    L2_GUARD_FUNCTION();
    CCreature::ValidateBaseMP();
    return true;
}

bool CNPC::ValidateOrgCP() { return true; }

bool CNPC::ValidateOrgHPRegen() {
    L2_GUARD_FUNCTION();
    ValidateHPRegen();
    return true;
}

bool CNPC::ValidateOrgMPRegen() {
    L2_GUARD_FUNCTION();
    ValidateMPRegen();
    return true;
}

bool CNPC::ValidateOrgCPRegen() { return true; }

bool CNPC::ValidateOrgPAttack() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PATTACK));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PATTACK));
    m_data->m_nOrgParam[VALIDATE::PATTACK] = runtime::AddInt32Wrapping(armorValue, weaponValue);
    ValidatePAttack();
    return true;
}

bool CNPC::ValidateOrgPDefend() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::PDEFEND));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::PDEFEND));
    m_data->m_nOrgParam[VALIDATE::PDEFEND] = runtime::AddInt32Wrapping(armorValue, weaponValue);
    ValidatePDefend();
    return true;
}

bool CNPC::ValidateOrgMAttack() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MATTACK));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MATTACK));
    m_data->m_nOrgParam[VALIDATE::MATTACK] = runtime::AddInt32Wrapping(armorValue, weaponValue);
    ValidateMAttack();
    return true;
}

bool CNPC::ValidateOrgMDefend() {
    L2_GUARD_FUNCTION();
    const std::int32_t armorValue = runtime::Win64CvttDoubleToInt32(EquipedArmorsValidate(VALIDATE::MDEFEND));
    const std::int32_t weaponValue = runtime::Win64CvttDoubleToInt32(EquipedWeaponValidate(VALIDATE::MDEFEND));
    m_data->m_nOrgParam[VALIDATE::MDEFEND] = runtime::AddInt32Wrapping(armorValue, weaponValue);
    ValidateMDefend();
    return true;
}

bool CNPC::Morph(int classId) {
    L2_GUARD_FUNCTION();
    SetMorphID(classId);
    ChangeMorph();
    return false;
}

void CNPC::UnMorph() {
    L2_GUARD_FUNCTION();
    SetMorphID(0);
    ChangeMorph();
    if (m_pClassConst->m_dCollisionHeight[0] > 0.0 && m_data->container_index != -1) {
        CWorld::Inst().MoveTo(this, GetPos(), false);
    }
}

void CNPC::ChangeMorph() {
    L2_GUARD_FUNCTION();
    DoNothing();
    ValidateChanged(VB_MAX);
    ParamChanged(NPCT_CHANGEMORPH);
}

bool CNPC::ValidateOrgSpeed() {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x00881794 stores the selected base speed in both
    // m_nOrgParam[SPEED] (+0x331c) and m_nMoveTypeSpeed (+0x3330). Equipment
    // modifiers later change only m_nOrgParam[SPEED].
    const int moveType = static_cast<int>(m_data->m_MoveMode);
    const int environment = static_cast<int>(m_data->m_Environment);
    const std::int32_t baseSpeed = m_pClassConst->m_SpeedByME[moveType][environment];
    m_data->m_nOrgParam[VALIDATE::SPEED] = baseSpeed;
    m_data->m_nMoveTypeSpeed = baseSpeed;

    const double weaponValue = EquipedWeaponValidate(VALIDATE::SPEED);
    const double armorValue = EquipedArmorsValidate(VALIDATE::SPEED);
    std::int32_t speed =
        runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(armorValue), m_data->m_nOrgParam[VALIDATE::SPEED]);
    speed = runtime::AddInt32Wrapping(speed, runtime::Win64CvttDoubleToInt32(weaponValue));
    m_data->m_nOrgParam[VALIDATE::SPEED] = speed;
    ValidateSpeed();
    return true;
}

bool CNPC::ValidateSpeed() {
    L2_GUARD_FUNCTION();
    double speed = (static_cast<double>(m_data->m_nOrgParam[VALIDATE::SPEED]) * m_dGMSpeed *
                           m_SkillMod.m_Mods[CCreatureSkillMod::Speed].m_Per * m_AreaMod.m_dSpeedModByArea +
                       m_SkillMod.m_Mods[CCreatureSkillMod::Speed].m_Diff) *
        m_dSpeedModByWeight;

    if (IsRidingWyvern()) {
        speed = 200.0;
    }
    const double maximumSpeed = IsBoss() ? 500.0 : 350.0;

    if (static_cast<std::int8_t>(m_FollowData.m_FollowType) > 0 && !IsSummonOrPet()) {
        CCreature* const target = m_spTarget.GetCreature();
        if (target != nullptr) {
            const FVector position = GetPos();
            const FVector targetPosition = target->GetPos();
            const double deltaX = position.x - targetPosition.x;
            const double deltaY = position.y - targetPosition.y;
            const double deltaZ = position.z - targetPosition.z;
            const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

            if (distance < static_cast<double>(runtime::MultiplyInt32Wrapping(m_FollowData.m_FollowDistance, 4))) {
                if (distance < static_cast<double>(runtime::MultiplyInt32Wrapping(m_FollowData.m_FollowDistance, 2))) {
                    m_data->SetMoveMode(static_cast<int>(target->m_data->m_MoveMode));
                    const std::int32_t targetSpeed = target->m_data->m_nOrgParam[VALIDATE::SPEED];
                    m_data->m_nOrgParam[VALIDATE::SPEED] = targetSpeed;
                    m_data->m_nBaseParam[VALIDATE::SPEED] = targetSpeed;
                    speed = target->m_data->m_dParam[VALIDATE::SPEED];
                } else {
                    m_data->SetMoveMode(1);
                    double candidate = static_cast<double>(m_data->m_nOrgParam[VALIDATE::SPEED]) * 2.0;
                    if (250.0 <= candidate) {
                        candidate = 250.0;
                    }
                    m_data->m_nOrgParam[VALIDATE::SPEED] = runtime::Win64CvttDoubleToInt32(candidate);

                    candidate = static_cast<double>(m_data->m_nOrgParam[VALIDATE::SPEED]) * 2.0;
                    if (250.0 <= candidate) {
                        candidate = 250.0;
                    }
                    m_data->m_nBaseParam[VALIDATE::SPEED] = runtime::Win64CvttDoubleToInt32(candidate);

                    candidate = static_cast<double>(m_data->m_nOrgParam[VALIDATE::SPEED]) * 2.0;
                    speed = 250.0 <= candidate ? 250.0 : candidate;
                }
            } else {
                m_data->SetMoveMode(1);
                m_data->m_nOrgParam[VALIDATE::SPEED] = 250;
                m_data->m_nBaseParam[VALIDATE::SPEED] = 250;
                speed = 250.0;
            }
        }
    }

    m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[VALIDATE::SPEED], speed, 0x3b);
    ValidateGradePenalty(VALIDATE::SPEED);

    const double publishedSpeed = m_data->m_dParam[VALIDATE::SPEED];
    if (1.0 <= publishedSpeed) {
        if (maximumSpeed <= publishedSpeed && publishedSpeed != maximumSpeed) {
            m_data->m_dParam[VALIDATE::SPEED] = maximumSpeed;
        }
    } else {
        m_data->m_dParam[VALIDATE::SPEED] = 1.0;
    }
    return true;
}

bool CNPC::ValidateMoveToTargetSpeedAndSendInfo() {
    L2_GUARD_FUNCTION();
    const MoveType moveMode = m_data->m_MoveMode;
    const std::int32_t originalSpeed = m_data->m_nOrgParam[VALIDATE::SPEED];
    const double speed = m_data->m_dParam[VALIDATE::SPEED];
    ValidateOrgSpeed();
    if (moveMode == m_data->m_MoveMode && originalSpeed == m_data->m_nOrgParam[VALIDATE::SPEED] &&
        speed == m_data->m_dParam[VALIDATE::SPEED]) {
        return false;
    }
    SendMyInfo();
    return true;
}

void CNPC::MoveToLocation(int x, int y, int z, PathFindType) {
    L2_GUARD_FUNCTION();
    if (m_data->m_StopMode != STP_SIT) {
        CCreature::MoveToLocation(x, y, z, UsePathFind);
    }
}

bool CNPC::IsFriendlyNPC() {
    L2_GUARD_FUNCTION();
    return IsFriendNPC() || IsSummonedNpc() || IsSummonOrPet();
}

void CNPC::OnTeleport() {
    L2_GUARD_FUNCTION();
    GetController()->DoNothing(true);
    DetachAllChannelling();
}

bool CNPC::TeleportToLocation(
    int x,
    int y,
    int z,
    unsigned int inZoneId,
    int heading,
    TeleportSourceType::Enum source,
    CCreature* requestor,
    int unknown
) {
    L2_GUARD_FUNCTION();

    CGeoData* const geo = CWorld::Inst().GetGeo();
    if (geo->GetZone(NpcTeleportZoneCoordinate(x, 0x50000), NpcTeleportZoneCoordinate(y, 0x40000)) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Try to teleport to the invalid Position[%d %d %d] InZoneID[%d] class id[%d]",
        //     L"NPC.cpp",
        //     0x46f,
        //     x,
        //     y,
        //     z,
        //     static_cast<int>(inZoneId),
        //     GetClassId())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]Try to teleport to the invalid Position[%d %d %d] InZoneID[%d] class id[%d]",
        //     L"NPC.cpp",
        //     0x46f,
        //     x,
        //     y,
        //     z,
        //     static_cast<int>(inZoneId),
        //     GetClassId())
        L2_LOG_ERROR(
            Log, "Cannot teleport NPC class %d to (%d, %d, %d), zone %u: destination has no geodata zone.", GetClassId(), x,
            y, z, inZoneId
        );
        return false;
    }

    CAirShip* const airShip = GetMyAirShip();
    const FVector destination{x, y, z};
    if (airShip != nullptr) {
        return TeleportWithAirShip(airShip, destination, inZoneId);
    }

    OnTeleport();
    if (heading < 0x10000) {
        m_data->SetDirByInt(heading);
    }
    // Current 0x0088a2b6 tests shared-data +0x3bec (m_moveType).
    if (static_cast<std::int32_t>(m_data->m_moveType) == CMT_FLY) {
        if (z < -1000) {
            z = -1000;
        } else if (z > 6000) {
            z = 6000;
        }
    }

    const FVector previousPosition = m_data->GetPos();
    CLogFormatter formatter{};
    formatter.WriteLog2_1(0x1f7c, this, requestor, x, y, z, static_cast<int>(m_nId), inZoneId, static_cast<int>(source), 0);

    if (m_data->m_nClassID == 0xfb953 || m_data->m_nClassID == 0xfba11) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"teleport is something wrong. [%d] can_move[%d] tt[%d] [%d][%d][%d]->[%d][%d][%d]",
        //     m_data->m_nClassID,
        //     m_data->m_bCanMove,
        //     static_cast<int>(source),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.x),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.y),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.z),
        //     x,
        //     y,
        //     z)
        // Noise: class-specific teleport trace has no failure condition.
        // the preceding accounting record keeps the teleport event.
        // L2_LOG_ERROR(Log,
        //     L"teleport is something wrong. [%d] can_move[%d] tt[%d] [%d][%d][%d]->[%d][%d][%d]",
        //     m_data->m_nClassID,
        //     m_data->m_bCanMove,
        //     static_cast<int>(source),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.x),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.y),
        //     runtime::Win64CvttDoubleToInt32(previousPosition.z),
        //     x,
        //     y,
        //     z)
    }

    const FVector adjustedDestination{x, y, z};
    if (inZoneId == GetInZoneID()) {
        g_ptlUserMap->BroadcastToNeighborContainer(
            this, 0x80000, previousPosition, "cddddddd", 0x22, static_cast<int>(m_nId), x, y, z, unknown, heading, 0
        );
        return CWorld::Inst().MoveTo(this, adjustedDestination, true);
    }
    return CWorld::Inst().MoveToZone(this, adjustedDestination, inZoneId);
}

bool CNPC::TeleportWithAirShip(CAirShip* airShip, FVector position, unsigned int inZoneId) {
    L2_GUARD_FUNCTION();

    OnTeleport();
    const FAngle direction = airShip->GetDir();
    const int yaw = direction.Yaw & 0xffff;
    const FVector origin = GetPos();
    const FVector axis{0, 0, 1};
    const FVector rotated = FVector::RotateAngleAxis(&position, yaw, &axis);
    const FVector destination{
        origin.x + rotated.x,
        origin.y + rotated.y,
        origin.z + rotated.z,
    };

    const FVector previousPosition = m_data->GetPos();
    g_ptlUserMap->BroadcastToNeighborContainer(
        this, 0x80000, previousPosition, "cddddddd", 0x22, static_cast<int>(m_nId),
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z), 0, m_data->GetDirByInt() & 0xffff, 0
    );

    if (airShip->GetInZoneID() == GetInZoneID()) {
        return CWorld::Inst().MoveTo(this, destination, true);
    }
    return CWorld::Inst().MoveToZone(this, destination, inZoneId);
}

bool CNPC::IsItemChanceAffectedNPCType(ItemChanceNpcType type) {
    L2_GUARD_FUNCTION();
    if (type == ICN_BOTH) {
        return true;
    }
    if (IsBoss() && type == ICN_BOSS) {
        return true;
    }
    if (!IsBoss() && type == ICN_NORMAL) {
        return true;
    }
    return false;
}

double CNPC::GetPrivateDropCorpseItemChanceProb(User* user) {
    // Current L2Server 0x0088da00.
    L2_GUARD_FUNCTION();
    const int npcLevel = m_data->m_nLevel;
    const int userLevel = user->m_data->m_nLevel;
    double probability = 1.0;

    L2_GUARD(user->m_SkillDurationMod.m_Lock);
    for (const auto& [skillId, itemChanceInfo] : user->m_SkillDurationMod.m_mapItemChanceInfo) {
        static_cast<void>(skillId);
        const ItemChanceNpcType npcType = itemChanceInfo.m_NpcType;
        const std::vector<ItemChanceType> itemChanceTypes = itemChanceInfo.m_vItemChanceTypeList;
        if (!IsItemChanceAffectedNPCType(npcType)) {
            continue;
        }

        for (const ItemChanceType& itemChanceType : itemChanceTypes) {
            bool inLevelRange = false;
            if (itemChanceType.m_LevelType == ICL_ABSOLUTE_LEVEL) {
                inLevelRange = itemChanceType.m_nMinLevel <= npcLevel && npcLevel <= itemChanceType.m_nMaxLevel;
            } else {
                inLevelRange =
                    userLevel + itemChanceType.m_nMinLevel <= npcLevel && npcLevel <= userLevel + itemChanceType.m_nMaxLevel;
            }
            if (inLevelRange) {
                probability += itemChanceType.m_dCorpseProb - 1.0;
            }
        }
    }

    m_dLastCorpseItemChanceProb = probability;
    return probability;
}

double CNPC::GetPrivateDropMultiItemChanceProb(User* user) {
    // Current L2Server 0x0088dc38.
    L2_GUARD_FUNCTION();
    const int npcLevel = m_data->m_nLevel;
    const int userLevel = user->m_data->m_nLevel;
    double probability = 1.0;

    L2_GUARD(user->m_SkillDurationMod.m_Lock);
    for (const auto& [skillId, itemChanceInfo] : user->m_SkillDurationMod.m_mapItemChanceInfo) {
        static_cast<void>(skillId);
        if (!IsItemChanceAffectedNPCType(itemChanceInfo.m_NpcType)) {
            continue;
        }

        const std::vector<ItemChanceType> chanceTypes = itemChanceInfo.m_vItemChanceTypeList;
        for (const ItemChanceType& chanceType : chanceTypes) {
            const bool inLevelRange = chanceType.m_LevelType == ICL_ABSOLUTE_LEVEL
                ? chanceType.m_nMinLevel <= npcLevel && npcLevel <= chanceType.m_nMaxLevel
                : userLevel + chanceType.m_nMinLevel <= npcLevel && npcLevel <= userLevel + chanceType.m_nMaxLevel;
            if (inLevelRange) {
                probability += chanceType.m_dMultiProb - 1.0;
            }
        }
    }
    return probability;
}

double CNPC::GetPrivateDropExItemChanceProb(User* user) {
    // Current L2Server 0x0088de60.
    L2_GUARD_FUNCTION();
    const int npcLevel = m_data->m_nLevel;
    const int userLevel = user->m_data->m_nLevel;
    double probability = 1.0;

    L2_GUARD(user->m_SkillDurationMod.m_Lock);
    for (const auto& [skillId, itemChanceInfo] : user->m_SkillDurationMod.m_mapItemChanceInfo) {
        static_cast<void>(skillId);
        if (!IsItemChanceAffectedNPCType(itemChanceInfo.m_NpcType)) {
            continue;
        }

        const std::vector<ItemChanceType> chanceTypes = itemChanceInfo.m_vItemChanceTypeList;
        for (const ItemChanceType& chanceType : chanceTypes) {
            const bool inLevelRange = chanceType.m_LevelType == ICL_ABSOLUTE_LEVEL
                ? chanceType.m_nMinLevel <= npcLevel && npcLevel <= chanceType.m_nMaxLevel
                : userLevel + chanceType.m_nMinLevel <= npcLevel && npcLevel <= userLevel + chanceType.m_nMaxLevel;
            if (inLevelRange) {
                probability += chanceType.m_dExProb - 1.0;
            }
        }
    }
    return probability;
}

double CNPC::GetDropItemChanceProb(DropItemType type) {
    // Current L2Server 0x0088e088. The selected personal contributor owns
    // the private item-chance state. Party and MPCC paths average over the
    // full kept member count, with a neutral 1.0 for inactive members.
    L2_GUARD_FUNCTION();
    if (m_pContribute == nullptr) {
        return 1.0;
    }

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    User* const user = contributor != nullptr && contributor->IsPC() ? contributor->CastUser() : nullptr;
    if (user == nullptr) {
        return 1.0;
    }

    auto memberChance = [this, type](User* member) -> double {
        if (member == nullptr || !member->m_SkillDurationMod.bItemChanceActivated) {
            return 1.0;
        }
        if (type == DROPITEM_MULTI) {
            return GetPrivateDropMultiItemChanceProb(member);
        }
        if (type == DROPITEM_EX) {
            return GetPrivateDropExItemChanceProb(member);
        }
        return 1.0;
    };

    if (user->m_data->m_nPartyId != 0) {
        CParty* const party = user->GetParty();
        if (party == nullptr) {
            return 1.0;
        }
        const int memberCount = party->GetMemberCount();
        std::vector<User*> members;
        party->GetAllMember(members);
        double total = 0.0;
        for (User* const member : members) {
            total += memberChance(member);
        }
        return total / static_cast<double>(memberCount);
    }

    if (user->IsMPCCMember()) {
        CMultiPartyCommandChannel* const channel = user->GetMPCC();
        if (channel == nullptr) {
            return 1.0;
        }
        const int memberCount = channel->GetMemberCount();
        std::vector<User*> members;
        channel->GetAllMember(members);
        double total = 0.0;
        for (User* const member : members) {
            total += memberChance(member);
        }
        return total / static_cast<double>(memberCount);
    }

    return memberChance(user);
}

double CNPC::GetPrivateDropVitalityProb() {
    // Current L2Server 0x0088af24.
    L2_GUARD_FUNCTION();
    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    User* const user = contributor != nullptr && contributor->IsPC() ? contributor->CastUser() : nullptr;
    if (user == nullptr) {
        return 1.0;
    }

    if (user->m_data->m_nPartyId == 0) {
        if (!user->IsMPCCMember()) {
            return user->GetVitalityItemDropBonus();
        }
        CMultiPartyCommandChannel* const channel = user->GetMPCC();
        if (channel == nullptr) {
            return 1.0;
        }
        const int memberCount = channel->GetMemberCount();
        std::vector<User*> members;
        channel->GetAllMember(members);
        double probability = 0.0;
        for (User* const member : members) {
            probability += member->GetVitalityItemDropBonus();
        }
        return probability / static_cast<double>(memberCount);
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return 1.0;
    }
    const int memberCount = party->GetMemberCount();
    std::vector<User*> members;
    party->GetAllMember(members);
    double probability = 0.0;
    for (User* const member : members) {
        probability += member->GetVitalityItemDropBonus();
    }
    return probability / static_cast<double>(memberCount);
}

double CNPC::GetItemDropPenaltyByLevelDiff() {
    // Current L2Server 0x0087ffc4.
    L2_GUARD_FUNCTION();
    const int contributorLevel = m_pContribute->m_PersonalGreatContributorLevel;
    const int levelDifference = std::max(runtime::SubtractInt32Wrapping(contributorLevel, m_data->m_nLevel), 0);
    if (levelDifference >= 10) {
        return 0.0;
    }
    if (contributorLevel < 85) {
        return 1.0;
    }
    return ArrayLevelPenaltyModifier[levelDifference];
}

void CNPC::DropExItemToInventory(const double probability) {
    // Current L2Server 0x0088e380.
    L2_GUARD_FUNCTION();
    const double itemChance = GetDropItemChanceProb(DROPITEM_EX);
    m_dLastExItemChanceProb = itemChance;

    for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ExItemDropListMulti) {
        const int probabilityDice = std::rand() % 100;
        const double pocketDice = ::genrand64_real1() * 100.0 + 0.0;
        double finalProbability = dropGroup.DropProb;
        if (dropGroup.m_bChanceAffected) {
            finalProbability = ClampNpcDropProbability(finalProbability * itemChance);
        }
        if (!(static_cast<double>(probabilityDice) <= probability) || !(pocketDice < finalProbability)) {
            continue;
        }

        const double itemDice = ::genrand64_real1() * 100.0 + 0.0;
        double accumulatedProbability = 0.0;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            accumulatedProbability += item->DropProb;
            if (!(itemDice <= accumulatedProbability)) {
                continue;
            }
            const int amount = runtime::Win64CvttDoubleToInt32(
                ::genrand64_real1() * (static_cast<double>(item->DropMax) - static_cast<double>(item->DropMin)) +
                static_cast<double>(item->DropMin) + 0.5
            );
            if (amount > 0) {
                AddItemToInventory(
                    item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
            }
            break;
        }
    }
}

void CNPC::DropVitalityItemToInventory(const double probability) {
    // Current L2Server 0x00895adc.
    L2_GUARD_FUNCTION();
    const bool isSown = m_data->m_bIsSow;
    const bool isSeed2 = m_data->m_bIsSeed2;
    const double premiumVitalityDropBonus = m_pContribute->GetContributesPremiumVitalItemDropBonus();
    const double runeDropRate = static_cast<double>(m_pContribute->GetContributesDropRateByRune(0)) / 100.0;
    const double runeDropAmount = static_cast<double>(m_pContribute->GetContributesDropAmountByRune(0)) / 100.0;

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    User* const contributorUser = contributor != nullptr && contributor->IsPC() ? contributor->CastUser() : nullptr;
    double eventAdenaRate = 1.0;
    double eventItemRate = 1.0;
    if (contributorUser != nullptr) {
        std::vector<CEvent*> events;
        Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
        for (CEvent* const event : events) {
            if (event == nullptr || !event->InEventTime()) {
                continue;
            }
            auto* const economyEvent = static_cast<CEvent_EconomyControlEvent*>(event);
            eventAdenaRate = economyEvent->GetAdenaDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            eventItemRate = economyEvent->GetItemDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            break;
        }
    }

    const double itemChance = GetDropItemChanceProb(DROPITEM_MULTI);
    m_dLastMultiItemChanceProb = itemChance;
    const double privateVitalityProbability = GetPrivateDropVitalityProb();

    {
        GuardHelper dropListGuard(L"DropItemToInventory1", CThreadManager::GetThreadID());
        for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ItemVitalityDropListMulti) {
            const int probabilityDice = std::rand() % 100;
            const double pocketDice = ::genrand64_real1() * 100.0 + 0.0;
            bool containsAdena = false;
            {
                GuardHelper adenaScanGuard(L"DropItemToInventory2", CThreadManager::GetThreadID());
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    if (item->ItemClassId == 0x39) {
                        containsAdena = true;
                        break;
                    }
                }
            }

            {
                GuardHelper itemDropGuard(L"DropItemToInventory3", CThreadManager::GetThreadID());
                const double originalProbability = dropGroup.DropProb;
                double finalProbability = originalProbability;
                if (!containsAdena) {
                    finalProbability = originalProbability * runeDropRate + originalProbability * premiumVitalityDropBonus;
                }
                if (dropGroup.m_bChanceAffected) {
                    finalProbability *= itemChance;
                }
                finalProbability =
                    ClampNpcDropProbability(finalProbability * g_dItemDropRatePer * eventItemRate / g_dVitalItemDropPenalty);
                finalProbability = ClampNpcDropProbability(finalProbability * privateVitalityProbability);
                if (!(static_cast<double>(probabilityDice) <= probability) || !(pocketDice < finalProbability)) {
                    continue;
                }

                const double itemDice = ::genrand64_real1() * 100.0 + 0.0;
                double accumulatedProbability = 0.0;
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    const bool currency = item->ItemClassId == 0x39 || item->ItemClassId == 0x18d8 ||
                        item->ItemClassId == 0x18d9 || item->ItemClassId == 0x18da;
                    accumulatedProbability += item->DropProb;
                    if (!isSown && !isSeed2 && !currency) {
                        continue;
                    }
                    if (!(itemDice <= accumulatedProbability)) {
                        continue;
                    }

                    int amount = runtime::Win64CvttDoubleToInt32(
                        ::genrand64_real1() * (static_cast<double>(item->DropMax) - static_cast<double>(item->DropMin)) +
                        static_cast<double>(item->DropMin) + 0.5
                    );
                    if (currency) {
                        const int scaled = runtime::Win64CvttDoubleToInt32(
                            static_cast<double>(amount) * g_dAdenaAmountPer * runeDropAmount * eventAdenaRate
                        );
                        amount = runtime::Win64CvttDoubleToInt32(
                            static_cast<double>(scaled) * premiumVitalityDropBonus + static_cast<double>(scaled)
                        );
                    }
                    if (amount > 0) {
                        AddItemToInventory(
                            item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                        );
                    }
                    break;
                }
            }
        }
    }
}

void CNPC::BeforeDropItemOnDying(int levelDifference) {
    // Current L2Server 0x008961e4.
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_data->m_nClass) == 0x51) {
        BeforeBossDropItemOnDying(levelDifference);
        return;
    }

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    if (contributor != nullptr) {
        CParty* const party = contributor->GetParty();
        if (party != nullptr) {
            int minimumLevel = 0;
            if (CMultiPartyCommandChannel* const channel = party->GetMPCC()) {
                minimumLevel = channel->GetMinLevelOfAllMember(true, 0xaf0, GetPos(), true, static_cast<int>(GetInZoneID()));
            } else {
                minimumLevel = party->GetMinLevelOfAllMember(true, 0xaf0, GetPos(), true, static_cast<int>(GetInZoneID()));
            }
            const int partyLevelDifference = runtime::SubtractInt32Wrapping(m_data->m_nLevel, minimumLevel);
            if (partyLevelDifference > g_nItemDropLevel2) {
                levelDifference = partyLevelDifference;
            }
        }
    }

    double dropProbability = 100.0;
    if (levelDifference > g_nItemDropLevel2 || levelDifference >= runtime::NegateInt32Wrapping(g_nItemDropLevel1)) {
        dropProbability = 100.0;
    } else if (levelDifference < runtime::NegateInt32Wrapping(g_nItemDropLevel2)) {
        dropProbability = 10.0;
    } else {
        dropProbability = static_cast<double>(levelDifference) * m_dItemDropValue1 + m_dItemDropValue2;
    }

    if (g_nChinaFatigueSystemType > 1) {
        CCreature* const personalContributor =
            g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
        User* const user =
            personalContributor != nullptr && personalContributor->IsPC() ? personalContributor->CastUser() : nullptr;
        if (user != nullptr && user->GetParty() == nullptr) {
            if (user->GetSocket()->IsFullFatigue_china()) {
                dropProbability = 0.0;
            } else if (user->GetSocket()->IsHalfFatigue_china()) {
                dropProbability *= 0.5;
            }
        }
    }

    dropProbability *= GetItemDropPenaltyByLevelDiff();
    DropItemToInventory(dropProbability);
    DropExItemToInventory(dropProbability);
    DropVitalityItemToInventory(dropProbability);
}

void CNPC::DropItemOnDying(CCreature* const killer) {
    // Current L2Server 0x0089653c.
    L2_GUARD_FUNCTION();

    const int contributorLevel = m_pContribute->m_PersonalGreatContributorLevel;
    const int npcLevel = m_data->m_nLevel;
    Singleton<CEventManager>::inst().CheckAndDropItem(this, killer, m_bAttackedByRange);
    ExDropItemSystem::inst().CheckAndDropItem(this, killer);
    CEventMakerManager::inst().CheckAndDropItem(this, killer, m_bAttackedByRange);
    if (!IsSummonOrPet()) {
        CCursedWeaponMgr::inst().RandomMake(this);
    }
    BeforeDropItemOnDying(runtime::SubtractInt32Wrapping(npcLevel, contributorLevel));

    bool checkPvpBattleSector = false;
    if (IsPet()) {
        checkPvpBattleSector = false;
        if (m_AreaMod.m_bIsInBattleField) {
            checkPvpBattleSector = true;
        } else {
            CPet* const pet = CastPet();
            User* const master = pet != nullptr ? pet->GetMaster() : nullptr;
            if (master != nullptr) {
                CPet* const currentPet = CastPet();
                User* const currentMaster = currentPet != nullptr ? currentPet->GetMaster() : nullptr;
                checkPvpBattleSector = currentMaster != nullptr && currentMaster->m_EventMatchUser.m_byEventMatchTeamId != 0;
            }
        }
    }

    if (checkPvpBattleSector) {
        const FVector zoneYPosition = GetPos();
        const int y = runtime::Win64CvttDoubleToInt32(zoneYPosition.y);
        const FVector zoneXPosition = GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(zoneXPosition.x);
        CGeoZone* const zone =
            CWorld::Inst().GetGeo()->GetZone(NpcTeleportZoneCoordinate(x, 0x50000), NpcTeleportZoneCoordinate(y, 0x40000));
        CGeoSector* sector = nullptr;
        if (zone != nullptr) {
            const FVector sectorYPosition = GetPos();
            const FVector sectorXPosition = GetPos();
            sector = zone->GetSector(
                (runtime::Win64CvttDoubleToInt32(sectorXPosition.x) >> 7) & 0xff,
                (runtime::Win64CvttDoubleToInt32(sectorYPosition.y) >> 7) & 0xff
            );
        }
        const std::uint16_t pvpMask = GeoFieldMask[GEO_FIELD_BATTLE_PVP];
        if (sector != nullptr && (static_cast<std::uint16_t>(sector->m_Field) & pvpMask) == pvpMask) {
            return;
        }
    }

    {
        L2_WRITE_GUARD(m_inv);
        CSharedItemData* itemData = m_inv.GetFirst();
        int depth = 0;
        while (itemData != nullptr) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            CSharedItemData* const next = itemData->Next();
            if (item && item->IsItem()) {
                const int itemClassId = item->m_data->m_nClassID;
                const bool deleteCursedWeapon = CCursedWeaponMgr::inst().FindConst(itemClassId) != nullptr &&
                    NpcTeleportZoneCoordinate(runtime::Win64CvttDoubleToInt32(GetPos().x), 0x50000) <= 0xe;
                if (deleteCursedWeapon) {
                    CCursedWeaponMgr::inst().BuilderCmd_Delete(nullptr, itemClassId);
                } else {
                    {
                        L2_GUARD(*item->m_lock);
                        item->m_ContributeDataID = CSmartID(static_cast<std::uint32_t>(m_pContribute->m_nId));
                        const std::uint32_t now = runtime::GetTickCount();
                        item->m_nDroppedTick = IsBoss() ? now + 300000U : now;
                    }
                    item->AddDropPeriodTimer();

                    FVector dropPosition = m_data->GetPos();
                    dropPosition.z = static_cast<double>(CWorld::Inst().GetGeo()->FindGround(
                        runtime::Win64CvttDoubleToInt32(dropPosition.x),
                        runtime::Win64CvttDoubleToInt32(dropPosition.y),
                        runtime::Win64CvttDoubleToInt32(dropPosition.z)
                    ));
                    const double angle =
                        static_cast<double>(runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 360.0 + 0.0)) * 3.14 /
                        360.0;
                    const double distance =
                        static_cast<double>(runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 16.0 + 30.0));
                    dropPosition.y += static_cast<double>(runtime::Win64CvttDoubleToInt32(std::sin(angle) * distance));
                    dropPosition.x += static_cast<double>(runtime::Win64CvttDoubleToInt32(std::cos(angle) * distance));

                    const bool suppressFakeItemDrop = m_bHadFakeItem && itemClassId == g_nFakeItemClassID &&
                        (static_cast<std::uint32_t>(item->m_data->m_nId) &
                            // 0xf8000000U) != 0x30000000U.
                            CLASS_MASK) != (SCLS_FAKE_ITEM << CLASS_SHIFT);
                    if (!suppressFakeItemDrop) {
                        if (g_bDropAdenaToInventory && itemClassId == 0x39) {
                            DropItemAdena(killer, item->CastItem(), itemData->m_nCount, dropPosition, false);
                        } else {
                            DropItem(item->CastItem(), itemData->m_nCount, dropPosition, false);
                        }
                    }

                    if (IsBoss() && itemClassId != 0x39 && item->m_nClassId != g_nBotCheckItemClassID) {
                        g_ptlUserMap->BroadcastToNeighborDist(
                            this, 0x80000, m_data->GetPos(), 2000, "chccdcdcd", 0x62, 0x4b8, 3, 2, GetClassId(), 3,
                            item->m_nClassId, 1, itemData->m_nCount
                        );
                    }
                }
            }

            ++depth;
            if (depth >= 0xcb) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth > %d at file[%s], line[%d]",
                //     0xca,
                //     "NPC.cpp",
                //     0xbde)
                // L2_LOG_ERROR(Log,
                //     "depth > %d at file[%s], line[%d]",
                //     0xca,
                //     "NPC.cpp",
                //     0xbde)
                L2_LOG_ERROR(
                    Log, "Stopped NPC death-drop inventory traversal at %d entries: traversal limit exceeded.", depth
                );
                break;
            }
            itemData = next;
        }
    }
}

bool CNPC::Die(CCreature* const killer, const int reason) {
    // Current L2Server 0x00896edc.
    L2_GUARD_FUNCTION();
    if (!CCreature::Die(killer, reason)) {
        return false;
    }

    if (killer != nullptr &&
        (killer->IsPC() || killer->IsSummonOrPet() || killer->IsDoppelganger() || killer->IsPCTrap() ||
            killer->IsSummonedNpc())) {
        User* const user = killer->GetUserOrMaster();
        if (user == nullptr) {
            if (m_bLogToError) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[UserQuestData] pUser is NULL NPC(%s) Enemy(%s)",
                //     GetName(),
                //     killer->GetName())
                // L2_LOG_ERROR(Log,
                //     L"[UserQuestData] pUser is NULL NPC(%s) Enemy(%s)",
                //     GetName(),
                //     killer->GetName())
                // L2_LOG_WARN(Log,
                //     "Cannot dispatch quest death event for NPC %s: killer %s has no user or master.",
                //     WideToUtf8(GetName()).c_str(), WideToUtf8(killer->GetName()).c_str())
                L2_LOG_WARN(
                    Log, "Cannot dispatch quest death event for NPC %s: killer %s has no user or master.",
                    Utf16ToUtf8(GetName()).c_str(), Utf16ToUtf8(killer->GetName()).c_str()
                );
            }
        } else {
            QuestNpcEventController* const controller = GetEventController(QUEST_NPC_EVENT_DIE, false);
            if (controller == nullptr) {
                if (m_bLogToError) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[UserQuestData] NPC Die Event Controller Is Not Exists. NPC(%s) User(%s)",
                    //     GetName(),
                    //     user->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[UserQuestData] NPC Die Event Controller Is Not Exists. NPC(%s) User(%s)",
                    //     GetName(),
                    //     user->GetName())
                    // L2_LOG_INFO(Log,
                    //     "No quest death-event controller for NPC %s and user %s.",
                    //     WideToUtf8(GetName()).c_str(), WideToUtf8(user->GetName()).c_str())
                    L2_LOG_INFO(
                        Log, "No quest death-event controller for NPC %s and user %s.", Utf16ToUtf8(GetName()).c_str(),
                        Utf16ToUtf8(user->GetName()).c_str()
                    );
                }
            } else {
                if (m_bLogToError) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[UserQuestData] NPC Die Event Controller Call. NPC(%s) User(%s)",
                    //     GetName(),
                    //     user->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[UserQuestData] NPC Die Event Controller Call. NPC(%s) User(%s)",
                    //     GetName(),
                    //     user->GetName())
                    // L2_LOG_INFO(Log,
                    //     "Calling quest death-event controller for NPC %s and user %s.",
                    //     WideToUtf8(GetName()).c_str(), WideToUtf8(user->GetName()).c_str())
                    L2_LOG_INFO(
                        Log, "Calling quest death-event controller for NPC %s and user %s.", Utf16ToUtf8(GetName()).c_str(),
                        Utf16ToUtf8(user->GetName()).c_str()
                    );
                }
                controller->Do(*user, this, 0, 0);
            }
        }
    }

    User* killerMaster = nullptr;
    if (CCreature* const masterObject = g_CreatureSmartIdManager.GetObjectW(m_sidKillerMaster);
        masterObject != nullptr && masterObject->IsPC()) {
        killerMaster = masterObject->CastUser();
    }
    g_npcserver.Send(
        "cdSd", 5, static_cast<int>(m_nId), killerMaster != nullptr ? killerMaster->GetName() : u"",
        killerMaster != nullptr ? m_nKillerMemberCount : 0
    );

    if (m_data->m_nResidenceID != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
        CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
        if (worldAgit != nullptr) {
            worldAgit->OnBossNpcDead(this);
        }
    }

    if (m_data->m_Race == RT_MERCENARY && m_nMercenaryKey != 0) {
        db.RequestUpdateMercenary(
            static_cast<int>(m_nMercenaryKey), m_data->GetPos(), m_data->GetDir(), m_data->m_dCollisionRadius,
            m_data->m_dCollisionHeight
        );
    }

    if (m_data->m_bIsNPCRelatedToPledge && GetClassId() == 0xf8d85) {
        CSPointer<CPledge> hostPledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_data->m_nPledgeID));
        const FVector position = GetPos();
        if (hostPledge) {
            hostPledge->SendSystemMessageToMembersWithNpcId(SystemMessageId::CLAN_FLAG_DESTROYED, GetClassId());

            User* const killerUser = killer != nullptr ? killer->CastUser() : nullptr;
            const std::u16string hostPledgeName = std::u16string(hostPledge->GetPledgeName());
            const std::int64_t hostPledgeDbId = hostPledge->GetDbId();
            const std::int64_t hostPledgeLevel = hostPledge->GetPledgeLevel();
            CLogFormatter formatter;
            if (killerUser == nullptr) {
                formatter.WriteLog(
                    0x803, 0, 0, 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
                    runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
                    hostPledgeName.c_str(), u"", nullptr, 0, 0, 0, 0, 0, hostPledgeDbId, hostPledgeLevel, 0, 0, 0, nullptr,
                    nullptr, nullptr, nullptr, 0
                );
            } else {
                CPledge* const killerPledge = killerUser->GetPledge();
                std::u16string killerPledgeName;
                std::int64_t killerPledgeDbId = 0;
                std::int64_t killerPledgeLevel = 0;
                if (killerPledge != nullptr) {
                    killerPledgeName = std::u16string(killerPledge->GetPledgeName());
                    killerPledgeDbId = killerPledge->GetDbId();
                    killerPledgeLevel = killerPledge->GetPledgeLevel();
                }
                formatter.WriteLog(
                    0x803, killerUser->GetDBID(), killerUser->GetAccountID(), 0, 0,
                    runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
                    runtime::Win64CvttDoubleToInt32(position.z), hostPledgeName.c_str(),
                    killerPledge != nullptr ? killerPledgeName.c_str() : u"", nullptr,
                    static_cast<std::int32_t>(killerUser->m_data->m_Race),
                    static_cast<std::int32_t>(killerUser->m_data->m_Sex),
                    static_cast<std::int32_t>(killerUser->m_data->m_nClass), killerUser->m_data->m_nLevel,
                    killerUser->GetSocialClass(), hostPledgeDbId, hostPledgeLevel, killerPledgeDbId, killerPledgeLevel, 0,
                    nullptr, nullptr, nullptr, nullptr, 0
                );
            }
        }
    }

    if (static_cast<std::int32_t>(m_data->m_nClass) == 0x51) {
        RelayClient::Inst()->SendDieBossNPCPacket(GetClassId());
    }
    if (m_bIsGMKill) {
        return false;
    }
    if (runtime::raw_atomic::IncrementI32(&m_nDead) > 1) {
        return false;
    }

    SendClanDiedEvent(this);
    if (m_pContribute->m_PersonalGreatContributorLevel == 0) {
        return true;
    }

    DropItemOnDying(killer);
    ResetAllTacticalSignToMe();
    if (IsBoss()) {
        const FVector position = m_data->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(this, 0x80000, position, 2000, "chd", 0x62, 0x4b9, 0);
        g_UniqNPCDB.DeleteUniqNPC(this);

        std::uint32_t elapsed = 0;
        if (m_dwDamagedTick != 0) {
            elapsed = (runtime::GetTickCount() - m_dwDamagedTick) / 1000U;
        }
        CCreature* actor = nullptr;
        std::int64_t actorFlag = 0;
        bool writeBossLog = true;
        if (killer != nullptr && (killer->IsPC() || killer->IsSummonOrPet())) {
            actorFlag = 1;
            actor = killer->GetUserOrMaster();
            writeBossLog = actor != nullptr;
        }
        if (writeBossLog) {
            CLogFormatter formatter;
            formatter.WriteLog1_1(
                0x461, actor, this, nullptr, nullptr, nullptr, elapsed, GetClassId(), Level(), m_nKillerMemberCount,
                actorFlag, 0, 0
            );
        }

        std::uint32_t auditElapsed = 0;
        if (m_dwDamagedTick != 0) {
            auditElapsed = (runtime::GetTickCount() - m_dwDamagedTick) / 1000U;
        }
        // Native printf requires a long long argument for every %I64d field.
#if defined(_WIN32)
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,,,,,%I64d,%I64d,,,,",
            static_cast<std::int64_t>(0x58f), static_cast<std::int64_t>(Id()), static_cast<std::int64_t>(m_nClassId),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<std::int64_t>(m_nClassId),
            static_cast<std::int64_t>(Level()), static_cast<std::int64_t>(auditElapsed), static_cast<std::int64_t>(Id()),
            static_cast<std::int64_t>(m_nClassId)
        );
#else
        // Log.AddT(
        //     LOG_AUDIT,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,,,,,%I64d,%I64d,,,,",
        //     static_cast<std::int64_t>(0x58f),
        //     static_cast<std::int64_t>(Id()),
        //     static_cast<std::int64_t>(m_nClassId),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(
        //             position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(
        //             position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(
        //             position.z)),
        //     static_cast<std::int64_t>(m_nClassId),
        //     static_cast<std::int64_t>(Level()),
        //     static_cast<std::int64_t>(auditElapsed),
        //     static_cast<std::int64_t>(Id()),
        //     static_cast<std::int64_t>(m_nClassId))
        Log.AddT(
            LOG_AUDIT, u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,%I64d,%I64d,%I64d,,,,,,,,%I64d,%I64d,,,,",
            static_cast<long long>(static_cast<std::int64_t>(0x58f)),
            static_cast<long long>(static_cast<std::int64_t>(Id())),
            static_cast<long long>(static_cast<std::int64_t>(m_nClassId)),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y))),
            static_cast<long long>(static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z))),
            static_cast<long long>(static_cast<std::int64_t>(m_nClassId)),
            static_cast<long long>(static_cast<std::int64_t>(Level())),
            static_cast<long long>(static_cast<std::int64_t>(auditElapsed)),
            static_cast<long long>(static_cast<std::int64_t>(Id())),
            static_cast<long long>(static_cast<std::int64_t>(m_nClassId))
        );
#endif
    }

    LogOnDying(killer);
    if (killer != nullptr && killer->IsPCOrSummon() && KalieEvent::Inst().m_bEventStarted) {
        KalieEvent::Inst().IncreaseKillNpcCnt();
    }
    NpcDieStat::Inst().Accumulate(runtime::SubtractInt32Wrapping(GetClassId(), 1000000));
    return true;
}

void CNPC::BeforeBossDropItemOnDying(int levelDifference) {
    // Current L2Server 0x008946f4.
    L2_GUARD_FUNCTION();

    double dropProbability =
        ClampNpcDropUnit(static_cast<double>(runtime::AddInt32Wrapping(levelDifference, 2)) * 0.15 + 1.0);
    if (levelDifference > g_nItemDropLevel2) {
        dropProbability = 100.0;
    }
    dropProbability *= GetItemDropPenaltyByLevelDiff();

    const double premiumDropBonus = m_pContribute->GetContributesPremiumDropBonus();
    const double premiumVitalityDropBonus = m_pContribute->GetContributesPremiumVitalItemDropBonus();
    const double runeDropRate = static_cast<double>(m_pContribute->GetContributesDropRateByRune(1)) / 100.0;
    const double runeDropAmount = static_cast<double>(m_pContribute->GetContributesDropAmountByRune(1)) / 100.0;

    const double itemChance = GetDropItemChanceProb(DROPITEM_MULTI);
    m_dLastMultiItemChanceProb = itemChance;

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    User* const contributorUser = contributor != nullptr && contributor->IsPC() ? contributor->CastUser() : nullptr;
    double eventAdenaRate = 1.0;
    double eventItemRate = 1.0;
    if (contributorUser != nullptr) {
        std::vector<CEvent*> events;
        Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
        for (CEvent* const event : events) {
            if (event == nullptr || !event->InEventTime()) {
                continue;
            }
            auto* const economyEvent = static_cast<CEvent_EconomyControlEvent*>(event);
            eventAdenaRate = economyEvent->GetAdenaDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            eventItemRate = economyEvent->GetItemDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            break;
        }
    }

    for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ItemAdditionalMakeListMulti) {
        const double pocketDice = ::genrand64_real1() * 100.0 + 0.0;
        bool containsAdena = false;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            if (item->ItemClassId == 0x39) {
                containsAdena = true;
                break;
            }
        }

        const double originalProbability = dropGroup.DropProb;
        double probabilityByRune = originalProbability;
        double probabilityByPremium = 0.0;
        if (!containsAdena) {
            probabilityByRune = originalProbability * runeDropRate;
            probabilityByPremium = originalProbability * premiumDropBonus;
        }
        double finalProbability = probabilityByRune + probabilityByPremium;

        if (m_data->m_nClassID == 0xfb3a4) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][ItemDropMultiList] 1.
            //     r[%f]/dDropMultiProbRand[%f]/dOriDropProb[%f]/dDropProbByRune[%f]/dDropProbByPremium[%f]",
            //     dropProbability,
            //     pocketDice,
            //     originalProbability,
            //     probabilityByRune,
            //     probabilityByPremium)
            // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][ItemDropMultiList] 1.
            //     r[%f]/dDropMultiProbRand[%f]/dOriDropProb[%f]/dDropProbByRune[%f]/dDropProbByPremium[%f]",
            //     dropProbability,
            //     pocketDice,
            //     originalProbability,
            //     probabilityByRune,
            //     probabilityByPremium)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][ItemDropMultiList] 2. dFinalDropProb[%f]")
            // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][ItemDropMultiList] 2. dFinalDropProb[%f]")
        }
        if (dropGroup.m_bChanceAffected) {
            finalProbability *= itemChance;
        }
        if (m_data->m_nClassID == 0xfb3a4) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][ItemDropMultiList] 3. dFinalDropProb[%f]")
            // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][ItemDropMultiList] 3. dFinalDropProb[%f]")
        }
        finalProbability = ClampNpcDropProbability(finalProbability * g_dItemDropRatePer * eventItemRate);
        if (m_data->m_nClassID == 0xfb3a4) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][ItemDropMultiList] 4.
            //     dFinalDropProb[%f]/g_dItemDropRatePer[%f]/dEconomyControlItemDropRate[%f]", finalProbability,
            //     g_dItemDropRatePer,
            //     eventItemRate)
            // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][ItemDropMultiList] 4.
            //     dFinalDropProb[%f]/g_dItemDropRatePer[%f]/dEconomyControlItemDropRate[%f]", finalProbability,
            //     g_dItemDropRatePer,
            //     eventItemRate)
        }
        if (!(pocketDice < finalProbability * dropProbability)) {
            continue;
        }

        const double itemDice = ::genrand64_real1() * 100.0 + 0.0;
        double accumulatedProbability = 0.0;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            accumulatedProbability += item->DropProb;
            if (m_data->m_nClassID == 0xfb3a4) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][ItemDropMultiList] 5. dDice[%f]/dRate[%f]/ids2->DropProb[%f]",
                //     itemDice,
                //     accumulatedProbability,
                //     item->DropProb)
                // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][ItemDropMultiList] 5. dDice[%f]/dRate[%f]/ids2->DropProb[%f]",
                //     itemDice,
                //     accumulatedProbability,
                //     item->DropProb)
            }
            if (!(itemDice <= accumulatedProbability)) {
                continue;
            }

            int amount = runtime::Win64CvttDoubleToInt32(
                ::genrand64_real1() * (static_cast<double>(item->DropMax) - static_cast<double>(item->DropMin)) +
                static_cast<double>(item->DropMin) + 0.5
            );
            const bool currency = item->ItemClassId == 0x39 || item->ItemClassId == 0x18d8 || item->ItemClassId == 0x18d9 ||
                item->ItemClassId == 0x18da;
            if (currency) {
                const int scaled = runtime::Win64CvttDoubleToInt32(
                    static_cast<double>(amount) * g_dAdenaAmountPer * runeDropAmount * eventAdenaRate
                );
                amount = runtime::Win64CvttDoubleToInt32(
                    static_cast<double>(scaled) * premiumDropBonus + static_cast<double>(scaled)
                );
            }
            if (amount > 0) {
                AddItemToInventory(
                    item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
                if (m_data->m_nClassID == 0xfb3a4) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[NO_ERROR][ItemDropMultiList] 6. ids2->ItemClassId[%d]")
                    // Noise: per-roll trace for NPC class 0xfb3a4, not an error.
                    // L2_LOG_ERROR(Log,
                    //     L"[NO_ERROR][ItemDropMultiList] 6. ids2->ItemClassId[%d]")
                }
            }
            break;
        }
    }

    const double exItemChance = GetDropItemChanceProb(DROPITEM_EX);
    m_dLastExItemChanceProb = exItemChance;
    for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ExItemDropListMulti) {
        const double pocketDice = ::genrand64_real1() * 100.0 + 0.0;
        double finalProbability = dropGroup.DropProb;
        if (dropGroup.m_bChanceAffected) {
            finalProbability = ClampNpcDropProbability(finalProbability * exItemChance);
        }
        if (!(pocketDice < finalProbability * dropProbability)) {
            continue;
        }

        const double itemDice = ::genrand64_real1() * 100.0 + 0.0;
        double accumulatedProbability = 0.0;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            accumulatedProbability += item->DropProb;
            if (!(itemDice <= accumulatedProbability)) {
                continue;
            }
            const int amount = runtime::Win64CvttDoubleToInt32(
                ::genrand64_real1() * (static_cast<double>(item->DropMax) - static_cast<double>(item->DropMin)) +
                static_cast<double>(item->DropMin) + 0.5
            );
            if (amount > 0) {
                AddItemToInventory(
                    item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
            }
            break;
        }
    }

    const double vitalityItemChance = GetDropItemChanceProb(DROPITEM_MULTI);
    for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ItemVitalityDropListMulti) {
        const double pocketDice = ::genrand64_real1() * 100.0 + 0.0;
        bool containsAdena = false;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            if (item->ItemClassId == 0x39) {
                containsAdena = true;
                break;
            }
        }

        const double originalProbability = dropGroup.DropProb;
        double probabilityByRune = originalProbability;
        double probabilityByPremium = 0.0;
        if (!containsAdena) {
            probabilityByRune = originalProbability * runeDropRate;
            probabilityByPremium = originalProbability * premiumVitalityDropBonus;
        }
        double finalProbability = probabilityByRune + probabilityByPremium;
        if (dropGroup.m_bChanceAffected) {
            finalProbability *= vitalityItemChance;
        }
        finalProbability =
            ClampNpcDropProbability(finalProbability * g_dItemDropRatePer * eventItemRate / g_dVitalItemDropPenalty);
        if (!(pocketDice < finalProbability * dropProbability)) {
            continue;
        }

        const double itemDice = ::genrand64_real1() * 100.0 + 0.0;
        double accumulatedProbability = 0.0;
        for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
            accumulatedProbability += item->DropProb;
            if (!(itemDice <= accumulatedProbability)) {
                continue;
            }

            int amount = runtime::Win64CvttDoubleToInt32(
                ::genrand64_real1() * (static_cast<double>(item->DropMax) - static_cast<double>(item->DropMin)) +
                static_cast<double>(item->DropMin) + 0.5
            );
            const bool currency = item->ItemClassId == 0x39 || item->ItemClassId == 0x18d8 || item->ItemClassId == 0x18d9 ||
                item->ItemClassId == 0x18da;
            if (currency) {
                const int scaled = runtime::Win64CvttDoubleToInt32(
                    static_cast<double>(amount) * g_dAdenaAmountPer * runeDropAmount * eventAdenaRate
                );
                amount = runtime::Win64CvttDoubleToInt32(
                    static_cast<double>(scaled) * premiumVitalityDropBonus + static_cast<double>(scaled)
                );
            }
            if (amount > 0) {
                AddItemToInventory(
                    item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
            }
            break;
        }
    }
}

void CNPC::LogOnDying(CCreature* const killer) {
    // Current L2Server 0x00884ec4.
    L2_GUARD_FUNCTION();
    if (killer == nullptr || !killer->IsPC() || m_data->m_dAcquireExpRate != 0.0) {
        return;
    }

    const FVector position = GetPos();
    const std::u16string actorName = std::u16string(killer->GetName());
    CLogFormatter formatter;
    formatter.WriteLog(
        0x464, killer->CastUser()->GetDBID(), killer->CastUser()->GetAccountID(), m_nClassId, m_nClassId,
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), u"", u"", u"", killer->m_data->m_Race, killer->m_data->m_Sex,
        killer->m_data->m_nClassID, killer->m_data->m_nLevel, 0, 0, 0, 0, 0, 0, actorName.c_str(),
        killer->CastUser()->m_sAccountName.data(), nullptr, nullptr, 0
    );
}

void CNPC::DropItemToInventory(double probability) {
    // Current L2Server 0x00895284.
    L2_GUARD_FUNCTION();

    const bool isSown = m_data->m_bIsSow;
    const bool isSeed2 = m_data->m_bIsSeed2;
    const double itemChance = GetDropItemChanceProb(DROPITEM_MULTI);
    m_dLastMultiItemChanceProb = itemChance;

    const double runeDropRate = static_cast<double>(m_pContribute->GetContributesDropRateByRune(0)) / 100.0;
    const double runeDropAmount = static_cast<double>(m_pContribute->GetContributesDropAmountByRune(0)) / 100.0;
    const double premiumDropBonus = m_pContribute->GetContributesPremiumDropBonus();

    CCreature* const contributor = g_CreatureSmartIdManager.GetObjectW(m_pContribute->m_PersonalGreatContributor);
    User* const contributorUser = contributor != nullptr && contributor->IsPC() ? contributor->CastUser() : nullptr;

    double eventAdenaRate = 1.0;
    double eventItemRate = 1.0;
    if (contributorUser != nullptr) {
        std::vector<CEvent*> events;
        Singleton<CEventManager>::inst().GetEvent(GAME_EVENT::ET_ECONOMY_CONTROL, events);
        for (CEvent* const event : events) {
            if (event == nullptr || !event->InEventTime()) {
                continue;
            }
            auto* const economyEvent = static_cast<CEvent_EconomyControlEvent*>(event);
            eventAdenaRate = economyEvent->GetAdenaDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            eventItemRate = economyEvent->GetItemDropRate(m_pContribute->m_PersonalGreatContributorLevel);
            break;
        }
    }

    {
        GuardHelper dropListGuard(L"DropItemToInventory1", CThreadManager::GetThreadID());
        for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ItemAdditionalMakeListMulti) {
            const int probabilityDice = std::rand() % 100;
            const double pocketDice = genrand64_real1() * 100.0;
            bool isAdenaGroup = false;
            {
                GuardHelper adenaScanGuard(L"DropItemToInventory2", CThreadManager::GetThreadID());
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    if (item->ItemClassId == 0x39) {
                        isAdenaGroup = true;
                        break;
                    }
                }
            }

            {
                GuardHelper itemDropGuard(L"DropItemToInventory3", CThreadManager::GetThreadID());
                const double originalProbability = dropGroup.DropProb;
                double probabilityByRune = originalProbability;
                double probabilityByPremium = 0.0;
                if (!isAdenaGroup) {
                    probabilityByRune = originalProbability * runeDropRate;
                    probabilityByPremium = originalProbability * premiumDropBonus;
                }
                double finalProbability = probabilityByRune + probabilityByPremium;
                if (dropGroup.m_bChanceAffected) {
                    finalProbability *= itemChance;
                }
                finalProbability = ClampNpcDropProbability(finalProbability * g_dItemDropRatePer * eventItemRate);

                if (contributorUser != nullptr && contributorUser->m_bShowItemDropProb && !isAdenaGroup) {
                    contributorUser->GetSocket()->SendSystemMessage(
                        u"SYS", u"finalDropProb = (dropProbByRune + dropProbByPremium) * itemChanceProb * itemDropRatePer"
                    );
                    std::array<char16_t, 0x800> message{};
                    FormatUtf16To(
                        message.data(), message.size(),
                        u"pocketDice[%lf], oriPocketDropProb[%lf] : dropProbByRune[%lf] dropProbByPremium[%lf] "
                        u"itemChanceProb[%lf] itemDropRatePer[%lf] economyControlRate[%lf] finalDropProb[%lf]",
                        pocketDice, originalProbability, probabilityByRune, probabilityByPremium, itemChance,
                        g_dItemDropRatePer, eventItemRate, finalProbability
                    );
                    contributorUser->GetSocket()->SendSystemMessage(u"SYS", message.data());
                }
                if (!(static_cast<double>(probabilityDice) <= probability) || !(pocketDice < finalProbability)) {
                    continue;
                }

                const double itemDice = genrand64_real1() * 100.0;
                double accumulatedProbability = 0.0;
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    accumulatedProbability += item->DropProb;
                    const bool isScaledCurrency = item->ItemClassId == 0x39 || item->ItemClassId == 0x18d8 ||
                        item->ItemClassId == 0x18d9 || item->ItemClassId == 0x18da;

                    // The current normal-drop body does not select a
                    // non-currency row while neither sowing flag is active. It only
                    // advances the cumulative probability in that branch.
                    if (!isSown && !isSeed2 && !isScaledCurrency) {
                        continue;
                    }
                    if (contributorUser != nullptr && contributorUser->m_bShowItemDropProb && !isAdenaGroup) {
                        std::array<char16_t, 0x800> message{};
                        FormatUtf16To(
                            message.data(), message.size(), u"[InnerPocket] itemId[%d] itemDropProb[%lf] itemDice[%f]",
                            item->ItemClassId, accumulatedProbability, itemDice
                        );
                        contributorUser->GetSocket()->SendSystemMessage(u"SYS", message.data());
                    }
                    if (!(itemDice <= accumulatedProbability)) {
                        continue;
                    }

                    const double minimum = static_cast<double>(item->DropMin);
                    const double maximum = static_cast<double>(item->DropMax);
                    std::int64_t amount =
                        runtime::Win64CvttDoubleToInt64(genrand64_real1() * (maximum - minimum) + minimum + 0.5);
                    if (isScaledCurrency) {
                        const std::int64_t scaled = runtime::Win64CvttDoubleToInt64(
                            static_cast<double>(amount) * g_dAdenaAmountPer * runeDropAmount * eventAdenaRate
                        );
                        amount = runtime::Win64CvttDoubleToInt64(
                            static_cast<double>(scaled) * premiumDropBonus + static_cast<double>(scaled)
                        );
                    }

                    if (amount > 0) {
                        AddItemToInventory(
                            item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                        );
                    }
                    break;
                }
            }
        }
    }
}

void CNPC::LuckyItemToInventory(User* user) {
    // Current L2Server 0x00885f9c. The preliminary scan for adena is
    // present in the shipped body but does not alter the lucky-drop route.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    {
        GuardHelper dropListGuard(L"DropItemToInventory1", CThreadManager::GetThreadID());
        for (const ItemDropMultiStruct& dropGroup : m_pClassConst->m_ItemLuckyStatListMulti) {
            const double groupDice = genrand64_real1() * 100.0 + 0.0;
            bool containsAdena = false;
            {
                GuardHelper adenaScanGuard(L"DropItemToInventory2", CThreadManager::GetThreadID());
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    if (item->ItemClassId == 0x39) {
                        containsAdena = true;
                        break;
                    }
                }
                (void)containsAdena;
            }

            {
                GuardHelper itemDropGuard(L"DropItemToInventory3", CThreadManager::GetThreadID());
                double groupProbability = dropGroup.DropProb;
                if (!(groupProbability >= 0.0)) {
                    groupProbability = 0.0;
                } else if (groupProbability > 100.0) {
                    groupProbability = 100.0;
                }
                if (groupDice >= groupProbability) {
                    continue;
                }

                const double itemDice = genrand64_real1() * 100.0;
                double accumulatedProbability = 0.0;
                for (const ItemDropStruct* const item : *dropGroup.pItemDropList) {
                    accumulatedProbability += item->DropProb;
                    if (itemDice > accumulatedProbability) {
                        continue;
                    }

                    const double minimum = static_cast<double>(item->DropMin);
                    const double maximum = static_cast<double>(item->DropMax);
                    const int amount =
                        runtime::Win64CvttDoubleToInt32(genrand64_real1() * (maximum - minimum) + minimum + 0.5);
                    if (amount > 0) {
                        user->AddItemToInventory(
                            item->ItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                        );
                    }
                    break;
                }
            }
        }
    }
}

bool CNPC::KillFakeNpc(CCreature& creature) {
    L2_GUARD_FUNCTION();
    if (!creature.IsPC() || !IsFakeNPC()) {
        return false;
    }

    User* const killer = creature.CastUser();
    m_bIsGMKill = true;
    m_pContribute->Clear();
    Die(nullptr, 0);

    if (killer->IsBuilder()) {
        char16_t system[] = u"sys";
        killer->SendSystemMessage(system, u"try kill fake npc!!");
    }

    const FVector position = creature.GetPos();
    const std::u16string ipAddress = creature.GetSocket()->GetIPAddressString();
    const std::u16string ipAddress16 = std::u16string(ipAddress.c_str());
    const std::u16string actorName = std::u16string(creature.GetName());
    CLogFormatter formatter;
    formatter.WriteAuditLog(
        0x585, creature.GetDBID(), killer->GetAccountID(), 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), ipAddress16.c_str(),
        u"", u"", 10, 0, 0, 0, 0, 0, 0, 0, 0, killer->Level(), actorName.c_str(), killer->m_sAccountName.data(), u"", u"", 0
    );

    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, user, {
        if (user == nullptr || !user->IsBuilder()) {
            continue;
        }

        const FVector observerPosition = user->GetPos();
        const double deltaX = position.x - observerPosition.x;
        const double deltaY = position.y - observerPosition.y;
        if (deltaX * deltaX + deltaY * deltaY >= 16777216.0) {
            continue;
        }

        const int objectId = static_cast<int>(creature.Id());
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        user->GetSocket()->Send(
            "cdddddddddddhhddddd", 0x48, 0, objectId, objectId, 0x7e8, 1, 500, 0, 0x5dc, x, y, z, 0, 2, x, y, z, 0, 0
        );
        user->GetSocket()->Send("cdddddd", 0x54, 0, objectId, 0x7e8, 1, 1, objectId);
    });
    return true;
}

bool CNPC::IsInDuel() const {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master != nullptr && master->IsInDuel();
}

int CNPC::GetCorpseTime() const { return m_data->m_nCorpseTime; }

bool CNPC::CanEquip(CItem* const item) {
    L2_GUARD_FUNCTION();
    return item != nullptr && item->m_const->m_bForNpc && item->CheckItemEquipCondition(this, 0);
}

void CNPC::TakeOffItems() {
    L2_GUARD_FUNCTION();

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_RHAND]);
    if (item && (item->m_data->m_Slot == 0 || !CanEquip(item.get()))) {
        UnequipItem(ST_RHAND, false);
    }

    {
        CSPointer<CItem> next = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_CHEST]);
        item = next;
    }
    if (item && (item->m_data->m_Slot == 0 || !CanEquip(item.get()))) {
        UnequipItem(ST_CHEST, false);
    }

    {
        CSPointer<CItem> next = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_NECK]);
        item = next;
    }
    if (item && (item->m_data->m_Slot == 0 || !CanEquip(item.get()))) {
        UnequipItem(ST_NECK, false);
    }
}

bool CNPC::EquipItemByClassID(const int itemClassId) {
    // Current L2Server 0x0087fc54.
    L2_GUARD_FUNCTION();

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] GetObject Fail",
        //     "NPC.cpp",
        //     0x956)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] GetObject Fail",
        //     "NPC.cpp",
        //     0x956)
        L2_LOG_ERROR(Log, "Cannot equip item class %d on NPC: object is missing or is not an item.", itemClassId);
        return false;
    }

    CItem* const item = object->CastItem();
    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }

    if (slot == ST_RHAND) {
        m_nRHandItemClassID = item->m_nClassId;
        m_nLHandItemClassID = 0;
    } else if (slot == ST_LHAND) {
        m_nRHandItemClassID = 0;
        m_nLHandItemClassID = item->m_nClassId;
    } else if (slot == ST_RLHAND) {
        m_nRHandItemClassID = item->m_nClassId;
        m_nLHandItemClassID = item->m_nClassId;
    }

    ParamChanged(NPCT_EQUIPITEM);
    return true;
}

int CNPC::EquipItem(CItem* item, ::SlotType) {
    // Current L2Server 0x00884b5c. The explicit slot argument is ignored.
    // the target derives the first matching slot from the item's slot mask.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] item==null",
        //     L"NPC.cpp",
        //     0x984)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] item==null",
        //     L"NPC.cpp",
        //     0x984)
        L2_LOG_ERROR(Log, "Cannot equip NPC item: item is missing.");
        return 0;
    }

    if (IsPet() && !CanEquip(item)) {
        CPet* const pet = CastPet();
        User* const master = pet == nullptr ? nullptr : pet->GetMaster();
        if (master != nullptr && master->GetSocket() != nullptr) {
            master->GetSocket()->SendSystemMessage(SystemMessageId::PET_CANNOT_USE_ITEM);
        }
        return 0;
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    if (slot != ST_RHAND && slot != ST_CHEST && (!IsPet() || slot != ST_NECK)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CNPC::EquipItem. invalid slot type(%d)",
        //     L"NPC.cpp",
        //     0x99b,
        //     static_cast<int>(slot))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CNPC::EquipItem. invalid slot type(%d)",
        //     L"NPC.cpp",
        //     0x99b,
        //     static_cast<int>(slot))
        L2_LOG_ERROR(
            Log, "Cannot equip NPC item class %d: derived slot %d is unsupported.", item->m_nClassId, static_cast<int>(slot)
        );
        return 0;
    }

    if (m_data->m_nSlotIndex[slot] > 0) {
        UnequipItem(slot, false);
    }
    if (CCreature::EquipItem(item, slot) == 0) {
        return 0;
    }

    if (!IsPet()) {
        ParamChanged(NPCT_EQUIPITEM2);
        return 1;
    }

    {
        L2_WRITE_GUARD(m_inv);
        m_inv.SetInventoryChanged(item, IUT_UPDATE);
    }

    CPet* const pet = CastPet();
    pet->ParamChanged(NPCT_EQUIPITEM2);
    pet->SendPetItemList(true);
    User* const master = pet->GetMaster();
    if (master == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]cannot find master from pet(%d)",
        //     L"NPC.cpp",
        //     0x9bd,
        //     pet->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]cannot find master from pet(%d)",
        //     L"NPC.cpp",
        //     0x9bd,
        //     pet->GetDBID())
        L2_LOG_ERROR(
            Log, "Pet DB ID %u equipped the item, but its master is missing; equip accounting record skipped.",
            static_cast<unsigned>(pet->GetDBID())
        );
        return 1;
    }

    CLogFormatter formatter{};
    formatter.WriteLog2_1(
        0x75, master, pet, pet->GetDBID(), GetClassId(), item->Id(), item->m_nClassId, item->GetDBID(),
        static_cast<int>(slot), item->GetDBID()
    );
    return 1;
}

SlotType CNPC::UnequipItem(CItem* const item, const bool force) {
    L2_GUARD_FUNCTION();
    return CCreature::UnequipItem(item, force);
}

SlotType CNPC::UnequipItem(const SlotType slotType, const bool force) {
    L2_GUARD_FUNCTION();

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slotType]);
    if (!item || UnequipItem(item.get(), force) == ST_MAX) {
        return ST_MAX;
    }

    if (!IsPet()) {
        ParamChanged(NPCT_UNEQUIPITEM);
    } else {
        {
            L2_WRITE_GUARD(m_inv);
            m_inv.SetInventoryChanged(item.get(), IUT_UPDATE);
        }
        CastPet()->SendSummonInfo(true, false);
        CastPet()->SendPetItemList(true);
    }
    return ST_REAR;
}

void CNPC::UnequipWeapon(bool) {
    L2_GUARD_FUNCTION();
    m_nOldLHandItemClassID = m_nLHandItemClassID;
    m_nOldRHandItemClassID = m_nRHandItemClassID;
    m_nLHandItemClassID = 0;
    m_nRHandItemClassID = 0;
    ParamChanged(NPCT_UNEQUIPWEAPON);
}

void CNPC::RestoreUnequipedWeapon() {
    L2_GUARD_FUNCTION();
    m_nLHandItemClassID = m_nOldLHandItemClassID;
    m_nRHandItemClassID = m_nOldRHandItemClassID;
    m_nOldLHandItemClassID = 0;
    m_nOldRHandItemClassID = 0;
    ParamChanged(NPCT_RESTORE_UNEQUIPWEAPON);
}

int CNPC::GetInventoryLimit() { return 1000; }

int CNPC::GetQuestInventoryLimit() { return 1000; }

int CNPC::GetArtifactInventoryLimit() { return 1000; }

bool CNPC::AddItemToInventory2(CItem* const item) {
    L2_GUARD_FUNCTION();
    if (!IsAlive()) {
        return false;
    }
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"NPC::AddItemToInventory2 failed. item is null")
        // L2_LOG_ERROR(Log,
        //     L"NPC::AddItemToInventory2 failed. item is null")
        L2_LOG_ERROR(Log, "NPC item pickup request skipped: item is missing; handler returns success.");
    } else {
        db.RequestNPCPickItem(static_cast<int>(m_nId), item->GetDBID(), item->m_data->m_nId);
    }
    return true;
}

void CNPC::LeaveWorld(bool) {
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::IncrementI32(&NpcSpawnInfo::Inst()->Server_LeaveWorld);
    CCreature::LeaveWorld(true);

    const FVector position = m_data->m_Pos;
    CWorld::Inst().LeaveWorld(this, position, false);
    // Current 0x0087fa20..0x0087fa50 reads the two category gates from
    // CSharedCreatureData +0x94 (m_nClass), not m_nClassID at +0x358c.
    if (static_cast<int>(m_data->m_nClass) == 0x55) {
        g_MonRaceDB.CheckMonRaceKeeper(0, 0);
    }
    if (static_cast<int>(m_data->m_nClass) == 0x56) {
        CTreeLoc::DeleteTree(static_cast<int>(m_nId));
    }
}

bool CNPC::DropItem(CItem* item, std::int64_t count, FVector& position, bool) {
    L2_GUARD_FUNCTION();
    if (m_bIsGMKill || !m_bCanDropItem || !CanDropItem(item, count, position, false)) {
        return false;
    }

    // Current L2Server 0x0088f985/0x0088f988 loads and passes
    // CSharedItemData::index (+0x28).
    // Passing m_nInventorySlotIndex left the item linked in the NPC inventory
    // while CWorld linked it into a world container, corrupting both lists.
    m_inv.Pop(item->m_data->index, false);
    if (!CWorld::Inst().PutItem(item, position, this)) {
        return false;
    }

    const FVector logPosition = GetPos();
    LogNpcDroppedItem(*this, *item, count, logPosition);
    item->m_nTraceNPCClassId = static_cast<std::uint32_t>(GetClassId());
    item->m_bTraceIsBoss = IsBoss();
    CCursedWeaponMgr::inst().OnDropByNPC(item);
    return true;
}

bool CNPC::DropItemAdena(CCreature* dropper, CItem* item, std::int64_t count, FVector& position, bool) {
    L2_GUARD_FUNCTION();
    if (m_bIsGMKill || !m_bCanDropItem || !CanDropItem(item, count, position, false)) {
        return false;
    }

    // Current L2Server 0x0088fd94/0x0088fd97 loads and passes
    // CSharedItemData::index (+0x28).
    // The inventory-slot lane is not an item-factory index.
    m_inv.Pop(item->m_data->index, false);
    if (dropper != nullptr && (dropper->IsSummon() || dropper->IsPet())) {
        if (User* const master = dropper->GetUserOrMaster()) {
            dropper = master;
        }
    }

    CContributeData* const contribute = static_cast<CContributeData*>(item->m_ContributeDataID.GetObjectW());
    if (contribute != nullptr) {
        const std::uint32_t contributorSid = contribute->m_PersonalGreatContributor;
        if (dropper == nullptr || contributorSid != dropper->Id()) {
            CObject* const contributor = CObject::GetObjectW(contributorSid);
            if (contributor != nullptr && contributor->IsPC()) {
                dropper = contributor->CastUser();
            }
        }
    }

    item->m_data->SetPos(position);
    item->m_data->m_bIsActive = true;
    item->m_nTraceNPCClassId = static_cast<std::uint32_t>(GetClassId());
    item->m_bTraceIsBoss = IsBoss();

    if (dropper == nullptr || !dropper->GetItem(item, true)) {
        item->m_data->m_bIsActive = false;
        if (!CWorld::Inst().PutItem(item, position, this)) {
            return false;
        }
    }

    const FVector logPosition = GetPos();
    LogNpcDroppedItem(*this, *item, count, logPosition);
    CCursedWeaponMgr::inst().OnDropByNPC(item);
    return true;
}

void CNPC::State(int state) {
    L2_GUARD_FUNCTION();
    m_State = state;
}

int CNPC::State() {
    L2_GUARD_FUNCTION();
    return m_State;
}

void CNPC::SetNickName(const char16_t* nickname) {
    L2_GUARD_FUNCTION();
    m_NickNameIndex = -1;
    CCreature::SetNickName(nickname);
}

void CNPC::SetNickNameFStr(int stringId, const char16_t* nickname) {
    L2_GUARD_FUNCTION();
    m_NickNameIndex = stringId;
    CCreature::SetNickName(nickname);
}

void CNPC::SetMasterName(const char16_t* masterName) {
    L2_GUARD_FUNCTION();
    const std::u16string_view targetName(masterName);
    if (m_MasterNameIndex == -1 && NpcUtf16NameEquals(m_sMasterName, targetName)) {
        return;
    }
    m_MasterNameIndex = -1;
    CopyNpcUtf16Name(m_sMasterName, targetName);
    m_data->m_NeedUpdatePacketBitSet.SetBit(99);
    static_cast<CCreature*>(this)->ParamChanged();
}

void CNPC::SetMasterNameFStr(int stringId, const wchar_t* masterName) {
    L2_GUARD_FUNCTION();
    const std::u16string targetName = target_utf16::FromRecoveredWindowsWide(masterName);
    if (m_MasterNameIndex == stringId && NpcUtf16NameEquals(m_sMasterName, targetName)) {
        return;
    }
    m_MasterNameIndex = stringId;
    CopyNpcUtf16Name(m_sMasterName, targetName);
    m_data->m_NeedUpdatePacketBitSet.SetBit(99);
    static_cast<CCreature*>(this)->ParamChanged();
}

void CNPC::SetBCCondList(std::vector<int>* conditions) {
    L2_GUARD_FUNCTION();
    m_vBCCondition.clear();
    for (const int condition : *conditions) {
        m_vBCCondition.push_back(condition);
    }
}

void CNPC::setExDropItem(std::vector<int>* values) {
    L2_GUARD_FUNCTION();
    m_vExDropItem.clear();
    for (const int id : *values) {
        ExDropItemData* const data = ExDropItemSystem::inst().getExDrop(id);
        if (data != nullptr) {
            m_vExDropItem.push_back(data);
        }
    }
}

void CNPC::_SetMultiDropList(std::vector<ItemDropMultiStruct*>* values) {
    L2_GUARD_FUNCTION();

    for (ItemDropMultiStruct* const multiDrop : *values) {
        for (ItemDropStruct* const itemDrop : *multiDrop->pItemDropList) {
            if (itemDrop->ItemClassId == 0x39) {
                const std::int64_t average = runtime::AddInt64Wrapping(itemDrop->DropMax, itemDrop->DropMin) / 2;
                m_pClassConst->m_nItemDropValue = runtime::AddInt64Wrapping(
                    m_pClassConst->m_nItemDropValue, runtime::MultiplyInt64Wrapping(average, 3) / 4
                );
            }
            if (itemDrop->ItemClassId == 0x18d8) {
                const std::int64_t average = runtime::AddInt64Wrapping(itemDrop->DropMax, itemDrop->DropMin) / 2;
                m_pClassConst->m_nItemDropValue = runtime::AddInt64Wrapping(
                    m_pClassConst->m_nItemDropValue, runtime::MultiplyInt64Wrapping(average, 9) / 4
                );
            }
            if (itemDrop->ItemClassId == 0x18d9) {
                const std::int64_t average = runtime::AddInt64Wrapping(itemDrop->DropMax, itemDrop->DropMin) / 2;
                m_pClassConst->m_nItemDropValue = runtime::AddInt64Wrapping(
                    m_pClassConst->m_nItemDropValue, runtime::MultiplyInt64Wrapping(average, 15) / 4
                );
            }
            if (itemDrop->ItemClassId == 0x18da) {
                const std::int64_t average = runtime::AddInt64Wrapping(itemDrop->DropMax, itemDrop->DropMin) / 2;
                m_pClassConst->m_nItemDropValue = runtime::AddInt64Wrapping(
                    m_pClassConst->m_nItemDropValue, runtime::MultiplyInt64Wrapping(average, 30) / 4
                );
            }
        }
        m_pClassConst->m_ItemAdditionalMakeListMulti.push_back(*multiDrop);
    }
}

void CNPC::Set(CObjectDB& objectDb, ObjectFieldType field, wchar_t* value) {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    if (field == OBJECT_FIELD_NAME) {
        SetName(value);
        objectDb.RegisterName(this, value);
        return;
    }
    if (field == OBJECT_FIELD_WEAPON) {
        return;
    }

    auto resolveClassId = [&]() -> int {
        const std::u16string targetName = target_utf16::FromRecoveredWindowsWide(value);
        return objectDb.GetClassIdFromName(targetName.c_str(), false);
    };
    const bool hasValue = value != nullptr && std::wcscmp(value, L"") != 0;
    if (field == OBJECT_FIELD_SLOT_CHEST) {
        if (hasValue) {
            m_pClassConst->m_nDefaultChestItemClassID = resolveClassId();
            m_nChestItemClassID = m_pClassConst->m_nDefaultChestItemClassID;
        } else {
            m_pClassConst->m_nDefaultChestItemClassID = -1;
        }
        return;
    }
    if (field == OBJECT_FIELD_SLOT_RHAND) {
        if (hasValue) {
            m_pClassConst->m_nDefaultRHandItemClassID = resolveClassId();
            m_nRHandItemClassID = m_pClassConst->m_nDefaultRHandItemClassID;
        } else {
            m_pClassConst->m_nDefaultRHandItemClassID = -1;
        }
        return;
    }
    if (field == OBJECT_FIELD_SLOT_LHAND) {
        if (hasValue) {
            m_pClassConst->m_nDefaultLHandItemClassID = resolveClassId();
            m_nLHandItemClassID = m_pClassConst->m_nDefaultLHandItemClassID;
        } else {
            m_pClassConst->m_nDefaultLHandItemClassID = -1;
        }
        return;
    }
    if (field == OBJECT_FIELD_AI) {
        objectDb.RegisterAI(this, value);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Undefined ObjectFieldType[%d], value(%s).",
    //     L"NPC.cpp",
    //     0x1151,
    //     static_cast<int>(field),
    //     value)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Undefined ObjectFieldType[%d], value(%s).",
    //     L"NPC.cpp",
    //     0x1151,
    //     static_cast<int>(field),
    //     value)
    L2_LOG_ERROR(
        Log, "Unsupported NPC string field %d; value: %s.", static_cast<int>(field),
        WideToUtf8(value ? value : L"(null)").c_str()
    );
}

void CNPC::Set(ObjectFieldType field, double value) {
    L2_GUARD_FUNCTION();
    m_bSetResult = true;
    L2_GUARD(*m_lock);

    auto setParam = [&](VALIDATE::Enum type) {
        m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dParam[type], value, 59);
    };
    switch (field) {
        case OBJECT_FIELD_PHYSICAL_HIT_MODIFY:
            m_data->m_nBaseParam[VALIDATE::PHIT] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_CRITICAL:
            setParam(VALIDATE::PCRITICAL);
            return;
        case OBJECT_FIELD_ATTACK_SPEED:
            setParam(VALIDATE::PATTACKSPEED);
            return;
        case OBJECT_FIELD_EXP: {
            const std::int64_t experience =
                g_bNpcExpMatchingByUserExpTable ? ExpByLevel[m_data->m_nLevel - 1] : static_cast<std::int64_t>(value);
            if (m_data->m_nExp != experience) {
                m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(m_data->m_nExp, experience, 6);
                m_data->m_NeedUpdatePacketBitSet.SetBit(7);
            }
            return;
        }
        case OBJECT_FIELD_BASE_HP_RECOVER:
        case OBJECT_FIELD_BASE_MP_RECOVER:
        case OBJECT_FIELD_BASE_HIT:
        case OBJECT_FIELD_BASE_AVOID:
        case OBJECT_FIELD_BASE_JUMP:
        case OBJECT_FIELD_BASE_EARSHOT:
        case OBJECT_FIELD_HP_RECOVER:
        case OBJECT_FIELD_MP_RECOVER:
        case OBJECT_FIELD_HIT:
        case OBJECT_FIELD_ATTACK_INTERVAL:
        case OBJECT_FIELD_PHYSICAL_AVOID:
        case OBJECT_FIELD_MAGIC_DEFEND:
        case OBJECT_FIELD_JUMP:
        case OBJECT_FIELD_EARSHOT:
        case OBJECT_FIELD_ORG_JUMP:
            return;
        case OBJECT_FIELD_BASE_PHYSICAL_ATTACK:
            m_data->m_nBaseParam[VALIDATE::PATTACK] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_BASE_CRITICAL:
            m_data->m_nBaseParam[VALIDATE::PCRITICAL] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_BASE_ATTACK_SPEED:
            m_data->m_nBaseParam[VALIDATE::PATTACKSPEED] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_BASE_DEFEND:
            m_data->m_nBaseParam[VALIDATE::PDEFEND] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_PHYSICAL_AVOID_MODIFY:
            m_data->m_nBaseParam[VALIDATE::PAVOID] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_BASE_MAGIC_ATTACK:
            m_data->m_nBaseParam[VALIDATE::MATTACK] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_BASE_MAGIC_DEFEND:
            m_data->m_nBaseParam[VALIDATE::MDEFEND] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_MAGIC_USE_SPEED:
        case OBJECT_FIELD_MAGIC_USE_SPEED_MODIFY:
            setParam(VALIDATE::MATTACKSPEED);
            return;
        case OBJECT_FIELD_ORG_HP:
            m_data->m_dOrgHP = value;
            return;
        case OBJECT_FIELD_ORG_MP:
            m_data->m_dOrgMP = value;
            return;
        case OBJECT_FIELD_PHYSICAL_ATTACK:
            setParam(VALIDATE::PATTACK);
            return;
        case OBJECT_FIELD_MAGIC_ATTACK:
            setParam(VALIDATE::MATTACK);
            return;
        case OBJECT_FIELD_ACQUIRE_SP:
            m_data->m_dAcquireSP = value;
            return;
        case OBJECT_FIELD_ORG_HP_REGEN:
            m_data->m_dOrgHPRegen = value;
            return;
        case OBJECT_FIELD_ORG_MP_REGEN:
            m_data->m_dOrgMPRegen = value;
            return;
        case OBJECT_FIELD_SHIELD_DEFEND_RATE:
            m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSERATE] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_SHIELD_DEFEND:
            m_data->m_nBaseParam[VALIDATE::SHIELDDEFENSE] = static_cast<int>(value);
            return;
        case OBJECT_FIELD_ACQUIRE_EXP_RATE:
            m_data->m_dAcquireExpRate = value;
            return;
        case OBJECT_FIELD_HIT_TIME_FACTOR:
            m_data->m_dHitTimeFactor = value;
            return;
        case OBJECT_FIELD_HIT_TIME_FACTOR_SKILL:
            m_data->m_dSkillHitTimeFactor = value;
            return;
        case OBJECT_FIELD_ACQUIRE_RAID_BOSS_POINT:
            m_data->m_nAcquireRaidBossPoint = static_cast<int>(value);
            return;
        case OBJECT_FIELD_S_NPC_PROP_HP_RATE:
            m_pClassConst->m_nHpRate = value;
            return;
        case OBJECT_FIELD_EX_CRT_EFFECT:
            m_data->m_nExCrtEffect = static_cast<int>(value);
            return;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid Object field type [%d] value[%lf].",
            //     L"NPC.cpp",
            //     0x0fc6,
            //     static_cast<int>(field),
            //     value)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Invalid Object field type [%d] value[%lf].",
            //     L"NPC.cpp",
            //     0x0fc6,
            //     static_cast<int>(field),
            //     value)
            L2_LOG_ERROR(
                Log, "Cannot set NPC numeric field %d to %g: field is unsupported.", static_cast<int>(field), value
            );
            m_bSetResult = false;
            return;
    }
}

void CNPC::Set(ObjectFieldType field, std::int64_t value) {
    L2_GUARD_FUNCTION();
    m_bSetResult = true;
    const std::int32_t narrowed = static_cast<std::int32_t>(value);

    {
        L2_GUARD(*m_lock);

        const auto setEnumWithBit = [&](auto& destination, int bit) {
            using Enum = std::remove_reference_t<decltype(destination)>;
            if (static_cast<std::int32_t>(destination) != narrowed) {
                destination = static_cast<Enum>(narrowed);
                m_data->m_NeedUpdatePacketBitSet.SetBit(bit);
            }
        };
        const auto setStat = [&](std::size_t index) {
            m_data->m_OriginalStat.m_N[index] = narrowed;
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_Stat.m_N[index], narrowed, 60);
        };

        switch (field) {
            case OBJECT_FIELD_BASE_ATTACK_RANGE:
                m_data->m_nBaseAttackRange = narrowed;
                return;
            case OBJECT_FIELD_NPC_ID:
                m_nClassId = runtime::AddInt32Wrapping(narrowed, 1000000);
                m_data->m_nClassID = runtime::AddInt32Wrapping(narrowed, 1000000);
                if (IsMerchant()) {
                    CPet* const pet = static_cast<CPet*>(this);
                    if (value == 0x1a || value == 0x1b) {
                        const std::uint8_t lowByte = value == 0x1a ? 1 : 0;
                        std::memcpy(&pet->m_MasterWorldID, &lowByte, sizeof(lowByte));
                    }
                }
                return;
            case OBJECT_FIELD_LEVEL:
                if (m_data->m_nLevel != narrowed) {
                    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nLevel, narrowed, 5);
                    m_data->m_NeedUpdatePacketBitSet.SetBit(7);
                }
                return;
            case OBJECT_FIELD_RACE:
                m_data->m_Race = static_cast<RaceType>(narrowed);
                return;
            case OBJECT_FIELD_CLASS:
                setEnumWithBit(m_data->m_nClass, 3);
                return;
            case OBJECT_FIELD_SEX:
                setEnumWithBit(m_data->m_Sex, 2);
                return;
            case OBJECT_FIELD_INT:
                setStat(0);
                return;
            case OBJECT_FIELD_STR:
                setStat(1);
                return;
            case OBJECT_FIELD_CON:
                setStat(2);
                return;
            case OBJECT_FIELD_MEN:
                setStat(3);
                return;
            case OBJECT_FIELD_DEX:
                setStat(4);
                return;
            case OBJECT_FIELD_WIT:
                setStat(5);
                return;
            case OBJECT_FIELD_LUC:
                setStat(6);
                return;
            case OBJECT_FIELD_CHA:
                setStat(7);
                return;
            case OBJECT_FIELD_BASE_ATTACK_TYPE:
                m_data->m_BaseAttackType = static_cast<AttackType>(narrowed);
                return;
            case OBJECT_FIELD_RAND_DAM:
                m_data->m_nBaseRandomDamage = narrowed;
                return;
            case OBJECT_FIELD_UNDYING:
                m_data->m_bUndying = value != 0;
                return;
            case OBJECT_FIELD_CAN_BE_ATTACKED:
                m_data->m_bCanBeAttacked = value != 0;
                return;
            case OBJECT_FIELD_AGRO_RANGE:
                m_data->m_nAgroRange = narrowed;
                return;
            case OBJECT_FIELD_CLAN_HELP_RANGE:
                m_data->m_nClanHelpRange = narrowed;
                return;
            case OBJECT_FIELD_BASE_REUSE_DELAY:
                m_data->m_nBaseReuseDelay = narrowed;
                return;
            case OBJECT_FIELD_CORPSE_TIME:
                m_data->m_nCorpseTime = narrowed;
                return;
            case OBJECT_FIELD_NO_SLEEP_MODE:
                m_data->m_cNoSleepMode = static_cast<char>(value);
                return;
            case OBJECT_FIELD_SOULSHOT_COUNT:
                if (!IsPet()) {
                    m_data->m_nSoulShotCount = narrowed;
                }
                return;
            case OBJECT_FIELD_SPIRITSHOT_COUNT:
                if (!IsPet()) {
                    m_data->m_nSpiritShotCount = narrowed;
                }
                return;
            case OBJECT_FILED_UNSOWING:
                m_data->m_bUnsowing = value == 1;
                return;
            case OBJECT_FIELD_PASSABLE_DOOR:
                m_data->m_bPassableDoor = narrowed == 1;
                return;
            case OBJECT_FIELD_UNIQUE:
                m_data->m_bUnique = narrowed == 1;
                return;
            case OBJECT_FIELD_NPC_CAN_MOVE:
                m_data->m_bCanMove = narrowed == 1;
                return;
            case OBJECT_FIELD_HAS_SUMMONER:
                m_bHasSummoner = narrowed == 1;
                return;
            case OBJECT_FIELD_TARGETABLE:
                m_data->m_StatusFlag[0] = narrowed == 1;
                return;
            case OBJECT_FIELD_SHOW_NAME_TAG:
                m_data->m_StatusFlag[1] = narrowed == 1;
                return;
            case OBJECT_FIELD_PRIVATE_RESPAWN_NO_LOG:
                m_data->m_nPrivateRespawnNoLog = narrowed == 1;
                return;
            case OBJECT_FIELD_FAKE_CLASS_ID:
                if (narrowed != -1) {
                    m_nFakeClassId = runtime::AddInt32Wrapping(narrowed, 1000000);
                }
                return;
            case OBJECT_FIELD_IS_DEATH_PENALTY:
                m_pClassConst->m_IsDeathPenalty = narrowed == 1;
                return;
            case OBJECT_FIELD_ENABLE_MOVE_AFTER_TALK:
                m_data->m_EnableMoveAfterTalk = narrowed == 1;
                return;
            case OBJECT_FIELD_NPC_CAN_CHANGE_DIR:
                m_bCanChanageDir = narrowed == 1;
                return;

            // Literal current no-op fields.
            case OBJECT_FIELD_TEND:
            case OBJECT_FIELD_HP_INCREASE:
            case OBJECT_FIELD_MP_INCREASE:
            case OBJECT_FIELD_WEIGHT_LIMIT:
            case OBJECT_FIELD_SATIETY:
            case OBJECT_FIELD_STATUS:
            case OBJECT_FIELD_TENDENCY:
            case OBJECT_FIELD_EVALUATION:
            case OBJECT_FIELD_TEMPER:
            case OBJECT_FIELD_SAFE_HEIGHT:
            case OBJECT_FIELD_MAX_ITEM:
            case OBJECT_FIELD_ALIGN:
            case OBJECT_FIELD_GUILD_LEADER:
            case OBJECT_FIELD_PLEDGE_LEADER:
            case OBJECT_FIELD_ALLIANCE_LEADER:
            case OBJECT_FIELD_RULER:
                return;
            default:
                break;
        }
    }

    // Current drops the integer-set lock, calls the double
    // overload, then reacquires at source line 0xee9 before leaving.
    Set(field, static_cast<double>(value));
    {
        L2_GUARD(*m_lock);
    }
}

void CNPC::Set(ObjectFieldType field, std::vector<double>* values) {
    L2_GUARD_FUNCTION();
    if (values == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Invalid parameter. pDoubleVector=NULL",
        //     "NPC.cpp",
        //     0x10a6)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Invalid parameter. pDoubleVector=NULL",
        //     "NPC.cpp",
        //     0x10a6)
        L2_LOG_ERROR(Log, "Cannot set NPC double-vector field %d: values are missing", static_cast<int>(field));
        return;
    }

    L2_GUARD(*m_lock);
    if (field == OBJECT_FIELD_COLLISION_RADIUS) {
        if (values->size() == 2) {
            m_pClassConst->m_dCollisionRadius[0] = runtime::CheckedVectorAt((*values), 0);
            m_pClassConst->m_dCollisionRadius[1] = runtime::CheckedVectorAt((*values), 1);
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                m_data->m_dCollisionRadius, runtime::CheckedVectorAt((*values), 0), 27
            );
        }
        return;
    }
    if (field == OBJECT_FIELD_COLLISION_HEIGHT) {
        if (values->size() == 2) {
            m_pClassConst->m_dCollisionHeight[0] = runtime::CheckedVectorAt((*values), 0);
            m_pClassConst->m_dCollisionHeight[1] = runtime::CheckedVectorAt((*values), 1);
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(
                m_data->m_dCollisionHeight, runtime::CheckedVectorAt((*values), 0), 28
            );
        }
        return;
    }
    if (field == OBJECT_FIELD_BASE_ATTRIBUTE_ATTACK) {
        if (values->size() == 2) {
            m_pClassConst->m_BaseAttribute.m_nAttackType =
                static_cast<std::int16_t>(runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 0)));
            m_pClassConst->m_BaseAttribute.m_nAttackValue =
                static_cast<std::int16_t>(runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 1)));
        }
        return;
    }
    if (field == OBJECT_FIELD_BASE_ATTRIBUTE_DEFEND) {
        if (values->size() == 7) {
            for (std::size_t index = 0; index < 6; ++index) {
                m_data->m_NeedUpdatePacketBitSet.SetShortValue_IfModifed(
                    m_pClassConst->m_BaseAttribute.m_nDefendValue[index],
                    static_cast<std::int16_t>(runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), index))), 36
                );
            }
            m_pClassConst->m_BaseAttribute.m_nNoneAttributeValue =
                static_cast<std::int16_t>(runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 6)));
        }
        return;
    }
    if (field == OBJECT_FIELD_EVENT_FLAG) {
        m_data->m_bEventFlag[0] = runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 0)) == 1;
        return;
    }
    if (field == OBJECT_FIELD_ABNORMAL_RESIST) {
        if (values->size() == 2) {
            m_data->m_OriginAbnormalResist.m_N[0] = runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 0));
            m_data->m_OriginAbnormalResist.m_N[1] = runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 1));
            m_data->m_AbnormalResist.m_N[0] = runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 0));
            m_data->m_AbnormalResist.m_N[1] = runtime::Win64CvttDoubleToInt32(runtime::CheckedVectorAt((*values), 1));
        }
        return;
    }

    std::vector<std::int64_t> integerValues;
    for (const double element : *values) {
        integerValues.push_back(runtime::Win64CvttDoubleToInt64(element));
    }
    Set(field, &integerValues);
}

void CNPC::Set(ObjectFieldType field, std::vector<std::int64_t>* values) {
    L2_GUARD_FUNCTION();
    if (values == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Invalid parameter. pIntVector=NULL",
        //     "NPC.cpp",
        //     0x0fd8)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Invalid parameter. pIntVector=NULL",
        //     "NPC.cpp",
        //     0x0fd8)
        L2_LOG_ERROR(Log, "Cannot set NPC integer-vector field %d: values are missing", static_cast<int>(field));
        return;
    }

    const auto setMovement = [&](int moveType, int environment) {
        m_pClassConst->m_SpeedByME[moveType][environment] = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 0));
        m_pClassConst->m_RegenByME[moveType][environment] = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 1));
        m_pClassConst->m_NoiseRangeByME[moveType][environment] =
            static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 2));
    };

    switch (field) {
        case OBJECT_FIELD_DAMAGE_RANGE:
            if (values->size() == 4) {
                m_data->m_BaseDamageRange.target = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 0));
                m_data->m_BaseDamageRange.direction = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 1));
                m_data->m_BaseDamageRange.distance = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 2));
                m_data->m_BaseDamageRange.angle = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), 3));
            }
            return;
        case OBJECT_FIELD_GROUND_LOW:
            for (int environment = 0; environment < 4; ++environment) {
                setMovement(0, environment);
            }
            return;
        case OBJECT_FIELD_GROUND_HIGH:
            for (int environment = 0; environment < 4; ++environment) {
                setMovement(1, environment);
            }
            return;
        case OBJECT_FIELD_UNDERWATER_LOW:
            setMovement(0, 1);
            return;
        case OBJECT_FIELD_UNDERWATER_HIGH:
            setMovement(1, 1);
            return;
        case OBJECT_FIELD_FLY_LOW:
            setMovement(0, 2);
            return;
        case OBJECT_FIELD_FLY_HIGH:
            setMovement(1, 2);
            return;
        case OBJECT_FIELD_FLOAT_LOW:
            setMovement(0, 3);
            return;
        case OBJECT_FIELD_FLOAT_HIGH:
            setMovement(1, 3);
            return;
        case OBJECT_FIELD_SKILL_LIST:
            m_pClassConst->m_SkillList.clear();
            for (const std::int64_t value : *values) {
                m_pClassConst->m_SkillList.push_back(value);
            }
            return;
        case OBJECT_FIELD_SIGHT:
            if (values->size() != 5) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] sight range loading failed. pIntVector size [%d].",
                //     "NPC.cpp",
                //     0x1050,
                //     static_cast<int>(values->size()))
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] sight range loading failed. pIntVector size [%d].",
                //     "NPC.cpp",
                //     0x1050,
                //     static_cast<int>(values->size()))
                L2_LOG_ERROR(Log, "Invalid NPC sight parameters: received %zu values, expected 5.", values->size());
            }
            return;
        case OBJECT_FIELD_CLAN:
            if (values->size() < 6) {
                m_data->m_nClanIdCount = static_cast<std::int32_t>(values->size());
                for (std::int32_t index = 0; index < m_data->m_nClanIdCount; ++index) {
                    m_data->m_ClanId[index] = static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), index));
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Clan id count is over 5.",
                //     L"NPC.cpp",
                //     0x0ff7)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Clan id count is over 5.",
                //     L"NPC.cpp",
                //     0x0ff7)
                L2_LOG_ERROR(Log, "Cannot set NPC clan IDs: received %zu entries, maximum is 5.", values->size());
            }
            return;
        case OBJECT_FIELD_IGNORE_CLAN_LIST: {
            std::size_t count = values->size();
            if (count > 50) {
                // Log.Add(
                //     LOG_ERROR,
                //     // Current keeps 50 entries but reports 20.
                //     L"Ignore clan list size overflow (max = 20)")
                // L2_LOG_ERROR(Log,
                //     // Current keeps 50 entries but reports 20.
                //     L"Ignore clan list size overflow (max = 20)")
                L2_LOG_ERROR(Log, "NPC ignore-clan list has %zu entries; processing only the first 50.", count);
                count = 50;
            }
            CNPCLoadingData& loadingData = g_NPCLoadingDataMap[m_data->m_nClassID];
            for (std::size_t index = 0; index < count; ++index) {
                loadingData.m_IgnoreDB.insert(static_cast<std::int32_t>(runtime::CheckedVectorAt((*values), index)));
            }
            return;
        }
        default:
            return;
    }
}

void CNPC::Set(ObjectFieldType field, std::vector<ItemDropStruct*>* values) {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    if (field == OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST) {
        ItemDropMultiStruct* const multiDrop = new ItemDropMultiStruct();
        multiDrop->pItemDropList = new std::vector<ItemDropStruct*>();
        multiDrop->DropProb = 100.0;
        for (ItemDropStruct* const itemDrop : *values) {
            multiDrop->pItemDropList->push_back(itemDrop);
        }

        std::vector<ItemDropMultiStruct*> multiDropList;
        multiDropList.push_back(multiDrop);
        _SetMultiDropList(&multiDropList);
        return;
    }

    if (field == OBJECT_FIELD_CORPSE_MAKE_LIST) {
        for (ItemDropStruct* const itemDrop : *values) {
            m_pClassConst->m_CorpseMakeList.push_back(*itemDrop);
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Undefined ObjectFieldType[%d]",
    //     L"NPC.cpp",
    //     0x118b,
    //     static_cast<int>(field))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Undefined ObjectFieldType[%d]",
    //     L"NPC.cpp",
    //     0x118b,
    //     static_cast<int>(field))
    L2_LOG_ERROR(Log, "Unsupported NPC item-drop list field %d.", static_cast<int>(field));
}

void CNPC::Set(ObjectFieldType field, std::vector<ItemDropMultiStruct*>* values) {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    if (field == OBJECT_FIELD_ITEM_ADDITIONAL_DROP_LIST_MULTI) {
        _SetMultiDropList(values);
        return;
    }
    if (field == OBJECT_FIELD_EX_ITEM_DROP_LIST) {
        for (ItemDropMultiStruct* const multiDrop : *values) {
            m_pClassConst->m_ExItemDropListMulti.push_back(*multiDrop);
        }
        return;
    }
    if (field == OBJECT_FIELD_VITALITY_ITEM_DROP_LIST) {
        for (ItemDropMultiStruct* const multiDrop : *values) {
            m_pClassConst->m_ItemVitalityDropListMulti.push_back(*multiDrop);
        }
        return;
    }
    if (field == OBJECT_FIELD_LUCKY_STAT_ITEM_LIST) {
        for (ItemDropMultiStruct* const multiDrop : *values) {
            m_pClassConst->m_ItemLuckyStatListMulti.push_back(*multiDrop);
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Undefined ObjectFieldType[%d]",
    //     L"NPC.cpp",
    //     0x11b9,
    //     static_cast<int>(field))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Undefined ObjectFieldType[%d]",
    //     L"NPC.cpp",
    //     0x11b9,
    //     static_cast<int>(field))
    L2_LOG_ERROR(Log, "Unsupported NPC multi-drop list field %d.", static_cast<int>(field));
}

void CNPC::Set(ObjectFieldType field, std::vector<GoodStruct*>* values) {
    L2_GUARD_FUNCTION();
    if (values == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Invalid parameter. pIntVector=NULL",
        //     "NPC.cpp",
        //     0x11f4)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Invalid parameter. pIntVector=NULL",
        //     "NPC.cpp",
        //     0x11f4)
        L2_LOG_ERROR(Log, "Cannot set NPC goods field %d: goods list is missing.", static_cast<int>(field));
        return;
    }

    L2_GUARD(*m_lock);
    CMerchant* const merchant = static_cast<CMerchant*>(this);
    if (field == OBJECT_FIELD_SELLING_GOODS) {
        for (GoodStruct* const good : *values) {
            merchant->AddSellingGood(good);
        }
        return;
    }
    if (field == OBJECT_FIELD_BUYING_GOODS) {
        for (GoodStruct* const good : *values) {
            merchant->AddBuyingGood(good);
        }
    }
}

void CNPC::GetOrgSpeeds(OrgSpeeds& speeds) {
    speeds.m_nOrgSpeed0 = m_pClassConst->m_SpeedByME[1][0];
    speeds.m_nOrgSpeed1 = m_pClassConst->m_SpeedByME[0][0];
    speeds.m_nOrgSpeed2 = m_pClassConst->m_SpeedByME[1][1];
    speeds.m_nOrgSpeed3 = m_pClassConst->m_SpeedByME[0][1];
    speeds.m_nOrgSpeed4 = m_pClassConst->m_SpeedByME[1][3];
    speeds.m_nOrgSpeed5 = m_pClassConst->m_SpeedByME[0][3];
    speeds.m_nOrgSpeed6 = m_pClassConst->m_SpeedByME[1][2];
    speeds.m_nOrgSpeed7 = m_pClassConst->m_SpeedByME[0][2];

    const int environment = static_cast<int>(m_data->m_Environment);
    // Current L2Server CNPC::GetOrgSpeeds@0x008832dc reads the selected
    // movement-type base lane at shared-data +0x3330. The already modified
    // m_nOrgParam[SPEED] lane at +0x331c is not used by this packet helper.
    const int speed = m_data->m_nMoveTypeSpeed;
    const bool moving = static_cast<int>(m_data->m_MoveMode) == 1;
    if (environment == 0) {
        (moving ? speeds.m_nOrgSpeed0 : speeds.m_nOrgSpeed1) = speed;
    } else if (environment == 1) {
        (moving ? speeds.m_nOrgSpeed2 : speeds.m_nOrgSpeed3) = speed;
    } else if (environment == 3) {
        (moving ? speeds.m_nOrgSpeed4 : speeds.m_nOrgSpeed5) = speed;
    } else if (environment == 2) {
        (moving ? speeds.m_nOrgSpeed6 : speeds.m_nOrgSpeed7) = speed;
    }
}

void CNPC::SendCharInfo_internal(User* user, bool createObject) {
    // Current L2Server 0x0088a6b4. This is the dedicated droid-NPC
    // character-info envelope. It is distinct from NPCInfo.
    L2_GUARD_FUNCTION();
    if (!m_isDroidNPC) {
        return;
    }

    const FVector position = GetPos();
    const int magicAttackSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::MATTACKSPEED]);
    const int physicalSkillSpeed = runtime::Win64CvttDoubleToInt32(m_data->m_dParam[VALIDATE::PSKILLSPEED]);

    OrgSpeeds speeds{};
    GetOrgSpeeds(speeds);
    const double moveModifier = GetMoveModifier();
    const double attackModifier = GetAttackModifier();

    std::array<char, 128> emptyBinary{};
    const int criminalRate = m_DroidNPCInfo.nKarma < 500 ? m_DroidNPCInfo.nKarma : 500;

    int face = m_DroidNPCInfo.nFace;
    int hairShape = m_DroidNPCInfo.nHairShape;
    int hairColor = m_DroidNPCInfo.nHairColor;
    if (m_DroidNPCInfo.cHairAccFlag == 0 || (m_DroidNPCInfo.nSlotHair == -1 && m_DroidNPCInfo.nSlotHair2 == -1)) {
        if (m_DroidNPCInfo.nFaceEx != 0) {
            face = m_DroidNPCInfo.nFaceEx;
        }
        if (m_DroidNPCInfo.nHairShapeEx != 0) {
            hairShape = m_DroidNPCInfo.nHairShapeEx;
        }
        if (m_DroidNPCInfo.nHairColorEx != 0) {
            hairColor = m_DroidNPCInfo.nHairColorEx;
        }
    }

    const std::u16string packetName = std::u16string(GetName());
    CPacketHelperEx<20480> abnormalVisualEffects;
    MakeAbnormalVisualEffectPacket(abnormalVisualEffects);
    CPacketHelperEx<20480> packet;

    const bool stopMode = (static_cast<std::uint32_t>(m_data->m_StopMode) & ~UINT32_C(2)) != 0;
    const bool moveMode = m_data->m_MoveMode != 0;
    packet.Assemble(
        "ccdddddShcdddddddddddddddddddcdddddddddcdddhhhhhhhhffffdddSddddccccccchbcchdddccdcccdddddchdcdddcdddddcdbccc", 0x31,
        !createObject, runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), 0, static_cast<int>(m_data->m_nId), packetName.c_str(),
        static_cast<int>(static_cast<std::int8_t>(m_DroidNPCInfo.cRace)),
        static_cast<int>(static_cast<std::int8_t>(m_DroidNPCInfo.cGender)), m_DroidNPCInfo.nBaseClass,
        m_DroidNPCInfo.nSlotUnderware, m_DroidNPCInfo.nSlotHead, m_DroidNPCInfo.nSlotRightHand, m_DroidNPCInfo.nSlotLeftHand,
        m_DroidNPCInfo.nSlotGloves, m_DroidNPCInfo.nSlotChest, m_DroidNPCInfo.nSlotLegs, m_DroidNPCInfo.nSlotFeet,
        m_DroidNPCInfo.nSlotBack, m_DroidNPCInfo.nSlotBothHands, m_DroidNPCInfo.nSlotHair, m_DroidNPCInfo.nSlotHair2,
        m_DroidNPCInfo.nVariationOpt1, m_DroidNPCInfo.nVariationOpt2, m_DroidNPCInfo.nVariationOpt1,
        m_DroidNPCInfo.nVariationOpt2, m_DroidNPCInfo.nVariationOpt1, m_DroidNPCInfo.nVariationOpt2, 0,
        m_DroidNPCInfo.nShapeShiftRightHand, m_DroidNPCInfo.nShapeShiftLeftHand, m_DroidNPCInfo.nShapeShiftBothHands,
        m_DroidNPCInfo.nShapeShiftGloves, m_DroidNPCInfo.nShapeShiftChest, m_DroidNPCInfo.nShapeShiftLegs,
        m_DroidNPCInfo.nShapeShiftFeet, m_DroidNPCInfo.nShapeShiftHair, m_DroidNPCInfo.nShapeShfitHair2, 0, criminalRate,
        magicAttackSpeed, physicalSkillSpeed, static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed0)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed1)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed2)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed3)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed4)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed5)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed6)),
        static_cast<int>(static_cast<std::int16_t>(speeds.m_nOrgSpeed7)), moveModifier, attackModifier,
        m_DroidNPCInfo.dCollisionRadius, m_DroidNPCInfo.dCollisionHeight, face, hairShape, hairColor, m_sNickName,
        m_DroidNPCInfo.nPledgeSID, m_DroidNPCInfo.nPledgeCrestDBID, m_DroidNPCInfo.nAllianceDBID,
        m_DroidNPCInfo.nAllianceCrestDBID, static_cast<int>(stopMode), static_cast<int>(moveMode),
        static_cast<int>(m_data->m_bIsInCombatMode), static_cast<int>(!m_data->m_bIsAlive), 0, 0, 0, 0,
        // Current 0x0088ab6c..0x0088ab78 places a zero byte count in the
        // first `b` lane and the local 128-byte buffer address in its pointer
        // lane. Omitting this length shifts every remaining variadic value.
        0, emptyBinary.data(), 0, static_cast<int>(static_cast<std::int8_t>(m_data->m_Environment)),
        m_DroidNPCInfo.nBonusCount, 0, m_DroidNPCInfo.nNowClass, 0, m_DroidNPCInfo.nEnchantWeapon, 0,
        m_DroidNPCInfo.nPledgeEmblemDBID, static_cast<int>(static_cast<std::int8_t>(m_DroidNPCInfo.cNobless)),
        static_cast<int>(static_cast<std::int8_t>(m_DroidNPCInfo.cHero)), 0, 0, 0, 0, m_DroidNPCInfo.nCharNameColor,
        static_cast<int>(static_cast<std::uint16_t>(m_data->GetDirByInt())), 0,
        static_cast<int>(static_cast<std::int16_t>(m_DroidNPCInfo.nPledgeType)), m_DroidNPCInfo.nNickNameColor, 0, 0, 0, 0,
        0, 0, runtime::Win64CvttDoubleToInt32(m_data->m_dHP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP),
        runtime::Win64CvttDoubleToInt32(m_data->m_dMP), runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP), 0,
        abnormalVisualEffects.GetAssembledCount(), abnormalVisualEffects.GetLength(), abnormalVisualEffects.GetPacket(), 0,
        static_cast<int>(static_cast<std::int8_t>(m_DroidNPCInfo.cHairAccFlag)), m_DroidNPCInfo.nAP
    );

    if (user == nullptr) {
        g_ptlUserMap->BroadcastToNeighborExceptSelfWithSenderVisibility(
            this, 0x80000, GetPos(), "b", packet.GetLength(), packet.GetPacket()
        );
    } else if (user->GetSocket() != nullptr) {
        user->GetSocket()->Send("b", packet.GetLength(), packet.GetPacket());
    }
}

void CNPC::BroadcastCharInfoUpdate() {
    // Current L2Server 0x0088b610.
    L2_GUARD_FUNCTION();
    SendCharInfo_internal(nullptr, false);
}

void CNPC::SendCreateObject(User* user) {
    // Current L2Server 0x0088b6b8.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        SendCharInfo_internal(user, true);
    }
}

int CNPC::GetPacketType() {
    if (IsPet()) {
        return 0;
    }
    if (!IsSummon()) {
        return 2;
    }
    return g_bSendSummonHPMPInfo ? 1 : 3;
}

void CNPC::SendNPCInfoImplForSpeed(User& user) {
    L2_GUARD_FUNCTION();
    CUserSocket* const targetSocket = user.GetSocket();
    if (runtime::client_protocol::NegotiatedRevision(targetSocket) ==
        runtime::client_protocol::kNorthAmericanRevision) {
        // P152 sends speed as FE:0132 with a four-bit field block.
        NPCInfoPacket snapshot;
        CPacketHelperEx<20480> statePacket;
        CPacketHelperEx<20480> speedPacket;
        if (snapshot.Init(GetPacketType())) {
            if (snapshot.EncodeState(false, this, statePacket)) {
                targetSocket->Send("b", statePacket.GetLength(), statePacket.GetPacket());
            }
            if (snapshot.EncodeSpeed(false, this, speedPacket)) {
                targetSocket->Send("b", speedPacket.GetLength(), speedPacket.GetPacket());
            }
        }
        return;
    }
    OrgSpeeds speeds{};
    GetOrgSpeeds(speeds);
    const double moveModifier = GetMoveModifier();
    if (runtime::runtime_timing::Matches(this)) {
        runtime::runtime_timing::Record(
            "npc_speed_packet", this, &user,
            "mode=%d environment=%d base_speed=%d "
            "effective_speed=%.6f modifier=%.9f "
            "org=%d,%d,%d,%d,%d,%d,%d,%d",
            static_cast<int>(m_data->m_MoveMode), static_cast<int>(m_data->m_Environment), m_data->m_nMoveTypeSpeed,
            m_data->m_dParam[VALIDATE::SPEED], moveModifier, speeds.m_nOrgSpeed0, speeds.m_nOrgSpeed1, speeds.m_nOrgSpeed2,
            speeds.m_nOrgSpeed3, speeds.m_nOrgSpeed4, speeds.m_nOrgSpeed5, speeds.m_nOrgSpeed6, speeds.m_nOrgSpeed7
        );
    }
    user.GetSocket()->Send(
        "chdcfdddddddd", 0xfe, 0x132, static_cast<int>(m_nId), static_cast<int>(m_data->m_MoveMode), moveModifier,
        speeds.m_nOrgSpeed0, speeds.m_nOrgSpeed1, speeds.m_nOrgSpeed2, speeds.m_nOrgSpeed3, speeds.m_nOrgSpeed4,
        speeds.m_nOrgSpeed5, speeds.m_nOrgSpeed6, speeds.m_nOrgSpeed7
    );
}

void CNPC::SendNPCInfoImpl(User& user, int sendType) {
    L2_GUARD_FUNCTION();

    CUserSocket* const targetSocket = user.GetSocket();
    if (runtime::client_protocol::NegotiatedRevision(targetSocket) ==
        runtime::client_protocol::kNorthAmericanRevision) {
        // Portable compatibility extension: P152 and the shipping 162
        // product use different NPC_INFO mask envelopes. Emit a full P152
        // snapshot for every affected update. The current delta path below
        // remains untouched for revision 162.
        NPCInfoPacket protocol152Packet;
        CPacketHelperEx<20480> output;
        if (protocol152Packet.Init(GetPacketType()) && protocol152Packet.EncodeProtocol152(&user, this, sendType, output)) {
            targetSocket->Send("b", output.GetLength(), output.GetPacket());
        }
        SendTacticalSignInfo(user);
        return;
    }

    const FVector sharedPosition(m_data->m_Pos);
    const FVector relativePosition(m_RelPos);
    int carrierId = 0;
    if (IsInVehicle()) {
        carrierId = CContainerDB::Inst().GetVehicleID(m_data->container_index);
    } else if (IsInAirShip()) {
        carrierId = static_cast<int>(GetMyAirShipID());
    } else if (IsInShuttle()) {
        carrierId = static_cast<int>(GetMyShuttleID());
    }
    const FVector packetPosition(carrierId > 0 ? relativePosition : sharedPosition);
    (void)packetPosition;

    (void)GetAttackModifier();
    (void)GetMoveModifier();

    if (m_data->m_nSlotIndex[ST_CHEST] != -1) {
        (void)CItemFactory::Inst();
    }
    if (m_data->m_nSlotIndex[ST_RHAND] != -1) {
        (void)CItemFactory::Inst();
    }
    if (m_data->m_nSlotIndex[ST_LHAND] != -1) {
        (void)CItemFactory::Inst();
    }

    if (IsSummonOrPet()) {
        (void)IsDoppelganger();
    }
    (void)GetDoppelgangerSummonerSID();

    User* const master = GetMaster();
    if (master == nullptr) {
        (void)CPVPMatchSystem::inst().GetPVPMatchTeamIndex(this);
    } else {
        if (master->IsEventMatch()) {
            (void)master->m_EventMatchUser.GetEventMatch();
        }
        if (master->IsInDuel() && IsAlive()) {
            (void)master->m_DuelUserInfo.GetTeamForEffect(master);
        }
        (void)master->GetParty();
    }

    (void)GetGuilty();
    (void)CalcCriminalRate();

    const bool hideMasterInformation = NeedToHideInfoAboutMaster();
    if (IsSummonOrPet()) {
        CSummon* const summon = CastSummon();
        User* const summonMaster = summon->GetMaster();
        if (summonMaster != nullptr && !hideMasterInformation) {
            (void)summonMaster->GetRelationTo(&user);
        }
    }

    CPacketHelperEx<20480> abnormalVisualEffectPacket;
    (void)MakeAbnormalVisualEffectPacket(abnormalVisualEffectPacket);

    if (IsNPCRelatedToPledge()) {
        CPledge* pledge = nullptr;
        {
            CSPointer<CPledge> pledgePointer =
                CPledge::FindPledge(runtime::process::CurrentWorldId(), static_cast<int>(m_data->m_nPledgeID));
            pledge = pledgePointer.get();
        }
        if (pledge != nullptr) {
            (void)pledge->GetEmblemId();
            (void)user.GetServerId();
        }
    }

    SendTacticalSignInfo(user);
}

bool CNPC::SendEnterWorldPacket(CCreature* creature, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (m_pac != nullptr && m_pav != nullptr && m_pat != nullptr && m_pad != nullptr && m_isEnterWorldPacket != 0) {
            CPacketHelperEx<20480> privatePacket;
            if (!m_NPCInfoPacket.EncodePrivate(GetPacketType(), user, this, privatePacket)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][%s] fail to EncodePrivate()",
                //     L"NPC.cpp",
                //     0x1d83,
                //     L"CNPC::SendEnterWorldPacket")
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][%s] fail to EncodePrivate()",
                //     L"NPC.cpp",
                //     0x1d83,
                //     L"CNPC::SendEnterWorldPacket")
                L2_LOG_ERROR(
                    Log, "Cannot encode private NPC world-entry data: NPC class=%d, object=%u", GetClassId(),
                    static_cast<unsigned int>(m_nId)
                );
            } else if (m_NPCInfoPacket.Encode(GetPacketType(), 2, this, privatePacket, *m_pac, *m_pav, packet)) {
                return true;
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][%s] fail to Encode()",
                //     L"NPC.cpp",
                //     0x1d89,
                //     L"CNPC::SendEnterWorldPacket")
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][%s] fail to Encode()",
                //     L"NPC.cpp",
                //     0x1d89,
                //     L"CNPC::SendEnterWorldPacket")
                L2_LOG_ERROR(
                    Log, "Cannot encode NPC world-entry packet: NPC class=%d, object=%u", GetClassId(),
                    static_cast<unsigned int>(m_nId)
                );
            }
        }
    }
    return false;
}

void CNPC::SendMyInfo() { ParamChanged(NPCT_MY_INFO); }

void CNPC::MakeEnterWorldPacket() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*m_lock);
        m_isEnterWorldPacket = 1;
    }

    m_NPCInfoPacket.Clear();
    m_NPCInfoPacket.SetSendDefaultPacket(GetPacketType());

    int allocated = 0;
    auto popPacket = [&]() -> CPacketHelperEx<20480>* {
        void* const storage = g_PacketHelperPool.Pop(allocated);
        return storage != nullptr ? new (storage) CPacketHelperEx<20480>() : nullptr;
    };

    m_pac = popPacket();
    m_pav = popPacket();
    m_pat = popPacket();
    m_pad = popPacket();
    if (m_pac == nullptr || m_pav == nullptr || m_pat == nullptr || m_pad == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s] m_pac == NULL || m_pav == NULL || "
        //     L"pat == NULL || pad == NULL",
        //     L"NPC.cpp")
        // L2_LOG_ERROR(Log,
        //     L"[%s] m_pac == NULL || m_pav == NULL || "
        //     L"pat == NULL || pad == NULL",
        //     L"NPC.cpp")
        L2_LOG_ERROR(
            Log,
            "Cannot allocate NPC world-entry packet buffers: NPC class=%d, object=%u, missing buffers: common=%d, "
            "visual=%d, state=%d, speed=%d; triggering crash report",
            GetClassId(), static_cast<unsigned int>(m_nId), m_pac == nullptr, m_pav == nullptr, m_pat == nullptr,
            m_pad == nullptr
        );
        CrashImpl();
    }

    m_pac->Reset();
    m_pav->Reset();
    m_pat->Reset();
    m_pad->Reset();
    const int packetType = GetPacketType();
    if (runtime::runtime_timing::Matches(this)) {
        OrgSpeeds speeds{};
        GetOrgSpeeds(speeds);
        runtime::runtime_timing::Record(
            "npc_info_initial_speed", this, nullptr,
            "packet_type=%d mode=%d environment=%d base_speed=%d "
            "effective_speed=%.6f org=%d,%d,%d,%d,%d,%d,%d,%d",
            packetType, static_cast<int>(m_data->m_MoveMode), static_cast<int>(m_data->m_Environment),
            m_data->m_nMoveTypeSpeed, m_data->m_dParam[VALIDATE::SPEED], speeds.m_nOrgSpeed0, speeds.m_nOrgSpeed1,
            speeds.m_nOrgSpeed2, speeds.m_nOrgSpeed3, speeds.m_nOrgSpeed4, speeds.m_nOrgSpeed5, speeds.m_nOrgSpeed6,
            speeds.m_nOrgSpeed7
        );
    }
    m_NPCInfoPacket.EncodeCommon(true, packetType, this, *m_pac);
    m_NPCInfoPacket.EncodeVisualEffect(true, this, *m_pav);
    m_NPCInfoPacket.EncodeState(true, this, *m_pat);
    m_NPCInfoPacket.EncodeSpeed(true, this, *m_pad);
    m_data->m_NeedUpdatePacketBitSet.Clear();
}

void CNPC::ClearEnterWorldPacket() {
    L2_GUARD_FUNCTION();
    if (m_isEnterWorldPacket == 0) {
        return;
    }

    if (m_pac != nullptr) {
        g_PacketHelperPool.Push(m_pac);
        m_pac = nullptr;
    }
    if (m_pav != nullptr) {
        g_PacketHelperPool.Push(m_pav);
        m_pav = nullptr;
    }
    if (m_pat != nullptr) {
        g_PacketHelperPool.Push(m_pat);
        // Shipping L2Server store at 0x008879c7 kept for audit. It
        // cleared the adjacent visual packet pointer and left pat dangling:
        // m_pav = nullptr
        m_pat = nullptr;
    }
    if (m_pad != nullptr) {
        g_PacketHelperPool.Push(m_pad);
        // Shipping L2Server store at 0x008879e2 repeated the same adjacent
        // field error after returning pad to the pool:
        // m_pav = nullptr
        m_pad = nullptr;
    }
    m_NPCInfoPacket.m_NeedUpdatePacketBitSet.Clear();
    {
        L2_GUARD(*m_lock);
        m_isEnterWorldPacket = 0;
    }
}

void CNPC::Suicide(DamageTypeEnum, int reason) {
    L2_GUARD_FUNCTION();
    m_data->m_LastAttackerServerID = 0;
    Die(nullptr, reason);
}

void CNPC::SendNPCInfo(User* user, int type, FVector position, int distance, bool checkZ, bool speedOnly) {
    L2_GUARD_FUNCTION();
    // Current 0x0088ca9d..0x0088cac8 loads CSharedCreatureData +0x94
    // once and compares the m_nClass category against 0x54 and 0x55.
    if (static_cast<int>(m_data->m_nClass) == 0x54) {
        return;
    }
    if (static_cast<int>(m_data->m_nClass) == 0x55) {
        g_MonRaceDB.SendRacePacket(user);
    }
    if (!m_data->m_bIsActive) {
        return;
    }
    if (IsCheckBot3(m_nClassId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CHECK_BOT] [%s][%d] S_NPC_INFO NpcId = %d, "
        //     L"NpcClassId = % d",
        //     L"NPC.cpp",
        //     0x5d6,
        //     static_cast<int>(m_nId),
        //     m_nClassId)
        // L2_LOG_ERROR(Log,
        //     L"[CHECK_BOT] [%s][%d] S_NPC_INFO NpcId = %d, "
        //     L"NpcClassId = % d",
        //     L"NPC.cpp",
        //     0x5d6,
        //     static_cast<int>(m_nId),
        //     m_nClassId)
        L2_LOG_ERROR(
            Log, "NPC info send skipped for check-bot NPC: object=%u, class=%d", static_cast<unsigned int>(m_nId), m_nClassId
        );
        return;
    }

    if (!g_isPacketOptimizeMode) {
        if (user == nullptr) {
            struct TLMFilter final : CTLMFilterRect {
                TLMFilter(int inZoneId, const FVector& filterPosition, int filterDistance, bool filterZ) :
                    CTLMFilterRect(inZoneId, filterPosition, static_cast<double>(filterDistance)), checkZ(filterZ) {}

                bool CanDo(CObject* object) override {
                    if (!CTLMFilterRect::CanDo(object)) {
                        return false;
                    }
                    User* const target = object->CastUser();
                    if (checkZ) {
                        const int filterZ = runtime::Win64CvttDoubleToInt32(__vPos.z);
                        const int targetZ = runtime::Win64CvttDoubleToInt32(target->m_data->m_Pos.z);
                        if (runtime::AddInt32Wrapping(filterZ, 200) < targetZ) {
                            return false;
                        }
                        const int collisionHeight = runtime::Win64CvttDoubleToInt32(target->m_data->m_dCollisionHeight);
                        if (runtime::AddInt32Wrapping(collisionHeight, targetZ) <
                            runtime::AddInt32Wrapping(filterZ, -200)) {
                            return false;
                        }
                    }
                    return true;
                }

                bool checkZ;
            };

            struct TLMFunc final : CTLMFunc {
                TLMFunc(CNPC* sender, bool senderIsSummonOrPet, int sendType, int senderMasterId, bool sendSpeedOnly) :
                    npc(sender),
                    senderIsSummonOrPet(senderIsSummonOrPet),
                    type(sendType),
                    masterId(senderMasterId),
                    speedOnly(sendSpeedOnly) {}

                bool CanDo(CObject* object, double) override {
                    if (!senderIsSummonOrPet || masterId != static_cast<int>(object->m_nId)) {
                        if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                            User* const target = object->CastUser();
                            if (!speedOnly) {
                                npc->SendNPCInfoImpl(*target, type);
                                CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(npc->GetClassId()));
                            } else {
                                npc->SendNPCInfoImplForSpeed(*target);
                            }
                        }
                    } else {
                        npc->CastSummon()->SendSummonInfo(true, false);
                    }
                    return true;
                }

                CNPC* npc;
                bool senderIsSummonOrPet;
                int type;
                int masterId;
                bool speedOnly;
            };


            TLMFilter filter(static_cast<int>(GetInZoneID()), position, distance, checkZ);
            int masterId = 0;
            if (GetMaster() != nullptr) {
                masterId = static_cast<int>(GetMaster()->m_nId);
            }
            TLMFunc function(
                this,
                IsSummonOrPet(), type, masterId, speedOnly
            );
            FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
        } else {
            SendNPCInfoImpl(*user, type);
            CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(GetClassId()));
        }
        return;
    }

    if (user != nullptr &&
        runtime::client_protocol::NegotiatedRevision(user->GetSocket()) ==
            runtime::client_protocol::kNorthAmericanRevision) {
        SendNPCInfoImpl(*user, type);
        CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(GetClassId()));
        return;
    }

    CPacketHelperEx<20480> packet;
    if (user == nullptr) {
        CPacketHelperEx<20480> commonPacket;
        CPacketHelperEx<20480> visualEffectPacket;
        CPacketHelperEx<20480> statePacket;
        CPacketHelperEx<20480> speedPacket;
        const bool initialize = type != 1;
        const bool commonChanged = m_NPCInfoPacket.EncodeCommon(initialize, GetPacketType(), this, commonPacket);
        const bool visualEffectChanged = m_NPCInfoPacket.EncodeVisualEffect(false, this, visualEffectPacket);
        constexpr std::uint32_t kUnusedBit = std::numeric_limits<std::uint32_t>::max();
        const bool sharedCommonChanged = m_data->m_NeedUpdatePacketBitSet.IsSet(
            98, 99, kUnusedBit, kUnusedBit, kUnusedBit, kUnusedBit, kUnusedBit, kUnusedBit, kUnusedBit, kUnusedBit
        );
        const bool commonVisualOrSharedChanged = commonChanged || visualEffectChanged || sharedCommonChanged;
        const bool stateChanged = m_NPCInfoPacket.EncodeState(initialize, this, statePacket);
        const bool speedChanged = m_NPCInfoPacket.EncodeSpeed(initialize, this, speedPacket);
        const bool extraSharedChanged =
            m_data->m_NeedUpdatePacketBitSet.IsSet(14, 5, 6, 7, 59, 73, 16, 16, kUnusedBit, kUnusedBit);
        m_data->m_NeedUpdatePacketBitSet.Clear();
        if (!commonVisualOrSharedChanged && !extraSharedChanged && !stateChanged && !speedChanged) {
            return;
        }

        CContainer* container = nullptr;
        // Current L2Server 0x0088d5a5 calls CObject vtable slot 25
        // (+0xc8), which is IsInRealWorld. Using IsInVehicle here leaves an
        // ordinary world NPC without a container and drops optimized NPC
        // delta publication to every neighboring user.
        if (!IsInRealWorld()) {
            CInstantZone* const instantZone = GetInstantZone();
            if (instantZone != nullptr) {
                container = instantZone->GetContainer();
            }
        } else {
            container = CContainerDB::Inst().GetWorldContainer(
                runtime::Win64CvttDoubleToInt32(m_data->m_Pos.x), runtime::Win64CvttDoubleToInt32(m_data->m_Pos.y),
                runtime::Win64CvttDoubleToInt32(m_data->m_Pos.z)
            );
        }
        CContainerIndex containerIndex = CContainerDB::Inst().GatherNeighborContainerIndex(container);

        int masterId = 0;
        if (GetMaster() != nullptr) {
            masterId = static_cast<int>(GetMaster()->m_nId);
        }

        struct ContainerFunc final : CContainerFunc {
            ContainerFunc(
                CNPC* sender,
                bool senderIsSummonOrPet,
                int sendType,
                int senderMasterId,
                CPacketHelperEx<20480>& common,
                CPacketHelperEx<20480>& visualEffect,
                CPacketHelperEx<20480>& state,
                CPacketHelperEx<20480>& speed,
                bool commonVisualOrShared,
                bool extraShared
            ) :
                npc(sender),
                senderIsSummonOrPet(senderIsSummonOrPet),
                type(sendType),
                masterId(senderMasterId),
                commonPacket(&common),
                visualEffectPacket(&visualEffect),
                statePacket(&state),
                speedPacket(&speed),
                commonVisualOrSharedChanged(commonVisualOrShared),
                extraSharedChanged(extraShared),
                didSend(false) {}

            void Do(CObject* object) override {
                if (!senderIsSummonOrPet || masterId != static_cast<int>(object->m_nId)) {
                    if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                        User* const target = object->CastUser();
                        if (target != nullptr && !target->IsDeleting() && target->m_Deleted == 0) {
                            CUserSocket* const targetSocket = target->GetSocket();
                            const bool protocol152 = runtime::client_protocol::NegotiatedRevision(targetSocket) ==
                                runtime::client_protocol::kNorthAmericanRevision;
                            // P152 accepts these updates without sending the position again.
                            CPacketHelperEx<20480> privatePacket;
                            CPacketHelperEx<20480> outputPacket;
                            if (commonPacket->GetLength() > 0 || commonVisualOrSharedChanged) {
                                npc->m_NPCInfoPacket.EncodePrivate(npc->GetPacketType(), target, npc, privatePacket);
                                if (protocol152) {
                                    npc->m_NPCInfoPacket.EncodeProtocol152(
                                        type, npc, privatePacket, *commonPacket, *visualEffectPacket, outputPacket
                                    );
                                } else {
                                    npc->m_NPCInfoPacket.Encode(
                                        npc->GetPacketType(), type, npc, privatePacket, *commonPacket, *visualEffectPacket,
                                        outputPacket
                                    );
                                }
                                target->GetSocket()->Send("b", outputPacket.GetLength(), outputPacket.GetPacket());
                            }
                            if (visualEffectPacket->GetLength() != 0) {
                                target->GetSocket()->Send(
                                    "b", visualEffectPacket->GetLength(), visualEffectPacket->GetPacket()
                                );
                            }
                            if (statePacket->GetLength() != 0) {
                                target->GetSocket()->Send("b", statePacket->GetLength(), statePacket->GetPacket());
                            }
                            if (speedPacket->GetLength() != 0) {
                                target->GetSocket()->Send("b", speedPacket->GetLength(), speedPacket->GetPacket());
                            }
                            npc->SendTacticalSignInfo(*target);
                            didSend = true;
                        }
                        CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(npc->GetClassId()));
                    }
                } else if (extraSharedChanged || commonVisualOrSharedChanged) {
                    npc->CastSummon()->SendSummonInfo(true, false);
                }
            }

            CNPC* npc;
            bool senderIsSummonOrPet;
            int type;
            int masterId;
            CPacketHelperEx<20480>* commonPacket;
            CPacketHelperEx<20480>* visualEffectPacket;
            CPacketHelperEx<20480>* statePacket;
            CPacketHelperEx<20480>* speedPacket;
            bool commonVisualOrSharedChanged;
            bool extraSharedChanged;
            bool didSend;
        };


        CContainerFilterNull filter;
        ContainerFunc function(
            this,
            IsSummonOrPet(), type, masterId, commonPacket, visualEffectPacket, statePacket, speedPacket,
            commonVisualOrSharedChanged, extraSharedChanged
        );
        FOR_ALL_OBJECT_IN_CONTAINER_INDEX(containerIndex, filter, function);
        m_NPCInfoPacket.m_NeedUpdatePacketBitSet.Clear();
        return;
    }

    if (type == 2 && m_isEnterWorldPacket != 0) {
        if (!SendEnterWorldPacket(user->CastCreature(), packet)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][%s] fail to SendEnterWorldPacket()",
            //     L"NPC.cpp",
            //     0x639,
            //     L"CNPC::SendNPCInfo")
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][%s] fail to SendEnterWorldPacket()",
            //     L"NPC.cpp",
            //     0x639,
            //     L"CNPC::SendNPCInfo")
            L2_LOG_ERROR(
                Log, "NPC info was not sent: world-entry packet unavailable for NPC class=%d, object=%u", GetClassId(),
                static_cast<unsigned int>(m_nId)
            );
            return;
        }
        CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(GetClassId()));
        if (!user->IsDeleting() && user->m_Deleted == 0) {
            if (packet.GetLength() != 0) {
                user->GetSocket()->Send("b", packet.GetLength(), packet.GetPacket());
                if (!g_bSendSummonHPMPInfo && IsSummon() && user->IsMyParty(this)) {
                    user->GetSocket()->Send(
                        "cddcccdcdcdcd", 0x18, static_cast<int>(m_nId), 0, 0, 4, 9,
                        runtime::Win64CvttDoubleToInt32(m_data->GetHP()), 10,
                        runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP), 11,
                        runtime::Win64CvttDoubleToInt32(m_data->GetMP()), 12,
                        runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP)
                    );
                }
            }
            SendTacticalSignInfo(*user);
        }
        return;
    }

    NPCInfoPacket npcInfoPacket;
    CPacketHelperEx<20480> privatePacket;
    CPacketHelperEx<20480> commonPacket;
    CPacketHelperEx<20480> visualEffectPacket;
    if (!npcInfoPacket.Init(GetPacketType())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][%s] fail to NPCInfoPacket.Init()",
        //     L"NPC.cpp",
        //     0x66b,
        //     L"CNPC::SendNPCInfo")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][%s] fail to NPCInfoPacket.Init()",
        //     L"NPC.cpp",
        //     0x66b,
        //     L"CNPC::SendNPCInfo")
        L2_LOG_ERROR(
            Log, "Cannot initialize NPC info packet: NPC class=%d, object=%u", GetClassId(), static_cast<unsigned int>(m_nId)
        );
        return;
    }
    if (npcInfoPacket.EncodeCommon(type != 1, GetPacketType(), this, commonPacket)) {
        npcInfoPacket.EncodeVisualEffect(type != 1, this, visualEffectPacket);
        npcInfoPacket.EncodePrivate(GetPacketType(), user, this, privatePacket);
        npcInfoPacket.Encode(GetPacketType(), type, this, privatePacket, commonPacket, visualEffectPacket, packet);
        if (!user->IsDeleting() && user->m_Deleted == 0) {
            user->GetSocket()->Send("b", packet.GetLength(), packet.GetPacket());
            if (!g_bSendSummonHPMPInfo && IsSummon() && user->IsMyParty(this)) {
                user->GetSocket()->Send(
                    "cddcccdcdcdcd", 0x18, static_cast<int>(m_nId), 0, 0, 4, 9,
                    runtime::Win64CvttDoubleToInt32(m_data->GetHP()), 10,
                    runtime::Win64CvttDoubleToInt32(m_data->m_dBaseHP), 11,
                    runtime::Win64CvttDoubleToInt32(m_data->GetMP()), 12,
                    runtime::Win64CvttDoubleToInt32(m_data->m_dBaseMP)
                );
            }
        }
    }
    if (!user->IsDeleting() && user->m_Deleted == 0) {
        SendTacticalSignInfo(*user);
    }
    CSendNPCInfoStat::Inst().Accumulate(static_cast<unsigned int>(GetClassId()));
}

void CNPC::SendNPCInfo(User* user, const bool first, const bool second) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        SendNPCInfo(nullptr, 1, m_data->m_Pos, 0xc00, false, second);
        return;
    }

    if (IsSummon() && user == GetMaster()) {
        CastSummon()->SendSummonInfo(false, first);
        return;
    }

    SendNPCInfo(user, first ? 2 : 0, m_data->m_Pos, 0xc00, false, second);
}

void CNPC::SendNPCInfoForSpecialCamera3(User* user, FVector position, int distance) {
    L2_GUARD_FUNCTION();
    SendNPCInfo(user, 2, position, distance, true, false);
}

void CNPC::SocialAction(int actionId, int distance, bool broadcast) {
    L2_GUARD_FUNCTION();
    if (!broadcast) {
        const int objectId = static_cast<int>(m_nId);
        const FVector position = m_data->m_Pos;
        g_ptlUserMap->BroadcastToNeighborDist(this, 0x5000, position, distance, "cddd", 0x27, objectId, actionId, 0);
        return;
    }

    const FVector cameraPosition = GetPos();
    SendNPCInfoForSpecialCamera3(nullptr, cameraPosition, distance);

    const int objectId = static_cast<int>(m_nId);
    const int maxZ = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(m_data->m_Pos.z), 200);
    const int minZ = runtime::AddInt32Wrapping(runtime::Win64CvttDoubleToInt32(m_data->m_Pos.z), -200);
    const FVector position = m_data->m_Pos;
    g_ptlUserMap->BroadcastToNeighborDistBetweenZ(
        *this, 0x5000, position, distance, minZ, maxZ, "cddd", 0x27, objectId, actionId, 0
    );
}

bool CNPC::IsRootNPC() { return m_bIsPureAncestor; }

User* CNPC::GetUserOrMaster() { return GetMaster(); }

void CNPC::ParamChanged(std::int64_t parameterIndex) {
    m_bParamChanged.exchange(1, std::memory_order_seq_cst);
    m_nParamChanged |= std::uint64_t{1} << (static_cast<std::uint64_t>(parameterIndex) & 0x3fU);
}

void CNPC::Say(const char16_t* text) {
    L2_GUARD_FUNCTION();
    const FVector position = GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x80000, position, 1000, "cddddS", 0x30, static_cast<int>(m_nId), 0x16, GetClassId(), -1,
        text
    );
}

void CNPC::SayFStr(
    int stringId,
    wchar_t* text1,
    wchar_t* text2,
    wchar_t* text3,
    wchar_t* text4,
    wchar_t* text5,
    int value1,
    int value2,
    int value3,
    int value4,
    int value5
) {
    L2_GUARD_FUNCTION();
    const FVector position = GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x80000, position, 1000, "cddddSSSSSddddd", 0x30, static_cast<int>(m_nId), 0x16, GetClassId(), stringId,
        target_utf16::FromRecoveredWindowsWide(text1).c_str(), target_utf16::FromRecoveredWindowsWide(text2).c_str(),
        target_utf16::FromRecoveredWindowsWide(text3).c_str(), target_utf16::FromRecoveredWindowsWide(text4).c_str(),
        target_utf16::FromRecoveredWindowsWide(text5).c_str(), value1, value2, value3, value4, value5
    );
}

void CNPC::Shout(const char16_t* text, int radius) {
    L2_GUARD_FUNCTION();
    if (radius > 0x4000 || radius == 0) {
        radius = 0x4000;
    }

    const FVector position = GetPos();
    g_ptlUserMap->BroadcastToNeighborRect(
        this, 0x5000, position, radius, "cddddS", 0x30, static_cast<int>(m_nId), 0x17, GetClassId(), -1,
        text
    );
}

void CNPC::ShoutFStr(
    int stringId,
    wchar_t* text1,
    wchar_t* text2,
    wchar_t* text3,
    wchar_t* text4,
    wchar_t* text5,
    int radius,
    int value1,
    int value2,
    int value3,
    int value4,
    int value5
) {
    L2_GUARD_FUNCTION();
    if (radius > 0x4000 || radius == 0) {
        radius = 0x4000;
    }

    const FVector position = GetPos();
    g_ptlUserMap->BroadcastToNeighborRect(
        this, 0x5000, position, radius, "cddddSSSSSddddd", 0x30, static_cast<int>(m_nId), 0x17, GetClassId(), stringId,
        target_utf16::FromRecoveredWindowsWide(text1).c_str(), target_utf16::FromRecoveredWindowsWide(text2).c_str(),
        target_utf16::FromRecoveredWindowsWide(text3).c_str(), target_utf16::FromRecoveredWindowsWide(text4).c_str(),
        target_utf16::FromRecoveredWindowsWide(text5).c_str(), value1, value2, value3, value4, value5
    );
}

bool CNPC::OnNpcMovePacket(int x, int y, int z) {
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_NPC_MOVEPACKET);
    if (m_nDead < 1 && m_SkillMod.m_nBlockMoveCount < 1) {
        if (!CWorld::Inst().IsValidPosition(x, y)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "invalid position (%d, %d) at file[%s], line[%d]",
            //     x,
            //     y,
            //     "NPC.cpp",
            //     0x170f)
            // L2_LOG_ERROR(Log,
            //     "invalid position (%d, %d) at file[%s], line[%d]",
            //     x,
            //     y,
            //     "NPC.cpp",
            //     0x170f)
            L2_LOG_ERROR(
                Log, "NPC move rejected: destination (%d, %d, %d) failed world XY validation; NPC class=%d, object=%u", x, y,
                z, GetClassId(), static_cast<unsigned int>(m_nId)
            );
            return false;
        }

        m_FollowData.Init();
        MoveToLocation(x, y, z, UsePathFind);
    }
    return false;
}

bool CNPC::OnNpcAbsoluteReturn(int x, int y, int z, int mode) {
    // Current L2Server 0x00885978.
    L2_GUARD_FUNCTION();

    CastCreature()->m_pContribute->Clear();
    m_data->SetHP(m_data->m_dBaseHP);
    m_data->SetMP(m_data->m_dBaseMP);
    SendStatusUpdate(VCP_HP, 0, 0);

    if (mode == 0) {
        m_data->m_bTeleporting = true;
        m_SkillMod.m_bCanGetDamageHP = false;
        m_SkillMod.m_bCanGetDamageMP = false;
        TeleportToLocation(x, y, z, GetInZoneID(), 0, TeleportSourceType::OnNpcAbsoluteReturn, nullptr, 0);
        m_data->m_bTeleporting = false;
        m_SkillMod.m_bCanGetDamageHP = true;
        m_SkillMod.m_bCanGetDamageMP = true;
    } else {
        m_nReturnedTime = runtime::crt::Time32Now();
        m_data->m_bTeleporting = true;
        m_SkillMod.m_bCanGetDamageHP = false;
        m_SkillMod.m_bCanGetDamageMP = false;
        MoveToLocation(x, y, z, UsePathFind);
    }
    return false;
}

bool CNPC::OnNpcFollow(int targetId, char followType, int first, int second) {
    L2_GUARD_FUNCTION();
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetId));
    if (target != nullptr) {
        const bool changed = m_FollowData.SetData(followType, first, second);
        if (m_spTarget.GetId() != target->m_nId || changed) {
            DoNothing();
            m_FollowData.SetData(followType, first, second);
            Action(target, false, false);
            Action(target, false, false);
        }
    }
    return false;
}

bool CNPC::OnNpcBroadcastScriptEvent(int argument1, int argument2, int argument3, unsigned int radius) {
    // Current L2Server 0x00885b58.
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    g_npcBroadcastIdCount = 0;
    g_npcBroadcastCandidateCount = 0;

    class CTLMFuncNpcBroadcastScriptEvent final : public CTLMFunc {
    public:
        explicit CTLMFuncNpcBroadcastScriptEvent(const FVector& position) : m_position(position) {}

        bool CanDo(CObject* object, double) override {
            if (object->IsNPC() && !object->IsSummonOrPet()) {
                CNPC* const npc = object->CastNPC();
                ++g_npcBroadcastCandidateCount;
                if (npc->m_data->m_bIsActive && g_npcBroadcastIdCount < static_cast<std::int32_t>(kMaxNpcBroadcastIds)) {
                    const std::int32_t zDifference = runtime::Win64CvttDoubleToInt32(npc->m_data->m_Pos.z - m_position.z);
                    if (runtime::AbsoluteInt32Wrapping(zDifference) < 0x1f5) {
                        g_npcBroadcastIds[static_cast<std::size_t>(g_npcBroadcastIdCount)] =
                            static_cast<std::int32_t>(npc->m_nId);
                        ++g_npcBroadcastIdCount;
                    }
                }
            }
            return true;
        }

    private:
        FVector m_position;
    } function(position);


    CTLMFilterDist filter(static_cast<int>(GetInZoneID()), position, static_cast<double>(radius));
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);

    NpcBroadCastStat::Inst().Accumulate(runtime::AddInt32Wrapping(GetClassId(), -1000000), g_npcBroadcastIdCount);
    if (g_npcBroadcastIdCount != 0) {
        g_npcserver.Send(
            "cddddbd", 0x3c, static_cast<std::int32_t>(m_nId), argument1, argument2, argument3,
            g_npcBroadcastIdCount * static_cast<std::int32_t>(sizeof(std::int32_t)), g_npcBroadcastIds.data(), -1
        );
    }
    return false;
}

bool CNPC::OnNpcBroadcastScriptEventCond(int argument1, int argument2, int argument3, unsigned int radius) {
    // Current L2Server 0x00885d74.
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    g_npcBroadcastIdCount = 0;
    g_npcBroadcastCandidateCount = 0;
    const unsigned int inZoneId = GetInZoneID();
    const std::int32_t sourceNpcSid = m_data->m_nId;

    class CTLMFuncNpcBroadcastScriptEvent final : public CTLMFunc {
    public:
        CTLMFuncNpcBroadcastScriptEvent(const FVector& position, std::int32_t sourceNpcSid) :
            m_position(position), m_sourceNpcSid(sourceNpcSid) {}

        bool CanDo(CObject* object, double) override {
            if (!object->IsNPC() || object->IsSummonOrPet()) {
                return true;
            }

            CCreature* const sourceObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_sourceNpcSid));
            CNPC* sourceNpc = nullptr;
            if (sourceObject != nullptr && sourceObject->IsNPC()) {
                sourceNpc = sourceObject->CastNPC();
            }
            if (sourceNpc == nullptr) {
                return true;
            }

            CNPC* const npc = object->CastNPC();
            ++g_npcBroadcastCandidateCount;
            if (!npc->m_data->m_bIsActive || g_npcBroadcastIdCount >= static_cast<std::int32_t>(kMaxNpcBroadcastIds)) {
                return true;
            }

            const std::int32_t zDifference = runtime::Win64CvttDoubleToInt32(npc->m_data->m_Pos.z - m_position.z);
            if (runtime::AbsoluteInt32Wrapping(zDifference) >= 0x1f5) {
                return true;
            }

            for (const std::int32_t condition : sourceNpc->m_vBCCondition) {
                if (runtime::AddInt32Wrapping(condition, 1000000) == npc->GetClassId()) {
                    g_npcBroadcastIds[static_cast<std::size_t>(g_npcBroadcastIdCount)] =
                        static_cast<std::int32_t>(npc->m_nId);
                    ++g_npcBroadcastIdCount;
                }
            }
            return true;
        }

    private:
        FVector m_position;
        std::int32_t m_sourceNpcSid;
    } function(position, sourceNpcSid);


    CTLMFilterDist filter(static_cast<int>(inZoneId), position, static_cast<double>(radius));
    FOR_ALL_NEIGHBOR_CREATURE_DIST(&filter, &function);

    NpcBroadCastStat::Inst().Accumulate(runtime::AddInt32Wrapping(GetClassId(), -1000000), g_npcBroadcastIdCount);
    if (g_npcBroadcastIdCount != 0) {
        g_npcserver.Send(
            "cddddbd", 0x3c, static_cast<std::int32_t>(m_nId), argument1, argument2, argument3,
            g_npcBroadcastIdCount * static_cast<std::int32_t>(sizeof(std::int32_t)), g_npcBroadcastIds.data(), -1
        );
    }
    return false;
}

void CNPC::NullSkillAction(std::int64_t skillNameId, bool stand, bool force, int followType, int followData1) {
    L2_GUARD_FUNCTION();

    const std::uint64_t packed = static_cast<std::uint64_t>(skillNameId);
    const std::uint32_t skillIdBits = static_cast<std::uint32_t>(packed >> 32U);
    std::int32_t skillId = 0;
    std::memcpy(&skillId, &skillIdBits, sizeof(skillId));
    const int level = static_cast<int>((packed >> 16U) & 0xffffU);
    const int subLevel = static_cast<int>(packed & 0xffffU);

    AcquireSkill(skillId, level, subLevel, true, true);
    ValidateParameters(VB_ITEMSKILL | VB_SKILLLIST, false);
    ValidateChanged(VB_MAX);
    SkillAction(skillId, force, stand, nullptr, nullptr, followType, followData1);
}

bool CNPC::OnNpcAttackPacket(int targetId, NpcAttackMoveType moveType, int forceAttack) {
    L2_GUARD_FUNCTION();
    if (m_nDead > 0 || m_SkillMod.m_nBlockMoveCount > 0) {
        return false;
    }

    CObject* const target = CObject::GetObjectW(static_cast<std::uint32_t>(targetId));
    if (target == nullptr || !target->IsVisible()) {
        return false;
    }

    if (!IsSummonOrPet() && !IsInRealWorld()) {
        CGeoData* const geo = CWorld::Inst().GetGeo();
        if (!geo->CanSee(CastWorldObject(), target->CastWorldObject(), true)) {
            return false;
        }
    }

    m_FollowData.Init();
    Action(target, forceAttack != 0, moveType == AMT_STAND);
    Action(target, forceAttack != 0, moveType == AMT_STAND);
    return true;
}

bool CNPC::OnNpcSetParam(CHAR_PARAM_TYPE parameter, double value) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(parameter) >= 0x29U) {
        return false;
    }

    switch (parameter) {
        case VCP_LEVEL:
            if (!IsPet()) {
                SetLevel(static_cast<unsigned int>(runtime::Win64CvttDoubleToInt64(value)));
            } else {
                CPet* const pet = CastPet();
                pet->SetLevel(runtime::Win64CvttDoubleToInt32(value));
            }
            SendStatusUpdate(VCP_LEVEL, 0, 0);
            break;

        case VCP_HP:
            if (value > 0.0 && value <= m_data->m_dOrgHP) {
                m_data->SetHP(value);
                SendStatusUpdate(VCP_HP, 0, 0);
            }
            break;

        case VCP_MAXHP:
            m_data->m_dOrgHP = value;
            ValidateOrgHP();
            SendStatusUpdate(VCP_MAXHP, 0, 0);
            break;

        case VCP_SP: {
            L2_GUARD(*m_lock);
            m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(
                m_data->m_nSP, runtime::Win64CvttDoubleToInt64(value), 0xe
            );
            SendStatusUpdate(VCP_SP, 0, 0);
            break;
        }

        case VCP_HP_REGEN: {
            L2_GUARD(*m_lock);
            m_data->m_dHPRegen = value;
            break;
        }

        default:
            break;
    }
    return false;
}

bool CNPC::OnNpcDispel(int targetId, const AbnormalStatusSlotType abnormalType) {
    L2_GUARD_FUNCTION();
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetId));
    if (target != nullptr) {
        target->DispelBySlot(abnormalType, this, -1, nullptr, SPET_DISPEL, false);
    }
    return false;
}

bool CNPC::OnNpcUseSkillPacket(
    int targetId, std::int64_t skillNameId, NpcAttackMoveType moveType, int forceAttack, int followType, int followData1
) {
    L2_GUARD_FUNCTION();
    if (m_nDead > 0 || m_SkillMod.m_nBlockMoveCount > 0) {
        return false;
    }

    const std::uint64_t packed = static_cast<std::uint64_t>(skillNameId);
    const std::uint32_t skillIdBits = static_cast<std::uint32_t>(packed >> 32U);
    std::int32_t skillId = 0;
    std::memcpy(&skillId, &skillIdBits, sizeof(skillId));
    const int level = static_cast<int>((packed >> 16U) & 0xffffU);
    const int subLevel = static_cast<int>(packed & 0xffffU);

    CObject* const target = CObject::GetObjectW(static_cast<std::uint32_t>(targetId));
    if (target == nullptr || !target->IsVisible()) {
        OnUseSkillFinished(targetId, skillId, level, subLevel, 0);
        return false;
    }

    ChangeTarget(target, CTR_DEFAULT);
    NullSkillAction(skillNameId, moveType == AMT_STAND, forceAttack != 0, followType, followData1);

    if (IsPet()) {
        User* const master = GetMaster();
        if (master != nullptr && target->IsCreature()) {
            CCreature* const targetCreature = target->CastCreature();
            int targetType = -1;
            if (targetCreature->IsPC()) {
                targetType = 0;
            } else if (targetCreature->IsSummonOrPet()) {
                targetType = 1;
            } else if (targetCreature->IsNPC()) {
                targetType = 2;
            }

            const std::uint32_t levelAndSubLevelBits =
                (static_cast<std::uint32_t>(subLevel) << 16U) | static_cast<std::uint32_t>(level);
            std::int32_t levelAndSubLevel = 0;
            std::memcpy(&levelAndSubLevel, &levelAndSubLevelBits, sizeof(levelAndSubLevel));

            CLogFormatter logFormatter{};
            logFormatter.WriteLog2_1(
                0x7d, master, targetCreature, GetClassId(), Level(), skillId, levelAndSubLevel, targetType, 0, 0
            );
        }
    }
    return true;
}

void CNPC::OnUseSkillFinished(int targetSid, int skillId, int skillLevel, int skillSubLevel, int success) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send(
        "cddQd", 0x19, static_cast<int>(m_nId), targetSid, PackNpcSkillParameters(skillId, skillLevel, skillSubLevel),
        success
    );
}

void CNPC::OnUseSkillStarted(int targetSid, int skillId, int skillLevel, int skillSubLevel) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send(
        "cddQ", 0xa7, static_cast<int>(m_nId), targetSid, PackNpcSkillParameters(skillId, skillLevel, skillSubLevel)
    );
}

void CNPC::_OnAbnormalStatusChanged(
    const CSkillInfo& skillInfo, CCreature* const caster, const SkillPumpStartType startType, const bool unknown
) {
    // Current L2Server 0x008828f0.
    L2_GUARD_FUNCTION();
    CCreature::_OnAbnormalStatusChanged(skillInfo, caster, startType, unknown);
    const int sourceId = caster != nullptr ? static_cast<int>(caster->m_nId) : static_cast<int>(m_nId);
    g_npcserver.Send(
        "cddddd", 0x60, static_cast<int>(m_nId), skillInfo.m_nSkillId, skillInfo.m_nLevel, skillInfo.m_nSubLevel, sourceId
    );
}

void CNPC::_OnAbnormalStatusDeleted(
    const CSkillInfo& skillInfo, CCreature* const caster, const SkillPumpEndType endType, const int unknown
) {
    // Current L2Server 0x00882a00.
    L2_GUARD_FUNCTION();
    CCreature::_OnAbnormalStatusDeleted(skillInfo, caster, endType, unknown);
    const int sourceId = caster != nullptr ? static_cast<int>(caster->m_nId) : static_cast<int>(m_nId);
    g_npcserver.Send("cddddd", 0x60, static_cast<int>(m_nId), skillInfo.m_nSkillId, 0, 0, sourceId);
}

void CNPC::OnCorpseDecayed() {
    L2_GUARD_FUNCTION();
    if (m_data->m_bDecayComplete) {
        Delete();
    }
}

void CNPC::Despawn(bool byScript, bool setDeadIfAlive) {
    L2_GUARD_FUNCTION();

    if (IsWorldTrap() && IsInRealWorld()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"WorldTrap should not despawn")
        // L2_LOG_ERROR(Log,
        //     L"WorldTrap should not despawn")
        L2_LOG_ERROR(
            Log,
            "Despawn requested for a world trap in the real world: NPC class=%d, object=%u, by script=%d, set dead if "
            "alive=%d",
            GetClassId(), static_cast<unsigned int>(m_nId), byScript, setDeadIfAlive
        );
    }

    if (setDeadIfAlive) {
        {
            L2_GUARD(*m_lock);
            if (m_data->m_bIsAlive == 0) {
                return;
            }
            m_data->SetAlive(false);
        }
    }

    m_bDespawnByScript = byScript;
    if (IsNPCRelatedToPledge() && GetClassId() == 0x0f8d85) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, static_cast<int>(m_data->m_nPledgeID));
        if (pledge) {
            pledge->SendSystemMessageToMembersWithNpcId(SystemMessageId::CLAN_FLAG_DISAPPEARED, GetClassId());
        }
    }

    Delete();
}

bool CNPC::FreezeIfNobodyNearMe() {
    L2_GUARD_FUNCTION();

    if (m_data->m_cNoSleepMode == 0 && IsInRealWorld() && SharedUserMapDensity::Inst().CountByPos(m_data->m_Pos) == 0 &&
        m_data->m_bIsAlive != 0) {
        if (!m_bIsFreezing) {
            m_bIsFreezing = true;
            runtime::raw_atomic::DecrementI32(&g_nHeartbeatingCreatureNum);
        }
        AddTimer(3000, 0);
        return true;
    }
    return false;
}

void CNPC::CacheMasterInfo(User* user) {
    L2_GUARD_FUNCTION();

    const bool changed = m_data->m_cIsGuilty != user->m_data->m_cIsGuilty || m_data->m_nKarma != user->m_data->m_nKarma;
    m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(m_data->m_cIsGuilty, user->m_data->m_cIsGuilty, 38);
    SetKarma(user->m_data->m_nKarma);
    if (changed) {
        ParamChanged(NPCT_MASTER_INFO);
    }
}

void CNPC::ValidateMeal() {}

User* CNPC::GetMaster() const {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_data->m_pMaster.id));
    if (creature && creature->IsPC()) {
        return creature->CastUser();
    }
    return nullptr;
}

void CNPC::SetMaster(User* master) {
    L2_GUARD_FUNCTION();
    m_data->m_pMaster = master;
    m_data->m_BossID = static_cast<std::int32_t>(master->m_nId);
}

User* CNPC::GetSummoner() const {
    L2_GUARD_FUNCTION();
    if (m_data->m_SummonerID == 0) {
        return nullptr;
    }
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_data->m_SummonerID);
    if (creature && creature->IsPC()) {
        return creature->CastUser();
    }
    return nullptr;
}

int CNPC::GetDoppelgangerSummonerSID() { return 0; }

void CNPC::OnNpcSocketClose() {
    L2_GUARD_FUNCTION();
    Delete();
}

bool CNPC::HasSummoner() { return m_bHasSummoner || GetMaster() != nullptr; }

CCreatureReleaseVerifier::CCreatureReleaseVerifier(CCreature* creature) { AsCreatureObject(creature)->SetDeleting(true); }

CNPCReleaseVerifier::CNPCReleaseVerifier(CNPC* npc, const wchar_t* caller) :
    CCreatureReleaseVerifier(static_cast<CCreature*>(npc)), m_pNPC(npc), m_guardStr(caller) {
    L2_ADD_REF(AsNpcObject(m_pNPC), ObjectRefType::VerifierRetain);
    AddTimer(5000, 0);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CNPCReleaseVerifier::TimerExpired(int) {
    CObject* const npc = AsNpcObject(m_pNPC);
    const std::int32_t remaining = L2_RELEASE(npc, ObjectRefType::VerifierRetain);
    if (remaining > 0) {
        const std::int32_t refCount = npc->RefCount();
        const std::string portableNpcName = ::Utf16ToUtf8(npc->GetName());
        const std::string portableCaller = ::WideToUtf8(std::wstring_view(m_guardStr));
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][%s][%d] ORT - NPC Release failed. "
        //     "npc[%s], refcount[%d] Caller is [%s]",
        //     kNpcSource,
        //     0x1d5,
        //     portableNpcName.c_str(),
        //     refCount,
        //     portableCaller.c_str())
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][%s][%d] ORT - NPC Release failed. "
        //     "npc[%s], refcount[%d] Caller is [%s]",
        //     kNpcSource,
        //     0x1d5,
        //     portableNpcName.c_str(),
        //     refCount,
        //     portableCaller.c_str())
        L2_LOG_ERROR(
            Log,
            "NPC still referenced after release verification: name=%s, release result=%d, current references=%d, caller=%s",
            portableNpcName.c_str(), static_cast<int>(remaining), static_cast<int>(refCount), portableCaller.c_str()
        );
        npc->ReportOrt(kNpcSource, 0x1d9);
        g_ServerContext.NotReleasedNpc.fetch_add(1, std::memory_order_seq_cst);
    }
}
