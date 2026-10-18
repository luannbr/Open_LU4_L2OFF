#include "DB.h"
#include "Agit.h"
#include "Alliance.h"
#include "AuctionAgit.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "CacheContextManager.h"
#include "Castle.h"
#include "CompeteAgit.h"
#include "ControlTower.h"
#include "CursedWeapon.h"
#include "DBSocket.h"
#include "DailyMission.h"
#include "Dominion.h"
#include "Door.h"
#include "Fortress.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "MiniGameAgit.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "PCParam.h"
#include "ParserExpData.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Setting.h"
#include "Shared/src/MemorySub.h"
#include "SiegeAgit.h"
#include "TeamBattleAgit.h"
#include "User.h"
#include "UserSocket.h"
#include "WareHouse.h"

#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MailListParameter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/net/Serialize.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/proto/Stream.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

void CDB::RequestSetUserNickname(User* const actor, User* const target, const char16_t* const nickname) {
    // Current L2Server 0x006454fc.
    L2_GUARD_FUNCTION();
    if (actor->m_data->m_nWorldId != target->m_data->m_nWorldId) {
        return;
    }

    // Preserve the current virtual-call order before publishing request 0x25.
    const std::uint32_t targetDatabaseId = target->GetDBID();
    const std::uint32_t actorDatabaseId = actor->GetDBID();
    const std::uint32_t targetServerId = target->GetServerId();
    const std::uint32_t actorServerId = actor->GetServerId();
    actor->SendDB(
        "cddddS", 0x25, actorServerId, targetServerId, actorDatabaseId, targetDatabaseId,
        nickname
    );
}

namespace {

// Diagnostic labels only. Job IDs and completion signaling remain unchanged.
const wchar_t* DatabaseJobLabel(JobFinishType type) noexcept {
    switch (type) {
        case JFT_LOAD_DB_RELATED_COUNTS:
            return L"database record counts";
        case JFT_LOAD_ALLIANCE:
            return L"alliances";
        case JFT_LOAD_CASTLE:
            return L"castles";
        case JFT_LOAD_AGIT:
            return L"clan halls";
        case JFT_LOAD_FORTRESS:
            return L"fortresses";
        case JFT_LOAD_DISMISS_RESERVED:
            return L"scheduled clan dismissals";
        case JFT_LOAD_WAR:
            return L"clan wars";
        case JFT_LOAD_NOBLESS:
            return L"noblesse data";
        case JFT_LOAD_PLEDGE:
            return L"clans";
        case JFT_LOAD_OLYMPIAD:
            return L"Olympiad data";
        case JFT_LOAD_PLEDGEGAME:
            return L"clan games";
        case JFT_LOAD_DOMINION:
            return L"dominions";
        case JFT_LOAD_NOBLESS_MATCH_COUNT:
            return L"noblesse match counts";
        case JFT_LOAD_FISHING_CHAMPIONSHIP:
            return L"fishing championship";
        case JFT_WAIT_PLEDGE_RESERVE:
            return L"clan reservation";
        case JFT_LOAD_WORLD_OLYMPIAD_USER:
            return L"world Olympiad participants";
        default:
            return L"unknown job";
    }
}


struct NewbieCheckRequest final : CStackOnly {
    std::int32_t serverId;
    std::int32_t databaseId;
    std::int32_t accountId;
    std::int32_t level;
    std::int32_t reserved;
};


void* const kInvalidSignal = reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());

#if !defined(_WIN32)
struct PortableAutoResetEvent final {
    std::mutex lock;
    std::condition_variable changed;
    bool signaled = false;
};
#endif

void* CreateSignal() noexcept {
#if defined(_WIN32)
    return ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
#else
    return new (std::nothrow) PortableAutoResetEvent;
#endif
}

void CloseSignal(void* signal) noexcept {
#if defined(_WIN32)
    ::CloseHandle(static_cast<HANDLE>(signal));
#else
    if (signal && signal != kInvalidSignal) {
        delete static_cast<PortableAutoResetEvent*>(signal);
    }
#endif
}

bool SetSignal(void* signal) noexcept {
#if defined(_WIN32)
    return ::SetEvent(static_cast<HANDLE>(signal)) != FALSE;
#else
    if (!signal || signal == kInvalidSignal) {
        return false;
    }
    auto& event = *static_cast<PortableAutoResetEvent*>(signal);
    {
        std::lock_guard<std::mutex> guard(event.lock);
        event.signaled = true;
    }
    event.changed.notify_one();
    return true;
#endif
}

bool WaitSignal(void* signal, std::uint32_t timeoutMilliseconds) noexcept {
#if defined(_WIN32)
    return ::WaitForSingleObject(static_cast<HANDLE>(signal), timeoutMilliseconds) == WAIT_OBJECT_0;
#else
    if (!signal || signal == kInvalidSignal) {
        return false;
    }
    auto& event = *static_cast<PortableAutoResetEvent*>(signal);
    std::unique_lock<std::mutex> guard(event.lock);
    bool finished = false;
    if (timeoutMilliseconds == std::numeric_limits<std::uint32_t>::max()) {
        event.changed.wait(guard, [&event] { return event.signaled; });
        finished = true;
    } else {
        finished = event.changed.wait_for(guard, std::chrono::milliseconds(timeoutMilliseconds), [&event] {
            return event.signaled;
        });
    }
    if (finished) {
        event.signaled = false;
    }
    return finished;
#endif
}

int EffectiveWorldId(int requestedWorldId) { return ::CThreadManager::GetThreadID() < 16 ? requestedWorldId : g_nWorldId; }

// Noise: the original file label was used only by the replaced human diagnostics below.
// constexpr wchar_t kDatabaseSourceFile[] = L"db.cpp"
volatile std::int32_t tempcount2 = 0;

int CurrentDatabaseLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

void ReportMissingWorldId() { SystemMail::sendNotSetThreadLocalWorldID(mailServer); }

} // namespace

CDB db;
CJobFinishSignal g_JobFinishSignal;

void CDB::RequestChangeSubJob(User* user, int subJobId) {
    L2_GUARD_FUNCTION();

    CVitality* const vitality = user->m_pVitality;
    const int vitalPoint = vitality->m_Data.vitalPoint;
    const int restoreCount = vitality->m_Data.restoreCount;
    const int latestResetDateVP = vitality->m_Data.latestResetDate_VP;
    const int latestResetDateRC = vitality->m_Data.latestResetDate_RC;
    CSharedCreatureData* const data = user->m_data;

    user->SendDB(
        "cdddddddffQQddddddddd", 0xde, user->GetServerId(), user->GetDBID(), 1, 0, 0, subJobId, data->m_nSubJobId,
        data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp, data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2,
        data->m_HennaId_3, vitalPoint, restoreCount, latestResetDateVP, latestResetDateRC, data->GetAP()
    );
}

void CDB::RequestCreateSubJob(User* const user, const int subJobId, const int newClassId, const bool force) {
    // Current L2Server 0x0064fffc.
    L2_GUARD_FUNCTION();
    if (!force) {
        int jobClassLevel = 0;
        if (!User::GetJobClassLevel(newClassId, &jobClassLevel) || jobClassLevel != 2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid subjob change trial. newClass = %d",
            //     newClassId)
            // Log.Add(
            //     LOG_WARN,
            //     L"[class] Subclass creation not requested for character DB ID %u, subclass slot %d: class %d failed
            //     validation (job-level value %d).", user != nullptr ? user->GetDBID() : 0U, subJobId, newClassId,
            //     jobClassLevel)
            L2_LOG_WARN(
                Log,
                u"[class] Subclass creation not requested for character DB ID %u, subclass slot %d: class %d failed "
                u"validation (job-level value %d).",
                user != nullptr ? user->GetDBID() : 0U, subJobId, newClassId, jobClassLevel
            );
            return;
        }
    }

    CSharedCreatureData* const data = user->m_data;
    user->SendDB(
        "cddddddddffQQddddd", 0xdc, user->GetServerId(), user->GetDBID(), 1, 0, 0, subJobId, newClassId, data->m_nSubJobId,
        data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp, data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2,
        data->m_HennaId_3, data->GetAP()
    );
}

void CDB::RequestCreateSubJobForNpc(
    User* const user, const int npcSid, const int userSid, const int subJobId, const int newClassId
) {
    // Current L2Server 0x006501fc.
    L2_GUARD_FUNCTION();
    int jobClassLevel = 0;
    if (!User::GetJobClassLevel(newClassId, &jobClassLevel) || jobClassLevel != 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid subjob change trial. newClass = %d",
        //     newClassId)
        // Log.Add(
        //     LOG_WARN,
        //     L"[class] NPC subclass creation not requested for character DB ID %u, subclass slot %d: class %d failed
        //     validation (job-level value %d).", user != nullptr ? user->GetDBID() : 0U, subJobId, newClassId,
        //     jobClassLevel)
        L2_LOG_WARN(
            Log,
            u"[class] NPC subclass creation not requested for character DB ID %u, subclass slot %d: class %d failed "
            u"validation (job-level value %d).",
            user != nullptr ? user->GetDBID() : 0U, subJobId, newClassId, jobClassLevel
        );
        return;
    }

    CSharedCreatureData* const data = user->m_data;
    user->SendDB(
        "cddddddddffQQddddd", 0xdc, user->GetServerId(), user->GetDBID(), 0, npcSid, userSid, subJobId, newClassId,
        data->m_nSubJobId, data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp, data->m_nLevel, data->m_HennaId_1,
        data->m_HennaId_2, data->m_HennaId_3, data->GetAP()
    );
}

void CDB::RequestChangeSubJobForNpc(User* const user, const int npcSid, const int userSid, const int subJobId) {
    // Current L2Server 0x00650d24.
    L2_GUARD_FUNCTION();
    CVitality* const vitality = user->m_pVitality;
    const int vitalPoint = vitality->m_Data.vitalPoint;
    const int restoreCount = vitality->m_Data.restoreCount;
    const int latestResetDateVP = vitality->m_Data.latestResetDate_VP;
    const int latestResetDateRC = vitality->m_Data.latestResetDate_RC;
    CSharedCreatureData* const data = user->m_data;

    user->SendDB(
        "cdddddddffQQddddddddd", 0xde, user->GetServerId(), user->GetDBID(), 0, npcSid, userSid, subJobId, data->m_nSubJobId,
        data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp, data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2,
        data->m_HennaId_3, vitalPoint, restoreCount, latestResetDateVP, latestResetDateRC, data->GetAP()
    );
}

void CDB::RequestRenewSubJob(
    User* const user,
    const int subJobId,
    const int newClassId,
    const int renewType,
    const bool force,
    const int npcSid,
    const int userSid
) {
    // Current L2Server 0x006504e0.
    L2_GUARD_FUNCTION();
    if (!force) {
        int jobClassLevel = 0;
        if (!User::GetJobClassLevel(newClassId, &jobClassLevel) || (renewType != 1 && jobClassLevel != 2)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid subjob change trial. newClass = %d",
            //     newClassId)
            // Log.Add(
            //     LOG_WARN,
            //     L"[class] Subclass renewal not requested for character DB ID %u, subclass slot %d: class %d failed
            //     validation (job-level value %d). Renewal type: %d.", user != nullptr ? user->GetDBID() : 0U, subJobId,
            //     newClassId,
            //     jobClassLevel,
            //     renewType)
            L2_LOG_WARN(
                Log,
                u"[class] Subclass renewal not requested for character DB ID %u, subclass slot %d: class %d failed "
                u"validation (job-level value %d). Renewal type: %d.",
                user != nullptr ? user->GetDBID() : 0U, subJobId, newClassId, jobClassLevel, renewType
            );
            return;
        }
    }

    CVitality* const vitality = user->m_pVitality;
    const int vitalPoint = vitality->m_Data.vitalPoint;
    const int restoreCount = vitality->m_Data.restoreCount;
    const int latestResetDateVP = vitality->m_Data.latestResetDate_VP;
    const int latestResetDateRC = vitality->m_Data.latestResetDate_RC;
    CSharedCreatureData* const data = user->m_data;

    int oldClassId = -1;
    switch (subJobId) {
        case 0:
            oldClassId = data->m_nSubJob0Class;
            break;
        case 1:
            oldClassId = data->m_nSubJob1Class;
            break;
        case 2:
            oldClassId = data->m_nSubJob2Class;
            break;
        case 3:
            oldClassId = data->m_nSubJob3Class;
            break;
        default:
            break;
    }

    user->SendDB(
        "chddddddddddffQQddddddddd", 0xea, 0x16, user->GetServerId(), user->GetDBID(), force, npcSid, userSid, subJobId,
        renewType, newClassId, data->m_nSubJobId, oldClassId, data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp,
        data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2, data->m_HennaId_3, vitalPoint, restoreCount, latestResetDateVP,
        latestResetDateRC, data->GetAP()
    );
}

void CDB::RequestStartTraniningZone(User* const user) {
    // Current L2Server 0x004c7da0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const FVector position = user->GetPos();
    user->SendDB(
        "chddddd", 0xea, 0x17e, static_cast<int>(user->GetDBID()), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), runtime::crt::Time32Now()
    );
}

void CDB::RequestEndTraniningZone(User* const user, const TrainingZone::EndReason reason, const int endTime) {
    // Current L2Server 0x004c7eb8.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chddd", 0xea, 0x17f, static_cast<int>(user->GetDBID()), static_cast<int>(reason), endTime);
    }
}

void CDB::RequestRewardTraniningZone(User* const user) {
    // Current L2Server 0x004c7f9c.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chd", 0xea, 0x180, static_cast<int>(user->GetDBID()));
    }
}

void CDB::RequestLoadTraniningZone(User* const user) {
    // Current L2Server 0x004c8068.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chddd", 0xea, 0x181, static_cast<int>(user->GetDBID()), 0, 0);
    }
}

void CDB::RequestCreateDualJob(
    User* const user,
    const int dualClassId,
    const int unknownAlwaysOne,
    const bool createFlag,
    const int npcSid,
    const int userSid
) {
    // Current L2Server 0x006507ac. The second integer argument is present in
    // the source ABI but the current body emits the literal one instead.
    L2_GUARD_FUNCTION();
    static_cast<void>(unknownAlwaysOne);
    CSharedCreatureData* const data = user->m_data;
    user->SendDB(
        "chddddddddffQQddddd", 0xea, 0x18d, static_cast<int>(user->GetServerId()), static_cast<int>(user->GetDBID()),
        createFlag, npcSid, userSid, 1, dualClassId, data->m_nSubJobId, data->m_dHP, data->m_dMP, data->m_nSP, data->m_nExp,
        data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2, data->m_HennaId_3, data->GetAP()
    );
}

void CDB::RequestSaveCharacterForSubJob(User* const user) {
    // Current L2Server 0x00650f18.
    L2_GUARD_FUNCTION();

    CSharedCreatureData* const data = user->m_data;
    user->SendDB(
        "cddddffffffQQddddd", 0xe1, static_cast<int>(user->GetDBID()), data->m_nSubJobId, static_cast<int>(data->m_nClass),
        data->m_DualClassID, data->m_dHP, data->m_dMP, data->m_dOrgHP, data->m_dOrgMP, data->m_dCP, data->m_dOrgCP,
        data->m_nSP, data->m_nExp, data->m_nLevel, data->m_HennaId_1, data->m_HennaId_2, data->m_HennaId_3, data->GetAP()
    );
}

void CDB::RequestSaveAcquiredSkills(User* const user, std::map<int, AcquiredSkillInfo>& acquiredSkills) {
    // Current L2Server 0x0066b9e0.
    L2_GUARD_FUNCTION();

    std::array<char, 0x2004> packet;
    int packetLength = 0;
    int count = 0;
    for (const auto& acquiredSkill : acquiredSkills) {
        const AcquiredSkillInfo& info = acquiredSkill.second;
        packetLength += ::Assemble(
            packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "ddd", info.m_nId, info.m_nLevel,
            info.m_nSubLevel
        );
        ++count;
    }

    if (count != 0) {
        user->SendDB(
            "cdddb", 0xe3, static_cast<int>(user->GetDBID()), user->m_data->m_nSubJobId, count, packetLength, packet.data()
        );
    } else {
        user->SendDB("cddd", 0xe3, static_cast<int>(user->GetDBID()), user->m_data->m_nSubJobId, 0);
    }
}

void CDB::SendSaveCharacterInfo(User* user, bool forceSave, bool saveCharacterInfo) const {
    L2_GUARD_FUNCTION();

    if (!forceSave && !user->m_data->m_bIsActive) {
        if (runtime::raw_atomic::IncrementI32(&tempcount2) < 3) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"not active user try save. dbid[%d]",
            //     user->GetDBID())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[db] Character save skipped for DB ID %u: character is inactive and force-save is disabled.",
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log, u"[db] Character save skipped for DB ID %u: character is inactive and force-save is disabled.",
                user->GetDBID()
            );
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        return;
    }

    if (saveCharacterInfo) {
        CSharedCreatureData* const data = user->m_data;
        double hp = data->GetHP();
        double mp = data->GetMP();
        double cp = data->GetCP();
        int x = 0;
        int y = 0;
        int z = 0;
        user->GetSavingPos(x, y, z);
        if (user->IsInDuel()) {
            user->m_DuelUserInfo.GetOriginalStatus(hp, mp, cp);
        }

        user->SendDB(
            "cdddddddffffffQQddddddddddddddddd", 7, user->GetDBID(), 0, data->m_nBuilder, static_cast<int>(data->m_nClass),
            x, y, z, hp, mp, data->m_dBaseHP, data->m_dBaseMP, cp, data->m_dBaseCP, data->m_nSP, data->m_nExp,
            data->m_nLevel, data->m_nKarma, data->m_nPKCount, data->m_nDuelCount, data->m_nPKPardonCount, data->m_nFace,
            data->m_nHairShape, data->m_nHairColor, user->IsInVehicle(), data->m_nSubJobId, data->m_nSubJob0Class,
            data->m_nSubJob1Class, data->m_nSubJob2Class, data->m_nSubJob3Class, data->m_DualClassID, data->m_nSSQDawnRound,
            data->GetAP()
        );
    }

    int useTime = 0;
    if (user->m_nUseTimeStamp > 0) {
        useTime = runtime::crt::Time32Now() - user->m_nUseTimeStamp;
    }
    user->GetSocket()->SetUseTime(useTime);
}

void CDB::RequestSaveSummon(User& user) {
    // Current L2Server.exe 0x0066be68, original owner db.obj.
    L2_GUARD_FUNCTION();

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(&user, summon, const bool isPet = summon->IsPet();
        const int objectId = isPet ? static_cast<int>(summon->GetDBID()) : summon->m_nIndexForMaster;

        user.SendDB(
            "chdddddddffddd", 0xea, 0xc6, user.GetDBID(), summon->m_data->m_nClassID, objectId, summon->m_data->m_nLevel,
            summon->m_nOrgDuration / 1000, summon->GetCurrentLife() / 1000, summon->m_data->m_nMasterExpRatio,
            summon->m_data->m_dHP, summon->m_data->m_dMP, summon->m_nConsumeItemClassID,
            static_cast<int>(summon->m_nConsumeItemCount), !isPet
        ););
}

void CDB::RequestLoadSummonAbnormalStatus(User& user, int summonClassId, int summonIndex) {
    // Current L2Server 0x0065a2c8.
    L2_GUARD_FUNCTION();
    user.SendDB(
        "chdddd", 0xea, 0xc5, static_cast<int>(user.GetDBID()), static_cast<int>(user.Id()), summonClassId, summonIndex
    );
}

void CDB::SaveCharacterSlotInfo(User* user) const {
    // Current L2Server 0x0066a990, original owner db.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    std::array<int, 64> itemDatabaseIds{};
    CItemFactory& itemFactory = CItemFactory::Inst();
    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSPointer<CItem> item = itemFactory.FindObjectSP(user->m_data->m_nSlotIndex[slot]);
        itemDatabaseIds[slot] = item ? static_cast<int>(item->GetDBID()) : 0;
    }

    int gloves = itemDatabaseIds[ST_GLOVES];
    int chest = itemDatabaseIds[ST_CHEST];
    int legs = itemDatabaseIds[ST_LEGS];
    int feet = itemDatabaseIds[ST_FEET];
    CSPointer<CItem> onePiece = itemFactory.FindObjectSP(user->m_data->m_nSlotIndex[ST_ONEPIECE]);
    if (onePiece) {
        chest = static_cast<int>(onePiece->GetDBID());
        legs = chest;
    }
    CSPointer<CItem> allDress = itemFactory.FindObjectSP(user->m_data->m_nSlotIndex[ST_ALLDRESS]);
    if (allDress) {
        chest = static_cast<int>(allDress->GetDBID());
        gloves = chest;
        legs = chest;
        feet = chest;
    }

    int hair = itemDatabaseIds[ST_HAIR];
    int hair2 = itemDatabaseIds[ST_HAIR2];
    CSPointer<CItem> hairAll = itemFactory.FindObjectSP(user->m_data->m_nSlotIndex[ST_HAIRALL]);
    if (hairAll) {
        hair = static_cast<int>(hairAll->GetDBID());
        hair2 = hair;
    }

    user->SendDB(
        "chddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd", 0xea, 0x62, user->GetDBID(), itemDatabaseIds[0],
        itemDatabaseIds[1], itemDatabaseIds[2], itemDatabaseIds[3], itemDatabaseIds[4], itemDatabaseIds[5],
        itemDatabaseIds[6], itemDatabaseIds[7], itemDatabaseIds[8], gloves, chest, legs, feet, itemDatabaseIds[13],
        itemDatabaseIds[14], hair, hair2, itemDatabaseIds[20], itemDatabaseIds[21], itemDatabaseIds[22], itemDatabaseIds[23],
        itemDatabaseIds[24], itemDatabaseIds[25], itemDatabaseIds[26], itemDatabaseIds[27], itemDatabaseIds[28],
        itemDatabaseIds[29], itemDatabaseIds[30], itemDatabaseIds[31], itemDatabaseIds[32], itemDatabaseIds[33],
        itemDatabaseIds[34], itemDatabaseIds[35], itemDatabaseIds[36], itemDatabaseIds[37], itemDatabaseIds[38],
        itemDatabaseIds[39], itemDatabaseIds[40], itemDatabaseIds[41], itemDatabaseIds[42], itemDatabaseIds[43],
        itemDatabaseIds[44], itemDatabaseIds[45], itemDatabaseIds[46], itemDatabaseIds[47], itemDatabaseIds[48],
        itemDatabaseIds[49], itemDatabaseIds[50], itemDatabaseIds[51], itemDatabaseIds[52], itemDatabaseIds[53],
        itemDatabaseIds[54], itemDatabaseIds[55], itemDatabaseIds[56], itemDatabaseIds[57], itemDatabaseIds[58],
        itemDatabaseIds[59], itemDatabaseIds[60], itemDatabaseIds[61], itemDatabaseIds[62]
    );
}

void CDB::SendSaveItemInfo(User* user) const {
    // Current L2Server 0x0066d740.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> itemPacket;
    {
        GuardHelper stage(L"cdb::sendsaveiteminfo_1", CThreadManager::GetThreadID());
        if (!user->m_data->m_bIsActive) {
            if (runtime::raw_atomic::IncrementI32(&tempcount2) < 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"not active user try save item. dbid[%d]",
                //     user->GetDBID())
                // Log.Add(
                //     LOG_ERROR,
                //     L"[db] Item save skipped for character DB ID %u: character is inactive.",
                //     user->GetDBID())
                L2_LOG_ERROR(Log, u"[db] Item save skipped for character DB ID %u: character is inactive.", user->GetDBID());
                runtime::process::ReportCallStack(LOG_ERROR, false);
            }
            return;
        }
    }

    int itemCount;
    {
        std::optional<GuardHelper> lockStage;
        lockStage.emplace(L"cdb::sendsaveiteminfo_readlock1", CThreadManager::GetThreadID());
        {
            L2_READ_GUARD(user->m_inv);
            lockStage.reset();
            itemCount = 0;
            {
                GuardHelper stage(L"cdb::sendsaveiteminfo_2", CThreadManager::GetThreadID());
                for (CSharedItemData* itemData = user->m_inv.GetFirst(); itemData != nullptr;) {
                    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
                    if (item) {
                        CItemAssemblerSendToCacheD assembler(*item.get(), 0);
                        assembler.AssembleItemInfo(itemPacket);
                        ++itemCount;
                    }

                    itemData = itemData->Next();
                    if (itemCount > 0x191) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%s][%d] depth %d>=%d char_id[%d]",
                        //     "db.cpp",
                        //     0x274,
                        //     itemCount,
                        //     0x192,
                        //     user->GetDBID())
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[db] Item save for character DB ID %u reached the 402-item cutoff after processing %d item
                        //     objects. Scanning stops and the partial list is passed to the save request.", user->GetDBID(),
                        //     itemCount)
                        L2_LOG_ERROR(
                            Log,
                            u"[db] Item save for character DB ID %u reached the 402-item cutoff after processing %d item "
                            u"objects; scanning stops and the partial list is passed to the save request.",
                            user->GetDBID(), itemCount
                        );
                        break;
                    }
                }
            }

            lockStage.emplace(L"cdb::sendsaveiteminfo_readunlock", CThreadManager::GetThreadID());
        }
    }

    {
        GuardHelper stage(L"cdb::sendsaveiteminfo_socket_send", CThreadManager::GetThreadID());
        const int privateStoreTime = user->GetPrivateStoreTime();
        int partyMemberCount = 0;
        int partyLevelSum = 0;
        if (CParty* const party = user->GetParty()) {
            partyMemberCount = party->m_nMemberCount;
            partyLevelSum = party->m_nLevSum;
        }
        const int inventoryCount = user->m_inv.GetTotalCount();
        const int inventoryLimit = user->GetInventoryLimit();
        const int questInventoryLimit = user->GetQuestInventoryLimit();
        const int carryingWeight = user->m_inv.GetCarryingWeight();
        const int carryWeight = runtime::Win64CvttDoubleToInt32(user->CarryWeight());
        const int artifactInventoryCount = user->m_inv.GetArtifactInventoryCount();
        const int artifactInventoryLimit = user->GetArtifactInventoryLimit();
        const std::uint32_t databaseId = user->GetDBID();
        const int questInventoryCount = user->m_data->m_nCurrentQuestInventoryNum;

        user->SendDB(
            "cddbddddddddddd", 8, databaseId, itemCount, itemPacket.GetLength(), itemPacket.GetPacket(), partyMemberCount,
            partyLevelSum, privateStoreTime, inventoryCount, inventoryLimit, questInventoryCount, questInventoryLimit,
            carryingWeight, carryWeight, artifactInventoryCount, artifactInventoryLimit
        );
    }
    db.SaveItemAgathionEnergy(user);
}

void CDB::SendLogout(User* user) const {
    // Current L2Server 0x0066f664.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_bIsActive) {
        SendSaveItemInfo(user);
    }
}

void CDB::ReqLogoutCharacter(User* user, int characterUseTime, _SYSTEMTIME* localTime) {
    // Current L2Server 0x006449b8.
    L2_GUARD_FUNCTION();
    if (user->m_nClassId == 0x88) {
        // Noise: this class-specific logout trace is not a failure. It repeats normal logout entry without an actionable
        // condition. Log.Add(
        //     LOG_ERROR,
        //     "[NO_ERROR] solasys Leaveworld inzone check")
        // Noise: this stack belongs only to the normal class-specific logout trace above.
        // runtime::process::ReportCallStack(2, false)
    }
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    int useTime = 0;
    if (user->m_nUseTimeStamp > 0) {
        useTime = runtime::crt::Time32Now() - user->m_nUseTimeStamp;
    }
    const ::in_addr address = socket->GetAddress();
    const auto* const bytes = reinterpret_cast<const std::uint8_t*>(&address.s_addr);
    user->SendDB(
        "cdddbddddQ", 0x16, static_cast<int>(user->GetDBID()), useTime, characterUseTime, 0x10, localTime,
        static_cast<int>(bytes[0]), static_cast<int>(bytes[1]), static_cast<int>(bytes[2]), static_cast<int>(bytes[3]),
        socket->GetMacAddress()
    );
}

void CDB::ReqLoginCharacter(User& user) {
    // Current L2Server 0x006447f4. Preserve the repeated socket reads and
    // their target order: the socket binding is allowed to change here.
    L2_GUARD_FUNCTION();
    if (!user.GetSocket()->IsUnderReloading() && user.m_data->m_nWorldId == g_nWorldId) {
        const bool pcCafeUser = user.m_data->m_bPCCafeUser;
        const std::int64_t macAddress = user.GetSocket()->GetMacAddress();
        const std::uint8_t address3 = reinterpret_cast<const std::uint8_t*>(&user.GetSocket()->m_Addr.s_addr)[3];
        const std::uint8_t address2 = reinterpret_cast<const std::uint8_t*>(&user.GetSocket()->m_Addr.s_addr)[2];
        const std::uint8_t address1 = reinterpret_cast<const std::uint8_t*>(&user.GetSocket()->m_Addr.s_addr)[1];
        const std::uint8_t address0 = reinterpret_cast<const std::uint8_t*>(&user.GetSocket()->m_Addr.s_addr)[0];
        const int premiumLevel = user.GetSocket()->m_iPremiumLevel;
        const std::uint32_t databaseId = user.GetDBID();
        user.SendDB(
            "cdddddddQ", 0x15, databaseId, pcCafeUser, premiumLevel, address0, address1, address2, address3, macAddress
        );
    }
}

void CDB::RequestSetCreateDate(
    User* const user, const int year, const int month, const int day, const int hour, const int minute
) {
    // Current L2Server 0x004c7400.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdddddd", 0xea, 0xe3, user->GetDBID(), year, month, day, hour, minute);
    }
}

void CDB::RequestResetShopLimit(User* const user, const int shopIndex) {
    // Current L2Server 0x004c8140.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x183, user->GetAccountID(), shopIndex);
    }
}

void CDB::RequestSaveBattleCampHp(CBattleCamp* const battleCamp) {
    // Current L2Server 0x006486b8.
    L2_GUARD_FUNCTION();
    battleCamp->SendDB("cdd", 0x66, battleCamp->GetDBID(), runtime::Win64CvttDoubleToInt32(battleCamp->GetHP()));
}

void CDB::EvolvePet(
    User* const user,
    CEtcItem* const petCollar,
    const int itemClassId,
    const int npcClassId,
    const std::int64_t experience,
    const int level,
    const int meal,
    const double x,
    const double y,
    const int unknown,
    const std::int64_t value1,
    const std::int64_t value2,
    const std::int64_t value3,
    const std::int64_t value4
) {
    // Current L2Server 0x0064a314. Preserve the target virtual-call order.
    L2_GUARD_FUNCTION();
    petCollar->m_bPetCollarTradable = false;
    const std::uint32_t collarDatabaseId = petCollar->GetDBID();
    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB(
        "cddcdddQddffdQQQQ", 0x76, userServerId, userDatabaseId, 0, collarDatabaseId, itemClassId, npcClassId, experience,
        level, meal, x, y, unknown, value1, value2, value3, value4
    );
}

void CDB::EvolvePetWithSameExp(
    User* const user,
    CEtcItem* const petCollar,
    const int itemClassId,
    const int npcClassId,
    const std::int64_t experience,
    const int level,
    const int meal,
    const int unknown
) {
    // Current L2Server 0x0064a4bc. Preserve the target virtual-call order.
    L2_GUARD_FUNCTION();
    petCollar->m_bPetCollarTradable = false;
    const std::uint32_t collarDatabaseId = petCollar->GetDBID();
    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB(
        "cddcdddQddffd", 0x76, userServerId, userDatabaseId, 1, collarDatabaseId, itemClassId, npcClassId, experience, level,
        meal, 0.0, 0.0, unknown
    );
}

void CDB::RequestUreadMailCount(User* const user) {
    // Current L2Server 0x006526f8. Retain the target's Uread spelling.
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t databaseId = user->GetDBID();
    user->SendDB("chdd", 0xea, 0x11, databaseId, serverId);
}

void CDB::RequestLoadUserPremiumItems(User* const user) {
    // Current L2Server 0x00657f44.
    L2_GUARD_FUNCTION();
    const std::uint32_t accountId = user->GetAccountID();
    const std::uint32_t serverId = user->GetServerId();
    user->SendDB("chdd", 0xea, 0x90, serverId, accountId);
}

void CDB::RequestBuyAirShip(
    User& user,
    const int residenceId,
    const int airShipId,
    const unsigned int itemId,
    const int itemCount,
    const unsigned int price
) {
    // Current L2Server 0x00659280.
    L2_GUARD_FUNCTION();
    if (user.m_data->m_nWorldId == g_nWorldId) {
        user.SendDB("chddddddd", 0xea, 0xb4, 0, user.GetServerId(), residenceId, airShipId, itemId, itemCount, price);
    }
}

void CDB::RequestCopyMacro(User* const user, const char16_t* const characterName) {
    // Current L2Server 0x0065b594.
    L2_GUARD_FUNCTION();
    const std::uint32_t databaseId = user->GetDBID();
    const std::uint32_t serverId = user->GetServerId();
    user->SendDB("chddS", 0xea, 0xfc, serverId, databaseId, characterName);
}

void CDB::RequestCopyBookmark(User* const user, const char16_t* const characterName) {
    // Current L2Server 0x0065b674.
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    user->SendDB("chdS", 0xea, 0xfd, serverId, characterName);
}

void CDB::RequestLoadCardUpdownGameRecord(User& user) {
    // Current L2Server 0x0066228c.
    L2_GUARD_FUNCTION();
    user.m_UpDownGame.OnRequestLoad();
    const std::uint32_t databaseId = user.GetDBID();
    user.SendDB("chd", 0xea, 0x1b5, databaseId);
}

void CDB::RequestNewbieCheck(User* const user) {
    // Current L2Server 0x006663d4.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    NewbieCheckRequest request{};
    request.serverId = user->GetServerId();
    request.databaseId = static_cast<std::int32_t>(user->GetDBID());
    request.accountId = static_cast<std::int32_t>(user->GetAccountID());
    request.level = user->Level();
    RequestTPacketProcess(user->m_data->m_nWorldId, 0, sizeof(request), request);
}

void CDB::RequestSaveNoblessType(User* const user, const int noblessType) {
    // Current L2Server 0x00666500.
    L2_GUARD_FUNCTION();
    DxSaveNoblessType request(static_cast<int>(user->GetDBID()), noblessType, 1000, 0, 0);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, static_cast<short>(0x1b), static_cast<int>(sizeof(request)), request);
}

void CDB::RequestUpdateOneDayRewardInfo(User* user, int updateType, const ICDailyMission* mission) {
    L2_GUARD_FUNCTION();
    if (!g_bUseOneDayReward) {
        return;
    }
    SendDB(
        g_nWorldId, "chddddddd", 0xea, 0x1ab, static_cast<int>(user->GetDBID()), updateType, mission->m_DBID,
        mission->m_UpdateCheckTime, mission->m_AccumulateCount, mission->m_StateType, mission->m_TimeRewarded
    );
}

CJobFinishSignal::CJobFinishSignal() :
    m_nLoadedNobless(0), m_nMustLoadWOlympiadUser(0), m_nLoadedWOlympiadUser(0) {
    L2_GUARD_FUNCTION();
    for (void*& signal : m_hJobFinishSignals) {
        signal = CreateSignal();
        if (!signal) {
            signal = kInvalidSignal;
            // Log.Add(
            //     LOG_ERROR,
            //     L"Cannot Create Job Finish Signal Event")
            // Log.Add(
            //     LOG_ERROR,
            //     L"[db] Cannot create completion signal for %ls (job %d). The slot remains invalid.",
            //     DatabaseJobLabel(static_cast<JobFinishType>(&signal - m_hJobFinishSignals)),
            //     static_cast<int>(&signal - m_hJobFinishSignals))
            L2_LOG_ERROR(
                Log, u"[db] Cannot create completion signal for %ls (job %d); the slot remains invalid.",
                DatabaseJobLabel(static_cast<JobFinishType>(&signal - m_hJobFinishSignals)),
                static_cast<int>(&signal - m_hJobFinishSignals)
            );
        }
    }

    m_nMustLoadAlliance = 0;
    m_nLoadedAlliance = 0;
    m_nMustLoadCastle = 0;
    m_nLoadedCastle = 0;
    m_nMustLoadAgit = 0;
    m_nLoadedAgit = 0;
    m_nMustLoadDismissReserved = 0;
    m_nLoadedDismissReserved = 0;
    m_nMustLoadWar = 0;
    m_nLoadedWar = 0;
    m_nMustLoadFortress = 0;
    m_nLoadedFortress = 0;
    m_nMustLoadNobless = 0;
    m_nMustLoadDominion = 0;
    m_nLoadedDominion = 0;
    m_nMustLoadMatchCount = 0;
    m_nLoadedMatchCount = 0;
}

CJobFinishSignal::~CJobFinishSignal() {
    L2_GUARD_FUNCTION();
    for (void* const signal : m_hJobFinishSignals) {
        CloseSignal(signal);
    }
}

bool CJobFinishSignal::WaitFinish(JobFinishType type, unsigned int timeoutSeconds) {
    L2_GUARD_FUNCTION();
    const std::uint32_t begin = runtime::GetTickCount();
    const std::uint32_t timeoutMilliseconds = timeoutSeconds * 1000U;
    const bool finished = WaitSignal(m_hJobFinishSignals[static_cast<std::int32_t>(type)], timeoutMilliseconds);
    const std::uint32_t durationSeconds = (runtime::GetTickCount() - begin) / 1000U;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] JobType: %d Duration: %d/%dsec  ",
    //     static_cast<std::int32_t>(type),
    //     static_cast<int>(durationSeconds),
    //     static_cast<int>(timeoutSeconds))
    // Log.Add(
    //     finished ? LOG_NORMAL : LOG_ERROR,
    //     L"[db] Wait for %ls (job %d): %ls after %u s. Requested timeout %u s, effective wait %u ms.",
    //     DatabaseJobLabel(type),
    //     static_cast<int>(type),
    //     finished ? L"completion signal received" : L"no completion signal",
    //     durationSeconds,
    //     timeoutSeconds,
    //     timeoutMilliseconds)
    L2_LOG(
        Log, finished ? LOG_NORMAL : LOG_ERROR,
        u"[db] Wait for %ls (job %d): %ls after %u s; requested timeout %u s, effective wait %u ms.", DatabaseJobLabel(type),
        static_cast<int>(type), finished ? L"completion signal received" : L"no completion signal", durationSeconds,
        timeoutSeconds, timeoutMilliseconds
    );
    return finished;
}

bool CJobFinishSignal::NotifyFinish(JobFinishType type) {
    L2_GUARD_FUNCTION();
    return SetSignal(m_hJobFinishSignals[static_cast<std::int32_t>(type)]);
}

void CJobFinishSignal::Init() {
    L2_GUARD_FUNCTION();
    db.RequestGetDbRelatedCounts();
    if (!WaitFinish(JFT_LOAD_DB_RELATED_COUNTS, 30)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Db Error in JobFinishSignal Init")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Database-count initialization continues without a completion signal. Requested counts have not been
        //     confirmed.")
        L2_LOG_ERROR(
            Log,
            u"[db] Database-count initialization continues without a completion signal; requested counts have not been "
            u"confirmed."
        );
    }
}

bool CJobFinishSignal::SetAllianceLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedAlliance;
    if (m_nLoadedAlliance < m_nMustLoadAlliance) {
        return false;
    }
    NotifyFinish(JFT_LOAD_ALLIANCE);
    return true;
}

bool CJobFinishSignal::SetCastleLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedCastle;
    if (m_nLoadedCastle < m_nMustLoadCastle) {
        return false;
    }
    NotifyFinish(JFT_LOAD_CASTLE);
    return true;
}

bool CJobFinishSignal::SetAgitLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedAgit;
    if (m_nLoadedAgit < m_nMustLoadAgit) {
        return false;
    }
    NotifyFinish(JFT_LOAD_AGIT);
    return true;
}

bool CJobFinishSignal::SetFortressLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedFortress;
    if (m_nLoadedFortress < m_nMustLoadFortress) {
        return false;
    }
    NotifyFinish(JFT_LOAD_FORTRESS);
    return true;
}

void CJobFinishSignal::SetDominionLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedDominion;
    if (m_nLoadedDominion >= m_nMustLoadDominion) {
        NotifyFinish(JFT_LOAD_DOMINION);
    }
}

bool CJobFinishSignal::SetWarLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedWar;
    if (m_nLoadedWar < m_nMustLoadWar) {
        return false;
    }
    NotifyFinish(JFT_LOAD_WAR);
    return true;
}

void CJobFinishSignal::SetNoblessLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedNobless;
    if (m_nLoadedNobless >= m_nMustLoadNobless) {
        NotifyFinish(JFT_LOAD_NOBLESS);
    }
}

void CJobFinishSignal::SetMatchCountLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedMatchCount;
    if (m_nLoadedMatchCount >= m_nMustLoadMatchCount) {
        NotifyFinish(JFT_LOAD_NOBLESS_MATCH_COUNT);
    }
}

void CJobFinishSignal::SetAllPledgeLoaded() {
    L2_GUARD_FUNCTION();
    NotifyFinish(JFT_LOAD_PLEDGE);
}

bool CJobFinishSignal::SetDismissReservedLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedDismissReserved;
    if (m_nLoadedDismissReserved < m_nMustLoadDismissReserved) {
        return false;
    }
    NotifyFinish(JFT_LOAD_DISMISS_RESERVED);
    return true;
}

void CJobFinishSignal::SetResevedFinish() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    NotifyFinish(JFT_WAIT_PLEDGE_RESERVE);
}

void CJobFinishSignal::SetWOlympiadUserLoaded() {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(m_lock);
    ++m_nLoadedWOlympiadUser;
    if (m_nLoadedWOlympiadUser >= m_nMustLoadWOlympiadUser) {
        NotifyFinish(JFT_LOAD_WORLD_OLYMPIAD_USER);
    }
}

SingleJobFinishSignal::SingleJobFinishSignal(const char* jobName) : m_hSignal(kInvalidSignal) {
    m_JobName.assign(std::string_view(jobName, std::strlen(jobName)));

    L2_GUARD_FUNCTION();
    m_hSignal = CreateSignal();
    if (!m_hSignal) {
        m_hSignal = kInvalidSignal;
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot Create Job Finish Signal Event simple")
        // Log.Add(
        //     LOG_ERROR,
        //     "[db] Cannot create completion signal for %s. The signal remains invalid.",
        //     m_JobName.c_str())
        L2_LOG_ERROR(Log, "[db] Cannot create completion signal for %s; the signal remains invalid.", m_JobName.c_str());
    }
}

SingleJobFinishSignal::~SingleJobFinishSignal() {
    L2_GUARD_FUNCTION();
    CloseSignal(m_hSignal);
}

bool SingleJobFinishSignal::NotifyFinish() {
    L2_GUARD_FUNCTION();
    return SetSignal(m_hSignal);
}

bool SingleJobFinishSignal::WaitFinish(unsigned int timeoutSeconds) {
    L2_GUARD_FUNCTION();
    const std::uint32_t begin = runtime::GetTickCount();
    const std::uint32_t timeoutMilliseconds = timeoutSeconds * 1000U;
    const bool finished = WaitSignal(m_hSignal, timeoutMilliseconds);
    const std::uint32_t durationSeconds = (runtime::GetTickCount() - begin) / 1000U;
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] JobType: %S Duration: %d/%dsec  ",
    //     m_JobName.c_str(),
    //     static_cast<int>(durationSeconds),
    //     static_cast<int>(timeoutSeconds))
    // Log.Add(
    //     finished ? LOG_NORMAL : LOG_ERROR,
    //     "[db] Wait for %s: %s after %u s. Requested timeout %u s, effective wait %u ms.",
    //     m_JobName.c_str(),
    //     finished ? "completion signal received" : "no completion signal",
    //     durationSeconds,
    //     timeoutSeconds,
    //     timeoutMilliseconds)
    L2_LOG(
        Log, finished ? LOG_NORMAL : LOG_ERROR,
        "[db] Wait for %s: %s after %u s; requested timeout %u s, effective wait %u ms.", m_JobName.c_str(),
        finished ? "completion signal received" : "no completion signal", durationSeconds, timeoutSeconds,
        timeoutMilliseconds
    );
    return finished;
}

CDB::CDB() : m_pDBSockets{nullptr, nullptr}, m_pCheckCacheDSignal(nullptr) {
    L2_GUARD_FUNCTION();
    m_pCheckCacheDSignal = new SingleJobFinishSignal("CheckCacheDVersion");
}

CDB::~CDB() {
    if (m_pCheckCacheDSignal) {
        delete m_pCheckCacheDSignal;
        m_pCheckCacheDSignal = nullptr;
    }
}

std::uint64_t CDB::CreateDBSocket(const std::uint64_t index) {
    // Current L2Server 0x006422f0. The target binds an ephemeral local
    // endpoint before connecting and reports the caller's low 32-bit index.
    const runtime::NativeSocket nativeSocket = runtime::OpenTcpSocket();
    if (!runtime::BindTcpListener(nativeSocket, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't bind socket to cache server")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] CacheD socket %llu could not %ls. Socket error %d. No connection was attempted.",
        //     static_cast<unsigned long long>(index),
        //     nativeSocket == runtime::kInvalidNativeSocket ? L"be opened" : L"bind an ephemeral local port",
        //     portable::GetNativeSocketLastError())
        L2_LOG_ERROR(
            Log, u"[db] CacheD socket %llu could not %ls; socket error %d. No connection was attempted.",
            static_cast<unsigned long long>(index),
            nativeSocket == runtime::kInvalidNativeSocket ? L"be opened" : L"bind an ephemeral local port",
            portable::GetNativeSocketLastError()
        );
        runtime::CloseTcpSocket(nativeSocket);
        return 0;
    }

    if (!runtime::ConnectTcpSocket(nativeSocket, cacheServerAddr, static_cast<std::uint16_t>(cacheServerPort))) {
        bCanConnectCacheServer = false;
        const std::uint32_t diagnosticAddress = ntohl(cacheServerAddr.s_addr);
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to cache server")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] CacheD socket %llu could not connect (configured peer %u.%u.%u.%u:%u). Socket error %d.",
        //     static_cast<unsigned long long>(index),
        //     (diagnosticAddress >> 24) & 0xffU,
        //     (diagnosticAddress >> 16) & 0xffU,
        //     (diagnosticAddress >> 8) & 0xffU,
        //     diagnosticAddress & 0xffU,
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(cacheServerPort)),
        //     portable::GetNativeSocketLastError())
        L2_LOG_ERROR(
            Log, u"[db] CacheD socket %llu could not connect (configured peer %u.%u.%u.%u:%u); socket error %d.",
            static_cast<unsigned long long>(index), (diagnosticAddress >> 24) & 0xffU, (diagnosticAddress >> 16) & 0xffU,
            (diagnosticAddress >> 8) & 0xffU, diagnosticAddress & 0xffU,
            static_cast<unsigned int>(static_cast<std::uint16_t>(cacheServerPort)), portable::GetNativeSocketLastError()
        );
        runtime::CloseTcpSocket(nativeSocket);
        return 0;
    }

    const std::uint32_t diagnosticAddress = ntohl(cacheServerAddr.s_addr);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Connected to cache server %d",
    //     static_cast<int>(index))
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[db] CacheD TCP connection %llu established (configured peer %u.%u.%u.%u:%u).",
    //     static_cast<unsigned long long>(index),
    //     (diagnosticAddress >> 24) & 0xffU,
    //     (diagnosticAddress >> 16) & 0xffU,
    //     (diagnosticAddress >> 8) & 0xffU,
    //     diagnosticAddress & 0xffU,
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(cacheServerPort)))
    L2_LOG_INFO(
        Log, u"[db] CacheD TCP connection %llu established (configured peer %u.%u.%u.%u:%u).",
        static_cast<unsigned long long>(index), (diagnosticAddress >> 24) & 0xffU, (diagnosticAddress >> 16) & 0xffU,
        (diagnosticAddress >> 8) & 0xffU, diagnosticAddress & 0xffU,
        static_cast<unsigned int>(static_cast<std::uint16_t>(cacheServerPort))
    );
    return nativeSocket;
}

bool CDB::InitCacheD() {
    // Current L2Server 0x006423d8. Failure is immediate and keeps any
    // lower-index socket already published by this loop.
    L2_GUARD_FUNCTION();
    for (std::uint64_t index = 0; index < 2; ++index) {
        const std::uint64_t nativeSocket = CreateDBSocket(index);
        if (nativeSocket == 0) {
            return false;
        }

        CDBSocket* const socket = new CDBSocket(nativeSocket, dbPacketTable);
        m_pDBSockets[index] = socket;
        const bool bound = portable::BindNativeSocketTransport(
            socket, nativeSocket,
            [](void* owner, const std::uint8_t* data, const std::size_t length) {
                return portable::FeedSocketRead(static_cast<CDBSocket*>(owner), data, length);
            },
            [](void* owner, const int error) { portable::FeedSocketTerminal(static_cast<CDBSocket*>(owner), error); }
        );
        if (!bound) {
            runtime::CloseTcpSocket(nativeSocket);
        }
        socket->Initialize();
    }
    return true;
}

L2_DEFINE_OBJECT_RELEASE(CDB)

void CDB::Send(const char* format, ...) const {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    if (runtime::process::CurrentWorldId() < 1) {
        ReportMissingWorldId();
    } else {
        m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

void CDB::SendDB(int worldId, const char* format, ...) const {
    L2_GUARD_FUNCTION();
    runtime::process::SetCurrentWorldId(EffectiveWorldId(worldId));
    CWorldSetter worldSetter;

    va_list arguments;
    va_start(arguments, format);
    if (runtime::process::CurrentWorldId() < 1) {
        ReportMissingWorldId();
    } else {
        m_pDBSockets[::LegacyCrtRand() & 1]->SendV(format, arguments);
    }
    va_end(arguments);
}

void CDB::SendPacket(
    int worldId, ::RQ_PROTOCOL protocol, ::RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet
) const {
    L2_GUARD_FUNCTION();
    runtime::process::SetCurrentWorldId(EffectiveWorldId(worldId));
    CWorldSetter worldSetter;
    if (runtime::process::CurrentWorldId() < 1) {
        ReportMissingWorldId();
    } else {
        m_pDBSockets[::LegacyCrtRand() & 1]->Send(protocol, extendedProtocol, packet);
    }
}

void CDB::RequestMailList(User* const user, CMailListParameter& parameter) {
    // Current L2Server 0x0065195c. GetServerId is evaluated before GetDBID
    // in the target even though the database id precedes it on the wire.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chddb", 0xea, 1, databaseId, serverId, packetLength, packet.data());
}

void CDB::RequestReadMail(User* const user, const int mailId, CMailListParameter& parameter) {
    // Current L2Server 0x00651a68. As in RequestMailList, the target calls
    // GetServerId before GetDBID although the database id is serialized first.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 2, databaseId, serverId, mailId, packetLength, packet.data());
}

void CDB::RequestGetTempMail(User* const user, const int mailId, CMailListParameter& parameter) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 9, databaseId, serverId, mailId, packetLength, packet.data());
}

void CDB::RequestSendMail(
    User* const user,
    const int value1,
    const int value2,
    const char16_t* const receiver,
    const char16_t* const title,
    const char16_t* const content,
    CMailListParameter& parameter
) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB(
        "chddddSSSb", 0xea, 4, databaseId, serverId, value1, value2,
        receiver, title,
        content, packetLength, packet.data()
    );
}

void CDB::RequestSendPledgeMail(User* const user, const char16_t* const title, const char16_t* const content) {
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }
    const char16_t* const pledgeName = pledge->GetPledgeName();
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB(
        "chddSSS", 0xea, 0x12, databaseId, serverId, pledgeName, title,
        content
    );
}

void CDB::RequestSaveMail(
    User* const user,
    const int value1,
    const int value2,
    const char16_t* const receiver,
    const char16_t* const title,
    const char16_t* const content,
    CMailListParameter& parameter
) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB(
        "chddddSSSb", 0xea, 5, databaseId, serverId, value1, value2,
        receiver, title,
        content, packetLength, packet.data()
    );
}

void CDB::RequestReplyMail(User* const user, const int mailId, CMailListParameter& parameter) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 10, databaseId, serverId, mailId, packetLength, packet.data());
}

void CDB::RequestForwardMail(User* const user, const int mailId, CMailListParameter& parameter) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 0xb, databaseId, serverId, mailId, packetLength, packet.data());
}

void CDB::RequestModifyMail(
    User* const user, const int mailId, const char16_t* const receiver, const char16_t* const title, const char16_t* const content
) {
    L2_GUARD_FUNCTION();
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB(
        "chdddSSS", 0xea, 0xc, databaseId, serverId, mailId, receiver,
        title, content
    );
}

void CDB::RequestModifySendMail(
    User* const user,
    const int mailId,
    const char16_t* const receiver,
    const char16_t* const title,
    const char16_t* const content,
    CMailListParameter& parameter
) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB(
        "chdddSSSb", 0xea, 0xd, databaseId, serverId, mailId, receiver,
        title, content,
        packetLength, packet.data()
    );
}

void CDB::RequestDeleteMail(User* const user, const int mailId, CMailListParameter& parameter) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 0xe, databaseId, serverId, mailId, packetLength, packet.data());
}

void CDB::RequestArchiveMail(User* const user, const int mailId, CMailListParameter& parameter) {
    L2_GUARD_FUNCTION();
    std::array<char, 0x400> packet;
    const int packetLength = parameter.AssemblePacket(packet.data(), static_cast<int>(packet.size()));
    const int serverId = static_cast<int>(user->GetServerId());
    const int databaseId = static_cast<int>(user->GetDBID());
    user->SendDB("chdddb", 0xea, 0xf, databaseId, serverId, mailId, packetLength, packet.data());
}

bool CDB::SendPrivateStoreCommit(
    User* const seller,
    User* const buyer,
    const std::int64_t totalPrice,
    const int itemCount,
    const int packetLength,
    char* const packet
) const {
    // Current L2Server 0x0064937c. The two independent wait flags and the
    // asymmetric rollback paths are target behavior.
    L2_GUARD_FUNCTION();
    if (seller->m_data->GetWorldId() != buyer->m_data->GetWorldId()) {
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&seller->m_lWaitItemJob, 1) == 1) {
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&buyer->m_lWaitItemJob, 1) == 1) {
        runtime::raw_atomic::ExchangeI32(&seller->m_lWaitItemJob, 0);
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&seller->m_nWaitGetItemOnce, 1) != 0) {
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&buyer->m_nWaitGetItemOnce, 1) != 0) {
        runtime::raw_atomic::ExchangeI32(&seller->m_nWaitGetItemOnce, 0);
        return false;
    }

    seller->SendDB("cddQdb", 0x63, seller->GetDBID(), buyer->GetDBID(), totalPrice, itemCount, packetLength, packet);
    return true;
}

void CDB::TradeDivideAdenaCommit(User* const user, const int targetCharacterId, const int packetLength, char* const packet) {
    // Current L2Server 0x00643468.
    L2_GUARD_FUNCTION();
    user->SendDB("chddb", 0xea, 0x174, user->GetDBID(), targetCharacterId, packetLength, packet);
}

void CDB::SendTradeCommit(
    User* const first,
    User* const second,
    const int firstItemCount,
    const int firstPacketLength,
    char* const firstPacket,
    const int secondItemCount,
    const int secondPacketLength,
    char* const secondPacket
) const {
    // Current L2Server 0x00643554.
    L2_GUARD_FUNCTION();
    if (first != nullptr) {
        if (second != nullptr) {
            if (first->m_data->GetWorldId() == second->m_data->GetWorldId()) {
                if (first->GetItemManipulation() < 2) {
                    if (second->GetItemManipulation() < 2) {
                        first->SendDB(
                            "cdddbdb", 0x20, first->GetDBID(), second->GetDBID(), firstItemCount, firstPacketLength,
                            firstPacket, secondItemCount, secondPacketLength, secondPacket
                        );
                        if (first->GetItemManipulation() == 1) {
                            first->LeaveItemManipulation();
                        }
                        if (second->GetItemManipulation() == 1) {
                            second->LeaveItemManipulation();
                        }
                        return;
                    }
                    second->TradeCancel(false);
                    second->SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                    return;
                }
                first->TradeCancel(false);
                first->SendSystemMessage(SystemMessageId::CANNOT_TRADE_WHILE_USING_ITEM);
                return;
            }
            first->TradeCancel(false);
            second->TradeCancel(false);
            return;
        }
        first->TradeCancel(false);
    }
    if (second != nullptr) {
        second->TradeCancel(false);
    }
}

void CDB::RequestCreateInitItems(const unsigned int characterId, const int classId, CUserSocket* const socket) const {
    // Current L2Server 0x00669db4.
    L2_GUARD_FUNCTION();

    int equippedItems[64];
    equippedItems[0] = -1;
    std::memset(equippedItems + 1, 0, 0xf8);
    std::memset(equippedItems, 0xff, 0xfc);

    const CSetting::InitialEquipment& initialEquipment = Singleton<CSetting>::inst().m_InitialEquipment[classId];
    const int itemCount = static_cast<int>(initialEquipment.size());
    char itemPacket[0x5000];
    int itemPacketSize = 0;

    for (const auto& entry : initialEquipment) {
        const int itemId = entry.first;
        const std::int64_t amount = entry.second;
        if (itemId <= 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] initial item is invalid. [%d]",
            //     "db.cpp",
            //     0x140,
            //     itemId)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because its ID must be
            //     positive.", characterId, classId, itemId)
            L2_LOG_ERROR(
                Log,
                u"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because its ID must be "
                u"positive.",
                characterId, classId, itemId
            );
            continue;
        }
        if (amount == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] initial item's amount is invalid. [%I64d]",
            //     "db.cpp",
            //     0x147,
            //     amount)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because the configured
            //     amount is zero.", characterId, classId, itemId)
            L2_LOG_ERROR(
                Log,
                u"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because the configured "
                u"amount is zero.",
                characterId, classId, itemId
            );
            continue;
        }

        CObject* const object = g_ObjectDB.GetObjectW(itemId, false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] cannot find initial item. [%d]",
            //     "db.cpp",
            //     0x14e,
            //     itemId)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because it was not
            //     found.", characterId, classId, itemId)
            L2_LOG_ERROR(
                Log,
                u"[db] Initial equipment for character DB ID %u, class %d: skipping template %d because it was not found.",
                characterId, classId, itemId
            );
            continue;
        }
        CItem* const item = static_cast<CItem*>(object);

        int slot = 0;
        while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
            ++slot;
        }

        switch (slot) {
            case ST_REAR:
            case ST_LEAR:
                if (equippedItems[ST_REAR] == -1) {
                    equippedItems[ST_REAR] = itemId;
                } else if (equippedItems[ST_LEAR] == -1) {
                    equippedItems[ST_LEAR] = itemId;
                }
                break;
            case ST_RFINGER:
            case ST_LFINGER:
                if (equippedItems[ST_RFINGER] == -1) {
                    equippedItems[ST_RFINGER] = itemId;
                } else if (equippedItems[ST_LFINGER] == -1) {
                    equippedItems[ST_LFINGER] = itemId;
                }
                break;
            case ST_RHAND:
                if (equippedItems[ST_RLHAND] == -1 && equippedItems[ST_RHAND] == -1) {
                    equippedItems[ST_RHAND] = itemId;
                }
                break;
            case ST_LHAND:
                if (equippedItems[ST_RLHAND] == -1 && equippedItems[ST_LHAND] == -1) {
                    equippedItems[ST_LHAND] = itemId;
                }
                break;
            case ST_CHEST:
            case ST_LEGS:
                if (equippedItems[slot] == -1 && equippedItems[ST_ONEPIECE] == -1) {
                    equippedItems[slot] = itemId;
                }
                break;
            case ST_RLHAND:
                if (equippedItems[ST_RLHAND] == -1 && equippedItems[ST_RHAND] == -1) {
                    equippedItems[ST_RHAND] = itemId;
                    equippedItems[ST_RLHAND] = itemId;
                }
                break;
            case ST_ONEPIECE:
                if (equippedItems[ST_CHEST] == -1 && equippedItems[ST_LEGS] == -1 && equippedItems[ST_ONEPIECE] == -1) {
                    equippedItems[ST_CHEST] = itemId;
                    equippedItems[ST_LEGS] = -1;
                    equippedItems[ST_ONEPIECE] = itemId;
                }
                break;
            case ST_HAIR:
            case ST_HAIR2:
                if (equippedItems[slot] == -1 && equippedItems[ST_HAIRALL] == -1) {
                    equippedItems[slot] = itemId;
                }
                break;
            case ST_ALLDRESS:
                if (equippedItems[ST_CHEST] == -1 && equippedItems[ST_ALLDRESS] == -1) {
                    for (int dressSlot = ST_HEAD; dressSlot <= ST_ONEPIECE; ++dressSlot) {
                        if (dressSlot != ST_RHAND && dressSlot != ST_LHAND && dressSlot != ST_BACK &&
                            dressSlot != ST_RLHAND) {
                            equippedItems[dressSlot] = -1;
                        }
                    }
                    equippedItems[ST_CHEST] = itemId;
                    equippedItems[ST_ALLDRESS] = itemId;
                }
                break;
            case ST_HAIRALL:
                if (equippedItems[ST_HAIR] == -1 && equippedItems[ST_HAIR2] == -1 && equippedItems[ST_HAIRALL] == -1) {
                    equippedItems[ST_HAIR] = itemId;
                    equippedItems[ST_HAIR2] = -1;
                    equippedItems[ST_HAIRALL] = itemId;
                }
                break;
            default:
                if (slot < ST_MAX && equippedItems[slot] == -1) {
                    equippedItems[slot] = itemId;
                }
                break;
        }

        const int assembled = ::Assemble(
            itemPacket + itemPacketSize, 0x5000 - itemPacketSize, "dQd", itemId, amount,
            static_cast<int>(item->m_data->m_ItemConsumeType)
        );
        if (assembled > 0) {
            itemPacketSize += assembled;
        }
    }

    SendDB(
        g_nWorldId, "cdddbb", 5, characterId, socket->GetServerId(), itemCount, itemPacketSize, itemPacket, 0xfc,
        equippedItems
    );
}

void CDB::RequestCrystallizeItem(
    CItem* const item, User* const user, const int itemCount, const int packetLength, char* const packet
) const {
    // Current L2Server 0x00645c94.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddddb", 0x5d, user->GetDBID(), 0, item->GetDBID(), item->m_data->m_nId, user->GetServerId(), itemCount,
        packetLength, packet
    );
}

void CDB::RequestCrystallizePetItem(
    CItem* const item, User* const user, CPet* const pet, const int itemCount, const int packetLength, char* const packet
) const {
    // Current L2Server 0x00645dc4.
    L2_GUARD_FUNCTION();
    int slot = 0;
    while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
        ++slot;
    }
    if (pet->IsEquiped(item->m_data->index, static_cast<SlotType>(slot)) < ST_MAX) {
        pet->UnequipItem(item, false);
    }

    user->SendDB(
        "cddddddb", 0x5d, user->GetDBID(), pet->GetDBID(), item->GetDBID(), item->m_data->m_nId, user->GetServerId(),
        itemCount, packetLength, packet
    );
}

void CDB::RequestSellItems(
    const int npcClassId,
    User* const user,
    const std::int64_t totalPrice,
    const int itemCount,
    const int packetLength,
    char* const packet
) const {
    // Current L2Server 0x006443d0.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cdddQdb", 0x11, user->GetDBID(), user->GetServerId(), npcClassId, totalPrice, itemCount, packetLength, packet
    );
}

void CDB::RequestBuyItems(
    const int npcClassId,
    User* const user,
    const std::int64_t totalPrice,
    const int tax,
    const int residenceId,
    const bool useResidenceOwner,
    const int itemCount,
    const int packetLength,
    char* const packet
) const {
    // Current L2Server 0x0066a1c4. The residence-owner master lookup and
    // the -1 fallback are part of the current CacheD request contract.
    L2_GUARD_FUNCTION();
    std::uint32_t residenceMasterDbId = std::numeric_limits<std::uint32_t>::max();
    CSPointer<CPledge> owner;
    if (residenceId != 0) {
        const CResidenceData& residence = CResidenceData::Inst(residenceId);
        owner = CPledge::FindPledge(user->m_data->m_nWorldId, static_cast<int>(residence.m_nResidenceOwnerID));
        if (owner) {
            residenceMasterDbId = owner->GetMasterDbId();
        }
    }

    user->SendDB(
        "cdddQddcdb", 0x12, user->GetDBID(), user->GetServerId(), npcClassId, totalPrice, tax, residenceMasterDbId,
        useResidenceOwner, itemCount, packetLength, packet
    );
}

void CDB::RequestDropItem(
    CItem* const item,
    User* const user,
    const FVector position,
    const SlotType slot,
    const bool unknown,
    const std::int64_t count
) const {
    // Current L2Server 0x00642680.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     L"db.cpp",
        //     0x1dc)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Item-drop request skipped: character is unavailable (amount %lld, equipment slot %d).",
        //     static_cast<long long>(count),
        //     static_cast<int>(slot))
        L2_LOG_ERROR(
            Log, u"[db] Item-drop request skipped: character is unavailable (amount %lld, equipment slot %d).",
            static_cast<long long>(count), static_cast<int>(slot)
        );
        return;
    }

    user->SendDB(
        "cdddddddQhh", 6, static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
        // Current L2Server 0x00642798 calls User vslot +0xd40.
        // User::vftable binds that slot to GetServerId@0x00a49a2c.
        // CacheD returns it as the live-object correlation ID consumed by
        // DBPacketHandler::DropItemPacket.
        user->GetServerId(), user->GetDBID(), item->GetDBID(), (static_cast<int>(slot) + 1) % ST_MAX, count, 0, unknown
    );
}

void CDB::RequestCancelEquipmentAddedEffectAndDropItem(
    User* const user,
    CItem* const item,
    FVector* const position,
    const SlotType slot,
    const std::int64_t count,
    const int effectMask,
    const bool setContributeData
) {
    // Current L2Server.exe 0x0065e464, original owner db.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr || item == nullptr || position == nullptr) {
        return;
    }

    user->SendDB(
        "chdddddddQhd", 0xea, 0x150, user->GetDBID(), user->GetServerId(), item->GetDBID(),
        runtime::Win64CvttDoubleToInt32(position->x), runtime::Win64CvttDoubleToInt32(position->y),
        runtime::Win64CvttDoubleToInt32(position->z), (static_cast<int>(slot) + 1) % ST_MAX, count, setContributeData,
        effectMask
    );
}

void CDB::RequestDropCastleGuard(
    CItem* const item,
    User* const user,
    const FVector position,
    FAngle direction,
    const unsigned int residenceId,
    ResidenceGuardInfo* const guardInfo
) const {
    // Current L2Server 0x0064283c.
    L2_GUARD_FUNCTION();
    direction.Yaw = static_cast<std::uint16_t>(direction.Yaw);
    user->SendDB(
        "cdddddddQhhdddddd", 6, static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z),
        user->GetServerId(), user->GetDBID(), item->GetDBID(), ST_MAX, std::int64_t{1}, 1, 0, direction.Yaw, residenceId,
        item->m_nClassId, guardInfo->nNpcId, guardInfo->nGuardType, guardInfo->bMoveable == true
    );
}

void CDB::RequestSaveMercenary(
    CResidence& residence,
    const int npcIndex,
    const int npcId,
    const FVector position,
    FAngle direction,
    const double hp,
    const double mp
) const {
    // Current L2Server 0x00642a04.
    L2_GUARD_FUNCTION();
    direction.Yaw = static_cast<std::uint16_t>(direction.Yaw);
    residence.SendDB(
        "cddddddddd", 0x9c, npcIndex, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0, npcId,
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), direction.Yaw, runtime::Win64CvttDoubleToInt32(hp),
        runtime::Win64CvttDoubleToInt32(mp)
    );
}

void CDB::RequestLoadMercenary(CResidence& residence) const {
    // Current L2Server 0x00642b34.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0x9d, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0);
}

void CDB::RequestDeleteMercenary(CResidence& residence) const {
    // Current L2Server 0x00642bf4.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0x9e, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0);
}

void CDB::RequestLoadGuardData(CResidence& residence) const {
    // Current L2Server 0x00642cb4.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0x98, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0);
}

void CDB::RequestDeleteGuardData(CResidence& residence) const {
    // Current L2Server 0x00642d74.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0xa6, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0);
}

void CDB::CreateUpdateItemsWithPoint(
    User* user,
    const int point,
    const int pointType,
    const int operation,
    const int value,
    const int itemCount,
    CPacketHelperEx<20480>* packet,
    const int packetSize,
    char* packetData,
    AtomicJob* atomicJob,
    const int line
) {
    L2_GUARD_FUNCTION();
    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (itemCount != 0) {
        if (packet == nullptr) {
            runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
            return;
        }
        helper = packet;
    }

    const int atomicJobId = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Push(atomicJob);
    user->SendDB(
        "chdddddddddbb", 0xea, 0x24, user->GetDBID(),
        // Current L2Server 0x0066f2bd calls User vslot +0xd40 here.
        // User::vftable binds that slot to GetServerId@0x00a49a2c.
        // CacheD returns it as the live-object correlation ID.
        user->GetServerId(), point, pointType, operation, line, atomicJobId, value, itemCount, helper->GetLength(),
        helper->GetPacket(), packetSize, packetData
    );
}

void CDB::_CreateUpdateItems(
    User* user,
    ManipulateType::Enum type,
    int id,
    int itemCount,
    int packetSize,
    char* packetData,
    AtomicJob* atomicJob,
    int etcManipulationCount,
    CPacketHelperEx<20480>* etcManipulationPacket,
    bool premium
) const {
    L2_GUARD_FUNCTION();

    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (etcManipulationCount != 0) {
        if (etcManipulationPacket == nullptr) {
            user->_DoneManiItemJob(type, id);
            return;
        }
        helper = etcManipulationPacket;
    }

    const int atomicJobId = PushAtomicJobHelper(atomicJob);
    user->SendDB(
        "chddddddddbb", 0xea, 0x173, user->GetDBID(), static_cast<int>(type), id, user->GetServerId(), atomicJobId,
        static_cast<int>(premium), etcManipulationCount, itemCount, helper->GetLength(), helper->GetPacket(), packetSize,
        packetData
    );
}

void CDB::CreateUpdateItems(
    User* user,
    int itemCount,
    int packetSize,
    char* packetData,
    AtomicJob* atomicJob,
    int etcManipulationCount,
    CPacketHelperEx<20480>* etcManipulationPacket,
    bool premium
) const {
    L2_GUARD_FUNCTION();

    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (etcManipulationCount != 0) {
        if (etcManipulationPacket == nullptr) {
            runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
            return;
        }
        helper = etcManipulationPacket;
    }

    const int atomicJobId = PushAtomicJobHelper(atomicJob);
    user->SendDB(
        "cddddddbb", 0x1e, user->GetDBID(), user->GetServerId(), atomicJobId, static_cast<int>(premium),
        etcManipulationCount, itemCount, helper->GetLength(), helper->GetPacket(), packetSize, packetData
    );
}

bool CDB::CreateUpdateItemsAtWarehouse(
    const int userDatabaseId,
    const int manipulationCount,
    ItemManip* const manipulations,
    AtomicJob* const atomicJob,
    const int etcManipulationCount,
    CPacketHelperEx<20480>* const etcManipulationPacket
) const {
    // Current L2Server.exe 0x0066e198.
    L2_GUARD_FUNCTION();
    if (userDatabaseId == 0 || (manipulationCount == 0 && etcManipulationCount == 0)) {
        return false;
    }

    ItemManipList manipulationList;
    manipulationList.Push(manipulationCount, manipulations);
    manipulationList.Serialize();

    std::array<char, 20480> itemPacket{};
    int itemPacketLength = 0;
    int itemCount = 0;
    for (const ItemManip& manipulation : manipulationList.m_vOutputList) {
        if (!manipulation.MakeDBPacket(
                itemPacket.data(), static_cast<int>(itemPacket.size()), itemPacketLength, itemCount
            )) {
            // Log.Add(
            //     LOG_WARN,
            //     L"[items] Warehouse update not sent for character DB ID %d: cannot encode item selector %d, amount delta
            //     %lld.", userDatabaseId, manipulation.mClassId, static_cast<long long>(manipulation.mCount))
            L2_LOG_WARN(
                Log,
                u"[items] Warehouse update not sent for character DB ID %d: cannot encode item selector %d, amount delta "
                u"%lld.",
                userDatabaseId, manipulation.mClassId, static_cast<long long>(manipulation.mCount)
            );
            return false;
        }
    }

    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (etcManipulationCount != 0) {
        if (etcManipulationPacket == nullptr) {
            return false;
        }
        helper = etcManipulationPacket;
    }

    SendDB(
        g_nWorldId, "chddddddbb", 0xea, 0x76, userDatabaseId, 0, PushAtomicJobHelper(atomicJob), 0, etcManipulationCount,
        itemCount, helper->GetLength(), helper->GetPacket(), itemPacketLength, itemPacket.data()
    );
    return true;
}

void CDB::CreateUpdatePremiumItems(
    User* const user, const std::int64_t warehouseNo, CItem* const item, InventoryParams parameters
) const {
    // Current L2Server 0x0066f718, original owner db.cpp.
    L2_GUARD_FUNCTION();

    std::array<char, 20480> itemPacket;
    int itemPacketLength = 0;
    MsgHelper_ManipulateItem(itemPacket.data(), static_cast<int>(itemPacket.size()), itemPacketLength, item, parameters);
    user->SendDB(
        "chddQb", 0xea, 0x93, user->GetServerId(), user->GetDBID(), warehouseNo, itemPacketLength, itemPacket.data()
    );
}

bool CDB::RequestManipulatePledgeItemNew(
    const int pledgeDatabaseId,
    const int userDatabaseId,
    const std::vector<CreateItemStruct>& items,
    AtomicJob* const atomicJob,
    const int etcManiCount,
    CPacketHelperEx<20480>* const etcManiPacket
) const {
    L2_GUARD_FUNCTION();

    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (etcManiCount != 0) {
        if (etcManiPacket == nullptr) {
            return false;
        }
        helper = etcManiPacket;
    }

    const int atomicJobId = PushAtomicJobHelper(atomicJob);
    CPacketHelperEx<20480> itemPacket;
    ItemUtil::AssembleItemList(items, itemPacket);
    SendDB(
        g_nWorldId, "chdddddbb", 0xea, 0x1c8, pledgeDatabaseId, userDatabaseId, atomicJobId, etcManiCount,
        static_cast<int>(items.size()), helper->GetLength(), helper->GetPacket(), itemPacket.GetLength(),
        itemPacket.GetPacket()
    );
    return true;
}

bool CDB::RequestManipulatePledgeItem(
    const User* const user,
    const int pledgeDatabaseId,
    const std::vector<CreateItemStruct>& items,
    AtomicJob* const atomicJob,
    const int etcManiCount,
    CPacketHelperEx<20480>* const etcManiPacket
) const {
    // Current L2Server 0x0066e474.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    static CPacketHelperNullObject nullHelper;
    IPacketHelper* helper = &nullHelper;
    if (etcManiCount != 0) {
        if (etcManiPacket == nullptr) {
            return false;
        }
        helper = etcManiPacket;
    }

    const int atomicJobId = PushAtomicJobHelper(atomicJob);
    CPacketHelperEx<20480> itemPacket;
    ItemUtil::AssembleItemList(items, itemPacket);
    const_cast<User*>(user)->SendDB(
        "chdddddbb", 0xea, 0x1b7, pledgeDatabaseId, static_cast<int>(user->GetDBID()), atomicJobId, etcManiCount,
        static_cast<int>(items.size()), helper->GetLength(), helper->GetPacket(), itemPacket.GetLength(),
        itemPacket.GetPacket()
    );
    return true;
}

void CDB::CreateUpdateItemsWithPoint(
    const int worldId,
    const std::uint32_t userDatabaseId,
    const std::uint32_t accountId,
    const int value1,
    const int point,
    const int pointType,
    const int value2,
    const int packetSize,
    char* packetData,
    AtomicJob* atomicJob,
    const int line
) {
    L2_GUARD_FUNCTION();
    static CPacketHelperNullObject nullHelper;
    const int atomicJobId = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Push(atomicJob);
    SendDB(
        worldId, "chdddddddddbb", 0xea, 0x24, userDatabaseId, accountId, value1, point, pointType, line, atomicJobId, value2,
        0, nullHelper.GetLength(), nullHelper.GetPacket(), packetSize, packetData
    );
}

void CDB::SaveSkillUse(
    User& user,
    ReuseDelaySaveReason::Type reason,
    int normalCount,
    int transformedCount,
    int normalLength,
    char* normalBuffer,
    int transformedLength,
    char* transformedBuffer
) const {
    L2_GUARD_FUNCTION();
    user.SendDB(
        "cdddddbb", 0x68, user.GetDBID(), reason, user.m_data->m_nSubJobId, normalCount, transformedCount, normalLength,
        normalBuffer, transformedLength, transformedBuffer
    );
}

void CDB::RequestSaveDropItemAmountPerServer(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0xe5, value1, value2);
}

void CDB::RequestLoadItemAmountPerServer(int itemCount, CPacketHelperEx<20480>* packet) {
    // Current L2Server 0x004c7504.
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdb", 0xea, 0xe4, itemCount, packet->GetLength(), packet->GetPacket());
}

void CDB::RequestLoadHonorsSkill(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x26ed)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Honors-skill load not requested: character is unavailable.")
        L2_LOG_ERROR(Log, u"[db] Honors-skill load not requested: character is unavailable.");
        return;
    }
    user->SendDB("chd", 0xea, 0x196, user->GetDBID());
}

void CDB::RequestSetHonorsSkill(User* user, int skillId, int level) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x26fe)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Honors-skill update not requested: character is unavailable (skill %d, level %d).",
        //     skillId,
        //     level)
        L2_LOG_ERROR(
            Log, u"[db] Honors-skill update not requested: character is unavailable (skill %d, level %d).", skillId, level
        );
        return;
    }
    user->SendDB("chddd", 0xea, 0x197, user->GetDBID(), skillId, level);
}

void CDB::RequestDeleteHonorsSkill(User* user, int skillId) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x2720)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Honors-skill deletion not requested: character is unavailable (skill %d).",
        //     skillId)
        L2_LOG_ERROR(Log, u"[db] Honors-skill deletion not requested: character is unavailable (skill %d).", skillId);
        return;
    }
    user->SendDB("chdd", 0xea, 0x198, user->GetDBID(), skillId);
}

void CDB::RequestResetEventData(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0xe8, value1, value2);
}

void CDB::RequestSetEventDataPeriod(int period) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0xe9, period, -2);
}

void CDB::DecreaseUserKarma_KillSummon(int, int) { L2_GUARD_FUNCTION(); }

void CDB::RequestNPCPickItem(int npcId, int itemId, int itemType) const {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x25, npcId, itemId, itemType);
}

void CDB::PetGetItem(unsigned int masterServerId, CPet& pet, CItem* item) {
    L2_GUARD_FUNCTION();

    pet.SendDB(
        "cdddddddQ", 0x7d, pet.GetDBID(), pet.Id(), masterServerId, item->Id(), item->GetDBID(), item->m_nClassId,
        item->m_data->m_ItemConsumeType, item->m_data->m_nCount
    );
}

void CDB::RequestPetDropItem(User*, CPet* pet, CItem* item, unsigned int petDatabaseId, std::int64_t) {
    // Current L2Server 0x0064ad34. The current packet does not serialize
    // either the User pointer or the requested count.
    L2_GUARD_FUNCTION();
    pet->SendDB("cddddd", 0x7e, pet->Id(), pet->GetDBID(), item->Id(), item->GetDBID(), petDatabaseId);
}

void CDB::RequestPickItem(User* user, CItem* item) const {
    L2_GUARD_FUNCTION();
    if (user == nullptr || item == nullptr) {
        return;
    }

    CCursedWeaponMgr& cursedWeaponManager = CCursedWeaponMgr::inst();
    if (cursedWeaponManager.FindConst(item->m_data->m_nClassID) == nullptr) {
        const bool castleGuardItem = item->m_const->m_EtcitemType == 8;
        const FVector position = item->GetPos();
        if (castleGuardItem) {
            user->SendDB(
                "cddddddQdhddd", 0x8c, user->GetDBID(), user->GetServerId(), item->m_data->m_nClassID,
                item->m_data->m_ItemConsumeType, item->GetDBID(), item->m_data->m_nId, item->m_data->m_nCount,
                item->m_nTraceNPCClassId, 1, runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
            );
        } else {
            user->SendDB(
                "cddddddQdh", 0x8c, user->GetDBID(), user->GetServerId(), item->m_data->m_nClassID,
                item->m_data->m_ItemConsumeType, item->GetDBID(), item->m_data->m_nId, item->m_data->m_nCount,
                item->m_nTraceNPCClassId, 0
            );
        }

        if (item->m_bTraceIsBoss) {
#if defined(_WIN32)
            const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
            std::array<wchar_t, 16> accountNameStorage{};
            for (std::size_t index = 0; index + 1 < accountNameStorage.size() && index < user->m_sAccountName.size() &&
                user->m_sAccountName[index] != u'\0';
                ++index) {
                accountNameStorage[index] = static_cast<wchar_t>(user->m_sAccountName[index]);
            }
            const wchar_t* const accountName = accountNameStorage.data();
#endif
            // Log.Add(
            //     LOG_AUDIT,
            //     L"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,%s,%s,,,",
            //     static_cast<std::int64_t>(0x592),
            //     static_cast<std::int64_t>(user->GetDBID()),
            //     static_cast<std::int64_t>(user->GetAccountID()),
            //     static_cast<std::int64_t>(
            //         item->m_nTraceNPCClassId),
            //     static_cast<std::int64_t>(
            //         item->m_data->m_nClassID),
            //     user->GetName(),
            //     accountName)
            Log.Add(
                LOG_AUDIT, u"%I64d,%I64d,%I64d,,,,,,,,,%I64d,%I64d,,,,,,,,,%s,%s,,,", static_cast<std::int64_t>(0x592),
                static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()),
                static_cast<std::int64_t>(item->m_nTraceNPCClassId), static_cast<std::int64_t>(item->m_data->m_nClassID),
                user->GetName(), accountName
            );
        }
        return;
    }

    CCursedWeapon* const cursedWeapon = cursedWeaponManager.GetCursedWeapon(item->m_data->m_nClassID);
    if (cursedWeapon == nullptr) {
        return;
    }

    if (user->m_nCursedWeaponClassId == 0 || user->m_nCursedWeaponClassId == item->m_data->m_nClassID) {
        user->SendDB(
            "chddddddfdddd", 0xea, 0x54, user->GetDBID(), item->GetDBID(), item->m_data->m_nClassID,
            cursedWeapon->m_tCreateDate, cursedWeapon->m_tLastKillDate, cursedWeapon->RecalcExpiredDateAfterDrop(), 0.0,
            user->m_data->m_nPKCount, cursedWeapon->m_nTotalPK, item->m_data->m_nId, item->m_nTraceNPCClassId
        );
        return;
    }

    if (CCursedWeapon* const ownedWeapon = cursedWeaponManager.GetCursedWeapon(user->m_nCursedWeaponClassId)) {
        ownedWeapon->PickAnotherCursedWeapon(user, item);
    }
}

void CDB::RequestSetAgitAuction(int agitId, int auctionId) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdd", 0x97, agitId, auctionId);
}

void CDB::RequestAgitCost(
    CAuctionAgit& agit, const int pledgeId, const std::int64_t lease, const int costFail, const std::int32_t time
) {
    // Current L2Server 0x0064c530.
    L2_GUARD_FUNCTION();
    agit.SendDB("cddQdd", 0xa2, agit.m_data->m_nResidenceID, pledgeId, lease, costFail, time);
}

void CDB::RequestAgitAutoSell(
    CAuctionAgit& agit, const std::int64_t minimumBidPrice, const int auctionPeriod, const int bidDay, const int unknown
) {
    // Current L2Server 0x0064c618.
    L2_GUARD_FUNCTION();
    agit.SendDB("cdQddd", 0xa7, agit.m_data->m_nResidenceID, minimumBidPrice, auctionPeriod, bidDay, unknown);
}

void CDB::RequestGetContributionRelatedPledge(CResidence& residence, const int residenceId) {
    // Current L2Server 0x00649208.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0x61, residenceId);
}

void CDB::RequestLoadAgitBid(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdd", 0x99, value1, value2);
}

void CDB::SendAgitPledgeAdena(User* user, int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddddd", 0xa0, user->GetDBID(), user->GetServerId(), value1, value2, value3);
}

void CDB::RequestAgitInfo(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendAgitPledgeAdena(user, value1, value2, 0);
}

void CDB::RequestAgitSale1(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendAgitPledgeAdena(user, value1, value2, 1);
}

void CDB::RequestAgitBidRe(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendAgitPledgeAdena(user, value1, value2, 3);
}

void CDB::RequestAgitBid1(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendAgitPledgeAdena(user, value1, value2, 2);
}

void CDB::RequestAgitBidCancelRequest(User* user, int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddddd", 0xa8, user->GetDBID(), user->GetServerId(), value1, value2, value3, value4);
}

void CDB::RequestCreateAgitAuction(
    User* user,
    int value1,
    int unused,
    const char16_t* description,
    std::int64_t price,
    int period,
    std::int64_t deposit,
    int value2
) {
    L2_GUARD_FUNCTION();
    static_cast<void>(unused);
    user->SendDB(
        "cdddSQdQd", 0x95, user->GetDBID(), user->GetServerId(), value1,
        description, price, period, deposit, value2
    );
}

void CDB::RequestCreateAgitBid(User* user, int value1, int value2, int value3, std::int64_t bid) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    user->SendDB("cddddQdd", 0x96, user->GetDBID(), user->GetServerId(), value1, value2, bid, value3, now);
}

void CDB::RequestCancelAgitAuction(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddd", 0xa1, user->GetDBID(), user->GetServerId(), value1, value2);
}

void CDB::RequestSavePetitionMsgForceGetDBID(User& user, const char16_t* value1, const char16_t* value2) {
    L2_GUARD_FUNCTION();
    user.SendDB("cSdS", 0xac, value1, user.GetDBID(), value2);
}

void CDB::RequestMngComment(int value1, User* user, const char16_t* value2, int value3, const char16_t* value4) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddSdS", 0xb0, value1, user->GetDBID(), user->GetServerId(), value2, value3, value4);
}

void CDB::RequestChangeTimeLimit(User* user, const char16_t* value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    user->SendDB("cSddd", 0xb8, value1, value2, value3, value4);
}

void CDB::RequestLoadLastTaxChange() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0xae);
}

void CDB::RequestSaveLastTaxChange(std::int32_t incomeUpdate, std::int32_t taxChange, std::int32_t manorResetUpdate) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddd", 0xaf, incomeUpdate, taxChange, manorResetUpdate);
}

void CDB::RequestLoadPlayTime(int accountId) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cd", 0xb1, accountId);
}

void CDB::RequestShortCutInfo(User* user) const {
    L2_GUARD_FUNCTION();
    const std::uint32_t databaseId = user->GetDBID();
    const std::uint32_t serverId = user->GetServerId();
    user->SendDB("cddd", 0x0e, serverId, databaseId, user->m_data->m_nSubJobId);
}

void CDB::RequestSetShortCut(User* user, int slot, int type, int id, int category, int macroValue) const {
    L2_GUARD_FUNCTION();
    const std::uint32_t databaseId = user->GetDBID();
    user->SendDB("cddddddd", 0x0f, databaseId, user->m_data->m_nSubJobId, slot, type, id, category, macroValue);
}

void CDB::EquipHenna(
    User* user, int value1, std::int64_t value2, std::int64_t value3, int value4, int value5, bool value6, int value7
) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddQQddd", 0xcb, user->GetDBID(), user->GetServerId(), value4, value1, value2, value3, value5, value6, value7
    );
}

void CDB::UnequipHenna(User* user, int value1, std::int64_t value2, std::int64_t value3, int value4, bool value5) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddddQQd", 0xcc, user->GetDBID(), user->GetServerId(), value4, value1, value2, value3, value5);
}

void CDB::RequestGetMacro(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddd", 0xcd, user->GetDBID(), user->GetServerId(), 4);
}

void CDB::RequestCreateMacro(User* user, int dataSize, const unsigned char* data) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddb", 0xcd, user->GetDBID(), user->GetServerId(), 1, dataSize, data);
}

void CDB::RequestUpdateMacro(User* user, int dataSize, const unsigned char* data) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddb", 0xcd, user->GetDBID(), user->GetServerId(), 2, dataSize, data);
}

void CDB::RequestDeleteMacro(User* user, int macroId) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddd", 0xcd, user->GetDBID(), user->GetServerId(), 3, macroId);
}

void CDB::RequestDeleteMacroAll(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdd", 0xea, 0x135, user->GetDBID(), user->GetServerId());
}

void CDB::RequestGetMemoList(User* user, int value, const char16_t* name) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cdddddS", 0xce, user->GetDBID(), user->GetServerId(), 1, user->m_data->m_nWorldId, value,
        name
    );
}

void CDB::RequestClanBBSView(User* user, int value) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddddd", 0xcf, user->GetDBID(), user->GetServerId(), 2, user->m_data->m_nWorldId, value);
}

void CDB::RequestBBSClanList(
    User* user,
    int worldId,
    int value1,
    int value2,
    int value3,
    SharedUtilsWideChar* name,
    int value4,
    int dataSize,
    const unsigned char* data
) {
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId == worldId) {
        user->SendDB(
            "cdddddddSdb", 0xd9, user->GetDBID(), user->GetServerId(), worldId, 1, value1, value2, value3, name, value4,
            dataSize, data
        );
    }
}

void CDB::RequestBBSClanAdmin(
    User* user, int value1, int value2, SharedUtilsWideChar* name, int value3, int dataSize, const unsigned char* data
) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddddSddb", 0xd9, user->GetDBID(), user->GetServerId(), user->m_data->m_nWorldId, 2, value1, value2, name, value3,
        dataSize, dataSize, data
    );
}

void CDB::RequestBuilderBookMarkAdd(User* user, const char16_t* bookmarkName, int x, int y, int z) {
    // The S operand consumes the bookmark name directly as UTF-16.
    L2_GUARD_FUNCTION();
    user->SendDB("cddddSddd", 0xb2, user->GetDBID(), user->GetServerId(), 1, user->GetDBID(), bookmarkName, x, y, z);
}

void CDB::RequestBuilderBookMarkList(User* user, int value) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddd", 0xb2, user->GetDBID(), user->GetServerId(), 2, value);
}

void CDB::RequestBuilderBookMarkOne(User* user, int value) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddd", 0xb2, user->GetDBID(), user->GetServerId(), 3, value);
}

void CDB::RequestBuilderBookMarkDel(User* user, int value) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddd", 0xb2, user->GetDBID(), user->GetServerId(), 4, value);
}

void CDB::RequestBuilderBookMarkTelbyName(User* user, const char16_t* name) {
    // Current L2Server 0x0064ff1c publishes a Win64 UTF-16 wchar_t* operand.
    L2_GUARD_FUNCTION();
    user->SendDB("cdddS", 0xb2, user->GetDBID(), user->GetServerId(), 5, name);
}

void CDB::RequestDeleteSubJob(User* user, int subJobId) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddd", 0xdd, user->GetServerId(), user->GetDBID(), subJobId);
}

void CDB::RequestCancelDualClass(User* user, bool value) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chddd", 0xea, 0x11b, user->GetServerId(), user->GetDBID(), value);
    }
}

void CDB::RequestMainClassSkill(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdd", 0xe8, user->GetServerId(), user->GetDBID());
}

void CDB::RequestReloadSkill(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddd", 0xe2, user->GetServerId(), user->GetDBID(),
        user->m_data->m_nSubJobId
    );
}

void CDB::RequestLoadSubJob(User* user, bool value) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddddd", 0xdf, user->GetServerId(), user->GetDBID(), value, -1, 0, 0);
}

void CDB::RequestGetSubJobListForNpc(User* user, int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddddddd", 0xdf, user->GetServerId(), user->GetDBID(), 0, value1, value2, value3, value4);
}

void CDB::RequestGetXMasSealAmount(User* user) {
    // Current L2Server 0x006513c0.
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t databaseId = user->GetDBID();
    user->SendDB("cddd", 0xe0, databaseId, serverId, 2);
}

void CDB::RequestInitXMasSealAmount(User* user) {
    // Current L2Server 0x0065149c.
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t databaseId = user->GetDBID();
    user->SendDB("cddd", 0xe0, databaseId, serverId, 99);
}

void CDB::RequestGetPledgeAnnounce(User* user, unsigned int pledgeId, int value) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x1e, user->GetServerId(), pledgeId, value);
}

void CDB::RequestSetPledgeAnnounce(
    User* const user, const unsigned int pledgeId, const int value, const char16_t* const announcement
) {
    L2_GUARD_FUNCTION();
    const int serverId = static_cast<int>(user->GetServerId());
    user->SendDB(
        "chdddS", 0xea, 0x1f, serverId, pledgeId, value, announcement
    );
}

void CDB::RequestSwitchPledgeAnnounce(User* const user, const unsigned int pledgeId, const int value) {
    L2_GUARD_FUNCTION();
    const int serverId = static_cast<int>(user->GetServerId());
    user->SendDB("chddd", 0xea, 0x20, serverId, pledgeId, value);
}

void CDB::RequestSetAcademyMaster(User* user, unsigned int value1, unsigned int value2, unsigned int value3) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdddd", 0xea, 0x49, user->GetServerId(), value1, value2, value3);
}

void CDB::RequestGraduateAcademy(CPledge& pledge, User* const user) {
    // Current L2Server 0x00643c78.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const int worldId = user->m_data->m_nWorldId;
    if (worldId == g_nWorldId && worldId == pledge.GetWorldId()) {
        user->SendDB(
            "chdddd", 0xea, 0x48, pledge.GetDbId(), user->GetServerId(), user->GetDBID(),
            User::GetClassGroupNoByClassType(user->m_data->m_nClass)
        );
    }
}

void CDB::RequestPCCafeCouponCheck(User* user, const int value1, const int value2, const int value3, const int value4) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddddddd", 0xea, 0x4d, user->GetAccountID(), user->GetServerId(), user->m_data->m_nWorldId, value1, value2, value3,
        value4
    );
}

void CDB::RequestConvertItem(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdddd", 0xea, 0x77, user->GetServerId(), user->GetDBID(), value1, value2);
}

void CDB::RequestChangeMergedName(int value1, int value2, const char16_t* name, CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chdddS", 0xea, 0x78, socket->GetServerId(), value1, value2,
        name
    );
}

void CDB::RequestGetTutorial(User& user) {
    // Current L2Server 0x006563e8 evaluates the socket ID before the
    // character DBID.
    L2_GUARD_FUNCTION();
    const std::uint32_t userSocketId = user.GetSocket()->GetServerId();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdd", 0xea, 0x7a, characterId, userSocketId);
}

void CDB::RequestLoadUI(User& user) {
    // Current L2Server 0x00656aec evaluates the socket ID before the
    // character DBID.
    L2_GUARD_FUNCTION();
    const std::uint32_t userSocketId = user.GetSocket()->GetServerId();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdd", 0xea, 0x7d, characterId, userSocketId);
}

void CDB::RequestSaveUI(User& user, int dataSize, unsigned char* data) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdddb", 0xea, 0x7e, user.GetDBID(), user.GetSocket()->GetServerId(), dataSize, dataSize, data);
}

void CDB::RequestSetAssociatedInZone(User& user, unsigned int value1, int value2) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddd", 0xea, 0x82, user.GetDBID(), value1, value2);
}

void CDB::RequestSaveInventorySlotIndex(User* const user) {
    // Current L2Server 0x00656cd8, original owner DB.cpp.
    L2_GUARD_FUNCTION();

    std::array<char, 20480> packet{};
    int packetSize = 0;
    const int itemCount = user->m_inv.MakeSlotIndexPacket(packet.data(), packetSize);
    if (itemCount > 0) {
        user->SendDB("chddb", 0xea, 0x80, static_cast<int>(user->GetDBID()), itemCount, packetSize, packet.data());
    }
}

void CDB::RequestWaitingTransferPledgeMaster(CPledge& pledge) {
    // Current L2Server 0x00656ed4.
    L2_GUARD_FUNCTION();
    const std::uint32_t pledgeDatabaseId = pledge.GetDbId();
    pledge.SendDB("chd", 0xea, 0x83, pledgeDatabaseId);
}

void CDB::RequestChangeNicknameColor(User& user, int color, const char16_t* name) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddS", 0xea, 0x9f, user.GetDBID(), color, name);
}

void CDB::SetBirthday(User& user, int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdddd", 0xea, 0xb3, user.GetDBID(), value1, value2, value3);
}

void CDB::LoadUserBookmark(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x91, 0, user->GetDBID(), user->GetServerId());
}

void CDB::LoadPeriodicItemInfo(User* user) {
    // Current L2Server 0x0065819c, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x92, 0, characterId, serverId);
    }
}

void CDB::RemoveExpiredPeriodicItem(
    const unsigned int itemDatabaseId, const unsigned int itemServerId, User& user, const int itemType
) {
    // Current L2Server 0x00658280, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user.GetServerId();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdddddd", 0xea, 0x92, 2, itemDatabaseId, itemServerId, characterId, serverId, itemType);
}

void CDB::RequestSaveRetrieveGoods(User* user, std::int64_t goodsUid, int itemPrimaryId, int itemClassId, int itemAmount) {
    // Current L2Server 0x0065ca60, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    user->SendDB("chQdddd", 0xea, 0x111, goodsUid, user->GetDBID(), itemPrimaryId, itemClassId, itemAmount);
}

void CDB::AddUserBookmarkSlot(User* user, int slot) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x91, 1, user->GetDBID(), slot);
}

void CDB::SaveUserBookmarkSlot(
    User* user, int slot, FVector& position, int value, const char16_t* name, const char16_t* tag
) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chdddddddSS", 0xea, 0x91, 2, user->GetDBID(), slot, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), value, name, tag
    );
}

void CDB::DeleteUserBookmarkSlot(User* user, int slot) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x91, 3, user->GetDBID(), slot);
}

void CDB::ChangeUserBookmarkSlot(User* user, int fromSlot, int toSlot) {
    // Current L2Server.exe 0x00657abc.
    L2_GUARD_FUNCTION();
    user->SendDB("chdddd", 0xea, 0x91, 4, user->GetDBID(), fromSlot, toSlot);
}

void CDB::RequestSaveDuelRecord(
    User* user,
    int individualWinCount,
    int individualLoseCount,
    int individualDrawCount,
    int partyWinCount,
    int partyLoseCount,
    int partyDrawCount
) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->SendDB(
            "chddddddd", 0xea, 0x5c, user->GetDBID(), individualWinCount, individualLoseCount, individualDrawCount,
            partyWinCount, partyLoseCount, partyDrawCount
        );
    }
}

void CDB::RequestLoadPVPPointRestrain(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chd", 0xea, 0x88, user->GetDBID());
}

void CDB::RequestSavePVPPointRestrain(User* user, int giveCount, int giveTime) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x87, user->GetDBID(), giveCount, giveTime);
}

void CDB::RequestLoadBossRecord(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x2a, user.GetDBID());
}

void CDB::RequestLoadDuelRecord(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chd", 0xea, 0x5d, user->GetDBID());
    }
}

void CDB::RequestLoadBirthInfo(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chd", 0xea, 0xa9, user->GetDBID());
}

void CDB::RequestLoad5YearInfo(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chd", 0xea, 0xac, user->GetDBID());
}

void CDB::RequestLoadPetItemCount(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0xc2, user.GetDBID());
}

void CDB::RequestLoadPostFriend(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0xd1, user.GetDBID());
}

void CDB::RequestAddPostFriend(User& user, const char16_t* name) {
    // Current L2Server 0x0065aacc.
    L2_GUARD_FUNCTION();
    user.SendDB("chdS", 0xea, 0xd0, user.GetDBID(), name);
}

void CDB::RequestDeletePostFriend(User& user, const char16_t* name) {
    // Current L2Server 0x0065ab98.
    L2_GUARD_FUNCTION();
    user.SendDB("chdS", 0xea, 0xd2, user.GetDBID(), name);
}

void CDB::RequestWritePost(
    User& user,
    const char16_t* receiver,
    int type,
    const char16_t* subject,
    const char16_t* body,
    int itemCount,
    int dataSize,
    char* data,
    std::int64_t money,
    int haveItem,
    std::int64_t fee
) {
    // Current L2Server.exe 0x00659618.
    L2_GUARD_FUNCTION();
    if (user.m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    int dailyLimit = 0;
    if (g_bDailyPostLimitForFreeUser && F2PConfig::IsDailyPostLimitUser(&user)) {
        dailyLimit = 2;
    }
    user.SendDB(
        "chdSdSSdbQdQd", 0xea, 0xb6, user.GetDBID(), receiver, type, subject, body, itemCount, dataSize, data, money,
        haveItem, fee, dailyLimit
    );
}

void CDB::RequestLoadSummons(User* user, int summonType) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("cdd", 0x03, summonType, user->GetDBID());
    }
}

void CDB::WithdrawPet(User* user, CItem* petCollar, bool newlySummoned) {
    L2_GUARD_FUNCTION();

    if (petCollar != nullptr && petCollar->IsEtcItem()) {
        CEtcItem* const etcItem = petCollar->CastEtcItem();
        etcItem->m_bPetCollarTradable = false;
        user->SendDB(
            "cddddddd", 0x77, user->GetServerId(), user->GetDBID(), petCollar->Id(), petCollar->GetDBID(),
            petCollar->m_data->GetEnchanted(), newlySummoned, user->m_nReloadPet
        );
        runtime::raw_atomic::ExchangeI32(&user->m_nReloadPet, 0);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] void CDB::WithdrawPet(User *pUser, CItem *pItem) "
    //     L"failed. not a etc item",
    //     L"db.cpp",
    //     0xee8)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[pet] Pet withdrawal not requested for character DB ID %u: %ls. Clearing the pending item-job flag.",
    //     user->GetDBID(),
    //     petCollar == nullptr ? L"collar is missing" : L"collar is not an etc item")
    L2_LOG_ERROR(
        Log, u"[pet] Pet withdrawal not requested for character DB ID %u: %ls; clearing the pending item-job flag.",
        user->GetDBID(), petCollar == nullptr ? L"collar is missing" : L"collar is not an etc item"
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
}

void CDB::LoadPetItems(CPet* pet, User& user, bool loadInventory, int requestValue) {
    // Current L2Server 0x0064a7a0.
    L2_GUARD_FUNCTION();
    user.SendDB("cddddd", 0x78, pet->Id(), pet->GetDBID(), user.GetDBID(), loadInventory, requestValue);
}

void CDB::GiveItemToPet(User* user, CItem* item, std::int64_t amount) {
    // Current L2Server 0x0066ea78.
    L2_GUARD_FUNCTION();

    CPet* const pet = user->GetPet();
    if (pet != nullptr && pet->GetMaster() == user) {
        user->SendDB(
            "cdddddQd", 0x7f, user->Id(), user->GetDBID(), pet->Id(), pet->GetDBID(), item->GetDBID(), amount,
            item->m_data->m_ItemConsumeType
        );
        return;
    }
    user->_DoneManiItemJob(ManipulateType::MANI_PET_ITEM, 0);
}

void CDB::GetItemFromPet(User* user, CItem* item, std::int64_t amount) {
    // Current L2Server 0x0066ec10.
    L2_GUARD_FUNCTION();

    CPet* const pet = user->GetPet();
    if (pet != nullptr && pet->GetMaster() == user) {
        user->SendDB(
            "cdddddQd", 0x80, user->Id(), user->GetDBID(), pet->Id(), pet->GetDBID(), item->GetDBID(), amount,
            item->m_data->m_ItemConsumeType
        );
        return;
    }
    user->_DoneManiItemJob(ManipulateType::MANI_PET_ITEM, 0);
}

void CDB::SaveSummonInfectedSkills(
    User& user, int npcClassId, int summonIndex, AtomicJob* atomicJob, int skillCount, int packetLength, char* packet
) const {
    L2_GUARD_FUNCTION();

    const int atomicJobId = atomicJob == nullptr ? 0 : PushAtomicJobHelper(atomicJob);
    const std::uint32_t databaseId = user.GetDBID();
    if (skillCount < 1) {
        user.SendDB("chdddddd", 0xea, 0xc4, user.GetServerId(), databaseId, npcClassId, summonIndex, atomicJobId, 0);
        return;
    }
    user.SendDB(
        "chddddddb", 0xea, 0xc4, user.GetServerId(), databaseId, npcClassId, summonIndex, atomicJobId, skillCount,
        packetLength, packet
    );
}

void CDB::SavePetInfo(CPet* pet, AtomicJob* atomicJob) {
    L2_GUARD_FUNCTION();
    if (pet == nullptr) {
        return;
    }

    CSPointer<CItem> rightHandItem = CItemFactory::Inst().FindObjectSP(pet->m_data->m_nSlotIndex[ST_RHAND]);
    CSPointer<CItem> chestItem = CItemFactory::Inst().FindObjectSP(pet->m_data->m_nSlotIndex[ST_CHEST]);
    CSPointer<CItem> neckItem = CItemFactory::Inst().FindObjectSP(pet->m_data->m_nSlotIndex[ST_NECK]);
    const std::uint32_t rightHandDbId = rightHandItem ? rightHandItem->GetDBID() : 0U;
    const std::uint32_t chestDbId = chestItem ? chestItem->GetDBID() : 0U;
    const std::uint32_t neckDbId = neckItem ? neckItem->GetDBID() : 0U;

    double hp = pet->m_data->GetHP();
    double mp = pet->m_data->GetMP();
    if (pet->IsInDuel()) {
        User* const master = pet->GetMaster();
        if (master != nullptr && master->IsInDuel()) {
            master->m_DuelUserInfo.GetSummonOriginalStatus(static_cast<int>(pet->Id()), hp, mp);
        }
    }

    const int atomicJobId = PushAtomicJobHelper(atomicJob);
    pet->SendDB(
        "cddddQdffQddddQddd", 0x7a, 0, pet->m_MasterUserSID, pet->m_MasterUserDBID, pet->GetDBID(), pet->m_data->m_nExp,
        pet->m_data->m_nLevel, hp, mp, pet->m_data->m_nSP, pet->m_data->m_Meal, rightHandDbId, chestDbId, neckDbId,
        pet->m_nDownExp, static_cast<int>(pet->IsAlive()), pet->m_nDeathCount.load(std::memory_order_relaxed), atomicJobId
    );
}

void CDB::ChangePetName(CPet* pet, const char16_t* name) {
    // Current L2Server 0x0064aa3c.
    L2_GUARD_FUNCTION();
    pet->SendDB(
        "cddSd", 0x7b, static_cast<int>(pet->Id()), static_cast<int>(pet->GetDBID()),
        name, static_cast<int>(pet->m_MasterUserDBID)
    );
}

void CDB::SavePetNameChanged(CPet& pet) {
    // Current L2Server 0x0064a96c.
    L2_GUARD_FUNCTION();
    pet.SendDB("cddd", 0x7a, 2, static_cast<int>(pet.m_MasterUserDBID), static_cast<int>(pet.GetDBID()));
}

void CDB::SaveStriderMeal(User* user, CUserRidingInfo& ridingInfo) {
    // Current L2Server 0x0064a8a0.
    L2_GUARD_FUNCTION();
    user->SendDB("cddd", 0x7a, 1, ridingInfo.DBID(), ridingInfo.Meal());
}

void CDB::RequestDeleteSummon(User& user, int summonIndex, AtomicJob* atomicJob) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdddd", 0xea, 0xf1, user.GetServerId(), user.GetDBID(), summonIndex, PushAtomicJobHelper(atomicJob));
}

void CDB::DepositPet(User* user, CPet* pet, bool complete, AtomicJob* atomicJob) {
    L2_GUARD_FUNCTION();
    if (!complete) {
        SavePetInfo(pet, atomicJob);
    } else if (atomicJob == g_AtomicPetDBJob) {
        user->OnPetStep(5);
    }

    user->SendDB("cdddddc", 0x79, user->Id(), user->GetDBID(), pet->Id(), pet->GetDBID(), pet->Level(), complete);
}

void CDB::ChangeSSQDropGuardStatus(bool status) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x10, status);
}

void CDB::SetDawnRoundToPledge(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x06, value1, value2);
}

void CDB::StartSSQEvent(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x07, value);
}

void CDB::InitTimeAttackRecord(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x08, value);
}

void CDB::LoadTimeAttackInfo() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x03);
}

void CDB::RequestLoadMarketPriceList() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x15);
}

void CDB::RequestLoadAllNobless(bool value1, bool value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x17, value1, value2);
}

void CDB::RequestReloadNobless(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x18, value);
}

void CDB::RequestLoadCursedWeaponList() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x38);
}

void CDB::RequestAddCursedWeapon(
    int value1, int value2, int value3, int value4, double value5, int value6, int value7, int value8, int value9
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddfdddd", 0xea, 0x39, value1, value2, value3, value4, value5, value6, value7, value8, value9);
}

void CDB::RequestUpdateCursedWeapon(int value1, double value2, int value3, int value4, int value5, int value6) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdfdddd", 0xea, 0x3a, value1, value2, value3, value4, value5, value6);
}

void CDB::RequestDelCursedWeapon(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x3b, value);
}

void CDB::RequestDelCursedWeaponOnlyInDB(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x3c, value1, value2);
}

void CDB::RequestLoadPledgeRanking() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x3d);
}

void CDB::RequestSetPledgePowerGrade(unsigned int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x3e, value1, value2);
}

void CDB::UpdatePledgeNameValue(
    unsigned int pledgeServerId, unsigned int pledgeId, int requestContext, int nameValueDelta, int worldId
) {
    L2_GUARD_FUNCTION();
    if (worldId != g_nWorldId) {
        return;
    }
    if (nameValueDelta > 0) {
        nameValueDelta = runtime::Win64CvttDoubleToInt32(static_cast<double>(nameValueDelta) * g_dPledgeNameValuePer);
    }
    SendDB(worldId, "chdddd", 0xea, 0x35, pledgeServerId, pledgeId, requestContext, nameValueDelta);
}

void CDB::UpdatePledgeNameValueByPledgeWar(
    unsigned int value1, unsigned int value2, unsigned int pledgeId, int value4, int nameValueDelta, unsigned int value6
) {
    L2_GUARD_FUNCTION();
    if (nameValueDelta > 0) {
        nameValueDelta = runtime::Win64CvttDoubleToInt32(static_cast<double>(nameValueDelta) * g_dPledgeNameValuePer);
    }
    SendDB(g_nWorldId, "chdddddd", 0xea, 0x145, value1, value2, pledgeId, value4, nameValueDelta, value6);
}

void CDB::UpdateDefenceSiegeCount(CPledge& pledge, const int defenceCount) {
    // Current L2Server 0x00653920.
    L2_GUARD_FUNCTION();
    const std::uint32_t pledgeServerId = pledge.Id();
    const std::uint32_t pledgeDatabaseId = pledge.GetDbId();
    pledge.SendDB("chddd", 0xea, 0x81, pledgeServerId, pledgeDatabaseId, defenceCount);
}

void CDB::RequestLoadFieldCycleInfo(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x61, value);
}

void CDB::RequestSaveFieldCycleInfo(int value1, int value2, int value3, int value4, int value5, int value6, int value7) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddddd", 0xea, 0x60, value1, value2, value3, value4, value5, value6, value7);
}

void CDB::RequestLoadAllAuction(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x6e, value);
}

void CDB::RequestLoadAllAuctionBid(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x6f, value);
}

void CDB::RequestCreateItemAuction(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddd", 0xea, 0x71, value1, value2, value3, value4);
}

void CDB::RequestSetEndTime(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x73, value1, value2, value3);
}

void CDB::RequestSetAuctionStatus(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x74, value1, value2);
}

void CDB::RequestSetAuctionItem(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x75, value1, value2);
}

void CDB::RequestLoadInstantZoneFlag(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0xca, value1, value2);
}

void CDB::RequestCreateInstantZone(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddd", 0xea, 0xcb, value1, value2, value3, value4);
}

void CDB::RequestLoadInZoneRestriction(User* user) {
    // Current L2Server 0x006564d0 evaluates the user SID before the DBID.
    L2_GUARD_FUNCTION();
    const std::uint32_t userServerId = user->GetServerId();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB("chdd", 0xea, 0x7b, characterId, userServerId);
}

void CDB::RequestMarkInZoneRestriction(
    std::CDualKey<int, int> userKey,
    int inzoneTypeId,
    int latestBindingTime,
    int entranceCount,
    bool groupRestriction,
    int maxEntranceCount,
    int inzoneDatabaseId
) {
    L2_GUARD_FUNCTION();
    SendDB(
        userKey.m_PrimaryKey, "chddddddd", 0xea, 0x7c, userKey.m_SecondaryKey, inzoneTypeId, latestBindingTime,
        entranceCount, groupRestriction, maxEntranceCount, inzoneDatabaseId
    );
}

void CDB::RequestResetInZoneRestriction(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x7f, user.GetDBID());
}

void CDB::RequestLoadUserInzonePoint(User& user) {
    L2_GUARD_FUNCTION();
    protocol::RQ_LOAD_USER_INZONE_POINT request;
    request.nUserSID = static_cast<int>(user.GetServerId());
    request.nUserDBID = static_cast<int>(user.GetDBID());
    user.SendDB(static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1a3), request);
}

void CDB::RequestUpdateUserInzonePoint(User& user, int inzoneTypeId, int point, int currentTime) {
    L2_GUARD_FUNCTION();
    protocol::RQ_UPDATE_USER_INZONE_POINT request;
    request.nUserDBID = static_cast<int>(user.GetDBID());
    request.nInzoneTypeId = inzoneTypeId;
    request.nPoint = point;
    request.nCurTime = currentTime;
    user.SendDB(static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1a2), request);
}

void CDB::RequestLoadInzoneMaxDBID() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0xcd);
}

void CDB::RequestSaveInstantZoneFlag(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xcc, value1, value2, value3);
}

void CDB::RequestLoadAirShip() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0xb4, 2);
}

void CDB::RequestSaveAirShip(unsigned int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xb4, 1, value1, value2);
}

void CDB::RequestRemoveAirShip(unsigned int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xb4, 3, value1, value2);
}

void CDB::RequestSendPost(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0xb7, value);
}

void CDB::RequestRejectPost(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xb9, value1, value2, value3);
}

void CDB::RequestOpenPost(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xbc, value1, value2, value3);
}

void CDB::RequestLoadPostList(User& user) {
    // Current L2Server.exe 0x00659d94.
    L2_GUARD_FUNCTION();
    if (user.m_data->m_nWorldId == g_nWorldId) {
        user.SendDB("chd", 0xea, 0xbd, user.GetDBID());
    }
}

void CDB::RequestReceivedPostList(User& user) {
    // Current L2Server 0x00659e6c.
    L2_GUARD_FUNCTION();
    if (user.m_data->m_nWorldId == g_nWorldId) {
        user.SendDB("chd", 0xea, 0xbe, user.GetDBID());
    }
}

void CDB::RequestSentPostList(User& user) {
    // Current L2Server 0x00659f44.
    L2_GUARD_FUNCTION();
    if (user.m_data->m_nWorldId == g_nWorldId) {
        user.SendDB("chd", 0xea, 0xbf, user.GetDBID());
    }
}

void CDB::RequestTPacketProcess(int worldId, short protocol, int dataSize, CStackOnly& packet) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chb", 0xe4, protocol, dataSize, &packet);
}

void CDB::RequestTQuery(const int worldId, const short protocol, void* const signal, const int dataSize, QueryBase& packet) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chppb", 0xe5, protocol, signal, &packet, dataSize, &packet);
}

int CDB::PushAtomicJobHelper(AtomicJob* const atomicJob) {
    return Singleton<ObjectCache<AtomicJob, 10240>>::inst().Push(atomicJob);
}

void CDB::RequestSaveVitality(IPacketHelper& packet, int worldId) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chb", 0xea, 0x10a, packet.GetLength(), packet.GetPacket());
}

void CDB::RequestCheckRaidWorld(int worldId) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chd", 0xea, 0xc7, worldId);
}

void CDB::RequestInsertEventRanking(int value1, int value2, const char16_t* value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddSd", 0xea, 0xda, value1, value2, value3, value4);
}

void CDB::RequestLoadEventTopRankers(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0xdb, value1, value2, value3);
}

void CDB::RequestUpdateEventRanking(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0xdc, value1, value2);
}

void CDB::RequestGetEventScore(int value1, int value2, int value3, const char16_t* value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddS", 0xea, 0xdd, value1, value2, value3, value4);
}

void CDB::RequestSetEventScore(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddd", 0xea, 0xde, value1, value2, value3, value4);
}

void CDB::RequestLoadEventDataFromDB() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0xee);
}

void CDB::RequestDumpEvent(int index, int total, std::u16string eventName, std::u16string contents) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddSS", 0xea, 0xf0, index, total, eventName.c_str(), contents.c_str());
}

void CDB::RequestDBSecuritySetting(DBSecurity::BuilderCmdType setting) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0xf3, static_cast<std::int32_t>(setting));
}

void CDB::ResetProfile() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0xf8);
}

void CDB::ReportProfile(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0xf9, value);
}

void CDB::RequestLoadAllDynamicQuest() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chc", 0xea, 0xfb, 0);
}

void CDB::RequestSaveDynamicQuest(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chcdd", 0xea, 0xfb, 1, value1, value2);
}

void CDB::RequestSetCastleCampaign(CResidence* residence, int result) {
    L2_GUARD_FUNCTION();
    residence->SendDB("chdd", 0xea, 300, residence->m_data->m_nResidenceID, result);
}

void CDB::ReqOwnCastle(
    CCastle* const castle,
    const int ownerId,
    const int oldOwnerId,
    const int castleType,
    const int worldId,
    const int oldWorldId
) {
    // Current L2Server 0x00644c80.
    L2_GUARD_FUNCTION();
    if ((ownerId == 0 || worldId > 0) && (oldOwnerId == 0 || oldWorldId > 0)) {
        castle->SendDB(
            "cdddddd", 0x19, castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, ownerId, oldOwnerId, castleType,
            worldId, oldWorldId
        );
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"Wrong worldID %d oldWorldID %d",
    //     worldId,
    //     oldWorldId)
    // Log.Add(
    //     LOG_ERROR,
    //     L"[db] Castle ownership update not requested: new clan %d in world %d, old clan %d in world %d. Each nonzero clan
    //     ID requires a positive world ID.", ownerId, worldId, oldOwnerId, oldWorldId)
    L2_LOG_ERROR(
        Log,
        u"[db] Castle ownership update not requested: new clan %d in world %d, old clan %d in world %d; each nonzero clan "
        u"ID requires a positive world ID.",
        ownerId, worldId, oldOwnerId, oldWorldId
    );
}

void CDB::ReqWriteTax(
    CResidence* const residence, const int kind, const int oldRate, const int newRate, const bool isCastle
) {
    // Current L2Server 0x00644e80.
    L2_GUARD_FUNCTION();
    residence->SendDB(
        "cdddddd", 0x1c, isCastle, residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0,
        residence->m_data != nullptr ? residence->m_data->m_nResidenceOwnerID : 0, kind, oldRate, newRate
    );
}

void CDB::RequestSetCastleType(CCastle& castle, const int residenceId, const int castleType) {
    // Current L2Server 0x0064c700.
    L2_GUARD_FUNCTION();
    castle.SendDB("chdd", 0xea, 0x134, residenceId, castleType);
}

void CDB::RequestSetCompeteAgitStatus(CCompeteAgit& competeAgit, const unsigned int status) {
    // Current L2Server 0x0064d1bc.
    L2_GUARD_FUNCTION();
    competeAgit.SendDB("chdd", 0xea, 0x13b, competeAgit.m_data != nullptr ? competeAgit.m_data->m_nResidenceID : 0, status);
}

void CDB::RequestResetCompeteAgitStatus(CCompeteAgit& competeAgit) {
    // Current L2Server 0x0064d348.
    L2_GUARD_FUNCTION();
    competeAgit.SendDB("chd", 0xea, 0x13d, competeAgit.m_data != nullptr ? competeAgit.m_data->m_nResidenceID : 0);
}

void CDB::RequestCompeteAgitWinner(CCompeteAgit& competeAgit, const unsigned int pledgeId, const bool accepted) {
    // Current L2Server 0x0064d40c.
    L2_GUARD_FUNCTION();
    competeAgit.SendDB(
        "chddd", 0xea, 0x13c, competeAgit.m_data != nullptr ? competeAgit.m_data->m_nResidenceID : 0, pledgeId, accepted
    );
}

void CDB::RequestSetPreviouseAgitOwnerID(CCompeteAgit& competeAgit, const int previousOwnerId) {
    // Current L2Server 0x00654850. Keep the shipped `Previouse` spelling.
    L2_GUARD_FUNCTION();
    competeAgit.SendDB(
        "chdd", 0xea, 0x144, competeAgit.m_data != nullptr ? competeAgit.m_data->m_nResidenceID : 0, previousOwnerId
    );
}

void CDB::RequestLoadInstantAgit() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chc", 0xea, 0x109, 7);
}

void CDB::RequestLoadInstantAgitDeco() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chc", 0xea, 0x109, 0x0b);
}

void CDB::RequestLoadInstantAgitDecoNpc() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chc", 0xea, 0x109, 0x0d);
}

void CDB::RequestTryChooseInstantAgitOwner(
    int instantAgitId, int nextLotteryDate, std::int64_t rentalFee, IPacketHelper& packet
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chcddQdb", 0xea, 0x109, 6, instantAgitId, nextLotteryDate, rentalFee, packet.GetAssembledCount(),
        packet.GetLength(), packet.GetPacket()
    );
}

void CDB::RequestClearEventTime(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chd", 0xea, 0xcf, user->GetDBID());
}

void CDB::RequestClearOneEventTime(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x115, value1, value2);
}

void CDB::RequestCharLevelUp(User& user, int nameValueDelta, const int value2, const int value3) {
    // Current L2Server 0x0065e610.
    L2_GUARD_FUNCTION();
    if (nameValueDelta > 0) {
        nameValueDelta = runtime::Win64CvttDoubleToInt32(static_cast<double>(nameValueDelta) * g_dPledgeNameValuePer);
    }

    CPledge* const pledge = user.GetPledge();
    if (pledge != nullptr) {
        user.SendDB(
            "chdddddddd", 0xea, 0x151, user.GetServerId(), pledge->Id(), user.GetDBID(), nameValueDelta, value2,
            user.m_data->m_nSubJobId, user.Level(), value3
        );
    }
}

void CDB::RequestModifyAppliedUserInfo(User* const user) {
    // Current L2Server 0x0065fc98.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chddd", 0xea, 0x168, user->GetDBID(), user->m_data->m_nLevel, user->GetClass());
    }
}

void CDB::LoadPeriodicShapedShiftedItemInfo(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x155, 0, characterId, serverId);
    }
}

void CDB::RequestUpdateMaxLevel(User& user, unsigned int level) {
    L2_GUARD_FUNCTION();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chddd", 0xea, 0x169, characterId, user.m_data->m_nSubJobId, level);
}

void CDB::RequestUpdateSociality(User& user, int argument, _SYSTEMTIME* time) {
    // Current L2Server 0x0064e73c.
    L2_GUARD_FUNCTION();
    user.SendDB("cdddb", 199, user.GetDBID(), user.GetSocialityCount(), argument, 0x10, time);
}

void CDB::RequestUpdateUserRaceSex(User* const user, const RaceType race, const SexType sex) {
    // Current L2Server 0x0065ad28.
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0xea, user->GetDBID(), race, sex);
}

void CDB::RequestDeletePetitionExternalNotify(User& user) {
    L2_GUARD_FUNCTION();
    const int worldId = user.m_data->m_nWorldId;
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdd", 0xea, 0x16b, worldId, characterId);
}

void CDB::RequestLoadPetitionExternalNotify(User& user) {
    L2_GUARD_FUNCTION();
    const int worldId = user.m_data->m_nWorldId;
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdd", 0xea, 0x16c, worldId, characterId);
}

void CDB::RequestLoadAccountMaxLevel(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x16e, value);
}

void CDB::RequestReloadUserFlagOn(User& user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t characterId = user.GetDBID();
    SendDB(g_nWorldId, "chdd", 0xea, 0x16f, characterId, 1);
}

void CDB::RequestBeautyPurchaseList(User& user, char requestFlag) {
    L2_GUARD_FUNCTION();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdc", 0xea, 0x175, characterId, static_cast<int>(requestFlag));
}

void CDB::RequestUpdateHairAccFlag(User& user) {
    // Current L2Server 0x00660614.
    L2_GUARD_FUNCTION();
    user.SendDB("chdc", 0xea, 0x176, user.GetDBID(), user.m_bHairAccFlag);
}

void CDB::RequestBeautyLoad(int groupCount, CPacketHelperEx<20480> packet) {
    L2_GUARD_FUNCTION();
    const int packetLength = packet.GetLength();
    char* const packetData = packet.GetPacket();
    SendDB(g_nWorldId, "chdb", 0xea, 0x146, groupCount, packetLength, packetData);
}

void CDB::RequestProductSoldCount(int productCount, CPacketHelperEx<20480>& packet) {
    SendDB(g_nWorldId, "chdb", 0xea, 0xd8, productCount, packet.GetLength(), packet.GetPacket());
}

void CDB::RequestBuyBeautyItem(
    User* user, int builder, std::int64_t adenaPrice, std::int64_t beautyCoinPrice, CPacketHelperEx<20480> packet
) {
    L2_GUARD_FUNCTION();
    const int packetLength = packet.GetLength();
    char* const packetData = packet.GetPacket();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB("chddQQb", 0xea, 0x147, characterId, builder, adenaPrice, beautyCoinPrice, packetLength, packetData);
}

void CDB::RequestResetBeautyItem(User* user, int builder, std::int64_t adenaPrice, CPacketHelperEx<20480> packet) {
    L2_GUARD_FUNCTION();
    const int packetLength = packet.GetLength();
    char* const packetData = packet.GetPacket();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB("chddQQb", 0xea, 0x148, characterId, builder, adenaPrice, std::int64_t{0}, packetLength, packetData);
}

void CDB::RequestLoadBalthusEventUserInfo(User& user) {
    // Current L2Server 0x00666d44.
    L2_GUARD_FUNCTION();
    protocol::RQ_LOAD_BALTHUS_EVENT_USER_INFO packet;
    packet.nUserSID = static_cast<int>(user.GetServerId());
    packet.nUserDBID = static_cast<int>(user.GetDBID());
    user.SendDB(RQ_EX, RQ_EX_LOAD_BALTHUS_EVENT_USER_INFO, packet);
}

void CDB::SendAcquiredAbilitySkill(User& user, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    const bool dualClass = user.m_data->m_IsDualClass;
    char* const packetData = packet.GetPacket();
    const int packetLength = packet.GetLength();
    const std::uint32_t serverId = user.GetServerId();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chdddb", 0xea, 0x177, characterId, serverId, static_cast<int>(dualClass), packetLength, packetData);
}

void CDB::RequestResetAbilitySkillToDB(User& user, std::int64_t costSp) {
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user.GetServerId();
    const std::uint32_t characterId = user.GetDBID();
    user.SendDB("chddQ", 0xea, 0x178, characterId, serverId, costSp);
}

void CDB::LoadPeriodicHennaInfo(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t serverId = user->GetServerId();
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x182, 0, characterId, serverId);
    }
}

void CDB::RequestLoadItems(User* user, bool forceReload) const {
    // Current L2Server.exe 0x00642564, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     L"db.cpp",
        //     0x110)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Item load not requested: character is unavailable (force reload: %ls).",
        //     forceReload ? L"yes" : L"no")
        L2_LOG_ERROR(
            Log, u"[db] Item load not requested: character is unavailable (force reload: %ls).", forceReload ? L"yes" : L"no"
        );
        return;
    }
    user->SendDB("cddd", 4, static_cast<int>(user->GetDBID()), static_cast<int>(user->GetServerId()), forceReload);
}

void CDB::RemoveExpiredPeriodicHenna(User* user, int hennaId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB("chdddd", 0xea, 0x182, 2, hennaId, characterId, serverId);
}

void CDB::RequestLoadChar(unsigned int accountId, CUserSocket* socket) const {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdd", RQ_LOAD_CHARACTER, accountId, socket->GetServerId());
}

void CDB::RequestCreateChar(
    const char16_t* const characterName,
    const int race,
    const int sex,
    const int classId,
    const int,
    const int,
    const int constitution,
    const int,
    const int,
    const int mentality,
    const int hairStyle,
    const int hairColor,
    const int face,
    CUserSocket* const socket,
    const int level
) const {
    // Current L2Server 0x00663c08.
    L2_GUARD_FUNCTION();

    const ClassType classType = static_cast<ClassType>(classId);
    const double originalHp = g_PCParamDB.GetOrgHP(classType, level);
    const double constitutionBonus = g_PCParamDB.GetParamBonus(PC_CON_BONUS, constitution);
    const double hp = originalHp * constitutionBonus;
    const double originalMp = g_PCParamDB.GetOrgMP(classType, level);
    const double mentalityBonus = g_PCParamDB.GetParamBonus(PC_MEN_BONUS, mentality);
    const double mp = originalMp * mentalityBonus;

    const FVector startPoint = Singleton<CSetting>::inst().GetRandomStartPoint(classType);
    const int x = runtime::Win64CvttDoubleToInt32(startPoint.x);
    const int y = runtime::Win64CvttDoubleToInt32(startPoint.y);
    const int z = runtime::Win64CvttDoubleToInt32(startPoint.z);

    const unsigned int accountId = socket->GetAccountID();
    const auto* const addressBytes = reinterpret_cast<const unsigned char*>(&socket->m_Addr);
    const int address4 = addressBytes[3];
    const int address3 = addressBytes[2];
    const int address2 = addressBytes[1];
    const int address1 = addressBytes[0];
    const unsigned int serverId = socket->GetServerId();
    const SharedUtilsWideChar* const accountName = socket->GetAccountName();
    const std::int64_t experience = ExpByLevel[level - 1];

    const SharedUtilsWideChar* const packetCharacterName = characterName;

    SendDB(
        g_nWorldId, "cSSddddddddffdddddddddQ", RQ_CREATE_CHARACTER, packetCharacterName, accountName, accountId, sex, race,
        classId, g_nWorldId, x, y, z, hp, mp, hairStyle, hairColor, face, serverId, address1, address2, address3, address4,
        level, experience
    );
}

void CDB::RequestDeleteChar(const unsigned int accountId, const unsigned int characterId, CUserSocket* const socket) const {
    // Current L2Server 0x0066429c.
    L2_GUARD_FUNCTION();
    const unsigned int serverId = socket->GetServerId();
    SendDB(g_nWorldId, "cddd", RQ_DELETE_CHARACTER, accountId, characterId, serverId);
}

void CDB::RestoreCharacter(const int characterId, const unsigned int accountId, CUserSocket* const socket) {
    // Current L2Server 0x00664c08.
    L2_GUARD_FUNCTION();
    const unsigned int serverId = socket->GetServerId();
    SendDB(g_nWorldId, "cddd", RQ_RESTORE_CHAR, characterId, accountId, serverId);
}

void CDB::RequestPremiumKamarokaPersonalRankInfo(int round, User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t characterId = user->GetDBID();
        SendDB(g_nWorldId, "chdd", 0xea, 0x191, round, characterId);
    }
}

void CDB::RequestPremiumKamarokaAddNewRecord(
    int round, int score, int leaderCharacterId, std::u16string characterList, CPacketHelperEx<20480>& packet
) {
    L2_GUARD_FUNCTION();
    char* const packetData = packet.GetPacket();
    const int packetLength = packet.GetLength();
    const int memberCount = packet.GetAssembledCount();
    SendDB(
        g_nWorldId, "chdddSdb", 0xea, 0x190, round, score, leaderCharacterId, characterList.c_str(), memberCount,
        packetLength, packetData
    );
}

void CDB::RequestLoadWorldChatPoint(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x187, user->GetDBID(), g_WorldChat.nMaxChatCnt);
    }
}

void CDB::RequestIncWorldChatPoint(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        std::uint32_t databaseId;
        int consumePoint;
        if (user->CheckPremiumType(PremiumUserType::PUT_FREE_USER)) {
            databaseId = user->GetDBID();
            consumePoint = g_WorldChat.nComsumePointForFree;
        } else {
            databaseId = user->GetDBID();
            consumePoint = g_WorldChat.nComsumePointForPremium;
        }
        user->SendDB("chddd", 0xea, 0x188, databaseId, consumePoint, g_WorldChat.nMaxChatCnt);
    }
}

void CDB::RequestResetWorldChatPoint() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x189, g_WorldChat.tNextResetTime, g_WorldChat.nMaxChatCnt);
}

void CDB::RequestWorldChatResetTime() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x18a);
}

void CDB::RequestPremiumKamarokaRound() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x18e);
}

void CDB::RequestPremiumKamarokaRank(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x18f, value1, value2);
}

void CDB::RequestLoadClientINI(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x193, value);
}

void CDB::RequestSaveClientINI(int accountId, char type, std::string value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdhs", 0xea, 0x194, accountId, static_cast<int>(type), value.c_str());
}

void CDB::RequestLoadDroidNPCAppearance(
    DROIDNPC_COMMAND::DROIDNPC_COMMAND_TYPE command,
    unsigned int requestContext,
    unsigned int accountId,
    unsigned int characterId
) {
    L2_GUARD_FUNCTION();
    const auto wireCommand = static_cast<std::uint32_t>(command);
    if (wireCommand < 2U) {
        SendDB(
            g_nWorldId, "chdddd", 0xea, 0x195, static_cast<std::int32_t>(command), requestContext, accountId, characterId
        );
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid command(%d)",
    //     L"db.cpp",
    //     0x2730,
    //     static_cast<std::int32_t>(command))
    // Log.Add(
    //     LOG_ERROR,
    //     L"[db] Droid appearance load not requested for character DB ID %u, account %u: command %u is invalid. Expected 0
    //     or 1 (request context %u).", characterId, accountId, wireCommand, requestContext)
    L2_LOG_ERROR(
        Log,
        u"[db] Droid appearance load not requested for character DB ID %u, account %u: command %u is invalid; expected 0 or "
        u"1 (request context %u).",
        characterId, accountId, wireCommand, requestContext
    );
}

void CDB::RequestUpdateWedding(User& user, int partnerDatabaseId, char updateType) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddc", 0xea, 0x19a, user.GetDBID(), partnerDatabaseId, static_cast<int>(updateType));
}

void CDB::RequestLoadAutoSoulShotUI(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x1ac, user->GetDBID(), user->GetServerId());
    }
}

void CDB::RequestSaveAutoSoulShotUI(User* user) {
    // Current L2Server.exe 0x0066745c.
    L2_GUARD_FUNCTION();
    if (g_L2ServerType == L2ServerType::WORLDSERVER || Singleton<WOlympiadServiceConfig>::inst().IsHost() ||
        user == nullptr) {
        return;
    }
    user->SendDB(
        "chddddddddd", 0xea, 0x1ad, static_cast<int>(user->GetDBID()),
        // user->m_AutoSoulShotUi[0].m_ItemClassID,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_ItemClassID,
        // user->m_AutoSoulShotUi[0].m_IsActiveState,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SOULSHOT].m_IsActiveState,
        // user->m_AutoSoulShotUi[1].m_ItemClassID,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_ItemClassID,
        // user->m_AutoSoulShotUi[1].m_IsActiveState,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SPIRITSHOT].m_IsActiveState,
        // user->m_AutoSoulShotUi[2].m_ItemClassID,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT].m_ItemClassID,
        // user->m_AutoSoulShotUi[2].m_IsActiveState,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SOULSHOT].m_IsActiveState,
        // user->m_AutoSoulShotUi[3].m_ItemClassID,
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT].m_ItemClassID,
        // user->m_AutoSoulShotUi[3].m_IsActiveState)
        user->m_AutoSoulShotUi[E_AUTOSOULSHOT_TYPE_SUMMON_SPIRITSHOT].m_IsActiveState
    );
}

void CDB::RequestInitUserPledgeBonus(
    User* user,
    CPledge* pledge,
    int bonusIndex,
    std::int32_t lastSaveTime,
    int currentBonusPoint,
    int maxBonusPoint,
    int todayRewardLevel
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x28a3)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Clan-bonus initialization not requested for bonus index %d: character %ls, clan %ls.",
        //     bonusIndex,
        //     user != nullptr ? L"available" : L"unavailable",
        //     pledge != nullptr ? L"available" : L"unavailable")
        L2_LOG_ERROR(
            Log, u"[db] Clan-bonus initialization not requested for bonus index %d: character %ls, clan %ls.", bonusIndex,
            user != nullptr ? L"available" : L"unavailable", pledge != nullptr ? L"available" : L"unavailable"
        );
        return;
    }
    user->SendDB(
        "chddddddd", 0xea, 0x1ae, user->GetDBID(), pledge->GetDbId(), bonusIndex, lastSaveTime, currentBonusPoint,
        maxBonusPoint, todayRewardLevel
    );
}

void CDB::RequestUpdateUserPledgeBonus(
    User* user, CPledge* pledge, int bonusIndex, std::int32_t lastSaveTime, int addedPoint, int maxBonusPoint
) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x28b4)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Clan-bonus update not requested for bonus index %d: character %ls, clan %ls.",
        //     bonusIndex,
        //     user != nullptr ? L"available" : L"unavailable",
        //     pledge != nullptr ? L"available" : L"unavailable")
        L2_LOG_ERROR(
            Log, u"[db] Clan-bonus update not requested for bonus index %d: character %ls, clan %ls.", bonusIndex,
            user != nullptr ? L"available" : L"unavailable", pledge != nullptr ? L"available" : L"unavailable"
        );
        return;
    }
    user->SendDB(
        "chdddddd", 0xea, 0x1af, user->GetDBID(), pledge->GetDbId(), bonusIndex, lastSaveTime, addedPoint, maxBonusPoint
    );
}

void CDB::RequestLoadUserPledgeBonus(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x28c5)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Clan-bonus load not requested: character is unavailable.")
        L2_LOG_ERROR(Log, u"[db] Clan-bonus load not requested: character is unavailable.");
        return;
    }
    user->SendDB("chd", 0xea, 0x1b0, user->GetDBID());
}

void CDB::RequestUpdateUserPledgeBonusReward(User* user, int rewardType, std::int32_t updateTime) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x28d6)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Clan-bonus reward update not requested: character is unavailable (reward type %d, update time %d).",
        //     rewardType,
        //     updateTime)
        L2_LOG_ERROR(
            Log, u"[db] Clan-bonus reward update not requested: character is unavailable (reward type %d, update time %d).",
            rewardType, updateTime
        );
        return;
    }
    user->SendDB("chddd", 0xea, 0x1b1, user->GetDBID(), rewardType, updateTime);
}

void CDB::RequestSetItemReuseDelay(User* user, int reuseKey, int reuseEndTime) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x291e)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Item cooldown update not requested: character is unavailable (reuse key %d, end time %d).",
        //     reuseKey,
        //     reuseEndTime)
        L2_LOG_ERROR(
            Log, u"[db] Item cooldown update not requested: character is unavailable (reuse key %d, end time %d).", reuseKey,
            reuseEndTime
        );
        return;
    }
    user->SendDB("chddd", 0xea, 0x1c7, user->GetDBID(), reuseKey, reuseEndTime);
}

void CDB::RequestUpdateCardUpdownGameRecord(User& user, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdb", 0xea, 0x1b6, user.GetDBID(), packet.GetLength(), packet.GetPacket());
}

void CDB::RequestLoadFactionPoint(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x275e)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Faction-point load not requested: character is unavailable.")
        L2_LOG_ERROR(Log, u"[db] Faction-point load not requested: character is unavailable.");
        return;
    }
    protocol::RQ_LOAD_FACTION_POINT request;
    request.nUserServerId = static_cast<int>(user->GetServerId());
    request.nUserDBID = static_cast<int>(user->GetDBID());
    user->SendDB(static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x19e), request);
}

void CDB::RequestUpdateFactionPoint(
    User* user, int factionType, std::int64_t factionPoint, protocol::Faction::UpdateCommand command, protocol::Faction::Reason reason
) {
    // Current L2Server 0x00666e28.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] User is NULL",
        //     kDatabaseSourceFile,
        //     0x2741)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Faction-point update not requested: character is unavailable (faction %d, amount %lld, command %d,
        //     reason %d).", factionType, static_cast<long long>(factionPoint), static_cast<int>(command),
        //     static_cast<int>(reason))
        L2_LOG_ERROR(
            Log,
            u"[db] Faction-point update not requested: character is unavailable (faction %d, amount %lld, command %d, "
            u"reason %d).",
            factionType, static_cast<long long>(factionPoint), static_cast<int>(command), static_cast<int>(reason)
        );
        return;
    }

    const int factionIndex = factionType - 1;
    protocol::RQ_UPDATE_FACTION_POINT request;
    request.cCommand = static_cast<std::int8_t>(command);
    request.nReason = static_cast<std::int16_t>(reason);
    request.nUserServerId = static_cast<int>(user->GetServerId());
    request.nUserDBID = static_cast<int>(user->GetDBID());
    request.nMaxPoint = FactionByLevel[factionIndex][FactionMaxLevel[factionIndex] - 1] - 1;
    request.nFaction.nType = factionType;
    request.nFaction.nPoint = factionPoint;
    user->SendDB(static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x19f), request);
}

void CDB::SendDeleteLoginAnnounce(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cd", 0x35, value);
}

void CDB::ShowIntervalAnnounce(CUserSocket* socket, int repetitive) {
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ShowIntervalAnnounce() failed. User is NULL",
        //     kDatabaseSourceFile,
        //     0xa06)
        // Log.Add(
        //     LOG_WARN,
        //     L"[announcements] Interval-announcement list not requested: the socket has no bound character (repetitive flag
        //     %d).", repetitive)
        L2_LOG_WARN(
            Log,
            u"[announcements] Interval-announcement list not requested: the socket has no bound character (repetitive flag "
            u"%d).",
            repetitive
        );
        return;
    }
    user->SendDB("cdd", 0xa3, user->GetServerId(), repetitive);
}

void CDB::SetIntervalAnnounce(CUserSocket* socket, int interval, int announceId, const char16_t* message) {
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetIntervalAnnounce() failed. User is NULL",
        //     kDatabaseSourceFile,
        //     0xa22)
        // Log.Add(
        //     LOG_WARN,
        //     L"[announcements] Interval-announcement update not requested: the socket has no bound character (announcement
        //     %d, interval %d).", announceId, interval)
        L2_LOG_WARN(
            Log,
            u"[announcements] Interval-announcement update not requested: the socket has no bound character (announcement "
            u"%d, interval %d).",
            announceId, interval
        );
        return;
    }
    user->SendDB("cdddS", 0xa4, user->GetServerId(), interval, announceId, message);
}

void CDB::DelIntervalAnnounce(CUserSocket* socket, int interval, int announceId) {
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] DelIntervalAnnounce() failed. User is NULL",
        //     kDatabaseSourceFile,
        //     0xa35)
        // Log.Add(
        //     LOG_WARN,
        //     L"[announcements] Interval-announcement deletion not requested: the socket has no bound character
        //     (announcement %d, interval %d).", announceId, interval)
        L2_LOG_WARN(
            Log,
            u"[announcements] Interval-announcement deletion not requested: the socket has no bound character (announcement "
            u"%d, interval %d).",
            announceId, interval
        );
        return;
    }
    user->SendDB("cddd", 0xa5, user->GetServerId(), interval, announceId);
}

void CDB::LoadIntervalAnnounce() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0xa9);
}

void CDB::LoadSelectedCharacter(
    const int worldId,
    const int characterId,
    CUserSocket* const socket,
    const AddtionalLocationType additionalLocationType,
    const int value1,
    const int value2,
    const int value3,
    const bool force
) {
    // Current L2Server 0x00664aec.
    L2_GUARD_FUNCTION();
    SendDB(
        worldId, "cddhdddd", 0x37, characterId, socket->GetServerId(), static_cast<short>(additionalLocationType), force,
        value1, value2, value3
    );
}

void CDB::RequestCreateAllianceByNpc(CNPC* const npc, User* const user, CPledge* const pledge, const char16_t* const name) {
    // Current L2Server 0x00649c14.
    L2_GUARD_FUNCTION();
    if (npc != nullptr && user != nullptr && user->m_data->m_nWorldId == g_nWorldId && pledge->GetWorldId() == g_nWorldId) {
        pledge->SendDB(
            "cdddSd", 0x6f, user->GetServerId(), npc->m_data->m_nId, pledge->Id(),
            name, pledge->GetDbId()
        );
    }
}

void CDB::RequestLoadAllAlliance() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0x85);
}

void CDB::RequestSetAllianceInfo(CAlliance& alliance, std::uint32_t selector, std::uint32_t value) {
    L2_GUARD_FUNCTION();
    const std::uint32_t allianceDatabaseId = alliance.m_data->m_nDbId;
    alliance.SendDB("cddd", 0x86, allianceDatabaseId, selector, value, std::int64_t{-2});
}

void CDB::RequestLoadAlliance(int worldId, int value) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chd", 0xea, 0xc8, value);
}

void CDB::RequestLoadDismissReservedPledge() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0x42);
}

void CDB::RequestLoadAllPledgeWar() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0x88);
}

void CDB::RequestDeletePledgeCrest(unsigned int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cd", 0x54, value);
}

void CDB::RequestGetDbRelatedCounts() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "c", 0x87);
}

void CDB::RequestSetUserPledgeInfo(
    CPledge* const pledge, const unsigned int userDatabaseId, const unsigned int selector, const unsigned int value
) {
    // Current L2Server 0x006480d0. The Pledge owner sends directly so the
    // caller's database id is kept even for an offline member.
    L2_GUARD_FUNCTION();
    if (pledge != nullptr) {
        pledge->SendDB("cddd", 0x46, userDatabaseId, selector, value);
    }
}

void CDB::RequestSetUserPledgeInfo(User* user, unsigned int selector, unsigned int value) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t dbId = user->GetDBID();
        user->SendDB("cddd", 0x46, dbId, selector, value);
    }
}

void CDB::SavePledgeWarPoint(CPledge& pledge, int value1, int value2) {
    L2_GUARD_FUNCTION();
    pledge.SendDB("chddd", 0xea, 0xfa, pledge.GetDbId(), value1, value2);
}

void CDB::SavePledgeWarPointPrev(CPledge& pledge, int value1, int value2) {
    L2_GUARD_FUNCTION();
    pledge.SendDB("chddd", 0xea, 0x142, pledge.GetDbId(), value1, value2);
}

void CDB::RequestUpdatePledgeWarKillCount(CPledge& pledge, int targetPledgeDatabaseId, int killCount) {
    L2_GUARD_FUNCTION();
    pledge.SendDB("chddd", 0xea, 0x143, targetPledgeDatabaseId, pledge.GetDbId(), killCount);
}

void CDB::ResetWarTargeted(CPledge& pledge, int value, std::int32_t resetTime) {
    L2_GUARD_FUNCTION();
    pledge.SendDB("chddd", 0xea, 0x14b, pledge.GetDbId(), value, resetTime);
}

void CDB::RequestViewSiegeList(User* user, unsigned int residenceId, const char16_t* pledgeName) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddS", 0x4d, user->GetServerId(), residenceId, pledgeName);
}

void CDB::RequestApproveBattle(User* user, unsigned int residenceId, const char16_t* pledgeName) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddS", 0x4e, user->GetServerId(), residenceId, pledgeName);
}

void CDB::RequestResetBotReport(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x9c, dbId);
}

void CDB::RequestSaveBotReport(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x99, dbId);
}

void CDB::RequestLoadBotReport(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chdd", 0xea, 0x9a, -1, dbId);
}

void CDB::RequestLoadBotReportByGM(User* user, User* target) {
    L2_GUARD_FUNCTION();
    const std::uint32_t userDbId = user->GetDBID();
    const std::uint32_t targetDbId = target->GetDBID();
    target->SendDB("chdd", 0xea, 0x9a, userDbId, targetDbId);
}

void CDB::RequestLoadBotReportTopTen(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x9b, dbId);
}

void CDB::RequestSetBotReport(User* user, int botReportPoint, int confirmTimes, int confirmSeconds, int delaySeconds) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chddddd", 0xea, 0x98, dbId, botReportPoint, confirmTimes, confirmSeconds, delaySeconds);
}

void CDB::RequestSetBotAdmin(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x9d, dbId);
}

void CDB::RequestDecayBotReport(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x9e, dbId);
}

void CDB::RequestLoadUserTimeToGiveKarmaPointToKiller(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("chd", 0xea, 0x116, user->GetDBID());
}

void CDB::RequestSaveUserTimeToGiveKarmaPointToKiller(User* user, int time) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chdd", 0xea, 0x117, dbId, time);
}

void CDB::RequestDeleteSkillList(User* user, int deleteCount, CPacketHelperEx<20480>* deletePacket) {
    // Current L2Server 0x0065cf68, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddbd", 0xea, 0x11a, user->GetDBID(), deleteCount, deletePacket->GetLength(), deletePacket->GetPacket(), 0
    );
}

void CDB::RequestDeleteAndSaveSkillList(
    User* user, int deleteCount, CPacketHelperEx<20480>* deletePacket, int saveCount, CPacketHelperEx<20480>* savePacket
) {
    // Current L2Server 0x0065d06c.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddbdb", 0xea, 0x11a, user->GetDBID(), deleteCount, deletePacket->GetLength(), deletePacket->GetPacket(),
        saveCount, savePacket->GetLength(), savePacket->GetPacket()
    );
}

void CDB::RequestLoadMinigameScores(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t dbId = user->GetDBID();
        user->SendDB("chd", 0xea, 0xdf, dbId);
    }
}

void CDB::RequestInsertMinigameScores(User* user, int score) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const char16_t* const name = user->GetName();
        user->SendDB("chSd", 0xea, 0xe0, name, score);
    }
}

void CDB::RequestGetLectureMark(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t accountId = user->GetAccountID();
        user->SendDB("chd", 0xea, 0xe1, accountId);
    }
}

void CDB::RequestSetLectureMark(User* user, char mark) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t accountId = user->GetAccountID();
        user->SendDB("chdc", 0xea, 0xe2, accountId, static_cast<int>(mark));
    }
}

void CDB::RequestLoadFriendList(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdd", 0x2c, user->GetDBID(), user->GetServerId());
}

void CDB::RequestBreakFriendship(User* user, unsigned int characterId) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddd", 0x2e, user->GetDBID(), user->GetServerId(), characterId);
}

void CDB::RequestBreakFriendshipBatch(User* user, std::vector<int>& characterIds) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const int characterId : characterIds) {
        packet.Assemble("d", characterId);
    }
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB(
        "chdddb", 0xea, 0x13, characterId, serverId, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket()
    );
}

void CDB::RequestDeleteBlockListBatch(User* user, std::vector<int>& results, std::vector<std::u16string>& names) const {
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> packet{};
    int packetLength = 0;
    for (std::size_t index = 0; index < results.size(); ++index) {
        packetLength += ::Assemble(
            packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "dS",
            runtime::CheckedVectorAt(results, index), runtime::CheckedVectorAt(names, index).c_str()
        );
    }
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t characterId = user->GetDBID();
    user->SendDB("chdddb", 0xea, 0x14, characterId, serverId, static_cast<int>(names.size()), packetLength, packet.data());
}

void CDB::RequestAddBlockList(User* user, const char16_t* characterName, bool flag) const {
    L2_GUARD_FUNCTION();
    user->SendDB("cddSd", 0x8f, user->GetDBID(), user->GetServerId(), characterName, flag);
}

void CDB::RequestDeleteBlockList(User* user, const char16_t* characterName) const {
    L2_GUARD_FUNCTION();
    user->SendDB("cddS", 0x90, user->GetDBID(), user->GetServerId(), characterName);
}

void CDB::RequestUpdateFriendMemo(User* user, int friendDatabaseId, const char16_t* memo) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddS", 0xea, 0xff, user->GetDBID(), friendDatabaseId, memo
    );
}

void CDB::RequestUpdateBlockMemo(User* user, int blockedDatabaseId, const char16_t* memo) {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddS", 0xea, 0xfe, user->GetDBID(), blockedDatabaseId, memo
    );
}

void CDB::RequestLoadFriendDetails(User* user, int characterId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t databaseId = user->GetDBID();
    user->SendDB("chddd", 0xea, 0x19c, databaseId, serverId, characterId);
}

void CDB::RequestLoadExDropCount(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x13f, dbId);
}

void CDB::SaveQuestInfo(User* user) {
    // Current L2Server 0x00663fd8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    if (user->m_bWaitForSavingQuest) {
        runtime::raw_atomic::ExchangeI32(&user->m_bNeedSaveQuest, 1);
        return;
    }

    L2_GUARD(user->m_QuestSaveLock);
    user->m_bWaitForSavingQuest = true;

    std::array<char, 0x5000> payload{};
    int payloadSize = 0;
    for (const MemoStruct& memo : user->m_data->m_Memo) {
        const int assembled = ::Assemble(
            payload.data() + payloadSize, static_cast<int>(payload.size()) - payloadSize, "dddddd", memo.nQuestID,
            memo.nJournal, memo.nState, memo.nState2, memo.nState3, memo.nState4
        );
        if (assembled > 0) {
            payloadSize += assembled;
        }
    }

    for (const MemoStruct& memo : user->m_data->m_NRMemo) {
        if (memo.nQuestID <= 0) {
            continue;
        }
        user->SendDB(
            "chddddddd", 0xea, 0x8b, user->GetDBID(), memo.nQuestID, memo.nJournal, memo.nState, memo.nState2, memo.nState3,
            memo.nState4
        );
    }

    std::array<char, 0x100> oneTimeFlags{};
    user->m_data->GetOneTimeFlagAll(oneTimeFlags.data(), static_cast<int>(oneTimeFlags.size()));
    const int assembled = ::Assemble(
        payload.data() + payloadSize, static_cast<int>(payload.size()) - payloadSize, "b",
        static_cast<int>(oneTimeFlags.size()), oneTimeFlags.data()
    );
    if (assembled > 0) {
        payloadSize += assembled;
    }

    user->SendDB("cdb", 0x17, user->GetDBID(), payloadSize, payload.data());
}

void CDB::SaveQuestNPCLog(User* user, int questId) const {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    std::array<char, 0x5000> payload{};
    int payloadSize = 0;
    int logCount = 0;
    for (int memoIndex = 0; memoIndex < 41; ++memoIndex) {
        const MemoStruct& memo = user->m_data->m_Memo[memoIndex];
        const bool selected = questId == 0 ? memo.nQuestID > 0 : memo.nQuestID == questId;
        if (!selected) {
            continue;
        }

        for (const UserNPCLog& log : memo.NPCLog) {
            if (log.nNPCID == 0) {
                continue;
            }
            const int assembled = ::Assemble(
                payload.data() + payloadSize, static_cast<int>(payload.size()) - payloadSize, "ddddd", memoIndex,
                memo.nQuestID, static_cast<int>(log.userLogType), log.nNPCID, log.nCount
            );
            if (assembled > 0) {
                payloadSize += assembled;
            }
            ++logCount;
        }
    }

    if (logCount > 0) {
        user->SendDB("chddb", 0xea, 0xc9, user->GetDBID(), logCount, payloadSize, payload.data());
    }
}

void CDB::RequestUpdateExDropCount(User* user, int countId, int count, L2ServerLong time) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chdddd", 0xea, 0x140, dbId, countId, count, time);
}

void CDB::RequestResetExDropCount(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chd", 0xea, 0x141, dbId);
}

void CDB::SendPunishmentHistory(User* user, int reason) {
    L2_GUARD_FUNCTION();
    const char16_t* const name = user->m_data->Name();
    const char16_t* const accountName = user->m_sAccountName.data();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("cddSS", 0xe6, dbId, reason, name, accountName);
}

void CDB::RequestSetPunish(User& user, int punishType, int save, int durationMinutes) {
    L2_GUARD_FUNCTION();
    user.SendDB("cdddd", 0x30, user.GetDBID(), punishType, save, durationMinutes);
}

void CDB::RequestGetPunish(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("cd", 0x31, user.GetDBID());
}

void CDB::RequestGetLoginAnnounce(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("cd", 0x33, user.GetDBID());
}

void CDB::RequestLogCharInfo(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("cd", 0x47, user.GetDBID());
}

void CDB::SaveDieDropExp(User& user, std::int64_t experience) const {
    L2_GUARD_FUNCTION();
    user.SendDB("cdQ", 0x64, user.GetDBID(), experience);
}

void CDB::AddHennaPeriod(User* user, std::uint32_t hennaId, int period) {
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chddddd", 0xea, 0x182, 3, dbId, serverId, hennaId, period);
}

void CDB::AddItemPeriod(User& user, unsigned int itemDbId, int period) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddd", 0xea, 0x92, 3, itemDbId, period);
}

void CDB::RequestLoadSharedReuseDelaysOfItems(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x57, user.GetDBID());
}

void CDB::RequestSaveSharedReuseDelayOfItem(User& user, int sharedReuseGroupId, int reuseEndTime) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddd", 0xea, 0x58, user.GetDBID(), sharedReuseGroupId, reuseEndTime);
}

void CDB::RequestLoadPCCafePoint(User* user, int pointType) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const int date = GetYYYYMMDDFromTime(now);
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chddd", 0xea, 0x22, dbId, pointType, date);
}

void CDB::RequestSavePCCafeCount(User* user, int count, int pointType) {
    // Current L2Server.exe 0x00652f7c.
    L2_GUARD_FUNCTION();
    const int date = GetYYYYMMDDFromTime(runtime::crt::Time32Now());
    user->SendDB("chdddd", 0xea, 0x23, user->GetDBID(), count, pointType, date);
}

void CDB::RequestSaveDailyPCCafePoint(User* user, int point, int checkLevel) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x113, user->GetDBID(), point, checkLevel);
}

void CDB::RequestSaveVoteSystem(User* user) {
    // Current L2Server 0x0064e618.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddddddb", 0xea, 0xce, user->GetDBID(), user->m_nBonusTimeLimit, user->m_nPlayTime, user->m_nVoteCount,
        user->m_nBonusCount, user->m_nPlayTimeCount, 0x10, &user->m_tmLastInitTime
    );
}

void CDB::RequestMakeVariationItem(
    CItem* targetItem,
    CItem* mineralItem,
    CItem* supportItem,
    const std::int64_t supportItemAmount,
    User* user,
    ItemOptionKey optionKey
) {
    // Current L2Server 0x00645f50, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (supportItem == nullptr) {
        user->SendDB(
            "chddddddddQdd", 0xea, 0x59, user->GetDBID(), user->GetServerId(), targetItem->GetDBID(), targetItem->m_nId,
            mineralItem->GetDBID(), mineralItem->m_nId, 0, 0, supportItemAmount, optionKey.variationOption1,
            optionKey.variationOption2
        );
        return;
    }

    user->SendDB(
        "chddddddddQdd", 0xea, 0x59, user->GetDBID(), user->GetServerId(), targetItem->GetDBID(), targetItem->m_nId,
        mineralItem->GetDBID(), mineralItem->m_nId, supportItem->GetDBID(), supportItem->m_nId, supportItemAmount,
        optionKey.variationOption1, optionKey.variationOption2
    );
}

void CDB::RequestCancelVariationItem(CItem* targetItem, User* user, const std::int64_t adenaAmount) {
    // Current L2Server 0x00646198, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddddQ", 0xea, 0x5a, user->GetDBID(), user->GetServerId(), targetItem->GetDBID(), targetItem->m_nId, adenaAmount
    );
}

void CDB::RequestChangeAttribute(CItem* scrollItem, CItem* targetItem, User* user, AttributeTypeEnum attributeType) {
    // Current L2Server 0x006462a4.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chddddddddd", 0xea, 0x119, user->GetDBID(), user->GetServerId(), scrollItem->GetDBID(), scrollItem->m_data->m_nId,
        scrollItem->m_data->m_ItemConsumeType, targetItem->GetDBID(), targetItem->m_data->m_nId,
        targetItem->m_data->m_ItemConsumeType, attributeType
    );
}

void CDB::RequestShapeShiftItem(
    User* user,
    CItem* extractionItem,
    CItem* targetItem,
    CItem* shapeShiftItem,
    bool extract,
    std::int64_t amount,
    int period
) {
    // Current L2Server 0x0065d400.
    L2_GUARD_FUNCTION();
    if (user == nullptr || extractionItem == nullptr || targetItem == nullptr || shapeShiftItem == nullptr) {
        return;
    }
    user->SendDB(
        "chddddddddddQd", 0xea, 0x128, user->GetDBID(), user->GetServerId(), extractionItem->GetDBID(),
        extractionItem->m_data->m_nId, extractionItem->m_data->m_nClassID, targetItem->GetDBID(), targetItem->m_data->m_nId,
        shapeShiftItem->GetDBID(), shapeShiftItem->m_data->m_nId, static_cast<int>(extract), amount, period
    );
}

void CDB::RequestShapeShiftItem(
    User* user, CItem* extractionItem, CItem* targetItem, int shapeShiftClassId, std::int64_t amount, int period
) {
    // Current L2Server 0x0065d5f4.
    L2_GUARD_FUNCTION();
    if (user == nullptr || extractionItem == nullptr || targetItem == nullptr) {
        return;
    }
    user->SendDB(
        "chddddddddQd", 0xea, 0x129, user->GetDBID(), user->GetServerId(), extractionItem->GetDBID(),
        extractionItem->m_data->m_nId, extractionItem->m_data->m_nClassID, targetItem->GetDBID(), targetItem->m_data->m_nId,
        shapeShiftClassId, amount, period
    );
}

void CDB::RequestRestoreShapeShiftItem(User* user, CItem* extractionItem, CItem* targetItem) {
    // Current L2Server 0x0065d78c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || targetItem == nullptr) {
        return;
    }
    user->SendDB(
        "chddddddd", 0xea, 0x12a, user->GetDBID(), user->GetServerId(), extractionItem->GetDBID(),
        extractionItem->m_data->m_nId, extractionItem->m_data->m_ItemConsumeType, targetItem->GetDBID(),
        targetItem->m_data->m_nId
    );
}

void CDB::RequestExpireShapeShiftItem(User* user, CItem* item) {
    // Current L2Server 0x0065d8f0, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr || item == nullptr) {
        return;
    }
    user->SendDB("chdddd", 0xea, 0x12b, user->GetDBID(), user->GetServerId(), item->GetDBID(), item->m_data->m_nId);
}

void CDB::RequestLockedItem(CItem* scrollItem, CItem* targetItem, User* user) {
    // Current L2Server 0x00646414.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chdcdddd", 0xea, 0x1c6, user->GetDBID(), 0, scrollItem->GetDBID(), scrollItem->m_data->m_nId, targetItem->GetDBID(),
        targetItem->m_data->m_nId
    );
}

void CDB::RequestUnLockedItem(CItem* scrollItem, CItem* targetItem, User* user) {
    // Current L2Server 0x00646538.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "chdcdddd", 0xea, 0x1c6, user->GetDBID(), 1, scrollItem->GetDBID(), scrollItem->m_data->m_nId, targetItem->GetDBID(),
        targetItem->m_data->m_nId
    );
}

void CDB::RequestEnchantItem(
    CItem* scrollItem, CItem* probabilityItem, CItem* targetItem, int enchantLevel, User* user, int probability, bool lucky
) {
    // Current L2Server 0x00646660.
    L2_GUARD_FUNCTION();

    const ItemConsumeType probabilityItemConsumeType =
        probabilityItem != nullptr ? probabilityItem->m_data->m_ItemConsumeType : ICT_NORMAL;
    const int probabilityItemDataDbId = probabilityItem != nullptr ? probabilityItem->m_data->m_nId : 0;
    const unsigned int probabilityItemDbId = probabilityItem != nullptr ? probabilityItem->GetDBID() : 0;

    user->SendDB(
        "cddddddddddddddc", 0x28, user->GetDBID(), scrollItem->GetDBID(), scrollItem->m_data->m_nId,
        scrollItem->m_data->m_ItemConsumeType, probabilityItemDbId, probabilityItemDataDbId, probabilityItemConsumeType,
        targetItem->GetDBID(), targetItem->m_data->m_nId, targetItem->m_data->m_ItemConsumeType, enchantLevel,
        user->GetServerId(), targetItem->m_data->m_Type, probability, lucky
    );
}

void CDB::RequestEnchantItemFail(
    CItem* scrollItem,
    CItem* probabilityItem,
    CItem* targetItem,
    User* user,
    int crystalClassId,
    int crystalCount,
    int probability
) {
    // Current L2Server 0x00646888.
    L2_GUARD_FUNCTION();

    const ItemConsumeType probabilityItemConsumeType =
        probabilityItem != nullptr ? probabilityItem->m_data->m_ItemConsumeType : ICT_NORMAL;
    const int probabilityItemDataDbId = probabilityItem != nullptr ? probabilityItem->m_data->m_nId : 0;
    const unsigned int probabilityItemDbId = probabilityItem != nullptr ? probabilityItem->GetDBID() : 0;

    user->SendDB(
        "cddddddddddddddd", 0x29, user->GetDBID(), scrollItem->GetDBID(), scrollItem->m_data->m_nId,
        scrollItem->m_data->m_ItemConsumeType, probabilityItemDbId, probabilityItemDataDbId, probabilityItemConsumeType,
        targetItem->GetDBID(), targetItem->m_data->m_nId, targetItem->m_data->m_ItemConsumeType, user->GetServerId(),
        targetItem->m_data->m_Type, probability, crystalClassId, crystalCount
    );
}

void CDB::RequestSaveAgathionSummonTime(User* user, int agathionId, L2ServerLong summonTime) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x10b, user->GetDBID(), agathionId, summonTime);
}

void CDB::RequestSaveAgathionDespawnTime(User* user, int agathionId, L2ServerLong despawnTime) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x10c, user->GetDBID(), agathionId, despawnTime);
}

void CDB::RequestInitAgathionElapsedTime(User* user, int agathionId, L2ServerLong elapsedTime) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x10f, user->GetDBID(), agathionId, elapsedTime);
}

void CDB::RequestUpdateAgathionElapsedTime(User* user, int agathionId, L2ServerLong elapsedTime) {
    L2_GUARD_FUNCTION();
    user->SendDB("chddd", 0xea, 0x10e, user->GetDBID(), agathionId, elapsedTime);
}

void CDB::RequestChangeAP(CSPointer<User> user, const int abilityPoints) {
    // Current L2Server 0x00669818.
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_CHANGE_AP request;
    request.characterId = static_cast<int>(user->GetDBID());
    request.abilityPoints = abilityPoints;
    user->SendDB(static_cast<RQ_PROTOCOL>(0xea), RQ_EX_CHANGE_AP, request);
}

void CDB::RequestLoadUserPoint(User* user, int pointType) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chdd", 0xea, 0x86, dbId, pointType);
}

void CDB::RequestSaveUserPoint(User* user, int pointType, int point, int previousPoint) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chdddd", 0xea, 0x85, dbId, pointType, point, previousPoint);
}

void CDB::SendDeletedSkill(User& user, int skillId) {
    L2_GUARD_FUNCTION();
    user.SendDB("cdd", 0x38, user.GetDBID(), skillId);
}

void CDB::SendDeletedPledgeSkill(User& user, int skillId) {
    L2_GUARD_FUNCTION();
    user.SendDB("cdd", 0xe9, user.GetDBID(), skillId);
}

void CDB::RequestDestroyItem(CItem* item, std::int64_t amount, User* user, unsigned int petDatabaseId) {
    L2_GUARD_FUNCTION();
    char16_t shapeShiftLogInfo[256]{};
    if (item != nullptr) {
        item->MakeShapeShiftLogInfo(shapeShiftLogInfo);
    }

    CLogFormatter formatter;
    formatter.WriteLog1_1(
        0x232d, user, nullptr, shapeShiftLogInfo, nullptr, nullptr, amount, item->m_data->m_nClassID, item->m_data->m_nCount,
        petDatabaseId, user->m_inv.GetAdenaAmount(), user->m_CrystallizeLevel > 0, item->GetDBID()
    );

    user->SendDB(
        "cddddddQ", 0x27, user->GetDBID(), petDatabaseId, item->GetDBID(), item->m_data->m_nId,
        item->m_data->m_ItemConsumeType, user->GetServerId(), amount
    );
}

void CDB::RequestUpdateSystem(User* user, int packetLength, char* packet) {
    L2_GUARD_FUNCTION();
    user->SendDB("chb", 0xea, 0x1b4, packetLength, packet);
}

void CDB::RequestLoadSkill(User* user) const {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("cdd", 0x2b, user->GetDBID(), user->GetServerId());
    }
}

void CDB::RequestPartyTelBookMark(User* user, User* target, const char16_t* bookmarkName) {
    // Current L2Server 0x00646b88 publishes a Win64 UTF-16 wchar_t* operand.
    L2_GUARD_FUNCTION();
    if (user != nullptr && target != nullptr) {
        user->SendDB("cddS", 0x57, user->GetDBID(), target->GetServerId(), bookmarkName);
    }
}

void CDB::RequestMPCCTelBookMark(User* user, User* target, const char16_t* bookmarkName) {
    // Current L2Server 0x00646c80 publishes a Win64 UTF-16 wchar_t* operand.
    L2_GUARD_FUNCTION();
    if (user != nullptr && target != nullptr) {
        user->SendDB("chddS", 0xea, 0x136, user->GetDBID(), target->GetServerId(), bookmarkName);
    }
}

void CDB::RequestEstablishFriendship(User* user, User* target) {
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId == target->m_data->m_nWorldId) {
        user->SendDB("cdddd", 0x2d, user->GetDBID(), user->GetServerId(), target->GetDBID(), target->GetServerId());
    }
}

void CDB::RequestChangeItemData(User* user, int value, int packetLength, char* packet) {
    L2_GUARD_FUNCTION();
    user->SendDB("cddb", 0x2f, user->GetDBID(), value, packetLength, packet);
}

void CDB::SendSetBuilder(User& user, int builderLevel) {
    L2_GUARD_FUNCTION();
    user.SendDB("cSd", 0x36, user.m_data->Name(), builderLevel);
}

void CDB::ReloadCharacter(User& user, int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = user.GetSocket();
    user.SendDB("chddddd", 0xea, 0xc3, user.GetDBID(), socket->GetServerId(), value1, value2, value3);
}

void CDB::Ping(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t tick = runtime::GetTickCount();
    user->SendDB("cdddd", 0x6a, 0, user->GetDBID(), user->GetServerId(), tick);
}

void CDB::PingOneAdenaDrop(
    User* const sender,
    const std::uint32_t correlationId,
    CItem* const item,
    User* const owner,
    const FVector position,
    const SlotType slot,
    const int amount
) {
    // Current L2Server 0x006497d4, 405 bytes.
    L2_GUARD_FUNCTION();
    sender->SendDB(
        "cddddddddddd", 0x6b, 0, correlationId, runtime::crt::Time32Now(), static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), owner->GetServerId(), owner->GetDBID(), item->GetDBID(),
        (static_cast<int>(slot) + 1) % ST_MAX, amount
    );
}

void CDB::PingNetwork(User* user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t tick = runtime::GetTickCount();
    user->SendDB("cddd", 0x6c, 0, user->GetServerId(), tick);
}

void CDB::SaveInfectedSkills(User& user, int count, int packetLength, char* packet) const {
    L2_GUARD_FUNCTION();
    const std::uint32_t databaseId = user.GetDBID();
    if (count < 1) {
        user.SendDB("cdd", 0x6d, databaseId, 0);
    } else {
        user.SendDB("cddb", 0x6d, databaseId, count, packetLength, packet);
    }
}

void CDB::LoadInfectedSkills(User* user) const {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("cdd", 0x6e, user->GetDBID(), user->GetServerId());
    }
}

void CDB::RequestLoadMenteeList(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x122, user.GetDBID());
}

void CDB::RequestLoadMentor(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x123, user.GetDBID());
}

void CDB::RequestAddMentee(User& user, int menteeDatabaseId) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x121, user.GetDBID(), menteeDatabaseId);
}

void CDB::RequestUpdateMentorStatus(
    User& user, int mentorDatabaseId, int menteeDatabaseId, int type, std::int32_t updateTime
) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdddd", 0xea, 0x124, mentorDatabaseId, menteeDatabaseId, type, updateTime);
}

void CDB::RequestUpdateMenteePlaytime(User& user, int mentorDatabaseId, int menteeDatabaseId, std::uint32_t playtime) {
    L2_GUARD_FUNCTION();
    user.SendDB("chddd", 0xea, 0x125, mentorDatabaseId, menteeDatabaseId, playtime);
}

void CDB::RequestUpdateMentorPlaytime(User& user, int mentorDatabaseId, std::uint32_t playtime) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x126, mentorDatabaseId, playtime);
}

void CDB::RequestResetMentorPenalty(User& user, int mentorDatabaseId) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x127, mentorDatabaseId);
}

void CDB::RequestSetTutorial(User& user, unsigned int tutorialState) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x79, user.GetDBID(), tutorialState);
}

void CDB::RequestPackageLoadTargetList(User* user) const {
    L2_GUARD_FUNCTION();
    const std::uint32_t serverId = user->GetServerId();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("cdd", 0x8d, dbId, serverId);
}

void CDB::RequestWareHouseItemList(User& target, User& requester, const int type) const {
    // Current L2Server 0x00648f50, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    target.SendDB("cddd", 0x5c, target.GetDBID(), requester.GetServerId(), type);
}

void CDB::RequestPackageSendToTarget(User* user, int value1, int value2, int packetLength, char* packet, int value3) const {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddddb", 0x8e, user->GetDBID(), user->GetServerId(), value1, value3, value2, packetLength, packet);
}

void CDB::RequestLoadWareHouseItems(
    User* const user,
    const int warehouseId,
    CWareHouseWithdraw* const warehouse,
    const int npcClassId,
    const int memoryId,
    const WareHouseClass warehouseClass,
    const int itemListGetType
) const {
    // Current L2Server 0x00667cd4. A null warehouse publishes
    // context zero instead of registering a null pointer.
    L2_GUARD_FUNCTION();
    int context = 0;
    if (warehouse != nullptr) {
        context = Singleton<CCacheContextManager>::inst().RegisterContext("p", warehouse);
    }
    user->SendDB(
        "cdddddddd", 0x0c, user->GetDBID(), user->GetServerId(), warehouseId, context, npcClassId, memoryId,
        static_cast<int>(warehouseClass), itemListGetType
    );
}

void CDB::RequestWithdrawWareHouseItems(
    User* const user,
    const int warehouseId,
    const int npcClassId,
    const int itemCount,
    const int packetLength,
    char* const packet
) const {
    // Current L2Server 0x00643fc4.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cdddddb", 0x0d, user->GetDBID(), warehouseId, user->GetServerId(), npcClassId, itemCount, packetLength, packet
    );
}

void CDB::RequestDepositWareHouseItems(
    User* const user,
    const int warehouseId,
    const int npcClassId,
    const int itemCount,
    const int warehouseItemLimit,
    const int packetLength,
    char* const packet
) const {
    // Current L2Server 0x006440d0.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddddb", 0x10, user->GetDBID(), warehouseId, user->GetServerId(), npcClassId, itemCount, warehouseItemLimit,
        packetLength, packet
    );
}

void CDB::RequestLoadWarehouseEx(int value1, int value2, User* user, int value3, int value4) const {
    L2_GUARD_FUNCTION();
    user->SendDB("cdddddd", 0x91, value1, value2, user->GetDBID(), user->GetServerId(), value3, value4);
}

void CDB::RequestWithdrawItemEx(
    int value1, int value2, User* user, int value3, int value4, int value5, int packetLength, char* packet
) const {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cdddddddb", 0x92, value1, value2, user->GetDBID(), user->GetServerId(), value4, value5, value3, packetLength, packet
    );
}

void CDB::RequestDepositItemEx(
    int value1, int value2, User* user, int value3, int value4, int packetLength, char* packet
) const {
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddddb", 0x93, value1, value2, user->GetDBID(), user->GetServerId(), value4, value3, packetLength, packet
    );
}

void CDB::UseRecipeItem(User& user, int recipeId) {
    L2_GUARD_FUNCTION();
    user.SendDB("cddd", 0xc2, user.GetDBID(), recipeId, user.GetServerId());
}

void CDB::RecipeMakeItems(
    User* user, int recipeId, int success, int critical, int assembledCount, int packetLength, char* packet, bool eventLucky
) const {
    // Current L2Server 0x0064b188.
    L2_GUARD_FUNCTION();
    user->SendDB(
        "cddddcddb", 0x8b, user->GetDBID(), user->GetServerId(), recipeId, success, eventLucky, critical, assembledCount,
        packetLength, packet
    );
}

bool CDB::RecipeShopMakeItems(
    User* customer,
    User* maker,
    const int recipeId,
    const int success,
    const std::int64_t makingFee,
    const int assembledCount,
    const int packetLength,
    char* packet,
    const bool eventLucky
) const {
    // Current L2Server 0x0064da2c.
    L2_GUARD_FUNCTION();
    const bool sameWorld = customer->m_data->m_nWorldId == maker->m_data->m_nWorldId;
    if (sameWorld) {
        customer->SendDB(
            "cddddddcQdb", 0xc6, maker->GetDBID(), maker->GetServerId(), customer->GetDBID(), customer->GetServerId(),
            recipeId, success, eventLucky, makingFee, assembledCount, packetLength, packet
        );
    }
    return sameWorld;
}

void CDB::RequestUseItem(CItem* const item, User* const user, const std::int64_t count, const int unknown) const {
    // Current L2Server 0x006444dc, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const L2ServerLong manipulation = user->EnterItemManipulation(2);
    user->m_nItemManipulationID = item->m_data->m_nClassID;
    user->m_nItemManipulationCount = static_cast<std::int32_t>(count);
    if (manipulation < 1 || manipulation == 2) {
        user->SendDB(
            "cddQdddd", 0x13, static_cast<int>(item->GetDBID()), static_cast<int>(item->m_data->m_ItemConsumeType), count,
            static_cast<int>(user->GetDBID()), item->m_data->m_nId, user->GetServerId(), unknown
        );
        return;
    }

    if (manipulation == 1) {
        user->TradeCancel(false);
        user->SendSystemMessage(SystemMessageId::CANNOT_PICK_UP_OR_USE_ITEMS_WHILE_TRADING);
    }
}

void CDB::PetUseItem(CPet* const pet, CItem* const item) {
    // Current L2Server 0x0064ac34, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr || pet == nullptr || item->m_data == nullptr) {
        return;
    }
    pet->SendDB(
        "cddddd", 0x81, static_cast<int>(pet->Id()), static_cast<int>(pet->GetDBID()), static_cast<int>(item->GetDBID()),
        static_cast<int>(item->m_data->m_ItemConsumeType), static_cast<int>(pet->m_MasterUserDBID)
    );
}

void CDB::SaveRecipeBookChapter(User& user, int chapter) {
    L2_GUARD_FUNCTION();
    user.SendDB("cddd", 0xc3, user.GetDBID(), chapter, user.GetServerId());
}

void CDB::DeleteRecipeBookChapter(User& user, int chapter) {
    L2_GUARD_FUNCTION();
    user.SendDB("cddd", 0xc4, user.GetDBID(), chapter, user.GetServerId());
}

void CDB::LoadRecipeBookChapter(User* user) {
    L2_GUARD_FUNCTION();
    user->SendDB("cdd", 0xc5, user->GetDBID(), user->GetServerId());
}

void CDB::RequestAccountStamp(User* user, int stampType, bool flag, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (static_cast<unsigned int>(stampType) >= 101U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] unknown type",
        //     kDatabaseSourceFile,
        //     0x2561)
        // Log.Add(
        //     LOG_WARN,
        //     L"[db] Account-stamp request rejected for character DB ID %u: type %d is outside 0..100.",
        //     user->GetDBID(),
        //     stampType)
        L2_LOG_WARN(
            Log, u"[db] Account-stamp request rejected for character DB ID %u: type %d is outside 0..100.", user->GetDBID(),
            stampType
        );
        return;
    }
    user->SendDB(
        "chdccb", 0xea, 0x184, user->GetDBID(), static_cast<int>(static_cast<std::int8_t>(stampType)),
        static_cast<int>(flag), packet.GetLength(), packet.GetPacket()
    );
}

void CDB::RequestAccountStampBuilder(User* user, int value1, int value2) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user->GetDBID();
    user->SendDB("chddd", 0xea, 0x185, dbId, value1, value2);
}

void CDB::RequestDeleteAccountStampLogBuilder(User* user, int stampId) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdd", 0xea, 0x186, user->GetDBID(), stampId);
}

void CDB::RequestInitializeBossRecord(User& user) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user.GetDBID();
    user.SendDB("chd", 0xea, 0x47, dbId);
}

void CDB::SaveGet5YearGiftTime(User& user, ::_SYSTEMTIME* time) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user.GetDBID();
    user.SendDB("chdb", 0xea, 0xad, dbId, 0x10, time);
}

void CDB::SaveGetBirthdayGiftTime(User& user, ::_SYSTEMTIME* time) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user.GetDBID();
    user.SendDB("chdddd", 0xea, 0xaa, dbId, time->wYear, time->wMonth, time->wDay);
}

void CDB::GetLastBirthdayGiftDate(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0xab, user.GetDBID());
}

void CDB::RequestLoadNRMemo(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x8c, user.GetDBID());
}

void CDB::RequestDeleteNRMemo(User& user, int memoId) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x8d, user.GetDBID(), memoId);
}

void CDB::RequestLoadDailyQuest(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0xb2, user.GetDBID());
}

void CDB::RequestGetDailyCount(User* user, int countType) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdd", 0xea, 0x11e, user->GetDBID(), countType);
}

void CDB::RequestSetDailyCount(User* user, int countType) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdd", 0xea, 0x11f, user->GetDBID(), countType);
}

void CDB::RequestClearDailyCount(User* user, int countType) {
    L2_GUARD_FUNCTION();
    user->SendDB("chdd", 0xea, 0x120, user->GetDBID(), countType);
}

void CDB::RequestAcademyGraduateList(User& user, unsigned int pledgeDatabaseId) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x53, user.GetServerId(), pledgeDatabaseId);
}

void CDB::RequestLoadUserMonsterBook(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x1b3, user.GetDBID());
}

void CDB::SetDailyQuest(User& user, int questId, std::int32_t completeTime) {
    L2_GUARD_FUNCTION();
    const std::uint32_t dbId = user.GetDBID();
    user.SendDB("chddd", 0xea, 0xb1, dbId, questId, completeTime);
}

void CDB::IncreaseUserDuelCount(int value, int worldId) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cd", 0x8a, value);
}

void CDB::RequestCreateMonRaceMon(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddddd", 0xbc, 1, value1, value2, value3, value4);
}

void CDB::RequestCreateMonRace(
    int value1, int value2, int value3, int value4, int value5, int value6, int value7, int value8, int value9
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdddddddddd", 0xbc, 2, value1, value2, value3, value4, value5, value6, value7, value8, value9);
}

void CDB::RequestGetMonRaceBet(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdd", 0xbc, 4, value);
}

void CDB::RequestEndMonRace(
    int value1,
    double value2,
    double value3,
    double value4,
    double value5,
    double value6,
    double value7,
    double value8,
    double value9,
    double value10,
    double value11,
    int value12,
    int value13,
    double value14,
    double value15
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "cddffffffffffddff", 0xbc, 5, value1, value2, value3, value4, value5, value6, value7, value8, value9,
        value10, value11, value12, value13, value14, value15
    );
}

void CDB::RequestUpdateMonRaceMon(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddddd", 0xbc, 6, value1, value2, value3, value4);
}

void CDB::RequestGetMonRaceTicket(User* user, int value) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_data->m_nWorldId == g_nWorldId) {
        user->SendDB("cdddd", 0xbc, 7, user->GetDBID(), user->GetServerId(), value);
    }
}

void CDB::RequestGetMonRaceTicketInfo(User* user, int value) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_data->m_nWorldId == g_nWorldId) {
        user->SendDB("cdddd", 0xbc, 8, user->GetDBID(), user->GetServerId(), value);
    }
}

void CDB::RequestDelMonRaceTicket(User* user, int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_data->m_nWorldId == g_nWorldId) {
        user->SendDB("cdddddd", 0xbc, 9, user->GetDBID(), user->GetServerId(), value1, value2, value3);
    }
}

void CDB::RequestGetMonRaceResult(User* user, int value) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && user->m_data->m_nWorldId == g_nWorldId) {
        user->SendDB("cdddd", 0xbc, 10, user->GetDBID(), user->GetServerId(), value);
    }
}

void CDB::RequestUpdateMercenary(
    int mercenaryKey, FVector position, FAngle direction, double collisionRadius, double collisionHeight
) const {
    // Current L2Server 0x00663ec4.
    L2_GUARD_FUNCTION();
    const int yaw = static_cast<std::uint16_t>(direction.Yaw);
    SendDB(
        g_nWorldId, "cddddddd", 0x9f, mercenaryKey, static_cast<int>(position.x), static_cast<int>(position.y),
        static_cast<int>(position.z), yaw, static_cast<int>(collisionRadius), static_cast<int>(collisionHeight)
    );
}

void CDB::RequestSavePledgeContribution(int residenceId, bool siegeAgit, bool clear, std::map<int, double>* contributions) {
    // Current L2Server 0x0066b7d8.
    L2_GUARD_FUNCTION();
    char packet[0x2008]{};
    int packetSize = 0;
    int count = 0;
    if (contributions != nullptr) {
        for (const auto& contribution : *contributions) {
            packetSize += ::Assemble(
                packet + packetSize, 0x2004 - packetSize, "dd", contribution.first, static_cast<int>(contribution.second)
            );
            ++count;
        }
    }

    if (count != 0) {
        SendDB(g_nWorldId, "cddddb", 0x60, residenceId, siegeAgit ? 1 : 0, clear ? 1 : 0, count, packetSize, packet);
    } else if (clear) {
        SendDB(g_nWorldId, "cdddd", 0x60, residenceId, siegeAgit ? 1 : 0, 1, 0);
    }
}

void CDB::RequestMoveToNextBossRecordRound() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x2e);
}

void CDB::RequestPCCafeCouponUsed(
    const int value1, unsigned int value2, const int value3, const int value4, const int value5, const int value6
) {
    L2_GUARD_FUNCTION();
    SendDB(value1, "chdddddd", 0xea, 0x4e, value2, value1, value3, value4, value5, value6);
}

void CDB::RequestLoadTransferredPledgeMaster() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x50);
}

void CDB::RequestPledgeMasterTransferByNpc(
    CNPC* const npc, User* const user, CPledge* const pledge, const char16_t* const masterName
) {
    // Current L2Server 0x006439d0.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr || pledge == nullptr) {
        return;
    }
    if (user->m_data->m_nWorldId != g_nWorldId || user->m_data->m_nWorldId != pledge->GetWorldId()) {
        return;
    }
    user->SendDB(
        "chdddddS", 0xea, 0x4a, npc->m_data->m_nClassID, user->GetServerId(), user->GetDBID(), pledge->Id(),
        pledge->GetDbId(), masterName
    );
}

void CDB::RequestPledgeMasterTransferCancelByNpc(CNPC* const npc, User* const user, CPledge* const pledge) {
    // Current L2Server 0x00643b2c.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr || pledge == nullptr) {
        return;
    }
    if (user->m_data->m_nWorldId != g_nWorldId || user->m_data->m_nWorldId != pledge->GetWorldId()) {
        return;
    }
    user->SendDB(
        "chddddd", 0xea, 0x4b, npc->m_data->m_nClassID, user->GetServerId(), user->GetDBID(), pledge->Id(), pledge->GetDbId()
    );
}

void CDB::RequestCreatePledgeByNpc(CNPC* const npc, User* const user, const char16_t* const pledgeName) {
    // Current L2Server 0x00643770.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    const int npcId = npc != nullptr ? npc->m_data->m_nId : 0;
    // Pass the UTF-16 name directly to the packet assembler.
    user->SendDB("cddSd", 0x3f, user->GetServerId(), npcId, pledgeName, user->GetDBID());
}

void CDB::RequestCreateAcademyByNpc(
    const int npcId, User* const user, CPledge* const pledge, const char16_t* const academyName
) {
    // Current L2Server 0x0064388c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        return;
    }
    if (user->m_data->m_nWorldId != g_nWorldId || user->m_data->m_nWorldId != pledge->GetWorldId()) {
        return;
    }

    // The current Win64 image's direct const char16_t* operand is already UTF-16.
    // Preserve those exact bytes on portable hosts.
    user->SendDB("chddSdd", 0xea, 0x2b, user->GetServerId(), npcId, academyName, user->GetDBID(), pledge->GetDbId());
}

void CDB::RequestQueryCastleSiegeForDismiss(User* const user, CPledge* const pledge, const unsigned int npcClassId) {
    // Current L2Server 0x00648938.
    L2_GUARD_FUNCTION();
    pledge->SendDB("cdddd", 0x53, user->GetServerId(), pledge->Id(), pledge->GetDbId(), npcClassId);
}

void CDB::UpdateItemDuration(unsigned int value1, int worldId, unsigned int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chddd", 0xea, 0x5b, value1, value3, value4);
}

void CDB::UpdateItemDuration(User& user, const unsigned int itemDatabaseId, const int duration) {
    // Current L2Server 0x00654db0.
    L2_GUARD_FUNCTION();
    user.SendDB("chddd", 0xea, 0x5b, user.GetDBID(), itemDatabaseId, duration);
}

void CDB::RequestLoadFortressSiege(CFortress* const fortress) {
    // Current L2Server 0x00655244.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB("chd", 0xea, 0x64, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0);
    }
}

void CDB::RequestLoadFortressSiegeRegistry(CFortress* const fortress) {
    // Current L2Server 0x00655310.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB("chd", 0xea, 0x65, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0);
    }
}

void CDB::RequestPayFortressRent(
    CFortress* const fortress, const int fortressOwnerId, const int castleOwnerId, const std::int64_t rent
) {
    // Current L2Server 0x006554c0.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB(
            "chdddQ", 0xea, 0x67, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, fortressOwnerId,
            castleOwnerId, rent
        );
    }
}

void CDB::RequestSaveFortressState(
    CFortress* const fortress, const int state, const int stateChangedTime, const int stateElapsedTime
) {
    // Current L2Server 0x006555b0.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB(
            "chdddd", 0xea, 0x68, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, state,
            stateChangedTime, stateElapsedTime
        );
    }
}

void CDB::RequestSaveFortressSiegeState(int value1, int value2, int value3, int value4, bool value5) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddd", 0xea, 0x69, value1, value2, value3, value4, value5);
}

void CDB::RequestOwnFortress(CFortress* const fortress, const int ownerId, const int lastOwnerChangeTime) {
    // Current L2Server 0x006553dc.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB(
            "chddd", 0xea, 0x66, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, ownerId,
            lastOwnerChangeTime
        );
    }
}

void CDB::RequestSaveFortressContractInfo(CFortress* const fortress, const int contractStatus, const int parentCastleId) {
    // Current L2Server 0x006557a4.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB(
            "chddd", 0xea, 0x6a, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, contractStatus,
            parentCastleId
        );
    }
}

void CDB::RequestSaveFortressRewardInfo(
    CFortress* const fortress, const int lastRewardTime, const int rewardCycle, const int treasureLevel
) {
    // Current L2Server 0x00655888.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr) {
        fortress->SendDB(
            "chdddd", 0xea, 0x6b, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, lastRewardTime,
            rewardCycle, treasureLevel
        );
    }
}

void CDB::RequestSaveFortressSiegeRegistry(
    CFortress* const fortress, const int pledgeCount, const int packetLength, char* const packet, const bool clear
) {
    // Current L2Server 0x00655974. The current guard accepts
    // a null payload only for the explicit clear request.
    L2_GUARD_FUNCTION();
    if (fortress != nullptr && (clear || packet != nullptr)) {
        fortress->SendDB(
            "chdddb", 0xea, 0x6c, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, clear, pledgeCount,
            packetLength, packet
        );
    }
}

void CDB::RequestSaveFortressSiegeRegisterPledge(CFortress* const fortress, const int pledgeId) {
    // Current L2Server 0x00655a80.
    L2_GUARD_FUNCTION();
    fortress->SendDB("chddd", 0xea, 0x6d, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, pledgeId, 1);
}

void CDB::RequestSaveFortressSiegeUnregisterPledge(CFortress* const fortress, const int pledgeId) {
    // Current L2Server 0x00655b54.
    L2_GUARD_FUNCTION();
    fortress->SendDB("chddd", 0xea, 0x6d, fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0, pledgeId, 0);
}

void CDB::RequestDeletePremiumItem(std::int64_t value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chQ", 0xea, 0x95, value);
}

void CDB::RequestLoadPremiumItemForBatch() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0xa0);
}

void CDB::SetRimPoint(User& user, const int pointType, const int updateTime, const int resetTime) {
    // Current L2Server 0x0065845c, original db.cpp owner.
    L2_GUARD_FUNCTION();
    user.SendDB("chdddd", 0xea, 0x96, static_cast<int>(user.GetDBID()), pointType, updateTime, resetTime);
}

void CDB::GetRimPointTop10(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x97, value);
}

void CDB::RequestSaveDominionRegistry(CDominion&, int, int, int) { L2_GUARD_FUNCTION(); }

void CDB::RequestSaveDominion(CDominion& dominion, const int dominionStatus, const int residenceState) {
    L2_GUARD_FUNCTION();

    const int residenceId = dominion.m_data != nullptr ? dominion.m_data->m_nResidenceID : 0;
    dominion.SendDB("chddd", 0xea, 0xa2, residenceId, dominionStatus, residenceState);
}

void CDB::RequestLoadDominionRegistry(CDominion&) { L2_GUARD_FUNCTION(); }

void CDB::RequestSaveOwnthing(int value1, int value2, int value3, int value4, int value5) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddd", 0xea, 0xa5, value1, value2, value3, value4, value5);
}

void CDB::RequestSaveDominionSiege(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddd", 0xea, 0xa7, value1, value2, value3, value4);
}

void CDB::ReqOwnAgit(CWorldAgit* const agit, const int ownerId) {
    // Current L2Server 0x00644b9c.
    L2_GUARD_FUNCTION();
    if (agit == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No Agit Detected for pledge %d",
        //     ownerId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Clan-hall ownership update not requested for clan %d: clan hall is unavailable.",
        //     ownerId)
        L2_LOG_ERROR(Log, u"[db] Clan-hall ownership update not requested for clan %d: clan hall is unavailable.", ownerId);
        return;
    }

    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    agit->SendDB("cdd", 0x18, residenceId, ownerId);
}

void CDB::RequestSetNextAgitBattleTime(CWorldAgit* const agit) {
    // Current L2Server 0x00648d74 has no null guard and
    // re-reads the data pointer before logging.
    L2_GUARD_FUNCTION();
    CResidenceData* data = agit->m_data;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    agit->SendDB("cddd", 0x62, residenceId, 4, data->m_NextSiege);

    data = agit->m_data;
    // Log.Add(
    //     LOG_NORMAL,
    //     L"set agit[%d] next battle[%d]",
    //     data != nullptr ? data->m_nResidenceID : 0,
    //     data->m_NextSiege)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[residence] Schedule update requested for clan hall %d. Latest local schedule: hall %d, next siege timestamp
    //     %d.", residenceId, data != nullptr ? data->m_nResidenceID : 0, data->m_NextSiege)
    L2_LOG_INFO(
        Log,
        u"[residence] Schedule update requested for clan hall %d; latest local schedule: hall %d, next siege timestamp %d.",
        residenceId, data != nullptr ? data->m_nResidenceID : 0, data->m_NextSiege
    );
}

void CDB::RequestLoadAllControlTower(CResidence* const residence) {
    // Current L2Server 0x00666178 has no null guards.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < residence->m_ControlTowerList.size(); ++index) {
        CControlTower* const tower = residence->m_ControlTowerList[index];
        residence->SendDB(
            "cSdddd", 0x9a, tower->m_sControlTowerName, residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0,
            tower->m_nControlLevel, runtime::Win64CvttDoubleToInt32(tower->m_data->m_dMaxHP),
            static_cast<int>(tower->m_eStatus)
        );
    }
}

void CDB::RequestLoadDoor(CDoor* const door) {
    // Current L2Server 0x00648cac has no null guard.
    L2_GUARD_FUNCTION();
    door->SendDB("cSd", 0x59, door->m_sDoorName, static_cast<int>(door->m_dBaseHp));
}

void CDB::RequestSaveDoorStatus(CDoor* const door) {
    // Current L2Server 0x00648e74 has no null guard.
    L2_GUARD_FUNCTION();
    door->SendDB(
        "cSdd", 0x5b, door->m_sDoorName, runtime::Win64CvttDoubleToInt32(door->GetHP()),
        runtime::Win64CvttDoubleToInt32(door->m_data->m_dMaxHP)
    );
}

void CDB::RequestInstallResidenceDeco(CResidence& residence, const int decoId, const int productId, const int duration) {
    // Current L2Server 0x0064912c dereferences m_data.
    L2_GUARD_FUNCTION();
    residence.SendDB("cdddd", 0x5f, residence.m_data->m_nResidenceID, decoId, productId, duration);
}

void CDB::RequestResetContribution(CResidence& residence) {
    // Current L2Server 0x006492bc.
    L2_GUARD_FUNCTION();
    residence.SendDB("cd", 0x65, residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0);
}

void CDB::RequestLoadTeamBattleAgit(CWorldAgit& agit) {
    // Current L2Server 0x0064cdc8.
    L2_GUARD_FUNCTION();
    agit.SendDB("cd", 0xb4, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0);
}

void CDB::RequestSetTeamBattleNpcType(CTeamBattleAgit& agit, const unsigned int pledgeDatabaseId, const int npcType) {
    // Current L2Server 0x0064ce88.
    L2_GUARD_FUNCTION();
    agit.SendDB("cddd", 0xc8, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0, pledgeDatabaseId, npcType);
}

void CDB::RequestLoadMiniGameAgit(CWorldAgit& agit) {
    // Current L2Server 0x0064cf60.
    L2_GUARD_FUNCTION();
    agit.SendDB("chd", 0xea, 0x2f, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0);
}

void CDB::RequestSetMiniGameAgitStatus(CMiniGameAgit& agit, const unsigned int status) {
    // Current L2Server 0x0064d028.
    L2_GUARD_FUNCTION();
    agit.SendDB("chdd", 0xea, 0x30, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0, status);
}

void CDB::RequestResetMiniGameAgit(CMiniGameAgit& agit) {
    // Current L2Server 0x0064d0f4.
    L2_GUARD_FUNCTION();
    agit.SendDB("chd", 0xea, 0x34, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0);
}

void CDB::RequestLoadCompeteAgit(CWorldAgit& agit) {
    // Current L2Server 0x0064d284.
    L2_GUARD_FUNCTION();
    agit.SendDB("chd", 0xea, 0x13e, agit.m_data != nullptr ? agit.m_data->m_nResidenceID : 0);
}

void CDB::RequestNewTeamBattleAgitPledge(
    CTeamBattleAgit* const agit, CPledge* const pledge, User* const user, CNPC* const npc, const int color, const int npcType
) {
    // Current L2Server 0x0066845c.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    L2_ADD_REF(user, ObjectRefType::Cached);
    if (npc != nullptr) {
        L2_ADD_REF(npc, ObjectRefType::Cached);
    }
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("pppp", agit, pledge, user, npc);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    pledge->SendDB(
        "cddddddd", 0xb5, context, residenceId, pledge->GetDbId(), pledge->GetMasterDbId(), runtime::crt::Time32Now(),
        color, npcType
    );
}

void CDB::RequestNewTeamBattleAgitMember(CTeamBattleAgit* const agit, User* const user, CNPC* const npc) {
    // Current L2Server 0x00668610.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(user, ObjectRefType::Cached);
    if (npc != nullptr) {
        L2_ADD_REF(npc, ObjectRefType::Cached);
    }
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("ppp", agit, user, npc);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    user->SendDB(
        "cddddd", 0xb6, context, residenceId, user->GetDBID(), user->m_data->m_nPledgeID, runtime::crt::Time32Now()
    );
}

void CDB::RequestUnregisterTeamBattleAgitPledge(CTeamBattleAgit* const agit, CPledge* const pledge, User* const user) {
    // Current L2Server 0x00668794.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    L2_ADD_REF(user, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("ppp", agit, pledge, user);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    pledge->SendDB("cddd", 0xba, context, residenceId, pledge->GetDbId());
}

void CDB::RequestUnregisterTeamBattleAgitMember(CTeamBattleAgit* const agit, User* const user) {
    // Current L2Server 0x006688ec.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(user, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("pp", agit, user);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    user->SendDB("cdddd", 0xbb, context, residenceId, user->GetDBID(), user->m_data->m_nPledgeID);
}

void CDB::RequestLoadSiegeAgit(CSiegeAgit* const agit) {
    // Current L2Server 0x00668a34.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", agit);
    agit->SendDB("cdd", 0xbd, context, agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0);
}

void CDB::RequestRegisterSiegeAgitPledge(User* const user, CSiegeAgit* const agit, CPledge* const pledge) {
    // Current L2Server 0x00668b2c.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(user, ObjectRefType::Cached);
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("ppp", user, agit, pledge);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    pledge->SendDB("cdddd", 0xbe, context, residenceId, pledge->GetDbId(), runtime::crt::Time32Now());
}

void CDB::RequestUnregisterSiegeAgitPledge(User* const user, CSiegeAgit* const agit, CPledge* const pledge) {
    // Current L2Server 0x00668c90.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(user, ObjectRefType::Cached);
    L2_ADD_REF(agit, ObjectRefType::Cached);
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("ppp", user, agit, pledge);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    pledge->SendDB("cddd", 0xbf, context, residenceId, pledge->GetDbId());
}

void CDB::RequestAddMiniGameAgitPledge(const unsigned int agitId, CPledge* const pledge, const int value) {
    // Current L2Server 0x00668de8.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", pledge);
    pledge->SendDB("chdddd", 0xea, 0x31, context, agitId, pledge->GetDbId(), value);
}

void CDB::RequestDelMiniGameAgitPledge(const unsigned int agitId, CPledge* const pledge) {
    // Current L2Server 0x00668f00. The shipped writer keeps the fourth
    // `d` in its format while supplying only three dword payload values.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", pledge);
    pledge->SendDB("chdddd", 0xea, 0x32, context, agitId, pledge->GetDbId());
}

void CDB::RequestAddMiniGameAgitMemeber(const unsigned int agitId, CPledge* const pledge, User* const user) {
    // Current L2Server 0x00669000. Keep the shipped `Memeber` spelling.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    L2_ADD_REF(user, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("pp", pledge, user);
    pledge->SendDB("chdddd", 0xea, 0x33, context, agitId, pledge->GetDbId(), user->GetDBID());
}

void CDB::RequestSetTeamBattleStatus(CTeamBattleAgit* const agit, const int status) {
    // Current L2Server 0x00669144.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(agit, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", agit);
    const int residenceId = agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0;
    agit->SendDB("cddd", 0xc0, context, residenceId, status);
}

void CDB::RequestResetSiegeAgitPledge(CSiegeAgit* const agit) {
    // Current L2Server 0x0066af5c.
    L2_GUARD_FUNCTION();
    char packet[0x2008];
    int packetSize = 0;
    int count = 0;

    {
        L2_GUARD(agit->m_SiegeAgitSect);
        for (const auto& entry : agit->m_mapAttackerPledge) {
            packetSize += ::Assemble(packet + packetSize, 0x2004 - packetSize, "dd", entry.first, entry.second);
            ++count;
        }
    }

    agit->SendDB("cddb", 0xc9, agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0, count, packetSize, packet);
}

void CDB::RequestResetTeamBattleAgitPledge(CTeamBattleAgit* const agit) {
    // Current L2Server 0x0066b104.
    L2_GUARD_FUNCTION();
    char pledgePacket[0x2008];
    char memberPacket[0x2010];
    int pledgePacketSize = 0;
    int memberPacketSize = 0;
    int pledgeCount = 0;
    int memberCount = 0;

    {
        L2_GUARD(agit->m_TeamBattleSect);
        const auto appendEntry = [&](const TeamBattleInfo& entry, const int color) {
            if (entry.nPledgeDbId == 0) {
                return;
            }
            pledgePacketSize += ::Assemble(
                pledgePacket + pledgePacketSize, 0x2004 - pledgePacketSize, "dddd", entry.nPledgeDbId, entry.tPropose, color,
                entry.nNpcType
            );
            ++pledgeCount;
            for (const auto& member : entry.mapEntryPledgeMember) {
                memberPacketSize += ::Assemble(
                    memberPacket + memberPacketSize, 0x2004 - memberPacketSize, "ddd", member.first, entry.nPledgeDbId,
                    member.second
                );
                ++memberCount;
            }
        };

        appendEntry(agit->m_TeamBattleMasterInfo, 0);
        for (int color = 0; color < TBC_MAX; ++color) {
            appendEntry(agit->m_TeamBattleEntryInfo[color], color);
        }
    }

    agit->SendDB(
        "cddbdb", 0xca, agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0, pledgeCount, pledgePacketSize,
        pledgePacket, memberCount, memberPacketSize, memberPacket
    );
}

void CDB::RequestResetCastleSiegePledge(CCastle* const castle) {
    // Current L2Server 0x00648a2c. Preserve the target's conjunction when
    // excluding the owner from the defending list.
    L2_GUARD_FUNCTION();
    char packet[0x2008];
    int packetSize = 0;
    int count = 0;

    CResidenceData* const data = castle->m_data;
    for (int index = 0; index < data->m_DefendingPledgeListSize; ++index) {
        const PledgeInfo& pledge = data->m_stDefendingPledgeList[static_cast<std::size_t>(index)];
        if (pledge.m_nPledgeID != static_cast<int>(data->m_nResidenceOwnerID) &&
            pledge.m_nWorldID != data->m_nResidenceOwnerWorldId) {
            packetSize +=
                ::Assemble(packet + packetSize, 0x2004 - packetSize, "ddd", pledge.m_nPledgeID, 2, pledge.m_nWorldID);
            ++count;
        }
    }
    for (int index = 0; index < data->m_OffendingPledgeListSize; ++index) {
        const PledgeInfo& pledge = data->m_stOffendingPledgeList[static_cast<std::size_t>(index)];
        packetSize += ::Assemble(packet + packetSize, 0x2004 - packetSize, "ddd", pledge.m_nPledgeID, 1, pledge.m_nWorldID);
        ++count;
    }

    castle->SendDB("cddb", 0x56, data->m_nResidenceID, count, packetSize, packet);
}

void CDB::RequestSaveCastleSiegeElapsedTime(CCastle& castle, const int elapsedTime) {
    // Current L2Server 0x00648be8.
    L2_GUARD_FUNCTION();
    castle.SendDB("cdd", 0x67, castle.m_data != nullptr ? castle.m_data->m_nResidenceID : 0, elapsedTime);
}

void CDB::RequestEndSiege(CCastle& castle, const int ownerPledgeId, const int attackerScore, const int defenderScore) {
    // Current L2Server 0x0064c8b8.
    L2_GUARD_FUNCTION();
    castle.SendDB(
        "cdddd", 0xab, castle.m_data != nullptr ? castle.m_data->m_nResidenceID : 0, ownerPledgeId, attackerScore,
        defenderScore
    );
}

void CDB::RequestLoadPledgeSiegeKillDeathCount(CCastle* const castle) {
    // Current L2Server 0x00652a3c.
    L2_GUARD_FUNCTION();
    castle->SendDB("chd", 0xea, 0x21, castle->m_data->m_nResidenceID);
}

void CDB::RequestSetPreviousCastleOwnerID(CCastle& castle) {
    // Current L2Server 0x00654778.
    L2_GUARD_FUNCTION();
    CResidenceData* const data = castle.m_data;
    castle.SendDB(
        "chddd", 0xea, 0x52, data != nullptr ? data->m_nResidenceID : 0, data->m_nResidenceOwnerID,
        data->m_nResidenceOwnerWorldId
    );
}

void CDB::RequestSaveAllControlTowerStatus(CResidence* const residence) {
    // Current L2Server 0x006662a8.
    L2_GUARD_FUNCTION();
    for (CControlTower* const tower : residence->m_ControlTowerList) {
        residence->SendDB(
            "cSddd", 0x9b, tower->m_sControlTowerName, tower->m_nControlLevel, static_cast<int>(tower->GetHP()),
            static_cast<int>(tower->m_eStatus)
        );
    }
}

void CDB::RequestSaveControlTowerStatus(CControlTower* const tower) {
    // Current L2Server 0x0064c7cc has no null guard.
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "cSddd", 0x9b, tower->m_sControlTowerName, tower->m_nControlLevel,
        runtime::Win64CvttDoubleToInt32(tower->GetHP()), static_cast<int>(tower->m_eStatus)
    );
}

void CDB::RequestPrepareCastleWar(
    User* const user, CCastle* const castle, CPledge* const pledge, const int operation, const int proposeTime
) {
    // Current L2Server 0x00667e0c.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(castle, ObjectRefType::Cached);
    L2_ADD_REF(pledge, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("pp", castle, pledge);
    SendDB(
        g_nWorldId, "cddddddd", 0x4c, static_cast<int>(user->GetServerId()), user->m_data->GetWorldId(), context, operation,
        castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, static_cast<int>(pledge->GetDbId()), proposeTime
    );
}

void CDB::RequestConfirmCastleDefence(
    User* const user, CCastle* const castle, const unsigned int pledgeId, const unsigned int operation, const int worldId
) {
    // Current L2Server 0x00667f90. The current sender places
    // the castle residence ID in the reply peer's third request lane and
    // worldId in its sixth lane.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(castle, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", castle);
    castle->SendDB(
        "cdddddd", 0x4f, static_cast<int>(user->GetServerId()), context,
        castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, pledgeId, operation, worldId
    );
}

void CDB::RequestQueryCastleSiegeForRegister(User* const user, CCastle* const castle, const int operation) {
    // Current L2Server 0x006680c4.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(castle, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", castle);
    castle->SendDB(
        "cddddddd", 0x50, static_cast<int>(user->GetServerId()), user->m_data->GetWorldId(), context,
        castle->m_data->m_nResidenceID, user->m_data->m_nPledgeType, static_cast<int>(user->m_data->m_nPledgeID), operation
    );
}

void CDB::RequestSetNextCastleSiege(User* const user, CCastle* const castle, const int siegeTime) {
    // Current L2Server 0x00668334.
    L2_GUARD_FUNCTION();
    const int userSid = user != nullptr ? static_cast<int>(user->GetServerId()) : 0;
    L2_ADD_REF(castle, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", castle);
    castle->SendDB(
        "cdddd", 0x94, userSid, context, castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, siegeTime
    );
}

void CDB::RequestQueryCastleSiegeForUnregister(User* const user, CCastle* const castle) {
    // Current L2Server 0x00668208.
    L2_GUARD_FUNCTION();
    L2_ADD_REF(castle, ObjectRefType::Cached);
    const int context = Singleton<CCacheContextManager>::inst().RegisterContext("p", castle);
    castle->SendDB(
        "cddddd", 0x51, static_cast<int>(user->GetServerId()), user->m_data->GetWorldId(), context,
        castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, user->m_data->m_nPledgeID
    );
}

void CDB::RequestLoadAllCastle() {
    // Current L2Server 0x0066b430 dereferences every mapped
    // residence and the selected castle data without null guards.
    L2_GUARD_FUNCTION();
    char packet[0x2008];
    int packetSize = 0;
    int count = 0;

    {
        L2_GUARD(g_ResidenceDB.m_ResidenceMapLock);
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence->IsCastle()) {
                CResidenceData* const data = residence->m_data;
                packetSize += ::Assemble(
                    packet + packetSize, 0x2004 - packetSize, "dS", data->m_nResidenceID, data->m_sResidenceName.data()
                );
                ++count;
            }
        }
    }

    g_JobFinishSignal.m_nMustLoadCastle = count;
    if (count == 0) {
        SendDB(g_nWorldId, "cd", 0x1a, 0);
    } else {
        SendDB(g_nWorldId, "cdb", 0x1a, count, packetSize, packet);
    }
}

void CDB::RequestLoadAllAgit() {
    // Current L2Server 0x0066b60c.
    L2_GUARD_FUNCTION();
    char packet[0x2008]{};
    int packetSize = 0;
    int count = 0;

    {
        L2_GUARD(g_ResidenceDB.m_ResidenceMapLock);
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence != nullptr && residence->IsWorldAgit()) {
                CResidenceData* const data = residence->m_data;
                packetSize += ::Assemble(
                    packet + packetSize, 0x2004 - packetSize, "dS", data->m_nResidenceID, data->m_sResidenceName.data()
                );
                ++count;
            }
        }
    }

    g_JobFinishSignal.m_nMustLoadAgit = count;
    if (count > 0) {
        SendDB(g_nWorldId, "cdb", 0x1b, count, packetSize, packet);
    }
}

void CDB::RequestLoadAllFortress() {
    // Current L2Server 0x0066bbb0 dereferences every mapped
    // residence and the selected fortress data without null guards.
    L2_GUARD_FUNCTION();
    char packet[0x2008];
    int packetSize = 0;
    int count = 0;

    {
        L2_GUARD(g_ResidenceDB.m_ResidenceMapLock);
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence->IsFortress()) {
                CResidenceData* const data = residence->m_data;
                packetSize += ::Assemble(
                    packet + packetSize, 0x2004 - packetSize, "dS", data->m_nResidenceID, data->m_sResidenceName.data()
                );
                ++count;
            }
        }
    }

    g_JobFinishSignal.m_nMustLoadFortress = count;
    if (count != 0) {
        SendDB(g_nWorldId, "chdb", 0xea, 0x63, count, packetSize, packet);
    }
}

void CDB::RequestSaveResidenceStatus(CResidence* residence) {
    L2_GUARD_FUNCTION();

    if (residence->IsCastle()) {
        const CResidenceData* const data = residence->m_data;
        const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
        const int residenceState = data != nullptr ? static_cast<int>(data->m_ResidenceState) : static_cast<int>(RS_MAX);
        residence->SendDB("cdd", 0x5a, residenceId, residenceState);
        return;
    }

    if (residence->IsWorldAgit()) {
        const CResidenceData* const data = residence->m_data;
        const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
        const int residenceState = data != nullptr ? static_cast<int>(data->m_ResidenceState) : static_cast<int>(RS_MAX);
        residence->SendDB("cddd", 0x62, residenceId, 3, residenceState);
        const CResidenceData* const publishedData = residence->m_data;
        // Log.Add(
        //     LOG_NORMAL,
        //     L"set agit[%d] status[%d]",
        //     publishedData != nullptr ? publishedData->m_nResidenceID : 0,
        //     publishedData != nullptr
        //         ? static_cast<int>(publishedData->m_ResidenceState)
        //         : static_cast<int>(RS_MAX))
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[residence] Status update requested for clan hall %d, status %d. Latest local state: hall %d, status %d.",
        //     residenceId,
        //     residenceState,
        //     publishedData != nullptr ? publishedData->m_nResidenceID : 0,
        //     publishedData != nullptr ? static_cast<int>(publishedData->m_ResidenceState) : static_cast<int>(RS_MAX))
        L2_LOG_INFO(
            Log, u"[residence] Status update requested for clan hall %d, status %d; latest local state: hall %d, status %d.",
            residenceId, residenceState, publishedData != nullptr ? publishedData->m_nResidenceID : 0,
            publishedData != nullptr ? static_cast<int>(publishedData->m_ResidenceState) : static_cast<int>(RS_MAX)
        );
        return;
    }

    if (residence->IsDominion()) {
        CDominion* const dominion = residence->CastDominion();
        if (dominion != nullptr) {
            const CResidenceData* const data = dominion->m_data;
            RequestSaveDominion(
                *dominion, data->m_nDominionStatus,
                data != nullptr ? static_cast<int>(data->m_ResidenceState) : static_cast<int>(RS_MAX)
            );
        }
    }
}

void CDB::RequestSaveDominionRenamed(User& user, int renamed) {
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0xae, user.GetDBID(), renamed);
}

void CDB::RequestDeleteAllDominionRenamed() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0xb0);
}

void CDB::SendCachePing() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0xb5, runtime::GetTickCount());
}

void CDB::RequestCommissionTradeRegTotalCount(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x108, value1, value2);
}

void CDB::RequestCommissionTradeDeleteAll(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x107, value);
}

void CDB::RequestCommissionTradeLoadInfo() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x100);
}

void CDB::RequestCommissionTradeLoadAll(std::int64_t value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chQ", 0xea, 0x101, value);
}

void CDB::RequestCommissionTradeRegister(
    CommissionOwner::Enum owner,
    Commission::Enum commissionType,
    int characterId,
    int itemId,
    SharedUtilsWideChar* itemName,
    std::int64_t itemPrice,
    std::int64_t itemAmount,
    int itemConsumeType,
    std::int64_t commissionFee,
    CommissionExpired::Enum expiredType,
    const char16_t* userName,
    CrystalType crystalType,
    GItemType::Enum generalType,
    CommissionFirst::Enum firstType,
    CommissionDiscountInfo::Enum discountType,
    int premiumItemClassId,
    int premiumItemId,
    std::int64_t premiumItemAmount,
    int enchanted
) {
    // Current L2Server 0x0065ba58.
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chddSQQdQdddSddddddQd", 0xea, 0x102, characterId, itemId, itemName, itemPrice, itemAmount,
        itemConsumeType, commissionFee, static_cast<int>(commissionType), static_cast<int>(expiredType),
        static_cast<int>(owner), userName, static_cast<int>(crystalType), static_cast<int>(generalType),
        static_cast<int>(firstType), static_cast<int>(discountType), premiumItemClassId, premiumItemId, premiumItemAmount,
        enchanted
    );
}

void CDB::RequestCommissionTradeRegisteredList(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x105, value);
}

void CDB::RequestCommissionTradeDelete(
    CommissionOwner::Enum owner,
    std::int64_t commissionId,
    int characterId,
    int itemId,
    std::int64_t itemAmount,
    Commission::Enum commissionType
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chQddQdd", 0xea, 0x103, commissionId, characterId, itemId, itemAmount, static_cast<std::int32_t>(owner),
        static_cast<std::int32_t>(commissionType)
    );
}

void CDB::RequestCommissionTradeBuyItem(
    int buyerId, std::int64_t commissionId, std::int64_t requestedAmount, CommissionOwner::Enum owner
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chQdQd", 0xea, 0x104, commissionId, buyerId, requestedAmount, static_cast<std::int32_t>(owner));
}

void CDB::RequestCommissionTradeSearch(
    int requestId,
    int depth,
    Commission::Enum commissionType,
    int itemGeneral,
    CrystalType itemGrade,
    SharedUtilsWideChar* itemName
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chdddddS", 0xea, 0x106, requestId, depth, static_cast<std::int32_t>(commissionType), itemGeneral,
        static_cast<std::int32_t>(itemGrade), itemName
    );
}

void CDB::RequestLoadNPCQuestLog(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x14a, value1, value2, value3);
}

void CDB::RequestCheckCharName(int value1, int value2, const char16_t* value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddS", 0xea, 0x110, value1, value2, value3);
}

void CDB::RequestSaveUserJobGroup(int worldId, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chdd", 0xea, 0x112, value2, value3);
}

void CDB::RequestSaveUserJobGroup(User& user) {
    // Current L2Server 0x0065cb54.
    L2_GUARD_FUNCTION();
    user.SendDB("chdd", 0xea, 0x112, user.GetDBID(), user.m_nJobGroup);
}

void CDB::RequestSetDailyResetTime(const char16_t* value1, const char16_t* value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chSS", 0xea, 0x114, value1, value2);
}

void CDB::RequestEndPledgeGame(int value1, int value2, int value3, int value4, int value5) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddd", 0xea, 0x12f, value1, value2, value3, value4, value5);
}

void CDB::RequestUpdatePledgeGameResult(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x130, value1, value2, value3);
}

void CDB::SetGotItemPledgeGame(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x131, value);
}

void CDB::RequestIncreasePledgeGameSurrenderCount(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x132, value1, value2, value3);
}

bool CDB::RequestLoadMatchingPoint(
    const MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<UserMatchingPoint>& users, const int flag
) {
    L2_GUARD_FUNCTION();

    if (users.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invald member count",
        //     L"CDB::RequestLoadMatchingPoint",
        //     0x25ce)
        // Log.Add(
        //     LOG_WARN,
        //     L"[matching] Point load not requested: participant list is empty (type %d).",
        //     static_cast<int>(type))
        L2_LOG_WARN(
            Log, u"[matching] Point load not requested: participant list is empty (type %d).", static_cast<int>(type)
        );
        return false;
    }
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MatchingPoint::MPT_MAX)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid parameter, cTypeo: %d",
        //     L"CDB::RequestLoadMatchingPoint",
        //     0x25d4,
        //     static_cast<int>(type))
        // Log.Add(
        //     LOG_WARN,
        //     L"[matching] Point load not requested: type %d is outside 0..%d (participants %zu).",
        //     static_cast<int>(type),
        //     static_cast<int>(MatchingPoint::MPT_MAX) - 1,
        //     users.size())
        L2_LOG_WARN(
            Log, u"[matching] Point load not requested: type %d is outside 0..%d (participants %zu).",
            static_cast<int>(type), static_cast<int>(MatchingPoint::MPT_MAX) - 1, users.size()
        );
        return false;
    }

    CPacketHelperEx<20480> packet;
    if (type == MatchingPoint::MPT_OLYMPIAD) {
        packet.Assemble("ccd", static_cast<int>(MatchingPoint::MPT_OLYMPIAD), flag, static_cast<int>(users.size()));
    } else {
        packet.Assemble("cd", static_cast<int>(type), static_cast<int>(users.size()));
    }
    for (const UserMatchingPoint& user : users) {
        packet.Assemble("dd", static_cast<int>(user.nUserSID), static_cast<int>(user.nUserDBID));
    }
    SendDB(g_nWorldId, "chb", 0xea, 0x18b, packet.GetLength(), packet.GetPacket());
    // Noise: normal matching-point request submission is routine and does not confirm a successful database load.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][MatchPoint][%s][%d] success",
    //     L"CDB::RequestLoadMatchingPoint",
    //     0x25f0)
    return true;
}

bool CDB::RequestSaveMatchingPoint(const MatchingPoint::MATCHINGPOINT_TYPE type, std::vector<UserMatchingPoint>& users) {
    L2_GUARD_FUNCTION();

    if (users.size() < 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid member count",
        //     L"CDB::RequestSaveMatchingPoint",
        //     0x2650)
        // Log.Add(
        //     LOG_WARN,
        //     L"[matching] Point save not requested: %zu participants supplied. At least two are required (type %d).",
        //     users.size(),
        //     static_cast<int>(type))
        L2_LOG_WARN(
            Log, u"[matching] Point save not requested: %zu participants supplied; at least two are required (type %d).",
            users.size(), static_cast<int>(type)
        );
        return false;
    }
    if (static_cast<std::uint32_t>(type) >= static_cast<std::uint32_t>(MatchingPoint::MPT_MAX)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MatchPoint][%s][%d] invalid parameter, cTypeo: %d",
        //     L"CDB::RequestSaveMatchingPoint",
        //     0x2656,
        //     static_cast<int>(type))
        // Log.Add(
        //     LOG_WARN,
        //     L"[matching] Point save not requested: type %d is outside 0..%d (participants %zu).",
        //     static_cast<int>(type),
        //     static_cast<int>(MatchingPoint::MPT_MAX) - 1,
        //     users.size())
        L2_LOG_WARN(
            Log, u"[matching] Point save not requested: type %d is outside 0..%d (participants %zu).",
            static_cast<int>(type), static_cast<int>(MatchingPoint::MPT_MAX) - 1, users.size()
        );
        return false;
    }

    CPacketHelperEx<20480> packet;
    packet.Assemble("cd", static_cast<int>(type), static_cast<int>(users.size()));
    for (const UserMatchingPoint& user : users) {
        packet.Assemble("dq", static_cast<int>(user.nUserDBID), user.nMatchingPoint);
    }
    SendDB(g_nWorldId, "chb", 0xea, 0x18c, packet.GetLength(), packet.GetPacket());
    // Noise: normal matching-point request submission is routine and does not confirm persistence.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][MatchPoint][%s][%d] success",
    //     L"CDB::RequestSaveMatchingPoint",
    //     0x266b)
    return true;
}

void CDB::RequestGetPledgeGameRecord(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdd", 0xea, 0x133, value1, value2);
}

void CDB::RequestUpdateKalieEvent(int value1, int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x139, value1, value2, value3);
}

void CDB::RequestUpdateKalieEventForUser(User& user, const int remainToken) {
    // Current L2Server 0x0065e018.
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x13a, 1, static_cast<int>(user.GetDBID()), remainToken);
}

void CDB::RequestUpdateKalieEventForUser(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x0065e104.
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdb", 0xea, 0x13a, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
}

void CDB::RequestLoadKalieEventForUser(User& user) {
    L2_GUARD_FUNCTION();
    user.SendDB("chd", 0xea, 0x138, user.GetDBID());
}

void CDB::RequestPledgeJoinSystemInit(int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x156, value);
}

void CDB::RequestLoadPledgeOnPledgeRecruitBoard() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x157);
}

void CDB::RequestWritePledgeRecruitBoard(
    User* const user,
    const int recruitType,
    const char16_t* const title,
    const char16_t* const description,
    const int sortCondition,
    const PledgeType pledgeType
) {
    // Current L2Server 0x0065e8f8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    pledge->SendDB(
        "chddddSSdd", 0xea, 0x158, pledge->GetWorldId(), pledge->GetDbId(), user->GetDBID(), recruitType,
        title, description, sortCondition, static_cast<int>(pledgeType)
    );
}

void CDB::RequestModifyPledgeRecruitBoard(
    User* const user,
    const int recruitType,
    const char16_t* const title,
    const char16_t* const description,
    const int sortCondition,
    const PledgeType pledgeType
) {
    // Current L2Server 0x0065ea3c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    pledge->SendDB(
        "chddddSSdd", 0xea, 0x159, pledge->GetWorldId(), pledge->GetDbId(), user->GetDBID(), recruitType,
        title, description, sortCondition, static_cast<int>(pledgeType)
    );
}

void CDB::RequestDeletePledgeRecruitBoard(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x15a, user->m_data->m_nWorldId, user->m_data->m_nPledgeID);
    }
}

void CDB::RequestDeletePledgeRecruitBoard(CPledge* const pledge) {
    // Current L2Server 0x0065ec58.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    const int worldId = pledge->GetWorldId();
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    pledge->SendDB("chdd", 0xea, 0x15a, worldId, pledgeDatabaseId);
}

void CDB::RequestSearchPledgeRecruitBoard(
    User* const user,
    const char16_t* const pledgeName,
    const int minimumLevel,
    const int maximumLevel,
    const char16_t* const masterName,
    const int pledgeType,
    const int sortCondition,
    const int orderCondition,
    const int searchCondition
) {
    // Current L2Server 0x0065ee10.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    user->SendDB(
        "chddSddSdddd", 0xea, 0x15b, user->GetDBID(), user->m_data->m_nWorldId, pledgeName, minimumLevel,
        maximumLevel, masterName, pledgeType, sortCondition, orderCondition, searchCondition
    );
}

void CDB::RequestPledgeOnPledgeRecruitBoard(User* user, int pledgeId) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x15d, worldId, characterId, pledgeId);
    }
}

void CDB::RequestApplyInfoOnPledgeWaitingList(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chdd", 0xea, 0x15e, worldId, characterId);
    }
}

void CDB::RequestWritePledgeWaitingUserList(User* const user, const int pledgeId, const char16_t* const comment) {
    // Current L2Server 0x0065f0f8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    user->SendDB(
        "chddSddddS", 0xea, 0x15f, user->m_data->m_nWorldId, user->GetDBID(), user->m_data->m_sName,
        user->GetClass(), user->m_data->m_nLevel, pledgeId, runtime::crt::Time32Now(),
        comment
    );
}

void CDB::RequestDeletePledgeWaitingUserList(User* user, int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t requesterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x160, worldId, requesterId, characterId);
    }
}

void CDB::RequestDeletePledgeWaitingUserList(const char16_t* value, int worldId) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "chddS", 0xea, 0x160, worldId, -1, value);
}

void CDB::RequestPledgeWaitingUserList(User* user, int pledgeId) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0x161, worldId, characterId, pledgeId);
    }
}

void CDB::RequestUserOnPledgeWaitingList(User* user, int pledgeId, int characterId) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t requesterId = user->GetDBID();
        user->SendDB("chdddd", 0xea, 0x162, worldId, requesterId, pledgeId, characterId);
    }
}

void CDB::RequestWritePledgeDraftUserList(User* const user, const int value) {
    // Current L2Server 0x0065f5c8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    user->SendDB(
        "chddSdddd", 0xea, 0x163, user->m_data->m_nWorldId, user->GetDBID(), user->m_data->m_sName,
        static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel, value, runtime::crt::Time32Now()
    );
}

void CDB::RequestDeletePledgeDraftUserList(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chdd", 0xea, 0x164, worldId, characterId);
    }
}

void CDB::RequestPledgeDraftUserList(
    User* user, int minimumLevel, int maximumLevel, int classGroup, const char16_t* name, int sortCondition, int orderCondition
) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const int worldId = user->m_data->m_nWorldId;
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB(
            "chdddddSdd", 0xea, 0x165, worldId, characterId, minimumLevel, maximumLevel, classGroup,
            name, sortCondition, orderCondition
        );
    }
}

void CDB::RequestPledgeRecruitApplyInfo(User* user) {
    L2_GUARD_FUNCTION();
    std::uint32_t pledgeId = 0;
    if (user != nullptr) {
        if (user->GetPledge() != nullptr) {
            pledgeId = user->GetPledge()->GetDbId();
        }
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chdd", 0xea, 0x166, characterId, pledgeId);
    }
}

void CDB::RequestSetPledgeRecruitCount(User* user, CPledge* pledge, int count) {
    L2_GUARD_FUNCTION();
    if (user != nullptr && pledge != nullptr) {
        const std::uint32_t pledgeId = pledge->GetDbId();
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chdddd", 0xea, 0x167, characterId, 1, pledgeId, count);
    }
}

void CDB::RequestPledgeRemainTime(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t pledgeId = user->m_data->m_nPledgeID;
        if (pledgeId != 0) {
            const std::uint32_t characterId = user->GetDBID();
            user->SendDB("chddd", 0xea, 0x167, characterId, 3, pledgeId);
        }
    }
}

void CDB::RequestUserRemainTime(User* user) {
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t characterId = user->GetDBID();
        user->SendDB("chdd", 0xea, 0x167, characterId, 2);
    }
}

void CDB::RequestSavePetitionExternalNotify(const char16_t* value1, unsigned int value2, int value3) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chSdd", 0xea, 0x16a, value1, value2, value3);
}

void CDB::RequestResetDormantHistory(int value1, int value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddd", 0xea, 0x16d, value1, -1, value2);
}

void CDB::RequestGetDormantInfo(int value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddd", 0xea, 0x172, value1, value2, value3, value4);
}

void CDB::RequestLoadWOlympiadMatchCount() {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "ch", 0xea, 0x1a6);
}

void CDB::RequestLoadPledge(int worldId, unsigned int value, bool flag) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cdc", 0x14, value, flag);
}

void CDB::RequestLoadPledge(int worldId, const char16_t* value) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cS", 0x69, value);
}

void CDB::RequestLoadSubPledge(CPledge& pledge) {
    // Current L2Server 0x0064465c.
    L2_GUARD_FUNCTION();
    pledge.SendDB("chdd", 0xea, 0x36, pledge.GetDbId(), pledge.GetRemainSubPledgeTypeBits());
}

void CDB::RequestLoadPledgeSkill(CPledge& pledge) {
    // Current L2Server 0x00644730.
    L2_GUARD_FUNCTION();
    pledge.SendDB("chd", 0xea, 0x37, pledge.GetDbId());
}

void CDB::RequestLoadBattleCamp(CResidence* const residence, const int value) {
    // Current L2Server 0x00665678.
    L2_GUARD_FUNCTION();
    if (residence == nullptr) {
        SendDB(g_nWorldId, "cdd", 0x4b, 0, value);
        return;
    }
    residence->SendDB("cdd", 0x4b, residence->m_data->m_nResidenceID, value);
}

void CDB::RequestInstallBattleCamp(
    CPledge* pledge, unsigned int residenceId, unsigned int maximumHp, FVector position, int baseType
) {
    // Current L2Server 0x00665770.
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "cddddddddd", 0x4a, 0, pledge->GetDbId(), pledge->GetWorldId(), residenceId, maximumHp,
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z), baseType
    );
}

void CDB::RequestUninstallBattleCampBy(CResidence* residence) {
    // Current L2Server 0x0066589c.
    L2_GUARD_FUNCTION();
    const int residenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
    SendDB(g_nWorldId, "cdd", 0x4a, 2, residenceId);
}

void CDB::RequestUninstallBattleCampBy(CPledge* pledge, int baseType) {
    // Current L2Server 0x0066596c.
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdddd", 0x4a, 1, pledge->GetDbId(), pledge->GetWorldId(), baseType);
}

void CDB::RequestDeletePledgeByTimer(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }
    pledge->SendDB("cdd", 0x40, pledge->Id(), pledge->GetDbId());
}

void CDB::RequestResetUserNickname(int worldId, unsigned int value) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cdd", 0x49, value);
}

void CDB::SendAcquiredSkillForAdmin(int value1, int value2, int value3, int value4, bool value5) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdddddd", 0x26, value1, value2, value3, value4, value5, -1);
}

void CDB::SendAcquiredSkill(User& user, int skillId, int skillLevel, int skillSubLevel, bool saveToDatabase) {
    // Current L2Server 0x00645634.
    L2_GUARD_FUNCTION();
    user.SendDB(
        "cdddddd", 0x26, user.GetDBID(), skillId, skillLevel, skillSubLevel, saveToDatabase, user.m_data->m_nSubJobId
    );
}

void CDB::RequestChangePos(const char16_t* value1, int value2, int value3, int value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cSddd", 0x32, value1, value2, value3, value4);
}

void CDB::SendSetLoginAnnounce(int value1, const char16_t* value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdS", 0x34, value1, value2);
}

void CDB::RequestSetPledgeInfo(int worldId, unsigned int value2, unsigned int value3, unsigned int value4) {
    L2_GUARD_FUNCTION();
    if (worldId == g_nWorldId) {
        SendDB(worldId, "cddd", 0x3a, value2, value3, value4);
    }
}

void CDB::RequestUpdateUserPledge(
    User* const user, CPledge* const pledge, const int updateType, const PledgeType pledgeType
) {
    // Current L2Server 0x00643da8.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int userWorldId = user->m_data->m_nWorldId;
    if (userWorldId != g_nWorldId || userWorldId != pledge->GetWorldId()) {
        return;
    }

    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t pledgeServerId = pledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB("cdddddd", 0x0a, userServerId, pledgeServerId, updateType, userDatabaseId, pledgeDatabaseId, pledgeType);
}

void CDB::RequestReorganizePledgeMember(
    User* const user,
    CPledge* const pledge,
    const int action,
    const unsigned int firstCharacterDatabaseId,
    const PledgeType pledgeType,
    const unsigned int secondCharacterDatabaseId
) {
    // Current L2Server 0x006549e4.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB(
        "chddddddd", 0xea, 0x55, userServerId, userDatabaseId, pledgeDatabaseId, action, firstCharacterDatabaseId,
        pledgeType, secondCharacterDatabaseId
    );
}

void CDB::RequestSigninPledge(CSPointer<User> user, CSPointer<CPledge> pledge, const PledgeType pledgeType) {
    // Current L2Server 0x00667af8.
    L2_GUARD_FUNCTION();
    if (!user || !pledge || user->m_data->m_nWorldId != g_nWorldId || user->m_data->m_nWorldId != pledge->GetWorldId()) {
        return;
    }

    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t userDatabaseId = user->GetDBID();
    const std::uint32_t pledgeServerId = pledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB("chddddd", 0xea, 0x19d, userServerId, pledgeServerId, userDatabaseId, pledgeDatabaseId, pledgeType);
}

void CDB::RequestOustPledgeMember(User* const user, CPledge* const pledge, const char16_t* const memberName) const {
    // Current L2Server 0x00644f7c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int userWorldId = user->m_data->m_nWorldId;
    if (userWorldId != g_nWorldId || userWorldId != pledge->GetWorldId()) {
        return;
    }

    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t pledgeServerId = pledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB(
        "cdddS", 0x1f, userServerId, pledgeServerId, pledgeDatabaseId,
        memberName
    );
}

void CDB::RequestSetPledgeCrest(User* const user, CPledge* const pledge, const unsigned char* const data, const int size) {
    // Current L2Server 0x0064509c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int userWorldId = user->m_data->m_nWorldId;
    if (userWorldId != g_nWorldId || userWorldId != pledge->GetWorldId()) {
        return;
    }

    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t pledgeServerId = pledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    if (size > 0) {
        user->SendDB("cddddb", 0x21, userServerId, pledgeServerId, pledgeDatabaseId, size, size, data);
    } else {
        user->SendDB("cdddd", 0x21, userServerId, pledgeServerId, pledgeDatabaseId, 0);
    }
}

void CDB::RequestSetPledgeEmblem(
    User* const user,
    CPledge* const pledge,
    const int pieceIndex,
    const int totalFileSize,
    const int pieceSize,
    const unsigned char* const pieceData,
    const bool skipCanRegister
) {
    // Current L2Server 0x0064520c, original owner DB.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int userWorldId = user->m_data->m_nWorldId;
    if (userWorldId != g_nWorldId || userWorldId != pledge->GetWorldId()) {
        return;
    }

    // Noise: each emblem fragment repeats routine upload progress. This entry does not report an error or a confirmed stored
    // emblem. Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR] RQ_EX_SET_PLEDGE_EMBLEM %d %d %d",
    //     pieceIndex,
    //     totalFileSize,
    //     pieceSize)
    const std::uint32_t pledgeDatabaseId = pledge->GetDbId();
    const std::uint32_t pledgeServerId = pledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    user->SendDB(
        "chdddddddb", 0xea, 0x19, userServerId, pledgeServerId, pledgeDatabaseId, pieceIndex, totalFileSize, pieceSize,
        skipCanRegister, pieceSize, pieceData
    );
}

void CDB::RequestSetAllianceCrest(
    User* const user, CAlliance* const alliance, const unsigned char* const data, const int size
) {
    // Current L2Server 0x006453bc.
    L2_GUARD_FUNCTION();
    if (user == nullptr || alliance == nullptr) {
        return;
    }

    const int userWorldId = user->m_data->m_nWorldId;
    if (userWorldId != g_nWorldId || userWorldId != alliance->m_nWorldId) {
        return;
    }

    const std::uint32_t allianceDatabaseId = alliance->m_data->m_nDbId;
    const std::uint32_t userServerId = user->GetServerId();
    if (size > 0) {
        user->SendDB("cdddb", 0x82, userServerId, allianceDatabaseId, size, size, data);
    } else {
        user->SendDB("cddd", 0x82, userServerId, allianceDatabaseId, 0);
    }
}

void CDB::RequestJoinAlliance(
    User* const user, CAlliance* const alliance, CPledge* const masterPledge, CPledge* const joiningPledge
) {
    // Current L2Server 0x00649d54 has no null/world gate and
    // routes through the joining pledge's DB socket.
    L2_GUARD_FUNCTION();
    const std::uint32_t allianceDatabaseId = alliance->m_data->m_nDbId;
    const std::uint32_t joiningPledgeDatabaseId = joiningPledge->GetDbId();
    const std::uint32_t joiningPledgeServerId = joiningPledge->Id();
    const std::uint32_t masterPledgeServerId = masterPledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    joiningPledge->SendDB(
        "cddddd", 0x70, userServerId, masterPledgeServerId, joiningPledgeServerId, allianceDatabaseId,
        joiningPledgeDatabaseId
    );
}

void CDB::RequestDismissAlliance(User* const user, CAlliance* const alliance, CPledge* const masterPledge) {
    // Current L2Server 0x00649e6c accepts a null user but not a null
    // alliance/master pledge pair.
    L2_GUARD_FUNCTION();
    if (alliance == nullptr || masterPledge == nullptr) {
        return;
    }

    std::uint32_t userServerId = 0;
    if (user != nullptr) {
        userServerId = user->GetServerId();
    }
    const std::uint32_t allianceDatabaseId = alliance->m_data->m_nDbId;
    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t masterPledgeDatabaseId = masterPledge->GetDbId();
    const std::uint32_t masterPledgeServerId = masterPledge->Id();
    const std::uint32_t allianceServerId = alliance->Id();
    alliance->SendDB(
        "cdddddd", 0x71, userServerId, allianceServerId, masterPledgeServerId, now, allianceDatabaseId,
        masterPledgeDatabaseId
    );
}

void CDB::RequestWithdrawAlliance(User* const user, CAlliance* const alliance, CPledge* const memberPledge) {
    // Current L2Server 0x00649f9c has no null/world gate.
    L2_GUARD_FUNCTION();
    const std::uint32_t allianceDatabaseId = alliance->m_data->m_nDbId;
    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t memberPledgeDatabaseId = memberPledge->GetDbId();
    const std::uint32_t memberPledgeServerId = memberPledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    const std::uint32_t allianceServerId = alliance->Id();
    alliance->SendDB(
        "cdddddd", 0x73, userServerId, allianceServerId, memberPledgeServerId, now, allianceDatabaseId,
        memberPledgeDatabaseId
    );
}

void CDB::RequestOustAllianceMemberPledge(
    User* const user, CAlliance* const alliance, CPledge* const masterPledge, CPledge* const oustedPledge
) {
    // Current L2Server 0x0064a0b4 has no null/world gate.
    L2_GUARD_FUNCTION();
    const std::uint32_t allianceDatabaseId = alliance->m_data->m_nDbId;
    const std::int32_t now = runtime::crt::Time32Now();
    const std::uint32_t oustedPledgeDatabaseId = oustedPledge->GetDbId();
    const std::uint32_t oustedPledgeServerId = oustedPledge->Id();
    const std::uint32_t masterPledgeServerId = masterPledge->Id();
    const std::uint32_t userServerId = user->GetServerId();
    const std::uint32_t allianceServerId = alliance->Id();
    alliance->SendDB(
        "cddddddd", 0x72, userServerId, allianceServerId, masterPledgeServerId, oustedPledgeServerId, now,
        allianceDatabaseId, oustedPledgeDatabaseId
    );
}

void CDB::RequestSavePledgeSkill(
    CPledge& pledge, const unsigned int skillId, const int skillLevel, const std::int32_t expiryTime
) {
    // Current L2Server 0x0065401c: this request is sent via
    // the pledge object so its owning world selects the DB socket.
    L2_GUARD_FUNCTION();
    pledge.SendDB("chdddd", 0xea, 0x40, pledge.GetDbId(), skillId, skillLevel, expiryTime);
}

void CDB::RequestDelPledgeSkill(CPledge& pledge, const unsigned int skillId) {
    // Current L2Server 0x00654104.
    L2_GUARD_FUNCTION();
    pledge.SendDB("chdd", 0xea, 0x42, pledge.GetDbId(), skillId);
}

void CDB::RequestSavePledgePower(CPledge& pledge, const int grade, const std::uint32_t power) {
    // Current L2Server 0x006541d0. Windows unsigned long is the 32-bit
    // protocol lane represented explicitly by std::uint32_t here.
    L2_GUARD_FUNCTION();
    pledge.SendDB("chddd", 0xea, 0x44, pledge.GetDbId(), grade, power);
}

void CDB::RequestModifyPledgeInfoOnBoard(CPledge* const pledge) {
    // Current L2Server 0x0065ef50.
    L2_GUARD_FUNCTION();
    if (pledge != nullptr) {
        pledge->SendDB("chd", 0xea, 0x15c, pledge->GetDbId());
    }
}

void CDB::RequestGetPledgeCrest(User* user, unsigned int crestId, unsigned int worldId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t userSocketId = user->GetServerId();
    SendDB(static_cast<int>(worldId), "cddd", 0x3c, userSocketId, crestId, worldId);
}

void CDB::RequestGetAllianceCrest(User* user, unsigned int crestId, unsigned int worldId) {
    L2_GUARD_FUNCTION();
    const std::uint32_t userSocketId = user->GetServerId();
    SendDB(static_cast<int>(worldId), "cddd", 0x83, userSocketId, crestId, worldId);
}

void CDB::RequestGetPledgeEmblem(unsigned int value1, unsigned int value2, unsigned int worldId, unsigned int value4) {
    L2_GUARD_FUNCTION();
    SendDB(static_cast<int>(worldId), "chdddd", 0xea, 0x1a, value1, value2, worldId, value4);
}

void CDB::RequestDeletePledgeEmblem(unsigned int value) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chd", 0xea, 0x1b, value);
}

void CDB::RequestModifyPledgeName(unsigned int value1, const char16_t* value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdS", 0x55, value1, value2);
}

void CDB::RequestModifyAllianceName(unsigned int value1, const char16_t* value2) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdS", 0xda, value1, value2);
}

void CDB::CreatePet(
    User& user,
    const int itemClassId,
    const int npcClassId,
    const std::int64_t experience,
    const int level,
    const double x,
    const double y,
    const int meal
) {
    // Current L2Server 0x0064a1e8. Preserve the target's virtual-call order.
    L2_GUARD_FUNCTION();
    const std::uint32_t databaseId = user.GetDBID();
    const std::uint32_t serverId = user.GetServerId();
    user.SendDB("cddddQdffd", 0x74, serverId, databaseId, itemClassId, npcClassId, experience, level, x, y, meal);
}

void CDB::CreatePetForAdmin(
    int value1, int value2, int value3, std::int64_t value4, int value5, double value6, double value7, int value8
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddddQdffd", 0x74, -1, value1, value2, value3, value4, value5, value6, value7, value8);
}

void CDB::DeletePet(int worldId, int value2, int value3, int value4, int value5, int value6) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cddddd", 0x75, value2, value3, value4, value5, value6);
}

void CDB::RequestSaveTimeLimitUsed(const char16_t* value1, int value2, int worldId, int value4, ::_SYSTEMTIME* value5) {
    L2_GUARD_FUNCTION();
    SendDB(worldId, "cSddb", 0xb9, value1, value2, value4, 0x10, value5);
}

void CDB::RequestSaveCastleIncome(
    CResidence& residence, std::int64_t income, std::int64_t temporaryIncome, int isCastle, int reason
) {
    // Current L2Server 0x0064903c.
    L2_GUARD_FUNCTION();
    const int residenceId = residence.m_data != nullptr ? residence.m_data->m_nResidenceID : 0;
    residence.SendDB("cdQQdd", 0x5e, residenceId, income, temporaryIncome, isCastle, reason);
}

bool CDB::RequestBuySeed(
    const int manorId,
    const int residenceId,
    User* const user,
    const std::int64_t totalPrice,
    const int itemCount,
    const int dataSize,
    char* const data
) {
    // Current L2Server 0x0064efec, original owner DB.cpp.
    L2_GUARD_FUNCTION();

    const bool isCurrentWorld = user->m_data->m_nWorldId == g_nWorldId;
    if (isCurrentWorld) {
        user->SendDB(
            "cddddQdb", 0xd0, user->GetDBID(), user->GetServerId(), manorId, residenceId, totalPrice, itemCount, dataSize,
            data
        );
    }
    return isCurrentWorld;
}

bool CDB::RequestProcureCrop(
    const int manorId, const int residenceId, User* const user, const int itemCount, const int dataSize, char* const data
) {
    // Current L2Server 0x0064f124, original owner DB.cpp.
    L2_GUARD_FUNCTION();

    const bool isCurrentWorld = user->m_data->m_nWorldId == g_nWorldId;
    if (isCurrentWorld) {
        user->SendDB("cdddddb", 0xd1, user->GetDBID(), user->GetServerId(), manorId, residenceId, itemCount, dataSize, data);
    }
    return isCurrentWorld;
}

void CDB::RequestSaveManorNextSeed(
    int value1, int value2, int value3, int value4, int value5, int value6, int value7, int value8, int value9, int value10
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdddddddddd", 0xd3, value1, value2, value3, value4, value5, value6, value7, value8, value9, value10);
}

void CDB::RequestSaveManorNowSeed(
    int value1,
    int value2,
    int value3,
    int value4,
    int value5,
    int value6,
    int value7,
    int value8,
    int value9,
    int value10,
    int value11,
    int value12,
    int value13
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "cddddddddddddd", 0xd4, value1, value2, value3, value4, value5, value6, value7, value8, value9, value10,
        value11, value12, value13
    );
}

void CDB::RequestSaveManorInfo(int value1, int value2, int value3, int value4, unsigned char value5, ::_SYSTEMTIME* value6) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddddcb", 0xd5, value1, value2, value3, value4, value5, 0x10, value6);
}

void CDB::RequestSetPledgeAnnounce(unsigned int value1, unsigned int value2, int value3, const char16_t* value4) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddS", 0xea, 0x1f, value1, value2, value3, value4);
}

void CDB::TestRequest(int value, int dataSize, const char* data) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdb", 0xea, 0xc0, value, dataSize, data);
}

void CDB::RequestLoadBlockedIP(bool load) {
    L2_GUARD_FUNCTION();
    if (!load) {
        SendDB(g_nWorldId, "chd", 0xea, 0xc1, 0);
        SendDB(g_nWorldId, "chd", 0xea, 0xc1, 1);
    } else {
        SendDB(g_nWorldId, "chd", 0xea, 0xc1, 2);
    }
}

void CDB::RequestWritePostBirth(
    int value1, const char16_t* value2, const char16_t* value3, int value4, int value5, int value6, int value7, int value8
) {
    // Current L2Server 0x0065b04c.
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chdSSddddd", 0xea, 0xf4, value1, value2, value3, value4, value5, value6,
        value7, value8
    );
}

void CDB::RequestDeletePostBirth(int value1, int value2, int value3, int value4, int value5) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddd", 0xea, 0xf5, value1, value2, value3, value4, value5);
}

void CDB::RequestManorMoveCrop(int value1, int value2, int value3, int value4, int dataSize, char* data) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cddddb", 0xd2, value1, value2, value3, value4, dataSize, data);
}

void CDB::RequestLoadAllManor(int value, int dataSize, char* data) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "cdb", 0xd6, value, dataSize, data);
}

void CDB::RequestUpdateCommPledgeMasterResult(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    int value = 0;
    int dataSize = 0;
    const unsigned char* data = ::Disassemble(packet, "dd", &value, &dataSize);
    SendDB(g_nWorldId, "chdb", 0xea, 0x51, value, dataSize, data);
}

void CDB::RequestSavePremiumItemForChar(
    int value1, int value2, const char16_t* value3, int value4, int value5, const char16_t* value6, int value7, int dataSize, char* data
) {
    L2_GUARD_FUNCTION();
    SendDB(
        g_nWorldId, "chddSddSdb", 0xea, 0x8e, value1, value2, value3, value4,
        value5, value6, value7, dataSize, data
    );
}

void CDB::RequestSavePremiumItemsForServer(int value, int dataSize, char* data) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdb", 0xea, 0x8f, value, dataSize, data);
}

void CDB::RequestGiveItem(int requestType, CPacketHelperEx<20480>* packet) {
    L2_GUARD_FUNCTION();
    if (packet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestGetItemsForServer helper is null")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Server-item request %d not sent: packet helper is missing.",
        //     requestType)
        L2_LOG_ERROR(Log, u"[db] Server-item request %d not sent: packet helper is missing.", requestType);
        return;
    }
    SendDB(g_nWorldId, "chdb", 0xea, 0xd6, requestType, packet->GetLength(), packet->GetPacket());
}

void CDB::RequestSendGiftPost(CPacketHelperEx<20480>* packet) {
    L2_GUARD_FUNCTION();
    if (packet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "RequestSendGiftPost helper is null")
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Gift-post request not sent: packet helper is missing.")
        L2_LOG_ERROR(Log, u"[db] Gift-post request not sent: packet helper is missing.");
        return;
    }
    SendDB(g_nWorldId, "chb", 0xea, 0x11d, packet->GetLength(), packet->GetPacket());
}

void CDB::RequestReceivePost(
    int value1, int value2, int value3, int value4, int value5, int value6, int dataSize, char* data, std::int64_t value9
) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chddddddbQ", 0xea, 0xb8, value1, value2, value3, value4, value5, value6, dataSize, data, value9);
}

void CDB::RequestCancelPost(int value1, int value2, int value3, int value4, int value5, int dataSize, char* data) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddddb", 0xea, 0xba, value1, value2, value3, value4, value5, dataSize, data);
}

void CDB::RequestDeletePost(int value1, int dataSize, char* data, int value4, int value5, int value6, int value7) {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdddddb", 0xea, 0xbb, value4, value5, value6, value7, value1, dataSize, data);
}

void CDB::SendCacheVersion() {
    L2_GUARD_FUNCTION();
    char revisionTag[80];
    int revision;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #373 $", "%s #%d", revisionTag, static_cast<unsigned>(sizeof(revisionTag)), &revision);
#else
    std::sscanf("$Revision: #373 $", "%79s #%d", revisionTag, &revision);
#endif
    SendDB(g_nWorldId, "cd", 0, revision);
}

bool CDB::WaitForCheckVersion(unsigned long timeoutSeconds) {
    L2_GUARD_FUNCTION();
    return m_pCheckCacheDSignal->WaitFinish(static_cast<unsigned int>(timeoutSeconds));
}

void CDB::SetDeletedInIBServer(int value, int dataSize, char* data) const {
    L2_GUARD_FUNCTION();
    SendDB(g_nWorldId, "chdb", 0xea, 0x94, value, dataSize, data);
}

void CDB::RequestLoadAllWOlympiadUser(bool startUp, bool newSeason) {
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_WOLYMPIAD_LOAD_ALL_USER packet;
    packet.bStartUp = startUp;
    packet.bNewSeason = newSeason;
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1a4), packet);
}

void CDB::RequestUpdateBalthusEventUserInfo(CSPointer<User> user) {
    // Current L2Server 0x0066c7ac.
    L2_GUARD_FUNCTION();
    protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO packet;
    packet.userInfoList.push_back(protocol::BalthusEventUserInfo{static_cast<int>(user->GetDBID()), user->m_nBalthusTokenCount});
    RequestUpdateBalthusEventUserInfo(packet);
}

void CDB::RequestUpdateBalthusEventUserInfo(protocol::RQ_UPDATE_BALTHUS_EVENT_USER_INFO& packet) {
    // Current L2Server 0x00669680.
    L2_GUARD_FUNCTION();

    CPacketHelperEx<20480> encoded;
    network::Stream stream(reinterpret_cast<unsigned char*>(encoded.GetPacket()), encoded.GetBufferSize());
    network::IPacket& typedPacket = packet;
    if (!typedPacket.Encode(&stream)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to Encode BalthusEvent Packet, errorNo: %d",
        //     kDatabaseSourceFile,
        //     0x22d7,
        //     CurrentDatabaseLastError())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[db] Balthus-event update not sent: encoding failed for %zu user records in a %d-byte buffer. Last OS error
        //     snapshot: %d (not an encoder error code).", packet.userInfoList.size(), encoded.GetBufferSize(),
        //     CurrentDatabaseLastError())
        L2_LOG_ERROR(
            Log,
            u"[db] Balthus-event update not sent: encoding failed for %zu user records in a %d-byte buffer. Last OS error "
            u"snapshot: %d (not an encoder error code).",
            packet.userInfoList.size(), encoded.GetBufferSize(), CurrentDatabaseLastError()
        );
        return;
    }

    const int encodedSize = static_cast<int>(stream.GetCursor() - stream.GetBuffer());
    SendDB(g_nWorldId, "chb", 0xea, 0x1a1, encodedSize, encoded.GetPacket());
}

void CDB::RequestWOlympiadGetMatchResult(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_WOLYMPIAD_GET_MATCH_RESULT packet;
    packet.nWorldID = worldId;
    packet.nUserDBID = characterId;
    packet.nSeason = WOlympiadService::inst().GetSeason() - 1;
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1a8), packet);
}

bool CDB::RequestLoadWOlympiadMatchingPoint(
    const std::vector<int>& worldIds, const std::vector<int>& userDatabaseIds, ::GameRuleType gameRule
) {
    L2_GUARD_FUNCTION();
    if (worldIds.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"db.cpp",
        //     0x27e3,
        //     L"WO_ASSERT_TRUE_RETURN")
        // Log.Add(
        //     LOG_WARN,
        //     L"[olympiad] World matching-point load not requested: world list is empty (character IDs %zu, game rule %d).",
        //     userDatabaseIds.size(),
        //     static_cast<int>(gameRule))
        L2_LOG_WARN(
            Log,
            u"[olympiad] World matching-point load not requested: world list is empty (character IDs %zu, game rule %d).",
            userDatabaseIds.size(), static_cast<int>(gameRule)
        );
        return false;
    }
    if (userDatabaseIds.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"db.cpp",
        //     0x27e4,
        //     L"WO_ASSERT_TRUE_RETURN")
        // Log.Add(
        //     LOG_WARN,
        //     L"[olympiad] World matching-point load not requested: character-ID list is empty (world IDs %zu, game rule
        //     %d).", worldIds.size(), static_cast<int>(gameRule))
        L2_LOG_WARN(
            Log,
            u"[olympiad] World matching-point load not requested: character-ID list is empty (world IDs %zu, game rule %d).",
            worldIds.size(), static_cast<int>(gameRule)
        );
        return false;
    }
    if (worldIds.size() != userDatabaseIds.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"db.cpp",
        //     0x27e5,
        //     L"WO_ASSERT_EQUAL_RETURN")
        // Log.Add(
        //     LOG_WARN,
        //     L"[olympiad] World matching-point load not requested: %zu world IDs do not match %zu character IDs (game rule
        //     %d).", worldIds.size(), userDatabaseIds.size(), static_cast<int>(gameRule))
        L2_LOG_WARN(
            Log,
            u"[olympiad] World matching-point load not requested: %zu world IDs do not match %zu character IDs (game rule "
            u"%d).",
            worldIds.size(), userDatabaseIds.size(), static_cast<int>(gameRule)
        );
        return false;
    }

    protocol::RQ_EX_WOLYMPIAD_MATCHING_POINT_LOAD packet;
    packet.cGameRule = static_cast<char>(gameRule);
    for (std::size_t index = 0; index < worldIds.size(); ++index) {
        protocol::PkWOlympiadUserKey key;
        key.nWorldID = runtime::CheckedVectorAt(worldIds, index);
        key.nUserDBID = runtime::CheckedVectorAt(userDatabaseIds, index);
        packet.vKeys.push_back(key);
    }
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1a9), packet);
    return true;
}

void CDB::RequestBuilderCommand(int userSid, const std::vector<std::u16string>& params) {
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_BUILDER_COMMAND packet;
    packet.nUserSid = userSid;
    for (const std::u16string& param : params) {
        packet.params.push_back(param);
    }
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1c5), packet);
}

void CDB::SetReservationAnnounce(protocol::RQ_EX_SET_RESERVATION_ANNOUNCE& packet) {
    L2_GUARD_FUNCTION();
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1cb), packet);
}

void CDB::LoadReservationAnnounce() {
    // Current L2Server 0x006675ac leaves packet.isBuilder
    // unwritten before the raw one-byte encoder at 0x009ce340.
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_LOAD_RESERVATION_ANNOUNCE packet;
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1c9), packet);
}

void CDB::DelReservationAnnounce(int announceNumber) {
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_DELETE_RESERVATION_ANNOUNCE packet;
    packet.announceNum = announceNumber;
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1ca), packet);
}

void CDB::ShowReservationAnnounce(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    User* const user = socket->GetUser();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Show ReservationAnnounce() failed. User is NULL",
        //     kDatabaseSourceFile,
        //     0x2950)
        // Log.Add(
        //     LOG_WARN,
        //     L"[announcements] Reservation-announcement list not requested: the socket has no bound character.")
        L2_LOG_WARN(Log, u"[announcements] Reservation-announcement list not requested: the socket has no bound character.");
        return;
    }
    protocol::RQ_EX_SHOW_RESERVATION_ANNOUNCE packet;
    packet.nUserServerId = static_cast<int>(user->GetServerId());
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x1cc), packet);
}

void CDB::RequestUpdateItemAuctionInfoForL2Assist(
    int auctionId,
    int currentItemType,
    std::int64_t currentInitPrice,
    std::int64_t currentItemAmount,
    int nextItemType,
    std::int64_t nextInitPrice,
    std::int64_t nextItemAmount,
    int nextAuctionDisplayStartTime
) {
    L2_GUARD_FUNCTION();
    protocol::RQ_EX_UPDATE_ITEM_AUCTION_INFO_FOR_L2ASSIST packet;
    packet.auctionId = auctionId;
    packet.currentItem.itemType = currentItemType;
    packet.currentItem.itemAmount = currentItemAmount;
    packet.currentItem.initPrice = currentInitPrice;
    packet.nextItem.itemType = nextItemType;
    packet.nextItem.itemAmount = nextItemAmount;
    packet.nextItem.initPrice = nextInitPrice;
    packet.nextAuctionDisplayStartTime = nextAuctionDisplayStartTime;
    SendPacket(g_nWorldId, static_cast<::RQ_PROTOCOL>(0xea), static_cast<::RQ_EX_PROTOCOL>(0x72), packet);
}

void CDB::NotifyFinishCheckVersion() {
    L2_GUARD_FUNCTION();
    m_pCheckCacheDSignal->NotifyFinish();
}

void CDB::RequestSaveCharExtraInfo(User* const user, const int socialClass, const double carryingWeightPercent) const {
    // Current L2Server 0x006694d8.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> attributePacket;
    CItem::AssembleAttribute(user->m_data->m_BaseAttribute, attributePacket);
    user->SendDB(
        "chdbbbcfd", 0xea, 0x118, static_cast<int>(user->GetDBID()), 0x80, user->m_data->m_dParam, 0x20,
        user->m_data->m_Stat.m_N, attributePacket.GetLength(), attributePacket.GetPacket(),
        static_cast<int>(static_cast<char>(socialClass)), carryingWeightPercent, user->m_data->m_nSubJobId
    );
}
