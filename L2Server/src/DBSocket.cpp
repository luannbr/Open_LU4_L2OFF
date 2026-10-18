#include "DBSocket.h"
#include "AccountJobFinishSignal.h"
#include "Agent.h"
#include "AgentGameGuard.h"
#include "AgitHtml.h"
#include "Alliance.h"
#include "AnnounceTimer.h"
#include "Area.h"
#include "AuctionAgit.h"
#include "Auth.h"
#include "BattleCamp.h"
#include "BuySell.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "CacheContextManager.h"
#include "Castle.h"
#include "ClanBbs.h"
#include "CommCli.h"
#include "ControlTower.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "DBPacketEx.h"
#include "DBSocketOp.h"
#include "Default.h"
#include "Door.h"
#include "EnchantOption.h"
#include "Friends.h"
#include "GMSetting.h"
#include "GameEventSystem.h"
#include "HtmlBuilder.h"
#include "Inspector.h"
#include "InstantZone.h"
#include "Inventory.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "L2Core/src/inc/Guard.h"
#include "L2Time.h"
#include "Manor.h"
#include "Memo.h"
#include "MonRaceData.h"
#include "NPC.h"
#include "NPSystem.h"
#include "NpcBattleAgit.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "OlympiadService.h"
#include "OlympiadServiceProxy.h"
#include "OlympiadStuff.h"
#include "PCParam.h"
#include "ParseHtml.h"
#include "ParserExpData.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "Pet.h"
#include "PetitionCli.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "PledgeJoinSystem.h"
#include "PledgeRaidNetwork.h"
#include "PledgeStuff.h"
#include "Portable/ClientProtocolRuntime.h"
#include "Portable/ProcessRuntime.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Residence.h"
#include "Setting.h"
#include "Shared/src/AtomicJob.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeCrest.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/proto/Stream.h"
#include "SiegeAgit.h"
#include "Skill.h"
#include "SkillAcquire.h"
#include "TeamBattleAgit.h"
#include "ThreadLocalWorldMap.h"
#include "Trade.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "WareHouse.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
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


int CurrentLastError() noexcept {
#if defined(_WIN32)
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

// Noise: An imported function address is not a connection ID. The updated socket diagnostics use the actual stored
// connection handle. int LegacySocketSymbolForLog() noexcept {
//     // The current instructions loads the imported `socket` function
//     // address for this diagnostic's first %d, instead of m_socket.
//     return static_cast<int>(
//         reinterpret_cast<std::intptr_t>(&::socket))
// }

CDBCallback* MainPacketTable() noexcept { return gPacketTableFromCache.GetData(); }

void StoreLittleEndian16(std::uint8_t* destination, std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8U);
}


void CrashCacheVersionBranch(
    const wchar_t* const guardName,
    const CrashInfo::ReasonType reason,
    const std::source_location location = std::source_location::current()
) {
    GuardHelper failureGuard(guardName, CThreadManager::GetThreadID());
    ForceCrash(reason, location);
}

// Current L2Server .rdata 0x00f77210. The historical shared header emits a
// translation-unit-local copy used by the bought-ammunition auto-equip path.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

#if defined(__clang__) || defined(__GNUC__)
#define L2_USED_INTERNAL_DB_CALLBACK __attribute__((used))
#else
#define L2_USED_INTERNAL_DB_CALLBACK
#endif

// Current L2Server 0x006a5c5c, original static DBSocket.cpp owner.
L2_USED_INTERNAL_DB_CALLBACK bool ReplyExPacket(CDBSocket* const socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::uint16_t extendedProtocol = 0;
    packet = ::Disassemble(packet, "h", &extendedProtocol);

    CDBCallback callback = &DummyPacket;
    if (extendedProtocol < CDBExCallbackMap::SIZE) {
        callback = g_DBPacketTableEx.GetCallback(extendedProtocol);
    }
    if (callback == &DummyPacket) {
        // Log.Add(extendedProtocol < CDBExCallbackMap::SIZE ? LOG_WARN : LOG_ERROR,
        //     "[database protocol] Ignoring extended reply %u: %s (supported IDs 0..307).",
        //     static_cast<unsigned int>(extendedProtocol),
        //     extendedProtocol < CDBExCallbackMap::SIZE ? "current handler performs no work" : "protocol ID is outside the
        //     supported range")
        L2_LOG(
            Log, extendedProtocol < CDBExCallbackMap::SIZE ? LOG_WARN : LOG_ERROR,
            "[database protocol] Ignoring extended reply %u: %s (supported IDs 0..307).",
            static_cast<unsigned int>(extendedProtocol),
            extendedProtocol < CDBExCallbackMap::SIZE ? "current handler performs no work"
                                                      : "protocol ID is outside the supported range"
        );
    }
    return callback(socket, packet);
}

#undef L2_USED_INTERNAL_DB_CALLBACK

} // namespace

// Original DBSocket.cpp-local owner. TotalChecker2015.pdb fixes the exact
// two-field 0x10-byte layout and access. Current Map is at 0x006a2f58.
class UserItemDurationMapper {
public:
    explicit UserItemDurationMapper(User* const user) : m_user(user), m_equippedDurationItem(false) {}
    ~UserItemDurationMapper() = default;

    void Map(const unsigned int itemDatabaseId, const int duration) {
        L2_GUARD_FUNCTION();

        CItem* const item = m_user->m_inv.GetByDBID(itemDatabaseId);
        if (item == nullptr || !item->IsDuration()) {
            return;
        }

        item->SetDuration(duration);
        if (m_user->HasEquipped(item)) {
            item->SetDurationCommitTick(static_cast<int>(m_user->m_nLastTick));
            item->DecreaseDurationOneUnit();
            m_user->_AddEquipDurationItem(item);
            m_user->SaveDurationItem(item);
        }
    }

private:
    User* m_user; // +0x00
    [[maybe_unused]] bool m_equippedDurationItem; // +0x08
};


bool DummyPacket(CDBSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    // Noise: Primary and extended reply dispatchers now report the protocol and no-op outcome. This generic error duplicated
    // that information and also marked intentional no-op registrations as ERROR.
    // ::LogIOError(
    //     "DummyPacket called in DBSocket or DBSocketEx")
    return false;
}

bool CacheVersionPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server.exe 0x006a3074 (1380 bytes), SHA-256
    // ac165836f034ed015568fbfe08af88ca5bb6c8c354061d18a6e3b2209db7400d.
    L2_GUARD_FUNCTION();

    std::int32_t result = -1;
    std::int32_t cachedItemCount = 0;
    packet = ::Disassemble(packet, "dd", &result, &cachedItemCount);

    if (result == 1) {
        std::array<char, 80> revisionTag{};
        std::int32_t revision = 0;
#if defined(_MSC_VER)
        ::sscanf_s("$Revision: #373 $", "%s #%d", revisionTag.data(), static_cast<unsigned>(revisionTag.size()), &revision);
#else
        std::sscanf("$Revision: #373 $", "%79s #%d", revisionTag.data(), &revision);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cache version is different: %d",
        //     revision)
        // L2_LOG_ERROR(Log,
        //     L"Cache version is different: %d",
        //     revision)
        L2_LOG_ERROR(
            Log, u"[database connection] CacheD rejected protocol revision %d (result 1); L2Server is terminating.", revision
        );
        CrashCacheVersionBranch(L"Cache version is different", CrashInfo::CACHE_VERSION_IS_DIFFERENT);
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(L"Cache version is different")
#if defined(_WIN32)
        PopUpWindow(L"Cache version is different");
#endif
        return true;
    }

    if (result == 2) {
        L2_LOG_ERROR(
            Log,
            u"[database connection] CacheD rejected the connection because character logout from the previous connection is "
            u"still pending "
            u"(result 2); terminating L2Server."
        );
        CrashCacheVersionBranch(L"FAILED_SAVING_DATA_AFTER_CRASH", CrashInfo::CACHE_VERSION_FAILED);
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(L"Cache is saving data after crash.")
#if defined(_WIN32)
        PopUpWindow(L"Cache is saving data after crash.");
#endif
        return true;
    }

    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cache Version Failed: %d",
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"Cache Version Failed: %d",
        //     result)
        L2_LOG_ERROR(
            Log, u"[database connection] CacheD version handshake failed with result %d; L2Server is terminating.", result
        );
        CrashCacheVersionBranch(L"Cache Version Failed", CrashInfo::CACHE_VERSION_FAILED);
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(
        //     L"Cache Version Failed: %d",
        //     result)
#if defined(_WIN32)
        PopUpWindow(L"Cache Version Failed: %d", result);
#endif
        return true;
    }

    if (cachedItemCount != g_ObjectDB.m_nRegisteredItemCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"%s is not updated: %d, %d",
        //     L"..\\Script\\Itemdata.txt",
        //     cachedItemCount,
        //     g_ObjectDB.m_nRegisteredItemCount)
        // L2_LOG_ERROR(Log,
        //     L"%s is not updated: %d, %d",
        //     L"..\\Script\\Itemdata.txt",
        //     cachedItemCount,
        //     g_ObjectDB.m_nRegisteredItemCount)
        L2_LOG_ERROR(
            Log,
            u"[database connection] Item count mismatch: L2Server loaded %d items, CacheD loaded %d. "
            u"Use matching Itemdata.txt files in the configured scripts directories; terminating L2Server.",
            g_ObjectDB.m_nRegisteredItemCount, cachedItemCount
        );
        CrashCacheVersionBranch(L"CacheDScript: itemdata.txt is not updated", CrashInfo::CACHEDSCRIPT_IS_NOT_UPDATED);
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(
        //     L"CacheDScript is not updated. Copy the %s to the "
        //     L"CacheDScript folder. Server[%d] CacheD[%d]",
        //     L"..\\Script\\Itemdata.txt",
        //     g_ObjectDB.m_nRegisteredItemCount,
        //     cachedItemCount)
#if defined(_WIN32)
        PopUpWindow(
            L"CacheDScript is not updated. Copy the %s to the "
            L"CacheDScript folder. Server[%d] CacheD[%d]",
            L"..\\Script\\Itemdata.txt", g_ObjectDB.m_nRegisteredItemCount, cachedItemCount
        );
#endif
        return true;
    }

    if (!ScriptFileUtil::CacheToServer::CheckFileInfo(packet)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CacheDScript is not updated.")
        // L2_LOG_ERROR(Log,
        //     L"CacheDScript is not updated.")
        L2_LOG_ERROR(
            Log,
            u"[database connection] Script size comparison failed; L2Server is terminating. "
            u"The preceding script error identifies the mismatched file."
        );
        CrashCacheVersionBranch(L"CacheDScript is not updated", CrashInfo::CACHEDSCRIPT_IS_NOT_UPDATED);
        // Noise: The error is recorded in the main log. Retain only the Windows modal copy.
        // PopUpWindow(
        //     L"CacheDScript is not updated. Copy the [%s][%s][%s][%s] "
        //     L"to the CacheDScript folder.",
        //     L"npc_pch.txt",
        //     L"manual_pch.txt",
        //     L"category_pch.txt",
        //     L"..\\Script\\categorydata.txt")
#if defined(_WIN32)
        PopUpWindow(
            L"CacheDScript is not updated. Copy the [%s][%s][%s][%s] "
            L"to the CacheDScript folder.",
            L"npc_pch.txt", L"manual_pch.txt", L"category_pch.txt", L"..\\Script\\categorydata.txt"
        );
#endif
        return true;
    }

    db.NotifyFinishCheckVersion();
    return false;
}

bool ReplyLoadPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a3718.
    L2_GUARD_FUNCTION();

    std::array<SharedUtilsWideChar, 28> pledgeName{};
    std::array<SharedUtilsWideChar, 28> masterName{};
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t masterDatabaseId = 0;
    std::int32_t memberCount = 0;
    packet = ::Disassemble(
        packet, "SddSd", 0x32, pledgeName.data(), &pledgeDatabaseId, &masterDatabaseId, 0x32, masterName.data(), &memberCount
    );

    const auto toHostWide = [](const auto& value) {
        std::u16string target;
        target.reserve(value.size());
        for (const SharedUtilsWideChar codeUnit : value) {
            if (codeUnit == SharedUtilsWideChar{}) {
                break;
            }
            target.push_back(static_cast<char16_t>(codeUnit));
        }
        return ::Utf16ToWide(target);
    };
    std::wstring hostPledgeName = toHostWide(pledgeName);
    std::wstring hostMasterName = toHostWide(masterName);
    CPledge::LoadPledge(
        hostPledgeName.data(), pledgeDatabaseId, masterDatabaseId, hostMasterName.data(), memberCount, packet
    );
    return false;
}

bool ReplyDeletePledgeByTimer(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a3894.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddd", &pledgeServerId, &pledgeDatabaseId, &result);

    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x1d17,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, u"[database reply] Timed clan deletion references missing clan runtime ID %u; reply ignored.",
            pledgeServerId
        );
    } else if (result == 1) {
        pledge->Dismiss();
        g_PledgeTimer.CancelDismiss(static_cast<std::uint32_t>(pledgeDatabaseId));
        commcli.RequestClanBBSDelete(pledgeDatabaseId, pledge->GetWorldId());
    }
    return false;
}

bool ReplyLoadPledgeWar(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a39bc.
    L2_GUARD_FUNCTION();

    std::int32_t rowCount = 0;
    packet = ::Disassemble(packet, "d", &rowCount);
    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::uint32_t pledgeServerId = 0;
        std::uint32_t enemyPledgeServerId = 0;
        std::uint32_t declaredTime = 0;
        std::uint32_t warPoint = 0;
        std::uint32_t previousWarPoint = 0;
        std::uint32_t deathCount = 0;
        packet = ::Disassemble(
            packet, "dddddd", &pledgeServerId, &enemyPledgeServerId, &declaredTime, &warPoint, &previousWarPoint, &deathCount
        );
        CPledge::ReserveWarDeclare(
            pledgeServerId, enemyPledgeServerId, declaredTime, warPoint, previousWarPoint, deathCount
        );
        const int worldId = runtime::process::CurrentDatabaseId();
        CPledge::ReserveLoaded(worldId, pledgeServerId, PRLT_PLEDGEWAR);
        CPledge::ReserveLoaded(worldId, enemyPledgeServerId, PRLT_PLEDGEWAR);
        if (g_JobFinishSignal.SetWarLoaded()) {
            CPledge::SetFlagReservePledge(PRLT_PLEDGEWAR);
        }
    }
    if (rowCount == 0) {
        (void)g_JobFinishSignal.SetWarLoaded();
        CPledge::SetFlagReservePledge(PRLT_PLEDGEWAR);
    }
    return false;
}

bool ReplyCreatePledgeByNpc(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a61f4.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t npcServerId = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::array<SharedUtilsWideChar, 25> pledgeName{};
    std::int32_t worldId = 0;
    ::Disassemble(packet, "dddSd", &userServerId, &npcServerId, &pledgeDatabaseId, 0x32, pledgeName.data(), &worldId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x694,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[clan] Create-by-NPC reply references missing user runtime ID %u; reply ignored.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CNPC* npc = nullptr;
    if (npcServerId != 0) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(npcServerId);
        npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid npc[%x]",
            //     L"DBSocket.cpp",
            //     0x69c,
            //     npcServerId)
            L2_LOG_ERROR(
                Log, u"[clan] Create-by-NPC reply references missing NPC runtime ID %u; reply ignored.", npcServerId
            );
            return false;
        }
    }

    if (pledgeDatabaseId < 1) {
        socket->SendSystemMessage(SystemMessageId::NAME_CANNOT_BE_USED);
        if (npc != nullptr) {
            g_npcserver.Send("cdddd", 0x21, user->GetServerId(), user->m_data->index, npc->m_data->index, 0);
        }
        return false;
    }

    std::u16string targetPledgeName;
    targetPledgeName.reserve(pledgeName.size());
    for (const SharedUtilsWideChar codeUnit : pledgeName) {
        if (codeUnit == SharedUtilsWideChar{}) {
            break;
        }
        targetPledgeName.push_back(static_cast<char16_t>(codeUnit));
    }
    std::wstring hostPledgeName = ::Utf16ToWide(targetPledgeName);
    CPledge* const pledge = new CPledge(
        pledgeDatabaseId, static_cast<int>(user->GetDBID()), hostPledgeName.data(), Utf16ToWide(user->GetName()).data(),
        worldId
    );
    if (pledge == nullptr) {
        return false;
    }

    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nPledgeType, MASTER_PLEDGE, 0x34);
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nGradeId, 6, 0x46);
    db.RequestSetPledgePowerGrade(user->GetDBID(), user->m_data->m_nGradeId);
    pledge->SetMaster(user);

    const char16_t* const nickname = user->m_sNickName;
    pledge->AddDbMember(
        user->GetDBID(), user->GetName(), nickname, user->m_data->m_nLevel, user->m_data->m_nClass,
        static_cast<int>(user->m_data->m_Sex), static_cast<int>(user->m_data->m_Race), user->GetServerId(), MASTER_PLEDGE,
        user->m_data->m_nGradeId, user->tPledgeBonusTimeStamp[0]
    );
    pledge->AddMember(user);
    pledge->ValidateTotalLevel();

    CSPointer<CPledge> pledgePointer(pledge, ObjectRefType::FunctionChain);
    CPledge::RegisterPledge(pledgePointer);
    user->SetPledge(pledge);
    user->ParamChanged(std::int64_t{PCB_PLEDGE | PCB_USER});
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
    pledge->OpenAllWindowPacket(user, false, nullptr);
    L2_RELEASE(pledge, ObjectRefType::CreateDelete);

    if (npc != nullptr) {
        g_npcserver.Send("cdddd", 0x21, user->GetServerId(), user->m_data->index, npc->m_data->index, 1);
    }
    pledge->SendPledgeOnlineMemberCount();
    if (CUserSocket* const currentSocket = user->GetSocket(); currentSocket != nullptr) {
        currentSocket->Send("chddd", 0xfe, 0x17d, 2, pledge->GetDbId(), pledge->GetWorldId());
    }
    CPledgeJoinSystem::Inst()->ClearUserListByPledgeJoin(user);
    user->tPledgeBonusTimeStamp[0] = runtime::crt::Time32Now();
    for (int bonusType = 0; bonusType < 2; ++bonusType) {
        pledge->m_PledgeBonus.m_PledgeBonus[bonusType].m_nMaxBonusPoint =
            CPledgeBonusSetting::Inst().GetGoalRate(bonusType, 0);
    }

    // Original: L"정예혈맹원"
    std::wstring elitePledgeName = L"Elite Members";
    pledge->SetSubPledge(SUB_1, elitePledgeName.data(), 0, false);
    PledgeStuff::SetDefaultPledgePower(pledge, 6);
    PledgeStuff::SetDefaultPledgePower(pledge, 7);
    pledge->m_pFunction->UpdateFunction(static_cast<int>(pledge->GetPledgeLevel()), pledge->m_pMastery);
    user->PledgeMissionLoadingCompleted();
    return false;
}

bool ReplyMonRace(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a5014.
    L2_GUARD_FUNCTION();
    std::int32_t type = 0;
    packet = ::Disassemble(packet, "d", &type);
    CMonRacePacket reply(type);
    reply.DoReply(packet);
    return false;
}

bool ReplyModPledgeNamePacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aae78.
    L2_GUARD_FUNCTION();

    std::int32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    std::array<SharedUtilsWideChar, 28> pledgeName{};
    ::Disassemble(packet, "ddS", &pledgeDatabaseId, &result, 0x32, pledgeName.data());

    std::u16string targetName;
    targetName.reserve(pledgeName.size());
    for (const SharedUtilsWideChar codeUnit : pledgeName) {
        if (codeUnit == SharedUtilsWideChar{}) {
            break;
        }
        targetName.push_back(static_cast<char16_t>(codeUnit));
    }
    std::wstring hostPledgeName = ::Utf16ToWide(targetName);

    if (result == 1) {
        db.RequestModifyPledgeName(pledgeDatabaseId, targetName.c_str());
        return false;
    }
    if (result != 2) {
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeDatabaseId);
    if (!pledge) {
        commcli.RequestSetPledgeInfo(worldId, pledgeDatabaseId, hostPledgeName.data(), L"");
        return false;
    }

    // Current performs both lowercase conversions but then updates the
    // object before unregistering it. Preserve that observable odd order.
    std::array<SharedUtilsWideChar, 28> oldLowerName{};
    std::array<SharedUtilsWideChar, 28> newLowerName{};
#if defined(_WIN32)
    ::ToLowerCase(reinterpret_cast<const SharedUtilsWideChar*>(pledge->GetPledgeName()), oldLowerName.data());
#else
    const std::u16string oldTargetName = std::u16string(pledge->GetPledgeName());
    ::ToLowerCase(oldTargetName.c_str(), oldLowerName.data());
#endif
    ::ToLowerCase(pledgeName.data(), newLowerName.data());
    (void)oldLowerName;
    (void)newLowerName;

    pledge->SetPledgeName(hostPledgeName.data());
    CPledge::UnregisterPledge(pledge);
    CPledge::RegisterPledge(pledge);
    pledge->UpdateInfoWindowPacket();
    pledge->UpdatePledgeInfoPacket();
    commcli.RequestSetPledgeInfo(
        worldId, pledgeDatabaseId, hostPledgeName.data(), Utf16ToWide(pledge->GetMasterName()).c_str()
    );
    CPledgeJoinSystem::Inst()->RequestModifyPledgeInfo(pledge.get());
    return false;
}

bool ReplyOustPledgeMemberPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a796c.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t pledgeServerId = 0;
    std::int32_t ignoredPledgeType = 0;
    std::uint32_t characterDatabaseId = 0;
    char16_t characterName[25]{};
    ::Disassemble(
        packet, "ddddS", &userServerId, &pledgeServerId, &ignoredPledgeType, &characterDatabaseId, 0x32, characterName
    );
    (void)ignoredPledgeType;

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1262,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[clan] Member-removal reply references missing user runtime ID %u; reply ignored.", userServerId
        );
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x1263,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, u"[clan] Member-removal reply references missing clan runtime ID %u; reply ignored.", pledgeServerId
        );
        return false;
    }

    if (characterDatabaseId == 0) {
        socket->SendSystemMessage(SystemMessageId::NOT_A_MEMBER_OF_THIS_CLAN);
        socket->Send("c", 0x35);
    } else {
        if (pledge->IsSubMaster(characterDatabaseId)) {
            const PledgeType pledgeType = pledge->GetSubMasterPledgeType(characterDatabaseId);
            pledge->SendSubPledgeUpdatedPacket(pledgeType);
        }
        const std::wstring hostCharacterName = ::FixedUtf16ToWide(characterName);
        pledge->Oust(characterDatabaseId, hostCharacterName.c_str());
        socket->Send("c", 0x35);
    }
    pledge->EndTransaction();
    return false;
}

namespace DBPacketHandler {

bool ReplyUpdateUserPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a8944.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t pledgeServerId = 0;
    std::int32_t pledgeTypeValue = 0;
    std::int32_t updateType = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddddd", &userServerId, &pledgeServerId, &pledgeTypeValue, &updateType, &result);

    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x1c7b,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, u"[clan] Membership-update reply references missing clan runtime ID %u; reply ignored.", pledgeServerId
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1c80,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[clan] Membership-update reply references missing user runtime ID %u; clan transaction is ending.",
            userServerId
        );
        pledge->EndTransaction();
        return false;
    }

    CUserSocket* const originalSocket = user->GetSocket();
    if (result == 1) {
        if (updateType == 1) {
            const std::int32_t adjustedTime = runtime::crt::Time32Now() - 0x5b68;
            const std::tm* const localTime = runtime::crt::LocalTime32(adjustedTime);
            const std::int32_t updateTime =
                ((adjustedTime - (localTime->tm_hour * 60 + localTime->tm_min) * 60) - localTime->tm_sec) + 0x1ace7;
            user->tPledgeBonusTimeStamp[0] = -updateTime;
            user->tPledgeBonusTimeStamp[1] = updateTime;
            user->tPledgeBonusTimeStamp[2] = updateTime;
            db.RequestUpdateUserPledgeBonusReward(user, 3, updateTime);

            user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nPledgeType, pledgeTypeValue, 0x34);
            const int gradeId = CPledge::GetDefaultPowerGrade(static_cast<PledgeType>(pledgeTypeValue));
            user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nGradeId, gradeId, 0x46);
            db.RequestSetPledgePowerGrade(user->GetDBID(), user->m_data->m_nGradeId);
            pledge->Join(user);
            user->ParamChanged(std::int64_t{PCB_USER});
            CPledgeJoinSystem::Inst()->ClearUserListByPledgeJoin(user);

            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->Send("chddd", 0xfe, 0x17d, 3, pledge->GetDbId(), pledge->GetWorldId());
            }
        } else if (updateType == 2) {
            if (pledge->IsSubMaster(user->GetDBID())) {
                const PledgeType pledgeType = pledge->GetSubMasterPledgeType(user->GetDBID());
                pledge->UpdateSubPledgeMaster(pledgeType, 0);
                pledge->SendSubPledgeUpdatedPacket(pledgeType);
            }
            pledge->Withdraw(user);
            originalSocket->Send("c", 0x34);
            user->ParamChanged(std::int64_t{PCB_USER});
            user->tPledgeBonusTimeStamp[0] = 0;

            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->Send("chddd", 0xfe, 0x17d, 4, pledge->GetDbId(), pledge->GetWorldId());
            }
        }
    } else {
        originalSocket->SendSystemMessage(SystemMessageId::OPERATION_NOT_PROCESSED_TRY_AGAIN);
        if (updateType == 1) {
            originalSocket->Send("c", 0x2d);
        } else if (updateType == 2) {
            originalSocket->Send("c", 0x34);
        }
    }

    if (updateType == 1) {
        pledge->SetPledgeWorkDone();
    }
    pledge->EndTransaction();
    return false;
}

} // namespace DBPacketHandler

bool ReplyDeletePledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a8dac.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t pledgeServerId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddd", &userServerId, &pledgeServerId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1cf4,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[clan] Clan-deletion reply references missing user runtime ID %u; reply ignored.", userServerId);
        return false;
    }

    (void)user->GetSocket();
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x1cf5,
        //     pledgeServerId)
        L2_LOG_ERROR(
            Log, u"[clan] Clan-deletion reply references missing clan runtime ID %u; reply ignored.", pledgeServerId
        );
        return false;
    }
    if (result == 1) {
        pledge->Dismiss();
        user->TakeDismissPenalty();
        const double experience = user->GetDefaultExpDownValue(user->m_data->m_nLevel);
        user->ExpDown(static_cast<std::int64_t>(experience));
    }
    return false;
}

namespace {

struct LoadCharacterRecord {
    std::array<char16_t, 24> characterName{};
    std::int32_t characterId = 0;
    std::array<char16_t, 15> accountName{};
    std::int32_t accountId = 0;
    std::int32_t pledgeId = 0;
    std::int32_t builder = 0;
    std::int32_t gender = 0;
    std::int32_t race = 0;
    std::int32_t mainClass = 0;
    std::int32_t worldId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    std::int32_t level = 0;
    std::int32_t alignment = 0;
    std::int32_t pkCount = 0;
    std::int32_t duelCount = 0;
    std::int32_t pkPardon = 0;
    std::array<std::int32_t, 60> itemTypes{};
    std::array<std::int32_t, 9> appearances{};
    std::array<std::uint16_t, 5> armorEnchants{};
    std::int32_t face = 0;
    std::int32_t hairShape = 0;
    std::int32_t hairColor = 0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    std::int32_t deletionAndBan = 0;
    std::int32_t currentClass = 0;
    std::int32_t latestCharacter = 0;
    std::int32_t weaponEnchant = 0;
    std::int32_t variationOption1 = 0;
    std::int32_t variationOption2 = 0;
    std::int32_t previousServer = 0;
    std::array<char16_t, 24> originName{};
    std::int32_t vitality = 0;
    std::int32_t jobGroup = -1;
    std::int32_t vitalityRestoreCount = 0;
    std::int32_t freeToPlayState = 0;
    std::int32_t faceEx = 0;
    std::int32_t hairShapeEx = 0;
    std::int32_t hairColorEx = 0;
    std::int32_t noblessType = 0;
    std::int32_t hero = 0;
    std::uint8_t hairAccessoryFlag = 0;
};

const std::uint8_t* DisassembleLoadCharacterRecord(const std::uint8_t* packet, LoadCharacterRecord& record) {
    constexpr const char* format = "SdSddddddddddffQQddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "dddhhhhhdddffdddcdddSdddddddddc";
    return ::Disassemble(
        packet, format, 0x30, record.characterName.data(), &record.characterId, 0x1e, record.accountName.data(),
        &record.accountId, &record.pledgeId, &record.builder, &record.gender, &record.race, &record.mainClass,
        &record.worldId, &record.x, &record.y, &record.z, &record.hp, &record.mp, &record.sp, &record.experience,
        &record.level, &record.alignment, &record.pkCount, &record.duelCount, &record.pkPardon, &record.itemTypes[0],
        &record.itemTypes[1], &record.itemTypes[2], &record.itemTypes[3], &record.itemTypes[4], &record.itemTypes[5],
        &record.itemTypes[6], &record.itemTypes[7], &record.itemTypes[8], &record.itemTypes[9], &record.itemTypes[10],
        &record.itemTypes[11], &record.itemTypes[12], &record.itemTypes[13], &record.itemTypes[14], &record.itemTypes[15],
        &record.itemTypes[16], &record.itemTypes[17], &record.itemTypes[18], &record.itemTypes[19], &record.itemTypes[20],
        &record.itemTypes[21], &record.itemTypes[22], &record.itemTypes[23], &record.itemTypes[24], &record.itemTypes[25],
        &record.itemTypes[26], &record.itemTypes[27], &record.itemTypes[28], &record.itemTypes[29], &record.itemTypes[30],
        &record.itemTypes[31], &record.itemTypes[32], &record.itemTypes[33], &record.itemTypes[34], &record.itemTypes[35],
        &record.itemTypes[36], &record.itemTypes[37], &record.itemTypes[38], &record.itemTypes[39], &record.itemTypes[40],
        &record.itemTypes[41], &record.itemTypes[42], &record.itemTypes[43], &record.itemTypes[44], &record.itemTypes[45],
        &record.itemTypes[46], &record.itemTypes[47], &record.itemTypes[48], &record.itemTypes[49], &record.itemTypes[50],
        &record.itemTypes[51], &record.itemTypes[52], &record.itemTypes[53], &record.itemTypes[54], &record.itemTypes[55],
        &record.itemTypes[56], &record.itemTypes[57], &record.itemTypes[58], &record.itemTypes[59], &record.appearances[0],
        &record.appearances[1], &record.appearances[2], &record.appearances[3], &record.appearances[4],
        &record.appearances[5], &record.appearances[6], &record.appearances[7], &record.appearances[8],
        &record.armorEnchants[0], &record.armorEnchants[1], &record.armorEnchants[2], &record.armorEnchants[3],
        &record.armorEnchants[4], &record.face, &record.hairShape, &record.hairColor, &record.maxHp, &record.maxMp,
        &record.deletionAndBan, &record.currentClass, &record.latestCharacter, &record.weaponEnchant,
        &record.variationOption1, &record.variationOption2, &record.previousServer, 0x30, record.originName.data(),
        &record.vitality, &record.jobGroup, &record.vitalityRestoreCount, &record.freeToPlayState, &record.faceEx,
        &record.hairShapeEx, &record.hairColorEx, &record.noblessType, &record.hero, &record.hairAccessoryFlag
    );
}

char16_t FoldAccountCodeUnit(const char16_t value) noexcept {
    if (value >= u'A' && value <= u'Z') {
        return static_cast<char16_t>(value + (u'a' - u'A'));
    }
    return value;
}

int CompareAccountNames(const char16_t* left, const char16_t* right) noexcept {
    for (;;) {
        const char16_t foldedLeft = FoldAccountCodeUnit(*left++);
        const char16_t foldedRight = FoldAccountCodeUnit(*right++);
        if (foldedLeft != foldedRight || foldedLeft == u'\0') {
            return static_cast<int>(foldedLeft) - static_cast<int>(foldedRight);
        }
    }
}

template <std::size_t Extent>
std::wstring TargetTextForLog(const std::array<char16_t, Extent>& value) {
    const auto end = std::find(value.begin(), value.end(), u'\0');
    return ::Utf16ToWide(std::u16string_view(value.data(), static_cast<std::size_t>(end - value.begin())));
}

std::wstring TargetTextForLog(const SharedUtilsWideChar* value, const std::size_t capacity) {
    const char16_t* const target = reinterpret_cast<const char16_t*>(value);
    std::size_t length = 0;
    while (length < capacity && target[length] != u'\0') {
        ++length;
    }
    return ::Utf16ToWide(std::u16string_view(target, length));
}

void CopyOriginName(std::array<char16_t, 24>& destination, const std::array<char16_t, 24>& source) noexcept {
    const auto terminator = std::find(source.begin(), source.end(), u'\0');
    if (terminator == source.end()) {
        destination[0] = u'\0';
        return;
    }
    std::copy(source.begin(), terminator + 1, destination.begin());
}

int AssembleCharacterListRecord(
    char* buffer,
    const int capacity,
    const LoadCharacterRecord& record,
    const int basicClass,
    const double levelPercent,
    const int transformId,
    const int vitalityBonus,
    const int vitalityItemUsableCount,
    const int hero
) {
    int face = record.face;
    // Current 0x006d115d..0x006d1189 gates the first beauty lane too.
    if (!(record.hairAccessoryFlag != 0 && (record.itemTypes[15] != 0 || record.itemTypes[16] != 0)) && record.faceEx != 0) {
        face = record.faceEx;
    }

    int hairShape = record.hairShape;
    if (!(record.hairAccessoryFlag != 0 && (record.itemTypes[15] != 0 || record.itemTypes[16] != 0)) &&
        record.hairShapeEx != 0) {
        hairShape = record.hairShapeEx;
    }

    int hairColor = record.hairColor;
    if (record.hairColorEx != 0) {
        hairColor = record.hairColorEx;
    }

    constexpr const char* format = "SdSddddddddddffQQfdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "ddddddddddddhhhhhdddffdddcdddddddffddddccc";
    return ::Assemble(
        buffer, capacity, format, record.characterName.data(), record.characterId, record.accountName.data(),
        record.accountId, 0, record.builder, record.gender, record.race, basicClass, record.worldId, record.x, record.y,
        record.z, record.hp, record.mp, record.sp, record.experience, levelPercent, record.level, record.alignment,
        record.pkCount, record.duelCount, record.pkPardon, 0, 0, 0, 0, 0, 0, 0, 0, record.itemTypes[0], record.itemTypes[1],
        record.itemTypes[2], record.itemTypes[3], record.itemTypes[4], record.itemTypes[5], record.itemTypes[6],
        record.itemTypes[7], record.itemTypes[8], record.itemTypes[9], record.itemTypes[10], record.itemTypes[11],
        record.itemTypes[12], record.itemTypes[13], record.itemTypes[14], record.itemTypes[15], record.itemTypes[16],
        record.itemTypes[17], record.itemTypes[18], record.itemTypes[19], record.itemTypes[20], record.itemTypes[21],
        record.itemTypes[22], record.itemTypes[23], record.itemTypes[24], record.itemTypes[25], record.itemTypes[26],
        record.itemTypes[27], record.itemTypes[28], record.itemTypes[29], record.itemTypes[30], record.itemTypes[31],
        record.itemTypes[32], record.itemTypes[33], record.itemTypes[34], record.itemTypes[35], record.itemTypes[36],
        record.itemTypes[37], record.itemTypes[38], record.itemTypes[39], record.itemTypes[40], record.itemTypes[41],
        record.itemTypes[42], record.itemTypes[43], record.itemTypes[44], record.itemTypes[45], record.itemTypes[46],
        record.itemTypes[47], record.itemTypes[48], record.itemTypes[49], record.itemTypes[50], record.itemTypes[51],
        record.itemTypes[52], record.itemTypes[53], record.itemTypes[54], record.itemTypes[55], record.itemTypes[56],
        record.itemTypes[57], record.itemTypes[58], record.itemTypes[59], record.appearances[0], record.appearances[1],
        record.appearances[2], record.appearances[3], record.appearances[4], record.appearances[5], record.appearances[6],
        record.appearances[7], record.appearances[8], record.armorEnchants[0], record.armorEnchants[1],
        record.armorEnchants[2], record.armorEnchants[3], record.armorEnchants[4], face, hairShape, hairColor, record.maxHp,
        record.maxMp, record.deletionAndBan, record.currentClass, record.latestCharacter, record.weaponEnchant,
        record.variationOption1, record.variationOption2, transformId, 0, 0, 0, 0, 0.0, 0.0, record.vitality, vitalityBonus,
        vitalityItemUsableCount, record.freeToPlayState, record.noblessType, hero, record.hairAccessoryFlag
    );
}

int AssembleCharacterListRecordP152(
    char* buffer,
    const int capacity,
    const LoadCharacterRecord& record,
    const double levelPercent,
    const int vitalityBonus,
    const int vitalityItemUsableCount,
    const int hero
) {
    // Portable protocol extension only. The pinned current L2Server owns the
    // revision-162 serializer above. This layout follows the Etina's Fate
    // P152 client contract recorded by the pinned revision-152
    // compatibility audit. This is not evidence about the
    // shipping revision-162 serializer above.
    // CacheD lobby 0x0048963e..0x0048965e and selected-character reply
    // both publish +0x430/+0x42c/+0x428: style/color/face. The extended
    // lanes have the same semantic order. Match the common equipment gate.
    int face = record.hairColor;
    if (record.hairColorEx != 0) {
        face = record.hairColorEx;
    }

    int hairShape = record.face;
    if (!(record.hairAccessoryFlag != 0 && (record.itemTypes[15] != 0 || record.itemTypes[16] != 0)) && record.faceEx != 0) {
        hairShape = record.faceEx;
    }

    int hairColor = record.hairShape;
    if (!(record.hairAccessoryFlag != 0 && (record.itemTypes[15] != 0 || record.itemTypes[16] != 0)) &&
        record.hairShapeEx != 0) {
        hairColor = record.hairShapeEx;
    }

    int length = 0;
    const auto append = [&](const char* format, auto... arguments) {
        const int assembled = ::Assemble(buffer + length, capacity - length, format, arguments...);
        if (assembled <= 0) {
            return false;
        }
        length += assembled;
        return true;
    };

    // P152 needs zero clan and builder flags, the base class and server ID 1.
    // A P162 builder flag would make the lobby show a GM preview.
    // Keep these P152 values even when the CacheD record has other values.
    if (!append(
            "SdSddddddddddffQQf", record.characterName.data(), record.characterId, record.accountName.data(),
            record.accountId, 0, 0, record.gender, record.race, record.mainClass, 1, record.x, record.y, record.z, record.hp,
            record.mp, record.sp, record.experience, levelPercent
        )) {
        return 0;
    }

    if (!append("dddd", record.level, record.alignment, record.pkCount, record.duelCount)) {
        return 0;
    }
    for (int index = 0; index < 9; ++index) {
        if (!append("d", 0)) {
            return 0;
        }
    }

    std::array<std::int32_t, 60> protocol152ItemTypes = record.itemTypes;

    // P152 PAPERDOLL_ORDER publishes PAPERDOLL_RHAND at positions 7 and 14.
    // Its inventory also maps both SLOT_R_HAND and SLOT_LR_HAND to that one
    // paperdoll owner. Current CacheD instead carries rightHand and bothHand
    // separately at indices 7 and 14. Collapse them before duplicating the
    // selected weapon, exactly as the P152 client contract requires.
    const std::int32_t protocol152WeaponItemType = record.itemTypes[7] != 0 ? record.itemTypes[7] : record.itemTypes[14];
    protocol152ItemTypes[7] = protocol152WeaponItemType;
    protocol152ItemTypes[14] = protocol152WeaponItemType;

    for (const std::int32_t itemType : protocol152ItemTypes) {
        if (!append("d", itemType)) {
            return 0;
        }
    }

    // Exact NA 5.0.0.35 Engine.dll stores the nine wire values at User+0x4d4
    // and User::GetLookChangeItemSlot@0x203f6480 maps them as
    // [both-hand, right-hand, left-hand, gloves, chest, legs, feet, hair,
    // hair2]. Current CacheD publishes
    // [right-hand, left-hand, gloves, chest, legs, feet, both-hand, hair,
    // hair2]. The former donor projection duplicated the weapon at 0 and 6,
    // shifting every native visual slot between them.
    const std::int32_t protocol152WeaponAppearance =
        record.itemTypes[7] != 0 ? record.appearances[0] : record.appearances[6];
    const std::array<std::int32_t, 9> protocol152Appearances{
        protocol152WeaponAppearance,
        protocol152WeaponAppearance,
        record.appearances[1],
        record.appearances[2],
        record.appearances[3],
        record.appearances[4],
        record.appearances[5],
        record.appearances[7],
        record.appearances[8],
    };
    for (const std::int32_t appearance : protocol152Appearances) {
        if (!append("d", appearance)) {
            return 0;
        }
    }

    // P152 needs five zero armor-enchant fields and no lobby transform.
    // The next three fields are hair style, hair color and face.
    // They use the same order as the CacheD record.
    if (!append(
            "hhhhhdddffdddcdddddddffddddccc", 0, 0, 0, 0, 0, hairShape, hairColor, face, record.maxHp, record.maxMp,
            record.deletionAndBan, record.currentClass, record.latestCharacter, record.weaponEnchant,
            record.variationOption1, record.variationOption2, 0, 0, 0, 0, 0, 0.0, 0.0, record.vitality, vitalityBonus,
            vitalityItemUsableCount, record.freeToPlayState, record.noblessType, hero, record.hairAccessoryFlag
        )) {
        return 0;
    }

    return length;
}

void SendCharacterSelectedP152(CUserSocket& socket, User& user, const ClassType baseClass) {
    // Portable P152 compatibility extension. The current machine's
    // S_CHARACTER_SELECTED writer below is kept byte-for-byte in its 162
    // branch. The complete loaded NA 5.0.0.35 Engine.dll parser at
    // 0x2035c2c0 proves the P152 base format and the empty-inspector tail used
    // here. This supersedes the former Mobius-derived 16/28-byte description.
    CPacketHelperEx<20480> packet;
    const FVector position = user.m_data->GetPos();
    packet.Assemble(
        "cSdS", 0x0b, user.m_data->Name(),
        // Exact P152 handler 0x2035c2c0 decodes this DWORD into [ebp-0x78]
        // and never reads it again. Do not invent cross-packet identity
        // meaning for an ignored compatibility lane: keep the pinned
        // target writer's database-character identity here. S_USER_INFO and
        // later world packets continue to publish the runtime object id.
        static_cast<int>(user.GetDBID()), user.m_sNickName
    );
    packet.Assemble(
        "dddddddddd", static_cast<int>(user.GetAccountID()), user.m_data->m_nPledgeID, 0, user.m_data->m_Sex,
        user.m_data->m_Race,
        // The exact P152 handler copies this sixth DWORD to User+0x848.
        // User::GetMeshType@0x203fbd90 reads +0x848 as the original class.
        // publishing the current subclass can select the wrong lobby model.
        static_cast<int>(baseClass), 1, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
    );
    packet.Assemble(
        "ffQQddddd", user.m_data->m_dHP, user.m_data->m_dMP, user.m_data->m_nSP, user.m_data->m_nExp, user.m_data->m_nLevel,
        user.m_data->m_nKarma, user.m_data->m_nPKCount, g_L2Time.GetTime() % (24 * 60),
        0
    ); // inspector parameter count

    packet.Assemble("ddddd", user.m_data->m_nClass, 0, 0, 0, 0);
    constexpr std::array<std::uint8_t, 64> kSelectedCharacterState{};
    packet.Assemble("bd", static_cast<int>(kSelectedCharacterState.size()), kSelectedCharacterState.data(), 0);
    socket.Send("b", packet.GetLength(), packet.GetPacket());
}

struct LoadSelectedCharacterRecord {
    std::int32_t userSocketId = 0;
    std::array<char16_t, 24> characterName{};
    std::int32_t characterId = 0;
    std::array<char16_t, 15> accountName{};
    std::int32_t accountId = 0;
    std::int32_t pledgeId = 0;
    std::int32_t builderLevel = 0;
    std::int32_t sex = 0;
    std::int32_t race = 0;
    std::int32_t classType = 0;
    std::int32_t worldId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t inVehicle = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    std::int32_t level = 0;
    std::int32_t karma = 0;
    std::int32_t pkCount = 0;
    std::int32_t duelCount = 0;
    std::int32_t pkPardonCount = 0;
    std::array<std::int32_t, 60> itemDatabaseIds{};
    std::array<std::int32_t, 60> itemTypes{};
    std::int32_t face = 0;
    std::int32_t hairShape = 0;
    std::int32_t hairColor = 0;
    double maxHp = 0.0;
    double maxMp = 0.0;
    std::array<char16_t, 24> nickname{};
    std::int64_t dropExperience = 0;
    std::array<std::int32_t, 6> initialTime{};
    std::int32_t socialityArgument = 0;
    std::int32_t socialityCount = 0;
    _SYSTEMTIME lastSocialityCheck{};
    std::array<std::int32_t, 3> hennaIds{};
    std::int32_t subJobId = 0;
    std::array<std::int32_t, 4> subJobClasses{};
    std::int32_t dualClassId = 0;
    double cp = 0.0;
    double maxCp = 0.0;
    std::int32_t ssqDawnRound = 0;
    std::array<unsigned char, 0x20> opaqueCharacterData{};
    std::array<std::int32_t, 3> pledgePenaltyTimes{};
    std::int32_t characterNameColor = 0;
    std::int32_t nicknameColor = 0;
    std::int32_t pledgeType = 0;
    std::int32_t gradeId = 0;
    std::int32_t academyPledgeDatabaseId = 0;
    std::int32_t associatedInstantZoneId = 0;
    std::array<std::int32_t, 6> lastLogoutTime{};
    std::int32_t associatedInstantZoneParam = 0;
    std::int32_t bookmarkSlotCount = 0;
    std::int32_t voteBonusTimeLimit = 0;
    std::int32_t votePlayTime = 0;
    std::int32_t voteCount = 0;
    std::int32_t voteBonusCount = 0;
    std::int32_t votePlayTimeCount = 0;
    _SYSTEMTIME voteTime{};
    std::int32_t accountLastLogoutTime = 0;
    std::int32_t jobGroup = -1;
    std::int32_t faceEx = 0;
    std::int32_t hairShapeEx = 0;
    std::int32_t hairColorEx = 0;
    std::uint8_t hairAccessoryFlag = 0;
    std::int32_t abilityPoint = 0;
    CVitalData vitalityData;
};

const std::uint8_t* DisassembleLoadSelectedCharacterRecord(const std::uint8_t* packet, LoadSelectedCharacterRecord& record) {
    constexpr const char* format = "SdSdddddddddddffQQ"
                                   "ddddd"
                                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
                                   "dddffSQ"
                                   "ddddddddb"
                                   "dddddddddffd";
    packet = ::Disassemble(
        packet, format, 0x30, record.characterName.data(), &record.characterId, 0x1e, record.accountName.data(),
        &record.accountId, &record.pledgeId, &record.builderLevel, &record.sex, &record.race, &record.classType,
        &record.worldId, &record.x, &record.y, &record.z, &record.inVehicle, &record.hp, &record.mp, &record.sp,
        &record.experience, &record.level, &record.karma, &record.pkCount, &record.duelCount, &record.pkPardonCount,
        &record.itemDatabaseIds[0], &record.itemDatabaseIds[1], &record.itemDatabaseIds[2], &record.itemDatabaseIds[3],
        &record.itemDatabaseIds[4], &record.itemDatabaseIds[5], &record.itemDatabaseIds[6], &record.itemDatabaseIds[7],
        &record.itemDatabaseIds[8], &record.itemDatabaseIds[9], &record.itemDatabaseIds[10], &record.itemDatabaseIds[11],
        &record.itemDatabaseIds[12], &record.itemDatabaseIds[13], &record.itemDatabaseIds[14], &record.itemDatabaseIds[15],
        &record.itemDatabaseIds[16], &record.itemDatabaseIds[17], &record.itemDatabaseIds[18], &record.itemDatabaseIds[19],
        &record.itemDatabaseIds[20], &record.itemDatabaseIds[21], &record.itemDatabaseIds[22], &record.itemDatabaseIds[23],
        &record.itemDatabaseIds[24], &record.itemDatabaseIds[25], &record.itemDatabaseIds[26], &record.itemDatabaseIds[27],
        &record.itemDatabaseIds[28], &record.itemDatabaseIds[29], &record.itemDatabaseIds[30], &record.itemDatabaseIds[31],
        &record.itemDatabaseIds[32], &record.itemDatabaseIds[33], &record.itemDatabaseIds[34], &record.itemDatabaseIds[35],
        &record.itemDatabaseIds[36], &record.itemDatabaseIds[37], &record.itemDatabaseIds[38], &record.itemDatabaseIds[39],
        &record.itemDatabaseIds[40], &record.itemDatabaseIds[41], &record.itemDatabaseIds[42], &record.itemDatabaseIds[43],
        &record.itemDatabaseIds[44], &record.itemDatabaseIds[45], &record.itemDatabaseIds[46], &record.itemDatabaseIds[47],
        &record.itemDatabaseIds[48], &record.itemDatabaseIds[49], &record.itemDatabaseIds[50], &record.itemDatabaseIds[51],
        &record.itemDatabaseIds[52], &record.itemDatabaseIds[53], &record.itemDatabaseIds[54], &record.itemDatabaseIds[55],
        &record.itemDatabaseIds[56], &record.itemDatabaseIds[57], &record.itemDatabaseIds[58], &record.itemDatabaseIds[59],
        &record.itemTypes[0], &record.itemTypes[1], &record.itemTypes[2], &record.itemTypes[3], &record.itemTypes[4],
        &record.itemTypes[5], &record.itemTypes[6], &record.itemTypes[7], &record.itemTypes[8], &record.itemTypes[9],
        &record.itemTypes[10], &record.itemTypes[11], &record.itemTypes[12], &record.itemTypes[13], &record.itemTypes[14],
        &record.itemTypes[15], &record.itemTypes[16], &record.itemTypes[17], &record.itemTypes[18], &record.itemTypes[19],
        &record.itemTypes[20], &record.itemTypes[21], &record.itemTypes[22], &record.itemTypes[23], &record.itemTypes[24],
        &record.itemTypes[25], &record.itemTypes[26], &record.itemTypes[27], &record.itemTypes[28], &record.itemTypes[29],
        &record.itemTypes[30], &record.itemTypes[31], &record.itemTypes[32], &record.itemTypes[33], &record.itemTypes[34],
        &record.itemTypes[35], &record.itemTypes[36], &record.itemTypes[37], &record.itemTypes[38], &record.itemTypes[39],
        &record.itemTypes[40], &record.itemTypes[41], &record.itemTypes[42], &record.itemTypes[43], &record.itemTypes[44],
        &record.itemTypes[45], &record.itemTypes[46], &record.itemTypes[47], &record.itemTypes[48], &record.itemTypes[49],
        &record.itemTypes[50], &record.itemTypes[51], &record.itemTypes[52], &record.itemTypes[53], &record.itemTypes[54],
        &record.itemTypes[55], &record.itemTypes[56], &record.itemTypes[57], &record.itemTypes[58], &record.itemTypes[59],
        &record.face, &record.hairShape, &record.hairColor, &record.maxHp, &record.maxMp, 0x30, record.nickname.data(),
        &record.dropExperience, &record.initialTime[0], &record.initialTime[1], &record.initialTime[2],
        &record.initialTime[3], &record.initialTime[4], &record.initialTime[5], &record.socialityArgument,
        &record.socialityCount, 0x10, &record.lastSocialityCheck, &record.hennaIds[0], &record.hennaIds[1],
        &record.hennaIds[2], &record.subJobId, &record.subJobClasses[0], &record.subJobClasses[1], &record.subJobClasses[2],
        &record.subJobClasses[3], &record.dualClassId, &record.cp, &record.maxCp, &record.ssqDawnRound
    );

    std::memcpy(record.opaqueCharacterData.data(), packet, record.opaqueCharacterData.size());
    packet += record.opaqueCharacterData.size();

    packet = ::Disassemble(
        packet, "ddd", &record.pledgePenaltyTimes[0], &record.pledgePenaltyTimes[1], &record.pledgePenaltyTimes[2]
    );
    packet = ::Disassemble(packet, "dd", &record.characterNameColor, &record.nicknameColor);
    packet = ::Disassemble(
        packet, "dddd", &record.pledgeType, &record.gradeId, &record.academyPledgeDatabaseId, &record.associatedInstantZoneId
    );
    packet = ::Disassemble(
        packet, "dddddd", &record.lastLogoutTime[0], &record.lastLogoutTime[1], &record.lastLogoutTime[2],
        &record.lastLogoutTime[3], &record.lastLogoutTime[4], &record.lastLogoutTime[5]
    );
    packet = ::Disassemble(packet, "d", &record.associatedInstantZoneParam);
    packet = ::Disassemble(packet, "d", &record.bookmarkSlotCount);
    packet = ::Disassemble(
        packet, "dddddbd", &record.voteBonusTimeLimit, &record.votePlayTime, &record.voteCount, &record.voteBonusCount,
        &record.votePlayTimeCount, 0x10, &record.voteTime, &record.accountLastLogoutTime
    );
    packet = ::Disassemble(packet, "d", &record.jobGroup);
    packet = ::Disassemble(packet, "ddd", &record.faceEx, &record.hairShapeEx, &record.hairColorEx);
    packet = ::Disassemble(packet, "c", &record.hairAccessoryFlag);
    packet = ::Disassemble(packet, "d", &record.abilityPoint);
    return record.vitalityData.Disassemble(packet);
}

std::int32_t LoadSelectedTimeToEpoch(const std::array<std::int32_t, 6>& value) {
    const auto targetSystemTimeWord = [](const std::int32_t field) {
        return static_cast<std::uint16_t>(static_cast<std::uint32_t>(field));
    };
    std::tm time{};
    time.tm_year = static_cast<int>(targetSystemTimeWord(value[0])) - 1900;
    time.tm_mon = static_cast<int>(targetSystemTimeWord(value[1])) - 1;
    time.tm_mday = static_cast<int>(targetSystemTimeWord(value[2]));
    time.tm_hour = static_cast<int>(targetSystemTimeWord(value[3]));
    time.tm_min = static_cast<int>(targetSystemTimeWord(value[4]));
    time.tm_sec = static_cast<int>(targetSystemTimeWord(value[5]));
#if defined(_MSC_VER)
    const std::int64_t epoch = ::_mktime64(&time);
#else
    const std::int64_t epoch = static_cast<std::int64_t>(std::mktime(&time));
#endif
    return runtime::Int32FromBits(static_cast<std::uint32_t>(static_cast<std::uint64_t>(epoch)));
}


} // namespace

bool LoadCharacterPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server.exe 0x006cffe8 (6975 bytes), SHA-256
    // f82ba045074c65b491c6cb156efe5a0ccb64c5f231cedac32a80e1439b3bedf4.
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    std::uint32_t characterCount = 0;
    std::int32_t weekPlayTime = 0;
    _SYSTEMTIME lastSavePlayTime{};
    std::int32_t characterDataLength = 0;
    packet = ::Disassemble(
        packet, "dddbd", &userSocketId, &characterCount, &weekPlayTime, 0x10, &lastSavePlayTime, &characterDataLength
    );

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Null UserSocket")
        L2_LOG_ERROR(Log, u"[database reply] User socket is null; reply cannot be delivered.");
        return false;
    }
    const std::int32_t clientProtocolRevision = runtime::client_protocol::NegotiatedRevision(socket.get());

    if (characterDataLength == 0 && characterCount != 0) {
        // Current 0x006d0359 passes the UTF-16 file literal to the narrow
        // logger's %s lane. Preserve that diagnostic oddity literally.
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d]LoadCharacterPacket invalid data account[%d]",
        //     L"DBSocket.cpp",
        //     0x457,
        //     socket->GetAccountID())
        L2_LOG_ERROR(
            Log,
            "[character list] CacheD returned %u characters with an empty record payload for account %u; reply rejected.",
            characterCount, socket->GetAccountID()
        );
        return false;
    }

    if (characterCount > 7) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Too many Characters on 1 account! [%d]",
        //     socket->GetAccountID())
        L2_LOG_ERROR(
            Log,
            "[character list] Account %d exceeds the seven-character limit; "
            "the list is truncated.",
            socket->GetAccountID()
        );
        characterCount = 7;
    }

    if (g_nPlayTimeLimit > 0) {
        _SYSTEMTIME localTime{};
        const auto getCurrentSystemTime = []() {
            const runtime::SystemTime hostTime = runtime::GetLocalTimeNow();
            return _SYSTEMTIME{hostTime.wYear, hostTime.wMonth, hostTime.wDayOfWeek, hostTime.wDay, hostTime.wHour,
                hostTime.wMinute, hostTime.wSecond, hostTime.wMilliseconds};
        };
        bool weekChanged = false;
        if (!socket->m_bIsNew) {
            localTime = getCurrentSystemTime();
            weekChanged = socket->CheckWeekChanged(&localTime);
        } else {
            socket->m_bIsNew = false;
            socket->SetLastSavePlayTime(&lastSavePlayTime);
            localTime = getCurrentSystemTime();
            weekChanged = socket->CheckWeekChanged(&localTime);
            if (!weekChanged) {
                socket->m_nWeekPlayTime = weekPlayTime;
            }
        }
        if (weekChanged) {
            socket->ResetRelaxTime(&localTime);
            db.RequestSaveTimeLimitUsed(
                u"", 0, g_nWorldId, static_cast<int>(socket->GetAccountID()), &localTime
            );
        }
    }

    std::fill_n(socket->m_nCharacterDBIDs.begin(), 7, 0);
    socket->m_eCharacterDeactivateReasons.fill(CharacterDeactivateReason::CDR_DEACTIVATE_DEFAULT);

    std::array<std::int32_t, 7> timeToDelete{};
    int deleteCharacterCount = 0;
    std::array<char, 0x5000> assembledCharacters{};
    int assembledLength = 0;
    bool accountMismatchLogged = false;
    LoadCharacterRecord record;

    for (std::uint32_t index = 0; index < characterCount; ++index) {
        // These are the only two character-record locals that current
        // 0x006d06af/0x006d06b3 clears again for every iteration.
        record.deletionAndBan = 0;
        record.weaponEnchant = 0;
        packet = DisassembleLoadCharacterRecord(packet, record);

        if (CompareAccountNames(reinterpret_cast<const char16_t*>(socket->GetAccountName()), record.accountName.data()) !=
                0 &&
            !accountMismatchLogged) {
            const std::wstring databaseAccount = TargetTextForLog(record.accountName);
            const std::wstring authenticatedAccount = TargetTextForLog(socket->GetAccountName(), 15);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] account_id[%d] db's account_name[%s] "
            //     L"auth's account_name[%s]",
            //     L"DBSocket.cpp",
            //     0x4fd,
            //     socket->GetAccountID(),
            //     databaseAccount.c_str(),
            //     authenticatedAccount.c_str())
            L2_LOG_ERROR(
                Log, u"[character list] Account %u name mismatch: CacheD returned '%s', authenticated session is '%s'.",
                socket->GetAccountID(), databaseAccount.c_str(), authenticatedAccount.c_str()
            );
            accountMismatchLogged = true;
        }

        if (record.jobGroup < 0 || record.jobGroup > 1) {
            if (static_cast<std::uint32_t>(record.mainClass) < 0xc0U) {
                record.jobGroup = record.mainClass == 0x90 ? 1 : static_cast<int>(CT_to_FMT[record.mainClass]);
                db.RequestSaveUserJobGroup(record.worldId, record.characterId, record.jobGroup);
            } else {
                record.jobGroup = 0;
                const std::wstring characterName = TargetTextForLog(record.characterName);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[DBSocket]user class invalid. %d [user : %s]",
                //     record.mainClass,
                //     characterName.c_str())
                L2_LOG_ERROR(
                    Log, u"[character list] Character '%s' has invalid main class %d; job group defaults to 0 and is saved.",
                    characterName.c_str(), record.mainClass
                );
            }
        }

        const int basicClass = static_cast<int>(CPCParamDB::GetBasicClassByRaceAndFTM(
            static_cast<RaceType>(record.race), record.jobGroup == 0, record.gender == 0
        ));
        socket->m_nCharacterDBIDs[index] = record.characterId;
        socket->m_nCharacterPrevServers[index] = record.previousServer;
        CopyOriginName(socket->m_sCharacterOriginNames[index], record.originName);

        if (record.deletionAndBan > 0 && deleteCharacterCount < 7) {
            timeToDelete[deleteCharacterCount] =
                runtime::AddInt32Wrapping(runtime::crt::Time32Now(), record.deletionAndBan);
            ++deleteCharacterCount;
        }

        int transformId = 0;
        const CursedWeaponInfo* const cursedWeaponInfo = CCursedWeaponMgr::inst().FindConst(record.itemTypes[7]);
        if (cursedWeaponInfo != nullptr) {
            transformId = cursedWeaponInfo->transformID;
        }

        double levelPercent = 0.0;
        if (record.level > 0 && record.level < 0x7a) {
            levelPercent = static_cast<double>(record.experience - ExpByLevel[record.level - 1]) /
                static_cast<double>(ExpByLevel[record.level] - ExpByLevel[record.level - 1]);
        } else {
            const std::wstring characterName = TargetTextForLog(record.characterName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid User Level(%d) [User:%s(%d)]",
            //     L"DBSocket.cpp",
            //     0x531,
            //     record.level,
            //     characterName.c_str(),
            //     record.characterId)
            L2_LOG_ERROR(
                Log, u"[character list] Character '%s' (DB ID %d) has invalid level %d; level-progress ratio remains zero.",
                characterName.c_str(), record.characterId, record.level
            );
        }

        if (F2PConfig::IsAllCharacterSlotUsableAccount(socket.get())) {
            record.freeToPlayState = 1;
        } else if ((record.mainClass > 0x8a && record.mainClass < 0x93) || record.mainClass == 0xbe ||
            record.mainClass == 0xbf) {
            record.freeToPlayState = 4;
        }
        socket->m_eCharacterDeactivateReasons[index] =
            static_cast<CharacterDeactivateReason::DeactivateReason>(record.freeToPlayState);

        const int vitalityBonus = F2PConfig::GetVitalityBonus(socket.get());
        const int vitalityItemUsableCount = F2PConfig::GetVitalityItemUsableCount(socket.get(), record.vitalityRestoreCount);
        const int hero = WOlympiadServiceProxyUtil::IsHero(record.characterId) ? 1 : 0;
        const int remainingCapacity = static_cast<int>(assembledCharacters.size()) - assembledLength;
        const int assembled = clientProtocolRevision == runtime::client_protocol::kNorthAmericanRevision
            ? AssembleCharacterListRecordP152(
                  assembledCharacters.data() + assembledLength, remainingCapacity, record, levelPercent, vitalityBonus,
                  vitalityItemUsableCount, hero
              )
            : AssembleCharacterListRecord(
                  assembledCharacters.data() + assembledLength, remainingCapacity, record, basicClass, levelPercent,
                  transformId, vitalityBonus, vitalityItemUsableCount, hero
              );
        if (assembled > 0) {
            assembledLength += assembled;
        }
    }

    authd.AddAuthUserDataCharacterInfo(
        static_cast<int>(socket->GetAccountID()), static_cast<int>(characterCount), deleteCharacterCount, timeToDelete.data()
    );
    if (!authd.AuthRequestGetUserData(
            static_cast<int>(socket->GetAccountID()), NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_READ,
            NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE
        )) {
        socket->SendSystemMessage(SystemMessageId::DISCONNECTED_RECONNECT_LATER);
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
    }

    socket->SetCharacterAmount(characterCount);
    int dormantFlag = 0;
    if (socket->IsDormantUser()) {
        const int markingTime = socket->GetDormantMarkingTime();
        if (markingTime > 0) {
            dormantFlag =
                (runtime::SubtractInt32Wrapping(static_cast<std::int32_t>(std::time(nullptr)), markingTime) < 0x15181)
                ? 2
                : 1;
        }
    }

    int billingMode = 1;
    if (!g_bDualBillingSystem) {
        billingMode = g_bFreeOpenServer ? 2 : 0;
    }
    if (clientProtocolRevision == runtime::client_protocol::kNorthAmericanRevision) {
        const char cannotCreateCharacter =
            static_cast<char>(characterCount == static_cast<std::uint32_t>(g_nProhibitCharCreationCount));
        // Portable P152 compatibility extension. After the Korean-client
        // DWORD, Etina's Fate writes two distinct one-byte fields: the
        // inactive-account gift flag and the Balthus-Knights creation flag.
        // A former cleanup kept only the latter and started the first
        // character record one byte too early. Spell out both fields instead
        // of relying on the old byte-equivalent `h(0)` representation.
        socket->Send(
            "cddccdccb", 9, characterCount, g_nProhibitCharCreationCount, static_cast<int>(cannotCreateCharacter), 1, 2, 0,
            0, assembledLength, assembledCharacters.data()
        );
    } else {
        socket->Send(
            "cddccdcb", 9, characterCount, g_nProhibitCharCreationCount, socket->m_bForceLogin, billingMode,
            socket->m_iPremiumLevel, dormantFlag, assembledLength, assembledCharacters.data()
        );
    }
    socket->SetLastUsePacket(0x2b, false);
    return false;
}

bool LoadSelectedCharacterPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server.exe 0x006caa68 (19524 bytes), SHA-256
    // f632a01983efa24f63f997a0c18f2455c3b555c87ecbae100fbfcc8f5b20fb94.
    L2_GUARD_FUNCTION();

    LoadSelectedCharacterRecord record;
    packet = ::Disassemble(packet, "d", &record.userSocketId);
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(record.userSocketId);
    if (!socket) {
        return false;
    }

    const std::uint8_t* const ssqPacket = DisassembleLoadSelectedCharacterRecord(packet, record);
    const FVector databasePosition{
        static_cast<double>(record.x), static_cast<double>(record.y), static_cast<double>(record.z)};
    FVector selectedPosition = databasePosition;

    if (record.inVehicle > 0) {
        selectedPosition = Singleton<CSetting>::inst().GetRestartPoint(
            static_cast<RaceType>(record.race), selectedPosition, record.karma < 0, record.pkCount
        );
        record.x = runtime::Win64CvttDoubleToInt32(selectedPosition.x);
        record.y = runtime::Win64CvttDoubleToInt32(selectedPosition.y);
        record.z = runtime::Win64CvttDoubleToInt32(selectedPosition.z);
    }

    const std::int32_t initialEpoch = LoadSelectedTimeToEpoch(record.initialTime);
    const std::int32_t lastLogoutEpoch = LoadSelectedTimeToEpoch(record.lastLogoutTime);
    if (static_cast<std::uint16_t>(record.initialTime[0]) != 0) {
        const std::int32_t mapX = runtime::AddInt32Wrapping(record.x, 0x50000) >> 15;
        const std::int32_t mapY = runtime::AddInt32Wrapping(record.y, 0x40000) >> 15;
        if (mapX == 0x0f && mapY == 0x0b && runtime::SubtractInt32Wrapping(lastLogoutEpoch, initialEpoch) > 0x708) {
            selectedPosition = Singleton<CSetting>::inst().GetRestartPoint(
                static_cast<RaceType>(record.race), selectedPosition, record.karma < 0, record.pkCount
            );
            record.x = runtime::Win64CvttDoubleToInt32(selectedPosition.x);
            record.y = runtime::Win64CvttDoubleToInt32(selectedPosition.y);
            record.z = runtime::Win64CvttDoubleToInt32(selectedPosition.z);
        }

        CArea* const noRestartArea = CAreaDB::inst().FindArea(0, selectedPosition, AREAT_NO_RESTART, true);
        if (noRestartArea != nullptr) {
            const int elapsedSinceInitial = runtime::Win64CvttDoubleToInt32(
                std::difftime(static_cast<std::time_t>(lastLogoutEpoch), static_cast<std::time_t>(initialEpoch))
            );
            const int elapsedSinceServerStart = runtime::Win64CvttDoubleToInt32(
                std::difftime(static_cast<std::time_t>(lastLogoutEpoch), static_cast<std::time_t>(server_up_time))
            );
            if (noRestartArea->GetRestartAllowedTime() == -1) {
                selectedPosition = noRestartArea->GetRandomRestartPoint(
                    static_cast<RaceType>(record.race), selectedPosition, record.karma < 0, record.pkCount
                );
                record.x = runtime::Win64CvttDoubleToInt32(selectedPosition.x);
                record.y = runtime::Win64CvttDoubleToInt32(selectedPosition.y);
                record.z = runtime::Win64CvttDoubleToInt32(selectedPosition.z);
            } else if (elapsedSinceInitial > noRestartArea->GetRestartAllowedTime() &&
                elapsedSinceServerStart > noRestartArea->GetRestartTime()) {
                selectedPosition = Singleton<CSetting>::inst().GetRestartPoint(
                    static_cast<RaceType>(record.race), selectedPosition, record.karma < 0, record.pkCount
                );
                record.x = runtime::Win64CvttDoubleToInt32(selectedPosition.x);
                record.y = runtime::Win64CvttDoubleToInt32(selectedPosition.y);
                record.z = runtime::Win64CvttDoubleToInt32(selectedPosition.z);
            }
        }

        if (IsGmNoRestartPosition(selectedPosition)) {
            selectedPosition = GetGMRestartPoint();
            record.x = runtime::Win64CvttDoubleToInt32(selectedPosition.x);
            record.y = runtime::Win64CvttDoubleToInt32(selectedPosition.y);
            record.z = runtime::Win64CvttDoubleToInt32(selectedPosition.z);
        }
    }

    const int pcCafeCodeCompare = std::strncmp(socket->GetPCCafeCode(), "0000000", 8);

    const std::CDualKey<int, int> userKey(g_nWorldId, record.characterId);
    CSPointer<User> duplicate = g_UserDBIDMap.FindObject(userKey);
    if (duplicate && !socket->IsUnderReloading()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BOT User Try Clone Login!!! (DBID = %d)",
        //     L"DBSocket.cpp",
        //     0x1a39,
        //     record.characterId)
        L2_LOG_ERROR(
            Log,
            u"[character login] Character DB ID %d is already active; duplicate login is rejected and the new connection is "
            u"closed.",
            record.characterId
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    std::u16string characterName(FixedUtf16View(record.characterName));
    CSPointer<User> duplicateByName = GetUserFromNameMap(g_nWorldId, characterName);
    if (duplicateByName && !socket->IsUnderReloading()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] BOT User Try Clone Login!!! "
        //     L"(CharacterName = %s)",
        //     L"DBSocket.cpp",
        //     0x1a42,
        //     characterName.c_str())
        L2_LOG_ERROR(
            Log,
            u"[character login] Character name '%s' is already active; duplicate login is rejected and the new connection "
            u"is closed.",
            characterName.c_str()
        );
        socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    const bool pcCafeFromF2P = F2PConfig::IsContainPCCafeUserBonus(socket.get());
    const bool pcCafeUser = pcCafeFromF2P || pcCafeCodeCompare != 0;
    const int premiumLevel = socket->m_iPremiumLevel;
    std::u16string accountName(FixedUtf16View(record.accountName));
    std::u16string nickname(FixedUtf16View(record.nickname));

    User* user = nullptr;
    enum class CharacterLoadOutcome { Loaded, MissingReloadUser, InactiveSocket };
    const CharacterLoadOutcome loadOutcome = [&]() {
        L2_GUARD(socket->mLock);
        if (socket->IsUnderReloading()) {
            user = socket->GetUser();
            if (user == nullptr) {
                return CharacterLoadOutcome::MissingReloadUser;
            }

            user->SetAtConstructor(
                record.pledgeId, record.builderLevel, record.sex, record.race, record.classType, record.worldId, record.x,
                record.y, record.z, record.hp, record.mp, record.sp, record.experience, record.level, record.karma,
                record.pkCount, static_cast<unsigned int>(record.duelCount), static_cast<unsigned int>(record.pkPardonCount),
                record.face, record.hairShape, record.hairColor, nickname.data(), record.opaqueCharacterData.data(),
                static_cast<unsigned int>(record.pledgePenaltyTimes[0]),
                static_cast<unsigned int>(record.pledgePenaltyTimes[1]),
                static_cast<unsigned int>(record.pledgePenaltyTimes[2]), record.subJobId, record.subJobClasses[0],
                record.subJobClasses[1], record.subJobClasses[2], record.subJobClasses[3], record.dualClassId,
                runtime::Win64CvttDoubleToInt32(record.cp), record.ssqDawnRound, record.pledgeType, record.gradeId,
                record.academyPledgeDatabaseId, pcCafeUser, premiumLevel, record.characterNameColor, record.nicknameColor,
                record.associatedInstantZoneId, record.associatedInstantZoneParam, record.bookmarkSlotCount,
                &record.lastSocialityCheck, record.socialityCount, record.socialityArgument, record.vitalityData,
                record.hennaIds[0], record.hennaIds[1], record.hennaIds[2], record.voteBonusTimeLimit, record.votePlayTime,
                record.voteCount, record.voteBonusCount, record.votePlayTimeCount, &record.voteTime, record.jobGroup,
                record.faceEx, record.hairShapeEx, record.hairColorEx, record.abilityPoint
            );
            user->m_SkillMod.InitAtConstructor();
            user->InitializeSkillMod();
            user->InitializeSkillDurationMod();
            user->InitAcquiredSkills();
            user->m_inv.ClearRuneState();
            user->m_nReloadPet = 0;
        } else {
            if (!socket->m_bActive) {
                return CharacterLoadOutcome::InactiveSocket;
            }

            user = new User(
                characterName.data(), accountName.data(), static_cast<unsigned int>(record.characterId),
                static_cast<unsigned int>(record.accountId), static_cast<unsigned int>(record.pledgeId), record.builderLevel,
                record.sex, record.race, record.classType, record.worldId, record.x, record.y, record.z, record.hp,
                record.mp, record.sp, record.experience, record.level, record.karma, record.pkCount,
                static_cast<unsigned int>(record.duelCount), static_cast<unsigned int>(record.pkPardonCount), record.face,
                record.hairShape, record.hairColor, nickname.data(), record.opaqueCharacterData.data(),
                static_cast<unsigned int>(record.pledgePenaltyTimes[0]),
                static_cast<unsigned int>(record.pledgePenaltyTimes[1]),
                static_cast<unsigned int>(record.pledgePenaltyTimes[2]), record.subJobId, record.subJobClasses[0],
                record.subJobClasses[1], record.subJobClasses[2], record.subJobClasses[3], record.dualClassId,
                runtime::Win64CvttDoubleToInt32(record.cp), record.ssqDawnRound, record.pledgeType, record.gradeId,
                record.academyPledgeDatabaseId, pcCafeUser, premiumLevel, record.characterNameColor, record.nicknameColor,
                record.associatedInstantZoneId, record.associatedInstantZoneParam, record.bookmarkSlotCount,
                &record.lastSocialityCheck, record.socialityCount, record.socialityArgument, record.vitalityData,
                record.hennaIds[0], record.hennaIds[1], record.hennaIds[2], record.voteBonusTimeLimit, record.votePlayTime,
                record.voteCount, record.voteBonusCount, record.votePlayTimeCount, &record.voteTime, record.jobGroup,
                record.faceEx, record.hairShapeEx, record.hairColorEx, record.abilityPoint
            );
            user->OnCreate();
            user->InitUser();
            socket->BindUser(user);
            user->Set(socket.get());
            user->InitMentorMentee();
        }

        if (user->m_data->m_nSubJob1Class == -1 && user->m_data->m_nSubJob2Class == -1 &&
            user->m_data->m_nSubJob3Class == -1) {
            user->SendUserSubJobInfo(SubJob::NOTIFY_CLASS_INFO);
            user->m_data->m_SubJobLevel[0] = user->m_data->m_nLevel;
            if (user->m_data->m_DualClassID == 0) {
                user->m_data->m_MainClassLevel = user->m_data->m_nLevel;
            }
        } else {
            db.RequestLoadSubJob(user, false);
        }
        return CharacterLoadOutcome::Loaded;
    }();

    switch (loadOutcome) {
        case CharacterLoadOutcome::MissingReloadUser: {
            const std::wstring socketAccount = TargetTextForLog(socket->GetAccountName(), 15);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[LoadSelectedCharacterPacket] AccountName[%s], "
            //     L"pUser(NULL), reason(%d), raid_status(%d)",
            //     socketAccount.c_str(),
            //     socket->m_nCloseReason,
            //     socket->GetRaidStatus())
            L2_LOG_ERROR(
                Log,
                u"[character login] Reloaded user is missing for account '%s': close reason %d, raid status %d; character "
                u"load aborted.",
                socketAccount.c_str(), socket->m_nCloseReason, socket->GetRaidStatus()
            );
            return false;
        }
        case CharacterLoadOutcome::InactiveSocket: {
            const std::wstring socketAccount = TargetTextForLog(socket->GetAccountName(), 15);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[LoadSelectedCharacterPacket] AccountName[%s] "
            //     L"m_bActive == false",
            //     socketAccount.c_str())
            L2_LOG_ERROR(
                Log, u"[character login] Socket for account '%s' became inactive during character load; request aborted.",
                socketAccount.c_str()
            );
            return false;
        }
        case CharacterLoadOutcome::Loaded:
            break;
    }

    user->SetHairAccFlag(record.hairAccessoryFlag == 1);
    user->m_data->SetAccountLastLogoutTime(record.accountLastLogoutTime);
    user->m_nLoginTimeStamp = initialEpoch;
    user->SetSSQInfo(ssqPacket);
    user->m_data->m_nAge = socket->GetAge();
    user->m_data->m_CountryCode = socket->m_CountryCode;

    for (std::size_t slot = 0; slot < 15; ++slot) {
        socket->m_nSlotItemDBID[slot] = record.itemDatabaseIds[slot];
    }
    socket->m_nSlotItemDBID[16] = record.itemDatabaseIds[15];
    socket->m_nSlotItemDBID[18] = record.itemDatabaseIds[16];
    socket->m_nSlotItemDBID[20] = record.itemDatabaseIds[17];
    for (std::size_t source = 18; source < record.itemDatabaseIds.size(); ++source) {
        socket->m_nSlotItemDBID[source + 3] = record.itemDatabaseIds[source];
    }

    FVector start = user->m_data->GetPos();
    if (!CWorld::Inst().IsValidPosition(
            runtime::Win64CvttDoubleToInt32(start.x), runtime::Win64CvttDoubleToInt32(start.y)
        )) {
        start = runtime::CheckedVectorAt(Singleton<CSetting>::inst().m_StartPoint, 0).m_Point;
    }
    user->m_nDownExp = record.dropExperience;
    user->AdjustStartPoint(start);
    user->m_data->SetPos(start);
    user->m_bConnected = true;

    if (socket->IsUnderReloading() || user->m_data->m_nWorldId != g_nWorldId) {
        runtime::raw_atomic::ExchangeI32(&user->m_nReloadPet, 1);
    } else {
        g_PetManager.CreatePet(user, nullptr);
        CPet* const pet = user->GetPet();
        if (pet == nullptr) {
            user->m_bPetLinked = false;
        } else {
            const auto teleportPet = [&]() {
                pet->TeleportToLocation(
                    runtime::Win64CvttDoubleToInt32(start.x), runtime::Win64CvttDoubleToInt32(start.y),
                    runtime::Win64CvttDoubleToInt32(start.z), 0, 0x10000, static_cast<TeleportSourceType::Enum>(0x0d),
                    nullptr, 0
                );
            };
            if (!databasePosition.IsEqual(start)) {
                teleportPet();
            } else if (!pet->IsAlive() && !pet->IsInRealWorld()) {
                CInstantZone* const instantZone = pet->GetInstantZone();
                if (instantZone != nullptr) {
                    instantZone->LeaveZone(pet->CastNPC());
                }
                teleportPet();
            }
            user->m_bPetLinked = true;
        }
    }

    db.RequestLoadSummons(user, record.userSocketId);

    for (int index = 0; index < g_nBotInspectorTestCount; ++index) {
        const FVector inspectorYPosition = user->GetPos();
        const FVector inspectorXPosition = user->GetPos();
        CInspector* const inspector = g_InspectorDB.GetInspector(
            runtime::Win64CvttDoubleToInt32(inspectorXPosition.x),
            runtime::Win64CvttDoubleToInt32(inspectorYPosition.y)
        );
        if (inspector == nullptr) {
            break;
        }
        user->m_pInspectors.push_back(inspector);
    }

    CPacketHelperEx<20480> inspectorPacket;
    inspectorPacket.Assemble("d", static_cast<int>(user->m_pInspectors.size()));
    if (static_cast<int>(user->m_pInspectors.size()) == g_nBotInspectorTestCount && g_nBotInspectorTestCount > 0) {
        CInspector* const first = user->m_pInspectors[0];
        inspectorPacket.Assemble("db", first->BinSize, first->BinSize, first->Bin);
        for (CInspector* const inspector : user->m_pInspectors) {
            inspectorPacket.Assemble("dddd", inspector->p1, inspector->p2, inspector->p3, inspector->p4);
        }
    }

    CPacketHelperEx<20480> gameGuardPacket;
    CPacketHelperEx<20480> newInspectorPacket;
    Agent::PacketList packets;
    packets.push_back(&gameGuardPacket);
    packets.push_back(&newInspectorPacket);
    const int shuffle = socket->GetShuffleNum();

    if (socket->IsUnderReloading()) {
        const FVector reloadZPosition = user->m_data->GetPos();
        const FVector reloadYPosition = user->m_data->GetPos();
        const FVector reloadXPosition = user->m_data->GetPos();
        socket->Send(
            "chddddd", 0xfe, 0xbb, socket->GetRaidCharServerId(), runtime::Win64CvttDoubleToInt32(reloadXPosition.x),
            runtime::Win64CvttDoubleToInt32(reloadYPosition.y), runtime::Win64CvttDoubleToInt32(reloadZPosition.z), 0
        );
        socket->SetRaidStatus(URS_NONE);
        socket->AddTimer(1000, 0);
        if (user->m_pAgent != nullptr) {
            static_cast<GameGuardAgent*>(user->m_pAgent)->m_csa.Close();
        }
        std::int64_t adena;
        std::int64_t totalPrice;
        std::int64_t totalCount;
        {
            std::optional<GuardHelper> lockStage;
            lockStage.emplace(L"LoadSelectedCharacterPacket_returnraid_readlock1", CThreadManager::GetThreadID());
            {
                L2_READ_GUARD(user->m_inv);
                lockStage.reset();
                CSharedItemData* itemData = user->m_inv.GetFirst();
                adena = 0;
                totalPrice = 0;
                totalCount = 0;
                {
                    GuardHelper inventoryWalk(L"LoadSelectedCharacterPacket_returnraid_2", CThreadManager::GetThreadID());
                    while (itemData != nullptr) {
                        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
                        if (item && item->m_data != nullptr) {
                            const std::int64_t count = item->m_data->m_nCount;
                            if (item->m_data->m_nClassID == 57) {
                                adena = runtime::AddInt64Wrapping(adena, count);
                            }
                            totalCount = runtime::AddInt64Wrapping(totalCount, count);
                            totalPrice = runtime::AddInt64Wrapping(
                                totalPrice, runtime::MultiplyInt64Wrapping(item->m_const->m_nDefaultPrice, count)
                            );
                        }
                        itemData = itemData->Next();
                    }
                }
                lockStage.emplace(L"LoadSelectedCharacterPacket_returnraid_readunlock", CThreadManager::GetThreadID());
            }
        }
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x1fac, user, nullptr, nullptr, nullptr, nullptr, user->m_data->m_nWorldId, g_nWorldId, adena, totalPrice,
            totalCount, 0, 0
        );
    } else if (user->m_data->m_nWorldId == g_nWorldId) {
        if (!user->m_pAgent->QueryFirst(packets, packets.begin())) {
            return false;
        }
        const FVector localZPosition = user->m_data->GetPos();
        const FVector localYPosition = user->m_data->GetPos();
        const FVector localXPosition = user->m_data->GetPos();
        if (runtime::client_protocol::NegotiatedRevision(socket.get()) ==
            runtime::client_protocol::kNorthAmericanRevision) {
            SendCharacterSelectedP152(*socket.get(), *user, user->m_nBaseClass);
        } else {
            socket->Send(
                "cSdSddddddddddffQQddddbdbbd", 0x0b, user->GetName(), static_cast<int>(user->GetDBID()), u"",
                static_cast<int>(user->GetAccountID()), user->m_data->m_nPledgeID, user->m_data->m_nPVPPoint,
                user->m_data->m_Sex, user->m_data->m_nClass, user->m_nBaseClass, 1,
                runtime::Win64CvttDoubleToInt32(localXPosition.x), runtime::Win64CvttDoubleToInt32(localYPosition.y),
                runtime::Win64CvttDoubleToInt32(localZPosition.z), user->m_data->m_dHP, user->m_data->m_dMP,
                user->m_data->m_nSP, user->m_data->m_nExp, user->m_data->m_nLevel, user->m_data->m_nKarma,
                user->m_data->m_nPKCount, g_L2Time.GetTime(), inspectorPacket.GetLength(), inspectorPacket.GetPacket(),
                user->m_data->m_Race, gameGuardPacket.GetLength(), gameGuardPacket.GetPacket(),
                newInspectorPacket.GetLength(), newInspectorPacket.GetPacket(), shuffle
            );
        }
    } else {
        if (!user->m_pAgent->QueryFirst(packets, packets.begin())) {
            return false;
        }

        const std::CDualKey<int, int> raidKey(user->m_data->m_nWorldId, static_cast<int>(user->GetDBID()));
        int raidCharacterServerId = 0;
        if (PledgeRaidNetworkUtil::IsHost()) {
            WOlympiadService& olympiadService = WOlympiadService::inst();
            const olympiad::PkOlympiadAuthInfo* const authInfo = olympiadService.FindAuthInfo(raidKey);
            if (authInfo == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[WORLD_OLYMPIAD][%s][%d]Invalid olympiad "
                //     L"user is loaded: %d, %d, %s",
                //     L"DBSocket.cpp",
                //     0x1bbe,
                //     user->m_data->m_nWorldId,
                //     user->GetDBID(),
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     L"[olympiad] Character '%s' (DB ID %u, origin world %d) has no host authorization record. Connection
                //     is being closed.", user->GetName(), user->GetDBID(), user->m_data->m_nWorldId)
                L2_LOG_ERROR(
                    Log,
                    u"[olympiad] Character '%s' (DB ID %u, origin world %d) has no host authorization record; connection is "
                    u"being closed.",
                    user->GetName(), user->GetDBID(), user->m_data->m_nWorldId
                );
                socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                return false;
            }

            CSPointer<User> referencedUser(user, ObjectRefType::FunctionChain);
            olympiadService.ConfirmOlympiadUser(referencedUser);
            raidCharacterServerId = user->m_data->m_nWorldId;
            user->m_RaidInfo.m_OriginPos = FVector{
                static_cast<double>(authInfo->nX), static_cast<double>(authInfo->nY), static_cast<double>(authInfo->nZ)};
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR]"
            //     L"[WORLD_OLYMPIAD][PACKET]"
            //     L"S_EX_RAID_CHARACTER_SELECTED sent: %d, %d",
            //     L"DBSocket.cpp",
            //     0x1bc8,
            //     user->m_data->m_nWorldId,
            //     user->GetDBID())
            // Noise: routine character confirmation. The packet is sent later,
            // so this trace also claimed delivery before the send call.
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d][NO_ERROR]"
            //     L"[WORLD_OLYMPIAD][PACKET]"
            //     L"S_EX_RAID_CHARACTER_SELECTED sent: %d, %d",
            //     L"DBSocket.cpp",
            //     0x1bc8,
            //     user->m_data->m_nWorldId,
            //     user->GetDBID())
        } else {
            RaidReserverInfo* const reservation = RaidServerManager::inst().GetRaidCandidateInfo(raidKey);
            if (reservation == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Raid user[%d:%d:%s] is loaded "
                //     L"from DB. but there is no reserver info.",
                //     L"DBSocket.cpp",
                //     0x1bd1,
                //     user->m_data->m_nWorldId,
                //     user->GetDBID(),
                //     user->GetName())
                // L2_LOG_ERROR(Log,
                //     L"[raid server] Character '%s' (DB ID %u, origin world %d) has no reservation record. Connection is
                //     being closed.", user->GetName(), user->GetDBID(), user->m_data->m_nWorldId)
                L2_LOG_ERROR(
                    Log,
                    u"[raid server] Character '%s' (DB ID %u, origin world %d) has no reservation record; connection is "
                    u"being closed.",
                    user->GetName(), user->GetDBID(), user->m_data->m_nWorldId
                );
                socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                return false;
            }

            raidCharacterServerId = reservation->GetCharServerId();
            RaidServerManager::inst().ConfirmRaidUser(*user);
            user->m_RaidInfo.m_OriginPos = reservation->GetOriginPos();
        }

        int randomSeed = shuffle;
        std::uint64_t publicKey = 0;
        socket->GetShuffleSeedAndPublicKey(randomSeed, publicKey);
        socket->Send("chdQ", 0xfe, 0x138, randomSeed, publicKey);
        socket->SetKey(publicKey);
        const FVector raidZPosition = user->m_data->GetPos();
        const FVector raidYPosition = user->m_data->GetPos();
        const FVector raidXPosition = user->m_data->GetPos();
        socket->Send(
            "chdddddbbb", 0xfe, 0xbb, raidCharacterServerId, runtime::Win64CvttDoubleToInt32(raidXPosition.x),
            runtime::Win64CvttDoubleToInt32(raidYPosition.y), runtime::Win64CvttDoubleToInt32(raidZPosition.z), 1,
            inspectorPacket.GetLength(), inspectorPacket.GetPacket(), gameGuardPacket.GetLength(),
            gameGuardPacket.GetPacket(), newInspectorPacket.GetLength(), newInspectorPacket.GetPacket()
        );
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR]Inspector [%d][%d] gameGuard [%d][%d] "
        //     L"newInspector [%d][%d]",
        //     inspectorPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         inspectorPacket.GetPacket())),
        //     gameGuardPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         gameGuardPacket.GetPacket())),
        //     newInspectorPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         newInspectorPacket.GetPacket())))
        // Noise: unconditional packet buffer sizes and truncated addresses do
        // not describe a failure or an operator action.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR]Inspector [%d][%d] gameGuard [%d][%d] "
        //     L"newInspector [%d][%d]",
        //     inspectorPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         inspectorPacket.GetPacket())),
        //     gameGuardPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         gameGuardPacket.GetPacket())),
        //     newInspectorPacket.GetLength(),
        //     static_cast<int>(reinterpret_cast<std::intptr_t>(
        //         newInspectorPacket.GetPacket())))
        // Diagnostic-only local used by the suppressed trace below.
        // const GameGuardAgent* const gameGuard =
        //     static_cast<GameGuardAgent*>(user->m_pAgent)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR]gameGuardData [%d][%d][%d][%d]",
        //     gameGuard->m_csa.m_AuthQuery.dwIndex,
        //     gameGuard->m_csa.m_AuthQuery.dwValue1,
        //     gameGuard->m_csa.m_AuthQuery.dwValue2,
        //     gameGuard->m_csa.m_AuthQuery.dwValue3)
        // Noise: routine authentication query contents are not an error and
        // are not needed in the operator log.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR]gameGuardData [%d][%d][%d][%d]",
        //     gameGuard->m_csa.m_AuthQuery.dwIndex,
        //     gameGuard->m_csa.m_AuthQuery.dwValue1,
        //     gameGuard->m_csa.m_AuthQuery.dwValue2,
        //     gameGuard->m_csa.m_AuthQuery.dwValue3)
        std::int64_t adena;
        std::int64_t totalPrice;
        std::int64_t totalCount;
        {
            std::optional<GuardHelper> lockStage;
            lockStage.emplace(L"LoadSelectedCharacterPacket_gotoraid_readlock1", CThreadManager::GetThreadID());
            {
                L2_READ_GUARD(user->m_inv);
                lockStage.reset();
                CSharedItemData* itemData = user->m_inv.GetFirst();
                adena = 0;
                totalPrice = 0;
                totalCount = 0;
                {
                    GuardHelper inventoryWalk(L"LoadSelectedCharacterPacket_gotoraid_2", CThreadManager::GetThreadID());
                    while (itemData != nullptr) {
                        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
                        if (item && item->m_data != nullptr) {
                            const std::int64_t count = item->m_data->m_nCount;
                            if (item->m_data->m_nClassID == 57) {
                                adena = runtime::AddInt64Wrapping(adena, count);
                            }
                            totalCount = runtime::AddInt64Wrapping(totalCount, count);
                            totalPrice = runtime::AddInt64Wrapping(
                                totalPrice, runtime::MultiplyInt64Wrapping(item->m_const->m_nDefaultPrice, count)
                            );
                        }
                        itemData = itemData->Next();
                    }
                }
                lockStage.emplace(L"LoadSelectedCharacterPacket_gotoraid_readunlock", CThreadManager::GetThreadID());
            }
        }
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x1fab, user, nullptr, nullptr, nullptr, nullptr, user->m_data->m_nWorldId, g_nWorldId, adena, totalPrice,
            totalCount, 0, 0
        );
    }

    socket->HackInfo.SendListTick = runtime::GetTickCount();
    return false;
}

bool LoadItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server.exe 0x006c3d00 (3163 bytes), SHA-256
    // d24a284d969932086e4865d0758e5ba462268ca17d684c76116ee9f3e01a6cff.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t totalItemCount = 0;
    std::uint32_t currentPackageItemCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &totalItemCount, &currentPackageItemCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xaa,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[inventory load] Reply references missing user runtime ID %d; item package ignored.", userServerId
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    std::optional<UserItemDurationMapper> durationMapperStorage;
    std::optional<std::set<int>> equippedArtifactClassesStorage;
    const bool loadComplete = [&] {
        L2_GUARD(*user->m_lock);
        L2_WRITE_GUARD(user->m_inv);

        auto& durationMapper = durationMapperStorage.emplace(user);
        const bool firstPackage =
            runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0) == 0;
        if (firstPackage) {
            for (std::int32_t& slotIndex : user->m_data->m_nSlotIndex) {
                slotIndex = -1;
            }
            user->ValidateOrgHP();
            user->ValidateOrgMP();
            user->ValidateOrgCP();
            user->DeleteAllItemsInInventory();
            user->ResetCheckEquipedFlag();
        }

        for (std::uint32_t packageIndex = 0; packageIndex < currentPackageItemCount; ++packageIndex) {
            CItemDisassemblerLoadItem itemData;
            packet = itemData.DisassembleItemInfo(packet);

            CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
            if (item == nullptr) {
                runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
                continue;
            }

            item->m_data->m_nInventorySlotIndex = itemData.m_nIndex;
            if (!user->m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBSocket.cpp",
                //     0x1dc)
                L2_LOG_ERROR(
                    Log, "[inventory] Item insertion failed while loading a package item for user %u.", user->GetDBID()
                );
            }
            item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
            if (!item->SetAmount(itemData.m_nAmount)) {
                // The current image supplies the first amount as a
                // 64-bit variadic lane even though the diagnostic says %d.
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%d][%I64d]",
                //     "DBSocket.cpp",
                //     0x1e3,
                //     static_cast<long long>(item->m_data->m_nCount),
                //     static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log, u"[inventory load] Item DB ID %d (class %d) rejected amount %I64d; current amount is %I64d.",
                    itemData.m_nDBID, itemData.m_nClassID, static_cast<long long>(itemData.m_nAmount),
                    static_cast<long long>(item->m_data->m_nCount)
                );
            }
            item->SetEnchanted(itemData.m_nEnchanted);
            item->m_Useable = user->CheckUseable(item);
            item->m_data->m_nDamaged = itemData.m_nDamaged;
            item->m_data->m_bBlessed = itemData.m_nIsBlessed;
            item->SetVariation(
                ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
            );
            item->SetAttribute(itemData.m_Attribute);
            item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
            item->m_data->SetLocked(itemData.m_hLocked);
            item->m_data->SetUseCount(itemData.m_hUseCount);

            for (int slot = ST_UNDERWEAR; slot < ST_MAX; ++slot) {
                if (userSocket->m_nSlotItemDBID[slot] != itemData.m_nDBID) {
                    continue;
                }

                if (slot >= ST_JEWEL1 && slot <= ST_JEWEL6) {
                    if (user->m_data->m_nSlotIndex[ST_BROOCH] == -1) {
                        user->m_bCheckBrooch = true;
                    }
                } else if (slot >= ST_DECO1 && slot <= ST_DECO6) {
                    if (user->m_data->m_nSlotIndex[ST_RBRACELET] == -1) {
                        user->m_bCheckRBrace = true;
                    }
                } else if (slot >= ST_AGATHION_MAIN && slot <= ST_AGATHION_SUB4) {
                    if (user->m_data->m_nSlotIndex[ST_LBRACELET] == -1) {
                        user->m_bCheckLBracelet = true;
                    }
                } else if (slot >= ST_ARTIFACT_A1 && slot < ST_MAX && user->m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                    user->m_bCheckArtifactbook = true;
                }

                user->m_data->m_nSlotIndex[slot] = item->m_data->index;
                user->m_inv.RemoveSlotInfo(item);
                if (slot == ST_RLHAND) {
                    if (item->m_const->m_AttackType == AT_BOW) {
                        user->m_bBowEquiped = true;
                    } else if (item->m_const->m_AttackType == AT_CROSSBOW ||
                        item->m_const->m_AttackType == AT_TWOHANDCROSSBOW) {
                        user->m_bCrossBowEquiped = true;
                    }
                }
            }

            const int chestIndex = user->m_data->m_nSlotIndex[ST_CHEST];
            if (chestIndex != -1 && chestIndex == user->m_data->m_nSlotIndex[ST_LEGS]) {
                user->m_data->m_nSlotIndex[ST_LEGS] = -1;
                if (chestIndex == user->m_data->m_nSlotIndex[ST_GLOVES]) {
                    user->m_data->m_nSlotIndex[ST_GLOVES] = -1;
                    user->m_data->m_nSlotIndex[ST_FEET] = -1;
                    user->m_data->m_nSlotIndex[ST_ALLDRESS] = chestIndex;
                } else {
                    user->m_data->m_nSlotIndex[ST_ONEPIECE] = chestIndex;
                }
            }

            if (user->m_data->m_nSlotIndex[ST_HAIR] != -1 &&
                user->m_data->m_nSlotIndex[ST_HAIR] == user->m_data->m_nSlotIndex[ST_HAIR2]) {
                user->m_data->m_nSlotIndex[ST_HAIR2] = -1;
                user->m_data->m_nSlotIndex[ST_HAIRALL] = user->m_data->m_nSlotIndex[ST_HAIR];
            }

            if (item->IsDuration()) {
                durationMapper.Map(static_cast<unsigned int>(itemData.m_nDBID), itemData.m_nDuration);
            }
            if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
                db.RequestLoadItemAgathionEnergy(user, item->m_data->m_nId, static_cast<int>(item->GetDBID()));
            }
            item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
            runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
        }

        if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
            runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
                runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
            if (user->m_bCheckRBrace && user->m_data->m_nSlotIndex[ST_RBRACELET] == -1) {
                for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
                    const int itemIndex = user->m_data->m_nSlotIndex[slot];
                    if (itemIndex == -1) {
                        continue;
                    }
                    CItem* const item = user->m_inv.GetByIndex(itemIndex);
                    if (item == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[LoadItemsPacket] invalid ")
                        L2_LOG_ERROR(Log, u"[LoadItemsPacket] invalid ");
                    } else {
                        item->m_data->m_nInventorySlotIndex = itemIndex;
                        user->m_data->m_nSlotIndex[slot] = -1;
                    }
                }
            }

            if (user->m_bCheckBrooch && user->m_data->m_nSlotIndex[ST_BROOCH] == -1) {
                for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
                    const int itemIndex = user->m_data->m_nSlotIndex[slot];
                    if (itemIndex == -1) {
                        continue;
                    }
                    CItem* const item = user->m_inv.GetByIndex(itemIndex);
                    if (item == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[LoadItemsPacket] invalid ")
                        L2_LOG_ERROR(Log, u"[LoadItemsPacket] invalid ");
                    } else {
                        item->m_data->m_nInventorySlotIndex = itemIndex;
                        user->m_data->m_nSlotIndex[slot] = -1;
                    }
                }
            }

            if (user->m_bCheckLBracelet && user->m_data->m_nSlotIndex[ST_LBRACELET] == -1) {
                for (int slot = ST_AGATHION_MAIN; slot <= ST_AGATHION_SUB4; ++slot) {
                    const int itemIndex = user->m_data->m_nSlotIndex[slot];
                    if (itemIndex == -1) {
                        continue;
                    }
                    CItem* const item = user->m_inv.GetByIndex(itemIndex);
                    if (item == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[LoadItemsPacket] invalid ")
                        L2_LOG_ERROR(Log, u"[LoadItemsPacket] invalid ");
                    } else {
                        item->m_data->m_nInventorySlotIndex = itemIndex;
                        user->m_data->m_nSlotIndex[slot] = -1;
                    }
                }
            }

            if (user->m_bCheckArtifactbook && user->m_data->m_nSlotIndex[ST_ARTIFACTBOOK] == -1) {
                for (int slot = ST_ARTIFACT_A1; slot < ST_MAX; ++slot) {
                    const int itemIndex = user->m_data->m_nSlotIndex[slot];
                    if (itemIndex == -1) {
                        continue;
                    }
                    CItem* const item = user->m_inv.GetByIndex(itemIndex);
                    if (item == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[LoadItemsPacket][%d] invalid item. "
                        //     L"SlotIndex[%d]",
                        //     0x2c4,
                        //     slot)
                        L2_LOG_ERROR(
                            Log,
                            u"[LoadItemsPacket][%d] invalid item. "
                            u"SlotIndex[%d]",
                            0x2c4, slot
                        );
                    } else {
                        item->m_data->m_nInventorySlotIndex = itemIndex;
                        user->m_data->m_nSlotIndex[slot] = -1;
                    }
                }
            }

            auto& equippedArtifactClasses = equippedArtifactClassesStorage.emplace();
            for (int slot = ST_ARTIFACT_A1; slot < ST_MAX; ++slot) {
                const int itemIndex = user->m_data->m_nSlotIndex[slot];
                if (itemIndex == -1) {
                    continue;
                }
                CItem* const item = user->m_inv.GetByIndex(itemIndex);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[LoadItemsPacket][%d] invalid item. SlotIndex[%d]",
                    //     0x2d9,
                    //     slot)
                    L2_LOG_ERROR(Log, u"[LoadItemsPacket][%d] invalid item. SlotIndex[%d]", 0x2d9, slot);
                } else if (!equippedArtifactClasses.insert(item->m_nClassId).second) {
                    item->m_data->m_nInventorySlotIndex = itemIndex;
                    user->m_data->m_nSlotIndex[slot] = -1;
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[LoadItemsPacket][ArtifactSystem] Same artifact item "
                    //     L"is equipped. User[%s] slot[%d] item[%s]",
                    //     user->GetName(),
                    //     slot,
                    //     item->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[LoadItemsPacket][ArtifactSystem] Same artifact item "
                    //     L"is equipped. User[%s] slot[%d] item[%s]",
                    //     user->GetName(),
                    //     slot,
                    //     item->GetName())
                    L2_LOG_ERROR(
                        Log,
                        u"[LoadItemsPacket][ArtifactSystem] Same artifact item "
                        u"is equipped. User[%s] slot[%d] item[%s]",
                        user->GetName(), slot, item->GetName()
                    );
                }
            }

            runtime::raw_atomic::ExchangeI32(&user->m_ArtifactSlotChanged, 1);

            if (user->m_bBowEquiped) {
                CSPointer<CItem> weapon = CItemFactory::Inst().FindObjectSP(user->m_data->m_nSlotIndex[ST_RLHAND]);
                if (weapon) {
                    const int ammunitionIndex = user->m_data->m_nSlotIndex[ST_LHAND];
                    if (ammunitionIndex == -1) {
                        CItem* const arrow = user->m_inv.GetArrowByCrystal(weapon->m_const->GetCrystalType());
                        if (arrow == nullptr) {
                            user->m_data->m_nSlotIndex[ST_LHAND] = -1;
                        } else {
                            user->m_data->m_nSlotIndex[ST_LHAND] = arrow->m_data->index;
                            user->m_inv.RemoveSlotInfo(arrow);
                        }
                    } else {
                        user->m_data->m_iLastArrowIndex = ammunitionIndex;
                    }
                }
            } else if (user->m_bCrossBowEquiped) {
                CSPointer<CItem> weapon = CItemFactory::Inst().FindObjectSP(user->m_data->m_nSlotIndex[ST_RLHAND]);
                if (weapon) {
                    const int ammunitionIndex = user->m_data->m_nSlotIndex[ST_LHAND];
                    if (ammunitionIndex == -1) {
                        CItem* const bolt = user->m_inv.GetBoltByCrystal(weapon->m_const->GetCrystalType());
                        if (bolt == nullptr) {
                            user->m_data->m_nSlotIndex[ST_LHAND] = -1;
                        } else {
                            user->m_data->m_nSlotIndex[ST_LHAND] = bolt->m_data->index;
                            user->m_inv.RemoveSlotInfo(bolt);
                        }
                    } else {
                        user->m_data->m_iLastArrowIndex = ammunitionIndex;
                    }
                }
            }

            runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
            return true;
        }

        return false;
    }();
    if (loadComplete) {
        db.RequestGetMacro(user);
        db.RequestLoadInZoneRestriction(user);
        db.LoadUserBookmark(user);
        db.LoadPeriodicItemInfo(user);
        db.LoadPeriodicShapedShiftedItemInfo(user);
        db.RequestLoadUserInzonePoint(*user);
    }
    return false;
}

bool DepositWarehouseItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a6804. The paired current CacheD producer is
    // primary request 16 / reply 12 and returns repeated dQ item balances.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x8f3,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[warehouse] Cannot apply a deposit reply: user server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == -4) {
        user->GetSocket()->Send("c", 0x43);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_SLOTS_FULL);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->GetSocket()->SendSystemMessage(SystemMessageId::WAREHOUSE_SLOTS_FULL);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return false;
    }

    if (result < 1) {
        user->GetSocket()->Send("cd", 0x43, result);
        if (result == -1) {
            user->GetSocket()->SendSystemMessage(SystemMessageId::WAREHOUSE_DEPOSIT_REJECTED_FOR_INSUFFICIENT_FUNDS);
        }
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return false;
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        for (int index = 0; index < result; ++index) {
            std::int32_t itemDatabaseId = 0;
            std::int64_t itemAmount = 0;
            packet = ::Disassemble(packet, "dQ", &itemDatabaseId, &itemAmount);

            CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr) {
                continue;
            }

            if (itemAmount < 1) {
                static_cast<void>(user->m_inv.Pop(item->m_data->index, false));
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                item->Delete();
                continue;
            }

            {
                L2_GUARD(*item->m_lock);
                if (!item->SetAmount(itemAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x940,
                    //     static_cast<long long>(item->m_data->m_nCount),
                    //     static_cast<long long>(itemAmount))
                    L2_LOG_ERROR(
                        Log, u"[warehouse] Cannot set deposited item %d amount from %lld to %lld.", itemDatabaseId,
                        static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemAmount)
                    );
                }
            }
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
        }
    }

    {
        L2_GUARD(*user->m_lock);
        if (user->m_pEconomy != nullptr) {
            user->GetSocket()->Send("c", 0x43);
            user->ReleaseEconomy();
        }
    }
    user->SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_SUCCEEDED);
    return false;
}

bool ReplyDepositWarehouseItemsEx(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c8c38. The paired current CacheD producer is
    // primary request 147 / reply 0x7f and returns complete item records.
    L2_GUARD_FUNCTION();

    std::int32_t warehouseClassType = 0;
    std::int32_t warehouseOwnerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "ddddd", &warehouseClassType, &warehouseOwnerId, &userServerId, &result, &itemCount);
    static_cast<void>(warehouseClassType);
    static_cast<void>(warehouseOwnerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3330,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[warehouse] Cannot apply an extended deposit reply: user server ID %d is not active.", userServerId
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result < 1) {
        if (itemCount == 0) {
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
            return false;
        }

        {
            L2_WRITE_GUARD(user->m_inv);
            for (int index = 0; index < itemCount; ++index) {
                CItemDisassemblerFromCacheD itemData;
                packet = itemData.DisassembleItemInfo(packet);

                CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d]deposit item error. Cannot find item. "
                    //     L"[%d]-[%d][%d][%I64d][%d][%d][%d][%d][%d]",
                    //     L"DBSocket.cpp",
                    //     0x33e4,
                    //     user->GetDBID(),
                    //     itemData.m_nDBID,
                    //     itemData.m_nClassID,
                    //     static_cast<long long>(itemData.m_nAmount),
                    //     itemData.m_nEnchanted,
                    //     itemData.m_nDamaged,
                    //     itemData.m_nIsBlessed,
                    //     itemData.m_nIdent,
                    //     itemData.m_nWished)
                    L2_LOG_ERROR(
                        Log,
                        u"[warehouse] Deposited item %d is missing from character %d inventory; "
                        u"class %d, amount %lld, enchant %d, damaged %d, blessed %d, identified %d, wished %d.",
                        itemData.m_nDBID, user->GetDBID(), itemData.m_nClassID, static_cast<long long>(itemData.m_nAmount),
                        itemData.m_nEnchanted, itemData.m_nDamaged, itemData.m_nIsBlessed, itemData.m_nIdent,
                        itemData.m_nWished
                    );
                    continue;
                }

                bool deleteItem = false;
                if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                    if (!item->SetAmount(0)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                        //     L"DBSocket.cpp",
                        //     0x33d8,
                        //     static_cast<long long>(
                        //         item->m_data->m_nCount),
                        //     static_cast<long long>(0))
                        L2_LOG_ERROR(
                            Log, u"[warehouse] Cannot clear non-stackable item %d amount from %lld to %lld.",
                            itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(0)
                        );
                    }
                    deleteItem = true;
                } else {
                    const std::int64_t currentAmount = item->m_data->m_nCount;
                    if (itemData.m_nAmount < currentAmount) {
                        const std::int64_t delta = runtime::NegateInt64Wrapping(itemData.m_nAmount);
                        if (!item->AddAmount(delta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                            //     L"DBSocket.cpp",
                            //     0x33b1,
                            //     static_cast<long long>(
                            //         item->m_data->m_nCount),
                            //     static_cast<long long>(delta))
                            L2_LOG_ERROR(
                                Log, u"[warehouse] Cannot adjust deposited item %d amount from %lld by %lld.",
                                itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                                static_cast<long long>(delta)
                            );
                        }
                        user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    } else {
                        if (itemData.m_nAmount == currentAmount) {
                            if (!item->SetAmount(0)) {
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                                //     L"DBSocket.cpp",
                                //     0x33ba,
                                //     static_cast<long long>(
                                //         item->m_data->m_nCount),
                                //     static_cast<long long>(0))
                                L2_LOG_ERROR(
                                    Log, u"[warehouse] Cannot clear deposited item %d amount from %lld to %lld.",
                                    itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                                    static_cast<long long>(0)
                                );
                            }
                        } else {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d]deposit item error. Count is invalid. "
                            //     L"[%d][%I64d]-[%d][%d][%I64d][%d][%d][%d][%d][%d]",
                            //     L"DBSocket.cpp",
                            //     0x33c5,
                            //     user->GetDBID(),
                            //     static_cast<long long>(currentAmount),
                            //     itemData.m_nDBID,
                            //     itemData.m_nClassID,
                            //     static_cast<long long>(itemData.m_nAmount),
                            //     itemData.m_nEnchanted,
                            //     itemData.m_nDamaged,
                            //     itemData.m_nIsBlessed,
                            //     itemData.m_nIdent,
                            //     itemData.m_nWished)
                            L2_LOG_ERROR(
                                Log,
                                u"[warehouse] Deposit reply amount %lld exceeds inventory amount %lld "
                                u"for character %d item %d (class %d, enchant %d, damaged %d, blessed %d, identified %d, "
                                u"wished %d).",
                                static_cast<long long>(itemData.m_nAmount), static_cast<long long>(currentAmount),
                                user->GetDBID(), itemData.m_nDBID, itemData.m_nClassID, itemData.m_nEnchanted,
                                itemData.m_nDamaged, itemData.m_nIsBlessed, itemData.m_nIdent, itemData.m_nWished
                            );
                            if (!item->SetAmount(0)) {
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                                //     L"DBSocket.cpp",
                                //     0x33cb,
                                //     static_cast<long long>(
                                //         item->m_data->m_nCount),
                                //     static_cast<long long>(0))
                                L2_LOG_ERROR(
                                    Log, u"[warehouse] Cannot clear inconsistent item %d amount from %lld to %lld.",
                                    itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                                    static_cast<long long>(0)
                                );
                            }
                        }
                        deleteItem = true;
                    }
                }

                if (deleteItem) {
                    static_cast<void>(user->m_inv.Pop(item->m_data->index, false));
                    user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                    item->Delete();
                }
            }
        }

        {
            L2_GUARD(*user->m_lock);
            if (user->m_pEconomy != nullptr) {
                user->GetSocket()->Send("c", 0x43);
                user->ReleaseEconomy();
            }
        }
        user->SendItemList(true, true, nullptr);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_SUCCEEDED);
        return false;
    }

    if (result == 2) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::WAREHOUSE_SLOTS_FULL);
    }
    user->GetSocket()->Send("c", 0x43);
    {
        L2_GUARD(*user->m_lock);
        user->ReleaseEconomy();
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
    return false;
}

bool LoadWarehouseItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c9738, CacheD reply opcode 9. Unlike the Ex
    // route, its context is kept across pages and returned
    // to the fixed ring only on a terminal package.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t warehouseId = 0;
    std::int32_t warehouseClassType = 0;
    std::int32_t itemListGetType = 0;
    std::int32_t remain = 0;
    std::int32_t contextId = 0;
    std::int32_t warehouseMemoryId = 0;
    std::int32_t totalItemCount = 0;
    std::int32_t currentPackageItemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddddd", &userServerId, &warehouseId, &warehouseClassType, &itemListGetType, &remain, &contextId,
        &warehouseMemoryId, &totalItemCount, &currentPackageItemCount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x79f,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[warehouse] Cannot load warehouse items: user server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (itemListGetType == 1) {
        CPackageItemPacketManager packageManager;
        std::optional<CSPointer<CItem>> itemStorage;
        std::int64_t adenaAmount;
        const bool itemsFound = [&] {
            L2_READ_GUARD(user->m_inv);
            for (CSharedItemData* data = user->m_inv.GetFirst(); data != nullptr; itemStorage.reset(), data = data->Next()) {
                auto& item = itemStorage.emplace(CItemFactory::Inst().FindObjectSP(data->index));
                if (!item) {
                    return false;
                }

                const bool ordinaryCandidate = !item->IsAccessary() && !item->IsWeapon() && !item->IsArmor() &&
                    item->m_const->m_EtcitemType != 2 && item->m_const->m_EtcitemType != 0x10;
                const bool equipmentCandidate = ordinaryCandidate || !user->IsEquipedItem(item->m_data->index);
                const bool optionCandidate = warehouseClassType == WHC_NORMAL ||
                    (((item->m_itemOption.itemOptionKey.variationOption1 < 1) ||
                         (item->m_itemOption.itemOptionKey.variationOption2 < 1)) &&
                        !item->IsDuration());
                if (!equipmentCandidate || !item->CanKeep(static_cast<WareHouseClass>(warehouseClassType)) ||
                    item->IsQuestItem() || (item->IsEtcItem() && !item->CastEtcItem()->m_bPetCollarTradable) ||
                    !optionCandidate) {
                    continue;
                }

                packageManager.CheckLimitPackageItemPacketDataSize();
                CItemAssembler assembler(*user, *item.get(), false);
                const int assembled =
                    packageManager.PackageAssembleItem(&assembler) + packageManager.PackageAssemble("d", item->GetDBID());
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"DBSocket.cpp",
                    //     0x7f0,
                    //     assembled,
                    //     packageManager.GetTotalPackageItemLength())
                    L2_LOG_ERROR(
                        Log, u"[warehouse] Cannot assemble deposit-list item: result %d, packet length %d.", assembled,
                        packageManager.GetTotalPackageItemLength()
                    );
                    continue;
                }
                packageManager.SetAssembleItemLengthPerItem(assembled);
                packageManager.IncreasePackageItemCount();
            }
            adenaAmount = user->m_inv.GetAdenaAmount();
            return true;
        }();
        if (!itemsFound) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item. [%d]",
            //     "DBSocket.cpp",
            //     0x7b1,
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log, u"[warehouse] Cannot build the deposit list because character %d has an unresolved inventory item.",
                user->GetDBID()
            );
            return false;
        }

        std::int32_t consumeItemCount = 0;
        cursor = ::Disassemble(cursor, "d", &consumeItemCount);
        CPacketHelperEx<20480> consumeItems;
        for (int index = 0; index < consumeItemCount; ++index) {
            std::int32_t itemClassId = 0;
            cursor = ::Disassemble(cursor, "d", &itemClassId);
            consumeItems.Assemble("d", itemClassId);
        }

        userSocket->Send(
            "cchQdhbd", S_WAREHOUSE_DEPOSIT_LIST, 1, static_cast<short>(warehouseClassType), adenaAmount, totalItemCount,
            static_cast<short>(consumeItems.GetAssembledCount()), consumeItems.GetLength(), consumeItems.GetPacket(),
            packageManager.GetTotalPackageItemCount()
        );
        packageManager.SendPacket_PackageItemData(userSocket, S_WAREHOUSE_DEPOSIT_LIST, PackageItemPacket::PIPT_ITEM_INFO);
        return false;
    }

    if (itemListGetType != 0) {
        return false;
    }

    CCacheContextManager& contextManager = Singleton<CCacheContextManager>::inst();
    if (totalItemCount < 1) {
        if (!contextManager.ReleaseContext(contextId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid context id[%x]",
            //     L"DBSocket.cpp",
            //     0x81f,
            //     contextId)
            L2_LOG_ERROR(
                Log, u"[warehouse] Cannot finish an empty warehouse-item reply: context 0x%x is invalid.", contextId
            );
            return false;
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        userSocket->SendSystemMessage(SystemMessageId::NO_DEPOSITED_WAREHOUSE_ITEMS);
        if (remain > 0) {
            userSocket->SendSystemMessage(SystemMessageId::PACKAGE_AT_ANOTHER_WAREHOUSE);
        }
        return false;
    }

    const bool firstPackage =
        runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0) == 0;
    if (firstPackage) {
        CPacketHelperEx<20480> header;
        header.Assemble(
            "cchQdd", S_WAREHOUSE_WITHDRAW_LIST, 1, static_cast<short>(WHC_NORMAL), user->m_inv.GetAdenaAmount(),
            user->m_inv.GetInventoryCount(), totalItemCount
        );
        user->PushPackageItemPacket(header);
    }

    CWareHouseWithdraw* contextWarehouse = nullptr;
    if (!contextManager.GetContextWithoutRelease(contextId, "p", &contextWarehouse)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x843,
        //     contextId)
        L2_LOG_ERROR(Log, u"[warehouse] Cannot load warehouse items: context 0x%x is invalid.", contextId);
        return false;
    }

    // Preserve current target asymmetry: acquisition 0x006c9f57. Invalid
    // ReleaseContext branches 0x006ca49d / 0x006ca61d jump to the epilogue
    // at 0x006ca6ca without unlocking. Other paths release at 0x006c9faf,
    // 0x006ca542 or 0x006ca6c2. A local guard would change those exits.
    user->m_inv.WriteLock(L"DBSocket.cpp", 0x845);
    if (contextWarehouse == nullptr || user->m_pEconomy == nullptr) {
        runtime::raw_atomic::AddI32(&user->m_lCurrentReceivedPackageItemCount, currentPackageItemCount);
        if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
            runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
                runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
            if (!contextManager.ReleaseContext(contextId)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid context id[%x]",
                //     L"DBSocket.cpp",
                //     0x84c,
                //     contextId)
                L2_LOG_ERROR(Log, u"[warehouse] Cannot release invalid warehouse context 0x%x.", contextId);
                return false;
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
            user->InitPackageItemPackets();
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] error. invalid warehouse.[%x] "
        //     L"char_id[%s][%d], nWarehouseID[%d] "
        //     L"nWarehouseClassType[%d] nWarehouseItemListGetType[%d] "
        //     L"nRemain[%d], nContextId[%d], nMemoryId[%d], "
        //     L"nItemCount[%d/%d]",
        //     "DBSocket.cpp",
        //     0x854,
        //     static_cast<unsigned int>(
        //         reinterpret_cast<std::uintptr_t>(contextWarehouse)),
        //     user->GetName(),
        //     user->GetDBID(),
        //     warehouseId,
        //     warehouseClassType,
        //     itemListGetType,
        //     remain,
        //     contextId,
        //     warehouseMemoryId,
        //     currentPackageItemCount,
        //     totalItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[warehouse] Missing warehouse state for character %ls (%d): "
        //     L"warehouse %d, class %d, list type %d, remaining %d, context 0x%x, "
        //     L"memory ID %d, package items %d/%d, context object %p.",
        //     user->GetName(),
        //     user->GetDBID(),
        //     warehouseId,
        //     warehouseClassType,
        //     itemListGetType,
        //     remain,
        //     contextId,
        //     warehouseMemoryId,
        //     currentPackageItemCount,
        //     totalItemCount,
        //     static_cast<void*>(contextWarehouse))
        L2_LOG_ERROR(
            Log,
            u"[warehouse] Missing warehouse state for character %ls (%d): "
            u"warehouse %d, class %d, list type %d, remaining %d, context 0x%x, "
            u"memory ID %d, package items %d/%d, context object %p.",
            user->GetName(), user->GetDBID(), warehouseId, warehouseClassType, itemListGetType, remain, contextId,
            warehouseMemoryId, currentPackageItemCount, totalItemCount, static_cast<void*>(contextWarehouse)
        );
        user->m_inv.WriteUnlock();
        return false;
    }

    if (user->m_pEconomy != contextWarehouse || contextWarehouse->MemoryObjectID() != warehouseMemoryId) {
        runtime::raw_atomic::AddI32(&user->m_lCurrentReceivedPackageItemCount, currentPackageItemCount);
        if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
            runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
                runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
            if (!contextManager.ReleaseContext(contextId)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid context id[%x]",
                //     L"DBSocket.cpp",
                //     0x861,
                //     contextId)
                L2_LOG_ERROR(Log, u"[warehouse] Cannot release mismatched warehouse context 0x%x.", contextId);
                return false;
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
            user->InitPackageItemPackets();
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] error. invalid warehouse.[%x] "
        //     L"char_id[%s][%d], nWarehouseID[%d] "
        //     L"nWarehouseClassType[%d] nWarehouseItemListGetType[%d] "
        //     L"nRemain[%d], nContextId[%d], nMemoryId[%d], "
        //     L"nItemCount[%d/%d]",
        //     "DBSocket.cpp",
        //     0x869,
        //     static_cast<unsigned int>(
        //         reinterpret_cast<std::uintptr_t>(contextWarehouse)),
        //     user->GetName(),
        //     user->GetDBID(),
        //     warehouseId,
        //     warehouseClassType,
        //     itemListGetType,
        //     remain,
        //     contextId,
        //     warehouseMemoryId,
        //     currentPackageItemCount,
        //     totalItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[warehouse] Warehouse state changed while loading character %ls (%d): "
        //     L"warehouse %d, class %d, list type %d, remaining %d, context 0x%x, "
        //     L"memory ID %d, package items %d/%d, context object %p.",
        //     user->GetName(),
        //     user->GetDBID(),
        //     warehouseId,
        //     warehouseClassType,
        //     itemListGetType,
        //     remain,
        //     contextId,
        //     warehouseMemoryId,
        //     currentPackageItemCount,
        //     totalItemCount,
        //     static_cast<void*>(contextWarehouse))
        L2_LOG_ERROR(
            Log,
            u"[warehouse] Warehouse state changed while loading character %ls (%d): "
            u"warehouse %d, class %d, list type %d, remaining %d, context 0x%x, "
            u"memory ID %d, package items %d/%d, context object %p.",
            user->GetName(), user->GetDBID(), warehouseId, warehouseClassType, itemListGetType, remain, contextId,
            warehouseMemoryId, currentPackageItemCount, totalItemCount, static_cast<void*>(contextWarehouse)
        );
        user->m_inv.WriteUnlock();
        return false;
    }

    L2_ADD_REF(contextWarehouse, ObjectRefType::Anonymous);
    user->m_inv.WriteUnlock();

    // Current source repeats the first-package publication after the context
    // check. Normally the earlier compare-exchange already owns the header.
    const bool firstValidatedPackage =
        runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0) == 0;
    if (firstValidatedPackage) {
        CPacketHelperEx<20480> header;
        header.Assemble(
            "cchQdd", S_WAREHOUSE_WITHDRAW_LIST, 1, static_cast<short>(WHC_NORMAL), user->m_inv.GetAdenaAmount(),
            user->m_inv.GetInventoryCount(), totalItemCount
        );
        user->PushPackageItemPacket(header);
    }

    CPacketHelperEx<20480> itemPacket;
    CPacketHelperEx<20480> consumeItems;
    for (int index = 0; index < currentPackageItemCount; ++index) {
        CItemAssemblerWarehouseList assembler;
        cursor = assembler.DisassembleFromDB(*user, cursor);
        contextWarehouse->AddItemId(assembler.GetDBId(), assembler.GetClassId());

        CItem* const inventoryItem = user->m_inv.GetFirstItemByClassID(assembler.GetClassId(), false, false);
        if (inventoryItem != nullptr &&
            (inventoryItem->m_data->m_ItemConsumeType == ICT_ASSET ||
                inventoryItem->m_data->m_ItemConsumeType == ICT_STACKABLE)) {
            consumeItems.Assemble("d", assembler.GetClassId());
        }

        const int itemLength = assembler.AssembleItemInfo(itemPacket);
        double agathionEnergy = 0.0;
        cursor = ::Disassemble(cursor, "f", &agathionEnergy);
        CItem* const prototype = static_cast<CItem*>(g_ObjectDB.GetObjectW(assembler.GetClassId(), false));
        int energyLength = 0;
        if (prototype == nullptr || prototype->m_const->m_dAgathionMaxEnergy <= 0.0) {
            energyLength = itemPacket.Assemble("dd", 0, 0);
        } else {
            energyLength = itemPacket.Assemble(
                "dd", runtime::Win64CvttDoubleToInt32(agathionEnergy + 0.5),
                runtime::Win64CvttDoubleToInt32(prototype->m_const->m_dAgathionMaxEnergy + 0.5)
            );
        }
        if (itemLength + energyLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assemble Error at file[%s], line[%d], "
            //     L"assemble result = [%d], total length = [%d]",
            //     L"DBSocket.cpp",
            //     0x8b8,
            //     itemLength + energyLength,
            //     itemPacket.GetLength())
            L2_LOG_ERROR(
                Log, u"[warehouse] Item packet assembly failed: result %d, accumulated length %d.",
                itemLength + energyLength, itemPacket.GetLength()
            );
        }
        runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
    }

    CPacketHelperEx<20480> footer;
    footer.Assemble(
        "cchbddb", S_WAREHOUSE_WITHDRAW_LIST, 2, static_cast<short>(consumeItems.GetAssembledCount()),
        consumeItems.GetLength(), consumeItems.GetPacket(), totalItemCount, currentPackageItemCount, itemPacket.GetLength(),
        itemPacket.GetPacket()
    );
    user->PushPackageItemPacket(footer);
    L2_RELEASE(contextWarehouse, ObjectRefType::Anonymous);

    if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
        runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
            runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
        if (!contextManager.ReleaseContext(contextId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid context id[%x]",
            //     L"DBSocket.cpp",
            //     0x8d2,
            //     contextId)
            L2_LOG_ERROR(Log, u"[warehouse] Cannot release completed warehouse context 0x%x.", contextId);
            return false;
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        user->SendPackageItemPackets();
    }
    return false;
}

bool ReplyWarehouseItemsEx(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c7e78, CacheD reply opcode 125. The packet has
    // two literal modes: mode 1 publishes the player's deposit candidates.
    // mode 0 streams a CWareHouseWithdraw snapshot in package frames.
    L2_GUARD_FUNCTION();

    std::int32_t warehouseClassType = 0;
    std::int32_t warehouseOwnerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t warehouseMemoryId = 0;
    std::int32_t itemListGetType = 0;
    std::int32_t totalItemCount = 0;
    std::int32_t currentPackageItemCount = 0;
    const unsigned char* cursor = ::Disassemble(
        packet, "ddddddd", &warehouseClassType, &warehouseOwnerId, &userServerId, &warehouseMemoryId, &itemListGetType,
        &totalItemCount, &currentPackageItemCount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3120,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[warehouse] Cannot apply an extended warehouse-list reply: user server ID %d is not active.", userServerId
        );
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (itemListGetType == 1) {
        CPackageItemPacketManager packageManager;
        std::optional<CSPointer<CItem>> itemStorage;
        std::int64_t adenaAmount;
        const bool itemsFound = [&] {
            L2_READ_GUARD(user->m_inv);
            for (CSharedItemData* data = user->m_inv.GetFirst(); data != nullptr; itemStorage.reset(), data = data->Next()) {
                auto& item = itemStorage.emplace(CItemFactory::Inst().FindObjectSP(data->index));
                if (!item) {
                    return false;
                }

                const bool ordinaryCandidate = !item->IsAccessary() && !item->IsWeapon() && !item->IsArmor() &&
                    item->m_const->m_EtcitemType != 2 && item->m_const->m_EtcitemType != 0x10;
                const bool equipmentCandidate = ordinaryCandidate || !user->IsEquipedItem(item->m_data->index);
                const bool optionCandidate = warehouseClassType == WHC_NORMAL ||
                    (((item->m_itemOption.itemOptionKey.variationOption1 < 1) ||
                         (item->m_itemOption.itemOptionKey.variationOption2 < 1)) &&
                        !item->IsDuration() &&
                        (warehouseClassType == WHC_NORMAL || !item->IsPeriodicShapeShiftedItem(user)));
                if (!equipmentCandidate || !item->CanKeep(static_cast<WareHouseClass>(warehouseClassType)) ||
                    item->IsQuestItem() || (item->IsEtcItem() && !item->CastEtcItem()->m_bPetCollarTradable) ||
                    item->m_data->GetLocked() != 0 || !optionCandidate) {
                    continue;
                }

                packageManager.CheckLimitPackageItemPacketDataSize();
                CItemAssembler assembler(*user, *item.get(), false);
                const int assembled =
                    packageManager.PackageAssembleItem(&assembler) + packageManager.PackageAssemble("d", item->GetDBID());
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"DBSocket.cpp",
                    //     0x3182,
                    //     assembled,
                    //     packageManager.GetTotalPackageItemLength())
                    L2_LOG_ERROR(
                        Log, u"[warehouse] Cannot assemble extended deposit-list item: result %d, packet length %d.",
                        assembled, packageManager.GetTotalPackageItemLength()
                    );
                    continue;
                }
                packageManager.SetAssembleItemLengthPerItem(assembled);
                packageManager.IncreasePackageItemCount();
            }
            adenaAmount = user->m_inv.GetAdenaAmount();
            return true;
        }();
        if (!itemsFound) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item. [%d]",
            //     "DBSocket.cpp",
            //     0x3133,
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log,
                u"[warehouse] Cannot build the extended deposit list because character %d has an unresolved inventory item.",
                user->GetDBID()
            );
            return false;
        }

        std::int32_t consumeItemCount = 0;
        cursor = ::Disassemble(cursor, "d", &consumeItemCount);
        CPacketHelperEx<20480> consumeItems;
        for (int index = 0; index < consumeItemCount; ++index) {
            std::int32_t itemClassId = 0;
            cursor = ::Disassemble(cursor, "d", &itemClassId);
            consumeItems.Assemble("d", itemClassId);
        }

        userSocket->Send(
            "cchQdhbd", S_WAREHOUSE_DEPOSIT_LIST, 1, static_cast<short>(warehouseClassType), adenaAmount, totalItemCount,
            static_cast<short>(consumeItems.GetAssembledCount()), consumeItems.GetLength(), consumeItems.GetPacket(),
            packageManager.GetTotalPackageItemCount()
        );
        packageManager.SendPacket_PackageItemData(userSocket, S_WAREHOUSE_DEPOSIT_LIST, PackageItemPacket::PIPT_ITEM_INFO);
        return false;
    }

    if (itemListGetType != 0) {
        return false;
    }
    if (totalItemCount < 0) {
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Invalid PacakageItemCount[%d/%d]",
        //     "DBSocket.cpp",
        //     0x31b4,
        //     totalItemCount,
        //     currentPackageItemCount)
        L2_LOG_ERROR(
            Log, u"[warehouse] Invalid package item counts in warehouse reply: total %d, current package %d.",
            totalItemCount, currentPackageItemCount
        );
        return false;
    }
    if (totalItemCount == 0) {
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        userSocket->SendSystemMessage(SystemMessageId::NO_DEPOSITED_WAREHOUSE_ITEMS);
        return false;
    }

    const bool firstPackage =
        runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0) == 0;
    if (firstPackage) {
        CPacketHelperEx<20480> header;
        header.Assemble(
            "cchQdd", S_WAREHOUSE_WITHDRAW_LIST, 1, static_cast<short>(warehouseClassType), user->m_inv.GetAdenaAmount(),
            user->m_inv.GetInventoryCount(), totalItemCount
        );
        user->PushPackageItemPacket(header);
    }

    CWareHouseWithdraw* warehouse = nullptr;
    int failureLine;
    {
        L2_WRITE_GUARD(user->m_inv);
        CEconomy* const economy = user->m_pEconomy;
        failureLine = 0;
        if (economy == nullptr) {
            failureLine = 0x31df;
        } else if (!economy->IsWarehouse()) {
            failureLine = 0x31f2;
        } else {
            warehouse = static_cast<CWareHouseWithdraw*>(economy);
            if (warehouse->MemoryObjectID() != warehouseMemoryId) {
                failureLine = 0x3204;
            } else if (warehouse->m_nWarehouseType != warehouseClassType || warehouse->m_nOwnerID != warehouseOwnerId) {
                failureLine = 0x3228;
            }
        }

        if (failureLine != 0) {
            runtime::raw_atomic::AddI32(&user->m_lCurrentReceivedPackageItemCount, currentPackageItemCount);
            if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
                runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
                    runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
                runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
                runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
                runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
                user->InitPackageItemPackets();
            }
        } else {
            L2_ADD_REF(warehouse, ObjectRefType::Anonymous);
        }
    }
    if (failureLine != 0) {
        if (failureLine == 0x31df) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] No warehouse. char[%d]",
            //     "DBSocket.cpp",
            //     failureLine,
            //     user->GetDBID())
            L2_LOG_ERROR(Log, u"[warehouse] Character %d has no active warehouse transaction.", user->GetDBID());
        } else if (failureLine == 0x31f2) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Warehouse is not warehouse. char[%d]",
            //     "DBSocket.cpp",
            //     failureLine,
            //     user->GetDBID())
            L2_LOG_ERROR(
                Log, u"[warehouse] Character %d economy transaction is not a warehouse transaction.", user->GetDBID()
            );
        } else if (failureLine == 0x3204) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] invalid warehouse ex. [%d][%d]",
            //     "DBSocket.cpp",
            //     failureLine,
            //     warehouse->MemoryObjectID(),
            //     warehouseMemoryId)
            L2_LOG_ERROR(
                Log, u"[warehouse] Warehouse object ID mismatch: active %d, reply %d.", warehouse->MemoryObjectID(),
                warehouseMemoryId
            );
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] Type is diff. char[%d], [%d][%d]",
            //     "DBSocket.cpp",
            //     failureLine,
            //     user->GetDBID(),
            //     warehouseClassType,
            //     warehouseOwnerId)
            L2_LOG_ERROR(
                Log, u"[warehouse] Warehouse owner/type mismatch for character %d: reply class %d, owner %d.",
                user->GetDBID(), warehouseClassType, warehouseOwnerId
            );
        }
        return false;
    }

    CPacketHelperEx<20480> itemPacket;
    CPacketHelperEx<20480> consumeItems;
    for (int index = 0; index < currentPackageItemCount; ++index) {
        CItemAssemblerWarehouseList assembler;
        cursor = assembler.DisassembleFromDB(*user, cursor);
        warehouse->AddItemId(assembler.GetDBId(), assembler.GetClassId());

        CItem* const inventoryItem = user->m_inv.GetFirstItemByClassID(assembler.GetClassId(), false, false);
        if (inventoryItem != nullptr &&
            (inventoryItem->m_data->m_ItemConsumeType == ICT_ASSET ||
                inventoryItem->m_data->m_ItemConsumeType == ICT_STACKABLE)) {
            consumeItems.Assemble("d", assembler.GetClassId());
        }

        const int itemLength = assembler.AssembleItemInfo(itemPacket);
        double agathionEnergy = 0.0;
        cursor = ::Disassemble(cursor, "f", &agathionEnergy);
        CItem* const prototype = static_cast<CItem*>(g_ObjectDB.GetObjectW(assembler.GetClassId(), false));
        int energyLength = 0;
        if (prototype == nullptr || prototype->m_const->m_dAgathionMaxEnergy <= 0.0) {
            energyLength = itemPacket.Assemble("dd", 0, 0);
        } else {
            energyLength = itemPacket.Assemble(
                "dd", runtime::Win64CvttDoubleToInt32(agathionEnergy + 0.5),
                runtime::Win64CvttDoubleToInt32(prototype->m_const->m_dAgathionMaxEnergy + 0.5)
            );
        }
        if (itemLength + energyLength < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assemble Error at file[%s], line[%d], "
            //     L"assemble result = [%d], total length = [%d]",
            //     L"DBSocket.cpp",
            //     0x3261,
            //     itemLength + energyLength,
            //     itemPacket.GetLength())
            L2_LOG_ERROR(
                Log, u"[warehouse] Cannot assemble withdrawal-list item: result %d, packet length %d.",
                itemLength + energyLength, itemPacket.GetLength()
            );
        }
        runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
    }

    CPacketHelperEx<20480> footer;
    footer.Assemble(
        "cchbddb", S_WAREHOUSE_WITHDRAW_LIST, 2, static_cast<short>(consumeItems.GetAssembledCount()),
        consumeItems.GetLength(), consumeItems.GetPacket(), totalItemCount, currentPackageItemCount, itemPacket.GetLength(),
        itemPacket.GetPacket()
    );
    user->PushPackageItemPacket(footer);
    L2_RELEASE(warehouse, ObjectRefType::Anonymous);

    if (runtime::raw_atomic::LoadI32Relaxed(&user->m_lPackageItemPacketJobFlag) != 0 &&
        runtime::raw_atomic::LoadI32Relaxed(&user->m_lTotalPackageItemCount) ==
            runtime::raw_atomic::LoadI32Relaxed(&user->m_lCurrentReceivedPackageItemCount)) {
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        user->SendPackageItemPackets();
    }
    return false;
}

bool ReplyLoadWarehouseItemList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c6888. The paired current CacheD producer is
    // primary request 0x5c / reply 0x4f and publishes package-framed item
    // records for the character warehouse list.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t userServerId = 0;
    std::int32_t warehouseType = 0;
    std::int32_t totalItemCount = 0;
    std::uint32_t currentPackageItemCount = 0;
    packet = ::Disassemble(
        packet, "ddddd", &characterId, &userServerId, &warehouseType, &totalItemCount, &currentPackageItemCount
    );
    static_cast<void>(warehouseType);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2261,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }
    if (user->GetSocket() == nullptr) {
        return false;
    }

    CSPointer<User> databaseUser =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!databaseUser) {
        return false;
    }

    const bool firstPackage =
        runtime::raw_atomic::CompareExchangeI32(&databaseUser->m_lTotalPackageItemCount, totalItemCount, 0) == 0;
    if (firstPackage) {
        CPacketHelperEx<20480> header;
        header.Assemble("ccSQd", 0x9b, 1, databaseUser->GetName(), databaseUser->m_inv.GetAdenaAmount(), totalItemCount);
        user->PushPackageItemPacket(header);
    }

    CPacketHelperEx<20480> itemPacket;
    for (std::uint32_t index = 0; index < currentPackageItemCount; ++index) {
        CItemAssemblerWarehouseList assembler;
        packet = assembler.DisassembleFromDB(*databaseUser.get(), packet);
        const int assembled = assembler.AssembleItemInfo(itemPacket);
        if (assembled < 1) {
            runtime::raw_atomic::ExchangeI32(&databaseUser->m_lTotalPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&databaseUser->m_lCurrentReceivedPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&databaseUser->m_lPackageItemPacketJobFlag, 0);
            user->InitPackageItemPackets();
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assembe Error at file[%s], line[%d], "
            //     L"assemble result = [%d], total length = [%d]",
            //     L"DBSocket.cpp",
            //     0x229a,
            //     assembled,
            //     itemPacket.GetLength())
            L2_LOG_ERROR(
                Log, u"[warehouse] Cannot assemble another character's warehouse item: result %d, packet length %d.",
                assembled, itemPacket.GetLength()
            );
            return false;
        }
        runtime::raw_atomic::IncrementI32(&databaseUser->m_lCurrentReceivedPackageItemCount);
    }

    if (totalItemCount != 0) {
        CPacketHelperEx<20480> footer;
        footer.Assemble(
            "ccddb", 0x9b, 2, totalItemCount, currentPackageItemCount, itemPacket.GetLength(), itemPacket.GetPacket()
        );
        user->PushPackageItemPacket(footer);
    }

    if (runtime::raw_atomic::LoadI32Relaxed(&databaseUser->m_lPackageItemPacketJobFlag) != 0 &&
        runtime::raw_atomic::LoadI32Relaxed(&databaseUser->m_lTotalPackageItemCount) ==
            runtime::raw_atomic::LoadI32Relaxed(&databaseUser->m_lCurrentReceivedPackageItemCount)) {
        runtime::raw_atomic::ExchangeI32(&databaseUser->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&databaseUser->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&databaseUser->m_lPackageItemPacketJobFlag, 0);
        user->SendPackageItemPackets();
    }
    return false;
}

bool ManipulateItemPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c5f58. The paired current CacheD primary
    // request/reply route is 0x1e/0x16. Unlike RP_EX 0xf0 this legacy reply
    // carries only the user sid, AtomicJob id and the two record counts.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t atomicJobId = -1;
    std::int32_t etcManipulationCount = 0;
    std::int32_t itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dddd", &userServerId, &atomicJobId, &etcManipulationCount, &itemCount);

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1071,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[inventory] Cannot apply item changes: user server ID %d is not active.", userServerId);
        if (atomicJob != nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[sid:%d]'s AtomicJob is ignored. "
            //     L"AtomicJobType[%d]",
            //     L"DBSocket.cpp",
            //     0x1075,
            //     userServerId,
            //     static_cast<int>(atomicJob->eType))
            L2_LOG_ERROR(
                Log, u"[inventory] Discarding atomic job type %d because user server ID %d is not active.",
                static_cast<int>(atomicJob->eType), userServerId
            );
        }
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (itemCount < 0 || etcManipulationCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[inventory] Disconnecting '%s' (%d): database item data is invalid.",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_ERROR(
            Log, u"[inventory] Disconnecting '%s' (%d): database item data is invalid.", user->GetName(),
            static_cast<int>(user->GetDBID())
        );
        user->StopMove();
        CUserSocket* const userSocket = user->GetSocket();
        if (userSocket != nullptr) {
            userSocket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }

    if (itemCount == 0 && etcManipulationCount == 0) {
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }

    cursor = L2EtcMani::ParseEtcMani(etcManipulationCount, cursor);
    while (itemCount-- > 0) {
        CItemDisassemblerItemMani itemData;
        cursor = itemData.DisassembleItemInfo(cursor);

        if (itemData.m_nIndex == 1) {
            CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CreateItem Failed",
                //     L"DBSocket.cpp",
                //     0x11d1)
                L2_LOG_ERROR(
                    Log, u"[inventory] Cannot create item class %d for database item %d.", itemData.m_nClassID,
                    itemData.m_nDBID
                );
                continue;
            }

            item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
            if (!item->SetAmount(itemData.m_nChangeAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                //     L"DBSocket.cpp",
                //     0x1194,
                //     item->m_data->m_nCount,
                //     itemData.m_nChangeAmount)
                L2_LOG_ERROR(
                    Log, u"[inventory] Cannot initialize item %d amount from %lld to %lld.", itemData.m_nDBID,
                    static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nChangeAmount)
                );
            }
            item->SetEnchanted(itemData.m_nEnchanted);
            item->m_Useable = user->CheckUseable(item);
            item->m_data->m_nDamaged = itemData.m_nDamaged;
            item->m_data->m_bBlessed = itemData.m_nIsBlessed;
            item->SetVariation(
                ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
            );
            item->SetAttribute(itemData.m_Attribute);
            if (item->IsDuration()) {
                db.UpdateItemDuration(*user, static_cast<unsigned int>(itemData.m_nDBID), item->GetDuration());
            }
            if (item->m_const->m_period > 0) {
                user->OnGetPeriodicItem(item);
            }
            if (itemData.m_nShapeShiftedClassID > 0) {
                item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
                if (itemData.m_nShapeShiftedPeriod >= 0) {
                    user->OnGetPeriodicShapeShiftedItem(item, itemData.m_nShapeShiftedPeriod);
                    user->SendPeriodicShapeShiftedItemList();
                }
            }
            if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
                item->SetAgathionEnergy(item->m_const->m_dAgathionMaxEnergy);
                user->SendBrAgathionEnergyInfo();
                db.RequestSaveItemAgathionEnergy(
                    user, static_cast<int>(item->m_data->m_nId), static_cast<int>(item->GetDBID()), item->GetAgathionEnergy()
                );
            }
            item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
            item->m_data->SetLocked(itemData.m_hLocked);
            item->m_data->SetUseCount(itemData.m_hUseCount);
            if (!user->m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBSocket.cpp",
                //     0x11c4)
                L2_LOG_ERROR(
                    Log, "[inventory] Item insertion failed while applying database item %d for user %u.", itemData.m_nDBID,
                    user->GetDBID()
                );
            }
            user->AutoEquipArrowOrBolt(item);
            user->m_inv.SetInventoryChanged(item, IUT_ADD);
            user->SendItemIncreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted());
            continue;
        }

        if (itemData.m_nIndex == 2) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBSocket.cpp",
                //     0x11eb,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[inventory] Cannot increase missing item %d for character %ls: "
                //     L"class %d, delta %lld, database amount %lld.",
                //     itemData.m_nDBID,
                //     user->GetName(),
                //     itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount),
                //     static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot increase missing item %d for character %ls: "
                    u"class %d, delta %lld, database amount %lld.",
                    itemData.m_nDBID, user->GetName(), itemData.m_nClassID, static_cast<long long>(itemData.m_nChangeAmount),
                    static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            {
                L2_GUARD(*item->m_lock);
                if (!item->AddAmount(itemData.m_nChangeAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBSocket.cpp",
                    //     0x11e0,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Cannot increase item %d amount from %lld by %lld.", itemData.m_nDBID,
                        static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nChangeAmount)
                    );
                }
            }
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            user->SendItemIncreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted());
            continue;
        }

        if (itemData.m_nIndex == 3) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBSocket.cpp",
                //     0x1204,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[inventory] Cannot decrease missing item %d for character %ls: "
                //     L"class %d, delta %lld, database amount %lld.",
                //     itemData.m_nDBID,
                //     user->GetName(),
                //     itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount),
                //     static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot decrease missing item %d for character %ls: "
                    u"class %d, delta %lld, database amount %lld.",
                    itemData.m_nDBID, user->GetName(), itemData.m_nClassID, static_cast<long long>(itemData.m_nChangeAmount),
                    static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            {
                L2_GUARD(*item->m_lock);
                if (!item->AddAmount(itemData.m_nChangeAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                    //     L"DBSocket.cpp",
                    //     0x11f9,
                    //     item->m_data->m_nCount,
                    //     itemData.m_nChangeAmount)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Cannot decrease item %d amount from %lld by %lld.", itemData.m_nDBID,
                        static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nChangeAmount)
                    );
                }
            }
            user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            user->SendItemDecreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true);
            continue;
        }

        if (itemData.m_nIndex == 4) {
            CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] pItemInv == NULL, bad. "
                //     L"[%s][%d][%d][%I64d][%I64d]",
                //     L"DBSocket.cpp",
                //     0x120f,
                //     user->GetName(),
                //     itemData.m_nDBID,
                //     itemData.m_nClassID,
                //     itemData.m_nChangeAmount,
                //     itemData.m_nAmount)
                // L2_LOG_ERROR(Log,
                //     L"[inventory] Cannot delete missing item %d for character %ls: "
                //     L"class %d, delta %lld, database amount %lld.",
                //     itemData.m_nDBID,
                //     user->GetName(),
                //     itemData.m_nClassID,
                //     static_cast<long long>(itemData.m_nChangeAmount),
                //     static_cast<long long>(itemData.m_nAmount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot delete missing item %d for character %ls: "
                    u"class %d, delta %lld, database amount %lld.",
                    itemData.m_nDBID, user->GetName(), itemData.m_nClassID, static_cast<long long>(itemData.m_nChangeAmount),
                    static_cast<long long>(itemData.m_nAmount)
                );
                continue;
            }
            user->SendItemDecreaseSystemMessage(itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true);
            if (itemData.m_nShapeShiftedClassID > 0 && itemData.m_nShapeShiftedPeriod >= 0) {
                user->OnRestorePeriodicShapeShiftedItem(item);
            }
            if (user->UnequipItem(item, true) == ST_NONE) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManipulateItemPacket Insert Queue "
                //     L"ItemDBID[%d], UserDBID[%d]",
                //     L"DBSocket.cpp",
                //     0x1225,
                //     item->GetDBID(),
                //     user->GetDBID())
                // Noise: ST_NONE means User::UnequipItem queued the work
                // while another item operation or the agathion is busy.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][%s][%d] ManipulateItemPacket Insert Queue "
                //     L"ItemDBID[%d], UserDBID[%d]",
                //     L"DBSocket.cpp",
                //     0x1225,
                //     item->GetDBID(),
                //     user->GetDBID())
            } else {
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                user->m_inv.Pop(item->m_data->index, false);
                item->Delete();
            }
            continue;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] manipulate item fail. unknown manipulate type[%d]",
        //     L"DBSocket.cpp",
        //     0x1235,
        //     itemData.m_nIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot apply item %d change: unknown manipulation type %d.", itemData.m_nDBID,
            itemData.m_nIndex
        );
    }

    if (atomicJob != nullptr) {
        static_cast<void>(atomicJob->Do());
        atomicJob->Destroy();
    }
    user->SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
    return false;
}

bool ReplyEnchantItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a7f54, 687 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t scrollServerId = 0;
    std::int64_t scrollAmount = 0;
    std::int32_t supportServerId = 0;
    std::int64_t supportAmount = 0;
    std::int32_t targetServerId = 0;
    std::int32_t enchant = 0;
    std::int32_t userServerId = 0;
    std::uint8_t lucky = 0;
    ::Disassemble(
        packet, "dQdQdddc", &scrollServerId, &scrollAmount, &supportServerId, &supportAmount, &targetServerId, &enchant,
        &userServerId, &lucky
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x130f,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    CItem* const scroll = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(scrollServerId));
    CItem* const target = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetServerId));
    CItem* const support = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(supportServerId));
    if (scroll == nullptr || target == nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
        user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
        int option0 = 0;
        int option1 = 0;
        int option2 = 0;
        if (target != nullptr && target->m_pEnchantOption != nullptr) {
            const EnchantOptionId& optionId = target->m_pEnchantOption->GetOptionId();
            option0 = optionId.at[0];
            option1 = optionId.at[1];
            option2 = optionId.at[2];
        }
        if (userSocket != nullptr) {
            userSocket->Send(
                "cddQdddd", 0x87,
                // 2,
                IER_CANCELED, 0, std::int64_t{0}, 0, option0, option1, option2
            );
        }
        user->InitAllEnchantBonusOptions();
        return false;
    }

    static_cast<void>(user->ReplyEnchantItem(*scroll, scrollAmount, support, supportAmount, *target, enchant, lucky != 0));
    return false;
}

bool ReplyEnchantItemFail(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a8204, 689 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t scrollServerId = 0;
    std::int64_t scrollAmount = 0;
    std::int32_t supportServerId = 0;
    std::int64_t supportAmount = 0;
    std::int32_t targetServerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t crystalItemDatabaseId = 0;
    std::int64_t crystalAmount = 0;
    ::Disassemble(
        packet, "dQdQdddQ", &scrollServerId, &scrollAmount, &supportServerId, &supportAmount, &targetServerId, &userServerId,
        &crystalItemDatabaseId, &crystalAmount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L" Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1359,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[enchant] Cannot apply enchant failure: user server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    CItem* const scroll = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(scrollServerId));
    CItem* const target = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(targetServerId));
    CItem* const support = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(supportServerId));
    if (scroll == nullptr || target == nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
        user->SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
        user->InitAllEnchantBonusOptions();
        int option0 = 0;
        int option1 = 0;
        int option2 = 0;
        if (target != nullptr && target->m_pEnchantOption != nullptr) {
            const EnchantOptionId& optionId = target->m_pEnchantOption->GetOptionId();
            option0 = optionId.at[0];
            option1 = optionId.at[1];
            option2 = optionId.at[2];
        }
        if (userSocket != nullptr) {
            userSocket->Send(
                "cddQdddd", 0x87,
                // 2,
                IER_CANCELED, 0, std::int64_t{0}, 0, option0, option1, option2
            );
        }
        return false;
    }

    user->ReplyEnchantItemFail(*scroll, scrollAmount, support, supportAmount, *target, crystalItemDatabaseId, crystalAmount);
    return false;
}

bool ManipulatePointItemPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c55dc, 2425 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t atomicJobId = -1;
    std::int32_t pointType = 0;
    std::int32_t pointDelta = 0;
    std::int32_t itemCount = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "ddddd", &userServerId, &atomicJobId, &pointType, &pointDelta, &itemCount);

    AtomicJob* const atomicJob = Singleton<ObjectCache<AtomicJob, 10240>>::inst().Pop(atomicJobId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xe6d,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[inventory] Cannot apply the point/item update: user server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (itemCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[inventory] Disconnecting '%s' (%d): database item data is invalid.",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_ERROR(
            Log, u"[inventory] Disconnecting '%s' (%d): database item data is invalid.", user->GetName(),
            static_cast<int>(user->GetDBID())
        );
        if (itemCount != -100) {
            user->StopMove();
            CUserSocket* const userSocket = user->GetSocket();
            if (userSocket != nullptr) {
                userSocket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        if (atomicJob != nullptr) {
            atomicJob->Destroy();
        }
        return false;
    }

    if (itemCount == 0) {
        if (atomicJob != nullptr) {
            static_cast<void>(atomicJob->Do());
            atomicJob->Destroy();
        }
        std::int32_t etcManipulationCount = 0;
        cursor = ::Disassemble(cursor, "d", &etcManipulationCount);
        static_cast<void>(L2EtcMani::ParseEtcMani(etcManipulationCount, cursor));
        return false;
    }

    if (pointDelta < 0) {
        const int absoluteDelta = runtime::SubtractInt32Wrapping(0, pointDelta);
        if (pointType == 5) {
            user->SendSystemMessage_d(SystemMessageId::FAME_CONSUMED, absoluteDelta);
        } else if (pointType == 8) {
            user->SendSystemMessage_d(SystemMessageId::RAID_POINTS_CONSUMED, absoluteDelta);
        }
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        while (itemCount-- > 0) {
            CItemDisassemblerItemMani itemData;
            cursor = itemData.DisassembleItemInfo(cursor);

            if (itemData.m_nIndex == 1) {
                CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CreateItem Failed",
                    //     L"DBSocket.cpp",
                    //     0xfd8)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Cannot create item class %d for database item %d from a point/item update.",
                        itemData.m_nClassID, itemData.m_nDBID
                    );
                    continue;
                }

                item->SetDBID(static_cast<std::uint32_t>(itemData.m_nDBID));
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->SetAmount(itemData.m_nAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                        //     L"DBSocket.cpp",
                        //     0xf98,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nAmount)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Cannot initialize point/item update item %d amount from %lld to %lld.",
                            itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                            static_cast<long long>(itemData.m_nAmount)
                        );
                    }
                    item->SetEnchanted(itemData.m_nEnchanted);
                    item->m_Useable = user->CheckUseable(item);
                    item->m_data->m_nDamaged = itemData.m_nDamaged;
                    item->m_data->m_bBlessed = itemData.m_nIsBlessed;
                    item->SetVariation(
                        ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                        static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
                    );
                    item->SetAttribute(itemData.m_Attribute);
                }
                if (item->IsDuration()) {
                    db.UpdateItemDuration(*user, static_cast<std::uint32_t>(itemData.m_nDBID), item->GetDuration());
                }
                if (item->m_const->m_period > 0) {
                    user->OnGetPeriodicItem(item);
                }
                if (itemData.m_nShapeShiftedClassID > 0) {
                    item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
                    if (itemData.m_nShapeShiftedPeriod >= 0) {
                        user->OnGetPeriodicShapeShiftedItem(item, itemData.m_nShapeShiftedPeriod);
                        user->SendPeriodicShapeShiftedItemList();
                    }
                }
                item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
                item->m_data->SetLocked(itemData.m_hLocked);
                item->m_data->SetUseCount(itemData.m_hUseCount);
                if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
                    item->SetAgathionEnergy(item->m_const->m_dAgathionMaxEnergy);
                    db.RequestSaveItemAgathionEnergy(
                        user, static_cast<int>(item->m_data->m_nId), static_cast<int>(item->GetDBID()),
                        item->GetAgathionEnergy()
                    );
                    user->SendBrAgathionEnergyInfo();
                }
                if (!user->m_inv.Push(item->m_data->index)) {
                    // ::LogIOError(
                    //     "inven push failed at file[%s], line[%d]",
                    //     L"DBSocket.cpp",
                    //     0xfcb)
                    L2_LOG_IO_ERROR(
                        "[inventory] Cannot add item DB ID %d (class %d) from a point/item update to character runtime ID "
                        "%d inventory; processing continues.",
                        static_cast<int>(itemData.m_nDBID), static_cast<int>(itemData.m_nClassID),
                        static_cast<int>(userServerId)
                    );
                }
                user->AutoEquipArrowOrBolt(item);
                user->m_inv.SetInventoryChanged(item, IUT_ADD);
                user->SendItemIncreaseSystemMessage(
                    itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted()
                );
                continue;
            }

            if (itemData.m_nIndex == 2) {
                CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     L"DBSocket.cpp",
                    //     0xff3,
                    //     user->GetName(),
                    //     itemData.m_nDBID,
                    //     itemData.m_nClassID,
                    //     itemData.m_nChangeAmount,
                    //     itemData.m_nAmount)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Cannot increase missing item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     itemData.m_nDBID,
                    //     user->GetName(),
                    //     itemData.m_nClassID,
                    //     static_cast<long long>(itemData.m_nChangeAmount),
                    //     static_cast<long long>(itemData.m_nAmount))
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Cannot increase missing item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        itemData.m_nDBID, user->GetName(), itemData.m_nClassID,
                        static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                    );
                    continue;
                }
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->AddAmount(itemData.m_nChangeAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                        //     L"DBSocket.cpp",
                        //     0xfe7,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Cannot increase point/item update item %d amount from %lld by %lld.",
                            itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                            static_cast<long long>(itemData.m_nChangeAmount)
                        );
                    }
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                user->SendItemIncreaseSystemMessage(
                    itemData.m_nClassID, itemData.m_nChangeAmount, item->m_data->GetEnchanted()
                );
                continue;
            }

            if (itemData.m_nIndex == 3) {
                CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     L"DBSocket.cpp",
                    //     0x100c,
                    //     user->GetName(),
                    //     itemData.m_nDBID,
                    //     itemData.m_nClassID,
                    //     itemData.m_nChangeAmount,
                    //     itemData.m_nAmount)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Cannot decrease missing item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     itemData.m_nDBID,
                    //     user->GetName(),
                    //     itemData.m_nClassID,
                    //     static_cast<long long>(itemData.m_nChangeAmount),
                    //     static_cast<long long>(itemData.m_nAmount))
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Cannot decrease missing item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        itemData.m_nDBID, user->GetName(), itemData.m_nClassID,
                        static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                    );
                    continue;
                }
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->AddAmount(itemData.m_nChangeAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                        //     L"DBSocket.cpp",
                        //     0x1001,
                        //     item->m_data->m_nCount,
                        //     itemData.m_nChangeAmount)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Cannot decrease point/item update item %d amount from %lld by %lld.",
                            itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                            static_cast<long long>(itemData.m_nChangeAmount)
                        );
                    }
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                user->SendItemDecreaseSystemMessage(
                    itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true
                );
                continue;
            }

            if (itemData.m_nIndex == 4) {
                CItem* const item = user->m_inv.GetByDBID(itemData.m_nDBID);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     L"DBSocket.cpp",
                    //     0x1017,
                    //     user->GetName(),
                    //     itemData.m_nDBID,
                    //     itemData.m_nClassID,
                    //     itemData.m_nChangeAmount,
                    //     itemData.m_nAmount)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Cannot delete missing item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     itemData.m_nDBID,
                    //     user->GetName(),
                    //     itemData.m_nClassID,
                    //     static_cast<long long>(itemData.m_nChangeAmount),
                    //     static_cast<long long>(itemData.m_nAmount))
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Cannot delete missing item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        itemData.m_nDBID, user->GetName(), itemData.m_nClassID,
                        static_cast<long long>(itemData.m_nChangeAmount), static_cast<long long>(itemData.m_nAmount)
                    );
                    continue;
                }
                user->SendItemDecreaseSystemMessage(
                    itemData.m_nClassID, itemData.m_nChangeAmount, itemData.m_nEnchanted, true
                );
                if (itemData.m_nShapeShiftedClassID > 0 && itemData.m_nShapeShiftedPeriod >= 0) {
                    user->OnRestorePeriodicShapeShiftedItem(item);
                }
                if (user->UnequipItem(item, true) != ST_NONE) {
                    user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                    user->m_inv.Pop(item->m_data->index, false);
                    item->Delete();
                }
                continue;
            }

            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] manipulate item fail. unknown manipulate type[%d]",
            //     L"DBSocket.cpp",
            //     0x103b,
            //     itemData.m_nIndex)
            L2_LOG_ERROR(
                Log, u"[inventory] Cannot apply point/item update for item %d: unknown manipulation type %d.",
                itemData.m_nDBID, itemData.m_nIndex
            );
        }
    }

    if (atomicJob != nullptr) {
        static_cast<void>(atomicJob->Do());
        atomicJob->Destroy();
    }
    user->SendItemList(true, true, nullptr);
    std::int32_t etcManipulationCount = 0;
    cursor = ::Disassemble(cursor, "d", &etcManipulationCount);
    static_cast<void>(L2EtcMani::ParseEtcMani(etcManipulationCount, cursor));
    return false;
}

bool RecipeMakeItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b6fb8, 2409 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t recipeId = 0;
    std::int32_t result = -1;
    std::int32_t increaseReason = 0;
    std::int32_t itemCount = 0;
    std::uint8_t lucky = 0;
    const unsigned char* cursor =
        ::Disassemble(packet, "dddddc", &userServerId, &recipeId, &result, &increaseReason, &itemCount, &lucky);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2df3,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[crafting] Cannot apply recipe %d result: user server ID %d is not active.", recipeId, userServerId
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    {
        L2_GUARD(*user->m_lock);
        user->m_bWaitRecipeMake = false;
    }

    if (itemCount == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        // L2_LOG_ERROR(Log,
        //     L"[inventory] Disconnecting '%s' (%d): database item data is invalid.",
        //     user->GetName(),
        //     static_cast<int>(user->GetDBID()))
        L2_LOG_ERROR(
            Log, u"[inventory] Disconnecting '%s' (%d): database item data is invalid.", user->GetName(),
            static_cast<int>(user->GetDBID())
        );
        user->StopMove();
        CUserSocket* const userSocket = user->GetSocket();
        if (userSocket != nullptr) {
            userSocket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        return false;
    }

    if (increaseReason == 1) {
        user->SendSystemMessage(SystemMessageId::CRAFTING_CRITICAL);
    }
    const FVector position = user->GetPos();
    {
        L2_WRITE_GUARD(user->m_inv);
        while (itemCount-- > 0) {
            std::int32_t manipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            std::int32_t ignored = 0;
            cursor = ::Disassemble(
                cursor, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignored
            );

            int logDirection = 0;
            bool writeLog = true;
            if (manipulationType == 1) {
                logDirection = 1;
                CItem* const item = g_ObjectDB.CreateItem(itemClassId);
                if (item != nullptr) {
                    item->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->SetAmount(amountAfter)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x2e24,
                            //     item->m_data->m_nCount,
                            //     amountAfter)
                            L2_LOG_ERROR(
                                Log, u"[crafting] Cannot initialize recipe %d item %d amount from %lld to %lld.", recipeId,
                                itemDatabaseId, static_cast<long long>(item->m_data->m_nCount),
                                static_cast<long long>(amountAfter)
                            );
                        }
                    }
                    if (!user->m_inv.Push(item->m_data->index)) {
                        // ::LogIOError(
                        //     "inven push failed at file[%s], line[%d]",
                        //     "DBSocket.cpp",
                        //     0x2e2c)
                        L2_LOG_IO_ERROR(
                            "[inventory] Cannot add item DB ID %d (class %d) from recipe %d to character runtime ID %d "
                            "inventory; processing continues.",
                            static_cast<int>(itemDatabaseId), static_cast<int>(itemClassId), static_cast<int>(recipeId),
                            static_cast<int>(userServerId)
                        );
                    }
                    user->AutoEquipArrowOrBolt(item);
                    user->m_inv.SetInventoryChanged(item, IUT_ADD);
                    user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2e39,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[crafting] Cannot create recipe %d item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     recipeId,
                    //     itemDatabaseId,
                    //     user->GetName(),
                    //     itemClassId,
                    //     static_cast<long long>(amountDelta),
                    //     static_cast<long long>(amountAfter))
                    L2_LOG_ERROR(
                        Log,
                        u"[crafting] Cannot create recipe %d item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        recipeId, itemDatabaseId, user->GetName(), itemClassId, static_cast<long long>(amountDelta),
                        static_cast<long long>(amountAfter)
                    );
                }
            } else if (manipulationType == 2) {
                logDirection = 1;
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item != nullptr) {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x2e48,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log, u"[crafting] Cannot increase recipe %d item %d amount from %lld by %lld.", recipeId,
                                itemDatabaseId, static_cast<long long>(item->m_data->m_nCount),
                                static_cast<long long>(amountDelta)
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2e52,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[crafting] Cannot increase missing recipe %d item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     recipeId,
                    //     itemDatabaseId,
                    //     user->GetName(),
                    //     itemClassId,
                    //     static_cast<long long>(amountDelta),
                    //     static_cast<long long>(amountAfter))
                    L2_LOG_ERROR(
                        Log,
                        u"[crafting] Cannot increase missing recipe %d item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        recipeId, itemDatabaseId, user->GetName(), itemClassId, static_cast<long long>(amountDelta),
                        static_cast<long long>(amountAfter)
                    );
                }
            } else if (manipulationType == 3) {
                logDirection = -1;
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item != nullptr) {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x2e60,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log, u"[crafting] Cannot decrease recipe %d item %d amount from %lld by %lld.", recipeId,
                                itemDatabaseId, static_cast<long long>(item->m_data->m_nCount),
                                static_cast<long long>(amountDelta)
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    user->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), false);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2e6a,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[crafting] Cannot decrease missing recipe %d item %d for character %s: "
                    //     L"class %d, delta %lld, database amount %lld.",
                    //     recipeId,
                    //     itemDatabaseId,
                    //     user->GetName(),
                    //     itemClassId,
                    //     static_cast<long long>(amountDelta),
                    //     static_cast<long long>(amountAfter))
                    L2_LOG_ERROR(
                        Log,
                        u"[crafting] Cannot decrease missing recipe %d item %d for character %s: "
                        u"class %d, delta %lld, database amount %lld.",
                        recipeId, itemDatabaseId, user->GetName(), itemClassId, static_cast<long long>(amountDelta),
                        static_cast<long long>(amountAfter)
                    );
                }
            } else if (manipulationType == 4) {
                logDirection = -1;
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x2e79)
                    L2_LOG_ERROR(Log, u"[crafting] Cannot delete missing recipe %d item %d.", recipeId, itemDatabaseId);
                    writeLog = false;
                } else {
                    user->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), false);
                    if (user->UnequipItem(item, true) != ST_NONE) {
                        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                        user->m_inv.Pop(item->m_data->index, false);
                        item->Delete();
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] RecipeMakeItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x2e90,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[crafting] Cannot apply recipe %d item %d: unknown manipulation type %d.", recipeId,
                    itemDatabaseId, manipulationType
                );
                writeLog = false;
            }

            if (writeLog && (logDirection == 1 || logDirection == -1)) {
                CLogFormatter formatter;
                formatter.WriteLog1(
                    logDirection == 1 ? 0x3a7 : 0x3a6, user, nullptr, nullptr, nullptr, nullptr, 0, 0,
                    logDirection == 1 ? increaseReason : 0, itemClassId,
                    logDirection == 1 ? amountDelta : runtime::AbsoluteInt64Wrapping(amountDelta), amountAfter,
                    itemDatabaseId
                );
            }
        }
    }

    if (lucky != 0) {
        SkillUseInfo skill;
        skill.m_skillID = 0x46b7;
        skill.m_skillLevel = 1;
        skill.m_skillSubLevel = 0;
        skill.m_castRange = 0;
        skill.m_type = 0;
        skill.m_param1 = 0;
        int parameter1 = 0;
        int parameter2 = 500;
        int parameter3 = 0;
        int parameter4 = 0x5dc;
        int parameter5 = 0;
        int parameter6 = 0;
        DefaultIOBuffer* const effectPacket = user->MakePacketForMagicSkillUse(
            parameter1, user, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
        );
        g_ptlUserMap->BroadcastToNeighborDist(user, 0xf000, position, user->GetBroadcastDist(0x1000), effectPacket);
        user->SendSystemMessage(SystemMessageId::LADY_LUCK_ACTIVATED);
    }

    user->SendItemList(true, true, nullptr);
    user->SendRecipeInfo(recipeId, result);
    return false;
}

bool PingOneAdenaDrop(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006abd60, 2560 bytes.
    L2_GUARD_FUNCTION();

    const std::int32_t mainReceiveTime = runtime::crt::Time32Now();
    std::int32_t userServerId = 0;
    std::int32_t pingStartTime = 0;
    std::int32_t databaseSendTime = 0;
    std::int32_t cacheProcessStartTime = 0;
    std::int32_t beforeDropTime = 0;
    std::int32_t sendToMainTime = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    packet = ::Disassemble(
        packet, "ddddddddd", &userServerId, &pingStartTime, &databaseSendTime, &cacheProcessStartTime, &beforeDropTime,
        &sendToMainTime, &x, &y, &z
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x269e,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[items] Cannot run one-adena drop probe: user server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    std::int32_t sourceOwnerId = 0;
    std::int32_t sourceItemDatabaseId = 0;
    std::int32_t sourceDropAmount = 0;
    std::int32_t droppedOwnerId = 0;
    std::int32_t droppedItemDatabaseId = 0;
    std::int32_t droppedAmount = 0;
    std::int32_t cacheThreadWait = 0;
    ::Disassemble(
        packet, "ddddddd", &sourceOwnerId, &sourceItemDatabaseId, &sourceDropAmount, &droppedOwnerId, &droppedItemDatabaseId,
        &droppedAmount, &cacheThreadWait
    );
    static_cast<void>(droppedOwnerId);

    if (sourceDropAmount == 0 && droppedAmount == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]drop item's count is 0.",
        //     "DBSocket.cpp",
        //     0x26ac)
        L2_LOG_ERROR(Log, u"[items] One-adena drop reply contains zero source and dropped amounts.");
        return false;
    }

    const bool validDrop = [&] {
        L2_GUARD(*user->m_lock);
        if (sourceItemDatabaseId == 0 || droppedItemDatabaseId == 0) {
            return false;
        }
        if (sourceOwnerId == 0) {
            CItem* const equippedItem = user->m_inv.GetByDBID(sourceItemDatabaseId);
            static_cast<void>(user->UnequipItem(equippedItem, false));
        }
        return true;
    }();
    if (!validDrop) {
        // Log.Add(LOG_ERROR, L"drop fail")
        L2_LOG_ERROR(Log, u"[items] Drop operation failed; database reply handling stopped.");
        return false;
    }

    auto closeForIllegalDrop = [&]() {
        user->StopMove();
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
    };

    const FVector position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    CItem* item = nullptr;
    CItem* sourceItem = nullptr;
    enum class DropOutcome { MissingWholeItem, InvalidWholeCount, MissingSplitItem, InvalidSplitCount, Completed };
    const auto dropOutcome = [&] {
        L2_WRITE_GUARD(user->m_inv);
        if (sourceOwnerId == 0) {
            item = user->m_inv.GetByDBID(sourceItemDatabaseId);
            if (item == nullptr) {
                return DropOutcome::MissingWholeItem;
            }
            if (item->m_data->m_nCount < static_cast<std::int64_t>(static_cast<std::uint32_t>(sourceDropAmount))) {
                return DropOutcome::InvalidWholeCount;
            }
            user->m_inv.Pop(item->m_data->index, false);
            user->m_inv.SetInventoryChanged(item, IUT_DELETE);
            {
                L2_GUARD(*item->m_lock);
                if (!item->SetAmount(droppedAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%d][%d]",
                    //     "DBSocket.cpp",
                    //     0x26f1,
                    //     item->m_data->m_nCount,
                    //     droppedAmount)
                    L2_LOG_ERROR(
                        Log, u"[items] Cannot set whole dropped item %d amount from %d to %d.", sourceItemDatabaseId,
                        item->m_data->m_nCount, droppedAmount
                    );
                }
            }
            CWorld::Inst().PutItem(item, position, user);
            item->m_nTraceCharId = user->GetDBID();
        } else {
            sourceItem = user->m_inv.GetByDBID(sourceItemDatabaseId);
            if (sourceItem == nullptr) {
                return DropOutcome::MissingSplitItem;
            }
            if (sourceItem->m_data->m_nCount < static_cast<std::int64_t>(static_cast<std::uint32_t>(sourceDropAmount))) {
                return DropOutcome::InvalidSplitCount;
            }
            {
                L2_GUARD(*sourceItem->m_lock);
                if (!sourceItem->AddAmount(-static_cast<std::int64_t>(sourceDropAmount))) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%d][%d]",
                    //     "DBSocket.cpp",
                    //     0x271d,
                    //     sourceItem->m_data->m_nCount,
                    //     -sourceDropAmount)
                    L2_LOG_ERROR(
                        Log, u"[items] Cannot subtract %d from split source item %d amount %d.", sourceDropAmount,
                        sourceItemDatabaseId, sourceItem->m_data->m_nCount
                    );
                }
            }

            CItem* const droppedItem = static_cast<CItem*>(sourceItem->Copy());
            if (!droppedItem->SetAmount(droppedAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%d][%d]",
                //     "DBSocket.cpp",
                //     0x2724,
                //     droppedItem->m_data->m_nCount,
                //     droppedAmount)
                L2_LOG_ERROR(
                    Log, u"[items] Cannot set split dropped item %d amount from %d to %d.", droppedItemDatabaseId,
                    droppedItem->m_data->m_nCount, droppedAmount
                );
            }
            droppedItem->SetDBID(static_cast<std::uint32_t>(droppedItemDatabaseId));
            CWorld::Inst().PutItem(droppedItem, position, user);
            droppedItem->m_nTraceCharId = user->GetDBID();
            droppedItem->m_data->m_nInventorySlotIndex = -1;
            user->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);
        }
        return DropOutcome::Completed;
    }();
    switch (dropOutcome) {
        case DropOutcome::MissingWholeItem:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBSocket.cpp",
            //     0x26d3,
            //     sourceOwnerId)
            L2_LOG_ERROR(Log, u"[items] Cannot drop whole inventory item %d because it is missing.", sourceItemDatabaseId);
            closeForIllegalDrop();
            return false;
        case DropOutcome::InvalidWholeCount:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Item count is invalid dbid[%d] "
            //     L"server count[%d] change[%d] drop [%d]",
            //     "DBSocket.cpp",
            //     0x26e1,
            //     sourceItemDatabaseId,
            //     item->m_data->m_nCount,
            //     sourceDropAmount,
            //     droppedAmount)
            L2_LOG_ERROR(
                Log, u"[items] Cannot drop whole item %d: inventory amount %d, requested %d, database result %d.",
                sourceItemDatabaseId, item->m_data->m_nCount, sourceDropAmount, droppedAmount
            );
            closeForIllegalDrop();
            return false;
        case DropOutcome::MissingSplitItem:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBSocket.cpp",
            //     0x2703,
            //     sourceItemDatabaseId)
            L2_LOG_ERROR(
                Log, u"[items] Cannot split inventory item %d for dropping because it is missing.", sourceItemDatabaseId
            );
            closeForIllegalDrop();
            return false;
        case DropOutcome::InvalidSplitCount:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Item count is invalid dbid[%d] "
            //     L"server count[%d] result[%d] drop [%d]",
            //     "DBSocket.cpp",
            //     0x2711,
            //     sourceItemDatabaseId,
            //     sourceItem->m_data->m_nCount,
            //     sourceDropAmount,
            //     droppedAmount)
            L2_LOG_ERROR(
                Log, u"[items] Cannot split item %d for dropping: inventory amount %d, requested %d, database result %d.",
                sourceItemDatabaseId, sourceItem->m_data->m_nCount, sourceDropAmount, droppedAmount
            );
            closeForIllegalDrop();
            return false;
        case DropOutcome::Completed:
            break;
    }
    user->SendItemList(true, true, nullptr);

    char16_t systemContext[] = u"SYS";
    char16_t message[0x5000]{};
    auto sendPingLine = [&](std::u16string_view format, int index, int value) {
        FormatUtf16To(message, std::size(message), format, index, value);
        user->SendSystemMessage(systemContext, message);
    };
    sendPingLine(u"%d Ping [%d]sec Start", 1, pingStartTime);
    sendPingLine(u"%d Ping [%d]sec db send", 2, runtime::SubtractInt32Wrapping(databaseSendTime, pingStartTime));
    sendPingLine(u"%d Ping [%d]m cache thread wait", 3, cacheThreadWait);
    sendPingLine(
        u"%d Ping [%d]sec cache process start", 4, runtime::SubtractInt32Wrapping(cacheProcessStartTime, pingStartTime)
    );
    sendPingLine(u"%d Ping [%d]sec before drop", 5, runtime::SubtractInt32Wrapping(beforeDropTime, pingStartTime));
    sendPingLine(u"%d Ping [%d]sec send to main", 6, runtime::SubtractInt32Wrapping(sendToMainTime, pingStartTime));
    sendPingLine(u"%d Ping [%d]sec main receive", 7, runtime::SubtractInt32Wrapping(mainReceiveTime, pingStartTime));
    sendPingLine(u"%d Ping [%d]sec ping end", 8, runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), pingStartTime));
    return false;
}

namespace DBPacketHandler {

bool ReplyTrade(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006bbae8, 5690 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t firstDatabaseId = 0;
    std::int32_t secondDatabaseId = 0;
    packet = ::Disassemble(packet, "dd", &firstDatabaseId, &secondDatabaseId);

    const int databaseId = runtime::process::CurrentDatabaseId();
    CSPointer<User> firstPointer = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, firstDatabaseId));
    CSPointer<User> secondPointer = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, secondDatabaseId));
    User* const first = firstPointer.get();
    User* const second = secondPointer.get();
    if (first == nullptr || second == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ReplyTrade]Cannot find user from g_UserDBIDMap, "
        //     L"world id[%d] dbid[%d], dbid[%d]",
        //     databaseId,
        //     firstDatabaseId,
        //     secondDatabaseId)
        L2_LOG_ERROR(
            Log, u"[trade] Cannot apply trade in world %d: character %d or %d is not active.", databaseId, firstDatabaseId,
            secondDatabaseId
        );
        for (User* const user : {first, second}) {
            if (user == nullptr) {
                continue;
            }
            user->StopMove();
            if (user->GetSocket() != nullptr) {
                user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        return false;
    }

    CTrade* const trade = first->GetTrade();
    if (trade == nullptr) {
        return false;
    }

    User* source = nullptr;
    User* target = nullptr;
    CItem* sourceItem = nullptr;
    std::int32_t sourceItemDatabaseId;
    std::int32_t targetItemDatabaseId;
    int totalItemCount;
    enum class TradeOutcome {
        MissingUpdatedSource,
        MissingUpdatedTarget,
        MissingSplitSource,
        TargetCreationFailed,
        MissingTradedSource,
        MissingMergedTarget,
        MissingMovedItem,
        InvalidTradeCase,
        Completed
    };
    const auto tradeOutcome = [&] {
        L2_WRITE_GUARD(first->m_inv);
        L2_WRITE_GUARD(second->m_inv);
        totalItemCount = 0;
        for (int section = 0; section < 2; ++section) {
            source = section == 0 ? first : second;
            target = section == 0 ? second : first;
            std::int32_t itemCount = 0;
            packet = ::Disassemble(packet, "d", &itemCount);
            totalItemCount += itemCount;
            for (int index = 0; index < itemCount; ++index) {
                std::int32_t sourceType = 0;
                sourceItemDatabaseId = 0;
                std::int32_t sourceItemServerId = 0;
                std::int64_t sourceAmount = 0;
                std::int32_t targetType = 0;
                targetItemDatabaseId = 0;
                std::int32_t targetItemServerId = 0;
                std::int64_t targetAmount = 0;
                packet = ::Disassemble(
                    packet, "dddQdddQ", &sourceType, &sourceItemDatabaseId, &sourceItemServerId, &sourceAmount, &targetType,
                    &targetItemDatabaseId, &targetItemServerId, &targetAmount
                );
                static_cast<void>(sourceItemServerId);
                static_cast<void>(targetItemServerId);

                if (sourceType == 1 && targetType == 1) {
                    sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
                    if (sourceItem == nullptr) {
                        return TradeOutcome::MissingUpdatedSource;
                    }
                    if (!sourceItem->SetAmount(sourceAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x162b,
                        //     sourceItem->m_data->m_nCount,
                        //     sourceAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot set source item %d amount from %lld to %lld for %s -> %s.",
                        //     sourceItemDatabaseId,
                        //     static_cast<long long>(sourceItem->m_data->m_nCount),
                        //     static_cast<long long>(sourceAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot set source item %d amount from %lld to %lld for %s -> %s.",
                            sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                            static_cast<long long>(sourceAmount), source->GetName(), target->GetName()
                        );
                    }
                    source->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);
                    CItem* const targetItem = target->m_inv.GetByDBID(targetItemDatabaseId);
                    if (targetItem == nullptr) {
                        return TradeOutcome::MissingUpdatedTarget;
                    }
                    if (!targetItem->SetAmount(targetAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x164a,
                        //     targetItem->m_data->m_nCount,
                        //     targetAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot set target item %d amount from %lld to %lld for %s -> %s.",
                        //     targetItemDatabaseId,
                        //     static_cast<long long>(targetItem->m_data->m_nCount),
                        //     static_cast<long long>(targetAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot set target item %d amount from %lld to %lld for %s -> %s.",
                            targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                            static_cast<long long>(targetAmount), source->GetName(), target->GetName()
                        );
                    }
                    target->m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);
                    continue;
                }

                if (sourceType == 1 && targetType == 2) {
                    sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
                    if (sourceItem == nullptr) {
                        return TradeOutcome::MissingSplitSource;
                    }
                    if (!sourceItem->SetAmount(sourceAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x166a,
                        //     sourceItem->m_data->m_nCount,
                        //     sourceAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot set split source item %d amount from %lld to %lld for %s -> %s.",
                        //     sourceItemDatabaseId,
                        //     static_cast<long long>(sourceItem->m_data->m_nCount),
                        //     static_cast<long long>(sourceAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot set split source item %d amount from %lld to %lld for %s -> %s.",
                            sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                            static_cast<long long>(sourceAmount), source->GetName(), target->GetName()
                        );
                    }
                    source->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);

                    CItem* const targetItem = g_ObjectDB.CreateItem(sourceItem->m_nClassId);
                    if (targetItem == nullptr) {
                        return TradeOutcome::TargetCreationFailed;
                    }
                    targetItem->SetDBID(static_cast<unsigned int>(targetItemDatabaseId));
                    if (!targetItem->SetAmount(targetAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x1687,
                        //     targetItem->m_data->m_nCount,
                        //     targetAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot initialize split target item %d amount from %lld to %lld for %s -> %s.",
                        //     targetItemDatabaseId,
                        //     static_cast<long long>(targetItem->m_data->m_nCount),
                        //     static_cast<long long>(targetAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot initialize split target item %d amount from %lld to %lld for %s -> %s.",
                            targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                            static_cast<long long>(targetAmount), source->GetName(), target->GetName()
                        );
                    }
                    if (!target->m_inv.Push(targetItem->m_data->index)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%S][%d][ReplyTrade]inven push failed, "
                        //     "from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x168d,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     "[trade] Cannot insert split item into %s inventory after transfer from %s.",
                        //     target->GetName(),
                        //     source->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot insert split item into %s inventory after transfer from %s.",
                            target->GetName(), source->GetName()
                        );
                    }
                    target->m_inv.SetInventoryChanged(targetItem, IUT_ADD);
                    continue;
                }

                if (sourceType == 3 && targetType == 1) {
                    sourceItem = trade->GetItem(source, sourceItemDatabaseId);
                    if (sourceItem == nullptr) {
                        return TradeOutcome::MissingTradedSource;
                    }
                    if (!sourceItem->SetAmount(targetAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x16ba,
                        //     sourceItem->m_data->m_nCount,
                        //     targetAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot clear traded source item %d amount from %lld to %lld for %s -> %s.",
                        //     sourceItemDatabaseId,
                        //     static_cast<long long>(sourceItem->m_data->m_nCount),
                        //     static_cast<long long>(targetAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot clear traded source item %d amount from %lld to %lld for %s -> %s.",
                            sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                            static_cast<long long>(targetAmount), source->GetName(), target->GetName()
                        );
                    }
                    static_cast<void>(source->m_inv.Pop(sourceItem->m_data->index, false));
                    source->m_inv.SetInventoryChanged(sourceItem, IUT_DELETE);
                    sourceItem->Delete();

                    CItem* const targetItem = target->m_inv.GetByDBID(targetItemDatabaseId);
                    if (targetItem == nullptr) {
                        return TradeOutcome::MissingMergedTarget;
                    }
                    if (!targetItem->SetAmount(targetAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x16da,
                        //     targetItem->m_data->m_nCount,
                        //     targetAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot set merged target item %d amount from %lld to %lld for %s -> %s.",
                        //     targetItemDatabaseId,
                        //     static_cast<long long>(targetItem->m_data->m_nCount),
                        //     static_cast<long long>(targetAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot set merged target item %d amount from %lld to %lld for %s -> %s.",
                            targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                            static_cast<long long>(targetAmount), source->GetName(), target->GetName()
                        );
                    }
                    target->m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);
                    continue;
                }

                if (sourceType == 3 && targetType == 4) {
                    CItem* const movedItem = trade->GetItem(source, targetItemDatabaseId);
                    if (movedItem == nullptr) {
                        return TradeOutcome::MissingMovedItem;
                    }
                    if (!movedItem->SetAmount(targetAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d][ReplyTrade] item amount is invalid"
                        //     L"[%I64d][%I64d] from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x170d,
                        //     movedItem->m_data->m_nCount,
                        //     targetAmount,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[trade] Cannot set moved item %d amount from %lld to %lld for %s -> %s.",
                        //     targetItemDatabaseId,
                        //     static_cast<long long>(movedItem->m_data->m_nCount),
                        //     static_cast<long long>(targetAmount),
                        //     source->GetName(),
                        //     target->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot set moved item %d amount from %lld to %lld for %s -> %s.",
                            targetItemDatabaseId, static_cast<long long>(movedItem->m_data->m_nCount),
                            static_cast<long long>(targetAmount), source->GetName(), target->GetName()
                        );
                    }
                    static_cast<void>(source->m_inv.Pop(movedItem->m_data->index, false));
                    source->m_inv.SetInventoryChanged(movedItem, IUT_DELETE);
                    if (!target->m_inv.Push(movedItem->m_data->index)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "[%S][%d][ReplyTrade]inven push failed, "
                        //     "from[%s] to[%s]",
                        //     "DBSocket.cpp",
                        //     0x1717,
                        //     source->GetName(),
                        //     target->GetName())
                        // L2_LOG_ERROR(Log,
                        //     "[trade] Cannot insert moved item into %s inventory after transfer from %s.",
                        //     target->GetName(),
                        //     source->GetName())
                        L2_LOG_ERROR(
                            Log, u"[trade] Cannot insert moved item into %s inventory after transfer from %s.",
                            target->GetName(), source->GetName()
                        );
                    }
                    target->m_inv.SetInventoryChanged(movedItem, IUT_ADD);
                    continue;
                }

                if (sourceType == 1 && targetType == 4) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d][ReplyTrade]Cannot occur this case. "
                    //     L"from[%s] to[%s]",
                    //     "DBSocket.cpp",
                    //     0x1696,
                    //     source->GetName(),
                    //     target->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[trade] Unsupported item transition type 1 -> 4 for %s -> %s.",
                    //     source->GetName(),
                    //     target->GetName())
                    L2_LOG_ERROR(
                        Log, u"[trade] Unsupported item transition type 1 -> 4 for %s -> %s.", source->GetName(),
                        target->GetName()
                    );
                } else if (sourceType == 3 && targetType == 2) {
                    return TradeOutcome::InvalidTradeCase;
                } else if (sourceType != 1 && sourceType != 3) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d][ReplyTrade]Cannot occur this case. "
                    //     L"from[%s] to[%s]",
                    //     "DBSocket.cpp",
                    //     0x1723,
                    //     source->GetName(),
                    //     target->GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[trade] Unsupported source item transition type %d for %s -> %s.",
                    //     sourceType,
                    //     source->GetName(),
                    //     target->GetName())
                    L2_LOG_ERROR(
                        Log, u"[trade] Unsupported source item transition type %d for %s -> %s.", sourceType,
                        source->GetName(), target->GetName()
                    );
                }
            }
        }

        return TradeOutcome::Completed;
    }();
    switch (tradeOutcome) {
        case TradeOutcome::MissingUpdatedSource:
            source->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot find item from inv. "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x1625,
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Source inventory item %d is missing for %s -> %s. Trade canceled.",
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Source inventory item %d is missing for %s -> %s; trade canceled.", sourceItemDatabaseId,
                source->GetName(), target->GetName()
            );
            return false;
        case TradeOutcome::MissingUpdatedTarget:
            target->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot find item from inv. "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x1643,
            //     targetItemDatabaseId,
            //     target->GetName(),
            //     source->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Target inventory item %d is missing for %s -> %s. Trade canceled.",
            //     targetItemDatabaseId,
            //     target->GetName(),
            //     source->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Target inventory item %d is missing for %s -> %s; trade canceled.", targetItemDatabaseId,
                target->GetName(), source->GetName()
            );
            return false;
        case TradeOutcome::MissingSplitSource:
            source->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot find item from inv. "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x1664,
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Split source item %d is missing for %s -> %s. Trade canceled.",
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Split source item %d is missing for %s -> %s; trade canceled.", sourceItemDatabaseId,
                source->GetName(), target->GetName()
            );
            return false;
        case TradeOutcome::TargetCreationFailed:
            source->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot create item [%d] "
            //     L"from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x1680,
            //     sourceItem->m_nClassId,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Cannot create target item class %d for %s -> %s. Trade canceled.",
            //     sourceItem->m_nClassId,
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Cannot create target item class %d for %s -> %s; trade canceled.", sourceItem->m_nClassId,
                source->GetName(), target->GetName()
            );
            return false;
        case TradeOutcome::MissingTradedSource:
            source->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot find item from trade. "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x16b4,
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Traded source item %d is missing for %s -> %s. Trade canceled.",
            //     sourceItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Traded source item %d is missing for %s -> %s; trade canceled.", sourceItemDatabaseId,
                source->GetName(), target->GetName()
            );
            return false;
        case TradeOutcome::MissingMergedTarget:
            target->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot find item from inv. "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x16d4,
            //     targetItemDatabaseId,
            //     target->GetName(),
            //     source->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Merge target item %d is missing for %s -> %s. Trade canceled.",
            //     targetItemDatabaseId,
            //     target->GetName(),
            //     source->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Merge target item %d is missing for %s -> %s; trade canceled.", targetItemDatabaseId,
                target->GetName(), source->GetName()
            );
            return false;
        case TradeOutcome::MissingMovedItem:
            source->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]cannot find item.1 "
            //     L"dbid[%d] from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x1706,
            //     targetItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Moved item %d is missing for %s -> %s. Trade canceled.",
            //     targetItemDatabaseId,
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Moved item %d is missing for %s -> %s; trade canceled.", targetItemDatabaseId,
                source->GetName(), target->GetName()
            );
            return false;
        case TradeOutcome::InvalidTradeCase:
            first->TradeCancel(false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d][ReplyTrade]Cannot occur this case. "
            //     L"from[%s] to[%s]",
            //     "DBSocket.cpp",
            //     0x16ef,
            //     source->GetName(),
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[trade] Unsupported item transition for %s -> %s. Trade canceled.",
            //     source->GetName(),
            //     target->GetName())
            L2_LOG_ERROR(
                Log, u"[trade] Unsupported item transition for %s -> %s; trade canceled.", source->GetName(),
                target->GetName()
            );
            return false;
        case TradeOutcome::Completed:
            break;
    }
    if (totalItemCount < 1) {
        first->TradeCancel(true);
    } else {
        first->TradeEnd();
    }
    return false;
}

bool DropItemPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d1b28, 3192 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    packet = ::Disassemble(packet, "dddd", &userServerId, &x, &y, &z);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xba1,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[items] Cannot apply drop-item reply: user server ID %d is not active.", userServerId);
        return false;
    }
    static_cast<void>(user->GetSocket());

    std::int32_t sourceOwnerId = 0;
    std::int32_t sourceItemDatabaseId = 0;
    std::int64_t sourceAmount = 0;
    std::int32_t droppedOwnerId = 0;
    std::int32_t droppedItemDatabaseId = 0;
    std::int64_t droppedAmount = 0;
    std::int16_t residenceGuardDrop = 0;
    std::int16_t dropOnDeath = 0;
    packet = ::Disassemble(
        packet, "ddQddQhh", &sourceOwnerId, &sourceItemDatabaseId, &sourceAmount, &droppedOwnerId, &droppedItemDatabaseId,
        &droppedAmount, &residenceGuardDrop, &dropOnDeath
    );
    static_cast<void>(droppedOwnerId);

    std::int32_t guardAngle = 0;
    std::int32_t residenceItemId = 0;
    std::int32_t residenceId = 0;
    std::int16_t guardQuerySucceeded = 0;
    if (residenceGuardDrop != 0) {
        ::Disassemble(packet, "dddh", &guardAngle, &residenceItemId, &residenceId, &guardQuerySucceeded);
    }

    if (sourceAmount == 0 && droppedAmount == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]drop item's count is 0. [%s][%d][%d]",
        //     "DBSocket.cpp",
        //     3000,
        //     user->GetName(),
        //     sourceItemDatabaseId,
        //     droppedItemDatabaseId)
        // L2_LOG_ERROR(Log,
        //     L"[items] Drop reply for character %s has zero amounts: source item %d, result item %d.",
        //     user->GetName(),
        //     sourceItemDatabaseId,
        //     droppedItemDatabaseId)
        L2_LOG_ERROR(
            Log, u"[items] Drop reply for character %s has zero amounts: source item %d, result item %d.", user->GetName(),
            sourceItemDatabaseId, droppedItemDatabaseId
        );
        if (sourceAmount == 0) {
            CItem* const item = user->m_inv.GetByDBID(sourceItemDatabaseId);
            if (item != nullptr) {
                {
                    L2_WRITE_GUARD(user->m_inv);
                    static_cast<void>(user->m_inv.Pop(item->m_data->index, false));
                    user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                }
                user->SendItemList(true, true, nullptr);
                user->LogAbnormalAction(AUA_TRY_TO_DROP_ZERO_ITEM, true, item->m_nClassId, 0, 0, 0);
            }
        }
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }

    if (sourceItemDatabaseId == 0 || droppedItemDatabaseId == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"drop fail. [%d][%d]",
        //     sourceItemDatabaseId,
        //     droppedItemDatabaseId)
        L2_LOG_ERROR(
            Log, u"[items] Drop operation failed: source DB ID %d, result DB ID %d.", sourceItemDatabaseId,
            droppedItemDatabaseId
        );
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        return false;
    }

    auto closeIllegalDrop = [user]() {
        user->StopMove();
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
    };
    const FVector position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    CItem* droppedItem = nullptr;
    if (sourceOwnerId == 0) {
        CItem* const item = user->m_inv.GetByDBID(sourceItemDatabaseId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] DropItemPacket. cannot find item(%d)",
            //     L"DBSocket.cpp",
            //     0xbdf,
            //     sourceItemDatabaseId)
            L2_LOG_ERROR(Log, u"[items] Cannot drop whole inventory item %d because it is missing.", sourceItemDatabaseId);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
            //     "DBSocket.cpp",
            //     0xbe0,
            //     sourceItemDatabaseId)
            // Noise: the preceding message already reports this exact missing
            // inventory item before the same disconnect path.
            // L2_LOG_ERROR(Log,
            //     L"[items] Cannot drop whole inventory item %d because it is missing.",
            //     sourceItemDatabaseId)
            closeIllegalDrop();
            return false;
        }

        static_cast<void>(user->UnequipItem(item, false));
        const bool removed = [&] {
            L2_WRITE_GUARD(user->m_inv);
            if (item->m_data->m_nCount < sourceAmount) {
                return false;
            }
            static_cast<void>(user->m_inv.Pop(item->m_data->index, false));
            user->m_inv.SetInventoryChanged(item, IUT_DELETE);
            return true;
        }();
        if (!removed) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Item count is invalid dbid[%d] "
            //     L"server count[%I64d] change[%I64d] drop [%I64d]",
            //     "DBSocket.cpp",
            //     0xbeb,
            //     sourceItemDatabaseId,
            //     item->m_data->m_nCount,
            //     sourceAmount,
            //     droppedAmount)
            L2_LOG_ERROR(
                Log, u"[items] Cannot drop whole item %d: inventory amount %lld, requested %lld, database result %lld.",
                sourceItemDatabaseId, static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(sourceAmount),
                static_cast<long long>(droppedAmount)
            );
            closeIllegalDrop();
            return false;
        }

        if (residenceGuardDrop != 0) {
            item->m_data->SetDirByInt(guardAngle);
        }
        {
            L2_GUARD(*item->m_lock);
            if (!item->SetAmount(droppedAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBSocket.cpp",
                //     0xbff,
                //     item->m_data->m_nCount,
                //     droppedAmount)
                L2_LOG_ERROR(
                    Log, u"[items] Cannot set whole dropped item %d amount from %lld to %lld.", sourceItemDatabaseId,
                    static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(droppedAmount)
                );
            }
        }

        if (dropOnDeath != 0) {
            if (CCursedWeaponMgr::inst().FindConst(item->m_data->m_nClassID) == nullptr) {
                CContributeDataForUserDie* const contribution = new CContributeDataForUserDie(user);
                contribution->AddTimer(20000, 0);
                L2_GUARD(*item->m_lock);
                item->m_ContributeDataID = contribution;
                item->m_nDroppedTick = runtime::GetTickCount();
            } else {
                L2_GUARD(*item->m_lock);
                item->m_ContributeDataID = static_cast<CObject*>(nullptr);
                item->m_bDroppedByAI = false;
            }
        }
        static_cast<void>(CWorld::Inst().PutItem(item, position, user));
        item->m_nTraceCharId = user->GetDBID();
        droppedItem = item;
    } else {
        CItem* sourceItem = nullptr;
        enum class SplitOutcome { MissingSource, InvalidCount, Completed };
        const auto splitOutcome = [&] {
            L2_WRITE_GUARD(user->m_inv);
            sourceItem = user->m_inv.GetByDBID(sourceItemDatabaseId);
            if (sourceItem == nullptr) {
                return SplitOutcome::MissingSource;
            }
            if (sourceItem->m_data->m_nCount < sourceAmount) {
                return SplitOutcome::InvalidCount;
            }
            {
                L2_GUARD(*sourceItem->m_lock);
                if (!sourceItem->AddAmount(-sourceAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0xc39,
                    //     sourceItem->m_data->m_nCount,
                    //     -sourceAmount)
                    L2_LOG_ERROR(
                        Log, u"[items] Cannot subtract %lld from split source item %d amount %lld.",
                        static_cast<long long>(sourceAmount), sourceItemDatabaseId,
                        static_cast<long long>(sourceItem->m_data->m_nCount)
                    );
                }
            }

            droppedItem = static_cast<CItem*>(sourceItem->Copy());
            if (!droppedItem->SetAmount(droppedAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBSocket.cpp",
                //     0xc41,
                //     droppedItem->m_data->m_nCount,
                //     droppedAmount)
                L2_LOG_ERROR(
                    Log, u"[items] Cannot set split dropped item %d amount from %lld to %lld.", droppedItemDatabaseId,
                    static_cast<long long>(droppedItem->m_data->m_nCount), static_cast<long long>(droppedAmount)
                );
            }
            droppedItem->SetDBID(static_cast<unsigned int>(droppedItemDatabaseId));
            droppedItem->m_nTraceCharId = user->GetDBID();
            droppedItem->m_data->m_nInventorySlotIndex = -1;
            user->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);
            return SplitOutcome::Completed;
        }();
        switch (splitOutcome) {
            case SplitOutcome::MissingSource:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]Cannot find item from inv. dbid[%d]",
                //     "DBSocket.cpp",
                //     0xc28,
                //     sourceItemDatabaseId)
                L2_LOG_ERROR(
                    Log, u"[items] Cannot split inventory item %d for dropping because it is missing.", sourceItemDatabaseId
                );
                closeIllegalDrop();
                return false;
            case SplitOutcome::InvalidCount:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]Item count is invalid dbid[%d] "
                //     L"server count[%I64d] result[%I64d] drop [%I64d]",
                //     "DBSocket.cpp",
                //     0xc31,
                //     sourceItemDatabaseId,
                //     sourceItem->m_data->m_nCount,
                //     sourceAmount,
                //     droppedAmount)
                L2_LOG_ERROR(
                    Log,
                    u"[items] Cannot split item %d for dropping: inventory amount %lld, requested %lld, database result "
                    u"%lld.",
                    sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                    static_cast<long long>(sourceAmount), static_cast<long long>(droppedAmount)
                );
                closeIllegalDrop();
                return false;
            case SplitOutcome::Completed:
                break;
        }
        if (residenceGuardDrop != 0) {
            droppedItem->m_data->SetDirByInt(guardAngle);
        }
        static_cast<void>(CWorld::Inst().PutItem(droppedItem, position, user));
    }

    if (residenceGuardDrop != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        if (guardQuerySucceeded == 0) {
            if (residence != nullptr) {
                L2_WRITE_GUARD(*residence);
                const auto count = residence->m_GuardNumMap.find(residenceItemId);
                if (count != residence->m_GuardNumMap.end()) {
                    --count->second;
                }
                const auto check = residence->m_mapResidenceGuardDataCheck.find(position);
                if (check != residence->m_mapResidenceGuardDataCheck.end()) {
                    residence->m_mapResidenceGuardDataCheck.erase(check);
                }
            }
            // Log.Add(LOG_ERROR, L"Error in Castleguard Load!!!!")
            L2_LOG_ERROR(Log, u"[castle guards] Guard load failed; the current entry is rejected.");
        } else if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Critical Error in Castleguard Load!!!!")
            L2_LOG_ERROR(Log, u"[castle guards] Guard load failed critically; load processing stopped.");
        } else {
            L2_WRITE_GUARD(*residence);
            const auto guard = residence->m_mapResidenceGuardItem.find(static_cast<std::uint32_t>(residenceItemId));
            if (guard == residence->m_mapResidenceGuardItem.end()) {
                const auto check = residence->m_mapResidenceGuardDataCheck.find(position);
                if (check != residence->m_mapResidenceGuardDataCheck.end()) {
                    residence->m_mapResidenceGuardDataCheck.erase(check);
                }
                // Log.Add(
                //     LOG_ERROR,
                //     L"Critical Error in Castleguard Load!!!!")
                L2_LOG_ERROR(Log, u"[castle guards] Guard load failed critically; load processing stopped.");
            } else {
                ResidenceGuardInfo* const info = guard->second;
                FAngle angle{};
                angle.Yaw = guardAngle;
                ResidenceGuardData* const data = new ResidenceGuardData(
                    static_cast<std::uint32_t>(residenceItemId), info->nGuardType, info->bMoveable, angle, info->nNpcId,
                    droppedItem->Id()
                );
                residence->m_mapResidenceGuardData.emplace(position, data);
            }
        }
    }

    user->SendItemList(true, true, nullptr);
    if (user->IsNowTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]item drop & trade try.",
        //     "DBSocket.cpp",
        //     0xc9f)
        // L2_LOG_ERROR(Log,
        //     L"[trade] Character %s dropped item %d while a trade was active. Trade canceled.",
        //     user->GetName(),
        //     droppedItemDatabaseId)
        L2_LOG_ERROR(
            Log, u"[trade] Character %s dropped item %d while a trade was active; trade canceled.", user->GetName(),
            droppedItemDatabaseId
        );
        user->TradeCancel(false);
    }
    runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
    return false;
}

bool PickItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d3bd8, 2249 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t sourceItemDatabaseId = 0;
    std::int32_t itemServerId = 0;
    std::int64_t pickedAmount = 0;
    std::int32_t resultItemDatabaseId = 0;
    std::int64_t resultAmount = 0;
    std::int32_t resultType = 0;
    std::int16_t unloadResidenceGuard = 0;
    packet = ::Disassemble(
        packet, "dddQdQdh", &userServerId, &sourceItemDatabaseId, &itemServerId, &pickedAmount, &resultItemDatabaseId,
        &resultAmount, &resultType, &unloadResidenceGuard
    );

    std::int32_t guardX = 0;
    std::int32_t guardY = 0;
    std::int32_t guardZ = 0;
    std::int16_t guardQuerySucceeded = 0;
    if (unloadResidenceGuard != 0) {
        ::Disassemble(packet, "dddh", &guardX, &guardY, &guardZ, &guardQuerySucceeded);
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2eea,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[items] Cannot apply pick-item reply: user server ID %d is not active.", userServerId);
        return false;
    }
    static_cast<void>(user->GetSocket());

    CItem* const pickedItem = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
    if (pickedItem == nullptr) {
        return false;
    }

    const int itemClassId = pickedItem->m_nClassId;
    const int itemEnchant = pickedItem->m_data->GetEnchanted();
    bool succeeded = true;
    if (resultType == 0) {
        CItem* const inventoryItem = user->m_inv.GetByDBID(resultItemDatabaseId);
        if (inventoryItem == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Pick item not in inven. dbid[%d] cls[%d] "
            //     L"[%s][%d][%d] pItem dbid[%d] [%d]",
            //     resultItemDatabaseId,
            //     itemClassId,
            //     user->GetName(),
            //     pickedAmount,
            //     resultAmount,
            //     static_cast<int>(pickedItem->m_data->m_nDBID),
            //     pickedItem->m_data->m_nCount)
            // L2_LOG_ERROR(Log,
            //     L"[items] Pick result is absent from inventory: result DB ID %d, "
            //     L"class %d, user '%s', picked %lld, result %lld, source DB ID %d, "
            //     L"source count %lld.",
            //     resultItemDatabaseId,
            //     itemClassId,
            //     user->GetName(),
            //     static_cast<long long>(pickedAmount),
            //     static_cast<long long>(resultAmount),
            //     static_cast<int>(pickedItem->m_data->m_nDBID),
            //     static_cast<long long>(pickedItem->m_data->m_nCount))
            L2_LOG_ERROR(
                Log,
                u"[items] Pick result is absent from inventory: result DB ID %d, "
                u"class %d, user '%s', picked %lld, result %lld, source DB ID %d, "
                u"source count %lld.",
                resultItemDatabaseId, itemClassId, user->GetName(), static_cast<long long>(pickedAmount),
                static_cast<long long>(resultAmount), static_cast<int>(pickedItem->m_data->m_nDBID),
                static_cast<long long>(pickedItem->m_data->m_nCount)
            );
            succeeded = false;
        } else {
            if (!inventoryItem->AddAmount(pickedAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d] "
                //     L"user[%s] item[%d]",
                //     "DBSocket.cpp",
                //     0x2efe,
                //     inventoryItem->m_data->m_nCount,
                //     pickedAmount,
                //     user->GetName(),
                //     inventoryItem->m_nClassId)
                // L2_LOG_ERROR(Log,
                //     L"[items] Cannot add picked amount %lld to item %d (class %d) for character %s. Current amount %lld.",
                //     static_cast<long long>(pickedAmount),
                //     resultItemDatabaseId,
                //     inventoryItem->m_nClassId,
                //     user->GetName(),
                //     static_cast<long long>(inventoryItem->m_data->m_nCount))
                L2_LOG_ERROR(
                    Log,
                    u"[items] Cannot add picked amount %lld to item %d (class %d) for character %s; current amount %lld.",
                    static_cast<long long>(pickedAmount), resultItemDatabaseId, inventoryItem->m_nClassId, user->GetName(),
                    static_cast<long long>(inventoryItem->m_data->m_nCount)
                );
            }
            if (inventoryItem->m_data->m_nCount != resultAmount) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Pick item result is different. "
                //     L"server[%I64d], db[%I64d] user[%s] item[%d]",
                //     "DBSocket.cpp",
                //     0x2f02,
                //     inventoryItem->m_data->m_nCount,
                //     resultAmount,
                //     user->GetName(),
                //     inventoryItem->m_nClassId)
                // L2_LOG_ERROR(Log,
                //     L"[items] Picked item %d (class %d) amount differs for character %s: server %lld, database %lld.",
                //     resultItemDatabaseId,
                //     inventoryItem->m_nClassId,
                //     user->GetName(),
                //     static_cast<long long>(inventoryItem->m_data->m_nCount),
                //     static_cast<long long>(resultAmount))
                L2_LOG_ERROR(
                    Log, u"[items] Picked item %d (class %d) amount differs for character %s: server %lld, database %lld.",
                    resultItemDatabaseId, inventoryItem->m_nClassId, user->GetName(),
                    static_cast<long long>(inventoryItem->m_data->m_nCount), static_cast<long long>(resultAmount)
                );
            }
            user->m_inv.SetInventoryChanged(inventoryItem, IUT_UPDATE);
            pickedItem->m_data->m_nInventorySlotIndex = inventoryItem->m_data->m_nInventorySlotIndex;
        }
        pickedItem->Delete();
    } else if (resultType == 1) {
        if (pickedItem->GetDBID() == 0) {
            pickedItem->SetDBID(static_cast<unsigned int>(resultItemDatabaseId));
        } else if (static_cast<int>(pickedItem->GetDBID()) != sourceItemDatabaseId) {
            // Log.Add(LOG_ERROR, L"chit.")
            L2_LOG_ERROR(
                Log,
                u"[items] Picked item DB ID conflicts with the database reply; "
                u"the pickup is marked failed."
            );
            succeeded = false;
        }
        if (!user->m_inv.Push(pickedItem->m_data->index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Push item fail. char name[%s] item class id [%d]",
            //     user->GetName(),
            //     pickedItem->m_nClassId)
            // L2_LOG_ERROR(Log,
            //     L"[inventory] Cannot insert picked item class %d for "
            //     L"character '%s'. The pickup is marked failed.",
            //     pickedItem->m_nClassId,
            //     user->GetName())
            L2_LOG_ERROR(
                Log,
                u"[inventory] Cannot insert picked item class %d for "
                u"character '%s'; the pickup is marked failed.",
                pickedItem->m_nClassId, user->GetName()
            );
            succeeded = false;
        }
        if (succeeded) {
            user->m_inv.SetInventoryChanged(pickedItem, IUT_ADD);
            user->AutoEquipArrowOrBolt(pickedItem);
            if (pickedItem->m_const->m_bForceEquip) {
                user->ForceEquipItem(pickedItem);
            }
        }
    } else if (resultType == 2) {
        // Log.Add(LOG_ERROR, L"error. in db pickitem. db fail.")
        L2_LOG_ERROR(Log, u"[items] Database rejected the pick-item operation.");
    } else {
        // Log.Add(LOG_ERROR, L"error. in db pickitem")
        L2_LOG_ERROR(Log, u"[items] Pick-item reply returned an unsupported result type.");
    }

    if (succeeded && (resultType == 0 || resultType == 1)) {
        CCursedWeaponMgr::inst().OnPickUp(user, pickedItem);
        CEventManager::inst().OnPickUp(user, pickedItem);
        if (itemClassId == 0x39) {
            user->GetSocket()->Send("chccQ", 0x62, SystemMessageId::OBTAINED_ADENA, 1, 6, pickedAmount);
        } else if (pickedAmount < 2) {
            const bool itemOption = pickedItem->m_itemOption.itemOptionKey.variationOption1 > 0 &&
                pickedItem->m_itemOption.itemOptionKey.variationOption2 > 0;
            if (itemClassId == 0x115a || itemEnchant < 1) {
                user->GetSocket()->Send("chccdc", 0x62, SystemMessageId::PICKED_UP_ITEM, 1, 8, itemClassId, itemOption);
            } else {
                user->GetSocket()->Send(
                    "chccccdc", 0x62, SystemMessageId::PICKED_UP_ENCHANTED_ITEM, 2, 0x13,
                    static_cast<int>(static_cast<char>(itemEnchant)), 8, itemClassId, itemOption
                );
            }
            user->BroadcastItemGet2Party(300, itemClassId, 1, itemEnchant, itemOption);
        } else {
            user->GetSocket()->Send(
                "chccdcQ", 0x62, SystemMessageId::PICKED_UP_MULTIPLE_ITEMS, 2, 3, itemClassId, 6, pickedAmount
            );
            user->BroadcastItemGet2Party(299, itemClassId, pickedAmount, 0, false);
        }
    }

    if (unloadResidenceGuard != 0) {
        if (guardQuerySucceeded == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Critical Error in Castleguard unLoad!!!! Type E")
            L2_LOG_ERROR(Log, u"[castle guards] Guard unload failed at type E; processing stopped.");
        } else {
            const FVector guardPosition{
                static_cast<double>(guardX), static_cast<double>(guardY), static_cast<double>(guardZ)};
            CPledge* const pledge = user->GetPledge();
            CCastle* const castle = pledge != nullptr ? pledge->GetCastle() : nullptr;
            if (castle != nullptr && user->HavePledgePower(PP_MANAGE_MERCENARY)) {
                CResidence* const residence = castle->CastResidence();
                if (residence == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Critical Error in Castleguard unLoad!!!! Type D")
                    L2_LOG_ERROR(Log, u"[castle guards] Guard unload failed at type D; processing stopped.");
                } else {
                    L2_WRITE_GUARD(*residence);
                    const auto guard = residence->m_mapResidenceGuardData.find(guardPosition);
                    if (guard == residence->m_mapResidenceGuardData.end()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Critical Error in Castleguard unLoad!!!! Type B")
                        L2_LOG_ERROR(Log, u"[castle guards] Guard unload failed at type B; processing stopped.");
                    } else {
                        ResidenceGuardData* const data = guard->second;
                        const auto count = residence->m_GuardNumMap.find(static_cast<int>(data->nItemId));
                        if (count != residence->m_GuardNumMap.end()) {
                            if (count->second < 1) {
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"Critical Error in Castleguard "
                                //     L"unLoad!!!! Type A")
                                L2_LOG_ERROR(Log, u"[castle guards] Guard unload failed at type A; processing stopped.");
                            } else {
                                --count->second;
                            }
                        }
                        delete data;
                        residence->m_mapResidenceGuardData.erase(guard);
                    }
                    const auto check = residence->m_mapResidenceGuardDataCheck.find(guardPosition);
                    if (check == residence->m_mapResidenceGuardDataCheck.end()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Critical Error in Castleguard unLoad!!! Type C")
                        L2_LOG_ERROR(Log, u"[castle guards] Guard unload failed at type C; processing stopped.");
                    } else {
                        residence->m_mapResidenceGuardDataCheck.erase(check);
                    }
                }
            }
        }
    }

    user->SendItemList(true, true, nullptr);
    return false;
}

} // namespace DBPacketHandler

bool ReplyPrivateStoreCommit(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006be714, 8928 bytes.
    L2_GUARD_FUNCTION();

    std::int32_t firstDatabaseId = 0;
    std::int32_t secondDatabaseId = 0;
    packet = ::Disassemble(packet, "dd", &firstDatabaseId, &secondDatabaseId);

    const int databaseId = runtime::process::CurrentDatabaseId();
    CSPointer<User> firstPointer = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, firstDatabaseId));
    CSPointer<User> secondPointer = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(databaseId, secondDatabaseId));
    User* const first = firstPointer.get();
    User* const second = secondPointer.get();

    if (first != nullptr) {
        {
            L2_GUARD(first->m_PrivateStoreLock);
            first->m_bWaitPrivateStoreCommit = false;
        }
    }

    const auto resetItemWaits = [](User* const user) {
        runtime::raw_atomic::ExchangeI32(&user->m_nWaitGetItemOnce, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    };

    if (first == nullptr || second == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, world id[%d] "
        //     L"dbid[%d], dbid[%d]",
        //     databaseId,
        //     firstDatabaseId,
        //     secondDatabaseId)
        L2_LOG_ERROR(
            Log,
            u"[database reply] Character lookup failed: world %d, first DB ID "
            u"%d, second DB ID %d.",
            databaseId, firstDatabaseId, secondDatabaseId
        );
        for (User* const user : {first, second}) {
            if (user == nullptr) {
                continue;
            }
            resetItemWaits(user);
            user->StopMove();
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            }
        }
        return false;
    }

    struct TransactionFailure {
        const wchar_t* message;
        int line;
        int itemDatabaseId;
        bool hasItemDatabaseId;
    } failure{};
    int totalItemCount;
    const bool transactionCompleted = [&] {
        L2_WRITE_GUARD(first->m_inv);
        L2_WRITE_GUARD(second->m_inv);

        const auto abortTransaction = [&](const wchar_t* const message, const int line, const int itemDatabaseId,
                                          const bool hasItemDatabaseId) {
            failure = {message, line, itemDatabaseId, hasItemDatabaseId};
            return false;
        };

        const auto copyCharacterName = [](char16_t (&destination)[24], const char16_t* const source) {
            std::fill(std::begin(destination), std::end(destination), u'\0');
            if (source == nullptr) {
                return;
            }
            const std::u16string_view name(source);
            const std::size_t count = std::min<std::size_t>(name.size(), 23);
            std::copy_n(name.data(), count, destination);
        };

        totalItemCount = 0;
        for (int section = 0; section < 2; ++section) {
            User* const source = section == 0 ? first : second;
            User* const target = section == 0 ? second : first;
            std::int32_t itemCount = 0;
            packet = ::Disassemble(packet, "d", &itemCount);
            totalItemCount += itemCount;

            int sourceItemServerIdForResult = 0;
            int targetItemServerIdForResult = 0;
            int itemClassId = 0;
            int itemEnchant = 0;
            ItemConsumeType consumeType = ICT_NORMAL;

            for (int index = 0; index < itemCount; ++index) {
                std::int32_t sourceType = 0;
                std::int32_t sourceItemDatabaseId = 0;
                std::int32_t sourceItemServerId = 0;
                std::int64_t sourceAmount = 0;
                std::int32_t targetType = 0;
                std::int32_t targetItemDatabaseId = 0;
                std::int32_t targetItemServerId = 0;
                std::int64_t targetAmount = 0;
                std::int64_t transferAmount = 0;
                packet = ::Disassemble(
                    packet, "dddQdddQQ", &sourceType, &sourceItemDatabaseId, &sourceItemServerId, &sourceAmount, &targetType,
                    &targetItemDatabaseId, &targetItemServerId, &targetAmount, &transferAmount
                );
                static_cast<void>(sourceItemServerId);
                static_cast<void>(targetItemServerId);

                CItem* sourceItem = nullptr;
                CItem* targetItem = nullptr;
                bool publishResult = true;

                if (sourceType == 1) {
                    if (targetType == 1) {
                        sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
                        if (sourceItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Source inventory item DB ID %d is missing; transaction aborted.", 0x24ad,
                                sourceItemDatabaseId, true
                            );
                        }
                        if (!sourceItem->SetAmount(sourceAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x24bf,
                            //     sourceItem->m_data->m_nCount,
                            //     sourceAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot set source item %d amount from %lld to %lld.",
                                sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                                static_cast<long long>(sourceAmount)
                            );
                        }
                        source->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);

                        targetItem = target->m_inv.GetByDBID(targetItemDatabaseId);
                        if (targetItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Target inventory item DB ID %d is missing; transaction aborted.", 0x24c9,
                                targetItemDatabaseId, true
                            );
                        }
                        if (!targetItem->SetAmount(targetAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x24db,
                            //     targetItem->m_data->m_nCount,
                            //     targetAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot set target item %d amount from %lld to %lld.",
                                targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                                static_cast<long long>(targetAmount)
                            );
                        }
                        target->m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);
                    } else if (targetType == 2) {
                        sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
                        if (sourceItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Split source item DB ID %d is missing; transaction aborted.", 0x24ef,
                                sourceItemDatabaseId, true
                            );
                        }
                        if (!sourceItem->SetAmount(sourceAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x2500,
                            //     sourceItem->m_data->m_nCount,
                            //     sourceAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot set split source item %d amount from %lld to %lld.",
                                sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                                static_cast<long long>(sourceAmount)
                            );
                        }
                        source->m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);

                        targetItem = g_ObjectDB.CreateItem(sourceItem->m_data->m_nClassID);
                        if (targetItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Cannot create target item class %d; transaction aborted.", 0x2509,
                                sourceItem->m_data->m_nClassID, true
                            );
                        }
                        targetItem->SetDBID(static_cast<unsigned int>(targetItemDatabaseId));
                        if (!targetItem->SetAmount(targetAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x251b,
                            //     targetItem->m_data->m_nCount,
                            //     targetAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot initialize target item %d amount from %lld to %lld.",
                                targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                                static_cast<long long>(targetAmount)
                            );
                        }
                        if (!target->m_inv.Push(targetItem->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     "DBSocket.cpp",
                            //     0x2520)
                            L2_LOG_ERROR(
                                Log,
                                "[inventory] Item insertion failed while transferring database item %d to character %u.",
                                targetItemDatabaseId, target->GetDBID()
                            );
                            publishResult = false;
                        } else {
                            target->m_inv.SetInventoryChanged(targetItem, IUT_ADD);
                        }
                    } else if (targetType == 4) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d]Cannot occur this case.",
                        //     "DBSocket.cpp",
                        //     0x252f)
                        L2_LOG_ERROR(
                            Log, u"[private store] Unsupported item transition type 1 -> 4; result publication skipped."
                        );
                        publishResult = false;
                    }
                } else if (sourceType == 3) {
                    if (targetType == 1) {
                        sourceItem = source->m_inv.GetByDBID(sourceItemDatabaseId);
                        if (sourceItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Traded source item DB ID %d is missing; transaction aborted.", 0x2540,
                                sourceItemDatabaseId, true
                            );
                        }
                        const SlotType unequipped = source->UnequipItem(sourceItem, true);
                        if (!sourceItem->SetAmount(targetAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x255d,
                            //     sourceItem->m_data->m_nCount,
                            //     targetAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot clear traded source item %d amount from %lld to %lld.",
                                sourceItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                                static_cast<long long>(targetAmount)
                            );
                        }
                        if (unequipped != ST_NONE) {
                            source->m_inv.Pop(sourceItem->m_data->index, false);
                            source->m_inv.SetInventoryChanged(sourceItem, IUT_DELETE);
                            sourceItem->Delete();
                        }

                        targetItem = target->m_inv.GetByDBID(targetItemDatabaseId);
                        if (targetItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Merge target item DB ID %d is missing; transaction aborted.", 0x256d,
                                targetItemDatabaseId, true
                            );
                        }
                        if (!targetItem->SetAmount(targetAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x257e,
                            //     targetItem->m_data->m_nCount,
                            //     targetAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot set merge target item %d amount from %lld to %lld.",
                                targetItemDatabaseId, static_cast<long long>(targetItem->m_data->m_nCount),
                                static_cast<long long>(targetAmount)
                            );
                        }
                        target->m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);
                    } else if (targetType == 2) {
                        return abortTransaction(
                            L"[private store] Unsupported source/target item combination; transaction aborted.", 0x258d, 0,
                            false
                        );
                    } else if (targetType == 4) {
                        sourceItem = source->m_inv.GetByDBID(targetItemDatabaseId);
                        if (sourceItem == nullptr) {
                            return abortTransaction(
                                L"[private store] Moved item DB ID %d is missing; transaction aborted.", 0x25a4,
                                targetItemDatabaseId, true
                            );
                        }
                        static_cast<void>(source->UnequipItem(sourceItem, false));
                        if (!sourceItem->SetAmount(targetAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x25b8,
                            //     sourceItem->m_data->m_nCount,
                            //     targetAmount)
                            L2_LOG_ERROR(
                                Log, u"[private store] Cannot set moved item %d amount from %lld to %lld.",
                                targetItemDatabaseId, static_cast<long long>(sourceItem->m_data->m_nCount),
                                static_cast<long long>(targetAmount)
                            );
                        }
                        source->m_inv.Pop(sourceItem->m_data->index, false);
                        source->m_inv.SetInventoryChanged(sourceItem, IUT_DELETE);
                        if (!target->m_inv.Push(sourceItem->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     "DBSocket.cpp",
                            //     0x25bf)
                            L2_LOG_ERROR(
                                Log, "[inventory] Existing item %u could not be inserted into character %u after transfer.",
                                sourceItem->GetDBID(), target->GetDBID()
                            );
                        }
                        target->m_inv.SetInventoryChanged(sourceItem, IUT_ADD);
                        targetItem = sourceItem;
                    }
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d]cannot occur this case.",
                    //     "DBSocket.cpp",
                    //     0x25cf)
                    L2_LOG_ERROR(
                        Log, u"[private store] Unsupported source item transition type %d; result publication skipped.",
                        sourceType
                    );
                    continue;
                }

                if (!publishResult) {
                    continue;
                }
                if (sourceItem != nullptr && targetItem != nullptr) {
                    sourceItemServerIdForResult = static_cast<int>(sourceItem->Id());
                    targetItemServerIdForResult = static_cast<int>(targetItem->Id());
                    itemClassId = sourceItem->m_data->m_nClassID;
                    itemEnchant = sourceItem->m_data->GetEnchanted();
                    consumeType = sourceItem->m_data->m_ItemConsumeType;
                }

                if (section != 0) {
                    continue;
                }

                if (consumeType == ICT_STACKABLE || consumeType == ICT_ASSET) {
                    source->GetSocket()->Send(
                        "chccScdcQ", 0x62, SystemMessageId::CHARACTER_BOUGHT_MULTIPLE_ITEMS, 3, 0x0c, target->GetName(), 3,
                        itemClassId, 6, transferAmount
                    );
                    target->GetSocket()->Send(
                        "chccScdcQ", 0x62, SystemMessageId::BOUGHT_MULTIPLE_ITEMS_FROM_CHARACTER, 3, 0x0c, source->GetName(),
                        3, itemClassId, 6, transferAmount
                    );
                } else if (itemEnchant > 0 && itemClassId != 0x115a) {
                    source->GetSocket()->Send(
                        "chccScccd", 0x62, SystemMessageId::CHARACTER_BOUGHT_ENCHANTED_ITEM, 3, 0x0c, target->GetName(),
                        0x13, static_cast<int>(static_cast<char>(itemEnchant)), 3, itemClassId
                    );
                    target->GetSocket()->Send(
                        "chccScccd", 0x62, SystemMessageId::BOUGHT_ENCHANTED_ITEM_FROM_CHARACTER, 3, 0x0c, source->GetName(),
                        0x13, static_cast<int>(static_cast<char>(itemEnchant)), 3, itemClassId
                    );
                } else {
                    source->GetSocket()->Send(
                        "chccScd", 0x62, SystemMessageId::CHARACTER_BOUGHT_ITEM, 2, 0x0c, target->GetName(), 3, itemClassId
                    );
                    target->GetSocket()->Send(
                        "chccScd", 0x62, SystemMessageId::BOUGHT_ITEM_FROM_CHARACTER, 2, 0x0c, source->GetName(), 3,
                        itemClassId
                    );
                }

                protocol::S_EX_PRIVATE_STORE_BUYING_RESULT buyingResult{};
                buyingResult.nItemSid = targetItemServerIdForResult;
                buyingResult.nAmount = transferAmount;
                copyCharacterName(buyingResult.charName, source->GetName());
                target->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1c5), buyingResult);

                protocol::S_EX_PRIVATE_STORE_SELLING_RESULT sellingResult{};
                sellingResult.nItemSid = sourceItemServerIdForResult;
                sellingResult.nAmount = transferAmount;
                copyCharacterName(sellingResult.charName, target->GetName());
                source->GetSocket()->Send(static_cast<S_PROTOCOL>(0xfe), static_cast<S_EX_PROTOCOL>(0x1c6), sellingResult);
            }
        }

        return true;
    }();
    if (!transactionCompleted) {
        const auto& [message, line, itemDatabaseId, hasItemDatabaseId] = failure;
        static_cast<void>(line);
        if (hasItemDatabaseId) {
            // Log.Add(
            //     LOG_ERROR,
            //     message,
            //     "DBSocket.cpp",
            //     line,
            //     itemDatabaseId)
            L2_LOG_ERROR(Log, message, itemDatabaseId);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     message,
            //     "DBSocket.cpp",
            //     line)
            L2_LOG_ERROR(Log, message);
        }
        resetItemWaits(first);
        resetItemWaits(second);
        return false;
    }
    resetItemWaits(first);
    resetItemWaits(second);
    if (totalItemCount > 0) {
        first->SendItemList(true, true, nullptr);
        second->SendItemList(true, true, nullptr);
    }

    if (first->IsNowTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]private store & trade try.",
        //     "DBSocket.cpp",
        //     0x264a)
        // L2_LOG_ERROR(Log,
        //     L"[trade] Character %s entered a private-store transaction while a trade was active. Trade canceled.",
        //     first->GetName())
        L2_LOG_ERROR(
            Log, u"[trade] Character %s entered a private-store transaction while a trade was active; trade canceled.",
            first->GetName()
        );
        first->TradeCancel(false);
    }
    if (second->IsNowTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]private store & trade try.",
        //     "DBSocket.cpp",
        //     0x2652)
        // L2_LOG_ERROR(Log,
        //     L"[trade] Character %s entered a private-store transaction while a trade was active. Trade canceled.",
        //     second->GetName())
        L2_LOG_ERROR(
            Log, u"[trade] Character %s entered a private-store transaction while a trade was active; trade canceled.",
            second->GetName()
        );
        second->TradeCancel(false);
    }
    return false;
}

bool DBSocketUtil::ParseItem(const unsigned char*& packet, User& user, const bool sendMessage, const bool adenaMessage) {
    // Current L2Server.exe 0x006c92d8.
    L2_GUARD_FUNCTION();

    CItemDisassemblerFromCacheD itemData;
    packet = itemData.DisassembleItemInfo(packet);

    CItem* item = user.m_inv.GetByDBID(itemData.m_nDBID);
    if (item == nullptr) {
        item = g_ObjectDB.CreateItem(itemData.m_nClassID);
        if (item == nullptr) {
            // Log.Add(LOG_ERROR, L"CreateItem fail.")
            L2_LOG_ERROR(Log, u"[inventory] Item creation failed; the database reply cannot be applied.");
            return false;
        }
        if (item->IsDuration()) {
            item->SetDuration(itemData.m_nDuration);
        }
        if (!user.m_inv.Push(item->m_data->index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "inven push failed at file[%s], line[%d]",
            //     "DBSocket.cpp",
            //     0x4747)
            L2_LOG_ERROR(
                Log, "[inventory] Item insertion failed while parsing database item %d for character %u.", itemData.m_nDBID,
                user.GetDBID()
            );
        }
        user.m_inv.SetInventoryChanged(item, IUT_ADD);
    }

    {
        L2_GUARD(*item->m_lock);
        item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
        if (!item->SetAmount(itemData.m_nAmount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
            //     "DBSocket.cpp",
            //     0x4755,
            //     item->m_data->m_nCount,
            //     itemData.m_nAmount)
            L2_LOG_ERROR(
                Log, u"[inventory] Cannot set database item %d amount from %lld to %lld for character %u.", itemData.m_nDBID,
                static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nAmount), user.GetDBID()
            );
        }
        item->SetEnchanted(itemData.m_nEnchanted);
        item->m_Useable = user.CheckUseable(item);
        item->m_data->m_nDamaged = itemData.m_nDamaged;
        item->m_data->m_bBlessed = itemData.m_nIsBlessed;
        item->SetVariation(
            ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
            static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
        );
        item->SetAttribute(itemData.m_Attribute);
        if (item->m_const->m_dAgathionMaxEnergy > 0.0) {
            db.RequestLoadItemAgathionEnergy(&user, item->m_data->m_nId, static_cast<int>(item->GetDBID()));
        }
        item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
        item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
        item->m_data->SetLocked(itemData.m_hLocked);
        item->m_data->SetUseCount(itemData.m_hUseCount);
    }

    user.m_inv.SetInventoryChanged(item, IUT_UPDATE);
    if (sendMessage) {
        std::int64_t amount = 0;
        packet = ::Disassemble(packet, "Q", &amount);
        if (adenaMessage || item->m_data->m_nClassID != 0x39) {
            user.GetSocket()->Send(
                "chccdcQ", 0x62, SystemMessageId::OBTAINED_MULTIPLE_ITEMS, 2, 3, item->GetClassId(), 6,
                static_cast<long long>(amount)
            );
        }
    }
    return true;
}

bool WithdrawWarehouseItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ca6f4. The paired current CacheD producer is
    // primary request 13 / reply 10 and may package complete item records.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t totalItemCount = 0;
    std::int32_t currentPackageItemCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &totalItemCount, &currentPackageItemCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x984,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[warehouse] Cannot apply withdrawal reply: user server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (totalItemCount < 1) {
        user->GetSocket()->Send("cd", 0x43, totalItemCount);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        return false;
    }

    static_cast<void>(runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0));

    {
        L2_WRITE_GUARD(user->m_inv);
        for (int index = 0; index < currentPackageItemCount; ++index) {
            const bool parsed = DBSocketUtil::ParseItem(packet, *user, false, false);
            runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
            if (!parsed) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] error DBSocketUtil::ParseItem(). "
                //     L"char_id[%s][%d], nItemCount[%d/%d]",
                //     L"DBSocket.cpp",
                //     0x9a5,
                //     user->GetName(),
                //     user->GetDBID(),
                //     currentPackageItemCount,
                //     totalItemCount)
                // L2_LOG_ERROR(Log,
                //     L"[warehouse] Cannot parse withdrawal item for character %s (%d): package item %d of %d.",
                //     user->GetName(),
                //     user->GetDBID(),
                //     currentPackageItemCount,
                //     totalItemCount)
                L2_LOG_ERROR(
                    Log, u"[warehouse] Cannot parse withdrawal item for character %s (%d): package item %d of %d.",
                    user->GetName(), user->GetDBID(), currentPackageItemCount, totalItemCount
                );
                break;
            }
        }
    }

    {
        L2_GUARD(*user->m_lock);
        if (user->m_lPackageItemPacketJobFlag == 0 ||
            user->m_lTotalPackageItemCount != user->m_lCurrentReceivedPackageItemCount) {
            return false;
        }

        runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
        runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
        if (user->m_pEconomy != nullptr) {
            user->GetSocket()->Send("c", 0x43);
            user->ReleaseEconomy();
        }
    }
    user->SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_SUCCEEDED);
    return false;
}

bool ReplyWithdrawWarehouseItemsEx(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006cf834. The paired current CacheD producer is
    // primary request 146 / reply 0x7e and keeps package framing.
    L2_GUARD_FUNCTION();

    std::int32_t warehouseClassType = 0;
    std::int32_t warehouseOwnerId = 0;
    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t totalItemCount = 0;
    std::int32_t currentPackageItemCount = 0;
    packet = ::Disassemble(
        packet, "dddddd", &warehouseClassType, &warehouseOwnerId, &userServerId, &result, &totalItemCount,
        &currentPackageItemCount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x32b6,
        //     userServerId)
        L2_LOG_ERROR(
            Log, u"[warehouse] Cannot apply extended withdrawal reply: user server ID %d is not active.", userServerId
        );
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result < 1) {
        static_cast<void>(runtime::raw_atomic::CompareExchangeI32(&user->m_lTotalPackageItemCount, totalItemCount, 0));

        {
            L2_WRITE_GUARD(user->m_inv);
            for (int index = 0; index < currentPackageItemCount; ++index) {
                const bool parsed = DBSocketUtil::ParseItem(packet, *user, false, false);
                runtime::raw_atomic::IncrementI32(&user->m_lCurrentReceivedPackageItemCount);
                if (!parsed) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] error DBSocketUtil::ParseItem(). "
                    //     L"char_id[%s][%d], WareHouseType[%d], "
                    //     L"WareHouseID[%d], nUserServerId[%d], "
                    //     L"nItemCount[%d/%d]",
                    //     L"DBSocket.cpp",
                    //     0x32e7,
                    //     user->GetName(),
                    //     user->GetDBID(),
                    //     warehouseClassType,
                    //     warehouseOwnerId,
                    //     userServerId,
                    //     currentPackageItemCount,
                    //     totalItemCount)
                    // L2_LOG_ERROR(Log,
                    //     L"[warehouse] Cannot parse extended withdrawal item for character %s (%d): "
                    //     L"warehouse class %d, owner %d, user server ID %d, package item %d of %d.",
                    //     user->GetName(),
                    //     user->GetDBID(),
                    //     warehouseClassType,
                    //     warehouseOwnerId,
                    //     userServerId,
                    //     currentPackageItemCount,
                    //     totalItemCount)
                    L2_LOG_ERROR(
                        Log,
                        u"[warehouse] Cannot parse extended withdrawal item for character %s (%d): "
                        u"warehouse class %d, owner %d, user server ID %d, package item %d of %d.",
                        user->GetName(), user->GetDBID(), warehouseClassType, warehouseOwnerId, userServerId,
                        currentPackageItemCount, totalItemCount
                    );
                    break;
                }
            }
        }

        {
            L2_GUARD(*user->m_lock);
            if (user->m_lPackageItemPacketJobFlag == 0 ||
                user->m_lTotalPackageItemCount != user->m_lCurrentReceivedPackageItemCount) {
                return false;
            }

            runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
            runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
            if (user->m_pEconomy != nullptr) {
                user->GetSocket()->Send("c", 0x43);
                user->ReleaseEconomy();
            }
        }
        user->SendItemList(true, true, nullptr);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(
            warehouseClassType == 3 ? SystemMessageId::SHARED_DIMENSIONAL_ITEM_RETRIEVED
                                    : SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_SUCCEEDED
        );
        return false;
    }

    user->GetSocket()->Send("c", 0x43);
    {
        L2_GUARD(*user->m_lock);
        user->ReleaseEconomy();
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    user->SendSystemMessage(
        warehouseClassType == 3 ? SystemMessageId::SHARED_DIMENSIONAL_ITEM_RETRIEVAL_FAILED
                                : SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED
    );
    runtime::raw_atomic::ExchangeI32(&user->m_lTotalPackageItemCount, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lCurrentReceivedPackageItemCount, 0);
    runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 0);
    return false;
}

bool ReplyViewSiegeList(CDBSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplyApproveBattle(CDBSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

void ReserveLoadByPledgeDBID(const int worldId, const int pledgeDatabaseId, const PledgeRequestLoadType type) {
    // Current L2Server 0x006a73d8, original DBSocket.cpp helper.
    CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeDatabaseId);
    if (!pledge) {
        CPledge::ReserveLoaded(worldId, pledgeDatabaseId, type);
    }
}

bool BuyItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c495c. The paired current CacheD producer is
    // primary request 18 / reply 14 and fixes the complete item record.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t adenaDatabaseId = 0;
    std::int64_t adenaAmount = 0;
    std::int32_t echoedValue = 0;
    std::int32_t result = 0;
    std::int32_t itemCount = 0;
    packet =
        ::Disassemble(packet, "ddQddd", &userServerId, &adenaDatabaseId, &adenaAmount, &echoedValue, &result, &itemCount);
    static_cast<void>(echoedValue);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x9ea,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[merchant] Cannot apply purchase reply: user server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    CSPointer<CBuySell> buySell = user->GetBuySell();
    char refresh = 1;

    switch (static_cast<BuySellResult>(result)) {
        case BSR_USER_CANCEL:
        case BSR_INVALID_ITEMS:
            refresh = 2;
            break;
        case BSR_LACK_OF_MONEY:
            if (buySell) {
                user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
            }
            refresh = 3;
            break;
        case BSR_SUCCESS: {
            {
                L2_WRITE_GUARD(user->m_inv);
                for (int index = 0; index < itemCount; ++index) {
                    CItemDisassemblerFromCacheD itemData;
                    packet = itemData.DisassembleItemInfo(packet);

                    CItem* item = user->m_inv.GetByDBID(itemData.m_nDBID);
                    if (item == nullptr) {
                        item = g_ObjectDB.CreateItem(itemData.m_nClassID);
                        if (item == nullptr) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "item create fail at file[%s], line[%d]",
                            //     L"DBSocket.cpp",
                            //     0xaf4)
                            // L2_LOG_ERROR(Log,
                            //     "item create fail at file[%s], line[%d]",
                            //     L"DBSocket.cpp",
                            //     0xaf4)
                            L2_LOG_ERROR(
                                Log,
                                "[inventory] Cannot create purchased item DB ID %d (class %d) for character runtime ID %d; "
                                "this item is skipped.",
                                static_cast<int>(itemData.m_nDBID), static_cast<int>(itemData.m_nClassID),
                                static_cast<int>(userServerId)
                            );
                            continue;
                        }

                        if (!item->SetAmount(itemData.m_nAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                            //     L"DBSocket.cpp",
                            //     0xab3,
                            //     static_cast<long long>(
                            //         item->m_data->m_nCount),
                            //     static_cast<long long>(
                            //         itemData.m_nAmount))
                            L2_LOG_ERROR(
                                Log, u"[merchant] Cannot initialize purchased item %d amount from %lld to %lld.",
                                itemData.m_nDBID, static_cast<long long>(item->m_data->m_nCount),
                                static_cast<long long>(itemData.m_nAmount)
                            );
                        }
                        item->SetDBID(static_cast<unsigned int>(itemData.m_nDBID));
                        item->SetEnchanted(itemData.m_nEnchanted);
                        item->m_Useable = user->CheckUseable(item);
                        item->m_data->m_nDamaged = itemData.m_nDamaged;
                        item->m_data->m_bBlessed = itemData.m_nIsBlessed;
                        item->SetVariation(
                            ItemOptionKey{itemData.m_nVariation1, itemData.m_nVariation2},
                            static_cast<std::uint32_t>(itemData.m_nIntensiveItemClassID)
                        );
                        item->SetAttribute(itemData.m_Attribute);
                        item->SetShapeShiftingClassId(itemData.m_nShapeShiftedClassID);
                        item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
                        item->m_data->SetLocked(itemData.m_hLocked);
                        item->m_data->SetUseCount(itemData.m_hUseCount);

                        if (!user->m_inv.Push(item->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     L"DBSocket.cpp",
                            //     0xacf)
                            // L2_LOG_ERROR(Log,
                            //     "inven push failed at file[%s], line[%d]",
                            //     L"DBSocket.cpp",
                            //     0xacf)
                            L2_LOG_ERROR(
                                Log,
                                "[inventory] Cannot add purchased item DB ID %d (class %d) to character runtime ID %d "
                                "inventory; remaining processing for this item is skipped.",
                                static_cast<int>(itemData.m_nDBID), static_cast<int>(itemData.m_nClassID),
                                static_cast<int>(userServerId)
                            );
                            continue;
                        }
                        user->m_inv.SetInventoryChanged(item, IUT_ADD);

                        CSharedItemData* weapon = nullptr;
                        bool compatibleWeapon = false;
                        if (item->m_const->m_EtcitemType == EIT_ARROW) {
                            weapon = user->GetEquipedWeapon();
                            compatibleWeapon = weapon != nullptr && weapon->GetConstant()->m_AttackType == AT_BOW;
                        } else if (item->m_const->m_EtcitemType == EIT_BOLT) {
                            weapon = user->GetEquipedWeapon();
                            compatibleWeapon = weapon != nullptr &&
                                (weapon->GetConstant()->m_AttackType == AT_CROSSBOW ||
                                    weapon->GetConstant()->m_AttackType == AT_TWOHANDCROSSBOW);
                        }
                        if (compatibleWeapon &&
                            BaseCrystal[static_cast<int>(weapon->GetConstant()->GetCrystalType())] ==
                                BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())]) {
                            user->AutoEquipArrowOrBolt(item);
                        }

                        if (item->IsDuration()) {
                            db.UpdateItemDuration(*user, static_cast<unsigned int>(itemData.m_nDBID), item->GetDuration());
                        }
                        continue;
                    }

                    {
                        L2_GUARD(*item->m_lock);
                        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) &&
                            !item->SetAmount(itemData.m_nAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                            //     L"DBSocket.cpp",
                            //     0xafe,
                            //     static_cast<long long>(item->m_data->m_nCount),
                            //     static_cast<long long>(itemData.m_nAmount))
                            L2_LOG_ERROR(
                                Log, u"[merchant] Cannot set purchased item %d amount from %lld to %lld.", itemData.m_nDBID,
                                static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(itemData.m_nAmount)
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                }

                CItem* const adena = user->m_inv.GetByDBID(adenaDatabaseId);
                if (adena != nullptr) {
                    if (!adena->SetAmount(adenaAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                        //     L"DBSocket.cpp",
                        //     0xb0c,
                        //     static_cast<long long>(adena->m_data->m_nCount),
                        //     static_cast<long long>(adenaAmount))
                        L2_LOG_ERROR(
                            Log, u"[merchant] Cannot set adena item %d amount from %lld to %lld after purchase.",
                            adenaDatabaseId, static_cast<long long>(adena->m_data->m_nCount),
                            static_cast<long long>(adenaAmount)
                        );
                    }
                    if (adena->m_data->m_nCount == 0) {
                        if (!user->m_inv.Pop(adena->m_data->index, false)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d]Error. Cannot pop item from inven.",
                            //     L"DBSocket.cpp",
                            //     0xb12)
                            L2_LOG_ERROR(
                                Log, u"[merchant] Cannot remove depleted adena item %d from inventory.", adenaDatabaseId
                            );
                        }
                        user->m_inv.SetInventoryChanged(adena, IUT_DELETE);
                        adena->Delete();
                    } else {
                        user->m_inv.SetInventoryChanged(adena, IUT_UPDATE);
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    if (!buySell) {
        user->SendBuySellFail(BuySellConst::BSFT_INVALID_DEAL);
    } else {
        user->ShowBuySellList(refresh);
    }
    return false;
}

bool ReplyLoadCastlePacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c4ff0. The fixed field order is independently
    // closed by current CacheD CCastleDb::SendReplyToLoadCastle@004a0024.
    L2_GUARD_FUNCTION();

    std::int32_t castleId;
    std::int32_t ownerPledgeId;
    std::int32_t ownerWorldId = 0;
    std::int32_t nextSiege;
    std::int32_t taxRate;
    std::int32_t childTaxRate = 0;
    std::uint32_t residenceState;
    std::array<SharedUtilsWideChar, 24> residenceName{};
    std::int32_t castleType;
    std::int32_t successCampaign;
    std::int64_t shopIncome = 0;
    std::int32_t siegeElapsed;
    std::int64_t shopIncomeTemp = 0;
    std::int32_t pendingTaxRate = 0;
    std::int32_t previousOwnerPledgeId = 0;
    std::int32_t previousOwnerWorldId = 0;
    std::int32_t pendingChildTaxRate = 0;
    std::int32_t warCount;
    std::int32_t decoCount;
    packet = ::Disassemble(
        packet, "dddddddSddQdQdddddd", &castleId, &ownerPledgeId, &ownerWorldId, &nextSiege, &taxRate, &childTaxRate,
        &residenceState, 0x30, reinterpret_cast<char16_t*>(residenceName.data()), &castleType, &successCampaign, &shopIncome,
        &siegeElapsed, &shopIncomeTemp, &pendingTaxRate, &previousOwnerPledgeId, &previousOwnerWorldId, &pendingChildTaxRate,
        &warCount, &decoCount
    );

    std::int32_t decoNpcCount = 0;
    packet = ::Disassemble(packet, "d", &decoNpcCount);

    CResidence* const residence = g_ResidenceDB.GetResidence(castleId);
    if (residence == nullptr) {
        return false;
    }

    CCastle* const castle = residence->CastCastle();
    if (castle == nullptr || !castle->IsCastle()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Server request LoadCastlePacket but castle is not exist.[%d]",
        //     L"DBSocket.cpp",
        //     0xd98,
        //     castleId)
        L2_LOG_ERROR(Log, u"[castle] Cannot apply castle-load reply: residence %d is not a castle.", castleId);
    } else {
        if (ownerPledgeId != 0 && (castleType < CastleType::CASTLE_GOOD || castleType > CastleType::CASTLE_BAD)) {
            castleType = CastleType::CASTLE_GOOD;
            db.RequestSetCastleType(*castle, castleId, CastleType::CASTLE_GOOD);
        }
        if (residenceState >= static_cast<std::uint32_t>(RS_MAX)) {
            residenceState = RS_ANNOUNCING;
            castle->ChangeResidenceState(RS_ANNOUNCING);
        }

        // The target feeds taxRate into CCastle::Set's ignored
        // siegeTerm lane, ignores childTaxRate, and passes a literal zero for
        // the ignored previousSiegeEnd lane.
        castle->Set(
            ownerPledgeId, ownerWorldId, nextSiege, taxRate, static_cast<int>(residenceState), residenceName.data(), 0,
            shopIncome, siegeElapsed, static_cast<CastleType::Enum>(castleType), successCampaign
        );
        static_cast<void>(childTaxRate);
        static_cast<void>(pendingTaxRate);
        static_cast<void>(pendingChildTaxRate);

        TaxInfo& activeTax = castle->m_data->m_TaxInfoData.data()[castleType];
        castle->m_data->m_nTaxRateToChange = static_cast<int>(activeTax.m_nTaxRate);
        castle->m_data->m_nTaxIncomeTemp = shopIncomeTemp;
        castle->m_data->m_nTaxForResidenceRateToChange = static_cast<int>(activeTax.m_nTaxForResidenceRate);

        // Current std::vector::erase calls clear these three end pointers but
        // leave m_lstTempReservedPledgeWorldId untouched.
        castle->m_lstTempReservedPledgeDbId.clear();
        castle->m_lstTempReservedProposeStatus.clear();
        castle->m_lstTempReservedProposeTime.clear();

        for (std::int32_t index = 0; index < warCount; ++index) {
            std::int32_t pledgeWorldId;
            std::int32_t pledgeDatabaseId;
            std::int32_t sourceStatus;
            std::int32_t proposeTime;
            packet = ::Disassemble(packet, "dddd", &pledgeWorldId, &pledgeDatabaseId, &sourceStatus, &proposeTime);
            castle->m_lstTempReservedPledgeWorldId.push_back(pledgeWorldId);
            castle->m_lstTempReservedPledgeDbId.push_back(pledgeDatabaseId);

            std::int32_t targetStatus = 0;
            if (sourceStatus == 2) {
                targetStatus = 3;
            } else if (sourceStatus == 3) {
                targetStatus = 2;
            } else if (sourceStatus == 4) {
                targetStatus = 4;
            }
            castle->m_lstTempReservedProposeStatus.push_back(targetStatus);
            castle->m_lstTempReservedProposeTime.push_back(proposeTime);
            ReserveLoadByPledgeDBID(pledgeWorldId, pledgeDatabaseId, PRLT_CASTLE);
        }

        if (ownerPledgeId != 0) {
            ReserveLoadByPledgeDBID(ownerWorldId, ownerPledgeId, PRLT_CASTLE);
        }
        if (previousOwnerPledgeId != 0) {
            ReserveLoadByPledgeDBID(previousOwnerWorldId, previousOwnerPledgeId, PRLT_CASTLE);
        }

        castle->m_PreviousOwnerPledgeDbId = previousOwnerPledgeId;
        castle->m_nPrevOwnerPledgeWorldId = previousOwnerWorldId;
        SharedCastleInfo* const sharedCastle = SharedCastleInfo::GetInfo(castle->m_data->m_nResidenceID);
        sharedCastle->m_nPreviousOwnerId = castle->m_PreviousOwnerPledgeDbId;
        sharedCastle->m_nPreviousOwnerWorldId = previousOwnerWorldId;

        for (CDoor* const door : castle->m_DoorList) {
            db.RequestLoadDoor(door);
        }
        db.RequestLoadAllControlTower(castle);

        for (std::int32_t index = 0; index < decoCount; ++index) {
            std::int32_t decoType;
            std::int32_t decoId;
            std::int32_t decoLevel;
            std::int32_t decoExpire;
            packet = ::Disassemble(packet, "dddd", &decoType, &decoId, &decoLevel, &decoExpire);
            Deco& deco = castle->m_data->m_aDeco.data()[decoType - 1];
            deco.nId = decoId;
            deco.nLevel = decoLevel;
            deco.nExpire = static_cast<std::uint32_t>(decoExpire);
        }
        castle->OnDecoSet();
        ResidenceUtil::DisassembleDecoNpc(packet, decoNpcCount, castle);
    }

    // Current completion publication is outside the CastCastle/IsCastle
    // branch, but still requires the original residence lookup to succeed.
    if (g_JobFinishSignal.SetCastleLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_CASTLE);
    }
    return false;
}

bool ReplyLoadAgitPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a7458.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    std::int32_t ownerPledgeId;
    std::int32_t hpStove;
    std::int32_t hpStoveExpire;
    std::int32_t mpFlame;
    std::int32_t mpFlameExpire;
    std::int32_t hatcherId;
    std::int32_t residenceState;
    std::int32_t nextSiege;
    std::int32_t auctionId = 0;
    std::int32_t auctionEnd = 0;
    std::int64_t lastPrice = 0;
    std::int32_t lastCancel = 0;
    std::int64_t minimumBidPrice = 0;
    std::int32_t teleportLevel;
    std::int32_t teleportExpire;
    std::array<char16_t, 256> auctionDescription{};
    std::int32_t nextCost = 0;
    std::int32_t costFail = 0;
    std::int32_t taxRate = 0;
    std::int32_t taxRateToChange = 0;
    std::int32_t taxForResidenceRate = 0;
    std::int32_t taxForResidenceRateToChange = 0;
    std::int64_t taxIncome = 0;
    std::int64_t taxIncomeTemp = 0;
    std::int32_t decoCount = 0;
    packet = ::Disassemble(
        packet, "dddddddddddQdQddSddddddQQd", &residenceId, &ownerPledgeId, &hpStove, &hpStoveExpire, &mpFlame,
        &mpFlameExpire, &hatcherId, &residenceState, &nextSiege, &auctionId, &auctionEnd, &lastPrice, &lastCancel,
        &minimumBidPrice, &teleportLevel, &teleportExpire, 0x200, auctionDescription.data(), &nextCost, &costFail, &taxRate,
        &taxRateToChange, &taxForResidenceRate, &taxForResidenceRateToChange, &taxIncome, &taxIncomeTemp, &decoCount
    );

    std::int32_t decoNpcCount = 0;
    packet = ::Disassemble(packet, "d", &decoNpcCount);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        return false;
    }

    residence->Set(hpStove, hpStoveExpire, mpFlame, mpFlameExpire, teleportLevel, teleportExpire, hatcherId);

    CWorldAgit* const worldAgit = residence->CastWorldAgit();
    if (worldAgit == nullptr || !worldAgit->IsWorldAgit()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NPC Server request ReplyLoadAgitPacket "
        //     L"but agit is not exist.[%d]",
        //     L"DBSocket.cpp",
        //     0xdfb,
        //     residenceId)
        L2_LOG_ERROR(Log, u"[agit] Cannot apply agit-load reply: residence %d is not a world agit.", residenceId);
        return false;
    }

    worldAgit->Set(ownerPledgeId, residenceState, nextSiege);
    worldAgit->m_data->m_nTaxRateToChange = taxRateToChange;
    worldAgit->m_data->m_nTaxForResidenceRateToChange = taxForResidenceRateToChange;
    worldAgit->m_data->m_nTaxIncome = taxIncome;
    worldAgit->m_data->m_nTaxIncomeTemp = taxIncomeTemp;
    if (taxRateToChange == -1) {
        worldAgit->m_data->m_nTaxRateToChange = taxRate;
    } else {
        worldAgit->m_data->m_nTaxRateToChange = taxRateToChange;
    }
    if (taxForResidenceRateToChange == -1) {
        worldAgit->m_data->m_nTaxForResidenceRateToChange =
            static_cast<std::int32_t>(worldAgit->m_data->m_TaxInfoData[0].m_nTaxForResidenceRate);
    } else {
        worldAgit->m_data->m_nTaxForResidenceRateToChange = taxForResidenceRateToChange;
    }
    static_cast<void>(taxForResidenceRate);

    for (std::int32_t index = 0; index < decoCount; ++index) {
        std::int32_t decoType;
        std::int32_t decoId;
        std::int32_t decoLevel;
        std::int32_t decoExpire;
        packet = ::Disassemble(packet, "dddd", &decoType, &decoId, &decoLevel, &decoExpire);
        Deco& deco = worldAgit->m_data->m_aDeco.data()[decoType - 1];
        deco.nId = decoId;
        deco.nLevel = decoLevel;
        deco.nExpire = static_cast<std::uint32_t>(decoExpire);
    }
    ResidenceUtil::DisassembleDecoNpc(packet, decoNpcCount, worldAgit);

    if (ownerPledgeId != 0) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), ownerPledgeId);
        if (!pledge) {
            CPledge::ReserveLoaded(runtime::process::CurrentDatabaseId(), ownerPledgeId, PRLT_AGIT);
        }
    }

    db.RequestGetContributionRelatedPledge(*residence, residenceId);

    CAuctionAgit* const auctionAgit = worldAgit->CastAuctionAgit();
    if (auctionAgit != nullptr) {
        auctionAgit->SetLastPrice(lastPrice);
        auctionAgit->SetAuctionLastCancel(lastCancel);
        auctionAgit->SetNextCost(nextCost, costFail);
        auctionAgit->SetSaleState(ownerPledgeId != 0 ? SALE_OWNED : SALE_NOTOWNED);
        if (auctionId > 0) {
            std::wstring hostDescription = ::Utf16ToWide(std::u16string_view(auctionDescription.data()));
            auctionAgit->SetAuctionData(auctionId, minimumBidPrice, hostDescription.data(), auctionEnd);
            db.RequestLoadAgitBid(residenceId, auctionId);
        }
    }

    worldAgit->OnReplyLoadAgitPacket();
    return false;
}

bool ReplyLoadDismissReservedPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a35d8. The zero-row path marks
    // the reservation class complete without consulting the counter result.
    L2_GUARD_FUNCTION();

    std::int32_t count = 0;
    packet = ::Disassemble(packet, "d", &count);
    std::int32_t index = 0;
    if (count > 0) {
        do {
            std::uint32_t pledgeDatabaseId;
            std::uint32_t deletionTime;
            packet = ::Disassemble(packet, "dd", &pledgeDatabaseId, &deletionTime);
            g_PledgeTimer.ReserveDismiss(pledgeDatabaseId, deletionTime);
            CPledge::ReserveLoaded(
                runtime::process::CurrentDatabaseId(), static_cast<int>(pledgeDatabaseId), PRLT_DISMISS
            );
            if (g_JobFinishSignal.SetDismissReservedLoaded()) {
                CPledge::SetFlagReservePledge(PRLT_DISMISS);
            }
            ++index;
        } while (index < count);
    }
    if (count == 0) {
        static_cast<void>(g_JobFinishSignal.SetDismissReservedLoaded());
        CPledge::SetFlagReservePledge(PRLT_DISMISS);
    }
    return false;
}

bool ReplySetAgitAuction(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a415c.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t result = 0;
    std::int32_t auctionId = 0;
    std::int64_t price = 0;
    std::int32_t winnerPledgeId = 0;
    std::int32_t nextCost = 0;
    ::Disassemble(packet, "dddQdd", &residenceId, &result, &auctionId, &price, &winnerPledgeId, &nextCost);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit != nullptr) {
        auctionAgit->SetNextCost(nextCost, 0);
        auctionAgit->ReplyDoAuction(result, auctionId, price, winnerPledgeId);
    }
    return false;
}

bool ReplyLoadControlTower(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d36cc.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 128> controlTowerName;
    std::int32_t ignored;
    std::int32_t controlLevel;
    std::int32_t hitPoints;
    std::uint32_t reservedStatus;
    ::Disassemble(packet, "Sdddd", 0x100, controlTowerName.data(), &ignored, &controlLevel, &hitPoints, &reservedStatus);

    CControlTower* const controlTower = CControlTowerDB::Inst().GetControlTower(controlTowerName.data());
    if (controlTower == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Invalid Control Tower Data [%s]",
        //     controlTowerName.data())
        // L2_LOG_ERROR(Log,
        //     L"[castle siege] Control tower '%s' has invalid data. Entry rejected.",
        //     controlTowerName.data())
        L2_LOG_ERROR(Log, u"[castle siege] Control tower '%s' has invalid data; entry rejected.", controlTowerName.data());
        return false;
    }

    controlTower->m_nControlLevel = controlLevel;
    controlTower->SetHP(static_cast<double>(hitPoints));
    if (reservedStatus > 2U) {
        reservedStatus = 2U;
    }
    controlTower->m_eReservedStatus = static_cast<ControlTowerStatusEnum>(reservedStatus);
    return false;
}

bool ReplyAgitCost(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a42bc.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t ignored = 0;
    std::int64_t shopTax = 0;
    std::int32_t reportTax = 0;
    std::int32_t nextCost = 0;
    std::int32_t costFail = 0;
    ::Disassemble(packet, "ddQddd", &residenceId, &ignored, &shopTax, &reportTax, &nextCost, &costFail);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return false;
    }

    if (reportTax != 0 && auctionAgit->m_data->m_nAgitParentCastleId > 0) {
        CResidence* const parent = g_ResidenceDB.GetResidence(auctionAgit->m_data->m_nAgitParentCastleId);
        std::int32_t parentOwnerPledgeId = 0;
        if (parent != nullptr && parent->m_data != nullptr) {
            parentOwnerPledgeId = static_cast<std::int32_t>(parent->m_data->m_nResidenceOwnerID);
        }
        if (parentOwnerPledgeId > 0) {
            const std::uint32_t savedTaxForResidenceRate = auctionAgit->m_data->m_TaxInfoData[0].m_nTaxForResidenceRate;
            auctionAgit->m_data->m_TaxInfoData[0].m_nTaxForResidenceRate = parent->m_data->m_TaxInfoData[0].m_nTaxRate;
            auctionAgit->ReportShopTax(shopTax, CastleType::CASTLE_NOTHING, true);
            auctionAgit->m_data->m_TaxInfoData[0].m_nTaxForResidenceRate = savedTaxForResidenceRate;
        }
    }
    auctionAgit->SetNextCost(nextCost, costFail);
    return false;
}

bool ReplyAgitAutoSell(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a4464. The fourth decoded dword is unused.
    L2_GUARD_FUNCTION();

    std::int32_t auctionId = 0;
    std::int32_t residenceId = 0;
    std::int64_t minimumBidPrice = 0;
    std::int32_t ignored = 0;
    std::int32_t auctionEnd = 0;
    ::Disassemble(packet, "ddQdd", &auctionId, &residenceId, &minimumBidPrice, &ignored, &auctionEnd);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit != nullptr) {
        auctionAgit->m_nAuctionId = auctionId;
        auctionAgit->SetSaleState(SALE_ACT);
        auctionAgit->StartAuctionTimer(auctionEnd);
        auctionAgit->m_nMinBidPrice = minimumBidPrice;
    }
    return false;
}

bool SellItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a6c6c.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t adenaDatabaseId = 0;
    std::int64_t adenaAmount = 0;
    std::int32_t result;
    std::int32_t itemCount = 0;
    packet = ::Disassemble(packet, "ddQdd", &userServerId, &adenaDatabaseId, &adenaAmount, &result, &itemCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x9d4,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    return user->OnSellItemsPacket(itemCount, adenaDatabaseId, adenaAmount, static_cast<BuySellResult>(result), packet);
}

bool ShortCutInfoPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a6df0.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t shortCutCount;
    packet = ::Disassemble(packet, "dd", &userServerId, &shortCutCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xb5b,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    for (std::int32_t index = 0; index < shortCutCount; ++index) {
        std::int32_t slot;
        std::uint32_t type;
        std::int32_t id;
        std::int32_t category;
        std::int32_t skillLevelSet;
        packet = ::Disassemble(packet, "ddddd", &slot, &type, &id, &category, &skillLevelSet);

        if (type == SCT_ITEM) {
            CItem* const item = user->m_inv.GetByDBID(id);
            if (item == nullptr) {
                continue;
            }
            id = static_cast<std::int32_t>(item->Id());
            skillLevelSet = -1;
        } else if (type == SCT_SKILL) {
            // Both the database id lane and skill-level-set lane are used
            // exactly as decoded.
        } else if (type - SCT_ACTION < 4U) {
            skillLevelSet = -1;
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Unknown ShortCut Type : %d",
            //     type)
            L2_LOG_ERROR(Log, u"[shortcuts] Shortcut type %d is unsupported; entry ignored.", type);
            continue;
        }

        user->m_ShortCut.AddShortCut(slot, static_cast<std::int32_t>(type), id, category, false, skillLevelSet, 0, 0);
    }
    user->SendSkillList(nullptr, true, 0);
    return false;
}

namespace DBPacketHandler {

bool ReplyEstablishFriendship(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a8634, original owner DBSocket.cpp. The paired
    // current CacheD opcode 0x2d/response 0x1f fixes all three lanes.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId;
    std::int32_t requesteeServerId;
    ::Disassemble(packet, "ddd", &result, &userServerId, &requesteeServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x142d,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (result == 0) {
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CHECK_FRIEND_LIST_AFTER_UPDATE_FAILURE);
        }
        return false;
    }

    CCreature* const requesteeCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(requesteeServerId));
    User* const requestee =
        requesteeCreature != nullptr && requesteeCreature->IsPC() ? requesteeCreature->CastUser() : nullptr;
    if (requestee != nullptr) {
        user->ReplyAddFriendResult(requestee);
    }
    return false;
}

} // namespace DBPacketHandler

bool ReplyBreakFriendship(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a87e8.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userServerId;
    std::int32_t characterId = 0;
    ::Disassemble(packet, "ddd", &result, &userServerId, &characterId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1468,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 0) {
        if (userSocket != nullptr) {
            userSocket->SendSystemMessage(SystemMessageId::CHECK_FRIEND_LIST_AFTER_UPDATE_FAILURE);
        }
    } else {
        user->BreakFriendship(characterId);
    }
    return false;
}

bool ReplySetPledgeCrest(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a8f58, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t pledgeServerId;
    std::uint32_t crestDatabaseId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &pledgeServerId, &crestDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1d38,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x1d39,
        //     pledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", pledgeServerId);
        return false;
    }

    pledge->SetNewCrest(crestDatabaseId);
    pledge->Save(0xd);
    pledge->UpdateInfoWindowPacket();
    pledge->UpdatePledgeInfoPacket();
    if (Singleton<WOlympiadServiceConfig>::inst().IsHost()) {
        Singleton<WOlympiadServiceProxy>::inst().UpdatePledgeInfo(
            static_cast<int>(pledge->GetDbId()), static_cast<int>(crestDatabaseId)
        );
    }

    socket->Send("cd", 0x69, 1);
    return false;
}

bool ReplySetUserNickname(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a9140, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t targetServerId;
    char16_t targetNickname[0x20];
    targetNickname[0] = u'\0';
    std::memset(targetNickname + 1, 0, 0x30);
    std::int32_t result = 0;
    ::Disassemble(packet, "ddSd", &userServerId, &targetServerId, 0x32, targetNickname, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1d61,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCreature* const targetCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetServerId));
    User* const target = targetCreature != nullptr && targetCreature->IsPC() ? targetCreature->CastUser() : nullptr;
    if (target == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1d62,
        //     targetServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", targetServerId);
        return false;
    }

    CUserSocket* const targetSocket = target->GetSocket();
    const std::u16string hostNicknameUtf16 = SanitizeUtf16(FixedUtf16View(targetNickname));
    const std::wstring hostNickname = Utf16ToWide(hostNicknameUtf16);
    if (result == 1) {
        target->SetNickName(hostNicknameUtf16.c_str());
        CPledge* const pledge = target->GetPledge();
        if (pledge != nullptr) {
            pledge->SetNickName(target->GetDBID(), const_cast<wchar_t*>(hostNickname.c_str()));
            pledge->ViewPledgeMemberInfo(Utf16ToWide(target->GetName()).data());
        }
        target->ParamChanged(PCB_NICKNAME | PCB_USER);
        targetSocket->SendSystemMessage(SystemMessageId::TITLE_CHANGED);
        socket->Send("cSSd", 0x8d, target->GetName(), targetNickname, 1);
    } else {
        socket->SendSystemMessage(SystemMessageId::OPERATION_NOT_PROCESSED_TRY_AGAIN);
        socket->Send("cSSd", 0x8d, target->GetName(), targetNickname, 0);
    }
    return false;
}

bool ReplyGetPledgeCrest(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c31bc, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::uint32_t crestDatabaseId = 0;
    std::int32_t worldId = 0;
    std::int32_t bitmapSize = 0;
    const std::uint8_t* const bitmap = ::Disassemble(packet, "dddd", &userServerId, &crestDatabaseId, &worldId, &bitmapSize);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1dac,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (bitmapSize < 1) {
        socket->Send("cddd", 0x6a, worldId, crestDatabaseId, 0);
        return false;
    }

    CPledgeCrest* const crest = new CPledgeCrest(crestDatabaseId, bitmapSize, bitmap);
    g_PledgeCrestMap.Insert(CPledgeCrestKey(worldId, crestDatabaseId), crest, const_cast<char*>("DBSocket.cpp"), 0x1db9);
    socket->Send("cdddb", 0x6a, worldId, crestDatabaseId, crest->bitmapSize(), crest->bitmapSize(), crest->bitmap());
    L2_RELEASE(crest, ObjectRefType::CreateDelete);
    return false;
}

bool ReplyDelPledgeCrestPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c33fc, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::uint32_t pledgeDatabaseId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &pledgeDatabaseId, &result);

    if (result == 1) {
        db.RequestDeletePledgeCrest(pledgeDatabaseId);
    } else if (result == 2) {
        CSPointer<CPledge> pledge =
            CPledge::FindPledge(runtime::process::CurrentDatabaseId(), static_cast<int>(pledgeDatabaseId));
        if (pledge) {
            g_PledgeCrestMap.DeleteObject(
                CPledgeCrestKey(pledge->GetWorldId(), pledge->GetCrestId()), const_cast<char*>("DBSocket.cpp"), 0x215f
            );
            pledge->SetNewCrest(0);
            pledge->UpdateInfoWindowPacket();
            pledge->UpdatePledgeInfoPacket();
        }
    }
    return false;
}

bool ReplyGetAllianceCrest(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c360c, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::uint32_t crestDatabaseId = 0;
    std::int32_t worldId = 0;
    std::int32_t bitmapSize = 0;
    const std::uint8_t* const bitmap = ::Disassemble(packet, "dddd", &userServerId, &crestDatabaseId, &worldId, &bitmapSize);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2d61,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (bitmapSize < 1) {
        socket->Send("cddd", 0xaf, worldId, crestDatabaseId, 0);
        return false;
    }

    CPledgeCrest* const crest = new CPledgeCrest(crestDatabaseId, bitmapSize, bitmap);
    g_PledgeCrestMap.Insert(CPledgeCrestKey(worldId, crestDatabaseId), crest, const_cast<char*>("DBSocket.cpp"), 0x2d6e);
    socket->Send("cdddb", 0xaf, worldId, crestDatabaseId, crest->bitmapSize(), crest->bitmapSize(), crest->bitmap());
    L2_RELEASE(crest, ObjectRefType::CreateDelete);
    return false;
}

bool ReplyCreateSubJob(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b4d20.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t mode;
    std::int32_t requestContext1;
    std::int32_t requestContext2;
    std::int32_t subJobId;
    std::int32_t classId;
    packet = ::Disassemble(packet, "dddddd", &userServerId, &mode, &requestContext1, &requestContext2, &subJobId, &classId);

    CVitalData vitalData;
    vitalData.Disassemble(packet);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x42a8,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (subJobId == 0) {
        user->m_data->m_bIsActive = true;
        runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"create subjob failed [%s][%d]",
        //     user->GetName(),
        //     classId)
        // L2_LOG_ERROR(Log,
        //     L"[subclass] Creation failed for character '%s', class %d.",
        //     user->GetName(),
        //     classId)
        L2_LOG_ERROR(Log, u"[subclass] Creation failed for character '%s', class %d.", user->GetName(), classId);
    } else {
        user->InitSubJob(subJobId, classId);
        user->OnChangeSubJobEnd(SubJob::CREATE, vitalData);
        user->SendVoteSystemInfoPacket();
        user->GetSocket()->Send(
            "chcch", 0x62, SystemMessageId::NEW_SUBCLASS_ACQUIRED, 1, 0xf, static_cast<std::int16_t>(classId)
        );
        user->OnPremiumHennaExpired();
    }

    if (mode == 0) {
        g_npcserver.Send("cddd", 0x43, requestContext1, requestContext2, subJobId != 0 ? classId : -1);
    }
    user->SendUserSubJobInfo(SubJob::CREATE_NEW_CLASS);
    return false;
}

bool ReplyDeleteSubJob(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b4f9c.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t subJobId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddd", &userServerId, &subJobId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x42da,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"delete subjob failed [%s][%d]",
        //     user->GetName(),
        //     subJobId)
        // L2_LOG_ERROR(Log,
        //     L"[subclass] Deletion failed for character '%s', subjob %d.",
        //     user->GetName(),
        //     subJobId)
        L2_LOG_ERROR(Log, u"[subclass] Deletion failed for character '%s', subjob %d.", user->GetName(), subJobId);
    } else {
        if (subJobId == 1) {
            user->m_data->m_nSubJob1Class = -1;
        } else if (subJobId == 2) {
            user->m_data->m_nSubJob2Class = -1;
        } else if (subJobId == 3) {
            user->m_data->m_nSubJob3Class = -1;
        }
        if (subJobId == user->m_data->m_DualClassID) {
            user->m_data->m_DualClassID = -1;
        }
        char16_t systemContext[] = u"SYS";
        user->GetSocket()->SendSystemMessage(systemContext, u"subjob deleted");
    }

    runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
    user->SendUserSubJobInfo(SubJob::NOTIFY_CLASS_INFO);
    return false;
}

namespace DBPacketHandler {

bool ReplyChangeSubJob(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b51a4. CacheD's current opcode 0xde response
    // fixes the complete loaded and failure field order.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t mode = 0;
    std::int32_t requestContext1 = 0;
    std::int32_t requestContext2 = 0;
    std::int32_t subJobId = 0;
    std::int32_t result = 0;
    double hp = 0.0;
    double mp = 0.0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    std::int32_t abilityPoints = 0;
    std::int32_t level = 0;
    std::int32_t hennaId1 = 0;
    std::int32_t hennaId2 = 0;
    std::int32_t hennaId3 = 0;
    std::int32_t oldSubJobId = 0;
    std::int32_t oldClassId = 0;
    packet = ::Disassemble(
        packet, "ddddddffQQddddddd", &userServerId, &mode, &requestContext1, &requestContext2, &subJobId, &result, &hp, &mp,
        &sp, &experience, &abilityPoints, &level, &hennaId1, &hennaId2, &hennaId3, &oldSubJobId, &oldClassId
    );
    static_cast<void>(oldSubJobId);

    CVitalData vitalData{};
    packet = vitalData.Disassemble(packet);

    std::int32_t potentialCount1 = 0;
    std::int32_t potentialCount2 = 0;
    std::int32_t potentialCount3 = 0;
    ::Disassemble(packet, "ddd", &potentialCount1, &potentialCount2, &potentialCount3);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x4336,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    std::int32_t classId = -1;
    if (subJobId == 0) {
        classId = user->m_data->m_nSubJob0Class;
    } else if (subJobId == 1) {
        classId = user->m_data->m_nSubJob1Class;
    } else if (subJobId == 2) {
        classId = user->m_data->m_nSubJob2Class;
    } else if (subJobId == 3) {
        classId = user->m_data->m_nSubJob3Class;
    }

    if (result == 0) {
        user->m_data->m_bIsActive = true;
        runtime::raw_atomic::ExchangeI32(&user->m_nChangeSubjobFlag, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"change subjob failed [%s][%d]",
        //     user->GetName(),
        //     subJobId)
        // L2_LOG_ERROR(Log,
        //     L"[subclass] Change failed for character '%s', subjob %d.",
        //     user->GetName(),
        //     subJobId)
        L2_LOG_ERROR(Log, u"[subclass] Change failed for character '%s', subjob %d.", user->GetName(), subJobId);
    } else {
        user->SaveSkillUsedTime(ReuseDelaySaveReason::SR_DUMP, true);
        user->m_data->m_nSubJobId = subJobId;
        user->m_data->m_IsDualClass = subJobId == user->m_data->m_DualClassID;
        if (user->m_data->m_nClass != static_cast<ClassType>(classId)) {
            user->m_data->m_nClass = static_cast<ClassType>(classId);
            user->m_data->SetNeedUpdatePacket(3);
        }
        user->m_data->SetHP(hp);
        user->m_data->SetMP(mp);
        user->m_data->SetSP(sp);
        if (user->m_data->m_nExp != experience) {
            user->m_data->m_NeedUpdatePacketBitSet.SetInt64Value_IfModifed(user->m_data->m_nExp, experience, 6);
            user->m_data->SetNeedUpdatePacket(7);
        }
        if (user->m_data->m_nLevel != level) {
            user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(user->m_data->m_nLevel, level, 5);
            user->m_data->SetNeedUpdatePacket(7);
        }
        user->m_data->m_HennaId_1 = hennaId1;
        user->m_data->m_HennaId_2 = hennaId2;
        user->m_data->m_HennaId_3 = hennaId3;
        user->m_data->m_stPotentialCount.nCount1 = potentialCount1;
        user->m_data->m_stPotentialCount.nCount2 = potentialCount2;
        user->m_data->m_stPotentialCount.nCount3 = potentialCount3;
        user->m_data->SetAP(abilityPoints, false);
        user->ChangeClass(static_cast<ClassType>(classId), 0);
        user->InitVitality(vitalData);
        db.RequestReloadSkill(user);
        user->m_data->ClearNeedUpdatePacket();
    }

    if (mode == 0) {
        g_npcserver.Send(
            "cdddd", 0x44, requestContext1, requestContext2, result != 0 ? classId : -1, result != 0 ? level : -1
        );
    }
    if (user->GetPM_WindowOpen()) {
        CSPointer<User> retained(user, ObjectRefType::FunctionChain);
        CPartyMatchingRequest::inst().OnChangeLocation(std::move(retained));
    }
    db.LoadPeriodicHennaInfo(user);
    user->SendVitalityEffectInfo(nullptr);
    user->GetSocket()->Send(
        "chcchch", 0x62, SystemMessageId::CLASS_CHANGED_FROM_ONE_TO_ANOTHER, 2, 0xf, static_cast<std::int16_t>(oldClassId),
        0xf, static_cast<std::int16_t>(user->m_data->m_nClass)
    );
    return false;
}

} // namespace DBPacketHandler

bool ReplyLoadSubJob(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b5720. CacheD's current RequestLoadSubJob and
    // L2NPC's current GetSubJobList close both packet boundaries.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t mode;
    std::int32_t requestContext1;
    std::int32_t requestContext2;
    std::int32_t requestContext3;
    std::int32_t requestContext4;
    std::int32_t count;
    packet = ::Disassemble(
        packet, "ddddddd", &userServerId, &mode, &requestContext1, &requestContext2, &requestContext3, &requestContext4,
        &count
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x43b4,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    std::array<std::int32_t, 4> classIds{-1, -1, -1, -1};
    std::array<std::int32_t, 4> levels{-1, -1, -1, -1};

    for (std::int32_t index = 0; index < count; ++index) {
        std::int32_t subJobId;
        std::int32_t level;
        packet = ::Disassemble(packet, "dd", &subJobId, &level);
        if (subJobId == 0) {
            classIds[0] = user->m_data->m_nSubJob0Class;
            user->m_data->m_SubJobLevel[0] = level;
            if (user->m_data->m_DualClassID == 0) {
                user->m_data->m_DualClassLevel = level;
            }
            levels[0] = level;
            user->m_data->m_MainClassLevel = level;
        } else if (subJobId == 1) {
            classIds[1] = user->m_data->m_nSubJob1Class;
            user->m_data->m_SubJobLevel[1] = level;
            if (user->m_data->m_DualClassID == 1) {
                user->m_data->m_DualClassLevel = level;
            }
            levels[1] = level;
        } else if (subJobId == 2) {
            classIds[2] = user->m_data->m_nSubJob2Class;
            user->m_data->m_SubJobLevel[2] = level;
            if (user->m_data->m_DualClassID == 2) {
                user->m_data->m_DualClassLevel = level;
            }
            levels[2] = level;
        } else if (subJobId == 3) {
            classIds[3] = user->m_data->m_nSubJob3Class;
            user->m_data->m_SubJobLevel[3] = level;
            if (user->m_data->m_DualClassID == 3) {
                user->m_data->m_DualClassLevel = level;
            }
            levels[3] = level;
        }
    }

    if (count == 0) {
        classIds[0] = static_cast<std::int32_t>(user->m_data->m_nClass);
        levels[0] = user->m_data->m_nLevel;
        user->m_data->m_MainClassLevel = user->m_data->m_nLevel;
        user->m_data->m_DualClassLevel = 0;
    }

    const std::int32_t activeSubJobId = user->m_data->m_nSubJobId;
    if (activeSubJobId >= 0 && activeSubJobId < 4) {
        classIds[activeSubJobId] = static_cast<std::int32_t>(user->m_data->m_nClass);
        levels[activeSubJobId] = user->m_data->m_nLevel;
    }

    user->SendUserSubJobInfo(SubJob::NOTIFY_CLASS_INFO);
    if (mode == 0) {
        if (requestContext1 != -1) {
            g_npcserver.Send(
                "cdddddddddddd", 0x42, requestContext1, requestContext2, requestContext3, requestContext4, classIds[0],
                levels[0], classIds[1], levels[1], classIds[2], levels[2], classIds[3], levels[3]
            );
        }
        return false;
    }

    char16_t systemContext[] = u"SYS";
    std::array<char16_t, 0x2004> message;
    FormatUtf16To(
        message.data(), message.size(), u"=== subjob id[%d] class[%d] level[%d] ===", user->m_data->m_nSubJobId,
        static_cast<std::int32_t>(user->m_data->m_nClass), user->m_data->m_nLevel
    );
    user->GetSocket()->SendSystemMessage(systemContext, message.data());
    FormatUtf16To(message.data(), message.size(), u"id_0 class[%d] level[%d]", classIds[0], levels[0]);
    user->GetSocket()->SendSystemMessage(systemContext, message.data());
    FormatUtf16To(message.data(), message.size(), u"id_1 class[%d] level[%d]", classIds[1], levels[1]);
    user->GetSocket()->SendSystemMessage(systemContext, message.data());
    FormatUtf16To(message.data(), message.size(), u"id_2 class[%d] level[%d]", classIds[2], levels[2]);
    user->GetSocket()->SendSystemMessage(systemContext, message.data());
    FormatUtf16To(message.data(), message.size(), u"id_3 class[%d] level[%d]", classIds[3], levels[3]);
    user->GetSocket()->SendSystemMessage(systemContext, message.data());
    return false;
}

bool AcquiredSkillList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a7ba4, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t characterDatabaseId = 0;
    std::int32_t acquiredSkillCount = 0;
    std::int32_t currentSubjobReuseCount = 0;
    std::int32_t sharedReuseCount = 0;
    packet = ::Disassemble(
        packet, "ddddd", &userServerId, &characterDatabaseId, &acquiredSkillCount, &currentSubjobReuseCount,
        &sharedReuseCount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1294,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->DeleteAllAcquireSkills(false);
    for (std::int32_t index = 0; index < acquiredSkillCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t skillLevel = 0;
        std::int32_t skillSubLevel = 0;
        std::int32_t skillLock = 0;
        packet = ::Disassemble(packet, "dddd", &skillId, &skillLevel, &skillSubLevel, &skillLock);
        if (skillId < 100001) {
            user->AcquireSkill(skillId, skillLevel, skillSubLevel, false, true);
            user->SetSkillLock(skillId, skillLevel, skillSubLevel, skillLock);
        }
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (std::int32_t index = 0; index < currentSubjobReuseCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t endTime = 0;
        std::int32_t reuseDelay = 0;
        packet = ::Disassemble(packet, "ddd", &skillId, &endTime, &reuseDelay);
        if (now < endTime) {
            const std::int64_t remaining = static_cast<std::int64_t>(runtime::SubtractInt32Wrapping(endTime, now)) * 1000;
            if (remaining > static_cast<std::int64_t>(UINT32_MAX)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] AcquiredSkillList, remain delay overflow: [%d, %d, %d]",
                //     L"DBSocket.cpp",
                //     0x12c2,
                //     characterDatabaseId,
                //     skillId,
                //     endTime)
                L2_LOG_ERROR(
                    Log,
                    u"[skills] Reuse delay overflow for character DB ID %d, skill %d: end time %d cannot be represented in "
                    u"milliseconds.",
                    characterDatabaseId, skillId, endTime
                );
            }
            user->m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                skillId, user->m_data->m_nSubJobId, static_cast<std::uint32_t>(reuseDelay),
                static_cast<std::uint32_t>(remaining)
            );
        }
    }
    for (std::int32_t index = 0; index < sharedReuseCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t endTime = 0;
        std::int32_t reuseDelay = 0;
        packet = ::Disassemble(packet, "ddd", &skillId, &endTime, &reuseDelay);
        if (now < endTime) {
            const std::int64_t remaining = static_cast<std::int64_t>(runtime::SubtractInt32Wrapping(endTime, now)) * 1000;
            if (remaining > static_cast<std::int64_t>(UINT32_MAX)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] AcquiredSkillList, remain delay overflow: [%d, %d, %d]",
                //     L"DBSocket.cpp",
                //     0x12d2,
                //     characterDatabaseId,
                //     skillId,
                //     endTime)
                L2_LOG_ERROR(
                    Log,
                    u"[skills] Reuse delay overflow for character DB ID %d, skill %d: end time %d cannot be represented in "
                    u"milliseconds.",
                    characterDatabaseId, skillId, endTime
                );
            }
            user->m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                skillId, 4, static_cast<std::uint32_t>(reuseDelay), static_cast<std::uint32_t>(remaining)
            );
        }
    }

    user->ValidateChanged(VB_SKILLLIST);
    user->m_ShortCut.Load(true);
    db.LoadInfectedSkills(user);
    return false;
}

namespace DBPacketHandler {

bool ReplyReloadSkill(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b5c88, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::uint8_t ignoredReloadType = 0;
    std::int32_t acquiredSkillCount = 0;
    std::int32_t currentSubjobReuseCount = 0;
    std::int32_t sharedReuseCount = 0;
    std::int32_t subjobChangeFinished = 0;
    packet = ::Disassemble(
        packet, "dcdddd", &userServerId, &ignoredReloadType, &acquiredSkillCount, &currentSubjobReuseCount,
        &sharedReuseCount, &subjobChangeFinished
    );
    static_cast<void>(ignoredReloadType);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x4448,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->DeleteAllAcquireSkills(false);
    for (std::int32_t index = 0; index < acquiredSkillCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t skillLevel = 0;
        std::int32_t skillSubLevel = 0;
        std::int32_t skillLock = 0;
        packet = ::Disassemble(packet, "dddd", &skillId, &skillLevel, &skillSubLevel, &skillLock);
        if (skillId < 100001) {
            user->AcquireSkill(skillId, skillLevel, skillSubLevel, true, true);
        }
        user->SetSkillLock(skillId, skillLevel, skillSubLevel, skillLock);
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (std::int32_t index = 0; index < currentSubjobReuseCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t endTime = 0;
        std::int32_t reuseDelay = 0;
        packet = ::Disassemble(packet, "ddd", &skillId, &endTime, &reuseDelay);
        if (now < endTime) {
            const std::int32_t remaining =
                runtime::MultiplyInt32Wrapping(runtime::SubtractInt32Wrapping(endTime, now), 1000);
            user->m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                skillId, user->m_data->m_nSubJobId, static_cast<std::uint32_t>(reuseDelay),
                static_cast<std::uint32_t>(remaining)
            );
        }
    }
    for (std::int32_t index = 0; index < sharedReuseCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t endTime = 0;
        std::int32_t reuseDelay = 0;
        packet = ::Disassemble(packet, "ddd", &skillId, &endTime, &reuseDelay);
        if (now < endTime) {
            const std::int32_t remaining =
                runtime::MultiplyInt32Wrapping(runtime::SubtractInt32Wrapping(endTime, now), 1000);
            user->m_SkillReuseDelay.AddOrUpdateSkillReuseDelay(
                skillId, 4, static_cast<std::uint32_t>(reuseDelay), static_cast<std::uint32_t>(remaining)
            );
        }
    }

    if (subjobChangeFinished == 0) {
        CVitalData vitalData{};
        user->OnChangeSubJobEnd(SubJob::CHANGE, vitalData);
        const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(0x622, 1, 0), user);
        if (skillInfo != nullptr) {
            user->InfectedSkillBy(nullptr, skillInfo, nullptr, runtime::crt::Time32Now());
        }
    }
    user->AcquireAllHonorsSkill();
    return false;
}

bool DestroyItemPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a84b8, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t itemServerId = 0;
    std::int64_t amount = 0;
    ::Disassemble(packet, "ddQ", &userServerId, &itemServerId, &amount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1390,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
    if (item != nullptr && amount != -1) {
        user->DestroyItemReturn(item, amount);
    }
    return false;
}

} // namespace DBPacketHandler

bool ReplyGetContributionRelatedPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ab8b8. Only NPC-battle and siege agits consume
    // the related-pledge list. Missing pledges are loaded from the incoming
    // packet's database instead of the outgoing world selector.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId;
    std::int32_t pledgeCount;
    packet = ::Disassemble(packet, "dd", &residenceId, &pledgeCount);
    if (residenceId != 0 && pledgeCount != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        if (residence->IsWorldAgit()) {
            CWorldAgit* const agit = static_cast<CWorldAgit*>(residence);
            if (agit->IsNpcBattleAgit() || agit->IsSiegeAgit()) {
                for (std::int32_t index = 0; index < pledgeCount; ++index) {
                    std::int32_t pledgeDatabaseId;
                    packet = ::Disassemble(packet, "d", &pledgeDatabaseId);
                    if (pledgeDatabaseId != 0) {
                        CSPointer<CPledge> pledge =
                            CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
                        if (!pledge) {
                            db.RequestLoadPledge(
                                runtime::process::CurrentDatabaseId(), static_cast<unsigned int>(pledgeDatabaseId), false
                            );
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[NO_ERROR] "
                            //     L"::ReplyGetContributionRelatedPledge "
                            //     L"LoadPledge PledgeDBID[%d]",
                            //     pledgeDatabaseId)
                            // Noise: requesting a clan absent from the local
                            // cache is normal contribution data loading.
                            // L2_LOG_ERROR(Log,
                            //     L"[NO_ERROR] "
                            //     L"::ReplyGetContributionRelatedPledge "
                            //     L"LoadPledge PledgeDBID[%d]",
                            //     pledgeDatabaseId)
                        }
                    }
                }
            }
        }
    }
    return false;
}

bool ReplyCreateAgitAuction(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006af850, primary CacheD reply 0x81.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t auctionId = 0;
    std::int32_t ignored = 0;
    char16_t description[256]{};
    std::int64_t minimumBid = 0;
    std::int32_t bidDay = 0;
    std::int32_t auctionEnd = 0;
    std::int32_t success = 0;
    ::Disassemble(
        packet, "dddSQddd", &userServerId, &auctionId, &ignored, 0x200, description, &minimumBid, &bidDay, &auctionEnd,
        &success
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3444,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    std::wstring hostDescription = ::FixedUtf16ToWide(description);
    CAgitHTML html(user->GetSocket());
    html.SellActionReply(auctionId, minimumBid, bidDay, hostDescription.data(), auctionEnd, success);
    return false;
}

bool ReplyCreateAgitBid(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006afa60, primary CacheD reply 0x82.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t ignoredHeader = 0;
    std::int32_t residenceId = 0;
    std::int32_t ignoredContext = 0;
    std::int64_t price = 0;
    std::int32_t ignoredResult = 0;
    std::int32_t success = 0;
    std::int32_t resultFlags = 0;
    ::Disassemble(
        packet, "ddddQddd", &userServerId, &ignoredHeader, &residenceId, &ignoredContext, &price, &ignoredResult, &success,
        &resultFlags
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x345d,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CAgitHTML html(user->GetSocket());
    html.AgitBidReply(residenceId, price, success, resultFlags);
    return false;
}

bool ReplyLoadAgitBid(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006afc1c. The first row dword is deliberately
    // decoded and ignored before AddAuction2 receives pledge/price/time.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t ignoredHeader = 0;
    std::int32_t bidCount = 0;
    packet = ::Disassemble(packet, "ddd", &residenceId, &ignoredHeader, &bidCount);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;

    std::int32_t ignoredAuctionId = 0;
    std::int64_t deposit = 0;
    std::int32_t pledgeDatabaseId = 0;
    std::int32_t registerTime = 0;
    if (auctionAgit != nullptr) {
        for (std::int32_t index = 0; index < bidCount; ++index) {
            packet = ::Disassemble(packet, "dQdd", &ignoredAuctionId, &deposit, &pledgeDatabaseId, &registerTime);
            auctionAgit->AddAuction2(pledgeDatabaseId, deposit, registerTime);

            CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
            if (!pledge) {
                db.RequestLoadPledge(
                    runtime::process::CurrentDatabaseId(), static_cast<unsigned int>(pledgeDatabaseId), false
                );
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR] ::ReplyLoadAgitBid "
                //     L"LoadPledge PledgeDBID[%d]",
                //     pledgeDatabaseId)
                // Noise: requesting a clan absent from the local cache is
                // normal clan hall bid loading, not a failed database reply.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR] ::ReplyLoadAgitBid "
                //     L"LoadPledge PledgeDBID[%d]",
                //     pledgeDatabaseId)
            }
        }
    }
    return false;
}

bool ReplyAgitPledgeAdena(CDBSocket*, const std::uint8_t* packet) {
    // CacheD reply 0x89 passes decodedCommand before the trailing zero.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t residenceId = 0;
    std::int32_t context = 0;
    std::int64_t pledgeAdena = 0;
    std::int32_t decodedCommand = 0;
    ::Disassemble(packet, "dddQd", &userServerId, &residenceId, &context, &pledgeAdena, &decodedCommand);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3591,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CAgitHTML html(user->GetSocket());
    html.AgitCommand(residenceId, context, pledgeAdena, decodedCommand, 0);
    return false;
}

bool ReplyCancelAgitAuction(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006affb4, primary CacheD reply 0x8a.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t ignoredAuctionId = 0;
    std::int32_t ignoredContext = 0;
    std::int32_t lastCancel = 0;
    ::Disassemble(packet, "dddd", &userServerId, &ignoredAuctionId, &ignoredContext, &lastCancel);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x35aa,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CAgitHTML html(user->GetSocket());
    html.SaleCancelReply(ignoredAuctionId, lastCancel);
    return false;
}

bool ReplyAgitBidCancel(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b0770, primary CacheD reply 0x90.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t residenceId = 0;
    std::int32_t pledgeId = 0;
    std::int32_t ignoredContext = 0;
    std::int64_t ignoredValue = 0;
    std::int64_t ignoredReplyValue = 0;
    std::int32_t result = 0;
    ::Disassemble(
        packet, "ddddQQd", &userServerId, &residenceId, &pledgeId, &ignoredContext, &ignoredValue, &ignoredReplyValue,
        &result
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3672,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (result == 1) {
        CAgitHTML html(userSocket);
        html.AgitBidCancelReply(residenceId, pledgeId, ignoredReplyValue);
    }
    return false;
}

namespace DBPacketHandler {

bool ReplyAddBlockList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006af02c, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t blockedCharacterId = 0;
    char16_t blockedName[25]{};
    std::int32_t listCommand = 0;
    ::Disassemble(packet, "ddSd", &userServerId, &blockedCharacterId, 0x32, blockedName, &listCommand);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3076,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    const std::u16string hostBlockedNameUtf16 = SanitizeUtf16(FixedUtf16View(blockedName));
    if (blockedCharacterId == 0) {
        socket->SendSystemMessage(SystemMessageId::BLOCK_LIST_ADDITION_FAILED);
        socket->Send("chd", 0xfe, 0xed, 0);
    } else {
        user->AddBlockList(blockedCharacterId, hostBlockedNameUtf16.c_str(), u"");
        socket->Send("chdS", 0xfe, 0xed, 1, blockedName);
        user->GetSocket()->SendSystemMessage_S(
            SystemMessageId::USER_ADDED_TO_BLOCK_LIST, hostBlockedNameUtf16.c_str()
        );
    }
    if (listCommand != 0) {
        ListBlockHtmlBuilder::Show(userServerId);
    }
    return false;
}

} // namespace DBPacketHandler

bool ReplyDeleteBlockList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006af250, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t deletedCharacterId = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &deletedCharacterId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x30a4,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (deletedCharacterId == 0) {
        socket->SendSystemMessage(SystemMessageId::BLOCK_LIST_REMOVAL_FAILED);
        socket->Send("chd", 0xfe, 0xee, 0);
    } else {
        char16_t deletedName[25]{};
        ::Disassemble(packet, "S", 0x32, deletedName);
        if (deletedCharacterId > 0) {
            user->DeleteBlockList(deletedCharacterId);
        }
        socket->Send("chdS", 0xfe, 0xee, 1, deletedName);
        const std::u16string hostDeletedName = SanitizeUtf16(FixedUtf16View(deletedName));
        user->GetSocket()->SendSystemMessage_S(
            SystemMessageId::USER_REMOVED_FROM_BLOCK_LIST, hostDeletedName.c_str()
        );
    }
    return false;
}

bool ReplyBlockList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006af45c, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId;
    std::int32_t blockCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &blockCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x30cd,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    for (std::int32_t index = 0; index < blockCount; ++index) {
        std::int32_t blockedCharacterId = 0;
        char16_t blockedName[24]{};
        char16_t blockMemo[51]{};
        packet = ::Disassemble(packet, "dSS", &blockedCharacterId, 0x30, blockedName, 0x66, blockMemo);
        const std::u16string blockedNameText(FixedUtf16View(blockedName));
        const std::u16string blockMemoText(FixedUtf16View(blockMemo));
        user->AddBlockList(blockedCharacterId, blockedNameText.c_str(), blockMemoText.c_str());
    }
    return false;
}

bool ReplyCreateAllianceByNpc(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d3004, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t npcServerId = 0;
    std::int32_t pledgeServerId = 0;
    std::int32_t allianceDatabaseId = 0;
    std::array<SharedUtilsWideChar, 25> allianceName{};
    ::Disassemble(
        packet, "ddddS", &userServerId, &npcServerId, &pledgeServerId, &allianceDatabaseId, 0x32, allianceName.data()
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x27c4,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcServerId));
    CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
    if (npc == nullptr) {
        return false;
    }

    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeServerId));
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x27ce,
        //     pledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", pledgeServerId);
        return false;
    }

    std::int32_t reply = 0;
    if (allianceDatabaseId == 0) {
        socket->SendSystemMessage(SystemMessageId::NAME_CANNOT_BE_USED);
    } else {
        std::u16string targetName;
        for (const SharedUtilsWideChar codeUnit : allianceName) {
            if (codeUnit == SharedUtilsWideChar{}) {
                break;
            }
            targetName.push_back(static_cast<char16_t>(codeUnit));
        }
        const std::wstring hostAllianceName = ::Utf16ToWide(targetName);
        const std::int32_t worldId = runtime::process::CurrentDatabaseId();
        CAlliance* const alliance = new CAlliance(
            static_cast<std::uint32_t>(allianceDatabaseId), hostAllianceName.c_str(), pledge->GetDbId(), worldId, 0, 0
        );
        alliance->AddMemberPledge(pledge);

        std::array<SharedUtilsWideChar, 25> lowerAllianceName{};
        ::ToLowerCase(allianceName.data(), lowerAllianceName.data());
        std::u16string lowerTargetName;
        for (const SharedUtilsWideChar codeUnit : lowerAllianceName) {
            if (codeUnit == SharedUtilsWideChar{}) {
                break;
            }
            lowerTargetName.push_back(static_cast<char16_t>(codeUnit));
        }
        const std::u16string lowerHostAllianceName = SanitizeUtf16(lowerTargetName);
        g_AllianceNameMap.Insert(worldId, lowerHostAllianceName, alliance);
        g_AllianceDbIdMap.Insert(
            std::CDualKey<int, int>(worldId, allianceDatabaseId), alliance, const_cast<char*>("DBSocket.cpp"), 0x27db
        );
        pledge->SetAlliance(alliance, true);
        pledge->UpdatePledgeInfoPacket();
        L2_RELEASE(alliance, ObjectRefType::CreateDelete);
        reply = 1;
    }

    g_npcserver.Send("cdddd", 0x35, user->GetServerId(), user->m_data->index, npc->m_data->index, reply);
    return false;
}

bool ReplyLoadAlliance(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d33a4, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t allianceDatabaseId = 0;
    std::array<SharedUtilsWideChar, 25> allianceName;
    std::int32_t masterPledgeDatabaseId = 0;
    std::int32_t oustTime = 0;
    std::int32_t crestId = 0;
    std::int32_t worldId = 0;
    std::int32_t memberCount = 0;
    packet = ::Disassemble(
        packet, "dSddddd", &allianceDatabaseId, 0x32, allianceName.data(), &masterPledgeDatabaseId, &oustTime, &crestId,
        &worldId, &memberCount
    );

    const std::CDualKey<int, int> allianceKey(worldId, allianceDatabaseId);
    if (g_AllianceDbIdMap.ExistKey(allianceKey)) {
        return false;
    }

    std::u16string targetName;
    for (const SharedUtilsWideChar codeUnit : allianceName) {
        if (codeUnit == SharedUtilsWideChar{}) {
            break;
        }
        targetName.push_back(static_cast<char16_t>(codeUnit));
    }
    const std::wstring hostAllianceName = ::Utf16ToWide(targetName);
    CAlliance* const alliance = new CAlliance(
        static_cast<std::uint32_t>(allianceDatabaseId), hostAllianceName.c_str(),
        static_cast<std::uint32_t>(masterPledgeDatabaseId), worldId, static_cast<std::uint32_t>(oustTime),
        static_cast<std::uint32_t>(crestId)
    );

    for (std::int32_t index = 0; index < memberCount; ++index) {
        std::int32_t pledgeDatabaseId = 0;
        packet = ::Disassemble(packet, "d", &pledgeDatabaseId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeDatabaseId);
        if (!pledge) {
            CPledge::ReserveLoaded(worldId, pledgeDatabaseId, PRLT_ALLIANCE);
        } else {
            alliance->AddMemberPledge(pledge.get());
            pledge->SetAlliance(alliance, false);
        }
    }

    std::array<SharedUtilsWideChar, 25> lowerAllianceName{};
    ::ToLowerCase(allianceName.data(), lowerAllianceName.data());
    std::u16string lowerTargetName;
    for (const SharedUtilsWideChar codeUnit : lowerAllianceName) {
        if (codeUnit == SharedUtilsWideChar{}) {
            break;
        }
        lowerTargetName.push_back(static_cast<char16_t>(codeUnit));
    }
    const std::u16string lowerHostAllianceName = SanitizeUtf16(lowerTargetName);
    g_AllianceNameMap.Insert(worldId, lowerHostAllianceName, alliance);
    g_AllianceDbIdMap.Insert(allianceKey, alliance, const_cast<char*>("DBSocket.cpp"), 0x2da5);
    L2_RELEASE(alliance, ObjectRefType::CreateDelete);

    if (g_JobFinishSignal.SetAllianceLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_ALLIANCE);
    }
    return false;
}

bool ReplyJoinAlliance(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ac968.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t masterPledgeServerId;
    std::uint32_t joiningPledgeServerId;
    int result;
    ::Disassemble(packet, "dddd", &userServerId, &masterPledgeServerId, &joiningPledgeServerId, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x27ff,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CPledge* const masterPledge = CPledge::GetObjectW(masterPledgeServerId);
    if (masterPledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2800,
        //     masterPledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", masterPledgeServerId);
        return false;
    }

    CPledge* const joiningPledge = CPledge::GetObjectW(joiningPledgeServerId);
    if (joiningPledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2801,
        //     joiningPledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", joiningPledgeServerId);
        return false;
    }

    CAlliance* const alliance = user->GetAlliance();
    if (alliance == nullptr) {
        return false;
    }

    if (result == 0) {
        alliance->AddMemberPledge(joiningPledge);
        joiningPledge->SetAlliance(alliance, true);
        socket->Send("cd", 0xbc, 1);
    } else if (result == 2) {
        socket->SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
        socket->Send("cd", 0xbc, 0);
    }
    masterPledge->EndTransaction();
    joiningPledge->EndTransaction();
    return false;
}

bool ReplyDismissAlliance(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d3854, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId = 0;
    std::uint32_t allianceServerId = 0;
    std::uint32_t masterPledgeServerId = 0;
    std::uint32_t dismissalTime = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "ddddd", &userServerId, &allianceServerId, &masterPledgeServerId, &dismissalTime, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2834,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CAlliance* const alliance = CAlliance::GetObjectW(allianceServerId);
    if (alliance == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid alliance[%x]",
        //     L"DBSocket.cpp",
        //     0x2836,
        //     allianceServerId)
        L2_LOG_ERROR(Log, u"[database reply] Alliance reference 0x%x is invalid.", allianceServerId);
        return false;
    }

    CPledge* const pledge = CPledge::GetObjectW(masterPledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2837,
        //     masterPledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", masterPledgeServerId);
        return false;
    }

    if (result == 0) {
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
            socket->Send("cd", 0xad, 0);
        }
        return false;
    }

    std::vector<CPledge*> pledges;
    alliance->GetAllMemberPledge(pledges);
    for (CPledge* const memberPledge : pledges) {
        memberPledge->SetAlliance(nullptr, true);
    }

    std::u16string targetName;
    for (const SharedUtilsWideChar codeUnit : alliance->m_data->m_sAllianceName) {
        if (codeUnit == SharedUtilsWideChar{}) {
            break;
        }
        targetName.push_back(static_cast<char16_t>(codeUnit));
    }
    const std::u16string hostAllianceName = SanitizeUtf16(targetName);
    g_AllianceNameMap.DeleteObject(
        alliance->m_nWorldId, hostAllianceName, const_cast<char*>("DBSocket.cpp"), 0x2845
    );
    g_AllianceDbIdMap.DeleteObject(
        std::CDualKey<int, int>(alliance->m_nWorldId, static_cast<int>(alliance->m_data->m_nDbId)),
        const_cast<char*>("DBSocket.cpp"), 0x2846
    );

    pledge->SetAllianceDismissTime(dismissalTime);
    pledge->Save(0x12);
    if (socket != nullptr) {
        socket->Send("cd", 0xad, 1);
    }

    RaidAllianceService_Dismiss service;
    RaidClientManager::inst().RequestAllianceService(*alliance, service);
    return false;
}

bool ReplyOustAllianceMemberPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006acbc8.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t allianceServerId;
    std::uint32_t masterPledgeServerId;
    std::uint32_t oustedPledgeServerId;
    std::uint32_t oustTime;
    int result;
    ::Disassemble(
        packet, "dddddd", &userServerId, &allianceServerId, &masterPledgeServerId, &oustedPledgeServerId, &oustTime, &result
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2873,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CAlliance* const alliance = CAlliance::GetObjectW(allianceServerId);
    if (alliance == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid alliance[%x]",
        //     L"DBSocket.cpp",
        //     0x2874,
        //     allianceServerId)
        L2_LOG_ERROR(Log, u"[database reply] Alliance reference 0x%x is invalid.", allianceServerId);
        return false;
    }

    CPledge* const masterPledge = CPledge::GetObjectW(masterPledgeServerId);
    if (masterPledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2875,
        //     masterPledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", masterPledgeServerId);
        return false;
    }

    CPledge* const oustedPledge = CPledge::GetObjectW(oustedPledgeServerId);
    if (oustedPledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2876,
        //     oustedPledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", oustedPledgeServerId);
        return false;
    }

    if (result == 0) {
        socket->SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
        socket->Send("cd", 0xac, 0);
    } else {
        alliance->DeleteMemberPledge(oustedPledge->SharedData()->m_nDbId);
        oustedPledge->SetAlliance(nullptr, true);
        oustedPledge->SetAllianceOustedTime(oustTime);
        oustedPledge->Save(0x10);
        alliance->m_data->m_nOustTime = oustTime;
        alliance->Save(0);
        socket->Send("cd", 0xac, 1);
    }
    masterPledge->EndTransaction();
    oustedPledge->EndTransaction();
    return false;
}

bool ReplyWithdrawAlliance(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ace94.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t allianceServerId;
    std::uint32_t pledgeServerId;
    std::uint32_t withdrawTime;
    int result;
    ::Disassemble(packet, "ddddd", &userServerId, &allianceServerId, &pledgeServerId, &withdrawTime, &result);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x28a3,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CAlliance* const alliance = CAlliance::GetObjectW(allianceServerId);
    if (alliance == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid alliance[%x]",
        //     L"DBSocket.cpp",
        //     0x28a4,
        //     allianceServerId)
        L2_LOG_ERROR(Log, u"[database reply] Alliance reference 0x%x is invalid.", allianceServerId);
        return false;
    }

    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x28a5,
        //     pledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", pledgeServerId);
        return false;
    }

    if (result == 0) {
        socket->SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
        socket->Send("cd", 0xab, 0);
    } else {
        alliance->DeleteMemberPledge(pledge->SharedData()->m_nDbId);
        pledge->SetAlliance(nullptr, true);
        pledge->SetAllianceWithdrawTime(withdrawTime);
        pledge->Save(0x11);
        socket->Send("cd", 0xab, 1);
    }
    pledge->EndTransaction();
    return false;
}

bool ReplySetAllianceCrest(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c0d40.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t crestId;
    ::Disassemble(packet, "dd", &userServerId, &crestId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2d37,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CAlliance* const alliance = user->GetAlliance();
    if (alliance == nullptr) {
        return false;
    }

    alliance->m_data->m_nCrestId = crestId;
    alliance->Save(1);
    alliance->SetAllianceCrestToMember(crestId);

    std::vector<CPledge*> pledges;
    alliance->GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->UpdateInfoWindowPacket();
        pledge->UpdatePledgeInfoPacket();
    }
    socket->Send("cd", 0xae, 1);
    return false;
}

bool ReplyModAllianceNamePacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006d2c70, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::uint32_t allianceDatabaseId = 0;
    std::int32_t result = 0;
    std::array<SharedUtilsWideChar, 25> allianceName{};
    ::Disassemble(packet, "ddS", &allianceDatabaseId, &result, 0x32, allianceName.data());

    const auto toHostWide = [](const std::array<SharedUtilsWideChar, 25>& value) {
        std::u16string target;
        target.reserve(value.size());
        for (const SharedUtilsWideChar codeUnit : value) {
            if (codeUnit == SharedUtilsWideChar{}) {
                break;
            }
            target.push_back(static_cast<char16_t>(codeUnit));
        }
        return ::Utf16ToWide(target);
    };

    if (result == 1) {
        const std::u16string name(allianceName.begin(), allianceName.end());
        db.RequestModifyAllianceName(allianceDatabaseId, name.c_str());
        return false;
    }
    if (result != 2) {
        return false;
    }

    CSPointer<CAlliance> alliance =
        g_AllianceDbIdMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(allianceDatabaseId)));
    if (!alliance) {
        return false;
    }

    std::array<SharedUtilsWideChar, 25> oldLowerName{};
    std::array<SharedUtilsWideChar, 25> newLowerName{};
    ::ToLowerCase(alliance->m_data->m_sAllianceName, oldLowerName.data());
    ::ToLowerCase(allianceName.data(), newLowerName.data());

    std::size_t nameLength = 0;
    while (nameLength < allianceName.size() && allianceName[nameLength] != SharedUtilsWideChar{}) {
        ++nameLength;
    }
    if (nameLength >= std::size(alliance->m_data->m_sAllianceName)) {
        // Exact observable wcscpy_s failure state: the destination becomes
        // an empty string. The target ignores the CRT return value.
        alliance->m_data->m_sAllianceName[0] = SharedUtilsWideChar{};
    } else {
        for (std::size_t index = 0; index <= nameLength; ++index) {
            alliance->m_data->m_sAllianceName[index] = allianceName[index];
        }
    }

    const std::wstring oldHostName = toHostWide(oldLowerName);
    g_AllianceNameMap.DeleteObject(g_nWorldId, WideToUtf16(oldHostName), const_cast<char*>("DBSocket.cpp"), 0x21d7);

    const std::wstring newHostName = toHostWide(newLowerName);
    g_AllianceNameMap.Insert(g_nWorldId, WideToUtf16(newHostName), alliance.get());

    std::vector<CPledge*> pledges;
    alliance->GetAllMemberPledge(pledges);
    for (CPledge* const pledge : pledges) {
        pledge->UpdateInfoWindowPacket();
        pledge->UpdatePledgeInfoPacket();
    }
    return false;
}

bool PartyTelBookMarkPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006baa84, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t ignoredWorldId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "dddddd", &userServerId, &result, &ignoredWorldId, &x, &y, &z);
    static_cast<void>(ignoredWorldId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x13ba,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result != 1) {
        return false;
    }

    CParty* const party = user->GetParty();
    if (party == nullptr) {
        return false;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    const int memberCount = static_cast<int>(members.size());
    for (int index = 0; index < memberCount; ++index) {
        const double randomX = ::genrand64_real2();
        const double randomY = ::genrand64_real2();
        User* const member = runtime::CheckedVectorAt(members, index);
        member->StopMove();
        member->TeleportToLocation(
            runtime::AddInt32Wrapping(x, runtime::Win64CvttDoubleToInt32(randomX * 21.0 + 0.0)),
            runtime::AddInt32Wrapping(y, runtime::Win64CvttDoubleToInt32(randomY * 21.0 + 0.0)), z, 0, 0x10000,
            TeleportSourceType::PartyTelBookMarkPacket, nullptr, 0
        );
    }
    return false;
}

bool ReplyBookMark(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c1128, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t ignored = 0;
    std::int32_t userServerId = 0;
    std::int32_t mode = 0;
    packet = ::Disassemble(packet, "ddd", &ignored, &userServerId, &mode);
    static_cast<void>(ignored);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3847,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (user->m_data->m_nBuilder > 7 || !user->IsBuilder()) {
        return false;
    }

    if (mode == 1 || mode == 4) {
        db.RequestBuilderBookMarkList(user, 1);
        return false;
    }

    if (mode == 2) {
        std::int32_t page = 0;
        std::int32_t rowCount = 0;
        packet = ::Disassemble(packet, "dd", &page, &rowCount);

        std::u16string html(
            u"<html><body><center><br>Bookmark list<br>"
            u"<table width=270 border=0>"
        );
        CParseHTML parser(userSocket);

        const auto appendInt = [&html](const std::int32_t value) {
            const std::u16string text = ::Utf8ToUtf16(std::to_string(value));
            html.append(std::u16string_view(text));
        };
        const auto appendName = [&html](const std::array<char16_t, 51>& name) {
            std::size_t length = 0;
            while (length < name.size() && name[length] != u'\0') {
                ++length;
            }
            html.append(std::u16string_view(name.data(), length));
        };

        std::int32_t bookmarkId = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::array<char16_t, 51> bookmarkName{};
        while (rowCount > 0) {
            rowCount = runtime::SubtractInt32Wrapping(rowCount, 1);
            packet = ::Disassemble(packet, "dSddd", &bookmarkId, 0x66, bookmarkName.data(), &x, &y, &z);

            html += u"<tr><td> <a action=\"bypass _bk3_";
            appendInt(bookmarkId);
            html += u"\"> [";
            appendName(bookmarkName);
            html += u", ";
            appendInt(x);
            html += u", ";
            appendInt(y);
            html += u", ";
            appendInt(z);
            html += u"] </a> </td><td> <a action=\"bypass _bk4_";
            appendInt(bookmarkId);
            html += u"\">del</a> </td> </tr>";

            bookmarkId = 0;
            x = 0;
            y = 0;
            z = 0;
            bookmarkName.fill(u'\0');
        }

        // The target decrements the exhausted count once more and never
        // consumes it afterwards.
        rowCount = runtime::SubtractInt32Wrapping(rowCount, 1);
        static_cast<void>(rowCount);

        html += u"<tr><td align=left><table width=270 border=0 "
                u"cellspacing=2 cellpadding=2><tr>";
        if (page > 0) {
            html += u"<td> <a action=\"bypass _bk2_";
            appendInt(runtime::SubtractInt32Wrapping(page, 1));
            html += u"\"> &lt; </a> </td>";
        }

        const std::int32_t candidate = runtime::SubtractInt32Wrapping(page, 5);
        std::int32_t index = candidate > 0 ? candidate : 1;
        while (index < page) {
            html += u"<td> <a action=\"bypass _bk2_";
            appendInt(index);
            html += u"\"> ";
            appendInt(index);
            html += u" </a> </td>";
            index = runtime::AddInt32Wrapping(index, 1);
        }

        html += u" <td> ";
        appendInt(page);
        html += u" </td>";

        index = page;
        while (true) {
            index = runtime::AddInt32Wrapping(index, 1);
            if (runtime::AddInt32Wrapping(page, 5) <= index) {
                break;
            }
            html += u" <td> <a action=\"bypass _bk2_";
            appendInt(index);
            html += u"\"> ";
            appendInt(index);
            html += u" </a> </td>";
        }

        html += u" <td> <a action=\"bypass _bk2_";
        appendInt(runtime::AddInt32Wrapping(page, 1));
        html += u"\"> &gt; </a> </td>"
                u"</tr></table></td><td></td></tr>"
                u"</table></center></body></html>";

        static_cast<void>(parser.SendHTML(std::u16string(html)));
        return false;
    }

    if (mode == 3 || mode == 5) {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        ::Disassemble(packet, "ddd", &x, &y, &z);
        user->StopMove();
        user->TeleportToLocation(x, y, z, user->GetInZoneID(), 0x10000, TeleportSourceType::ReplyBookMark1, nullptr, 0);
    }
    return false;
}

bool ReplyLoadAbnormalStatus(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c09f4, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t abnormalCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &abnormalCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2784,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    user->m_nSongDanceCount = 0;
    user->LoadAbnormalData(static_cast<std::uint32_t>(abnormalCount), packet);

    CSPointer<User> duplicate =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(user->m_data->m_nWorldId, static_cast<int>(user->GetDBID())));
    CUserSocket* socket = user->GetSocket();
    if (socket->IsUnderReloading() || !duplicate) {
        socket = user->GetSocket();
        if (socket != nullptr) {
            const bool enteredWorld = [&] {
                L2_GUARD(socket->mLock);
                if (!socket->m_bActive) {
                    return false;
                }
                user->EnterWorld();
                return true;
            }();
            if (!enteredWorld) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[ReplyLoadAbnormalStatus] m_bActive == false")
                L2_LOG_ERROR(Log, u"[ReplyLoadAbnormalStatus] m_bActive == false");
                return false;
            }
            user->SendCoolTime();
            db.RequestLoadHonorsSkill(user);
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Duplicate character? !!! close both character. "
        //     L"world id[%d] dbid[%d]",
        //     user->m_data->m_nWorldId,
        //     user->GetDBID())
        L2_LOG_ERROR(
            Log,
            u"[session] Duplicate character detected in world %d for DB ID "
            u"%d; both connections are closed.",
            user->m_data->m_nWorldId, user->GetDBID()
        );
        duplicate->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
    }
    user->m_bAbnormalStatusChanged = true;
    return false;
}

bool ReplyConfirmCastleDefence(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aa0e0.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    int context;
    std::uint32_t pledgeId;
    int operation;
    int affectedRows;
    int worldId;
    ::Disassemble(packet, "dddddd", &userServerId, &context, &pledgeId, &operation, &affectedRows, &worldId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1f82,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCastle* castle;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &castle)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x1f84,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (affectedRows == 0) {
        socket->SendSystemMessage(
            operation == 0 ? SystemMessageId::CASTLE_DEFENDER_REJECTION_FAILED
                           : SystemMessageId::CASTLE_DEFENDER_APPROVAL_FAILED
        );
    } else {
        if (operation == 0) {
            CSPointer<CPledge> pledge = CPledge::FindPledge(worldId, pledgeId);
            castle->CancelCastleDefenseSiege(pledge.get());
        } else {
            castle->ApproveDefence(pledgeId, worldId);
        }
        castle->SendCastleSiegeDefenderList(socket, user);
    }

    L2_RELEASE(castle, ObjectRefType::Cached);
    return false;
}

bool ReplyQueryCastleSiegeForUnregister(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aa674. Current CacheD publishes each row as
    // `dd`. This current receiver consumes `ddd`.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    int context;
    int castleId;
    std::uint32_t pledgeId;
    int rowCount;
    packet = ::Disassemble(packet, "ddddd", &userServerId, &context, &castleId, &pledgeId, &rowCount);
    static_cast<void>(pledgeId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2087,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCastle* castle;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &castle)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x2089,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    bool found = false;
    bool wasAttacker = false;
    for (int index = 0; index < rowCount; ++index) {
        int relatedCastleId;
        int relatedNextSiege;
        int registrationType;
        packet = ::Disassemble(packet, "ddd", &relatedCastleId, &relatedNextSiege, &registrationType);
        static_cast<void>(relatedNextSiege);
        if (relatedCastleId == castleId) {
            found = true;
            if (registrationType == 1) {
                wasAttacker = true;
                found = true;
            }
            break;
        }
    }

    CPledge* const pledge = user->GetPledge();
    if (found && pledge != nullptr) {
        db.RequestPrepareCastleWar(user, castle, pledge, wasAttacker ? 3 : 4, runtime::crt::Time32Now());
    } else {
        socket->SendSystemMessage(SystemMessageId::NOT_REGISTERED_FOR_CASTLE_SIEGE);
    }

    L2_RELEASE(castle, ObjectRefType::Cached);
    return false;
}

bool ReplyQueryCastleSiegeForInfo(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aa8f8. Current CacheD publishes each row as
    // `dd`. This current receiver consumes `ddd`.
    L2_GUARD_FUNCTION();

    int npcSharedIndex;
    std::uint32_t userServerId;
    int context;
    int castleId;
    std::uint32_t pledgeId;
    int rowCount;
    packet = ::Disassemble(packet, "dddddd", &npcSharedIndex, &userServerId, &context, &castleId, &pledgeId, &rowCount);
    static_cast<void>(pledgeId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x20ce,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    CCastle* castle;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &castle)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x20d0,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    int registrationType = -1;
    for (int index = 0; index < rowCount; ++index) {
        int relatedCastleId;
        int relatedNextSiege;
        int relatedRegistrationType;
        packet = ::Disassemble(packet, "ddd", &relatedCastleId, &relatedNextSiege, &relatedRegistrationType);
        static_cast<void>(relatedNextSiege);
        registrationType = relatedRegistrationType;
        if (relatedCastleId == castleId) {
            break;
        }
        registrationType = -1;
    }

    int reply = 5;
    if (registrationType == 1) {
        reply = 1;
    } else if (registrationType == 2) {
        reply = 2;
    } else if (registrationType == 3) {
        reply = 4;
    } else if (registrationType == 4) {
        reply = 3;
    }
    g_npcserver.Send("cddddd", 0x36, user->GetServerId(), user->m_data->index, npcSharedIndex, 4, reply);

    L2_RELEASE(castle, ObjectRefType::Cached);
    return false;
}

bool ReplyQueryCastleSiegeForDismiss(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aabc0. The third dword is the echoed pledge DBID
    // and is decoded but otherwise ignored.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t pledgeServerId;
    std::uint32_t pledgeDatabaseId;
    int npcSharedIndex;
    int rowCount;
    ::Disassemble(packet, "ddddd", &userServerId, &pledgeServerId, &pledgeDatabaseId, &npcSharedIndex, &rowCount);
    static_cast<void>(pledgeDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2122,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CPledge* const pledge = CPledge::GetObjectW(pledgeServerId);
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid pledge[%x]",
        //     L"DBSocket.cpp",
        //     0x2123,
        //     pledgeServerId)
        L2_LOG_ERROR(Log, u"[database reply] Pledge reference 0x%x is invalid.", pledgeServerId);
        return false;
    }

    if (rowCount < 1) {
        user->TakeDismissPenalty();
        if (!g_bTestServerPenalty) {
            user->ExpDown(runtime::Win64CvttDoubleToInt64(user->GetDefaultExpDownValue(user->m_data->m_nLevel)));
        }
        const std::int32_t now = runtime::crt::Time32Now();
        pledge->SetDismissReservedTime(
            static_cast<std::uint32_t>(runtime::AddInt32Wrapping(BP::_PLEDGE_DISMISS_TIMEOUT, now))
        );
        pledge->SetStatus(3);
        pledge->Save(0xf);
        pledge->Save(0xb);
        pledge->UpdateInfoWindowPacket();
        g_PledgeTimer.ReserveDismiss(pledge->GetDbId(), pledge->GetDismissReservedTime());
        g_npcserver.Send("cdddd", 0x22, user->GetServerId(), user->m_data->index, npcSharedIndex, 1);
    } else {
        g_npcserver.Send("cdddd", 0x22, user->GetServerId(), user->m_data->index, npcSharedIndex, 0);
        socket->SendSystemMessage(SystemMessageId::CANNOT_DISSOLVE_CLAN_PARTICIPATING_IN_SIEGE);
    }
    return false;
}

bool ReplyCrystallizeItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ab1d8. The paired current CacheD opcode 0x5d
    // owner fixes the source-result header and all generated ddQd rows.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t sourceItemServerId = 0;
    std::int64_t sourceRemainingAmount = 0;
    std::int32_t generatedCount = 0;
    packet = ::Disassemble(packet, "ddQd", &userServerId, &sourceItemServerId, &sourceRemainingAmount, &generatedCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x22dd,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    CItem* const sourceDescriptor = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(sourceItemServerId));
    if (sourceDescriptor == nullptr) {
        user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        return false;
    }
    if (sourceRemainingAmount == -1) {
        user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] ReplyCrystallizeItem fail.",
        //     "DBSocket.cpp",
        //     0x22ed)
        L2_LOG_ERROR(
            Log, u"[crafting] Crystallization failed for source item runtime ID %d; CacheD returned -1.", sourceItemServerId
        );
        return false;
    }

    bool sourceRemoved = false;
    bool unequipFailed = false;
    bool useUserInventory = true;
    CInventory* inventory = &user->m_inv;
    CItem* sourceItem = inventory->GetByDBID(static_cast<int>(sourceDescriptor->GetDBID()));
    CPet* const pet = user->GetPet();
    if (sourceItem == nullptr && pet != nullptr) {
        inventory = &pet->m_inv;
        sourceItem = inventory->GetByDBID(static_cast<int>(sourceDescriptor->GetDBID()));
        useUserInventory = false;
    }

    if (sourceItem != nullptr) {
        if (sourceRemainingAmount == 0) {
            if (useUserInventory) {
                if (user->UnequipItem(sourceItem, true) == ST_NONE) {
                    unequipFailed = true;
                }
            } else {
                pet->UnequipItem(sourceItem, false);
            }
            if (!unequipFailed) {
                {
                    L2_WRITE_GUARD(*inventory);
                    inventory->Pop(sourceItem->m_data->index, false);
                    inventory->SetInventoryChanged(sourceItem, IUT_DELETE);
                }
                sourceRemoved = true;
            }
        } else {
            {
                L2_WRITE_GUARD(*inventory);
                if (!sourceItem->SetAmount(sourceRemainingAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2320,
                    //     sourceItem->m_data->m_nCount,
                    //     sourceRemainingAmount)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                        sourceItem->m_data->m_nCount, sourceRemainingAmount
                    );
                }
                inventory->SetInventoryChanged(sourceItem, IUT_UPDATE);
            }
        }
    }

    const bool hasVariation = sourceDescriptor->m_itemOption.itemOptionKey.variationOption1 > 0 &&
        sourceDescriptor->m_itemOption.itemOptionKey.variationOption2 > 0;
    user->GetSocket()->Send(
        "chccdc", 0x62, SystemMessageId::ITEM_CRYSTALLIZED, 1, 8, sourceDescriptor->m_data->m_nClassID, hasVariation
    );
    user->PledgeMissionCheckAndAddCount(static_cast<PledgeMission::GoalType>(0x13), 0, 1, 1);

    for (std::int32_t index = 0; index < generatedCount; ++index) {
        std::int32_t generatedDatabaseId = 0;
        std::int32_t generatedClassId = 0;
        std::int64_t generatedAmount = 0;
        std::int32_t created = 0;
        packet = ::Disassemble(packet, "ddQd", &generatedDatabaseId, &generatedClassId, &generatedAmount, &created);
        user->GetSocket()->Send(
            "chccdcQ", 0x62, SystemMessageId::EARNED_MULTIPLE_ITEMS, 2, 3, generatedClassId, 6, generatedAmount
        );

        if (created == 0) {
            CItem* const generatedItem = inventory->GetByDBID(generatedDatabaseId);
            if (generatedItem == nullptr) {
                user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
                runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"DBSocket.cpp",
                //     0x2341,
                //     generatedDatabaseId)
                L2_LOG_ERROR(
                    Log, u"[crafting] Crystallization result references missing item DB ID %d.", generatedDatabaseId
                );
                return false;
            }
            {
                L2_WRITE_GUARD(*inventory);
                if (!generatedItem->AddAmount(generatedAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2348,
                    //     generatedItem->m_data->m_nCount,
                    //     generatedAmount)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                        generatedItem->m_data->m_nCount, generatedAmount
                    );
                }
                inventory->SetInventoryChanged(generatedItem, IUT_UPDATE);
            }
        } else {
            CItem* const generatedItem = g_ObjectDB.CreateItem(generatedClassId);
            if (generatedItem == nullptr) {
                user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
                runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] cannot create Item[%d].",
                //     L"DBSocket.cpp",
                //     0x2355,
                //     generatedClassId)
                L2_LOG_ERROR(Log, u"[crafting] Cannot create crystallization result item class %d.", generatedClassId);
                return false;
            }
            generatedItem->SetDBID(static_cast<unsigned int>(generatedDatabaseId));
            if (!generatedItem->SetAmount(generatedAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBSocket.cpp",
                //     0x235c,
                //     generatedItem->m_data->m_nCount,
                //     generatedAmount)
                L2_LOG_ERROR(
                    Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                    generatedItem->m_data->m_nCount, generatedAmount
                );
            }
            if (!inventory->Push(generatedItem->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     "DBSocket.cpp",
                //     0x2361)
                L2_LOG_ERROR(
                    Log, "[inventory] Generated item %u could not be inserted into its destination inventory.",
                    generatedItem->GetDBID()
                );
            }
            inventory->SetInventoryChanged(generatedItem, IUT_ADD);
        }
    }

    user->SetPendingUseETCItem(nullptr, PUIT_CRYSTAL);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitVariation, 0);
    if (sourceRemoved && !unequipFailed) {
        sourceDescriptor->Delete();
    }
    if (useUserInventory) {
        user->SendItemList(true, true, nullptr);
    } else {
        pet->SendPetItemList(true);
    }
    return false;
}

namespace DBPacketHandler {

bool _ReplyPrepareCastleWar(
    const std::uint32_t userServerId,
    const std::uint32_t context,
    const std::uint32_t operation,
    const std::uint32_t proposeTime,
    const std::uint32_t result
) {
    // Current L2Server 0x006a9dc4.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1f16,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCastle* castle;
    CPledge* pledge;
    if (!Singleton<CCacheContextManager>::inst().GetContext(static_cast<int>(context), "pp", &castle, &pledge)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x1f19,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ReplyPrepareCastleWar [%d][%d][%d]",
        //     operation,
        //     castle->m_data != nullptr
        //         ? castle->m_data->m_nResidenceID
        //         : 0,
        //     pledge->GetDbId())
        L2_LOG_ERROR(
            Log,
            u"[castle siege] Prepare-war reply failed: operation %d, castle "
            u"%d, pledge %d.",
            operation, castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, pledge->GetDbId()
        );
        L2_RELEASE(pledge, ObjectRefType::Cached);
        L2_RELEASE(castle, ObjectRefType::Cached);
        return false;
    }

    const bool isUsersPledge = pledge->GetDbId() == user->m_data->m_nPledgeID;
    if (operation == 1) {
        castle->ReserveAttacker(pledge, static_cast<int>(proposeTime));
        if (isUsersPledge) {
            castle->SendCastleSiegeAttackerList(socket, user);
        }
    } else if (operation == 2) {
        castle->ReserveDefender(pledge, static_cast<int>(proposeTime), 2);
        if (isUsersPledge) {
            castle->SendCastleSiegeDefenderList(socket, user);
        }
    } else if (operation == 3) {
        castle->CancelCastleAttackSiege(pledge);
        if (isUsersPledge) {
            castle->SendCastleSiegeAttackerList(socket, user);
        }
        castle->CancelCastleDefenseSiege(pledge);
        if (isUsersPledge) {
            castle->SendCastleSiegeDefenderList(socket, user);
        }
    } else if (operation == 4) {
        castle->CancelCastleDefenseSiege(pledge);
        if (isUsersPledge) {
            castle->SendCastleSiegeDefenderList(socket, user);
        }
    }

    L2_RELEASE(pledge, ObjectRefType::Cached);
    L2_RELEASE(castle, ObjectRefType::Cached);
    return true;
}

bool ReplyPrepareCastleWar(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b66e8.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::uint32_t context;
    std::uint32_t operation;
    std::uint32_t proposeTime;
    std::uint32_t result;
    ::Disassemble(packet, "ddddd", &userServerId, &context, &operation, &proposeTime, &result);
    _ReplyPrepareCastleWar(userServerId, context, operation, proposeTime, result);
    return false;
}

bool __ReplyQueryCastleSiegeForRegister(
    const std::uint32_t userServerId,
    const int context,
    const std::uint32_t castleId,
    const std::uint32_t pledgeId,
    const int operation,
    const int rowCount,
    const std::uint8_t* packet
) {
    // Current L2Server 0x006aa340. Current CacheD publishes
    // each related-siege row as `dd`. This current receiver still consumes
    // `ddd`, including the third lane and its resulting packet advance.
    L2_GUARD_FUNCTION();

    static_cast<void>(pledgeId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x1fcf,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CCastle* castle;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &castle)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x1fd1,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (rowCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"__ReplyQueryCastleSiegeForRegister [%d][%d][%d]",
        //     operation,
        //     castle->m_data != nullptr
        //         ? castle->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID())
        L2_LOG_ERROR(
            Log,
            u"[castle siege] Registration query reply failed: operation %d, "
            u"castle %d, character %d.",
            operation, castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0, user->GetDBID()
        );
        L2_RELEASE(castle, ObjectRefType::Cached);
        return false;
    }

    const std::int32_t castleNextSiege = castle->m_data->m_NextSiege;
    int overlapKind = 0;
    int overlappingRegistrationType = 0;
    for (int index = 0; index < rowCount; ++index) {
        std::int32_t relatedCastleId;
        std::int32_t relatedNextSiege;
        std::int32_t registrationType;
        packet = ::Disassemble(packet, "ddd", &relatedCastleId, &relatedNextSiege, &registrationType);
        if (runtime::SubtractInt32Wrapping(relatedNextSiege, 0x708) <
                runtime::AddInt32Wrapping(castleNextSiege, 0x1c20) &&
            runtime::SubtractInt32Wrapping(castleNextSiege, 0x708) <
                runtime::AddInt32Wrapping(relatedNextSiege, 0x1c20)) {
            overlapKind = (castleId != static_cast<std::uint32_t>(relatedCastleId)) + 1;
            overlappingRegistrationType = registrationType;
            break;
        }
    }

    int requestedRegistrationType = -1;
    if (operation == 1) {
        requestedRegistrationType = 1;
    } else if (operation == 2) {
        requestedRegistrationType = 2;
    }

    bool prepared = false;
    if (overlapKind == 0 || (overlapKind == 2 && overlappingRegistrationType == requestedRegistrationType)) {
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr) {
            db.RequestPrepareCastleWar(user, castle, pledge, requestedRegistrationType, runtime::crt::Time32Now());
            prepared = true;
        }
    } else if (overlapKind == 1) {
        socket->SendSystemMessage(SystemMessageId::ALREADY_REGISTERED_FOR_CASTLE_SIEGE);
    } else if (overlapKind == 2) {
        socket->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_ALREADY_IN_ANOTHER_CASTLE_SIEGE);
    }

    L2_RELEASE(castle, ObjectRefType::Cached);
    return prepared;
}

bool ReplyQueryCastleSiegeForRegister(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b67dc.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::int32_t context;
    std::uint32_t castleId;
    std::uint32_t pledgeId;
    std::int32_t operation;
    std::int32_t rowCount;
    packet = ::Disassemble(packet, "dddddd", &userServerId, &context, &castleId, &pledgeId, &operation, &rowCount);
    __ReplyQueryCastleSiegeForRegister(userServerId, context, castleId, pledgeId, operation, rowCount, packet);
    return false;
}

bool _ReplySetNextCastleSiege(
    const std::uint32_t userServerId,
    const int context,
    const std::uint32_t castleId,
    const std::uint32_t siegeTime,
    const std::uint32_t result
) {
    // Current L2Server 0x006af634.
    L2_GUARD_FUNCTION();

    static_cast<void>(siegeTime);
    CCastle* castle;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &castle)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3419,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (userServerId == 0) {
        if (result == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to Register Next Castle[%d] Siege, Try Next Time",
            //     castle->m_data != nullptr
            //         ? castle->m_data->m_nResidenceID
            //         : 0)
            L2_LOG_ERROR(
                Log,
                u"[castle siege] Cannot register the next siege for castle "
                u"%d; the server will retry later.",
                castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0
            );
        }
        L2_RELEASE(castle, ObjectRefType::Cached);
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // The consumed castle context is not released here.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3426,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Failed to Register Next Castle[%d] Siege, Try Next Time",
        //     castleId)
        L2_LOG_ERROR(
            Log,
            u"[castle siege] Cannot register the next siege for castle %d; "
            u"the server will retry later.",
            castleId
        );
    }
    L2_RELEASE(castle, ObjectRefType::Cached);
    return true;
}

bool ReplySetNextCastleSiege(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b7924.
    L2_GUARD_FUNCTION();

    std::uint32_t userServerId;
    std::int32_t context;
    std::uint32_t castleId;
    std::uint32_t siegeTime;
    std::uint32_t result;
    ::Disassemble(packet, "ddddd", &userServerId, &context, &castleId, &siegeTime, &result);
    _ReplySetNextCastleSiege(userServerId, context, castleId, siegeTime, result);
    return false;
}

} // namespace DBPacketHandler

bool ReplyContributionWinnerPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b68d8, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t publishWinner = 0;
    std::int32_t pledgeDatabaseId = 0;
    ::Disassemble(packet, "ddd", &residenceId, &publishWinner, &pledgeDatabaseId);
    if (residenceId == 0) {
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CWorldAgit* const worldAgit = residence != nullptr ? residence->CastWorldAgit() : nullptr;
    if (worldAgit == nullptr) {
        return false;
    }

    CNpcBattleAgit* const npcBattleAgit = worldAgit->CastNpcBattleAgit();
    if (npcBattleAgit != nullptr) {
        if (publishWinner != 0) {
            npcBattleAgit->m_nWinnerPledgeId = pledgeDatabaseId;
            return false;
        }
        if (pledgeDatabaseId == 0) {
            return false;
        }

        CSPointer<CNPC> boss = npcBattleAgit->GetBossNpc();
        CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
        if (boss && pledge) {
            g_npcserver.Send(
                "cdddd", 0x2e, static_cast<std::int32_t>(boss->m_nId), boss->m_data->index,
                static_cast<std::int32_t>(pledge->GetDbId()), pledge->GetIndex()
            );
        }
        return false;
    }

    if (worldAgit->CastTeamBattleAgit() != nullptr) {
        return false;
    }
    CSiegeAgit* const siegeAgit = worldAgit->CastSiegeAgit();
    if (siegeAgit == nullptr) {
        return false;
    }
    if (publishWinner != 0) {
        siegeAgit->m_nWinnerPledgeId = pledgeDatabaseId;
        return false;
    }
    if (pledgeDatabaseId == 0) {
        return false;
    }

    CSPointer<CNPC> boss = siegeAgit->GetBossNpc();
    CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
    if (boss && pledge) {
        g_npcserver.Send(
            "cdddd", 0x2e, static_cast<std::int32_t>(boss->m_nId), boss->m_data->index,
            static_cast<std::int32_t>(pledge->GetDbId()), pledge->GetIndex()
        );
    }
    return false;
}

bool CreateCharacterFailurePacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t userSocketId = 0;
    ::Disassemble(packet, "dd", &result, &userSocketId);
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBSocket.cpp",
        //     0x614)
        L2_LOG_ERROR(
            Log, u"[character] Cannot deliver character-creation failure %d: socket ID %d is no longer active.", result,
            userSocketId
        );
        return false;
    }
    socket->Send("cd", 0x10, result);
    return false;
}

bool DeleteCharacterSuccessPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    std::int32_t accountId = 0;
    std::int32_t characterId = 0;
    std::int32_t characterLevel = 0;
    std::int32_t firstAuthValue = 0;
    std::int32_t secondAuthValue = 0;
    std::int32_t reloadCharacterList = 0;
    ::Disassemble(
        packet, "ddddddd", &userSocketId, &accountId, &characterId, &characterLevel, &firstAuthValue, &secondAuthValue,
        &reloadCharacterList
    );

    if (g_bDualBillingSystem && firstAuthValue >= 0) {
        authd.AuthDeleteChar(accountId, characterId, characterLevel, firstAuthValue, secondAuthValue);
    }
    if (reloadCharacterList <= 0) {
        return false;
    }

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (socket) {
        socket->Send("c", 0x1d);
        db.RequestLoadChar(socket->GetAccountID(), socket.get());
    }
    return false;
}

bool DeleteCharacterFailurePacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &userSocketId, &result);
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBSocket.cpp",
        //     0x65a)
        L2_LOG_ERROR(
            Log, u"[character] Cannot deliver character-deletion failure %d: socket ID %d is no longer active.", result,
            userSocketId
        );
        return false;
    }
    socket->Send("cd", 0x1e, result);
    db.RequestLoadChar(socket->GetAccountID(), socket.get());
    return false;
}

bool CreateInitItemsPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    ::Disassemble(packet, "d", &userSocketId);
    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBSocket.cpp",
        //     0x674)
        L2_LOG_ERROR(
            Log, u"[character] Cannot deliver initial-item result: socket ID %d is no longer active.", userSocketId
        );
        return false;
    }
    socket->Send("cd", 0x0f, 1);
    return false;
}

bool ReplyGetDbRelatedCounts(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t mustLoadAlliance = 0;
    std::int32_t mustLoadWar = 0;
    std::int32_t mustLoadDismissReserved = 0;
    std::int32_t mustLoadNobless = 0;
    std::int32_t mustLoadMatchCount = 0;
    std::int32_t mustLoadWOlympiadUser = 0;
    packet = ::Disassemble(
        packet, "ddddd", &mustLoadAlliance, &mustLoadWar, &mustLoadDismissReserved, &mustLoadNobless, &mustLoadMatchCount
    );
    ::Disassemble(packet, "d", &mustLoadWOlympiadUser);

    g_JobFinishSignal.m_nMustLoadAlliance = mustLoadAlliance;
    g_JobFinishSignal.m_nMustLoadWar = mustLoadWar;
    g_JobFinishSignal.m_nMustLoadDismissReserved = mustLoadDismissReserved;
    g_JobFinishSignal.m_nMustLoadNobless = mustLoadNobless;
    g_JobFinishSignal.m_nMustLoadMatchCount = mustLoadMatchCount;
    g_JobFinishSignal.m_nMustLoadWOlympiadUser = mustLoadWOlympiadUser;
    g_JobFinishSignal.NotifyFinish(JFT_LOAD_DB_RELATED_COUNTS);
    // Log.Add(
    //     LOG_ERROR,
    //     "[NO_ERROR] Alliance: [%d], Dismiss: [%d], LoadWar: [%d]",
    //     g_JobFinishSignal.m_nMustLoadAlliance,
    //     g_JobFinishSignal.m_nMustLoadDismissReserved,
    //     g_JobFinishSignal.m_nMustLoadWar)
    // Diagnostic improvement: this is a normal startup count reply from CacheD.
    L2_LOG_INFO(
        Log,
        "[database] CacheD reported records to load: alliances=%d, "
        "clans pending dissolution=%d, clan wars=%d. Zero means no records to load.",
        g_JobFinishSignal.m_nMustLoadAlliance, g_JobFinishSignal.m_nMustLoadDismissReserved, g_JobFinishSignal.m_nMustLoadWar
    );
    return false;
}

bool ReplyEventPoint(CDBSocket*, const std::uint8_t*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool ReplySavePledgePower(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    int ignoredPledgeId;
    int payloadSize = 0;
    std::uint8_t payload[0x100];
    ::Disassemble(packet, "ddb", &ignoredPledgeId, &payloadSize, 0x100, payload);
    return false;
}

bool GetPunishPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006bd5d0, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t punishCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &punishCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user && punishCount > 0) {
        std::int32_t punishType = 0;
        std::int32_t ignoredPunishOn = 0;
        std::int32_t remainingMinutes = 0;
        for (std::int32_t index = 0; index < punishCount; ++index) {
            packet = ::Disassemble(packet, "ddd", &punishType, &ignoredPunishOn, &remainingMinutes);
            if (punishType == 2) {
                user->SetStopSay(
                    static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(remainingMinutes, 60000)), false
                );
            } else if (punishType == 3) {
                user->SetStopBotReport(
                    static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(remainingMinutes, 60000)), false
                );
            } else if (punishType == 4) {
                user->SetStopHeroSay(
                    static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(remainingMinutes, 60000)), false
                );
            } else if (punishType == 5) {
                user->SetStopPartyMatch(
                    static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(remainingMinutes, 60000)), false
                );
                CPartyMatchingRequest::inst().OnLeaveWorld(user);
            } else if (punishType == 6) {
                user->SetStopWorldSay(
                    static_cast<std::uint32_t>(runtime::MultiplyInt32Wrapping(remainingMinutes, 60000)), false
                );
            }
            punishType = 0;
            ignoredPunishOn = 0;
            remainingMinutes = 0;
        }
    }
    return false;
}

bool GetLoginAnnouncePacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006bd91c, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t announceCount = 0;
    packet = ::Disassemble(packet, "dd", &characterId, &announceCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }

    std::int32_t ignoredAnnounceId = 0;
    char16_t announce[0x100];
    std::memset(announce, 0, 0x1fe);
    for (std::int32_t index = 0; index < announceCount; ++index) {
        packet = ::Disassemble(packet, "dS", &ignoredAnnounceId, 0x1fe, announce);
        const std::u16string announceText(::FixedUtf16View(announce));
        user->GetSocket()->SendSystemMessage_Announce(announceText.c_str());
        ignoredAnnounceId = 0;
        std::memset(announce, 0, 0x1fe);
    }

    const std::uint32_t accountId = user->GetAccountID();
    std::uint32_t loginFlag = 0;
    {
        L2_WRITE_GUARD(AccountAuthInfoLock);
        const auto account = g_AccountAuthInfoMap.find(accountId);
        if (account == g_AccountAuthInfoMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] account not found... account_id(%d)",
            //     "DBSocket.cpp",
            //     0x1801,
            //     accountId)
            L2_LOG_ERROR(
                Log, u"[authentication] Account ID %u has no cached authentication state; login flags default to zero.",
                accountId
            );
        } else {
            loginFlag = static_cast<std::uint32_t>(account->second.nLoginFlag);
        }
    }

    if ((loginFlag & 2U) != 0U) {
        user->GetSocket()->SendSystemMessage_Announce(
            // Original: u"오픈 베타 기간내에 실명확인센터를 통해 실명확인 작업을 진행하여 주시기 바랍니다."
            u"Please verify your real name at the Real Name Verification Center during the open beta."
        );
        user->GetSocket()->SendSystemMessage_Announce(
            // Original: u"실명확인이 안된 고객은 상용화 전환이 되지 않으며, 서비스 정책에 따라 관련 모든 정보가 삭제될 수
            // 있습니다."
            u"Accounts without real name verification do not move to the paid service, and all their data may be deleted "
            u"under the service policy."
        );
        // Original: u"자세한 사항은 홈페이지를 참고하시기 바랍니다."
        user->GetSocket()->SendSystemMessage_Announce(u"Please see the website for details.");
    }

    const std::int32_t payKind = user->GetSocket()->m_nPayKind;
    if (!user->GetSocket()->m_bReenterWorld) {
        user->GetSocket()->m_bReenterWorld = true;
        if (payKind == 3 || payKind == 1 || payKind == 4) {
            const std::int32_t leftTime = user->GetSocket()->GetCurrentLeftTime();
            const std::int32_t secondsWithinHour = leftTime % 3600;
            const std::int8_t minutes = static_cast<std::int8_t>(secondsWithinHour / 60);
            const std::int16_t hours = static_cast<std::int16_t>(leftTime / 3600);

            if (payKind == 4) {
                user->GetSocket()->Send(
                    "chcchcccc", 0x62, SystemMessageId::PREPAID_USAGE_TIME_AND_RESERVATIONS_REMAINING, 3, 0x15,
                    static_cast<int>(hours), 0x14, static_cast<int>(minutes), 0x14,
                    static_cast<int>(static_cast<std::int8_t>(user->GetSocket()->m_nReservations))
                );
            } else {
                user->GetSocket()->Send(
                    "chcchcc", 0x62,
                    payKind == 1 ? SystemMessageId::INDIVIDUAL_ACCOUNT_USAGE_HOURS_AND_MINUTES_REMAINING
                                 : SystemMessageId::PC_CAFE_PREPAID_HOURS_AND_MINUTES_REMAINING,
                    2, 0x15, static_cast<int>(hours), 0x14, static_cast<int>(minutes)
                );
            }
        }
    } else if (payKind == 1 || payKind == 4) {
        authd.AuthRequestGetValueInfo(static_cast<int>(accountId));
    } else if (payKind == 3) {
        const std::int32_t leftTime = user->GetSocket()->GetCurrentLeftTime();
        const std::int32_t secondsWithinHour = leftTime % 3600;
        user->GetSocket()->Send(
            "chcchcc", 0x62, SystemMessageId::PC_CAFE_PREPAID_HOURS_AND_MINUTES_REMAINING, 2, 0x15,
            static_cast<int>(static_cast<std::int16_t>(leftTime / 3600)), 0x14,
            static_cast<int>(static_cast<std::int8_t>(secondsWithinHour / 60))
        );
    }

    if (g_nPlayTimeLimit > 0 && !user->GetSocket()->m_bForceLogin) {
        static_cast<void>(user->GetSocket()->GetSocketPlayTime());
        // Log.Add(
        //     LOG_ERROR,
        //     L" not used Charge Type. ")
        L2_LOG_ERROR(
            Log,
            u"[play time] Database reply contains an unused charge type; "
            u"the value is ignored."
        );
    }

    if (user->GetSocket()->IsApplyChinaFatigueSystem() && g_nChinaFatigueSystemType > 1) {
        user->GetSocket()->ShowFatigueStatus_china();
        if (user->GetSocket()->IsFullFatigue_china()) {
            user->GetSocket()->SendSystemMessage_d(
                SystemMessageId::UNHEALTHY_PLAYTIME_REWARDS_DISABLED_UNTIL_RESTED, g_ChinaFullFatigueTime
            );
        } else if (user->GetSocket()->IsHalfFatigue_china()) {
            user->GetSocket()->SendSystemMessage_d(SystemMessageId::FATIGUE_PLAYTIME_REWARDS_HALVED, g_ChinaHalfFatigueTime);
        }
    }
    return false;
}

bool ReplyResetUserNickname(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    ::Disassemble(packet, "d", &characterId);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->SetNickName(u"");
        user->ParamChanged(PCB_NICKNAME | PCB_USER);
    }
    return false;
}

bool UpdateCharPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006bb698, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int64_t sp = 0;
    std::int64_t experience = 0;
    std::int32_t karma = 0;
    std::int32_t pkCount = 0;
    std::uint32_t pkPardonCount = 0;
    std::uint32_t duelCount = 0;
    ::Disassemble(packet, "dQQdddd", &characterId, &sp, &experience, &karma, &pkCount, &pkPardonCount, &duelCount);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, "
        //     L"world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterId)
        L2_LOG_ERROR(
            Log, u"[database reply] Character lookup failed in world %d for DB ID %d.",
            runtime::process::CurrentDatabaseId(), characterId
        );
        return false;
    }

    user->UpdateChar(sp, experience, karma, pkCount, pkPardonCount, duelCount);
    user->ValidateChanged(VB_MAX);
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_UPDATECHAR});
    return false;
}

bool UpdateCharPledgePacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    char16_t nickname[0x100]{};
    ::Disassemble(packet, "dS", &characterId, 0x200, nickname);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, "
        //     L"world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterId)
        L2_LOG_ERROR(
            Log, u"[database reply] Character lookup failed in world %d for DB ID %d.",
            runtime::process::CurrentDatabaseId(), characterId
        );
        return false;
    }

    const std::u16string hostNickname = SanitizeUtf16(FixedUtf16View(nickname));
    user->SetNickName(hostNickname.c_str());
    user->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_NICKNAME});
    return false;
}

bool ReplySendSystemMessage(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int16_t messageId = -1;
    char parameterType = 0;
    packet = ::Disassemble(packet, "dhc", &characterId, &messageId, &parameterType);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        return false;
    }
    if (parameterType == 0 && messageId > 0) {
        user->SendSystemMessage(static_cast<unsigned int>(messageId));
        return false;
    }

    std::int32_t parameterSize = 0;
    packet = ::Disassemble(packet, "d", &parameterSize);
    user->GetSocket()->Send(
        "chcb", 0x62, static_cast<int>(messageId), static_cast<int>(parameterType), parameterSize, packet
    );
    return false;
}

bool ReportPacketLog(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::uint32_t packedProtocol = 0;
    ::Disassemble(packet, "dd", &characterId, &packedProtocol);

    User* user = nullptr;
    {
        CSPointer<User> found =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
        user = found.get();
    }
    if (user && user->GetSocket()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLog] By CacheD[Protocol No : RQ(%d) RQ_EX(%d)]",
        //     static_cast<std::int32_t>(packedProtocol) >> 2,
        //     packedProtocol & 0x10000U)
        // Log.Add(
        //     LOG_ERROR,
        //     "[packet history] CacheD requested a report for character DB ID %u: received protocol word 0x%08x (legacy RQ
        //     %d, RQ_EX %u). Report remains subject to the enable setting and interval gate.",
        //     static_cast<std::uint32_t>(characterId), packedProtocol,
        //     static_cast<std::int32_t>(packedProtocol) >> 2, packedProtocol & 0x10000U)
        L2_LOG_ERROR(
            Log,
            "[packet history] CacheD requested a report for character DB ID %u: received protocol word 0x%08x (legacy RQ "
            "%d, RQ_EX %u); report remains subject to the enable setting and interval gate.",
            static_cast<std::uint32_t>(characterId), packedProtocol, static_cast<std::int32_t>(packedProtocol) >> 2,
            packedProtocol & 0x10000U
        );
        user->GetSocket()->ReportPacketLog(false);
    }
    return false;
}

bool KickCharPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t accountId = 0;
    std::int32_t ignored = 0;
    ::Disassemble(packet, "dd", &accountId, &ignored);

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kick (%d)(%d)(%s)",
        //     user->GetAccountID(),
        //     user->m_nId,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[session] Disconnecting account %d, user SID %d ('%s') after a "
        //     L"database consistency failure.",
        //     user->GetAccountID(),
        //     user->m_nId,
        //     user->GetName())
        L2_LOG_ERROR(
            Log,
            u"[session] Disconnecting account %d, user SID %d ('%s') after a "
            u"database consistency failure.",
            user->GetAccountID(), user->m_nId, user->GetName()
        );
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
    }
    return false;
}

bool UseItemPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t itemServerId = 0;
    std::int64_t remainingAmount = 0;
    std::int32_t responseFlag = 0;
    ::Disassemble(packet, "ddQd", &userServerId, &itemServerId, &remainingAmount, &responseFlag);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xcb5,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    return user->OnReplyUseItemPacket(itemServerId, remainingAmount, responseFlag);
}

bool ReplySaveRecipeBookInfo(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t recipeId = 0;
    ::Disassemble(packet, "dd", &userServerId, &recipeId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3af3,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    return false;
}

bool ReplyUseRecipeItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b1b18, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t itemDatabaseId = 0;
    ::Disassemble(packet, "dd", &userServerId, &itemDatabaseId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3ae0,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    return user->OnReplyUseRecipeItem(itemDatabaseId);
}

bool ReplyDeleteRecipeItem(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t recipeId = 0;
    ::Disassemble(packet, "dd", &userServerId, &recipeId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3b06,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (recipeId < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] error. Delete recipe book info fail. "
        //     L"char dbid[%d], recipe id[%d]",
        //     "DBSocket.cpp",
        //     0x3b0a,
        //     user->GetDBID(),
        //     recipeId)
        L2_LOG_ERROR(
            Log, u"[crafting] Cannot delete recipe %d from character DB ID %d recipe book.", recipeId, user->GetDBID()
        );
    }
    return false;
}

bool ReplyLoadRecipeItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b1f0c, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t recipeCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &recipeCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3b1f,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (recipeCount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] error. Load recipe book info fail. "
        //     L"char dbid[%d]",
        //     "DBSocket.cpp",
        //     0x3b27,
        //     user->GetDBID())
        L2_LOG_ERROR(
            Log, u"[crafting] Cannot load recipe book for character DB ID %d: CacheD returned a negative count.",
            user->GetDBID()
        );
    } else if (recipeCount > 0) {
        user->InitRecipeBook();
        for (std::int32_t index = 0; index < recipeCount; ++index) {
            std::int32_t recipeId = 0;
            packet = ::Disassemble(packet, "d", &recipeId);
            user->AddRecipeBook(recipeId, true);
        }
    }
    return false;
}

bool ReplyRecipeShopMakeItem(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b7a18, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t makerServerId = 0;
    std::int32_t customerServerId = 0;
    std::int32_t recipeId = 0;
    std::int32_t result = -1;
    std::int32_t customerItemCount = 0;
    packet = ::Disassemble(packet, "ddddd", &makerServerId, &customerServerId, &recipeId, &result, &customerItemCount);

    CCreature* creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(makerServerId));
    User* const maker = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (maker == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3b5c,
        //     makerServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", makerServerId);
        return false;
    }

    static_cast<void>(maker->GetSocket());
    {
        L2_GUARD(*maker->m_lock);
        maker->m_bWaitRecipeMake = false;
    }

    creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(customerServerId));
    User* const customer = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (customer == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3b5f,
        //     customerServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", customerServerId);
        return false;
    }

    static_cast<void>(customer->GetSocket());
    const FVector customerPosition = customer->GetPos();
    const FVector makerPosition = maker->GetPos();

    if (customerItemCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid items. (%s)(%d)",
        //     customer->GetName(),
        //     customer->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[inventory] Disconnecting '%s' (%d): database item data is invalid.",
        //     customer->GetName(),
        //     customer->GetDBID())
        L2_LOG_ERROR(
            Log, u"[inventory] Disconnecting '%s' (%d): database item data is invalid.", customer->GetName(),
            customer->GetDBID()
        );
        customer->StopMove();
        if (customer->GetSocket() != nullptr) {
            customer->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        }
        return false;
    }

    char luckyEffect = 0;
    const std::uint8_t* customerCursor = ::Disassemble(packet, "c", &luckyEffect);

    const std::uint8_t* scanCursor = customerCursor;
    {
        L2_GUARD(*customer->m_lock);
        for (std::int32_t index = 0; index < customerItemCount; ++index) {
            std::int32_t manipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            std::int32_t ignoredValue = 0;
            scanCursor = ::Disassemble(
                scanCursor, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter,
                &ignoredValue
            );
        }
    }

    std::int32_t scannedMakerItemCount = 0;
    scanCursor = ::Disassemble(scanCursor, "d", &scannedMakerItemCount);
    std::int64_t makerAdenaDelta = 0;
    for (std::int32_t index = 0; index < scannedMakerItemCount; ++index) {
        std::int32_t manipulationType = 0;
        std::int32_t itemClassId = 0;
        std::int32_t itemDatabaseId = 0;
        std::int64_t amountDelta = 0;
        std::int64_t amountAfter = 0;
        std::int32_t ignoredValue = 0;
        scanCursor = ::Disassemble(
            scanCursor, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignoredValue
        );
        if (itemClassId == 0x39) {
            makerAdenaDelta = runtime::AddInt64Wrapping(makerAdenaDelta, amountDelta);
        }
    }

#if defined(_WIN32)
    const wchar_t* const customerAccountName = reinterpret_cast<const wchar_t*>(customer->m_sAccountName.data());
    const wchar_t* const makerAccountName = reinterpret_cast<const wchar_t*>(maker->m_sAccountName.data());
#else
    std::size_t customerAccountNameLength = 0;
    while (customerAccountNameLength < customer->m_sAccountName.size() &&
        customer->m_sAccountName[customerAccountNameLength] != u'\0') {
        ++customerAccountNameLength;
    }
    const std::wstring customerAccountNameStorage =
        ::Utf16ToWide(std::u16string_view(customer->m_sAccountName.data(), customerAccountNameLength));
    const wchar_t* const customerAccountName = customerAccountNameStorage.c_str();

    std::size_t makerAccountNameLength = 0;
    while (makerAccountNameLength < maker->m_sAccountName.size() && maker->m_sAccountName[makerAccountNameLength] != u'\0') {
        ++makerAccountNameLength;
    }
    const std::wstring makerAccountNameStorage =
        ::Utf16ToWide(std::u16string_view(maker->m_sAccountName.data(), makerAccountNameLength));
    const wchar_t* const makerAccountName = makerAccountNameStorage.c_str();
#endif

    {
        L2_WRITE_GUARD(customer->m_inv);
        std::int32_t customerLogState = 0;
        while (customerItemCount-- > 0) {
            std::int32_t manipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            std::int32_t ignoredValue = 0;
            customerCursor = ::Disassemble(
                customerCursor, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter,
                &ignoredValue
            );

            CItem* increaseItem = nullptr;
            if (manipulationType == 1) {
                customerLogState = 1;
                increaseItem = g_ObjectDB.CreateItem(itemClassId);
                if (increaseItem != nullptr) {
                    increaseItem->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
                    {
                        L2_GUARD(*increaseItem->m_lock);
                        if (!increaseItem->SetAmount(amountAfter)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3bba,
                            //     increaseItem->m_data->m_nCount,
                            //     amountAfter)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                increaseItem->m_data->m_nCount, amountAfter
                            );
                        }
                    }
                    if (!customer->m_inv.Push(increaseItem->m_data->index)) {
                        // ::LogIOError(
                        //     "inven push failed at file[%s], line[%d]",
                        //     "DBSocket.cpp",
                        //     0x3bc2)
                        L2_LOG_IO_ERROR(
                            "[inventory] Cannot add item DB ID %d (class %d) to crafting customer runtime ID %d inventory "
                            "(maker runtime ID %d, recipe %d); processing continues.",
                            static_cast<int>(itemDatabaseId), static_cast<int>(itemClassId),
                            static_cast<int>(customerServerId), static_cast<int>(makerServerId), static_cast<int>(recipeId)
                        );
                    }
                    customer->AutoEquipArrowOrBolt(increaseItem);
                    customer->m_inv.SetInventoryChanged(increaseItem, IUT_ADD);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3c0d,
                    //     maker->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 2) {
                customerLogState = 1;
                increaseItem = customer->m_inv.GetByDBID(itemDatabaseId);
                if (increaseItem != nullptr) {
                    {
                        L2_GUARD(*increaseItem->m_lock);
                        if (!increaseItem->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3c1b,
                            //     increaseItem->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                increaseItem->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    customer->m_inv.SetInventoryChanged(increaseItem, IUT_UPDATE);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3c63,
                    //     customer->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", customer->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        customer->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 3) {
                customerLogState = -1;
                CItem* const item = customer->m_inv.GetByDBID(itemDatabaseId);
                if (item != nullptr) {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3c71,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    customer->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            customer->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            customer->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3c8e,
                    //     customer->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", customer->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        customer->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 4) {
                customerLogState = -1;
                CItem* const item = customer->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x3cb0)
                    L2_LOG_ERROR(
                        Log, u"[crafting] Cannot delete missing item DB ID %d while applying the database reply.",
                        itemDatabaseId
                    );
                    continue;
                }
                if (itemClassId != 0x39) {
                    const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                    if (absoluteAmount < 2) {
                        customer->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                    } else {
                        customer->GetSocket()->Send(
                            "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                            absoluteAmount
                        );
                    }
                }
                if (customer->UnequipItem(item, true) != ST_NONE) {
                    customer->m_inv.SetInventoryChanged(item, IUT_DELETE);
                    customer->m_inv.Pop(item->m_data->index, false);
                    item->Delete();
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] RecipeMakeItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3cc8,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[crafting] Recipe result returned unsupported item manipulation type %d.", manipulationType
                );
            }

            if (increaseItem != nullptr) {
                const int enchant = increaseItem->m_data->GetEnchanted();
                const bool adena = manipulationType == 1 ? increaseItem->m_data->m_nClassID == 0x39 : itemClassId == 0x39;
                if (adena) {
                    customer->GetSocket()->Send("chccQ", 0x62, SystemMessageId::EARNED_ADENA, 1, 6, amountDelta);
                } else if (amountDelta > 1) {
                    maker->GetSocket()->Send(
                        "chccScdcQcQ", 0x62, SystemMessageId::CRAFTED_MULTIPLE_ITEMS_FOR_CHARACTER_FOR_ADENA, 4, 0xc,
                        customer->GetName(), 3, itemClassId, 6, amountDelta, 6, makerAdenaDelta
                    );
                    customer->GetSocket()->Send(
                        "chccScdcQcQ", 0x62, SystemMessageId::CHARACTER_CRAFTED_MULTIPLE_ITEMS_FOR_ADENA, 4, 0xc,
                        maker->GetName(), 3, itemClassId, 6, amountDelta, 6, makerAdenaDelta
                    );
                    customer->GetSocket()->Send(
                        "chccdcQ", 0x62, SystemMessageId::EARNED_MULTIPLE_ITEMS, 2, 3, itemClassId, 6, amountDelta
                    );
                } else if (enchant > 0) {
                    const std::uint8_t enchantBits = static_cast<std::uint8_t>(enchant);
                    const int signedEnchant =
                        enchantBits < 0x80U ? static_cast<int>(enchantBits) : static_cast<int>(enchantBits) - 0x100;
                    customer->GetSocket()->Send(
                        "chccScccdcQ", 0x62, SystemMessageId::SOLD_ENCHANTED_ITEM_TO_CHARACTER_FOR_ADENA, 4, 0xc,
                        maker->GetName(), 0x13, signedEnchant, 3, itemClassId, 6, makerAdenaDelta
                    );
                    maker->GetSocket()->Send(
                        "chccScccdcQ", 0x62, SystemMessageId::BOUGHT_ENCHANTED_ITEM_FROM_CHARACTER_FOR_ADENA, 4, 0xc,
                        customer->GetName(), 0x13, signedEnchant, 3, itemClassId, 6, makerAdenaDelta
                    );
                } else {
                    maker->GetSocket()->Send(
                        "chccScdcQ", 0x62, SystemMessageId::CRAFTED_ITEM_FOR_CHARACTER_FOR_ADENA, 3, 0xc,
                        customer->GetName(), 3, itemClassId, 6, makerAdenaDelta
                    );
                    customer->GetSocket()->Send(
                        "chccScdcQ", 0x62, SystemMessageId::CHARACTER_CRAFTED_ITEM_FOR_ADENA, 3, 0xc, maker->GetName(), 3,
                        itemClassId, 6, makerAdenaDelta
                    );
                    customer->GetSocket()->Send("chccd", 0x62, SystemMessageId::OBTAINED_ITEM, 1, 3, itemClassId);
                }
            }

            if (customerLogState == 1) {
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3c1),
                //     static_cast<std::int64_t>(customer->GetDBID()),
                //     static_cast<std::int64_t>(
                //         static_cast<std::int32_t>(
                //             customer->GetAccountID())),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.z)),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_Race),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_Sex),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_nClass),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     amountDelta,
                //     amountAfter,
                //     customer->GetName(),
                //     customerAccountName,
                //     static_cast<std::int64_t>(itemDatabaseId))
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3c1), static_cast<std::int64_t>(customer->GetDBID()),
                    static_cast<std::int64_t>(static_cast<std::int32_t>(customer->GetAccountID())),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.z)),
                    static_cast<std::int64_t>(customer->m_data->m_Race), static_cast<std::int64_t>(customer->m_data->m_Sex),
                    static_cast<std::int64_t>(customer->m_data->m_nClass),
                    static_cast<std::int64_t>(customer->m_data->m_nLevel), static_cast<std::int64_t>(itemClassId),
                    amountDelta, amountAfter, customer->GetName(), customerAccountName,
                    static_cast<std::int64_t>(itemDatabaseId)
                );
            } else if (customerLogState == -1) {
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3c0),
                //     static_cast<std::int64_t>(customer->GetDBID()),
                //     static_cast<std::int64_t>(
                //         static_cast<std::int32_t>(
                //             customer->GetAccountID())),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             customerPosition.z)),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_Race),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_Sex),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_nClass),
                //     static_cast<std::int64_t>(
                //         customer->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     runtime::AbsoluteInt64Wrapping(amountDelta),
                //     amountAfter,
                //     customer->GetName(),
                //     customerAccountName,
                //     static_cast<std::int64_t>(itemDatabaseId))
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3c0), static_cast<std::int64_t>(customer->GetDBID()),
                    static_cast<std::int64_t>(static_cast<std::int32_t>(customer->GetAccountID())),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(customerPosition.z)),
                    static_cast<std::int64_t>(customer->m_data->m_Race), static_cast<std::int64_t>(customer->m_data->m_Sex),
                    static_cast<std::int64_t>(customer->m_data->m_nClass),
                    static_cast<std::int64_t>(customer->m_data->m_nLevel), static_cast<std::int64_t>(itemClassId),
                    runtime::AbsoluteInt64Wrapping(amountDelta), amountAfter, customer->GetName(), customerAccountName,
                    static_cast<std::int64_t>(itemDatabaseId)
                );
            }
        }
    }

    if (luckyEffect != 0) {
        SkillUseInfo skill;
        skill.m_skillID = 0x46b7;
        skill.m_skillLevel = 1;
        skill.m_skillSubLevel = 0;
        skill.m_castRange = 0;
        skill.m_type = 0;
        skill.m_param1 = 0;
        int parameter1 = 0;
        int parameter2 = 500;
        int parameter3 = 0;
        int parameter4 = 0x5dc;
        int parameter5 = 0;
        int parameter6 = 0;
        DefaultIOBuffer* const effectPacket = customer->MakePacketForMagicSkillUse(
            parameter1, customer, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
        );
        g_ptlUserMap->BroadcastToNeighborDist(
            customer, 0xf000, customerPosition, customer->GetBroadcastDist(0x1000), effectPacket
        );
        customer->SendSystemMessage(SystemMessageId::LADY_LUCK_ACTIVATED);
    }

    std::int32_t makerItemCount = 0;
    const std::uint8_t* makerCursor = ::Disassemble(customerCursor, "d", &makerItemCount);
    {
        L2_WRITE_GUARD(maker->m_inv);
        std::int32_t makerLogState = 0;
        while (makerItemCount-- > 0) {
            std::int32_t manipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            std::int32_t ignoredValue = 0;
            makerCursor = ::Disassemble(
                makerCursor, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter,
                &ignoredValue
            );

            if (manipulationType == 1) {
                makerLogState = 1;
                CItem* const item = g_ObjectDB.CreateItem(itemClassId);
                if (item != nullptr) {
                    item->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->SetAmount(amountAfter)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3d10,
                            //     item->m_data->m_nCount,
                            //     amountAfter)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountAfter
                            );
                        }
                    }
                    if (!maker->m_inv.Push(item->m_data->index)) {
                        // ::LogIOError(
                        //     "inven push failed at file[%s], line[%d]",
                        //     "DBSocket.cpp",
                        //     0x3d18)
                        L2_LOG_IO_ERROR(
                            "[inventory] Cannot add item DB ID %d (class %d) to crafting maker runtime ID %d inventory "
                            "(customer runtime ID %d, recipe %d); processing continues.",
                            static_cast<int>(itemDatabaseId), static_cast<int>(itemClassId), static_cast<int>(makerServerId),
                            static_cast<int>(customerServerId), static_cast<int>(recipeId)
                        );
                    }
                    maker->AutoEquipArrowOrBolt(item);
                    maker->m_inv.SetInventoryChanged(item, IUT_ADD);
                    maker->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3d25,
                    //     maker->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 2) {
                makerLogState = 1;
                CItem* const item = maker->m_inv.GetByDBID(itemDatabaseId);
                if (item != nullptr) {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3d33,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    maker->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    maker->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3d3d,
                    //     maker->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 3) {
                makerLogState = -1;
                CItem* const item = maker->m_inv.GetByDBID(itemDatabaseId);
                if (item != nullptr) {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3d4a,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    maker->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    maker->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), false);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3d54,
                    //     maker->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        maker->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                }
            } else if (manipulationType == 4) {
                makerLogState = -1;
                CItem* const item = maker->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x3d62)
                    L2_LOG_ERROR(
                        Log, u"[crafting] Cannot delete missing item DB ID %d while applying the database reply.",
                        itemDatabaseId
                    );
                    continue;
                }
                maker->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), false);
                if (maker->UnequipItem(item, true) != ST_NONE) {
                    maker->m_inv.SetInventoryChanged(item, IUT_DELETE);
                    maker->m_inv.Pop(item->m_data->index, false);
                    item->Delete();
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] RecipeMakeItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3d7b,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[crafting] Recipe result returned unsupported item manipulation type %d.", manipulationType
                );
            }

            if (makerLogState == 1) {
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3c1),
                //     static_cast<std::int64_t>(maker->GetDBID()),
                //     static_cast<std::int64_t>(
                //         static_cast<std::int32_t>(
                //             maker->GetAccountID())),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.z)),
                //     static_cast<std::int64_t>(maker->m_data->m_Race),
                //     static_cast<std::int64_t>(maker->m_data->m_Sex),
                //     static_cast<std::int64_t>(maker->m_data->m_nClass),
                //     static_cast<std::int64_t>(maker->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     amountDelta,
                //     amountAfter,
                //     maker->GetName(),
                //     makerAccountName,
                //     static_cast<std::int64_t>(itemDatabaseId))
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3c1), static_cast<std::int64_t>(maker->GetDBID()),
                    static_cast<std::int64_t>(static_cast<std::int32_t>(maker->GetAccountID())),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.z)),
                    static_cast<std::int64_t>(maker->m_data->m_Race), static_cast<std::int64_t>(maker->m_data->m_Sex),
                    static_cast<std::int64_t>(maker->m_data->m_nClass), static_cast<std::int64_t>(maker->m_data->m_nLevel),
                    static_cast<std::int64_t>(itemClassId), amountDelta, amountAfter, maker->GetName(), makerAccountName,
                    static_cast<std::int64_t>(itemDatabaseId)
                );
            } else if (makerLogState == -1) {
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                //     static_cast<std::int64_t>(0x3c0),
                //     static_cast<std::int64_t>(maker->GetDBID()),
                //     static_cast<std::int64_t>(
                //         static_cast<std::int32_t>(
                //             maker->GetAccountID())),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             makerPosition.z)),
                //     static_cast<std::int64_t>(maker->m_data->m_Race),
                //     static_cast<std::int64_t>(maker->m_data->m_Sex),
                //     static_cast<std::int64_t>(maker->m_data->m_nClass),
                //     static_cast<std::int64_t>(maker->m_data->m_nLevel),
                //     static_cast<std::int64_t>(itemClassId),
                //     runtime::AbsoluteInt64Wrapping(amountDelta),
                //     amountAfter,
                //     maker->GetName(),
                //     makerAccountName,
                //     static_cast<std::int64_t>(itemDatabaseId))
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%s,%s,,,%I64d",
                    static_cast<std::int64_t>(0x3c0), static_cast<std::int64_t>(maker->GetDBID()),
                    static_cast<std::int64_t>(static_cast<std::int32_t>(maker->GetAccountID())),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(makerPosition.z)),
                    static_cast<std::int64_t>(maker->m_data->m_Race), static_cast<std::int64_t>(maker->m_data->m_Sex),
                    static_cast<std::int64_t>(maker->m_data->m_nClass), static_cast<std::int64_t>(maker->m_data->m_nLevel),
                    static_cast<std::int64_t>(itemClassId), runtime::AbsoluteInt64Wrapping(amountDelta), amountAfter,
                    maker->GetName(), makerAccountName, static_cast<std::int64_t>(itemDatabaseId)
                );
            }
        }
    }

    if (customer->IsNowTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]recipe shop make & trade try.",
        //     "DBSocket.cpp",
        //     0x3d99)
        // L2_LOG_ERROR(Log,
        //     L"[crafting] Customer '%s' (%d) remained in trade after recipe %d reply. Cancelling the trade.",
        //     customer->GetName(),
        //     customer->GetDBID(),
        //     recipeId)
        L2_LOG_ERROR(
            Log, u"[crafting] Customer '%s' (%d) remained in trade after recipe %d reply; cancelling the trade.",
            customer->GetName(), customer->GetDBID(), recipeId
        );
        customer->TradeCancel(false);
    }
    customer->SendItemList(true, true, nullptr);
    maker->SendItemList(true, true, nullptr);
    maker->RecipeStoreShowInfo(customer, recipeId, result);
    return false;
}

bool ReplyEquipHenna(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b20bc, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t hennaId = 0;
    std::int32_t manipulationCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &hennaId, &manipulationCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3dc5,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (manipulationCount < 1) {
        return false;
    }

    {
        L2_WRITE_GUARD(user->m_inv);

        std::int32_t manipulationType = 0;
        std::int32_t itemClassId = 0;
        std::int32_t itemDatabaseId = 0;
        std::int64_t amountDelta = 0;
        std::int64_t amountAfter = 0;
        std::int32_t ignoredItemValue = 0;
        packet = ::Disassemble(
            packet, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignoredItemValue
        );

        if (manipulationType != -1) {
            if (manipulationType == 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3ddc)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 2) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3de1)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 3) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3e0a,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                } else {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3ded,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                }
            } else if (manipulationType == 4) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x3e2a)
                    L2_LOG_ERROR(
                        Log, u"[henna] Cannot delete missing item DB ID %d while applying the database reply.",
                        itemDatabaseId
                    );
                } else {
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                    if (user->UnequipItem(item, true) != ST_NONE) {
                        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                        user->m_inv.Pop(item->m_data->index, false);
                        item->Delete();
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] EquipHennaItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3e49,
                //     manipulationType)
                L2_LOG_ERROR(Log, u"[henna] Equip reply returned unsupported item manipulation type %d.", manipulationType);
            }
        }

        manipulationType = 0;
        itemClassId = 0;
        itemDatabaseId = 0;
        amountDelta = 0;
        amountAfter = 0;
        ignoredItemValue = 0;
        packet = ::Disassemble(
            packet, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignoredItemValue
        );

        if (manipulationType != -1) {
            if (manipulationType == 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3e5a)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 2) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3e5f)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 3) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3e87,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                } else {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3e6a,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                }
            } else if (manipulationType == 4) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x3ea7)
                    L2_LOG_ERROR(
                        Log, u"[henna] Cannot delete missing item DB ID %d while applying the database reply.",
                        itemDatabaseId
                    );
                } else {
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                    if (user->UnequipItem(item, true) != ST_NONE) {
                        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                        user->m_inv.Pop(item->m_data->index, false);
                        item->Delete();
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] EquipHennaItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3ec6,
                //     manipulationType)
                L2_LOG_ERROR(Log, u"[henna] Equip reply returned unsupported item manipulation type %d.", manipulationType);
            }
        }
    }

    std::int32_t periodic = 0;
    std::int32_t expireTime = 0;
    ::Disassemble(packet, "dd", &periodic, &expireTime);
    user->AddHennaFromBD(hennaId, periodic == 1, expireTime);
    user->AcquireHennaSkill(hennaId);
    user->GetSocket()->SendSystemMessage(SystemMessageId::SYMBOL_ADDED);
    user->ValidateHenna();
    user->SendItemList(true, true, nullptr);
    user->ValidateChanged(
        std::int64_t{VB_BASEDATA | VB_BASEATTR | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED}
    );
    return false;
}

bool ReplyUnequipHenna(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b2a4c, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t hennaId = 0;
    std::int32_t manipulationCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &hennaId, &manipulationCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3f02,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (manipulationCount < 1) {
        return false;
    }

    {
        L2_WRITE_GUARD(user->m_inv);

        std::int32_t manipulationType = 0;
        std::int32_t itemClassId = 0;
        std::int32_t itemDatabaseId = 0;
        std::int64_t amountDelta = 0;
        std::int64_t amountAfter = 0;
        std::int32_t ignoredItemValue = 0;
        packet = ::Disassemble(
            packet, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignoredItemValue
        );

        if (manipulationType != -1) {
            if (manipulationType == 1) {
                CItem* const item = g_ObjectDB.CreateItem(itemClassId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3f33,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                } else {
                    item->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->SetAmount(amountAfter)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3f20,
                            //     item->m_data->m_nCount,
                            //     amountAfter)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountAfter
                            );
                        }
                    }
                    if (!user->m_inv.Push(item->m_data->index)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "inven push failed at file[%s], line[%d]",
                        //     "DBSocket.cpp",
                        //     0x3f26)
                        L2_LOG_ERROR(
                            Log,
                            "[inventory] Item %u could not be inserted while applying a database manipulation reply for "
                            "character %u.",
                            item->GetDBID(), user->GetDBID()
                        );
                    }
                    user->AutoEquipArrowOrBolt(item);
                    user->m_inv.SetInventoryChanged(item, IUT_ADD);
                    user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                }
            } else if (manipulationType == 2) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3f4a,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                } else {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3f40,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                }
            } else if (manipulationType == 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3f50)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 4) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3f55)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] UnequipHennaItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3f60,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[henna] Unequip reply returned unsupported item manipulation type %d.", manipulationType
                );
            }
        }

        manipulationType = 0;
        itemClassId = 0;
        itemDatabaseId = 0;
        amountDelta = 0;
        amountAfter = 0;
        ignoredItemValue = 0;
        packet = ::Disassemble(
            packet, "dddQQd", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter, &ignoredItemValue
        );

        if (manipulationType != -1) {
            if (manipulationType == 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3f71)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 2) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] Error, Check code. ",
                //     "DBSocket.cpp",
                //     0x3f76)
                L2_LOG_ERROR(Log, u"[henna] Unexpected item manipulation branch; reply processing continues.");
            } else if (manipulationType == 3) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x3fa1,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                } else {
                    {
                        L2_GUARD(*item->m_lock);
                        if (!item->AddAmount(amountDelta)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d]",
                            //     "DBSocket.cpp",
                            //     0x3f84,
                            //     item->m_data->m_nCount,
                            //     amountDelta)
                            L2_LOG_ERROR(
                                Log,
                                u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                                item->m_data->m_nCount, amountDelta
                            );
                        }
                    }
                    user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                }
            } else if (manipulationType == 4) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Delete item failed.",
                    //     L"DBSocket.cpp",
                    //     0x3fc1)
                    L2_LOG_ERROR(
                        Log, u"[henna] Cannot delete missing item DB ID %d while applying the database reply.",
                        itemDatabaseId
                    );
                } else {
                    if (itemClassId != 0x39) {
                        const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                        if (absoluteAmount < 2) {
                            user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                        } else {
                            user->GetSocket()->Send(
                                "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6,
                                absoluteAmount
                            );
                        }
                    }
                    if (user->UnequipItem(item, true) != ST_NONE) {
                        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                        user->m_inv.Pop(item->m_data->index, false);
                        item->Delete();
                    }
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] UnequipHennaItem item fail. "
                //     L"unknown manipulate type[%d]",
                //     "DBSocket.cpp",
                //     0x3fe0,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[henna] Unequip reply returned unsupported item manipulation type %d.", manipulationType
                );
            }
        }
    }

    std::int32_t periodic = 0;
    ::Disassemble(packet, "d", &periodic);
    user->DelHennaFromBD(hennaId, periodic == 1);
    user->AcquireUnEquipHennaSkill(hennaId);
    user->DeleteAcquireHennaSkill(hennaId);
    user->GetSocket()->SendSystemMessage(SystemMessageId::SYMBOL_REMOVED);
    user->ValidateHenna();
    user->ValidateChanged(
        std::int64_t{VB_BASEDATA | VB_BASEATTR | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED}
    );
    user->SendItemList(true, true, nullptr);
    return false;
}

bool PingNetwork(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    const std::uint32_t currentTick = runtime::GetTickCount();
    std::int32_t userServerId = 0;
    std::uint32_t requestTick = 0;
    std::int32_t cacheThreadWait = 0;
    ::Disassemble(packet, "ddd", &userServerId, &requestTick, &cacheThreadWait);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2764,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    char16_t systemContext[] = u"SYS";
    char16_t message[0x5000]{};
    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"Main to Cache network Ping [%d]millisec",
        static_cast<std::int32_t>(currentTick - requestTick)
    );
    user->SendSystemMessage(systemContext, message);

    std::memset(message, 0, sizeof(message));
    FormatUtf16To(message, sizeof(message) / sizeof(message[0]), u"Cache thread wait [%d]millisec", cacheThreadWait);
    user->SendSystemMessage(systemContext, message);
    return false;
}

bool PingTest(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    packet = ::Disassemble(packet, "d", &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2665,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());

    std::int32_t cachedBeginTick = 0;
    std::int32_t packetCreateElapsed = 0;
    std::int32_t saveUserElapsed = 0;
    std::int32_t saveItemElapsed = 0;
    std::int32_t cachedServerTime = 0;
    ::Disassemble(
        packet, "ddddd", &cachedBeginTick, &packetCreateElapsed, &saveUserElapsed, &saveItemElapsed, &cachedServerTime
    );

    char16_t systemContext[] = u"SYS";
    char16_t message[0x5000]{};
    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]),
        u"%d Ping (CPacket::OnIOCallback - new CPacket) "
        u"time [%d]milliseconds",
        1, packetCreateElapsed
    );
    user->SendSystemMessage(systemContext, message);

    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"%d Ping Save user elapsed [%d]milliseconds ", 2, saveUserElapsed
    );
    user->SendSystemMessage(systemContext, message);

    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"%d Ping Save item elapsed [%d]milliseconds ", 3, saveItemElapsed
    );
    user->SendSystemMessage(systemContext, message);

    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"%d Ping Cached->Server elapsed [%d]secs", 4,
        static_cast<std::int32_t>(std::time(nullptr)) - cachedServerTime
    );
    user->SendSystemMessage(systemContext, message);

    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"%d Ping Test elapsed [%d]milliseconds", 5,
        static_cast<std::int32_t>(runtime::GetTickCount() - static_cast<std::uint32_t>(cachedBeginTick))
    );
    user->SendSystemMessage(systemContext, message);
    return false;
}

bool ReplyShowIntervalAnnounce(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b0130, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t replacePublishedState = 0;
    std::int32_t announceCount = 0;
    packet = ::Disassemble(packet, "ddd", &userServerId, &replacePublishedState, &announceCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x35f1,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (replacePublishedState != 0) {
        CAnnounceTimer::Inst()->ClearIntervalMsg();
    }

    for (std::int32_t index = 0; index < announceCount; ++index) {
        std::int32_t interval = 0;
        std::int32_t announceId = 0;
        char16_t announce[0x100]{};
        packet = ::Disassemble(packet, "ddS", &interval, &announceId, 0x200, announce);

        const std::wstring hostAnnounce = ::FixedUtf16ToWide(announce);
        char16_t message[0x100]{};
        FormatUtf16To(
            message, sizeof(message) / sizeof(message[0]), u"[%d][%d]%ls", interval, announceId, hostAnnounce.c_str()
        );
        char16_t systemContext[] = u"SYS";
        userSocket->SendSystemMessage(systemContext, message);

        if (replacePublishedState != 0) {
            CAnnounceTimer::Inst()->SetIntervalMsg(interval, announceId, announce);
        }
    }
    return false;
}

bool ReplySetIntervalAnnounce(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    packet = ::Disassemble(packet, "d", &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x361b,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    std::int32_t interval = 0;
    std::int32_t announceId = 0;
    char16_t announce[0x100]{};
    ::Disassemble(packet, "ddS", &interval, &announceId, 0x200, announce);

    const std::wstring hostAnnounce = ::FixedUtf16ToWide(announce);
    char16_t message[0x100]{};
    FormatUtf16To(
        message, sizeof(message) / sizeof(message[0]), u"set [%d][%d]%ls", interval, announceId, hostAnnounce.c_str()
    );
    char16_t systemContext[] = u"SYS";
    userSocket->SendSystemMessage(systemContext, message);
    return false;
}

bool ReplyDelIntervalAnnounce(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b0590, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    packet = ::Disassemble(packet, "d", &userServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3636,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    std::int32_t interval = 0;
    std::int32_t announceId = 0;
    ::Disassemble(packet, "dd", &interval, &announceId);

    char16_t message[0x100]{};
    FormatUtf16To(message, sizeof(message) / sizeof(message[0]), u"delete [%d][%d]", interval, announceId);
    char16_t systemContext[] = u"SYS";
    userSocket->SendSystemMessage(systemContext, message);

    char16_t emptyMessage[]{u'\0'};
    CAnnounceTimer::Inst()->SetIntervalMsg(interval, announceId, emptyMessage);
    return false;
}

bool ReplyLoadIntervalAnnounce(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b0920, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t announceCount = 0;
    packet = ::Disassemble(packet, "d", &announceCount);
    CAnnounceTimer::Inst()->ClearIntervalMsg();

    for (std::int32_t index = 0; index < announceCount; ++index) {
        std::int32_t interval = 0;
        std::int32_t announceId = 0;
        char16_t announce[0x100]{};
        packet = ::Disassemble(packet, "ddS", &interval, &announceId, 0x200, announce);
        CAnnounceTimer::Inst()->SetIntervalMsg(interval, announceId, announce);
    }
    return false;
}

bool ReplyManageBattleCamp(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a9588, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t command = 0;
    packet = ::Disassemble(packet, "d", &command);

    if (command == 0) {
        std::int32_t battleCampDatabaseId = 0;
        packet = ::Disassemble(packet, "d", &battleCampDatabaseId);
        if (battleCampDatabaseId == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "MBC_INSTALL_BATTLE_CAMP BattleCampDBID[%d]",
            //     battleCampDatabaseId)
            L2_LOG_ERROR(Log, "[battle camp] Install reply reported battle-camp DB ID %d.", battleCampDatabaseId);
            return false;
        }

        std::int32_t pledgeDatabaseId = 0;
        std::int32_t pledgeWorldId = 0;
        std::int32_t residenceId = 0;
        std::int32_t maximumHp = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t baseType = 0;
        ::Disassemble(
            packet, "dddddddd", &pledgeDatabaseId, &pledgeWorldId, &residenceId, &maximumHp, &x, &y, &z, &baseType
        );

        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(pledgeWorldId, pledgeDatabaseId);
        if (residence != nullptr && pledge) {
            CBattleCamp* const battleCamp = new CBattleCamp(
                static_cast<std::uint32_t>(battleCampDatabaseId), residence, pledge.get(),
                FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)), maximumHp, maximumHp,
                baseType
            );
            pledge->SetBattleCamp(battleCamp);
            CBattleCampDb::inst().AddBattleCamp(baseType == 1 ? u"advanced_headquarter" : u"headquarter", battleCamp);
            L2_RELEASE(battleCamp, ObjectRefType::CreateDelete);
        }
        return false;
    }

    if (command == 1) {
        std::int32_t ignored = 0;
        std::int32_t pledgeWorldId = 0;
        std::int32_t battleCampDatabaseId = 0;
        ::Disassemble(packet, "ddd", &ignored, &pledgeWorldId, &battleCampDatabaseId);
        if (battleCampDatabaseId == 0) {
            return false;
        }

        CSPointer<CBattleCamp> battleCamp =
            CBattleCampDb::inst().GetBattleCamp(static_cast<std::uint32_t>(battleCampDatabaseId));
        if (battleCamp) {
            CSPointer<CPledge> pledge =
                CPledge::FindPledge(pledgeWorldId, static_cast<int>(battleCamp->GetOwningPledgeID()));
            if (pledge) {
                pledge->SetBattleCamp(nullptr);
            }
            CBattleCampDb::inst().RemoveBattleCamp(static_cast<std::uint32_t>(battleCampDatabaseId));
            battleCamp->Delete();
        }
        return false;
    }

    if (command == 2) {
        std::int32_t ignored = 0;
        std::int32_t battleCampCount = 0;
        packet = ::Disassemble(packet, "dd", &ignored, &battleCampCount);
        for (std::int32_t index = 0; index < battleCampCount; ++index) {
            std::int32_t battleCampDatabaseId = 0;
            packet = ::Disassemble(packet, "d", &battleCampDatabaseId);
            CSPointer<CBattleCamp> battleCamp =
                CBattleCampDb::inst().GetBattleCamp(static_cast<std::uint32_t>(battleCampDatabaseId));
            if (!battleCamp) {
                continue;
            }

            CSPointer<CPledge> pledge = CPledge::FindPledge(
                static_cast<int>(battleCamp->GetOwningPledgeWorldID()), static_cast<int>(battleCamp->GetOwningPledgeID())
            );
            if (pledge) {
                pledge->SetBattleCamp(nullptr);
            }
            CBattleCampDb::inst().RemoveBattleCamp(static_cast<std::uint32_t>(battleCampDatabaseId));
            battleCamp->Delete();
        }
    }
    return false;
}

bool ReplyLoadBattleCamp(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a9a74, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t battleCampCount = 0;
    packet = ::Disassemble(packet, "d", &battleCampCount);
    for (std::int32_t index = 0; index < battleCampCount; ++index) {
        std::int32_t battleCampDatabaseId = 0;
        std::int32_t pledgeDatabaseId = 0;
        std::int32_t residenceId = 0;
        std::int32_t maximumHp = 0;
        std::int32_t hp = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t baseType = 0;
        packet = ::Disassemble(
            packet, "ddddddddd", &battleCampDatabaseId, &pledgeDatabaseId, &residenceId, &maximumHp, &hp, &x, &y, &z,
            &baseType
        );

        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        CSPointer<CPledge> pledge = CPledge::FindPledge(runtime::process::CurrentDatabaseId(), pledgeDatabaseId);
        if (residence == nullptr || !pledge) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"ReplyLoadBattleCamp : invalid battle camp data[%d] in [%d]",
            //     battleCampDatabaseId,
            //     residenceId)
            L2_LOG_ERROR(
                Log,
                u"[battle camp] Loaded camp DB ID %d has invalid data for "
                u"residence %d; entry rejected.",
                battleCampDatabaseId, residenceId
            );
            continue;
        }

        CBattleCamp* const battleCamp = new CBattleCamp(
            static_cast<std::uint32_t>(battleCampDatabaseId), residence, pledge.get(),
            FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)), maximumHp, hp, baseType
        );
        if (hp < 1) {
            battleCamp->m_data->m_bIsActive = false;
        }
        if (residence->IsCastle()) {
            const int castleId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
            if (pledge->GetCurrentCastleSiegeStatus(castleId) == 2) {
                battleCamp->m_data->m_bIsActive = false;
            }
        }

        CBattleCampDb::inst().AddBattleCamp(baseType == 1 ? u"advanced_headquarter" : u"headquarter", battleCamp);
        pledge->SetBattleCamp(battleCamp);
        L2_RELEASE(battleCamp, ObjectRefType::CreateDelete);
    }
    return false;
}

bool ReplyLoadCastleGuard(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c2464, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t guardCount = 0;
    packet = ::Disassemble(packet, "dd", &residenceId, &guardCount);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        return false;
    }

    for (std::int32_t index = 0; index < guardCount; ++index) {
        std::int32_t itemClassId = 0;
        std::uint16_t guardTypeValue = 0;
        std::uint16_t moveableValue = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t direction = 0;
        std::int32_t npcId = 0;
        packet =
            ::Disassemble(packet, "dhhddddd", &itemClassId, &guardTypeValue, &moveableValue, &x, &y, &z, &direction, &npcId);

        CItem* const item = g_ObjectDB.CreateItem(itemClassId);
        if (item == nullptr) {
            continue;
        }

        if (!item->SetAmount(1)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%I64d][%d]",
            //     "DBSocket.cpp",
            //     0x349e,
            //     item->m_data->m_nCount,
            //     1)
            L2_LOG_ERROR(
                Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                item->m_data->m_nCount, 1
            );
        }
        item->m_data->SetDirByInt(direction);

        std::optional<FVector> positionStorage;
        {
            L2_WRITE_GUARD(*residence);
            FAngle angle{};
            angle.Yaw = direction;
            ResidenceGuardData* const guardData = new ResidenceGuardData(
                static_cast<std::uint32_t>(itemClassId), static_cast<ResidenceGuardTypeEnum>(guardTypeValue),
                moveableValue != 0, angle, static_cast<std::uint32_t>(npcId), item->Id()
            );

            const FVector& position =
                positionStorage.emplace(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
            residence->m_mapResidenceGuardData.emplace(position, guardData);
            residence->m_mapResidenceGuardDataCheck.emplace(position, static_cast<ResidenceGuardTypeEnum>(guardTypeValue));

            const auto guardCountEntry = residence->m_GuardNumMap.find(itemClassId);
            if (guardCountEntry != residence->m_GuardNumMap.end()) {
                ++guardCountEntry->second;
            }
        }

        CWorld::Inst().PutItem(item, *positionStorage, nullptr);
    }
    return false;
}

bool ReplyLoadMercenary(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b948c, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t mercenaryCount = 0;
    packet = ::Disassemble(packet, "dd", &residenceId, &mercenaryCount);

    for (std::int32_t index = 0; index < mercenaryCount; ++index) {
        std::int32_t mercenaryKey = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t direction = 0;
        std::int32_t npcId = 0;
        std::int32_t hp = 0;
        std::int32_t mp = 0;
        packet = ::Disassemble(packet, "dddddddd", &mercenaryKey, &x, &y, &z, &direction, &npcId, &hp, &mp);

        if (hp <= 0) {
            continue;
        }

        const std::int32_t npcClassId = runtime::AddInt32Wrapping(npcId, 1'000'000);
        if (npcClassId < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "invalid npc id at file[%s], line[%d]",
            //     "DBSocket.cpp",
            //     0x3522)
            L2_LOG_ERROR(Log, "[mercenary] NPC ID %d overflows the shared class-ID range; entry rejected.", npcId);
            continue;
        }

        CNPC* const npc = g_ObjectDB.CreateNPC(npcClassId, true);
        if (npc == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "There is no NPC id at file[%s], line[%d]",
            //     "DBSocket.cpp",
            //     0x352a)
            L2_LOG_ERROR(Log, "[mercenary] NPC class %d could not be created; entry rejected.", npcClassId);
            continue;
        }

        const wchar_t* const aiName = g_ObjectDB.GetAI(npcClassId);
        if (aiName == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "AI does not specified [clsid=%d]",
            //     npc->m_nClassId)
            L2_LOG_ERROR(Log, "[mercenary] NPC class %d has no AI name; spawn rejected.", npc->m_nClassId);
            npc->Delete();
            continue;
        }

        std::array<char, 0x2000> buffer;
        int bufferLength = 0;
        int parameterCount = 0;
        for (const auto& parameter : npc->m_pClassConst->m_mapParameter) {
            const int assembled = ::Assemble(
                buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "SS", parameter.first.c_str(),
                parameter.second.c_str()
            );
            ++parameterCount;
            if (assembled > 0) {
                bufferLength += assembled;
            }
        }

        npc->m_data->m_SummonType = NST_NOTHING;
        npc->m_data->m_BossID = 0;
        npc->m_data->m_nMasterExpRatio = 0;
        npc->m_nMercenaryKey = static_cast<std::uint32_t>(mercenaryKey);
        npc->m_data->m_nResidenceID = residenceId;

        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        npc->m_data->m_nResidenceOwnerID =
            residence != nullptr && residence->m_data != nullptr ? residence->m_data->m_nResidenceOwnerID : 0;

        NpcServer::NPCCreateParam parameter{};
        parameter.pNPC = npc;
        parameter.pDefine = nullptr;
        parameter.makerId = 0;
        parameter.pos = FVector(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
        parameter.dir = direction;
        parameter.isMercenary = 1;
        parameter.hp = hp;
        parameter.mp = mp;
        parameter.aiName = const_cast<wchar_t*>(aiName);
        parameter.paramCount = parameterCount;
        parameter.bufLen = bufferLength;
        parameter.pBuf = buffer.data();
        parameter.airshipSID = 0;
        parameter.inzoneSID = 0;
        g_npcserver.SendNPCCreatePacket(parameter);
        npc->OnCreated();
    }
    return false;
}

bool ReplySaveMercenary(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b9870, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t creatureIndex = 0;
    std::uint32_t mercenaryKey = 0;
    ::Disassemble(packet, "dd", &creatureIndex, &mercenaryKey);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(creatureIndex);
    if (creature && creature->IsCreature()) {
        creature->m_nMercenaryKey = mercenaryKey;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Mercenary creature index mismatch!!!")
        L2_LOG_ERROR(
            Log,
            u"[mercenary] Creature index does not match the database reply; "
            u"operation rejected."
        );
    }
    return false;
}

bool ReplyDxPacket(CDBSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::uint16_t protocol = 0;
    packet = ::Disassemble(packet, "h", &protocol);
    DxPacketCallback callback = &DummyDXPacket;
    if (protocol < DxPacketCallbackMap::SIZE) {
        callback = g_DxPacketTable.GetCallback(protocol);
    }
    if (callback == DummyDXPacket) {
        // Log.Add(LOG_ERROR,
        //     "[database protocol] Ignoring DX reply %u: %s (supported IDs 0..115).",
        //     static_cast<unsigned int>(protocol),
        //     protocol < 116 ? "no handler is registered" : "protocol ID is outside the supported range")
        L2_LOG_ERROR(
            Log, "[database protocol] Ignoring DX reply %u: %s (supported IDs 0..115).", static_cast<unsigned int>(protocol),
            protocol < 116 ? "no handler is registered" : "protocol ID is outside the supported range"
        );
    }
    return callback(socket, packet);
}

bool ReplyQueryPacket(CDBSocket* socket, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::uint16_t protocol = 0;
    packet = ::Disassemble(packet, "h", &protocol);
    QueryPacketCallback callback = &DummyQueryPacket;
    if (protocol < QueryPacketCallbackMap::SIZE) {
        callback = g_QueryPacketTable.GetCallback(protocol);
    }
    if (callback == DummyQueryPacket) {
        // Log.Add(LOG_ERROR,
        //     "[database protocol] Ignoring query reply %u: %s (supported IDs 0..3).",
        //     static_cast<unsigned int>(protocol),
        //     protocol < 4 ? "no handler is registered" : "protocol ID is outside the supported range")
        L2_LOG_ERROR(
            Log, "[database protocol] Ignoring query reply %u: %s (supported IDs 0..3).",
            static_cast<unsigned int>(protocol),
            protocol < 4 ? "no handler is registered" : "protocol ID is outside the supported range"
        );
    }
    return callback(socket, packet);
}

namespace DBPacketHandler {

bool ReplyLoadFriendList(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t friendCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &friendCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x13e9,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    std::vector<FriendInfo, std::allocator<FriendInfo>> friendInfos;
    for (std::int32_t index = 0; index < friendCount; ++index) {
        FriendInfo friendInfo{};
        std::array<char16_t, 28> friendName{};
        std::array<char16_t, 56> friendMemo{};
        packet = ::Disassemble(
            packet, "dShhS", &friendInfo.nCharDBID, 0x30, friendName.data(), &friendInfo.nLevel, &friendInfo.nClass, 0x66,
            friendMemo.data()
        );
        friendInfo.wName = std::u16string(friendName.data());
        friendInfo.szFriendMemo = std::u16string(friendMemo.data());
        friendInfos.push_back(friendInfo);
    }

    CFriends* const friends = new CFriends(friendInfos);
    user->m_osFriends.SetObject(friends);
    user->CheckAbnormalVisualEffect();
    L2_RELEASE(friends, ObjectRefType::CreateDelete);
    user->SendLogonMsgToFriend();
    user->OnRequestL2FriendList();
    return false;
}

} // namespace DBPacketHandler

bool ReplyGetCharDBIDByName(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    char16_t targetName[0x18]{};
    std::int32_t requesterId = 0;
    char16_t petitionMessage[0x1f5]{};
    ::Disassemble(packet, "dSdS", &characterId, 0x30, targetName, &requesterId, 0x3ea, petitionMessage);

    if (characterId < 1) {
        const std::wstring hostTargetName = ::FixedUtf16ToWide(targetName);
        char16_t message[0x38]{};
        FormatUtf16To(message, sizeof(message) / sizeof(message[0]), u"Cannot find user. [%ls]", hostTargetName.c_str());

        CSPointer<User> user =
            g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), requesterId));
        if (user) {
            char16_t petitionContext[] = u"Peti";
            user->SendSystemMessage(petitionContext, message);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d], %s",
        //     "DBSocket.cpp",
        //     0x36bc,
        //     message)
        L2_LOG_ERROR(Log, u"[petition] Character lookup failed: %s.", message);
    }
    return false;
}

bool ReplyMngComment(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t writeComment = 0;
    std::int32_t userServerId = 0;
    std::int32_t writerCharacterId = 0;
    std::int32_t commentCount = 0;
    char16_t targetName[0x19]{};
    packet =
        ::Disassemble(packet, "ddddS", &writeComment, &userServerId, &writerCharacterId, &commentCount, 0x32, targetName);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x37d7,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    const std::u16string targetNameText(::FixedUtf16View(targetName));
    char16_t commentContext[] = u"COMMENT";
    socket->SendSystemMessage(commentContext, targetNameText);

    for (std::int32_t index = 0; index < commentCount; ++index) {
        char16_t comment[0x100]{};
        char16_t writerName[0x19]{};
        packet = ::Disassemble(packet, "SS", 0x200, comment, 0x32, writerName);
        const std::wstring hostComment = ::FixedUtf16ToWide(comment);
        const std::wstring hostWriterName = ::FixedUtf16ToWide(writerName);
        char16_t message[0x200]{};
        FormatUtf16To(
            message, sizeof(message) / sizeof(message[0]), u"[%ls]%ls", hostWriterName.c_str(), hostComment.c_str()
        );
        char16_t systemContext[] = u"SYS";
        socket->SendSystemMessage(systemContext, message);
    }
    return false;
}

bool ReplyLoadTeamBattleAgit(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t residenceId = 0;
    std::int32_t subStatus = 0;
    std::int32_t pledgeCount = 0;
    packet = ::Disassemble(packet, "ddd", &residenceId, &subStatus, &pledgeCount);

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CTeamBattleAgit* const agit =
        residence && residence->IsWorldAgit() ? residence->CastWorldAgit()->CastTeamBattleAgit() : nullptr;
    if (!agit) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"not team battle agit[%d]",
        //     residenceId)
        L2_LOG_ERROR(
            Log,
            u"[team battle] Residence %d is not a team-battle clan hall; "
            u"request rejected.",
            residenceId
        );
        return false;
    }

    agit->SetSubStatus(static_cast<TEAM_BATTLE_SUBSTATUS>(subStatus));
    for (std::int32_t index = 0; index < pledgeCount; ++index) {
        std::int32_t pledgeDatabaseId = 0;
        std::int32_t proposeTime = 0;
        std::int32_t color = 0;
        std::int32_t npcType = 0;
        packet = ::Disassemble(packet, "dddd", &pledgeDatabaseId, &proposeTime, &color, &npcType);
        agit->AddEntryPledge(
            static_cast<std::uint32_t>(pledgeDatabaseId), proposeTime, static_cast<TEAM_BATTLE_COLOR>(color), npcType
        );

        const int databaseId = runtime::process::CurrentDatabaseId();
        CSPointer<CPledge> pledge = CPledge::FindPledge(databaseId, pledgeDatabaseId);
        if (!pledge) {
            CPledge::ReserveLoaded(databaseId, pledgeDatabaseId, PRLT_TEAMBATTLEAGIT);
        }
    }

    std::int32_t memberCount = 0;
    packet = ::Disassemble(packet, "d", &memberCount);
    for (std::int32_t index = 0; index < memberCount; ++index) {
        std::int32_t userDatabaseId = 0;
        std::int32_t pledgeDatabaseId = 0;
        std::int32_t proposeTime = 0;
        packet = ::Disassemble(packet, "ddd", &userDatabaseId, &pledgeDatabaseId, &proposeTime);
        const int result = agit->AddEntryPledgeMember(
            static_cast<std::uint32_t>(userDatabaseId), static_cast<std::uint32_t>(pledgeDatabaseId), proposeTime
        );
        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Error[%d] in Add Entry Member[%d][%d]",
            //     result,
            //     userDatabaseId,
            //     pledgeDatabaseId)
            L2_LOG_ERROR(
                Log, u"[team battle] Adding member %d to pledge %d failed with result %d.", userDatabaseId, pledgeDatabaseId,
                result
            );
        }
    }

    agit->StartTimer();
    if (g_JobFinishSignal.SetAgitLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_AGIT);
    }
    return false;
}

bool ReplyNewTeamBattleAgitPledge(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    std::int32_t color = 0;
    std::int32_t npcType = 0;
    std::int32_t proposeTime = 0;
    ::Disassemble(packet, "ddddd", &context, &result, &color, &npcType, &proposeTime);

    CTeamBattleAgit* agit = nullptr;
    CPledge* pledge = nullptr;
    User* user = nullptr;
    CNPC* npc = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "pppp", &agit, &pledge, &user, &npc)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x391d,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result != 0) {
        agit->AddEntryPledge(pledge->GetDbId(), proposeTime, static_cast<TEAM_BATTLE_COLOR>(color), npcType);
        agit->AddEntryPledgeMember(pledge->GetMasterDbId(), pledge->GetDbId(), proposeTime);
        if (agit->GetBattleRoyalEntryCount() > 4) {
            agit->BroadcastSystemMessageW(SystemMessageId::PRELIMINARY_MATCH_REGISTRATION_CLOSED);
        }
    }

    if (npc) {
        g_npcserver.Send("cdddd", 0x3f, npc->m_data->index, user->m_data->index, 1, result != 0 ? color : -1);
        L2_RELEASE(npc, ObjectRefType::Cached);
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(pledge, ObjectRefType::Cached);
    L2_RELEASE(user, ObjectRefType::Cached);
    return false;
}

bool ReplyNewTeamBattleAgitMember(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    std::int32_t proposeTime = 0;
    ::Disassemble(packet, "ddd", &context, &result, &proposeTime);

    CTeamBattleAgit* agit = nullptr;
    User* user = nullptr;
    CNPC* npc = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "ppp", &agit, &user, &npc)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3951,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"db error occurred in AddEntryPledgeMember in Team Battle Agit / may be registration overlapped")
        L2_LOG_ERROR(
            Log,
            u"[team battle] Database rejected member registration; the entry "
            u"may overlap an existing registration."
        );
    } else {
        agit->AddEntryPledgeMember(user->GetDBID(), user->m_data->m_nPledgeID, proposeTime);
    }

    if (npc) {
        g_npcserver.Send("cdddd", 0x3f, npc->m_data->index, user->m_data->index, 2, result != 0 ? 2 : 0);
        L2_RELEASE(npc, ObjectRefType::Cached);
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(user, ObjectRefType::Cached);
    return false;
}

bool ReplyTeamBattleAgitStatus(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t ignoredStatus = 0;
    std::int32_t ignoredTime = 0;
    ::Disassemble(packet, "ddd", &context, &ignoredStatus, &ignoredTime);

    CObject* contextObject = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &contextObject)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x397e,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }
    L2_RELEASE(contextObject, ObjectRefType::Cached);
    return false;
}

bool ReplyUnregisterTeamBattleAgitPledge(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &context, &result);

    CTeamBattleAgit* agit = nullptr;
    CPledge* pledge = nullptr;
    User* user = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "ppp", &agit, &pledge, &user)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x39cb,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result != 0) {
        agit->RemoveEntryPledge(pledge->GetDbId());
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(pledge, ObjectRefType::Cached);
    L2_RELEASE(user, ObjectRefType::Cached);
    return false;
}

bool ReplyUnregisterTeamBattleAgitMember(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &context, &result);

    CTeamBattleAgit* agit = nullptr;
    User* user = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "pp", &agit, &user)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x39e6,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result != 0) {
        agit->RemoveEntryPledgeMember(user->m_data->m_nPledgeID, user->GetDBID());
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(user, ObjectRefType::Cached);
    return false;
}

bool ReplySetTeamBattleStatus(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    std::int32_t ignoredStatus = 0;
    ::Disassemble(packet, "ddd", &context, &result, &ignoredStatus);

    CTeamBattleAgit* agit = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &agit)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3aa4,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error occurred in [set team battle status]")
        L2_LOG_ERROR(Log, u"[team battle] Database rejected the status update.");
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    return false;
}

bool ReplySetTeamBattleWinner(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t ignoredPledgeId = 0;
    std::int32_t result = 0;
    std::int32_t ignoredTime = 0;
    ::Disassemble(packet, "dddd", &context, &ignoredPledgeId, &result, &ignoredTime);

    CObject* contextObject = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &contextObject)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3ac3,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error occurred in [set team battle winner]")
        L2_LOG_ERROR(Log, u"[team battle] Database rejected the winner update.");
    }
    L2_RELEASE(contextObject, ObjectRefType::Cached);
    return false;
}

bool ReplyLoadSiegeAgit(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t pledgeCount = 0;
    packet = ::Disassemble(packet, "dd", &context, &pledgeCount);

    CSiegeAgit* agit = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "p", &agit)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3a15,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    for (std::int32_t index = 0; index < pledgeCount; ++index) {
        std::int32_t pledgeDatabaseId = 0;
        std::int32_t score = 0;
        std::int32_t ignored = 0;
        packet = ::Disassemble(packet, "ddd", &pledgeDatabaseId, &score, &ignored);
        agit->RegisterAttackerPledge(static_cast<std::uint32_t>(pledgeDatabaseId), score);

        const int databaseId = runtime::process::CurrentDatabaseId();
        CSPointer<CPledge> pledge = CPledge::FindPledge(databaseId, pledgeDatabaseId);
        if (!pledge) {
            CPledge::ReserveLoaded(databaseId, pledgeDatabaseId, PRLT_SIEGEAGIT);
        }
    }

    for (CDoor* const door : agit->m_DoorList) {
        db.RequestLoadDoor(door);
    }
    agit->StartTimer();
    if (g_JobFinishSignal.SetAgitLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_AGIT);
    }
    L2_RELEASE(agit, ObjectRefType::Cached);
    return false;
}

namespace DBPacketHandler {

bool _ReplyRegisterSiegeAgitPledge(const int context, const int result, const int score) {
    L2_GUARD_FUNCTION();

    User* user = nullptr;
    CSiegeAgit* agit = nullptr;
    CPledge* pledge = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "ppp", &user, &agit, &pledge)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3a57,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result != 0) {
        agit->RegisterAttackerPledge(pledge->GetDbId(), score);
        const int residenceId = agit->m_data ? agit->m_data->m_nResidenceID : 0;
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0xdf, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(), residenceId, 0, 0, 0, 0, 0
        );
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket) {
        agit->SendSiegeAttackerList(socket, user);
    }
    L2_RELEASE(user, ObjectRefType::Cached);
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(pledge, ObjectRefType::Cached);
    return true;
}

bool ReplyRegisterSiegeAgitPledge(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    std::int32_t score = 0;
    ::Disassemble(packet, "ddd", &context, &result, &score);
    _ReplyRegisterSiegeAgitPledge(context, result, score);
    return false;
}

} // namespace DBPacketHandler

bool ReplyUnregisterSiegeAgitPledge(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c1964, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t context = 0;
    std::int32_t result = 0;
    ::Disassemble(packet, "dd", &context, &result);

    User* user = nullptr;
    CSiegeAgit* agit = nullptr;
    CPledge* pledge = nullptr;
    if (!Singleton<CCacheContextManager>::inst().GetContext(context, "ppp", &user, &agit, &pledge)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid context id[%x]",
        //     L"DBSocket.cpp",
        //     0x3a7d,
        //     context)
        L2_LOG_ERROR(Log, u"[database reply] Context 0x%x is invalid.", context);
        return false;
    }

    if (result != 0) {
        agit->UnregisterAttackerPledge(pledge->GetDbId());
        const int residenceId = agit->m_data ? agit->m_data->m_nResidenceID : 0;

        const int z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
        const int y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
        const int x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
        const char16_t* const userName = user->GetName();
        const int pledgeDatabaseId = pledge->GetDbId();
        const char16_t* const pledgeName = pledge->GetPledgeName();
        const int accountId = user->GetAccountID();
        const int userDatabaseId = user->GetDBID();

#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
        std::size_t accountNameLength = 0;
        while (accountNameLength < user->m_sAccountName.size() && user->m_sAccountName[accountNameLength] != u'\0') {
            ++accountNameLength;
        }
        const std::wstring accountNameStorage =
            ::Utf16ToWide(std::u16string_view(user->m_sAccountName.data(), accountNameLength));
        const wchar_t* const accountName = accountNameStorage.c_str();
#endif

        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        //     std::int64_t{0xe1},
        //     static_cast<std::int64_t>(userDatabaseId),
        //     static_cast<std::int64_t>(accountId),
        //     static_cast<std::int64_t>(x),
        //     static_cast<std::int64_t>(y),
        //     static_cast<std::int64_t>(z),
        //     pledgeName,
        //     static_cast<std::int64_t>(user->m_data->m_Race),
        //     static_cast<std::int64_t>(user->m_data->m_Sex),
        //     static_cast<std::int64_t>(user->m_data->m_nClass),
        //     static_cast<std::int64_t>(user->m_data->m_nLevel),
        //     static_cast<std::int64_t>(pledgeDatabaseId),
        //     static_cast<std::int64_t>(residenceId),
        //     userName,
        //     accountName)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
            std::int64_t{0xe1}, static_cast<std::int64_t>(userDatabaseId), static_cast<std::int64_t>(accountId),
            static_cast<std::int64_t>(x), static_cast<std::int64_t>(y), static_cast<std::int64_t>(z), pledgeName,
            static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
            static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
            static_cast<std::int64_t>(pledgeDatabaseId), static_cast<std::int64_t>(residenceId), userName, accountName
        );
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        agit->SendSiegeAttackerList(socket, user);
    }
    L2_RELEASE(user, ObjectRefType::Cached);
    L2_RELEASE(agit, ObjectRefType::Cached);
    L2_RELEASE(pledge, ObjectRefType::Cached);
    return false;
}

bool ReplyManMacro(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b32fc, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t ignoredCharacterId = 0;
    std::int32_t userServerId = 0;
    std::int32_t macroType = 0;
    std::int32_t macroIndex = 0;
    std::int32_t loaded = 0;
    packet = ::Disassemble(packet, "ddcdc", &ignoredCharacterId, &userServerId, &macroType, &macroIndex, &loaded);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    if (loaded == 0) {
        user->GetSocket()->Send("ccdcc", 0xe8, macroType, macroIndex, loaded, 0);
        return false;
    }

    std::int32_t macroId = 0;
    std::array<char16_t, 0x40> name{};
    std::array<char16_t, 0x40> description{};
    std::array<char16_t, 0x40> acronym{};
    std::int32_t unknown = 0;
    std::int32_t actionCount = 0;
    packet = ::Disassemble(
        packet, "dSSSdc", &macroId, 0x80, name.data(), 0x80, description.data(), 0x80, acronym.data(), &unknown, &actionCount
    );

    CPacketHelperEx<20480> actions;
    for (std::int32_t index = 0; index < actionCount; ++index) {
        std::uint8_t actionType = 0;
        std::uint8_t actionParam = 0;
        std::int32_t actionId = 0;
        std::uint8_t actionUse = 0;
        std::array<char16_t, 0x51> command{};
        packet = ::Disassemble(packet, "ccdcS", &actionType, &actionParam, &actionId, &actionUse, 0xa2, command.data());
        actions.Assemble("ccdcS", actionType, actionParam, actionId, actionUse, command.data());
    }

    user->GetSocket()->Send(
        "ccdccdSSSdcb", 0xe8, macroType, macroIndex, loaded, 1, macroId, name.data(), description.data(), acronym.data(),
        unknown, actionCount, actions.GetLength(), actions.GetPacket()
    );
    return false;
}

bool ReplyManMemo(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b36ac, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t ignoredCharacterId = 0;
    std::int32_t userServerId = 0;
    std::int32_t operation = 0;
    packet = ::Disassemble(packet, "ddd", &ignoredCharacterId, &userServerId, &operation);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x4054,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (operation == 1) {
        CMemoHTML memo(socket);
        memo.MemoList(packet);
    } else if (operation == 2) {
        CMemoHTML memo(socket);
        memo.MemoRead(packet);
    } else if (operation == 3) {
        std::int32_t result = 0;
        ::Disassemble(packet, "d", &result);
        if (result == -1) {
            socket->SendSystemMessage(SystemMessageId::MEMO_LIMIT_EXCEEDED);
        }
    } else if (operation == 4 || operation == 6) {
        db.RequestGetMemoList(user, 1, u"");
    } else if (operation == 5) {
        CMemoHTML memo(socket);
        memo.MemoModifyForm(6, packet);
    }
    return false;
}

bool ReplyManPledgeBBS(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b38e0, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();
    std::int32_t ignoredCharacterId = 0;
    std::int32_t userServerId = 0;
    std::int32_t operation = 0;
    packet = ::Disassemble(packet, "ddd", &ignoredCharacterId, &userServerId, &operation);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x4099,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (operation == 1) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSList(packet);
    } else if (operation == 2) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSView(packet);
    } else if (operation == 3) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSView2(packet);
    } else if (operation == 4) {
        CClanBBSHTML clanBbs(socket);
        clanBbs.ClanBBSAdmin(packet);
    }
    return false;
}

bool ReplyManorBuySeed(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b3abc, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t requestId = 0;
    std::int32_t userServerId = 0;
    std::int32_t manorId = 0;
    std::int32_t ignoredValue = 0;
    std::int64_t ignoredAmount = 0;
    std::int32_t manipulationCount = 0;
    packet = ::Disassemble(
        packet, "dddddQd", &result, &requestId, &userServerId, &manorId, &ignoredValue, &ignoredAmount, &manipulationCount
    );

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x40d5,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] buy seed fail id[%d], id2[%d]",
        //     "DBSocket.cpp",
        //     0x40da,
        //     requestId,
        //     manorId)
        L2_LOG_ERROR(Log, u"[manor] Seed purchase reply IDs do not match: expected %d, received %d.", requestId, manorId);
        const int messageId = result == 6
            ? SystemMessageId::NOT_ENOUGH_ADENA
            : (result == 9 ? SystemMessageId::NOT_ENOUGH_SEEDS_OR_REMAINING_PROCUREMENT_QUANTITY
                           : SystemMessageId::CANNOT_PROCEED_ERROR_OCCURRED);
        user->GetSocket()->SendSystemMessage(messageId);
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid manor id. [%d](%s)(%d)",
        //     manorId,
        //     user->GetName(),
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[manor] Disconnecting '%s' (%d): manor ID %d is invalid.",
        //     user->GetName(),
        //     user->GetDBID(),
        //     manorId)
        L2_LOG_ERROR(
            Log, u"[manor] Disconnecting '%s' (%d): manor ID %d is invalid.", user->GetName(), user->GetDBID(), manorId
        );
        user->StopMove();
        user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    {
        L2_WRITE_GUARD(*manor);
        L2_WRITE_GUARD(user->m_inv);
        while (manipulationCount > 0) {
            --manipulationCount;

            std::int32_t manipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            packet =
                ::Disassemble(packet, "dddQQ", &manipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter);

            const auto writeItemLog = [user, manor, itemClassId](const std::int64_t amount) {
                const int currentManorId = manor->m_data != nullptr ? manor->m_data->m_nManorId : 0;
                CLogFormatter formatter;
                formatter.WriteLog2_1(0x10b, user, nullptr, currentManorId, itemClassId, amount, 0, 0, 0, 0);
            };

            if (manipulationType == 1) {
                CItem* const item = g_ObjectDB.CreateItem(itemClassId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CreateItem Failed",
                    //     L"DBSocket.cpp",
                    //     0x412a)
                    L2_LOG_ERROR(
                        Log, u"[manor] Cannot create seed purchase item class %d (DB ID %d).", itemClassId, itemDatabaseId
                    );
                    continue;
                }

                item->SetDBID(static_cast<std::uint32_t>(itemDatabaseId));
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->SetAmount(amountAfter)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid"
                        //     L"[%I64d][%I64d]",
                        //     "DBSocket.cpp",
                        //     0x4113,
                        //     item->m_data->m_nCount,
                        //     amountAfter)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                            item->m_data->m_nCount, amountAfter
                        );
                    }
                }
                if (!user->m_inv.Push(item->m_data->index)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "inven push failed at file[%s], line[%d]",
                    //     "DBSocket.cpp",
                    //     0x4119)
                    L2_LOG_ERROR(
                        Log, "[inventory] Item %u could not be inserted while applying an item-use reply for character %u.",
                        item->GetDBID(), user->GetDBID()
                    );
                }
                user->AutoEquipArrowOrBolt(item);
                user->m_inv.SetInventoryChanged(item, IUT_ADD);
                user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                manor->SetSeedSold(itemClassId, amountDelta);
                writeItemLog(amountDelta);
                continue;
            }

            if (manipulationType == 2) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x4146,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                    continue;
                }
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->AddAmount(amountDelta)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid"
                        //     L"[%I64d][%I64d]",
                        //     "DBSocket.cpp",
                        //     0x4138,
                        //     item->m_data->m_nCount,
                        //     amountDelta)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                            item->m_data->m_nCount, amountDelta
                        );
                    }
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                manor->SetSeedSold(itemClassId, amountDelta);
                writeItemLog(amountDelta);
            } else if (manipulationType == 3) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x415f,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                    continue;
                }
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->AddAmount(amountDelta)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid"
                        //     L"[%I64d][%I64d]",
                        //     "DBSocket.cpp",
                        //     0x4153,
                        //     item->m_data->m_nCount,
                        //     amountDelta)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                            item->m_data->m_nCount, amountDelta
                        );
                    }
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                user->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), true);
                writeItemLog(runtime::NegateInt64Wrapping(amountDelta));
            } else if (manipulationType == 4) {
                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] pItemInv == NULL, bad. "
                    //     L"[%s][%d][%d][%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x4169,
                    //     user->GetName(),
                    //     itemDatabaseId,
                    //     itemClassId,
                    //     amountDelta,
                    //     amountAfter)
                    // L2_LOG_ERROR(Log,
                    //     L"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result
                    //     %I64d.", user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Item lookup failed for '%s': item DB ID %d, class %d, change %I64d, database result "
                        u"%I64d.",
                        user->GetName(), itemDatabaseId, itemClassId, amountDelta, amountAfter
                    );
                    continue;
                }
                user->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), true);
                writeItemLog(runtime::NegateInt64Wrapping(amountDelta));
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                user->m_inv.Pop(item->m_data->index, false);
                item->Delete();
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] buy seed item fail. "
                //     L"unknown buy seed type[%d]",
                //     "DBSocket.cpp",
                //     0x417a,
                //     manipulationType)
                L2_LOG_ERROR(
                    Log, u"[manor] Seed purchase returned unsupported item manipulation type %d.", manipulationType
                );
            }
        }
    }
    user->SendItemList(true, true, nullptr);
    return false;
}

bool ReplyManorSellCrop(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b4394, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t requestId = 0;
    std::int32_t userServerId = 0;
    std::int32_t manorId = 0;
    std::int32_t ignoredValue = 0;
    std::int32_t manipulationCount = 0;
    packet =
        ::Disassemble(packet, "dddddd", &result, &requestId, &userServerId, &manorId, &ignoredValue, &manipulationCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x419f,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] sell crop fail id[%d], id2[%d]",
        //     "DBSocket.cpp",
        //     0x41a3,
        //     requestId,
        //     manorId)
        L2_LOG_ERROR(Log, u"[manor] Crop sale reply IDs do not match: expected %d, received %d.", requestId, manorId);
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_ERROR_OCCURRED);
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Kicked for invalid manor id. [%d](%s)(%d)",
        //     manorId,
        //     user->GetName(),
        //     user->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[manor] Disconnecting '%s' (%d): crop sale references unregistered manor ID %d.",
        //     user->GetName(),
        //     user->GetDBID(),
        //     manorId)
        L2_LOG_ERROR(
            Log, u"[manor] Disconnecting '%s' (%d): crop sale references unregistered manor ID %d.", user->GetName(),
            user->GetDBID(), manorId
        );
        user->StopMove();
        user->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
        return false;
    }

    {
        L2_WRITE_GUARD(*manor);
        L2_WRITE_GUARD(user->m_inv);
        while (manipulationCount > 0) {
            --manipulationCount;

            std::int32_t rawManipulationType = 0;
            std::int32_t itemClassId = 0;
            std::int32_t itemDatabaseId = 0;
            std::int64_t amountDelta = 0;
            std::int64_t amountAfter = 0;
            packet = ::Disassemble(
                packet, "dddQQ", &rawManipulationType, &itemClassId, &itemDatabaseId, &amountDelta, &amountAfter
            );
            const std::uint16_t manipulationType = static_cast<std::uint16_t>(rawManipulationType);

            const auto writeItemLog = [user, manor, itemClassId](
                                          const int logId, const std::int64_t amount, const std::int64_t itemDatabaseIdForLog
                                      ) {
                const int currentManorId = manor->m_data != nullptr ? manor->m_data->m_nManorId : 0;
                CLogFormatter formatter;
                formatter.WriteLog2_1(
                    logId, user, nullptr, currentManorId, itemClassId, amount, 0, 0, 0, itemDatabaseIdForLog
                );
            };

            if (manipulationType == 1 || manipulationType == 2) {
                if (!user->m_inv.CreateOrAddItem(itemDatabaseId, itemClassId, amountDelta)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CreateOrAddItem Failed",
                    //     L"DBSocket.cpp",
                    //     0x41d7)
                    L2_LOG_ERROR(Log, u"[manor] Failed to create or merge the crop-sale reward item.");
                    continue;
                }

                CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
                if (item == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] CreateOrAddItem Failed",
                    //     L"DBSocket.cpp",
                    //     0x41cd)
                    L2_LOG_ERROR(Log, u"[manor] Failed to create or merge the crop-sale reward item.");
                    continue;
                }
                user->SendItemIncreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted());
                writeItemLog(0x108, amountDelta, static_cast<std::int64_t>(itemDatabaseId));
                continue;
            }

            if (manipulationType != 3 && manipulationType != 4) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] sell crop item fail. "
                //     L"unknown sell crop type[%d]",
                //     "DBSocket.cpp",
                //     0x422b,
                //     static_cast<int>(manipulationType))
                L2_LOG_ERROR(
                    Log, u"[manor] Crop sale returned unsupported item manipulation type %d.",
                    static_cast<int>(manipulationType)
                );
                continue;
            }

            CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     manipulationType == 3
                //         ? L"[%S][%d]decrease pItemInv == NULL, bad. "
                //           L"dbid[%d] change_amount[%I64d] "
                //           L"result_amount[%I64d]"
                //         : L"[%S][%d]delete pItemInv == NULL, bad. "
                //           L"dbid[%d] change_amount[%I64d] "
                //           L"result_amount[%I64d]",
                //     "DBSocket.cpp",
                //     manipulationType == 3 ? 0x41f6 : 0x4200,
                //     itemDatabaseId,
                //     amountDelta,
                //     amountAfter)
                L2_LOG_ERROR(
                    Log,
                    manipulationType == 3
                        ? L"[inventory] Cannot decrease missing crop item DB ID %d; change %I64d, database result %I64d."
                        : L"[inventory] Cannot delete missing crop item DB ID %d; change %I64d, database result %I64d.",
                    itemDatabaseId, amountDelta, amountAfter
                );
                continue;
            }

            std::int64_t itemDatabaseIdForLog = 0;
            if (manipulationType == 3) {
                {
                    L2_GUARD(*item->m_lock);
                    if (!item->AddAmount(amountDelta)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid"
                        //     L"[%I64d][%I64d]",
                        //     "DBSocket.cpp",
                        //     0x41e6,
                        //     item->m_data->m_nCount,
                        //     amountDelta)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                            item->m_data->m_nCount, amountDelta
                        );
                    }
                }
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                itemDatabaseIdForLog = static_cast<std::int64_t>(static_cast<std::int32_t>(item->GetDBID()));
                user->SendItemDecreaseSystemMessage(itemClassId, amountDelta, item->m_data->GetEnchanted(), true);
            } else {
                const std::int64_t absoluteAmount = runtime::AbsoluteInt64Wrapping(amountDelta);
                if (itemClassId == 0x39) {
                    user->GetSocket()->Send("chccQ", 0x62, SystemMessageId::ADENA_DISAPPEARED, 1, 6, absoluteAmount);
                } else if (absoluteAmount < 2) {
                    user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, itemClassId);
                } else {
                    user->GetSocket()->Send(
                        "chccdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DISAPPEARED, 2, 3, itemClassId, 6, absoluteAmount
                    );
                }

                itemDatabaseIdForLog = static_cast<std::int64_t>(static_cast<std::int32_t>(item->GetDBID()));
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                user->m_inv.Pop(item->m_data->index, false);
                item->Delete();
            }

            const std::int64_t procuredAmount = runtime::NegateInt64Wrapping(amountDelta);
            manor->SetCropProcured(itemClassId, procuredAmount);
            writeItemLog(0x107, procuredAmount, itemDatabaseIdForLog);
        }
    }
    user->SendItemList(true, true, nullptr);
    return false;
}

bool ReplyClanBBSList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b4b88, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();
    std::int32_t ignoredCharacterId = 0;
    std::int32_t userServerId = 0;
    std::int32_t ignoredValue = 0;
    std::int32_t operation = 0;
    packet = ::Disassemble(packet, "dddd", &ignoredCharacterId, &userServerId, &ignoredValue, &operation);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x4280,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CClanBBSHTML clanBbs(user->GetSocket());
    if (operation == 1) {
        clanBbs.ClanBBSList2(packet);
    } else if (operation == 2) {
        clanBbs.ClanBBSAdmin2(packet);
    }
    return false;
}

bool ReplyManorInfoAndData(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a50d8, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::int32_t manorId = 0;
    std::int32_t residenceId = 0;
    std::int64_t seedSellBank = 0;
    std::int64_t procureBuyRemainBank = 0;
    std::uint8_t changeState = 0;
    SYSTEMTIME lastChanged{};
    std::int64_t currentData[40][10]{};
    std::int64_t nextData[40][7]{};
    ::Disassemble(
        packet, "ddQQcbbb", &manorId, &residenceId, &seedSellBank, &procureBuyRemainBank, &changeState, 0x10, &lastChanged,
        0xc80, currentData, 0x8c0, nextData
    );

    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Cannot find manor. [%d]",
        //     "DBSocket.cpp",
        //     0x4264,
        //     manorId)
        L2_LOG_ERROR(Log, u"[manor] Manor ID %d is not registered; reply ignored.", manorId);
        return false;
    }

    {
        L2_WRITE_GUARD(*manor);
        manor->SetInfo(residenceId, seedSellBank, procureBuyRemainBank, changeState, &lastChanged);
        manor->SetSeedData(currentData, nextData);
    }
    manor->ChcekDayChanged();
    return false;
}

bool BuilderList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006cf6ac, original owner DBSocket.cpp.
    L2_GUARD_FUNCTION();

    std::uint32_t builderCount = 0;
    char builderAccount[0x20];
    std::memset(builderAccount, 0, 0x14);
    packet = ::Disassemble(packet, "d", &builderCount);

    if (builderCount <= 200U) {
        g_mapBuilderAccount.clear();
        for (std::uint32_t index = 0; index < builderCount; ++index) {
            std::memset(builderAccount, 0, 0x14);
            packet = ::Disassemble(packet, "s", 0x14, builderAccount);

            // The current owner stores the address of its reused stack
            // buffer unchanged. Preserve that dangling-pointer behavior.
            g_mapBuilderAccount.push_back(builderAccount);
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Too many builders[%d]",
        //     static_cast<std::int32_t>(builderCount))
        L2_LOG_ERROR(Log, u"[builder] CacheD returned %u builder accounts; the supported limit is 200.", builderCount);
    }
    return false;
}

bool ReplyLoadLastChangeResidence(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t incomeUpdate = 0;
    std::int32_t taxChange = 0;
    std::int32_t manorResetUpdate = 0;
    ::Disassemble(packet, "ddd", &incomeUpdate, &taxChange, &manorResetUpdate);

    if (incomeUpdate == -1 || taxChange == -1 || manorResetUpdate == -1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[%s][%d] -1",
        //     L"DBSocket.cpp",
        //     0x37b0)
        L2_LOG_INFO(Log, u"[residence] Last-change timestamps are unavailable; CacheD returned -1.");
        return false;
    }

    const std::tm* localTime = runtime::crt::LocalTime32(incomeUpdate);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"latest income update [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    // print calendar year/month and label local time.
    // The recovered log is kept below. The stored timestamp is unchanged.
    // L2_LOG_INFO(Log,
    //     L"latest income update [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    L2_LOG_INFO(
        Log, u"[residence] Latest income update: %04lld-%02d-%02d %02d:%02d:%02d local time (Unix seconds %d).",
        static_cast<long long>(localTime->tm_year) + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour,
        localTime->tm_min, localTime->tm_sec, incomeUpdate
    );

    localTime = runtime::crt::LocalTime32(taxChange);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"latest tax change [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    // calendar date/local-time diagnostics. Recovered log kept.
    // L2_LOG_INFO(Log,
    //     L"latest tax change [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    L2_LOG_INFO(
        Log, u"[residence] Latest tax change: %04lld-%02d-%02d %02d:%02d:%02d local time (Unix seconds %d).",
        static_cast<long long>(localTime->tm_year) + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour,
        localTime->tm_min, localTime->tm_sec, taxChange
    );

    if (manorResetUpdate < 0) {
        manorResetUpdate = 0;
    }
    localTime = runtime::crt::LocalTime32(manorResetUpdate);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"latest manor reset [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    // calendar date/local-time diagnostics. Recovered log kept.
    // L2_LOG_INFO(Log,
    //     L"latest manor reset [%d-%d-%d %d:%d:%d]",
    //     localTime->tm_year,
    //     localTime->tm_mon,
    //     localTime->tm_mday,
    //     localTime->tm_hour,
    //     localTime->tm_min,
    //     localTime->tm_sec)
    L2_LOG_INFO(
        Log, u"[residence] Latest manor reset: %04lld-%02d-%02d %02d:%02d:%02d local time (Unix seconds %d).",
        static_cast<long long>(localTime->tm_year) + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour,
        localTime->tm_min, localTime->tm_sec, manorResetUpdate
    );

    g_L2Time.timeLastTaxChange = taxChange;
    g_L2Time.timeLastIncomeUpdate = incomeUpdate;
    g_L2Time.timeLastManorResetUpdate = manorResetUpdate;
    return false;
}

bool ReplyPackageLoadTargetList(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::uint32_t targetCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &userServerId, &targetCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2fcd,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CPacketHelperEx<8192> targets;
    for (std::uint32_t index = 0; index < targetCount; ++index) {
        std::int32_t targetCharacterId = 0;
        std::array<char16_t, 50> targetName{};
        cursor = ::Disassemble(cursor, "dS", &targetCharacterId, 100, targetName.data());
        targets.Assemble("dS", targetCharacterId, targetName.data());
    }

    if (static_cast<std::int32_t>(targetCount) > 0) {
        socket->Send("cdb", 200, targetCount, targets.GetLength(), targets.GetPacket());
    } else {
        socket->SendSystemMessage(SystemMessageId::NO_CHARACTER_CAN_RECEIVE_CARGO);
    }
    return false;
}

bool FriendRemoveInfo(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t friendCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &characterId, &friendCount);
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));

    if (user && friendCount > 0) {
        for (std::int32_t index = 0; index < friendCount; ++index) {
            std::int32_t friendCharacterId = 0;
            cursor = ::Disassemble(cursor, "d", &friendCharacterId);
            CSPointer<User> friendUser = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), friendCharacterId)
            );
            if (!friendUser) {
                continue;
            }

            CSPointer<CFriends> friends = friendUser->GetFriends();
            if (!friends) {
                continue;
            }

            friends->RemoveFriend(characterId);
            friends->RemoveFriendDetail(characterId);
            friendUser->GetSocket()->Send("cdS", 0x57, 1, user->GetName());
        }
    }
    return false;
}

bool ReplyLoadSummons(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userSocketId = 0;
    std::int32_t summonCount = 0;
    const std::uint8_t* const summonRows = ::Disassemble(packet, "dd", &userSocketId, &summonCount);

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (socket) {
        User* const user = socket->GetUser();
        if (!user) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"summon loading occurred before user connected!")
            L2_LOG_ERROR(
                Log, u"[summons] Loaded %d summons before socket ID %d had an attached user; summon rows were ignored.",
                summonCount, userSocketId
            );
        } else {
            user->ReserveSummon(summonCount, summonRows);
        }
    }
    return false;
}

bool ReplyLoadSuspendPledgeWar(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t rowCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &rowCount);
    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::int32_t firstPledgeId = 0;
        std::int32_t secondPledgeId = 0;
        std::int32_t ignoredThird = 0;
        std::int32_t ignoredFourth = 0;
        cursor = ::Disassemble(cursor, "dddd", &firstPledgeId, &secondPledgeId, &ignoredThird, &ignoredFourth);

        CSPointer<CPledge> firstPledge = CPledge::FindPledge(g_nWorldId, firstPledgeId);
        CSPointer<CPledge> secondPledge = CPledge::FindPledge(g_nWorldId, secondPledgeId);
    }
    return false;
}

bool ReplyLoadDoor(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    char16_t doorName[50]{};
    std::int32_t hitPoints = 0;
    std::int32_t maximumHitPoints = 0;
    ::Disassemble(packet, "Sdd", 100, doorName, &hitPoints, &maximumHitPoints);

    std::wstring hostDoorName = ::FixedUtf16ToWide(doorName);
    CDoor* const door = g_DoorDB.GetDoor(doorName, 0);
    if (!door) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error in Loading Door[%s]",
        //     hostDoorName.c_str())
        L2_LOG_ERROR(Log, u"[world] Door '%s' is not registered; state reply ignored.", hostDoorName.c_str());
        return false;
    }

    door->m_data->m_dMaxHP = static_cast<double>(maximumHitPoints < hitPoints ? hitPoints : maximumHitPoints);
    door->SetHP(static_cast<double>(hitPoints));
    door->ValidateDamageGrade();
    door->ValidatePlaneCollision();
    return false;
}

bool ReplyInstallAgitDeco(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a3e80, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t agitId;
    std::int32_t decoType;
    std::int32_t value;
    std::int32_t expireTime;
    ::Disassemble(packet, "dddd", &agitId, &decoType, &value, &expireTime);

    CResidence* const residence = g_ResidenceDB.GetResidence(agitId);
    if (residence != nullptr && residence->IsWorldAgit()) {
        switch (decoType) {
            case 0:
                residence->InstallHpStove(value, expireTime);
                break;
            case 1:
                residence->InstallMpFlame(value, expireTime);
                break;
            case 2:
                residence->InstallHatcher(value);
                break;
            case 3:
                residence->InstallTeleport(value, expireTime);
                break;
            default:
                break;
        }
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Server request InstallAgitDeco but agit is not exist.[%d]",
    //     L"DBSocket.cpp",
    //     0x239f,
    //     agitId)
    L2_LOG_ERROR(Log, u"[residence] Cannot install agit decoration: agit ID %d is not registered.", agitId);
    return false;
}

bool ReplyPetNameChange(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ae674, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t result;
    std::int32_t petServerId;
    packet = ::Disassemble(packet, "dd", &result, &petServerId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(petServerId));
    if (creature != nullptr && creature->IsPet()) {
        if (result == 0) {
            std::int32_t errorCode;
            ::Disassemble(packet, "d", &errorCode);
            CPet* const pet = creature->CastPet();
            User* const master = pet->GetMaster();
            if (master != nullptr) {
                unsigned int messageId = SystemMessageId::CANNOT_SET_PET_NAME;
                if (errorCode == -4) {
                    messageId = SystemMessageId::PET_NAME_ALREADY_EXISTS;
                } else if (errorCode == -3) {
                    messageId = SystemMessageId::PET_NAME_CONTAINS_FORBIDDEN_WORD;
                } else if (errorCode == -2) {
                    messageId = SystemMessageId::PET_NAME_IS_PROHIBITED;
                } else if (errorCode == -1) {
                    messageId = SystemMessageId::PET_NAME_CONTAINS_SPACE;
                }
                master->GetSocket()->SendSystemMessage(messageId);
            }
        } else {
            char16_t petName[0x20];
            ::Disassemble(packet, "S", 0x32, petName);
            const std::wstring hostPetName = ::FixedUtf16ToWide(petName);
            creature->CastPet()->PetNameChanged(hostPetName.c_str());
        }
    }
    return false;
}

bool ReplyPetUseItem(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t petServerId = 0;
    packet = ::Disassemble(packet, "dd", &result, &petServerId);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetUseItem failed. fail from cached",
        //     L"DBSocket.cpp",
        //     0x2d0c)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected item use for pet runtime ID %d.", petServerId);
        return false;
    }

    std::int32_t itemDatabaseId = 0;
    std::int64_t remainingCount = 0;
    ::Disassemble(packet, "dQ", &itemDatabaseId, &remainingCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(petServerId));
    CPet* const pet = creature && creature->IsPet() ? creature->CastPet() : nullptr;
    if (!pet) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadItems failed. cannot find npc",
        //     L"DBSocket.cpp",
        //     0x2d16)
        L2_LOG_ERROR(Log, u"[pets] Pet runtime ID %d is not active; item reply ignored.", petServerId);
        return false;
    }

    CItem* const item = pet->m_inv.GetByDBID(itemDatabaseId);
    if (!item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetUseItem failed. pet[%d] invalid item[%d]",
        //     L"DBSocket.cpp",
        //     0x2d1f,
        //     pet->GetDBID(),
        //     itemDatabaseId)
        L2_LOG_ERROR(
            Log, u"[pets] Pet DB ID %d does not contain item DB ID %d; item-use reply ignored.", pet->GetDBID(),
            itemDatabaseId
        );
        return false;
    }

    pet->UseETCItem(item, remainingCount, 0, false, 0);
    pet->SendPetItemList(true);
    return false;
}

bool ReplyPackageSendToTarget(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006aec68, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t resultCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &resultCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x3001,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const userSocket = user->GetSocket();
    if (resultCount == -4) {
        userSocket->SendSystemMessage(SystemMessageId::TARGET_WAREHOUSE_CAPACITY_EXCEEDED);
        return false;
    }
    if (resultCount == -3) {
        userSocket->SendSystemMessage(SystemMessageId::CARGO_SEND_CANCELLED);
        return false;
    }
    if (resultCount == -2) {
        userSocket->SendSystemMessage(SystemMessageId::CARGO_SEND_FAILED);
        return false;
    }
    if (resultCount == -1) {
        userSocket->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        return false;
    }

    const std::uint8_t* unequipPacket = packet;
    {
        L2_GUARD(*user->m_lock);
        for (std::int32_t index = 0; index < resultCount; ++index) {
            std::int32_t itemDatabaseId = 0;
            std::int64_t changeCount = 0;
            unequipPacket = ::Disassemble(unequipPacket, "dQ", &itemDatabaseId, &changeCount);
            CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
            if (item != nullptr && (item->IsWeapon() || item->IsArmor() || item->IsAccessary())) {
                user->UnequipItem(item, false);
            }
        }
    }

    {
        L2_WRITE_GUARD(user->m_inv);
        for (std::int32_t index = 0; index < resultCount; ++index) {
            std::int32_t itemDatabaseId = 0;
            std::int64_t changeCount = 0;
            packet = ::Disassemble(packet, "dQ", &itemDatabaseId, &changeCount);
            CItem* const item = user->m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr || item->m_data->m_nCount < changeCount) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot find item. dbid[%d], change count[%I64d]",
                //     itemDatabaseId,
                //     changeCount)
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot consume item DB ID %d by %I64d for package delivery: item is missing or its amount "
                    u"is too small.",
                    itemDatabaseId, changeCount
                );
                continue;
            }

            const std::int64_t amountDelta = runtime::NegateInt64Wrapping(changeCount);
            if (!item->AddAmount(amountDelta)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBSocket.cpp",
                //     0x304f,
                //     item->m_data->m_nCount,
                //     amountDelta)
                L2_LOG_ERROR(
                    Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                    item->m_data->m_nCount, amountDelta
                );
            }
            if (item->m_data->m_nCount < 1) {
                user->m_inv.Pop(item->m_data->index, false);
                user->m_inv.SetInventoryChanged(item, IUT_DELETE);
                item->Delete();
            } else {
                user->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            }
        }
    }
    user->SendItemList(true, true, nullptr);
    return false;
}

bool ReplyLoadMainClassSkill(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t rowCount = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &rowCount);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x455e,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    if (user->m_data->m_nSubJobId == 0 || rowCount <= 0) {
        return false;
    }

    const CSkillAcquireDB::SkillAcquireInfoVector& subjobSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_SUBJOB);
    const CSkillAcquireDB::SkillAcquireInfoVector& dualjobSkills = g_SkillAcquireDB.GetAcquirableEtcSkills(ESAT_DUALJOB);
    for (std::int32_t index = 0; index < rowCount; ++index) {
        std::int32_t skillId = 0;
        std::int32_t skillLevel = 0;
        std::int32_t skillSubLevel = 0;
        packet = ::Disassemble(packet, "ddd", &skillId, &skillLevel, &skillSubLevel);

        bool acquire = false;
        for (SkillAcquireInfo* const info : subjobSkills) {
            if (skillId == info->nSkillID) {
                acquire = true;
                break;
            }
        }
        if (!acquire && user->m_data->m_IsDualClass && !user->IsDoneQuest(0x28e2)) {
            for (SkillAcquireInfo* const info : dualjobSkills) {
                const int requiredLevel = info->m_GetMainDualClassLevel;
                if (skillId == info->nSkillID && requiredLevel > 0 && requiredLevel <= user->m_data->m_MainClassLevel &&
                    requiredLevel <= user->m_data->m_DualClassLevel) {
                    acquire = true;
                    break;
                }
            }
        }
        if (acquire) {
            user->AcquireSkill(skillId, skillLevel, skillSubLevel, false, false);
        }
    }
    user->ValidateChanged(std::int64_t{VB_SKILLLIST});
    return false;
}

bool ReplyPetiGetCharList(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a3b4c. Unlike the older donor, current returns
    // CPetitionCli::ReplyCharList's sendability result to CDBPacket.
    L2_GUARD_FUNCTION();
    return petitionCli.ReplyCharList(packet);
}

bool ShowPetitionMsg(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006be594.
    L2_GUARD_FUNCTION();
    std::int32_t characterDatabaseId = 0;
    char16_t message[1008];
    ::Disassemble(packet, "dS", &characterDatabaseId, 0x7d2, message);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterDatabaseId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user... char dbid[%d]",
        //     "DBSocket.cpp",
        //     0x1e37,
        //     characterDatabaseId)
        L2_LOG_ERROR(Log, u"[petition] Character DB ID %d is not active; message cannot be shown.", characterDatabaseId);
        return false;
    }

    std::wstring hostMessage = ::FixedUtf16ToWide(message);
    petitionCli.ShowMessage(user.get(), hostMessage.data());
    return false;
}

bool TeleportCharPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t ignoredWorldId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "ddddd", &characterId, &ignoredWorldId, &x, &y, &z);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (user) {
        user->StopMove();
        user->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::TeleportCharPacket, nullptr, 0);
    }
    return false;
}

bool AdminToolAnnouncePacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    char16_t targetName[128]{};
    char16_t announcement[300]{};
    ::Disassemble(packet, "SS", 0x100, targetName, 600, announcement);

    std::u16string hostTargetName = SanitizeUtf16(FixedUtf16View(targetName));
    CSPointer<User> user = GetUserFromNameMap(runtime::process::CurrentDatabaseId(), hostTargetName);
    if (user) {
        user->GetSocket()->Send("cddSdS", 0x4a, 0, 10, u"", -1, announcement);
    }
    return false;
}

bool ReplyAdminTool(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006a45b8.
    L2_GUARD_FUNCTION();

    int action = 0;
    packet = ::Disassemble(packet, "d", &action);
    switch (action) {
        case 1: {
            CDBSocketOp operation(1);
            operation.SendHome(packet);
            break;
        }
        case 2: {
            CDBSocketOp operation(2);
            operation.PledgeLev(packet);
            break;
        }
        case 3: {
            CDBSocketOp operation(3);
            operation.SetSkillAll(packet);
            break;
        }
        case 4: {
            CDBSocketOp operation(4);
            operation.ManIntAnn(packet);
            break;
        }
        case 5: {
            CDBSocketOp operation(5);
            operation.ModWeekPlay(packet);
            break;
        }
        case 6: {
            CDBSocketOp operation(6);
            operation.ChangeCharName(packet);
            break;
        }
        case 7: {
            CDBSocketOp operation(7);
            operation.SetInstantAnnounce(packet);
            break;
        }
        case 8: {
            CDBSocketOp operation;
            operation.ModDepositedSSQItem(packet);
            break;
        }
        case 9: {
            CDBSocketOp operation;
            operation.CreatePet(packet);
            break;
        }
        case 10: {
            CDBSocketOp operation(10);
            operation.OustPledgeMember(packet);
            break;
        }
        case 11: {
            CDBSocketOp operation(11);
            operation.DelPledgeAnnounce(packet);
            break;
        }
        case 12: {
            CDBSocketOp operation(12);
            operation.DelPledgeEmblem(packet);
            break;
        }
        case 13: {
            CDBSocketOp operation(13);
            operation.SetNobless(packet);
            break;
        }
        case 14: {
            CDBSocketOp operation(14);
            operation.SetHero(packet);
            break;
        }
        case 15: {
            CDBSocketOp operation(15);
            operation.GmReg(packet);
            break;
        }
        case 16: {
            CDBSocketOp operation;
            operation.CreateSubjob(packet);
            break;
        }
        case 17: {
            CDBSocketOp operation;
            operation.AddPledgeSkill(packet);
            break;
        }
        case 18: {
            CDBSocketOp operation(18);
            operation.ModPledgeSkill(packet);
            break;
        }
        case 19: {
            CDBSocketOp operation(19);
            operation.DelPledgeSkill(packet);
            break;
        }
        case 20: {
            CDBSocketOp operation(20);
            operation.LoadNobless(packet);
            break;
        }
        case 21: {
            CDBSocketOp operation(21);
            operation.AddSubPledgeSkill(packet);
            break;
        }
        case 22: {
            CDBSocketOp operation(22);
            operation.DelSubPledgeSKill(packet);
            break;
        }
        case 23: {
            CDBSocketOp operation(23);
            operation.ModSubPledgeSkill(packet);
            break;
        }
        case 24: {
            CDBSocketOp operation;
            operation.BroadcastInstantAnnounce(packet);
            break;
        }
        case 25: {
            CDBSocketOp operation;
            operation.SetPetitionDisableTime(packet);
            break;
        }
        case 26:
            // Log.Add(
            //     LOG_ERROR,
            //     "ADMIN_O_SPAWN_EVENT_NPC deprecated")
            L2_LOG_WARN(Log, "[admin] Operation 26 (spawn event NPC) is deprecated; request ignored.");
            break;
        case 27: {
            CDBSocketOp operation(27);
            operation.CancelCommission(packet);
            break;
        }
        case 28: {
            CDBSocketOp operation(28);
            operation.BroadcastScreenAnnounce(packet);
            break;
        }
        case 29: {
            CDBSocketOp operation(29);
            operation.SpawnEventNpc(packet);
            break;
        }
        case 30: {
            CDBSocketOp operation(30);
            operation.IBroadcastScreenAnnounce(packet);
            break;
        }
        case 31: {
            CDBSocketOp operation(31);
            operation.TakeGMAnnounce(packet);
            break;
        }
        default:
            break;
    }
    return false;
}

bool CreateCharacterSuccessPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t classId = 0;
    std::int32_t userSocketId = 0;
    ::Disassemble(packet, "ddd", &characterId, &classId, &userSocketId);

    CSPointer<CUserSocket> socket = g_UserSocketMap.FindObject(userSocketId);
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]pUserSocket is NULL.",
        //     "DBSocket.cpp",
        //     0x5f9)
        L2_LOG_ERROR(Log, u"[character] Character creation succeeded, but the requesting socket is no longer available.");
        return false;
    }

    User::AddNewCharID(characterId);
    db.RequestCreateInitItems(static_cast<unsigned int>(characterId), classId, socket.get());
    return false;
}

bool LoadQuestPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    packet = ::Disassemble(packet, "d", &userServerId);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0xcc7,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    {
        L2_GUARD(*user->m_lock);
        for (MemoStruct& memo : user->m_data->m_Memo) {
            packet = ::Disassemble(
                packet, "dddddd", &memo.nQuestID, &memo.nJournal, &memo.nState, &memo.nState2, &memo.nState3, &memo.nState4
            );
            if (memo.nJournal == 0) {
                memo.nJournal = 1;
            }
        }
        user->m_data->SetOneTimeFlagAll(reinterpret_cast<char*>(const_cast<std::uint8_t*>(packet)));
        user->LoadQuestProgressFromMemo();
    }
    db.RequestLoadNPCQuestLog(user->GetDBID(), userServerId, user->m_data->m_nWorldId);
    return false;
}

bool UpdateSkillPacket(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006bae98, original DBSocket.cpp owner.
    L2_GUARD_FUNCTION();

    std::int32_t characterId;
    std::int32_t mode = 0;
    std::int32_t skillId = 0;
    std::uint32_t packedLevel = 0;
    ::Disassemble(packet, "dddd", &characterId, &mode, &skillId, &packedLevel);

    const int skillLevel = static_cast<int>(packedLevel & 0xffffU);
    const int skillSubLevel = runtime::Int32FromBits(packedLevel) >> 16;
    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, "
        //     L"world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterId)
        L2_LOG_ERROR(
            Log, u"[database reply] Character DB ID %d in world %d is not active.", characterId,
            runtime::process::CurrentDatabaseId()
        );
        return false;
    }

    if (mode == 1) {
        user->AcquireSkill(skillId, skillLevel, skillSubLevel, true, true);
    } else if (mode == 2) {
        user->DeleteAcquireSkill(skillId);
    } else if (mode == 3) {
        user->ModifyAcquireSkill(skillId, skillLevel, skillSubLevel, true);
    }
    user->ValidateChanged(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED);
    return false;
}

bool UpdateQuestPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t operation = 0;
    std::int32_t questId = 0;
    std::int32_t value = 0;
    std::int32_t category = 0;
    ::Disassemble(packet, "ddddd", &characterId, &operation, &questId, &value, &category);

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot find user from g_UserDBIDMap, world id[%d] dbid[%d]",
        //     runtime::process::CurrentDatabaseId(),
        //     characterId)
        L2_LOG_ERROR(
            Log, u"[database reply] Character DB ID %d in world %d is not active.", characterId,
            runtime::process::CurrentDatabaseId()
        );
        return false;
    }

    {
        L2_GUARD(*user->m_lock);
        if (operation == 1) {
            user->m_data->SetOneTimeFlag(questId, value == 1);
        } else if (operation == 2) {
            if (category == 0) {
                user->m_data->SetMemoState(questId, 0, value);
            } else if (category == 1) {
                user->m_data->SetNRMemoState(questId, 0, value);
            }
        } else if (operation == 3) {
            if (category == 0) {
                user->m_data->RemoveMemo(questId);
            } else if (category == 1) {
                user->m_data->RemoveNRMemo(questId);
            }
        }
    }
    user->SendQuestList(nullptr);
    return false;
}

bool UpdateItemPacket(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t characterId = 0;
    std::int32_t operation = 0;
    [[maybe_unused]] std::int32_t itemServerId = 0;
    std::int32_t itemDatabaseId = 0;
    std::int32_t itemClassId = 0;
    std::int64_t count = 0;
    [[maybe_unused]] std::int32_t enchanted = 0;
    [[maybe_unused]] std::int32_t eroded = 0;
    [[maybe_unused]] std::int32_t blessed = 0;
    [[maybe_unused]] std::int32_t identified = 0;
    [[maybe_unused]] std::int32_t wished = 0;
    ::Disassemble(
        packet, "dddddQddddd", &characterId, &operation, &itemServerId, &itemDatabaseId, &itemClassId, &count, &enchanted,
        &eroded, &blessed, &identified, &wished
    );

    CSPointer<User> user =
        g_UserDBIDMap.FindObject(std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), characterId));
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Cannot find user from g_UserDBIDMap, "
        //     L"world id[%d] dbid[%d]",
        //     L"DBSocket.cpp",
        //     0x155a,
        //     runtime::process::CurrentDatabaseId(),
        //     characterId)
        L2_LOG_ERROR(
            Log, u"[database reply] Character DB ID %d in world %d is not active.", characterId,
            runtime::process::CurrentDatabaseId()
        );
        return false;
    }

    if (operation == 1) {
        user->AddItemToInventory(itemClassId, count, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{});
    } else if (operation == 2) {
        CItem* const item = [&] {
            L2_GUARD(*user->m_lock);
            return user->m_inv.GetByDBID(itemDatabaseId);
        }();
        if (item != nullptr) {
            db.RequestDestroyItem(item, count, user.get(), 0);
        }
    }
    return false;
}

bool ReplyCreatePet(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t itemClassId = 0;
    std::int32_t enchanted = 0;
    std::int32_t itemDatabaseId = 0;
    ::Disassemble(
        packet, "ddddd", &userServerId, &result, &itemClassId,
        &enchanted, &itemDatabaseId
    );
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyCreatePet failed. fail from cache",
        //     L"DBSocket.cpp",
        //     0x28c9)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected pet creation for character runtime ID %d.", userServerId);
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x28cd,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    CItem* const item = g_ObjectDB.CreateItem(itemClassId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyCreatePet Failed. item(%d)",
        //     L"DBSocket.cpp",
        //     0x28d2,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, u"[pets] Cannot create collar item class %d for character runtime ID %d.", itemClassId, userServerId
        );
        return false;
    }
    if (!item->SetAmount(1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item amount is invalid[%d][%d]",
        //     "DBSocket.cpp",
        //     0x28d8,
        //     item->m_data->m_nCount,
        //     1)
        L2_LOG_ERROR(
            Log, u"[inventory] Item amount update was rejected: current %d, database reply value %d.",
            item->m_data->m_nCount, 1
        );
    }
    item->SetDBID(static_cast<unsigned int>(itemDatabaseId));
    item->SetEnchanted(enchanted);
    item->m_Useable = user->CheckUseable(item);

    {
        L2_WRITE_GUARD(user->m_inv);
        if (!user->m_inv.Push(item->m_data->index)) {
            // ::LogIOError(
            //     "inven push failed at file[%s], line[%d]",
            //     "DBSocket.cpp",
            //     0x28e2)
            L2_LOG_IO_ERROR(
                "[inventory] Cannot add newly created pet item DB ID %d (class %d) to character runtime ID %d inventory; "
                "reply processing stops before the inventory refresh.",
                static_cast<int>(itemDatabaseId), static_cast<int>(itemClassId), static_cast<int>(userServerId)
            );
            return false;
        }
        user->m_inv.SetInventoryChanged(item, IUT_ADD);
    }
    user->SendItemList(true, true, nullptr);
    socket->Send("chccd", 0x62, SystemMessageId::PICKED_UP_ITEM, 1, 3, item->m_data->m_nClassID);
    return false;
}

bool ReplyDeletePet(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    std::int32_t itemDatabaseId = 0;
    ::Disassemble(packet, "ddd", &userServerId, &result, &itemDatabaseId);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyDeletePet failed. fail from cache",
        //     L"DBSocket.cpp",
        //     0x28fe)
        L2_LOG_ERROR(
            Log, u"[pets] CacheD rejected pet deletion for character runtime ID %d (collar DB ID %d).", userServerId,
            itemDatabaseId
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2902,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    static_cast<void>(user->GetSocket());
    CItem* item = nullptr;
    {
        L2_WRITE_GUARD(user->m_inv);
        item = user->m_inv.GetByDBID(itemDatabaseId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ReplyDeletePet failed. no such item",
            //     L"DBSocket.cpp",
            //     0x290a)
            L2_LOG_ERROR(
                Log,
                u"[pets] Collar item DB ID %d is missing from character runtime ID %d inventory; pet deletion reply "
                u"ignored.",
                itemDatabaseId, userServerId
            );
            return false;
        }

        user->m_inv.Pop(item->m_data->index, false);
        user->m_inv.SetInventoryChanged(item, IUT_DELETE);
    }
    item->Delete();
    user->SendItemList(true, true, nullptr);
    user->GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED, 1, 3, item->m_data->m_nClassID);
    return false;
}

bool ReplyEvolvePet(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006ad6a0.
    L2_GUARD_FUNCTION();

    std::int32_t userServerId = 0;
    std::int32_t result = 0;
    packet = ::Disassemble(packet, "dd", &userServerId, &result);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyEvolvePet failed. fail from cache",
        //     L"DBSocket.cpp",
        //     0x292b)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected pet evolution for character runtime ID %d.", userServerId);
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userServerId));
    User* const user = creature && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x292f,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    user->OnReplyEvolvePet(packet);
    return false;
}

namespace DBPacketHandler {

bool _ReplyWithdrawPet(
    const std::uint32_t userServerId,
    const std::uint32_t petCollarServerId,
    const int result,
    CPet* const pet,
    const std::int64_t sp,
    const int meal,
    const int slot0,
    const int slot1,
    const int slot2,
    const std::int64_t experience,
    const double hp,
    const double mp,
    wchar_t* const name,
    const int loadItems,
    const std::int64_t downExp,
    const int alive,
    const int deathCount
) {
    // Current L2Server 0x006ad7f4.
    L2_GUARD_FUNCTION();

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid user[sid:%d]",
        //     L"DBSocket.cpp",
        //     0x2975,
        //     userServerId)
        L2_LOG_ERROR(Log, u"[database reply] User server ID %d is not active.", userServerId);
        return false;
    }

    user->m_target5711PetWithdrawByte = std::byte{1};
    if (result == 0) {
        user->EndSpawnPet();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyWithDrawPet failed. fail from cache",
        //     L"DBSocket.cpp",
        //     0x297e)
        L2_LOG_ERROR(
            Log, u"[pets] CacheD rejected pet withdrawal for character runtime ID %u (collar runtime ID %u).", userServerId,
            petCollarServerId
        );
    } else if (pet == nullptr) {
        user->EndSpawnPet();
        // Log.Add(
        //     LOG_ERROR,
        //     "Pet class is NULL at file[%s], line[%d]",
        //     "DBSocket.cpp",
        //     0x2985)
        L2_LOG_ERROR(Log, "[pets] Pet withdrawal for character runtime ID %u returned no pet object.", userServerId);
    } else {
        CItem* const collar = ItemSmartIdManager()->GetObjectW(petCollarServerId);
        if (collar == nullptr || !collar->IsPetCollarItem()) {
            user->EndSpawnPet();
            // Log.Add(
            //     LOG_ERROR,
            //     L"ReplyWithdrawPet failed. invalid pet collar")
            L2_LOG_ERROR(
                Log,
                u"[pets] Pet withdrawal failed for character runtime ID %u: collar runtime ID %u is missing or is not a pet "
                u"collar.",
                userServerId, petCollarServerId
            );
        } else if (!user->m_inv.Have(collar->m_data->index)) {
            user->EndSpawnPet();
            const unsigned int collarDatabaseId = collar->GetDBID();
            const unsigned int userDatabaseId = user->GetDBID();
            // Log.Add(
            //     LOG_ERROR,
            //     L"ReplyWithdrawPet failed. don't have pet collar. "
            //     L"user[%d] collar[%d]",
            //     userDatabaseId,
            //     collarDatabaseId)
            L2_LOG_ERROR(
                Log, u"[pets] Character DB ID %u does not own collar DB ID %u; pet withdrawal aborted.", userDatabaseId,
                collarDatabaseId
            );
        } else if (g_PetManager.IsSummonedPetCollar(collar->GetDBID())) {
            user->EndSpawnPet();
            const unsigned int collarDatabaseId = collar->GetDBID();
            const unsigned int userDatabaseId = user->GetDBID();
            // Log.Add(
            //     LOG_ERROR,
            //     L"IsSummonedPetCollar in _ReplyWithdrawPet. "
            //     L"user[%d] collar[%d]",
            //     userDatabaseId,
            //     collarDatabaseId)
            L2_LOG_ERROR(
                Log, u"[pets] Collar DB ID %u for character DB ID %u is already linked to a summoned pet.", collarDatabaseId,
                userDatabaseId
            );
        } else {
            enum class PetLinkResult { Observing, AlreadyPresent, Linked };
            const PetLinkResult linkResult = [&] {
                L2_GUARD(*user->m_lock);
                if (user->m_bIsObserving) {
                    return PetLinkResult::Observing;
                }
                if (g_CreatureSmartIdManager.GetObjectW(user->m_PetSID) != nullptr) {
                    return PetLinkResult::AlreadyPresent;
                }
                pet->SetDBID(collar->GetDBID());
                g_PetManager.LinkPet(user, pet);
                return PetLinkResult::Linked;
            }();
            if (linkResult == PetLinkResult::Observing) {
                user->EndSpawnPet();
                const unsigned int userDatabaseId = user->GetDBID();
                // The current vararg call keeps these two surplus leading
                // arguments even though the format has only one conversion.
                // Log.Add(
                //     LOG_ERROR,
                //     L"ReplyWithdrawPet failed. observer mode. user[%d]",
                //     L"DBSocket.cpp",
                //     0x29a5,
                //     userDatabaseId)
                L2_LOG_ERROR(Log, u"[pets] Character DB ID %u is observing; pet withdrawal aborted.", userDatabaseId);
            } else if (linkResult == PetLinkResult::AlreadyPresent) {
                user->EndSpawnPet();
                user->GetSocket()->SendSystemMessage(SystemMessageId::PET_ALREADY_PRESENT);
            } else {
                pet->m_bIsPetAlive = alive > 0;
                pet->m_nDownExp = downExp;
                pet->m_nDeathCount.store(deathCount, std::memory_order_relaxed);
                pet->m_data->SetSP(sp);
                pet->m_data->SetHP(hp);
                pet->m_data->SetMP(mp);
                pet->m_data->m_Meal = meal;
                const FVector position = pet->ChoiceValidPosition(user, nullptr);
                pet->m_data->SetPos(position);
                pet->m_data->m_nPetDBID = collar->m_data->m_nDBID;
                pet->m_nSlots[0] = slot0;
                pet->m_nSlots[1] = slot1;
                pet->m_nSlots[2] = slot2;
                pet->SetMasterName(user->GetName());
                pet->SetNickName(WideToUtf16(name).c_str());
                pet->m_data->SetLevel(collar->m_data->GetEnchanted());
                if (!pet->IsSyncLevel()) {
                    pet->SetExp(experience);
                }
                pet->ValidateChanged(VB_MAX);
                pet->SetPrivateID(user->GetPrivateID());
                db.LoadPetItems(pet, *user, true, loadItems);
                user->OnPetStep(7);
                runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            }
        }
    }

    if (pet != nullptr && loadItems != 0) {
        if (user->IsInvisibleBySkill()) {
            pet->SetVisibleBySkill(false, true);
        } else if (!user->IsVisible()) {
            pet->SetVisible(false);
        }
    }
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return false;
}

bool ReplyWithdrawPet(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b6c08. Paired current CacheD reply 0x61.
    L2_GUARD_FUNCTION();

    std::int32_t result;
    std::uint32_t userServerId;
    std::uint32_t petCollarServerId;
    std::int32_t npcClassId;
    std::int64_t experience;
    std::int64_t sp;
    double hp;
    double mp;
    std::int32_t meal;
    char16_t targetPetName[0x400];
    targetPetName[0] = u'\0';
    std::int32_t slot0;
    std::int32_t slot1;
    std::int32_t slot2;
    std::int32_t loadItems;
    std::int64_t downExp;
    std::int32_t alive;
    std::int32_t deathCount;
    ::Disassemble(
        packet, "ddddQQffdSddddQdd", &result, &userServerId, &petCollarServerId, &npcClassId, &experience, &sp, &hp, &mp,
        &meal, 0x800, targetPetName, &slot0, &slot1, &slot2, &loadItems, &downExp, &alive, &deathCount
    );

    CPet* pet = nullptr;
    CNPC* const npc = g_ObjectDB.CreateNPC(npcClassId, true);
    if (npc != nullptr) {
        pet = npc->CastPet();
    }
    if (pet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "There is no NPC class [%d] at file[%s], line[%d]",
        //     npcClassId,
        //     "DBSocket.cpp",
        //     0x2960)
        L2_LOG_ERROR(
            Log, "[pets] NPC class %d is undefined or cannot create a pet; withdrawal reply continues with a null pet.",
            npcClassId
        );
    }

    std::wstring hostPetName = ::FixedUtf16ToWide(targetPetName);
    static_cast<void>(_ReplyWithdrawPet(
        userServerId, petCollarServerId, result, pet, sp, meal, slot0, slot1, slot2, experience, hp, mp, hostPetName.data(),
        loadItems, downExp, alive, deathCount
    ));
    return false;
}

bool _ReplyDepositPet(
    const std::uint32_t userServerId,
    const std::uint32_t userDatabaseId,
    const std::uint32_t,
    const std::int32_t petDatabaseId
) {
    // Current L2Server 0x006addd0.
    L2_GUARD_FUNCTION();

    CCreature* creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    User* user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        CPet* const pet = user->GetPet();
        if (pet == nullptr) {
            user->m_nPetItemCount = 0;
        } else {
            const int itemCount = pet->GetItemCount();
            user->m_nPetItemCount = itemCount;
            if (itemCount > 0) {
                user->SendSystemMessage(SystemMessageId::CANNOT_SELL_OR_DROP_PET_SUMMONING_ITEM_PET_INVENTORY_NOT_EMPTY);
            }

            CLogFormatter formatter;
            formatter.WriteLog2(
                0x6a, user, nullptr, pet->GetDBID(), pet->m_data->m_nClassID, pet->m_data->m_nExp, pet->m_data->m_nLevel,
                pet->m_data->m_Meal, pet->m_bIsPetAlive, 0
            );
        }
    }

    g_PetManager.DeletePet(userDatabaseId, userServerId);

    creature = g_CreatureSmartIdManager.GetObjectW(userServerId);
    user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    user->m_PetSID = 0;
    CItem* const collar = user->m_inv.GetByDBID(petDatabaseId);
    if (collar != nullptr && !user->IsRidingOf(COND_COLLAR, petDatabaseId)) {
        collar->CastEtcItem()->m_bPetCollarTradable = true;
    }
    user->OnDespawnPet();
    user->OnPetStep(3);
    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
    return true;
}

bool ReplyDepositPet(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006b6e54. Paired current CacheD request 121.
    L2_GUARD_FUNCTION();

    std::int32_t result;
    packet = ::Disassemble(packet, "d", &result);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyDepositPet failed. fail from cached",
        //     L"DBSocket.cpp",
        //     0x2aff)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected the pet deposit request.");
        return false;
    }

    std::uint32_t userServerId;
    std::uint32_t userDatabaseId;
    std::uint32_t petServerId;
    std::int32_t petDatabaseId;
    std::int32_t petIndex;
    std::uint8_t deletePet;
    ::Disassemble(packet, "dddddc", &userServerId, &userDatabaseId, &petServerId, &petDatabaseId, &petIndex, &deletePet);

    static_cast<void>(_ReplyDepositPet(userServerId, userDatabaseId, petServerId, petDatabaseId));
    if (deletePet != 0) {
        db.DeletePet(
            static_cast<int>(userServerId), static_cast<int>(userServerId), static_cast<int>(userDatabaseId), petDatabaseId,
            1, petIndex
        );
    }
    return false;
}

bool ReplyLoadPetItems(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c6ea0.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t petServerId = 0;
    std::int32_t requestValue = 0;
    packet = ::Disassemble(packet, "ddd", &result, &petServerId, &requestValue);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(petServerId));
    CPet* const pet = creature != nullptr && creature->IsPet() ? creature->CastPet() : nullptr;
    if (pet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadItems failed. cannot find npc",
        //     L"DBSocket.cpp",
        //     0x2a1d)
        L2_LOG_ERROR(Log, u"[pets] Pet runtime ID %d is not active; item reply ignored.", petServerId);
        return false;
    }

    User* const master = pet->GetMaster();
    if (master == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadItems failed. cannot find master",
        //     L"DBSocket.cpp",
        //     0x2a29)
        L2_LOG_ERROR(Log, u"[pets] Pet runtime ID %d has no active master; item-load reply ignored.", petServerId);
        return false;
    }
    if (result == 0) {
        master->EndSpawnPet();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyLoadItems failed. fail from cache",
        //     L"DBSocket.cpp",
        //     0x2a31)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected item loading for pet runtime ID %d.", petServerId);
        return false;
    }

    std::int32_t autoEquip = 0;
    std::uint32_t itemCount = 0;
    packet = ::Disassemble(packet, "dd", &autoEquip, &itemCount);
    for (std::int32_t& slotIndex : pet->m_data->m_nSlotIndex) {
        slotIndex = -1;
    }

    {
        L2_WRITE_GUARD(pet->m_inv);
        for (std::uint32_t index = 0; index < itemCount; ++index) {
            CItemDisassemblerFromCacheD itemData;
            packet = itemData.DisassembleItemInfo(packet);
            CItem* const item = g_ObjectDB.CreateItem(itemData.m_nClassID);
            if (item == nullptr) {
                continue;
            }
            if (!pet->m_inv.Push(item->m_data->index)) {
                // Log.Add(
                //     LOG_ERROR,
                //     "inven push failed at file[%s], line[%d]",
                //     L"DBSocket.cpp",
                //     0x2a82)
                // L2_LOG_ERROR(Log,
                //     "inven push failed at file[%s], line[%d]",
                //     L"DBSocket.cpp",
                //     0x2a82)
                L2_LOG_ERROR(
                    Log,
                    "[inventory] Cannot add loaded item DB ID %d (class %d) to pet runtime ID %d inventory; item "
                    "initialization continues.",
                    static_cast<int>(itemData.m_nDBID), static_cast<int>(itemData.m_nClassID), static_cast<int>(petServerId)
                );
            }
            item->SetDBID(itemData.m_nDBID);
            if (!item->AddAmount(itemData.m_nAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] item amount is invalid[%I64d][%I64d]",
                //     L"DBSocket.cpp",
                //     0x2a89,
                //     item->m_data->m_nCount,
                //     itemData.m_nAmount)
                L2_LOG_ERROR(
                    Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                    item->m_data->m_nCount, itemData.m_nAmount
                );
            }
            item->SetEnchanted(itemData.m_nEnchanted);
            item->m_data->m_nDamaged = itemData.m_nDamaged;
            item->m_data->m_bBlessed = itemData.m_nIsBlessed;
            item->m_data->SetLocked(itemData.m_hLocked);
            item->SetEnsoulOptionData(itemData.m_mEnsoulInfo, true);
        }
    }

    if (autoEquip == 1) {
        pet->AutoEquip();
    }
    for (const std::int32_t itemDatabaseId : pet->m_nSlots) {
        CItem* const item = master->m_inv.GetByDBID(itemDatabaseId);
        if (item != nullptr) {
            db.GiveItemToPet(master, item, item->m_data->m_nCount);
        }
    }

    const wchar_t* const aiName = g_ObjectDB.GetAI(pet->m_data->m_nClassID);
    if (aiName == nullptr) {
        master->EndSpawnPet();
        // Log.Add(
        //     LOG_ERROR,
        //     "pet AI does not specified [clsid=%d]",
        //     pet->m_data->m_nClassID)
        L2_LOG_ERROR(Log, "[pets] Pet class %d has no AI name; spawn aborted and pet deleted.", pet->m_data->m_nClassID);
        pet->Delete();
        return false;
    }

    std::array<char, 0x2000> parameterBuffer;
    int parameterCount = 0;
    int bufferLength = 0;
    for (const auto& parameter : pet->m_pClassConst->m_mapParameter) {
        const int assembled = ::Assemble(
            parameterBuffer.data() + bufferLength, static_cast<int>(parameterBuffer.size()) - bufferLength, "SS",
            parameter.first.c_str(), parameter.second.c_str()
        );
        ++parameterCount;
        if (assembled > 0) {
            bufferLength += assembled;
        }
    }

    if (!g_PetManager.CreatePet(master, pet)) {
        master->EndSpawnPet();
        pet->Delete();
        return false;
    }

    master->CopyUserBuffToNPC_Reserve(pet);
    NpcServer::NPCCreateParam parameter{};
    parameter.pNPC = pet;
    parameter.pos = pet->GetPos();
    parameter.aiName = const_cast<wchar_t*>(aiName);
    parameter.paramCount = parameterCount;
    parameter.bufLen = bufferLength;
    parameter.pBuf = parameterBuffer.data();
    parameter.inzoneSID = static_cast<int>(master->GetInZoneID());
    g_npcserver.SendNPCCreatePacket(parameter);
    master->OnPetStep(8);
    master->EndSpawnPet();
    if (requestValue == 0) {
        pet->OnCreated();
    }
    return false;
}

} // namespace DBPacketHandler

bool ReplyGiveItemToPet(CDBSocket*, const std::uint8_t* packet) {
    // Current L2Server 0x006c74cc.
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    packet = ::Disassemble(packet, "d", &result);
    if (result == 0) {
        std::int32_t creatureServerId = 0;
        std::int32_t ignored = 0;
        ::Disassemble(packet, "dd", &creatureServerId, &ignored);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyGiveItemToPet. fail from cached",
        //     L"DBSocket.cpp",
        //     0x2b58)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected the pet-item transfer for creature runtime ID %d.", creatureServerId);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureServerId));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            {
                L2_GUARD(user->m_ManipulateQueueLock);
                user->_DoneManiItemJob(ManipulateType::MANI_PET_ITEM, 0);
            }
        }
        return false;
    }

    for (int recordIndex = 0; recordIndex < 2; ++recordIndex) {
        std::int32_t creatureServerId = 0;
        std::int32_t type = 0;
        packet = ::Disassemble(packet, "dd", &creatureServerId, &type);

        std::int32_t itemDatabaseId = 0;
        std::int32_t itemClassId = 0;
        std::int64_t count = 0;
        std::int32_t enchanted = 0;
        std::int16_t attackType = 0;
        std::int16_t attackValue = 0;
        std::int16_t defendValues[6]{};
        std::int32_t shapeShiftClassId = 0;
        std::map<int, EnsoulOptionData*> ensoulOptions[3];
        std::int16_t useCount = 0;
        std::int16_t locked = 0;

        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatureServerId));
        if (creature == nullptr) {
            if (type == 1) {
                packet = ::Disassemble(
                    packet, "ddQdhhhhhhhhd", &itemDatabaseId, &itemClassId, &count, &enchanted, &attackType, &attackValue,
                    &defendValues[0], &defendValues[1], &defendValues[2], &defendValues[3], &defendValues[4],
                    &defendValues[5], &shapeShiftClassId
                );
                packet = CItem::DisassembleEnsoul(packet, ensoulOptions);
                packet = ::Disassemble(packet, "h", &useCount);
                packet = ::Disassemble(packet, "h", &locked);
            } else if (type == 2) {
                packet = ::Disassemble(packet, "d", &itemDatabaseId);
            } else if (type == 3) {
                packet = ::Disassemble(packet, "dQ", &itemDatabaseId, &count);
            }
            if (type == 1 || type == 2 || type == 3) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet failed. cannot find creature sid[%d] dbid[%d] nType[%d]",
                //     L"DBSocket.cpp",
                //     0x2b99,
                //     creatureServerId,
                //     itemDatabaseId,
                //     type)
                L2_LOG_ERROR(
                    Log, u"[pets] Creature runtime ID %d is not active; item DB ID %d operation type %d was ignored.",
                    creatureServerId, itemDatabaseId, type
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet failed. cannot find creature sid[%d] nType[%d]",
                //     L"DBSocket.cpp",
                //     0x2b9e,
                //     creatureServerId,
                //     type)
                L2_LOG_ERROR(
                    Log, u"[pets] Creature runtime ID %d is not active; unsupported item operation type %d was ignored.",
                    creatureServerId, type
                );
            }
            continue;
        }

        bool publish = true;
        if (type == 1) {
            packet = ::Disassemble(
                packet, "ddQdhhhhhhhhd", &itemDatabaseId, &itemClassId, &count, &enchanted, &attackType, &attackValue,
                &defendValues[0], &defendValues[1], &defendValues[2], &defendValues[3], &defendValues[4], &defendValues[5],
                &shapeShiftClassId
            );
            packet = CItem::DisassembleEnsoul(packet, ensoulOptions);
            packet = ::Disassemble(packet, "h", &useCount);
            packet = ::Disassemble(packet, "h", &locked);

            CItem* const item = g_ObjectDB.CreateItem(itemClassId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet(%d) failed. create item failed itemDBid[%d] item cls id[%d]",
                //     L"DBSocket.cpp",
                //     0x2bb9,
                //     recordIndex,
                //     itemDatabaseId,
                //     itemClassId)
                L2_LOG_ERROR(
                    Log, u"[pets] Transfer record %d cannot create item DB ID %d (class %d).", recordIndex, itemDatabaseId,
                    itemClassId
                );
                publish = false;
            } else {
                item->SetDBID(itemDatabaseId);
                if (!item->AddAmount(count)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "DBSocket.cpp",
                    //     0x2bc0,
                    //     item->m_data->m_nCount,
                    //     count)
                    L2_LOG_ERROR(
                        Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                        item->m_data->m_nCount, count
                    );
                }
                item->SetEnchanted(enchanted);
                BaseAttribute attribute(attackType, attackValue, defendValues);
                item->SetAttribute(attribute);
                item->SetShapeShiftingClassId(shapeShiftClassId);
                item->SetEnsoulOptionData(ensoulOptions, true);
                item->m_data->SetUseCount(useCount);
                item->m_data->SetLocked(locked);

                {
                    L2_WRITE_GUARD(creature->m_inv);
                    creature->m_inv.Push(item->m_data->index);
                    creature->m_inv.SetInventoryChanged(item, IUT_ADD);
                }
                if (creature->IsPC()) {
                    creature->CastUser()->AutoEquipArrowOrBolt(item);
                }
            }
        } else if (type == 2) {
            packet = ::Disassemble(packet, "d", &itemDatabaseId);
            CItem* const item = creature->m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet(%d) failed. delete item failed. item dbid[%d]",
                //     L"DBSocket.cpp",
                //     0x2be8,
                //     recordIndex,
                //     itemDatabaseId)
                L2_LOG_ERROR(
                    Log, u"[pets] Transfer record %d cannot delete missing item DB ID %d.", recordIndex, itemDatabaseId
                );
                publish = false;
            } else {
                SlotType slot = ST_UNDERWEAR;
                while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
                    slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
                }
                if (slot < ST_MAX) {
                    const SlotType equipped = creature->IsEquiped(item->m_data->index, slot);
                    if (equipped != ST_MAX && equipped != static_cast<SlotType>(-1)) {
                        if (!creature->IsPC()) {
                            creature->UnequipItem(equipped, false);
                        } else if (creature->UnequipItem(equipped, true) == static_cast<SlotType>(-1)) {
                            goto publish_record;
                        }
                    }
                }
                {
                    L2_WRITE_GUARD(creature->m_inv);
                    creature->m_inv.SetInventoryChanged(item, IUT_DELETE);
                    creature->m_inv.Pop(item->m_data->index, false);
                }
                item->Delete();
            }
        } else if (type == 3) {
            packet = ::Disassemble(packet, "dQ", &itemDatabaseId, &count);
            CItem* const item = creature->m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet(%d) failed. update failed item dbid[%d] count[%d]",
                //     L"DBSocket.cpp",
                //     0x2c14,
                //     recordIndex,
                //     itemDatabaseId,
                //     count)
                L2_LOG_ERROR(
                    Log, u"[pets] Transfer record %d cannot update missing item DB ID %d by %I64d.", recordIndex,
                    itemDatabaseId, count
                );
                publish = false;
            } else {
                {
                    L2_WRITE_GUARD(creature->m_inv);
                    if (!item->AddAmount(count)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     "DBSocket.cpp",
                        //     0x2c1a,
                        //     item->m_data->m_nCount,
                        //     count)
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                            item->m_data->m_nCount, count
                        );
                    }
                    creature->m_inv.SetInventoryChanged(item, IUT_UPDATE);
                }
            }
        }

    publish_record:
        if (!publish) {
            continue;
        }
        if (!creature->IsPC()) {
            if (!creature->IsPet()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] ReplyGiveItemToPet failed. what creature!",
                //     L"DBSocket.cpp",
                //     0x2c33)
                L2_LOG_ERROR(Log, u"[pets] Pet-item transfer resolved to a creature that is neither a player nor a pet.");
            } else {
                if (type == 1) {
                    creature->CastPet()->AutoEquip();
                }
                creature->CastPet()->SendPetItemList(true);
            }
        } else {
            creature->CastUser()->SendItemList(true, true, nullptr);
            CYieldLock* const manipulateQueueLock = &creature->CastUser()->m_ManipulateQueueLock;
            {
                L2_GUARD(*manipulateQueueLock);
                creature->CastUser()->_DoneManiItemJob(ManipulateType::MANI_PET_ITEM, 0);
            }
        }
    }
    return false;
}

bool ReplyPetGetItem(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    [[maybe_unused]] std::int32_t ignored = 0;
    std::int32_t petServerId = 0;
    packet = ::Disassemble(packet, "ddd", &result, &ignored, &petServerId);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetGetItem from cached",
        //     L"DBSocket.cpp",
        //     0x2c48)
        L2_LOG_ERROR(Log, u"[pets] CacheD rejected item pickup for pet runtime ID %d.", petServerId);
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(petServerId));
    CPet* const pet = creature && creature->IsPet() ? creature->CastPet() : nullptr;
    if (pet == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetGetItem failed. cannot find pet(%d)",
        //     L"DBSocket.cpp",
        //     0x2c50,
        //     petServerId)
        L2_LOG_ERROR(Log, u"[pets] Pet runtime ID %d is not active; item-pickup reply ignored.", petServerId);
        return false;
    }

    std::int32_t manipulationType = 0;
    packet = ::Disassemble(packet, "d", &manipulationType);
    if (manipulationType == 1) {
        std::int32_t itemClassId = 0;
        std::int32_t itemDatabaseId = 0;
        std::int64_t count = 0;
        std::int32_t enchanted = 0;
        ::Disassemble(packet, "ddQd", &itemClassId, &itemDatabaseId, &count, &enchanted);
        CItem* const item = g_ObjectDB.CreateItem(itemClassId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ReplyPetGetItem failed. create item failed",
            //     L"DBSocket.cpp",
            //     0x2c63)
            L2_LOG_ERROR(Log, u"[pets] Cannot create item class %d for pet runtime ID %d.", itemClassId, petServerId);
            return false;
        }
        item->SetDBID(static_cast<unsigned int>(itemDatabaseId));
        if (!item->SetAmount(count)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
            //     "DBSocket.cpp",
            //     0x2c6a,
            //     item->m_data->m_nCount,
            //     count)
            L2_LOG_ERROR(
                Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                item->m_data->m_nCount, count
            );
        }
        item->SetEnchanted(enchanted);
        {
            L2_WRITE_GUARD(pet->m_inv);
            pet->m_inv.Push(item->m_data->index);
            pet->m_inv.SetInventoryChanged(item, IUT_ADD);
        }
        pet->SendPetItemList(true);
        return false;
    }
    if (manipulationType == 3) {
        std::int32_t itemDatabaseId = 0;
        std::int64_t count = 0;
        ::Disassemble(packet, "dQ", &itemDatabaseId, &count);
        CItem* const item = pet->m_inv.GetByDBID(itemDatabaseId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] ReplyPetGetItem failed. "
            //     L"cannot find item in inventory",
            //     L"DBSocket.cpp",
            //     0x2c7e)
            L2_LOG_ERROR(
                Log, u"[pets] Item DB ID %d is missing from pet runtime ID %d inventory; update reply ignored.",
                itemDatabaseId, petServerId
            );
            return false;
        }
        {
            L2_WRITE_GUARD(pet->m_inv);
            if (!item->SetAmount(count)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "DBSocket.cpp",
                //     0x2c84,
                //     item->m_data->m_nCount,
                //     count)
                L2_LOG_ERROR(
                    Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
                    item->m_data->m_nCount, count
                );
            }
            pet->m_inv.SetInventoryChanged(item, IUT_UPDATE);
        }
        pet->SendPetItemList(true);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] ReplyPetGetItem failed. what! k(%d)",
    //     L"DBSocket.cpp",
    //     0x2c8e,
    //     manipulationType)
    L2_LOG_ERROR(Log, u"[pets] Pet item pickup returned unsupported manipulation type %d.", manipulationType);
    return false;
}

bool ReplyPetDropItem(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t result = 0;
    std::int32_t petServerId = 0;
    std::int32_t itemServerId = 0;
    packet = ::Disassemble(packet, "ddd", &result, &petServerId, &itemServerId);
    if (result == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetDestroyItem failed. fail from cached",
        //     L"DBSocket.cpp",
        //     0x2ca4)
        L2_LOG_ERROR(
            Log, u"[pets] CacheD rejected item drop for pet runtime ID %d and item runtime ID %d.", petServerId, itemServerId
        );
        return false;
    }

    std::int64_t remainingAmount = 0;
    std::int32_t inZoneId = 0;
    ::Disassemble(packet, "Qd", &remainingAmount, &inZoneId);
    CItem* const item = CSmartID(static_cast<std::uint32_t>(itemServerId)).GetItem();
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ReplyPetDestroyItem failed. invalid item",
        //     L"DBSocket.cpp",
        //     0x2cae)
        L2_LOG_ERROR(Log, u"[pets] Item runtime ID %d is not active; pet item-drop reply ignored.", itemServerId);
        return false;
    }
    if (!item->SetAmount(remainingAmount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
        //     "DBSocket.cpp",
        //     0x2cb3,
        //     item->m_data->m_nCount,
        //     remainingAmount)
        L2_LOG_ERROR(
            Log, u"[inventory] Item amount update was rejected: current %I64d, database reply value %I64d.",
            item->m_data->m_nCount, remainingAmount
        );
    }
    item->m_data->m_nCount = remainingAmount;

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(petServerId));
    if (creature != nullptr && creature->IsPet()) {
        CPet* const pet = creature->CastPet();
        {
            L2_WRITE_GUARD(pet->m_inv);
            pet->m_inv.Pop(item->m_data->index, false);
            pet->m_inv.SetInventoryChanged(item, IUT_DELETE);
        }
        pet->SendPetItemList(true);
    }

    const FVector position = item->m_data->GetPos();
    CWorld::Inst().PutItem(item, position, static_cast<std::uint32_t>(inZoneId), creature);
    return false;
}

bool ReplyLoadDayPlayTime(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::int32_t accountId = 0;
    std::int32_t playTime = 0;
    _SYSTEMTIME lastSave{};
    ::Disassemble(packet, "ddb", &accountId, &playTime, 0x10, &lastSave);

    const runtime::SystemTime hostTime = runtime::GetLocalTimeNow();
    _SYSTEMTIME current{hostTime.wYear, hostTime.wMonth, hostTime.wDayOfWeek, hostTime.wDay, hostTime.wHour,
        hostTime.wMinute, hostTime.wSecond, hostTime.wMilliseconds};

    bool allow = false;
    if (current.wYear != lastSave.wYear || current.wMonth != lastSave.wMonth || current.wDay != lastSave.wDay) {
        if (current.wDayOfWeek == 5 || GetDayDiff(&current, &lastSave) > 6 ||
            (current.wDayOfWeek + 2) % 7 < (lastSave.wDayOfWeek + 2) % 7) {
            allow = true;
        }
    }
    if (playTime < g_nPlayTimeLimit) {
        allow = true;
    }

    if (allow) {
        if (g_nAgeCheckType == 1) {
            authd.AuthRequestAgeKoreanStyle(accountId);
        } else {
            authd.AuthPlayOk(accountId);
        }
    } else {
        authd.AuthPlayFail(accountId, 0x1e);
        NPSystem::CNPD::LoginStep_Logout(static_cast<unsigned int>(accountId));
    }
    return false;
}

bool ReplyChangeTimeLimit(CDBSocket*, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    [[maybe_unused]] std::int32_t ignored = 0;
    std::int32_t newWeekPlayTime = 0;
    std::int32_t userServerId = 0;
    ::Disassemble(packet, "dddd", &accountId, &ignored, &newWeekPlayTime, &userServerId);

    if (accountId == 0) {
        CSPointer<User> user = g_UserMap.FindObject(userServerId);
        if (user) {
            char16_t context[] = u"SYS";
            user->GetSocket()->SendSystemMessage(context, u"Change time limit fail.");
        }
        return false;
    }

    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account != accountMap.end()) {
            account->second->m_nWeekPlayTime = newWeekPlayTime;
        }
    }

    CSPointer<User> user = g_UserMap.FindObject(userServerId);
    if (user) {
        char16_t context[] = u"SYS";
        user->GetSocket()->SendSystemMessage(context, u"Change time limit success.");
    }
    return false;
}

CDBCallbackMap gPacketTableFromCache(&DummyPacket, u"RP_PROTOCOL");

namespace {

// Current L2Server.exe 0x00d85af4..0x00d879e8: all 199 primary
// CacheD reply registrations. Keep every explicit Bind so registration
// coverage, static initialization and the per-slot default replacement
// remain observable.
L2_BIND_PACKET(kBindPrimary000, gPacketTableFromCache, 0x00, &CacheVersionPacket);
L2_BIND_PACKET(kBindPrimary001, gPacketTableFromCache, 0x01, &LoadCharacterPacket);
L2_BIND_PACKET(kBindPrimary002, gPacketTableFromCache, 0x02, &LoadItemsPacket);
L2_BIND_PACKET(kBindPrimary003, gPacketTableFromCache, 0x03, &CreateCharacterSuccessPacket);
L2_BIND_PACKET(kBindPrimary004, gPacketTableFromCache, 0x04, &CreateCharacterFailurePacket);
L2_BIND_PACKET(kBindPrimary005, gPacketTableFromCache, 0x05, &CreateInitItemsPacket);
L2_BIND_PACKET(kBindPrimary006, gPacketTableFromCache, 0x06, &DeleteCharacterSuccessPacket);
L2_BIND_PACKET(kBindPrimary007, gPacketTableFromCache, 0x07, &DeleteCharacterFailurePacket);
L2_BIND_PACKET(kBindPrimary008, gPacketTableFromCache, 0x08, &ReplyLoadSummons);
L2_BIND_PACKET(kBindPrimary009, gPacketTableFromCache, 0x09, &LoadWarehouseItemsPacket);
L2_BIND_PACKET(kBindPrimary010, gPacketTableFromCache, 0x0a, &WithdrawWarehouseItemsPacket);
L2_BIND_PACKET(kBindPrimary011, gPacketTableFromCache, 0x0b, &ShortCutInfoPacket);
L2_BIND_PACKET(kBindPrimary012, gPacketTableFromCache, 0x0c, &DepositWarehouseItemsPacket);
L2_BIND_PACKET(kBindPrimary013, gPacketTableFromCache, 0x0d, &SellItemsPacket);
L2_BIND_PACKET(kBindPrimary014, gPacketTableFromCache, 0x0e, &BuyItemsPacket);
L2_BIND_PACKET(kBindPrimary015, gPacketTableFromCache, 0x0f, &DBPacketHandler::DropItemPacket);
L2_BIND_PACKET(kBindPrimary016, gPacketTableFromCache, 0x10, &UseItemPacket);
L2_BIND_PACKET(kBindPrimary017, gPacketTableFromCache, 0x11, &ReplyLoadPledge);
L2_BIND_PACKET(kBindPrimary018, gPacketTableFromCache, 0x12, &DummyPacket);
L2_BIND_PACKET(kBindPrimary019, gPacketTableFromCache, 0x13, &LoadQuestPacket);
L2_BIND_PACKET(kBindPrimary020, gPacketTableFromCache, 0x14, &ReplyLoadCastlePacket);
L2_BIND_PACKET(kBindPrimary021, gPacketTableFromCache, 0x15, &ReplyLoadAgitPacket);
L2_BIND_PACKET(kBindPrimary022, gPacketTableFromCache, 0x16, &ManipulateItemPacket);
L2_BIND_PACKET(kBindPrimary023, gPacketTableFromCache, 0x17, &ReplyOustPledgeMemberPacket);
L2_BIND_PACKET(kBindPrimary024, gPacketTableFromCache, 0x18, &AcquiredSkillList);
L2_BIND_PACKET(kBindPrimary025, gPacketTableFromCache, 0x19, &DummyPacket);
L2_BIND_PACKET(kBindPrimary026, gPacketTableFromCache, 0x1a, &DBPacketHandler::DestroyItemPacket);
L2_BIND_PACKET(kBindPrimary027, gPacketTableFromCache, 0x1b, &ReplyEnchantItem);
L2_BIND_PACKET(kBindPrimary028, gPacketTableFromCache, 0x1c, &ReplyEnchantItemFail);
L2_BIND_PACKET(kBindPrimary029, gPacketTableFromCache, 0x1d, &DummyPacket);
L2_BIND_PACKET(kBindPrimary030, gPacketTableFromCache, 0x1e, &DBPacketHandler::ReplyLoadFriendList);
L2_BIND_PACKET(kBindPrimary031, gPacketTableFromCache, 0x1f, &DBPacketHandler::ReplyEstablishFriendship);
L2_BIND_PACKET(kBindPrimary032, gPacketTableFromCache, 0x20, &ReplyBreakFriendship);
L2_BIND_PACKET(kBindPrimary033, gPacketTableFromCache, 0x21, &KickCharPacket);
L2_BIND_PACKET(kBindPrimary034, gPacketTableFromCache, 0x22, &UpdateSkillPacket);
L2_BIND_PACKET(kBindPrimary035, gPacketTableFromCache, 0x23, &UpdateQuestPacket);
L2_BIND_PACKET(kBindPrimary036, gPacketTableFromCache, 0x24, &UpdateItemPacket);
L2_BIND_PACKET(kBindPrimary037, gPacketTableFromCache, 0x25, &UpdateCharPacket);
L2_BIND_PACKET(kBindPrimary038, gPacketTableFromCache, 0x26, &DummyPacket);
L2_BIND_PACKET(kBindPrimary039, gPacketTableFromCache, 0x27, &UpdateCharPledgePacket);
L2_BIND_PACKET(kBindPrimary040, gPacketTableFromCache, 0x28, &DBPacketHandler::ReplyTrade);
L2_BIND_PACKET(kBindPrimary041, gPacketTableFromCache, 0x29, &FriendRemoveInfo);
L2_BIND_PACKET(kBindPrimary042, gPacketTableFromCache, 0x2a, &TeleportCharPacket);
L2_BIND_PACKET(kBindPrimary043, gPacketTableFromCache, 0x2b, &GetPunishPacket);
L2_BIND_PACKET(kBindPrimary044, gPacketTableFromCache, 0x2c, &GetLoginAnnouncePacket);
L2_BIND_PACKET(kBindPrimary045, gPacketTableFromCache, 0x2d, &LoadSelectedCharacterPacket);
L2_BIND_PACKET(kBindPrimary046, gPacketTableFromCache, 0x2e, &DBPacketHandler::ReplyUpdateUserPledge);
L2_BIND_PACKET(kBindPrimary047, gPacketTableFromCache, 0x2f, &ReplyDeletePledge);
L2_BIND_PACKET(kBindPrimary048, gPacketTableFromCache, 0x30, &ReplySetPledgeCrest);
L2_BIND_PACKET(kBindPrimary049, gPacketTableFromCache, 0x31, &DummyPacket);
L2_BIND_PACKET(kBindPrimary050, gPacketTableFromCache, 0x32, &DummyPacket);
L2_BIND_PACKET(kBindPrimary051, gPacketTableFromCache, 0x33, &DummyPacket);
L2_BIND_PACKET(kBindPrimary052, gPacketTableFromCache, 0x34, &ReplySetUserNickname);
L2_BIND_PACKET(kBindPrimary053, gPacketTableFromCache, 0x35, &DummyPacket);
L2_BIND_PACKET(kBindPrimary054, gPacketTableFromCache, 0x36, &ReplyGetPledgeCrest);
L2_BIND_PACKET(kBindPrimary055, gPacketTableFromCache, 0x37, &DummyPacket);
L2_BIND_PACKET(kBindPrimary056, gPacketTableFromCache, 0x38, &ReplyLoadPledgeWar);
L2_BIND_PACKET(kBindPrimary057, gPacketTableFromCache, 0x39, &ReplyCreatePledgeByNpc);
L2_BIND_PACKET(kBindPrimary058, gPacketTableFromCache, 0x3a, &ReplyDeletePledgeByTimer);
L2_BIND_PACKET(kBindPrimary059, gPacketTableFromCache, 0x3b, &ReplyResetUserNickname);
L2_BIND_PACKET(kBindPrimary060, gPacketTableFromCache, 0x3c, &ReplyLoadDismissReservedPledge);
L2_BIND_PACKET(kBindPrimary061, gPacketTableFromCache, 0x3d, &DummyPacket);
L2_BIND_PACKET(kBindPrimary062, gPacketTableFromCache, 0x3e, &ReplyPetiGetCharList);
L2_BIND_PACKET(kBindPrimary063, gPacketTableFromCache, 0x3f, &ShowPetitionMsg);
L2_BIND_PACKET(kBindPrimary064, gPacketTableFromCache, 0x40, &ReplyManageBattleCamp);
L2_BIND_PACKET(kBindPrimary065, gPacketTableFromCache, 0x41, &ReplyLoadBattleCamp);
L2_BIND_PACKET(kBindPrimary066, gPacketTableFromCache, 0x42, &DBPacketHandler::ReplyPrepareCastleWar);
L2_BIND_PACKET(kBindPrimary067, gPacketTableFromCache, 0x43, &ReplyViewSiegeList);
L2_BIND_PACKET(kBindPrimary068, gPacketTableFromCache, 0x44, &ReplyApproveBattle);
L2_BIND_PACKET(kBindPrimary069, gPacketTableFromCache, 0x45, &ReplyConfirmCastleDefence);
L2_BIND_PACKET(kBindPrimary070, gPacketTableFromCache, 0x46, &DBPacketHandler::ReplyQueryCastleSiegeForRegister);
L2_BIND_PACKET(kBindPrimary071, gPacketTableFromCache, 0x47, &ReplyQueryCastleSiegeForUnregister);
L2_BIND_PACKET(kBindPrimary072, gPacketTableFromCache, 0x48, &ReplyQueryCastleSiegeForInfo);
L2_BIND_PACKET(kBindPrimary073, gPacketTableFromCache, 0x49, &ReplyQueryCastleSiegeForDismiss);
L2_BIND_PACKET(kBindPrimary074, gPacketTableFromCache, 0x4a, &ReplyDelPledgeCrestPacket);
L2_BIND_PACKET(kBindPrimary075, gPacketTableFromCache, 0x4b, &ReplyModPledgeNamePacket);
L2_BIND_PACKET(kBindPrimary076, gPacketTableFromCache, 0x4c, &PartyTelBookMarkPacket);
L2_BIND_PACKET(kBindPrimary077, gPacketTableFromCache, 0x4d, &BuilderList);
L2_BIND_PACKET(kBindPrimary078, gPacketTableFromCache, 0x4e, &ReplyLoadDoor);
L2_BIND_PACKET(kBindPrimary079, gPacketTableFromCache, 0x4f, &ReplyLoadWarehouseItemList);
L2_BIND_PACKET(kBindPrimary080, gPacketTableFromCache, 0x50, &ReplyCrystallizeItem);
L2_BIND_PACKET(kBindPrimary081, gPacketTableFromCache, 0x51, &ReplyInstallAgitDeco);
L2_BIND_PACKET(kBindPrimary082, gPacketTableFromCache, 0x52, &ReplyContributionWinnerPledge);
L2_BIND_PACKET(kBindPrimary083, gPacketTableFromCache, 0x53, &ReplyGetContributionRelatedPledge);
L2_BIND_PACKET(kBindPrimary084, gPacketTableFromCache, 0x54, &ReplyPrivateStoreCommit);
L2_BIND_PACKET(kBindPrimary085, gPacketTableFromCache, 0x55, &PingTest);
L2_BIND_PACKET(kBindPrimary086, gPacketTableFromCache, 0x56, &PingOneAdenaDrop);
L2_BIND_PACKET(kBindPrimary087, gPacketTableFromCache, 0x57, &PingNetwork);
L2_BIND_PACKET(kBindPrimary088, gPacketTableFromCache, 0x58, &ReplyLoadAbnormalStatus);
L2_BIND_PACKET(kBindPrimary089, gPacketTableFromCache, 0x59, &ReplyCreateAllianceByNpc);
L2_BIND_PACKET(kBindPrimary090, gPacketTableFromCache, 0x5a, &ReplyJoinAlliance);
L2_BIND_PACKET(kBindPrimary091, gPacketTableFromCache, 0x5b, &ReplyDismissAlliance);
L2_BIND_PACKET(kBindPrimary092, gPacketTableFromCache, 0x5c, &ReplyOustAllianceMemberPledge);
L2_BIND_PACKET(kBindPrimary093, gPacketTableFromCache, 0x5d, &ReplyWithdrawAlliance);
L2_BIND_PACKET(kBindPrimary094, gPacketTableFromCache, 0x5e, &ReplyCreatePet);
L2_BIND_PACKET(kBindPrimary095, gPacketTableFromCache, 0x5f, &ReplyDeletePet);
L2_BIND_PACKET(kBindPrimary096, gPacketTableFromCache, 0x60, &ReplyEvolvePet);
L2_BIND_PACKET(kBindPrimary097, gPacketTableFromCache, 0x61, &DBPacketHandler::ReplyWithdrawPet);
L2_BIND_PACKET(kBindPrimary098, gPacketTableFromCache, 0x62, &DBPacketHandler::ReplyLoadPetItems);
L2_BIND_PACKET(kBindPrimary099, gPacketTableFromCache, 0x63, &DBPacketHandler::ReplyDepositPet);
L2_BIND_PACKET(kBindPrimary100, gPacketTableFromCache, 0x64, &ReplyGiveItemToPet);
L2_BIND_PACKET(kBindPrimary101, gPacketTableFromCache, 0x65, &DummyPacket);
L2_BIND_PACKET(kBindPrimary102, gPacketTableFromCache, 0x66, &ReplyPetGetItem);
L2_BIND_PACKET(kBindPrimary103, gPacketTableFromCache, 0x67, &ReplyPetDropItem);
L2_BIND_PACKET(kBindPrimary104, gPacketTableFromCache, 0x68, &ReplyPetNameChange);
L2_BIND_PACKET(kBindPrimary105, gPacketTableFromCache, 0x69, &ReplyPetUseItem);
L2_BIND_PACKET(kBindPrimary106, gPacketTableFromCache, 0x6a, &ReplySetAllianceCrest);
L2_BIND_PACKET(kBindPrimary107, gPacketTableFromCache, 0x6b, &ReplyGetAllianceCrest);
L2_BIND_PACKET(kBindPrimary108, gPacketTableFromCache, 0x6c, &DummyPacket);
L2_BIND_PACKET(kBindPrimary109, gPacketTableFromCache, 0x6d, &ReplyLoadAlliance);
L2_BIND_PACKET(kBindPrimary110, gPacketTableFromCache, 0x6e, &DummyPacket);
L2_BIND_PACKET(kBindPrimary111, gPacketTableFromCache, 0x6f, &ReplyGetDbRelatedCounts);
L2_BIND_PACKET(kBindPrimary112, gPacketTableFromCache, 0x70, &RecipeMakeItem);
L2_BIND_PACKET(kBindPrimary113, gPacketTableFromCache, 0x71, &DBPacketHandler::PickItem);
L2_BIND_PACKET(kBindPrimary114, gPacketTableFromCache, 0x72, &DummyPacket);
L2_BIND_PACKET(kBindPrimary115, gPacketTableFromCache, 0x73, &DummyPacket);
L2_BIND_PACKET(kBindPrimary116, gPacketTableFromCache, 0x74, &DummyPacket);
L2_BIND_PACKET(kBindPrimary117, gPacketTableFromCache, 0x75, &DummyPacket);
L2_BIND_PACKET(kBindPrimary118, gPacketTableFromCache, 0x76, &DummyPacket);
L2_BIND_PACKET(kBindPrimary119, gPacketTableFromCache, 0x77, &DummyPacket);
L2_BIND_PACKET(kBindPrimary120, gPacketTableFromCache, 0x78, &ReplyPackageLoadTargetList);
L2_BIND_PACKET(kBindPrimary121, gPacketTableFromCache, 0x79, &ReplyPackageSendToTarget);
L2_BIND_PACKET(kBindPrimary122, gPacketTableFromCache, 0x7a, &DBPacketHandler::ReplyAddBlockList);
L2_BIND_PACKET(kBindPrimary123, gPacketTableFromCache, 0x7b, &ReplyDeleteBlockList);
L2_BIND_PACKET(kBindPrimary124, gPacketTableFromCache, 0x7c, &ReplyBlockList);
L2_BIND_PACKET(kBindPrimary125, gPacketTableFromCache, 0x7d, &ReplyWarehouseItemsEx);
L2_BIND_PACKET(kBindPrimary126, gPacketTableFromCache, 0x7e, &ReplyWithdrawWarehouseItemsEx);
L2_BIND_PACKET(kBindPrimary127, gPacketTableFromCache, 0x7f, &ReplyDepositWarehouseItemsEx);
L2_BIND_PACKET(kBindPrimary128, gPacketTableFromCache, 0x80, &DBPacketHandler::ReplySetNextCastleSiege);
L2_BIND_PACKET(kBindPrimary129, gPacketTableFromCache, 0x81, &ReplyCreateAgitAuction);
L2_BIND_PACKET(kBindPrimary130, gPacketTableFromCache, 0x82, &ReplyCreateAgitBid);
L2_BIND_PACKET(kBindPrimary131, gPacketTableFromCache, 0x83, &ReplySetAgitAuction);
L2_BIND_PACKET(kBindPrimary132, gPacketTableFromCache, 0x84, &ReplyLoadCastleGuard);
L2_BIND_PACKET(kBindPrimary133, gPacketTableFromCache, 0x85, &ReplyLoadAgitBid);
L2_BIND_PACKET(kBindPrimary134, gPacketTableFromCache, 0x86, &ReplyLoadControlTower);
L2_BIND_PACKET(kBindPrimary135, gPacketTableFromCache, 0x87, &ReplyLoadMercenary);
L2_BIND_PACKET(kBindPrimary136, gPacketTableFromCache, 0x88, &ReplySaveMercenary);
L2_BIND_PACKET(kBindPrimary137, gPacketTableFromCache, 0x89, &ReplyAgitPledgeAdena);
L2_BIND_PACKET(kBindPrimary138, gPacketTableFromCache, 0x8a, &ReplyCancelAgitAuction);
L2_BIND_PACKET(kBindPrimary139, gPacketTableFromCache, 0x8b, &ReplyAgitCost);
L2_BIND_PACKET(kBindPrimary140, gPacketTableFromCache, 0x8c, &ReplyShowIntervalAnnounce);
L2_BIND_PACKET(kBindPrimary141, gPacketTableFromCache, 0x8d, &ReplySetIntervalAnnounce);
L2_BIND_PACKET(kBindPrimary142, gPacketTableFromCache, 0x8e, &ReplyDelIntervalAnnounce);
L2_BIND_PACKET(kBindPrimary143, gPacketTableFromCache, 0x8f, &ReplyAgitAutoSell);
L2_BIND_PACKET(kBindPrimary144, gPacketTableFromCache, 0x90, &ReplyAgitBidCancel);
L2_BIND_PACKET(kBindPrimary145, gPacketTableFromCache, 0x91, &ReplyLoadIntervalAnnounce);
L2_BIND_PACKET(kBindPrimary146, gPacketTableFromCache, 0x92, &DummyPacket);
L2_BIND_PACKET(kBindPrimary147, gPacketTableFromCache, 0x93, &ReplyGetCharDBIDByName);
L2_BIND_PACKET(kBindPrimary148, gPacketTableFromCache, 0x94, &ReplyAdminTool);
L2_BIND_PACKET(kBindPrimary149, gPacketTableFromCache, 0x95, &ReplyLoadLastChangeResidence);
L2_BIND_PACKET(kBindPrimary150, gPacketTableFromCache, 0x96, &ReplyMngComment);
L2_BIND_PACKET(kBindPrimary151, gPacketTableFromCache, 0x97, &ReplyLoadDayPlayTime);
L2_BIND_PACKET(kBindPrimary152, gPacketTableFromCache, 0x98, &ReplyBookMark);
L2_BIND_PACKET(kBindPrimary153, gPacketTableFromCache, 0x99, &ReplySavePledgePower);
L2_BIND_PACKET(kBindPrimary154, gPacketTableFromCache, 0x9a, &ReplyLoadTeamBattleAgit);
L2_BIND_PACKET(kBindPrimary155, gPacketTableFromCache, 0x9b, &ReplyNewTeamBattleAgitPledge);
L2_BIND_PACKET(kBindPrimary156, gPacketTableFromCache, 0x9c, &ReplyNewTeamBattleAgitMember);
L2_BIND_PACKET(kBindPrimary157, gPacketTableFromCache, 0x9d, &ReplyTeamBattleAgitStatus);
L2_BIND_PACKET(kBindPrimary158, gPacketTableFromCache, 0x9e, &ReplyChangeTimeLimit);
L2_BIND_PACKET(kBindPrimary159, gPacketTableFromCache, 0x9f, &ReplyUnregisterTeamBattleAgitPledge);
L2_BIND_PACKET(kBindPrimary160, gPacketTableFromCache, 0xa0, &ReplyUnregisterTeamBattleAgitMember);
L2_BIND_PACKET(kBindPrimary161, gPacketTableFromCache, 0xa1, &ReplyMonRace);
L2_BIND_PACKET(kBindPrimary162, gPacketTableFromCache, 0xa2, &ReplyLoadSiegeAgit);
L2_BIND_PACKET(kBindPrimary163, gPacketTableFromCache, 0xa3, &DBPacketHandler::ReplyRegisterSiegeAgitPledge);
L2_BIND_PACKET(kBindPrimary164, gPacketTableFromCache, 0xa4, &ReplyUnregisterSiegeAgitPledge);
L2_BIND_PACKET(kBindPrimary165, gPacketTableFromCache, 0xa5, &ReplySetTeamBattleStatus);
L2_BIND_PACKET(kBindPrimary166, gPacketTableFromCache, 0xa6, &ReplySetTeamBattleWinner);
L2_BIND_PACKET(kBindPrimary167, gPacketTableFromCache, 0xa7, &ReplyUseRecipeItem);
L2_BIND_PACKET(kBindPrimary168, gPacketTableFromCache, 0xa8, &ReplySaveRecipeBookInfo);
L2_BIND_PACKET(kBindPrimary169, gPacketTableFromCache, 0xa9, &ReplyDeleteRecipeItem);
L2_BIND_PACKET(kBindPrimary170, gPacketTableFromCache, 0xaa, &ReplyLoadRecipeItem);
L2_BIND_PACKET(kBindPrimary171, gPacketTableFromCache, 0xab, &ReplyRecipeShopMakeItem);
L2_BIND_PACKET(kBindPrimary172, gPacketTableFromCache, 0xac, &ReplyEquipHenna);
L2_BIND_PACKET(kBindPrimary173, gPacketTableFromCache, 0xad, &ReplyUnequipHenna);
L2_BIND_PACKET(kBindPrimary174, gPacketTableFromCache, 0xae, &ReplyManMacro);
L2_BIND_PACKET(kBindPrimary175, gPacketTableFromCache, 0xaf, &ReplyManMemo);
L2_BIND_PACKET(kBindPrimary176, gPacketTableFromCache, 0xb0, &ReplyManPledgeBBS);
L2_BIND_PACKET(kBindPrimary177, gPacketTableFromCache, 0xb1, &ReplyManorBuySeed);
L2_BIND_PACKET(kBindPrimary178, gPacketTableFromCache, 0xb2, &ReplyManorSellCrop);
L2_BIND_PACKET(kBindPrimary179, gPacketTableFromCache, 0xb3, &ReplyManorInfoAndData);
L2_BIND_PACKET(kBindPrimary180, gPacketTableFromCache, 0xb4, &ReplyEventPoint);
L2_BIND_PACKET(kBindPrimary181, gPacketTableFromCache, 0xb5, &ReplyClanBBSList);
L2_BIND_PACKET(kBindPrimary182, gPacketTableFromCache, 0xb6, &ReplyModAllianceNamePacket);
L2_BIND_PACKET(kBindPrimary183, gPacketTableFromCache, 0xb7, &DummyPacket);
L2_BIND_PACKET(kBindPrimary184, gPacketTableFromCache, 0xb8, &ReplyCreateSubJob);
L2_BIND_PACKET(kBindPrimary185, gPacketTableFromCache, 0xb9, &ReplyDeleteSubJob);
L2_BIND_PACKET(kBindPrimary186, gPacketTableFromCache, 0xba, &DBPacketHandler::ReplyChangeSubJob);
L2_BIND_PACKET(kBindPrimary187, gPacketTableFromCache, 0xbb, &ReplyLoadSubJob);
L2_BIND_PACKET(kBindPrimary188, gPacketTableFromCache, 0xbc, &DummyPacket);
L2_BIND_PACKET(kBindPrimary189, gPacketTableFromCache, 0xbd, &DBPacketHandler::ReplyReloadSkill);
L2_BIND_PACKET(kBindPrimary190, gPacketTableFromCache, 0xbe, &AdminToolAnnouncePacket);
L2_BIND_PACKET(kBindPrimary191, gPacketTableFromCache, 0xbf, &ReplySendSystemMessage);
L2_BIND_PACKET(kBindPrimary192, gPacketTableFromCache, 0xc0, &ReplyDxPacket);
L2_BIND_PACKET(kBindPrimary193, gPacketTableFromCache, 0xc1, &ReplyQueryPacket);
L2_BIND_PACKET(kBindPrimary194, gPacketTableFromCache, 0xc2, &ManipulatePointItemPacket);
L2_BIND_PACKET(kBindPrimary195, gPacketTableFromCache, 0xc3, &ReportPacketLog);
L2_BIND_PACKET(kBindPrimary196, gPacketTableFromCache, 0xc4, &ReplyLoadSuspendPledgeWar);
L2_BIND_PACKET(kBindPrimary197, gPacketTableFromCache, 0xc5, &ReplyLoadMainClassSkill);
L2_BIND_PACKET(kBindPrimary198, gPacketTableFromCache, 0xc6, &ReplyExPacket);

} // namespace

CDBCallback* dbPacketTable = gPacketTableFromCache.GetData();

L2_DEFINE_OBJECT_CLASS_ID(CDBSocket)

L2_DEFINE_OBJECT_CLASS_NAME(CDBSocket, m_pClassName_CDBSocket)

L2_DEFINE_OBJECT_NEW(CDBSocket)

L2_DEFINE_OBJECT_ALLOCATOR(CDBSocket, m_pClassName_CDBSocket)

CDBSocket::CDBSocket(std::uint64_t socket, CDBCallback* packetTable) :
    ::CIOSocket(socket, 0x1000),
    m_socket(socket),
    m_threadCount(0),
    m_packetTable(packetTable),
    m_mode(::SocketMode::SM_READ_LEN),
    m_packetLength(0) {
    L2_GUARD_FUNCTION();
    if (m_packetTable == MainPacketTable()) {
        // Noise: CDB::CreateDBSocket already reports the TCP connection and configured peer. Constructing its wrapper is not
        // another connection or an error.
        // ::LogIOError(
        //     L"DBSocket open %x(%x)",
        //     static_cast<unsigned int>(m_socket),
        //     static_cast<unsigned int>(
        //         reinterpret_cast<std::uintptr_t>(this)))
    } else {
        // ::LogIOError(L"Invalid DBPacketTable")
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Connection %llu was constructed with an unrecognized reply table. Initialization continues.",
        //     static_cast<unsigned long long>(m_socket))
        L2_LOG_ERROR(
            Log,
            "[database connection] Connection %llu was constructed with an unrecognized reply table; initialization "
            "continues.",
            static_cast<unsigned long long>(m_socket)
        );
    }
}

CDBSocket::~CDBSocket() { m_packetTable = nullptr; }

L2_DEFINE_OBJECT_RELEASE(CDBSocket)

bool CDBSocket::CheckPendingWrite() {
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // ::LogIOError(
        //     L"[%s][%d] Write to invalid db socket.",
        //     L"DBSocket.cpp",
        //     0x48d5)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Rejecting a write on closed connection %llu. The caller will close the socket.",
        //     static_cast<unsigned long long>(m_socket))
        L2_LOG_ERROR(
            Log, "[database connection] Rejecting a write on closed connection %llu; the caller will close the socket.",
            static_cast<unsigned long long>(m_socket)
        );
        // Noise: Duplicate of the invalid-socket failure reported immediately above.
        // ::LogIOError(
        //     L"[%s][%d] Write to invalid db socket.",
        //     L"DBSocket.cpp",
        //     0x48d6)
        return false;
    }
    // if (m_nPendingWrite.load(std::memory_order_relaxed) < 0x800001) {
    //     return true
    // }
    const std::int32_t pendingWrite = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pendingWrite < 0x800001) {
        return true;
    }
    // ::LogIOError(
    //     L"dbsocket pending write > %d KB, Close!",
    //     0x2000)
    // Log.Add(LOG_ERROR,
    //     "[CacheD] Rejecting a write on connection %llu: pending writes reached %d bytes (limit 8388608). The caller will
    //     close the socket.", static_cast<unsigned long long>(m_socket), static_cast<int>(pendingWrite))
    L2_LOG_ERROR(
        Log,
        "[database connection] Rejecting a write on connection %llu: pending writes reached %d bytes (limit 8388608); the "
        "caller will close the socket.",
        static_cast<unsigned long long>(m_socket), static_cast<int>(pendingWrite)
    );
    // Noise: Duplicate of the pending-write limit failure reported immediately above. The call-stack report below is
    // kept.
    // ::LogIOError(
    //     L"dbsocket pending write > %d KB, Close!",
    //     0x2000)
    runtime::process::ReportCallStack(10, false);
    return false;
}

void CDBSocket::OnClose() {
    L2_GUARD_FUNCTION();
    GuardHelper closed(L"CacheSocket Closed", CThreadManager::GetThreadID());
    // ::LogIOError(
    //     L"DBSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     CurrentLastError())
    // Log.Add(LOG_ERROR,
    //     "[CacheD] Connection %llu closed. L2Server is terminating. Last OS error observed: %d. This saved value may not
    //     identify the cause.", static_cast<unsigned long long>(m_socket), CurrentLastError())
    L2_LOG_ERROR(
        Log,
        "[database connection] Connection %llu closed; L2Server is terminating. Last OS error observed: %d; this saved "
        "value may not identify the cause.",
        static_cast<unsigned long long>(m_socket), CurrentLastError()
    );
    // Noise: Duplicate of the fatal-close record immediately above. The original termination policy is kept.
    // ::LogIOError(
    //     L"DBSocket Close %x(%x). GetLastError(%d)",
    //     static_cast<unsigned int>(m_socket),
    //     static_cast<unsigned int>(
    //         reinterpret_cast<std::uintptr_t>(this)),
    //     CurrentLastError())

    // Current L2Server writes its process-global crash metadata here and
    // enters a non-returning crash routine. abort is the portable process
    // boundary. Returning would change the shipping close meaning.
    std::abort();
}

void CDBSocket::Send(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    SendV(format, arguments);
    va_end(arguments);
}

void CDBSocket::SendV(const char* format, va_list arguments) {
    L2_GUARD_FUNCTION();
    if (m_mode == ::SocketMode::SM_CLOSE) {
        return;
    }

    ::DefaultIOBuffer* const buffer = ::DefaultIOBuffer::Alloc();
    const int bodySize = VAssemble(reinterpret_cast<char*>(buffer->m_Buffer.data() + 3), 0x4ffd, format, arguments);
    if (bodySize == 0) {
        // ::LogIOError(
        //     L"%d: assemble too large packet. format %s",
        //     LegacySocketSymbolForLog(),
        //     format)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Could not assemble an outbound packet on connection %llu: formatter returned 0 (capacity 20477
        //     bytes, format %s). A zero-length buffer is passed to the writer.", static_cast<unsigned long long>(m_socket),
        //     format != nullptr ? format : "(null)")
        L2_LOG_ERROR(
            Log,
            "[database connection] Could not assemble an outbound packet on connection %llu: formatter returned 0 (capacity "
            "20477 bytes, format %s). A zero-length buffer is passed to the writer.",
            static_cast<unsigned long long>(m_socket), format != nullptr ? format : "(null)"
        );
        buffer->m_dwSize = 0;
    } else {
        std::uint16_t frameSize = static_cast<std::uint16_t>(bodySize + 3);
        if (m_packetTable == MainPacketTable()) {
            frameSize = static_cast<std::uint16_t>(bodySize + 5);
            StoreLittleEndian16(buffer->m_Buffer.data() + frameSize - 2, frameSize);
        }
        StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
        buffer->m_Buffer[2] = static_cast<std::uint8_t>(runtime::process::CurrentWorldId());
        buffer->m_dwSize = frameSize;
    }
    Write(buffer);
}

void CDBSocket::Send(::RQ_PROTOCOL protocol, ::RQ_EX_PROTOCOL extendedProtocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (m_mode == ::SocketMode::SM_CLOSE) {
        return;
    }
    if (m_packetTable != MainPacketTable()) {
        // ::LogIOError(
        //     L"[%s][%d] Invalid DBPacketTable.",
        //     L"DBSocket.cpp",
        //     0x4885)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Outbound protocol %d (extended %d) was not sent on connection %llu: reply table is not the primary
        //     table.", static_cast<int>(protocol), static_cast<int>(extendedProtocol), static_cast<unsigned long
        //     long>(m_socket))
        L2_LOG_ERROR(
            Log,
            "[database protocol] Outbound protocol %d (extended %d) was not sent on connection %llu: reply table is not the "
            "primary table.",
            static_cast<int>(protocol), static_cast<int>(extendedProtocol), static_cast<unsigned long long>(m_socket)
        );
        return;
    }

    ::DefaultIOBuffer* const buffer = ::DefaultIOBuffer::Alloc();
    const std::int32_t protocolValue = static_cast<std::int32_t>(protocol);
    const int headerSize = protocolValue == 0xea ? 6 : 4;
    network::Stream stream(buffer->m_Buffer.data() + headerSize, 0x5000 - headerSize);
    // if (!packet.Encode(&stream)) {
    //     buffer->Release()
    //     return.
    // }
    if (!packet.Encode(&stream)) {
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Outbound protocol %d (extended %d) was not sent on connection %llu: encoding failed (capacity %d
        //     bytes), releasing its buffer.", protocolValue, static_cast<int>(extendedProtocol), static_cast<unsigned long
        //     long>(m_socket), 0x5000 - headerSize)
        L2_LOG_ERROR(
            Log,
            "[database protocol] Outbound protocol %d (extended %d) was not sent on connection %llu: encoding failed "
            "(capacity %d bytes); releasing its buffer.",
            protocolValue, static_cast<int>(extendedProtocol), static_cast<unsigned long long>(m_socket), 0x5000 - headerSize
        );
        buffer->Release();
        return;
    }

    const std::ptrdiff_t payloadSize = stream.GetCursor() - (buffer->m_Buffer.data() + headerSize);
    const std::uint16_t frameSize = static_cast<std::uint16_t>(headerSize + payloadSize + 2);
    const int worldId = runtime::process::CurrentWorldId();
    if (worldId < 0x100) {
        StoreLittleEndian16(buffer->m_Buffer.data(), frameSize);
        buffer->m_Buffer[2] = static_cast<std::uint8_t>(worldId);
        buffer->m_Buffer[3] = static_cast<std::uint8_t>(protocolValue);
        if (protocolValue == 0xea) {
            StoreLittleEndian16(
                buffer->m_Buffer.data() + 4, static_cast<std::uint16_t>(static_cast<std::int32_t>(extendedProtocol))
            );
        }
        StoreLittleEndian16(buffer->m_Buffer.data() + frameSize - 2, frameSize);
        buffer->m_dwSize = frameSize;
        Write(buffer);
    } else {
        // The current branch does not release the freshly allocated buffer.
        // ::LogIOError(
        //     L"[%s][%d] WorldId is too large, header cannot be written.",
        //     L"DBSocket.cpp",
        //     0x48a5)
        // Log.Add(LOG_ERROR,
        //     "[CacheD] Outbound protocol %d (extended %d) was not sent on connection %llu: routing world ID %d is at least
        //     256, so its header cannot be written.", protocolValue, static_cast<int>(extendedProtocol),
        //     static_cast<unsigned long long>(m_socket), worldId)
        L2_LOG_ERROR(
            Log,
            "[database protocol] Outbound protocol %d (extended %d) was not sent on connection %llu: routing world ID %d is "
            "at least 256, so its header cannot be written.",
            protocolValue, static_cast<int>(extendedProtocol), static_cast<unsigned long long>(m_socket), worldId
        );
    }
}

void CDBSocket::OnRead() {
    L2_GUARD_FUNCTION();
    if (m_mode == ::SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    const std::int32_t readSize = static_cast<std::int32_t>(m_pReadBuf->m_dwSize);
    std::int32_t packetOffset = 0;
    while (packetOffset < readSize) {
        if (m_mode == ::SocketMode::SM_READ_LEN) {
            if (readSize < packetOffset + 3) {
                Read(readSize - packetOffset);
                return;
            }
            const std::uint8_t* const header = m_pReadBuf->m_Buffer.data() + packetOffset;
            m_packetLength = static_cast<std::int32_t>(
                                 static_cast<std::uint32_t>(header[0]) | (static_cast<std::uint32_t>(header[1]) << 8U)
                             ) -
                3;
            runtime::process::SetCurrentDatabaseId(header[2]);
            if (m_packetLength < 1 || m_packetLength > 0x4ffd) {
                // ::LogIOError(
                //     L"dbsocket %d: bad packet size %d",
                //     LegacySocketSymbolForLog(),
                //     m_packetLength)
                // Log.Add(LOG_ERROR,
                //     "[CacheD] Closing connection %llu: incoming body length %d bytes is outside 1..20477.",
                //     static_cast<unsigned long long>(m_socket), static_cast<int>(m_packetLength))
                L2_LOG_ERROR(
                    Log, "[database protocol] Closing connection %llu: incoming body length %d bytes is outside 1..20477.",
                    static_cast<unsigned long long>(m_socket), static_cast<int>(m_packetLength)
                );
                m_mode = ::SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_mode = ::SocketMode::SM_READ_BODY;
            packetOffset += 3;
            // } else if (m_mode != ::SocketMode::SM_READ_BODY) {
            //     Close()
            //     return.
            // }
        } else if (m_mode != ::SocketMode::SM_READ_BODY) {
            // Log.Add(LOG_ERROR,
            //     "[CacheD] Closing connection %llu: unexpected read state %d.",
            //     static_cast<unsigned long long>(m_socket), static_cast<int>(m_mode))
            L2_LOG_ERROR(
                Log, "[database protocol] Closing connection %llu: unexpected read state %d.",
                static_cast<unsigned long long>(m_socket), static_cast<int>(m_mode)
            );
            Close();
            return;
        }

        if (readSize < packetOffset + m_packetLength) {
            Read(readSize - packetOffset);
            return;
        }

        const std::uint8_t opcode = m_pReadBuf->m_Buffer[static_cast<std::size_t>(packetOffset)];
        if (m_packetTable == MainPacketTable() && opcode > 198) {
            // ::LogIOError(
            //     L"[dbsocket]unknown protocol %d",
            //     static_cast<int>(opcode))
            // Log.Add(LOG_ERROR,
            //     "[CacheD] Closing connection %llu: incoming primary protocol %u is outside 0..198.",
            //     static_cast<unsigned long long>(m_socket), static_cast<unsigned int>(opcode))
            L2_LOG_ERROR(
                Log, "[database protocol] Closing connection %llu: incoming primary protocol %u is outside 0..198.",
                static_cast<unsigned long long>(m_socket), static_cast<unsigned int>(opcode)
            );
            m_mode = ::SocketMode::SM_CLOSE;
            Close();
            return;
        }

        CDBPacket* const packet = new CDBPacket;
        const int databaseId = runtime::process::CurrentDatabaseId();
        packet->m_nDatabaseID = databaseId != 0 ? databaseId : g_nWorldId;
        packet->m_pSocket = this;
        m_pReadBuf->AddRef();
        packet->m_pBuf = m_pReadBuf;
        packet->m_pFunc = m_packetTable[opcode];
        if (packet->m_pFunc == &DummyPacket) {
            // Log.Add(LOG_WARN,
            //     "[database protocol] Incoming primary reply %u on CacheD connection %llu selects a no-op handler. Its
            //     payload will be ignored.", static_cast<unsigned int>(opcode), static_cast<unsigned long long>(m_socket))
            L2_LOG_WARN(
                Log,
                "[database protocol] Incoming primary reply %u on CacheD connection %llu selects a no-op handler; its "
                "payload will be ignored.",
                static_cast<unsigned int>(opcode), static_cast<unsigned long long>(m_socket)
            );
        }
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        ::CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        packet->PostObject(packetOffset);

        packetOffset += m_packetLength;
        m_mode = ::SocketMode::SM_READ_LEN;
    }
    Read(0);
}

void CDBPacket::OnIOCallback(int success, std::uint32_t transferred, ::PortableOverlapped* overlapped) {
    ::g_Timer.BeginJob(::JOB_DBSOCKET__TOTAL);
    runtime::process::SetCurrentDatabaseId(m_nDatabaseID);
    CDBPacketBase::OnIOCallback(success, transferred, overlapped);
    ::g_Timer.EndJob(::JOB_DBSOCKET__TOTAL);
}
