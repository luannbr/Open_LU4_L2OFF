#include "NpcSocketEx.h"
#include "BeautyManager.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "Castle.h"
#include "Chair.h"
#include "CompeteAgit.h"
#include "Creature.h"
#include "DB.h"
#include "DynamicQuest.h"
#include "EventCampaign.h"
#include "FieldCycle.h"
#include "InstantZone.h"
#include "L2Time.h"
#include "NPC.h"
#include "NpcServer.h"
#include "NpcSocket.h"
#include "Party.h"
#include "Pledge.h"
#include "PledgeGameSystem.h"
#include "PledgeMission.h"
#include "PledgeRaid.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "Relay.h"
#include "Residence.h"
#include "SerializedMessage.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "UserUtil.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

namespace {

struct PkNC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE {
    std::int32_t values[10];
    std::array<char16_t, 256> strings[5];
};

const unsigned char* DisassemblePacket(
    const unsigned char* const packet, PkNC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE& value
) {
    // Current L2Server.exe 0x008c188c.
    L2_GUARD_FUNCTION();
    // The current image passes the second format fragment as the first
    // variadic argument instead of concatenating it. Preserve that literal
    // call-site oddity instead of repairing it here.
    return ::Disassemble(
        packet, "dddddddddd", "SSSSS", &value.values[0], &value.values[1], &value.values[2], &value.values[3],
        &value.values[4], &value.values[5], &value.values[6], &value.values[7], &value.values[8], &value.values[9], 0x200,
        value.strings[0].data(), 0x200, value.strings[1].data(), 0x200, value.strings[2].data(), 0x200,
        value.strings[3].data(), 0x200, value.strings[4].data()
    );
}

struct PkNC_EX_BROADCAST_FIELD_EVENT {
    std::int32_t npcSid;
    std::int32_t radius;
    std::int32_t eventArgument;
};

const unsigned char* DisassemblePacket(const unsigned char* const packet, PkNC_EX_BROADCAST_FIELD_EVENT& value) {
    // Current L2Server.exe 0x008c1a20.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &value.npcSid, &value.radius, &value.eventArgument);
}

struct PkNC_EX_CAST_BUFF_TO_RANDOM_USER {
    std::int32_t npcSid;
    std::int64_t packedSkill;
};

const unsigned char* DisassemblePacket(const unsigned char* const packet, PkNC_EX_CAST_BUFF_TO_RANDOM_USER& value) {
    // Current L2Server.exe 0x008c1adc.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "dQ", &value.npcSid, &value.packedSkill);
}

void WriteLOG_SKILL_CAST(
    CNPC* const npc, CSPointer<User> user, const int skillId, const int skillLevel, const int skillSubLevel
) {
    // Current L2Server.exe 0x008c4484.
    L2_GUARD_FUNCTION();
    const FVector position = npc->GetPos();
    char16_t firstNpcClass[64]{};
    char16_t secondNpcClass[64]{};
    const int firstStringClassId = npc->GetClassId();
    FormatUtf16To(firstNpcClass, std::size(firstNpcClass), u"%d", firstStringClassId);
    const int secondStringClassId = npc->GetClassId();
    FormatUtf16To(secondNpcClass, std::size(secondNpcClass), u"%d", secondStringClassId);
    const std::wstring accountName = Utf16ToWide(user->m_sAccountName.data());
    const int firstLoggedClassId = npc->GetClassId();
    const int secondLoggedClassId = npc->GetClassId();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,%s,%s,",
    //     std::int64_t{0x193},
    //     static_cast<std::int64_t>(secondLoggedClassId),
    //     static_cast<std::int64_t>(firstLoggedClassId),
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
    //     static_cast<std::int64_t>(skillId),
    //     static_cast<std::int64_t>(
    //         static_cast<std::int32_t>(
    //             (static_cast<std::uint32_t>(skillSubLevel) << 16) |
    //             (static_cast<std::uint32_t>(skillLevel) & 0xffffU))),
    //     std::int64_t{0},
    //     std::int64_t{0},
    //     firstNpcClass,
    //     secondNpcClass,
    //     user->GetName(),
    //     accountName.c_str())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,%s,%s,",
        std::int64_t{0x193}, static_cast<std::int64_t>(secondLoggedClassId), static_cast<std::int64_t>(firstLoggedClassId),
        static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
        static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
        static_cast<std::int64_t>(skillId),
        static_cast<std::int64_t>(static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(skillSubLevel) << 16) | (static_cast<std::uint32_t>(skillLevel) & 0xffffU)
        )),
        std::int64_t{0}, std::int64_t{0}, firstNpcClass, secondNpcClass, user->GetName(), accountName.c_str()
    );
}

} // namespace

NpcSocketExCallbackMap g_NPCSocketEx(&NpcSocketEx::DummyNpcSocketEx, u"NC_EX_PROTOCOL");

bool NpcExSocket(NpcSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x0089d628.
    L2_GUARD_FUNCTION();
    std::uint16_t opcode = 0;
    const unsigned char* const payload = ::Disassemble(packet, "h", &opcode);
    NpcSocketExCallback callback = &NpcSocketEx::DummyNpcSocketEx;
    if (opcode < NpcSocketExCallbackMap::SIZE) {
        callback = g_NPCSocketEx.GetCallback(static_cast<int>(opcode));
    }
    return callback(socket, payload);
}

namespace NpcSocketEx {

bool DummyNpcSocketEx(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    return true;
}

bool CheckWeightToChangeSubclass(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c1f24.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t argument = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &argument);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        const bool canChange = user->CanChangeSubClass(argument);
        g_npcserver.Send("cdddd", 0xb5, npcSid, userSid, canChange, argument);
    }
    return false;
}

bool IncreaseAPByNpc(NpcSocket*, const unsigned char*) {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] Deprecated Function",
    //     "NpcSocketEx.cpp",
    //     0x3d9)
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d] Deprecated Function",
    //     "NpcSocketEx.cpp",
    //     0x3d9)
    L2_LOG_WARN(Log, "Ignoring deprecated NPC request to increase AP; no ability points are changed.");
    return false;
}

bool ShowItemAttributionChange(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t ignored = 0;
    ::Disassemble(packet, "dd", &userSid, &ignored);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    if (creature && creature->IsPC()) {
        static_cast<void>(creature->CastUser());
    }
    return false;
}

bool SetUserLevel(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c214c.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t level = 0;
    ::Disassemble(packet, "dd", &userSid, &level);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->SetLevel(static_cast<unsigned int>(level));
    }
    return false;
}

bool GiveQuest(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c2244.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    ::Disassemble(packet, "dd", &userSid, &questId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->AcceptQuest(questId);
        user->SendQuestList(nullptr);
    }
    return false;
}

bool CompleteQuest(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c2348.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    ::Disassemble(packet, "dd", &userSid, &questId);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->CompleteQuest(questId);
    }
    return false;
}

bool StartJournal(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c2440.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    std::int32_t journalId = 0;
    ::Disassemble(packet, "ddd", &userSid, &questId, &journalId);
    if (questId < 1 || journalId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcSocketEx::StartJournal() - Invalid parameter. "
        //     L"questID:%d, Journal:%d",
        //     L"NpcSocketEx.cpp",
        //     0xb7,
        //     questId,
        //     journalId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NpcSocketEx::StartJournal() - Invalid parameter. "
        //     L"questID:%d, Journal:%d",
        //     L"NpcSocketEx.cpp",
        //     0xb7,
        //     questId,
        //     journalId)
        L2_LOG_WARN(
            Log,
            "Cannot start journal %d for quest %d and user SID %u: quest and journal IDs must both be positive; the request "
            "is skipped.",
            journalId, questId, static_cast<unsigned>(userSid)
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->StartJournal(questId, journalId);
    }
    return false;
}

bool CompleteJournal(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c259c. The target diagnostic intentionally
    // names StartJournal.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t questId = 0;
    std::int32_t journalId = 0;
    ::Disassemble(packet, "ddd", &userSid, &questId, &journalId);
    if (questId < 1 || journalId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] NpcSocketEx::StartJournal() - Invalid parameter. "
        //     L"questID:%d, Journal:%d",
        //     L"NpcSocketEx.cpp",
        //     0xd2,
        //     questId,
        //     journalId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] NpcSocketEx::StartJournal() - Invalid parameter. "
        //     L"questID:%d, Journal:%d",
        //     L"NpcSocketEx.cpp",
        //     0xd2,
        //     questId,
        //     journalId)
        L2_LOG_WARN(
            Log,
            "Cannot complete journal %d for quest %d and user SID %u: quest and journal IDs must both be positive; the "
            "request is skipped.",
            journalId, questId, static_cast<unsigned>(userSid)
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        user->CompleteJournal(questId, journalId);
    }
    return false;
}

bool SetShillienMoon(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c26f8.
    L2_GUARD_FUNCTION();
    std::int32_t state = 0;
    ::Disassemble(packet, "d", &state);
    g_L2Time.SetForceShillienMoon(state == 1);
    CUserBroadcaster::inst().BroadcastToAllUser("chd", 0xfe, 0x117, state);
    return false;
}

bool NpcResetCastle(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c12a8.
    L2_GUARD_FUNCTION();
    std::int32_t residenceId = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "dd", &residenceId, &userSid);
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr) {
        static_cast<CCastle*>(residence)->ResetCastleOwner(userSid);
    }
    return false;
}

bool NpcSetCastleType(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c1378.
    L2_GUARD_FUNCTION();
    std::int32_t residenceId = 0;
    std::int32_t castleType = 0;
    ::Disassemble(packet, "dd", &residenceId, &castleType);
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr) {
        static_cast<CCastle*>(residence)->SetCastleType(static_cast<CastleType::Enum>(castleType));
    }
    return false;
}

bool RegisterPledgeGameMaker(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c1448.
    L2_GUARD_FUNCTION();
    std::int32_t npcMakerId = 0;
    ::Disassemble(packet, "d", &npcMakerId);
    g_PledgeGameSystem.RegisterPledgeGameMaker(npcMakerId);
    return false;
}

bool NpcPledgeGameFunc(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c5030.
    L2_GUARD_FUNCTION();
    std::uint8_t function = 0;
    std::int32_t npcIndex = 0;
    std::int32_t userIndex = 0;
    std::int32_t fieldId = 0;
    ::Disassemble(packet, "cddd", &function, &npcIndex, &userIndex, &fieldId);
    CPledgeGameFuncHandler handler(function, npcIndex, userIndex, fieldId);
    if (handler.PreCheck()) {
        handler.Execute();
    }
    return false;
}

bool GiveItemViaPledgeGame(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c14fc.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t itemClassId = 0;
    ::Disassemble(packet, "dd", &userSid, &itemClassId);
    g_PledgeGameSystem.GiveItemToWinner(userSid, itemClassId, 0, false);
    return false;
}

bool SetGotItemPledgeGame(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x008c15c0.
    L2_GUARD_FUNCTION();
    g_PledgeGameSystem.SetGotItemPledgeGame();
    return false;
}

bool AddLogFromNPCEx(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c2984. NC_EX 0x0f keeps the
    // 0x803 branch's five output pointers for a four-field format.
    L2_GUARD_FUNCTION();
    std::int32_t logId = 0;
    packet = ::Disassemble(packet, "d", &logId);
    CLogFormatter formatter;

    if (logId == 0x8c) {
        std::int32_t userSid = 0;
        std::int32_t npcSid = 0;
        std::int32_t first = 0;
        std::int32_t second = 0;
        std::array<char16_t, 0x20> decoded{};
        ::Disassemble(packet, "ddddS", &userSid, &npcSid, &first, &second, 0x40, decoded.data());

        CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (user != nullptr && npc != nullptr) {
            const std::u16string text = Utf16Text(decoded.data()).String();
            formatter.WriteLog1_1(logId, user, npc, text.data(), nullptr, nullptr, first, second, 0, 0, 0, 0, 0);
        }
        return false;
    }

    if (logId == 0x14a || logId == 0x14b) {
        std::int32_t userSid = 0;
        ::Disassemble(packet, "d", &userSid);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            formatter.WriteLog2(logId, user, nullptr, 0, 0, 0, 0, 0, 0, 0);
        }
        return false;
    }

    if (logId == 0x14c || logId == 0x14d) {
        std::int32_t userSid = 0;
        ::Disassemble(packet, "d", &userSid);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        CParty* const party = user != nullptr ? user->GetParty() : nullptr;
        if (user != nullptr && party != nullptr) {
            const std::u16string memberNames = party->GetAllMemberName();
            const std::u16string hostMemberNames = Utf16Text(memberNames).String();
            formatter.WriteLog1(logId, user, nullptr, hostMemberNames.data(), nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0);
        }
        return false;
    }

    if (logId == 0x14e) {
        std::int32_t userSid = 0;
        std::int32_t first = 0;
        std::int32_t second = 0;
        ::Disassemble(packet, "ddd", &userSid, &first, &second);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            formatter.WriteLog2(logId, user, nullptr, first, second, 0, 0, 0, 0, 0);
        }
        return false;
    }

    if (logId == 0x14f) {
        std::int32_t userSid = 0;
        std::int32_t first = 0;
        std::int32_t second = 0;
        ::Disassemble(packet, "ddd", &userSid, &first, &second);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        CParty* const party = user != nullptr ? user->GetParty() : nullptr;
        if (user != nullptr && party != nullptr) {
            const std::u16string memberNames = party->GetAllMemberName();
            const std::u16string hostMemberNames = Utf16Text(memberNames).String();
            formatter.WriteLog1(
                logId, user, nullptr, hostMemberNames.data(), nullptr, nullptr, first, second, 0, 0, 0, 0, 0
            );
        }
        return false;
    }

    if (logId == 0x1f9f) {
        std::int32_t userSid = 0;
        std::int32_t first = 0;
        std::int32_t second = 0;
        ::Disassemble(packet, "ddd", &userSid, &first, &second);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            std::u16string memberNames;
            if (CParty* const party = user->GetParty()) {
                memberNames = party->GetAllMemberName();
            }
            const std::u16string hostMemberNames = Utf16Text(memberNames).String();
            formatter.WriteLog1_1(
                logId, user, nullptr, hostMemberNames.data(), nullptr, nullptr, first, second, 0, 0, 0, 0, 0
            );
        }
        return false;
    }

    if (logId == 0x802) {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t userSid = 0;
        ::Disassemble(packet, "dddd", &x, &y, &z, &userSid);
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        CPledge* const pledge = user != nullptr ? user->GetPledge() : nullptr;
        if (user != nullptr && pledge != nullptr) {
            const std::u16string pledgeName = std::u16string(pledge->GetPledgeName());
            formatter.WriteLog(
                logId, user->GetDBID(), user->GetAccountID(), 0, 0, x, y, z, pledgeName.c_str(), nullptr, nullptr,
                static_cast<int>(user->m_data->m_Race), static_cast<int>(user->m_data->m_Sex),
                static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel, user->GetSocialClass(), pledge->GetDbId(),
                pledge->GetPledgeLevel(), 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
            );
        }
        return false;
    }

    if (logId == 0x803) {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        std::int32_t firstUserSid = 0;
        std::int32_t secondUserSid = 0;
        ::Disassemble(packet, "dddd", &x, &y, &z, &firstUserSid, &secondUserSid);

        CCreature* const firstCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(firstUserSid));
        User* const firstUser = firstCreature != nullptr && firstCreature->IsPC() ? firstCreature->CastUser() : nullptr;
        CCreature* const secondCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(secondUserSid));
        User* const secondUser = secondCreature != nullptr && secondCreature->IsPC() ? secondCreature->CastUser() : nullptr;
        if (firstUser != nullptr && secondUser != nullptr) {
            CPledge* const firstPledge = firstUser->GetPledge();
            CPledge* const secondPledge = secondUser->GetPledge();
            if (firstPledge != nullptr) {
                const std::u16string firstPledgeName = std::u16string(firstPledge->GetPledgeName());
                const std::u16string secondPledgeName = std::u16string(secondPledge->GetPledgeName());
                formatter.WriteLog(
                    logId, secondUser->GetDBID(), secondUser->GetAccountID(), 0, 0, x, y, z, firstPledgeName.c_str(),
                    secondPledgeName.c_str(), nullptr, static_cast<int>(secondUser->m_data->m_Race),
                    static_cast<int>(secondUser->m_data->m_Sex), static_cast<int>(secondUser->m_data->m_nClass),
                    secondUser->m_data->m_nLevel, secondUser->GetSocialClass(), firstPledge->GetDbId(),
                    firstPledge->GetPledgeLevel(), secondPledge->GetDbId(), secondPledge->GetPledgeLevel(), 0, nullptr,
                    nullptr, nullptr, nullptr, 0
                );
            }
        }
    }
    return false;
}

bool NpcEventCampaignEx(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c643c.
    L2_GUARD_FUNCTION();
    std::uint8_t action = 0;
    std::int32_t userSid = 0;
    std::int32_t campaignId = 0;
    std::int32_t stepId = 0;
    std::int32_t goalGroupId = 0;
    std::int32_t goalId = 0;
    std::int32_t point = 0;
    ::Disassemble(packet, "cdddddd", &action, &userSid, &campaignId, &stepId, &goalGroupId, &goalId, &point);
    if (action == 2) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            CEventCampaignManager::Inst()->OnAddEventCampaignPoint(*user, campaignId, stepId, goalGroupId, goalId, point);
        }
    } else if (action == 3) {
        CEventCampaignManager::Inst()->InitNPCMaker();
    }
    return false;
}

bool SetCompeteAgitOwner(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c27d4.
    L2_GUARD_FUNCTION();
    std::int32_t residenceId = 0;
    std::int32_t pledgeId = 0;
    std::int32_t state = 0;
    ::Disassemble(packet, "ddd", &residenceId, &pledgeId, &state);
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    CCompeteAgit* const agit = CCompeteAgit::GetCompeteAgit(residenceId);
    if (agit != nullptr && pledge && agit->CanBeOwner(pledge.get()) == CompeteAgitResult::SUCCESS) {
        agit->OnSetWinner(pledge.get(), state == 1);
    }
    return false;
}

bool StartDynamicQuest(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c1cd4.
    L2_GUARD_FUNCTION();
    std::int32_t questId = 0;
    std::int32_t npcId = 0;
    std::int32_t userId = 0;
    ::Disassemble(packet, "ddd", &questId, &npcId, &userId);

    std::int32_t result = 1;
    DynamicQuestManager* const manager = DynamicQuestManager::Inst();
    if (!manager->CanExecuteDQuest()) {
        result = 0;
    } else {
        manager->CheckExecution(true, questId);
    }
    g_npcserver.Send("cddd", 0xb7, npcId, userId, result);
    return false;
}

bool ShowBeautyShop(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c6604.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t talkNpcIndex = 0;
    std::int32_t shopType = 0;
    ::Disassemble(packet, "ddd", &userSid, &talkNpcIndex, &shopType);

    CSPointer<CCreature> npcObject = CCreatureFactory::Inst().FindObjectSP(talkNpcIndex);
    if (!npcObject || !npcObject->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowBeautyShop Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"ShowBeautyShop Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        L2_LOG_WARN(
            Log,
            "Cannot open beauty shop for user SID %u: creature index %d is missing or is not an NPC; the window is not "
            "opened.",
            static_cast<unsigned>(userSid), talkNpcIndex
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (user->IsRegisteredAtOlympiad() || user->IsOlympiadPlaying()) {
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_USE_BEAUTY_SHOP_WHILE_REGISTERED_FOR_OLYMPIAD);
        }
        return false;
    }
    if (user->m_PledgeGameUser.IsRegistered() || user->m_PledgeGameUser.IsObserver() || user->m_PledgeGameUser.IsPlayer() ||
        g_PledgeGameSystem.IsRegisteredUser(user)) {
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::CANNOT_USE_BEAUTY_SHOP_WHILE_REGISTERED_FOR_CEREMONY_OF_CHAOS);
        }
        return false;
    }
    CNPC* const npc = npcObject->CastNPC();
    if (!user->IsTalkableDistance(npc)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowBeautyShop Error too distant")
        // Noise: Moving outside NPC talk range is an expected interaction refusal. Preserve the distance check and any
        // existing client notification without an operator error. L2_LOG_ERROR(Log,
        //     L"ShowBeautyShop Error too distant")
        return false;
    }

    TalkChannelCache::inst().mChannels[17].Cache(user->Id(), npc->m_data->m_nId, 0);
    user->SetBeautyShop(true);

    const int face = user->GetFaceForBroadcast();
    const int hairShape = user->GetHairShapeForBroadcast();
    const int hairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
    if (socket != nullptr) {
        socket->Send("chdddd", 0xfe, 0x134, shopType, face, hairShape, hairColor);
    }

    // if (shopType == 0) {
    if (shopType == Beautyshop::BSST_BUYSHOP) {
        if (user->m_BeautyPurchase.m_nLoaded == 1) {
            CBeautyManager::Inst()->GetDataList(userSid, Beauty::HAIR);
        } else {
            CBeautyManager::Inst()->RequestBeautyPurchaseList(*user, 0);
        }
    } else if (socket != nullptr) {
        socket->Send(
            "chQQddd", 0xfe, 0x137, user->m_inv.GetAdenaAmount(), user->m_inv.GetItemAmount(0x8dd4, 0, true),
            user->m_data->m_nFace, user->m_data->m_nHairColor, user->m_data->m_nHairShape
        );
    }

    const int logHairColor = user->m_data->m_nHairColorEx != 0 ? user->m_data->m_nHairColorEx : user->m_data->m_nHairColor;
    const int logHairShape = user->m_data->m_nHairShapeEx != 0 ? user->m_data->m_nHairShapeEx : user->m_data->m_nHairShape;
    const int logFace = user->m_data->m_nFaceEx != 0 ? user->m_data->m_nFaceEx : user->m_data->m_nFace;
    const FVector position = user->GetPos();
    const std::u16string actorName = std::u16string(user->GetName());
    CLogFormatter formatter;
    formatter.WriteLog(
        shopType == 0 ? 0x1fae : 0x1faf, user->GetDBID(), user->GetAccountID(), 0, 0, static_cast<std::int64_t>(position.x),
        static_cast<std::int64_t>(position.y), static_cast<std::int64_t>(position.z), nullptr, nullptr, nullptr,
        static_cast<int>(user->m_data->m_Race), static_cast<int>(user->m_data->m_Sex),
        static_cast<int>(user->m_data->m_nClass), user->m_data->m_nLevel, logFace, logHairShape, logHairColor, 0, 0, 0,
        actorName.c_str(), user->m_sAccountName.data(), nullptr, nullptr, 0
    );
    return false;
}

bool MoveToServer(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c7aec.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t worldId = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    ::Disassemble(packet, "ddddd", &userSid, &worldId, &x, &y, &z);
    const FVector position(x, y, z);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }
    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return false;
    }

    if (!g_CaptchaSetting.enable || user->m_nBotReportPoint < g_CaptchaSetting.reportCntSendCaptcha) {
        char16_t context[] = u"SYS";
        if (!user->m_data->IsOlympiadApplicant()) {
            if (!RaidClientManager::inst().RequestRaidReserve(worldId, user, position)) {
                socket->SendSystemMessage(context, u"fail to try to reserve raid server");
                socket->SendSystemMessage(SystemMessageId::CANNOT_TELEPORT_DIMENSIONAL_SPACE_CLOSED);
            } else {
                socket->SendSystemMessage(context, u"success to try to reserve raid server");
            }
        } else {
            socket->SendSystemMessage(context, u"cannot go to raid server in olympiad play");
        }
    } else {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_UNTIL_REPORT_VERIFICATION_COMPLETED);
    }
    return false;
}

bool ShowTutorialPageID(NpcSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x008c551c.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    std::int32_t tutorialId;
    ::Disassemble(packet, "ddd", &npcIndex, &userIndex, &tutorialId);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userIndex);
    if (creature && creature->IsPC()) {
        creature->CastUser()->GetSocket()->Send("chd", 0xfe, 0x1e5, tutorialId);
    }
    return false;
}

bool SetMasterFriendNPC(NpcSocket*, const unsigned char* packet) {
    // Current L2Server.exe 0x008c4ee8.
    L2_GUARD_FUNCTION();
    std::int32_t masterSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &masterSid, &npcSid);

    CCreature* const masterCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(masterSid));
    User* const master = masterCreature != nullptr && masterCreature->IsPC() ? masterCreature->CastUser() : nullptr;
    if (master != nullptr) {
        CCreature* const npcCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = npcCreature != nullptr && npcCreature->IsNPC() ? npcCreature->CastNPC() : nullptr;
        if (npc != nullptr) {
            npc->SetMaster(master);
            npc->SetAsSummonedFriendNpc();
        }
    }
    return false;
}

bool ShowAPWnd(NpcSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    ::Disassemble(packet, "d", &userSid);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->Send("ch", 0xfe, 0x165);
        }
    }
    return false;
}

bool ReturnToOrigin(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3488.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    ::Disassemble(packet, "d", &userSid);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        const bool succeeded = RaidServerManager::inst().ReturnToOrigin(user);
        user->GetSocket()->SendSystemMessage(
            u"SYS", succeeded ? u"You return to origin server..." : u"You or your target is not on raid"
        );
    }
    return false;
}

bool ShowCardCollectSystem(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c7064.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t talkNpcIndex = 0;
    ::Disassemble(packet, "dd", &userSid, &talkNpcIndex);

    CSPointer<CCreature> npcObject = CCreatureFactory::Inst().FindObjectSP(talkNpcIndex);
    CNPC* const npc = npcObject && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"showCardCollect Error Invalid userID[%d]",
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"showCardCollect Error Invalid userID[%d]",
        //     userSid)
        L2_LOG_WARN(
            Log, "Cannot open card collection: user SID %u is missing or is not a player; the request is skipped.",
            static_cast<unsigned>(userSid)
        );
    } else if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"showCardCollect Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"showCardCollect Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        L2_LOG_WARN(
            Log,
            "Cannot open card collection for user SID %u: creature index %d is missing or is not an NPC; the window is not "
            "opened.",
            static_cast<unsigned>(userSid), talkNpcIndex
        );
    } else if (!user->IsTalkableDistance(npc)) {
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowCardCollect Error too distant")
        // Noise: Moving outside NPC talk range is an expected interaction refusal. Preserve the distance check and any
        // existing client notification without an operator error. L2_LOG_ERROR(Log,
        //     L"ShowCardCollect Error too distant")
    } else {
        TalkChannelCache::inst().mChannels[18].Cache(user->Id(), npc->m_data->m_nId, 0);
        user->OnShowCardCollectSystem();
    }
    return false;
}

bool NpcServerLoadComplete(NpcSocket*, const unsigned char*) {
    // Current L2Server 0x008c1714.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"[NpcServer] Load Complete")
    // L2_LOG_INFO(Log, L"[NpcServer] Load Complete")
    L2_LOG_INFO(Log, "L2NPC reported loading complete.");
    g_Memory.SetLogging(1);
    return false;
}

bool NpcUpdateEventScoreEx(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c35b8.
    L2_GUARD_FUNCTION();
    std::int32_t kind = 0;
    std::int32_t userSid = 0;
    std::int32_t score = 0;
    ::Disassemble(packet, "ddd", &kind, &userSid, &score);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr && kind == 0x11) {
        user->UpdateInkEnergy(score, false);
    }
    return false;
}

bool ShowLuckyGame(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c6e20.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t gameType = 0;
    std::int32_t talkNpcIndex = 0;
    ::Disassemble(packet, "ddd", &userSid, &gameType, &talkNpcIndex);

    CSPointer<CCreature> npcObject = CCreatureFactory::Inst().FindObjectSP(talkNpcIndex);
    CNPC* const npc = npcObject && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const userCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userCreature != nullptr && userCreature->IsPC() ? userCreature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"showLuckyGame Error Invalid userID[%d]",
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"showLuckyGame Error Invalid userID[%d]",
        //     userSid)
        L2_LOG_WARN(
            Log, "Cannot open lucky game: user SID %u is missing or is not a player; the request is skipped.",
            static_cast<unsigned>(userSid)
        );
    } else if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"showLuckyGame Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"showLuckyGame Error Invalid NPC nUserId[%d] "
        //     L"TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        L2_LOG_WARN(
            Log,
            "Cannot open lucky game for user SID %u: creature index %d is missing or is not an NPC; the window is not "
            "opened.",
            static_cast<unsigned>(userSid), talkNpcIndex
        );
    } else if (!user->IsTalkableDistance(npc)) {
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::NPC_TOO_FAR);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"showLuckyGame Error too distant")
        // Noise: Moving outside NPC talk range is an expected interaction refusal. Preserve the distance check and any
        // existing client notification without an operator error. L2_LOG_ERROR(Log,
        //     L"showLuckyGame Error too distant")
    } else {
        TalkChannelCache::inst().mChannels[19].Cache(user->Id(), npc->m_data->m_nId, 0);
        user->SendStartLuckyGame(gameType);
    }
    return false;
}

bool TrainingState(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c36bc.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t state = 0;
    ::Disassemble(packet, "dd", &userSid, &state);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        if (state == 0) {
            user->RewardTraining();
        } else if (state == 1) {
            user->StartTraining();
        }
    }
    return false;
}

bool NpcGiveItemOnAccount(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c38cc.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t deleteItemClassId = 0;
    std::int64_t deleteItemAmount = 0;
    std::int32_t addItemCount = 0;
    std::int32_t addItemClassIds[10]{};
    std::int64_t addItemAmounts[10]{};
    std::int32_t addItemEnchants[10]{};
    const unsigned char* cursor =
        ::Disassemble(packet, "ddQd", &userSid, &deleteItemClassId, &deleteItemAmount, &addItemCount);
    for (std::int32_t index = 0; index < addItemCount; ++index) {
        cursor = ::Disassemble(cursor, "dQd", &addItemClassIds[index], &addItemAmounts[index], &addItemEnchants[index]);
    }
    std::int32_t type = 0;
    ::Disassemble(cursor, "d", &type);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        if (addItemCount == 1) {
            user->GiveItemOnAccount(
                100, deleteItemClassId, deleteItemAmount, addItemClassIds[0], addItemAmounts[0], addItemEnchants[0], type,
                const_cast<wchar_t*>(L"NoEvent"), 0, 0
            );
        } else if (addItemCount > 1) {
            user->GiveItemsOnAccount(
                100, deleteItemClassId, deleteItemAmount, addItemCount, addItemClassIds, addItemAmounts, addItemEnchants,
                type
            );
        }
    }
    return false;
}

bool NpcGiveItemOnAccountForEvent(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3b18.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t deleteItemClassId = 0;
    std::int64_t deleteItemAmount = 0;
    std::int32_t addItemClassId = 0;
    std::int64_t addItemAmount = 0;
    std::int32_t addItemEnchant = 0;
    std::int32_t type = 0;
    const unsigned char* cursor = ::Disassemble(packet, "ddQ", &userSid, &deleteItemClassId, &deleteItemAmount);
    cursor = ::Disassemble(cursor, "dQd", &addItemClassId, &addItemAmount, &addItemEnchant);
    cursor = ::Disassemble(cursor, "d", &type);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        std::int32_t npcId = 0;
        std::array<char16_t, 56> eventName{};
        ::Disassemble(cursor, "dS", &npcId, 100, eventName.data());
        std::wstring hostEventName = Utf16ToWide(eventName.data());
        user->GiveItemOnAccount(
            100, deleteItemClassId, deleteItemAmount, addItemClassId, addItemAmount, addItemEnchant, type,
            hostEventName.data(), npcId, 0
        );
    }
    return false;
}

bool NpcCreateDualJob(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c5270.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t dualClassId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &dualClassId);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(userSid);
    if (!creature || !creature->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid arguments in [create/change/update subjob]")
        // L2_LOG_ERROR(Log,
        //     L"invalid arguments in [create/change/update subjob]")
        L2_LOG_WARN(
            Log,
            "Cannot create dual class %d from NPC request %d: creature index %d is missing or is not a player; the request "
            "is skipped.",
            dualClassId, npcSid, userSid
        );
        return false;
    }
    User* const user = creature->CastUser();
    if (!user->IsErtheia() || (user->m_data->m_nSubJobId > 0 && user->m_data->m_nSubJobId != 1)) {
        g_npcserver.Send("cddd", 0xba, npcSid, userSid, -1);
        return false;
    }
    user->OnNpcCreateDualJobPacket(npcSid, dualClassId);
    return false;
}

bool ShowKamarokaRankingList(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3ce0.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t page = 0;
    ::Disassemble(packet, "dd", &userSid, &page);
    if (page > 0) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            CPremiumInstantRankingSystem::Inst().ShowRankingList(user, page);
        }
    }
    return false;
}

bool ShowKamarokaUserRankInfo(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3de4.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    ::Disassemble(packet, "d", &userSid);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        CPremiumInstantRankingSystem::Inst().RequestUserRankingInfo(user);
    }
    return false;
}

bool AddKamarokaNewRecord(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3ed4.
    L2_GUARD_FUNCTION();
    std::int32_t score = 0;
    std::int32_t partySid = 0;
    ::Disassemble(packet, "dd", &score, &partySid);

    CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partySid));
    if (party != nullptr) {
        CPremiumInstantRankingSystem::Inst().RequestRegisterNewRecord(score, party);
    }
    return false;
}

bool GetGreatEventContributor(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c5138.
    L2_GUARD_FUNCTION();
    std::int32_t talkNpcIndex = 0;
    ::Disassemble(packet, "d", &talkNpcIndex);

    CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(talkNpcIndex);
    if (creature) {
        const std::uint32_t contributorId = creature->m_pContribute->GetGreatEventContributorID();
        g_npcserver.Send("cdd", 0xbd, talkNpcIndex, contributorId);
    }
    return false;
}

bool GeneralObjectPacket(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c3fa4.
    L2_GUARD_FUNCTION();
    std::uint8_t objectType = 0;
    const unsigned char* cursor = ::Disassemble(packet, "c", &objectType);
    if (objectType != 1) {
        return false;
    }

    std::uint8_t messageType = 0;
    cursor = ::Disassemble(cursor, "c", &messageType);
    if (messageType == 1) {
        std::int32_t groupId = 0;
        std::int32_t partySid = 0;
        ::Disassemble(cursor, "dd", &groupId, &partySid);
        CParty* const party = g_PartySmartIdManager.GetObjectW(static_cast<std::uint32_t>(partySid));
        if (party == nullptr || groupId < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid party or group[%d]",
            //     L"NpcSocketEx.cpp",
            //     0x50e,
            //     groupId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid party or group[%d]",
            //     L"NpcSocketEx.cpp",
            //     0x50e,
            //     groupId)
            L2_LOG_WARN(
                Log,
                "Cannot assign chair group %d to party SID %u: the party is unavailable or the group ID is not positive; "
                "assignment is skipped.",
                groupId, static_cast<unsigned>(partySid)
            );
            return false;
        }
        if (g_ChairManager.AssignChairOwnerGroup(groupId, partySid)) {
            return false;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid party or group[%d]",
        //     L"NpcSocketEx.cpp",
        //     0x514,
        //     groupId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid party or group[%d]",
        //     L"NpcSocketEx.cpp",
        //     0x514,
        //     groupId)
        L2_LOG_ERROR(
            Log, "Assigning chair group %d to party SID %u failed; requesting group cleanup at (0, 0, 0).", groupId,
            static_cast<unsigned>(partySid)
        );
        g_ChairManager.ClearChairGroup(groupId, 0, 0, 0);
    } else if (messageType == 2) {
        std::int32_t groupId = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        ::Disassemble(cursor, "dddd", &groupId, &x, &y, &z);
        g_ChairManager.ClearChairGroup(groupId, x, y, z);
    }
    return false;
}

bool SetDroidNPC(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c58a8.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t setDroidNPC = 0;
    std::int32_t userSid = 0;
    ::Disassemble(packet, "ddd", &npcSid, &setDroidNPC, &userSid);

    CSPointer<CNPC> npc = g_NPCMap.FindObject(npcSid);
    if (setDroidNPC == 0) {
        if (!npc) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC1(npc_sid = %d, pc_sid = %d)",
            //     L"NpcSocketEx.cpp",
            //     0x57d,
            //     npcSid,
            //     userSid)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC1(npc_sid = %d, pc_sid = %d)",
            //     L"NpcSocketEx.cpp",
            //     0x57d,
            //     npcSid,
            //     userSid)
            L2_LOG_WARN(
                Log, "Cannot clear droid NPC SID %u for user SID %u: the NPC is unavailable; the request is skipped.",
                static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
            );
            return false;
        }
        if (!npc->IsDroidNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC2(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x583,
            //     npcSid,
            //     userSid)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC2(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x583,
            //     npcSid,
            //     userSid)
            L2_LOG_WARN(
                Log,
                "Cannot clear droid NPC SID %u for user SID %u: the NPC is not marked as a droid; the request is skipped.",
                static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
            );
            return false;
        }
        if (!npc->IsActive() || npc->IsDeleting() || npc->m_Deleted != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC4(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x589,
            //     npcSid,
            //     userSid)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC4(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x589,
            //     npcSid,
            //     userSid)
            L2_LOG_WARN(
                Log,
                "Cannot clear droid NPC SID %u for user SID %u: the NPC is inactive, deleting or already deleted; the "
                "request is skipped.",
                static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
            );
            return false;
        }
        if (!npc->StartDroidNPCSetting()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC3(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x58f,
            //     npcSid,
            //     userSid)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][DroidNPC] fail to ClearDroidNPC3(npc_sid = %d, pc_sid = %d), is not droid npc",
            //     L"NpcSocketEx.cpp",
            //     0x58f,
            //     npcSid,
            //     userSid)
            L2_LOG_WARN(
                Log,
                "Cannot clear droid NPC SID %u for user SID %u: the droid-setting flag is already set; the request is "
                "skipped.",
                static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
            );
            return false;
        }

        npc->ClearDroidNPCInfo();
        npc->EndDroidNPCSetting();
        npc->DoNothing();
        // npc->ActionFailed(0)
        npc->ActionFailed(AF_DEFAULT);
        npc->PushSerializedMessage(new CSerializedCreatureResetVisiblity(npc->CastCreature()));
        return false;
    }

    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC1(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x551,
        //     npcSid,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC1(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x551,
        //     npcSid,
        //     userSid)
        L2_LOG_WARN(
            Log, "Cannot assign droid NPC SID %u to user SID %u: the NPC is unavailable; the request is skipped.",
            static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
        );
        return false;
    }
    CSPointer<User> user = g_UserMap.FindObject(userSid);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC2(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x558,
        //     npcSid,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC2(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x558,
        //     npcSid,
        //     userSid)
        L2_LOG_WARN(
            Log, "Cannot assign droid NPC SID %u to user SID %u: the player is unavailable; the request is skipped.",
            static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
        );
        return false;
    }
    if (npc->IsDroidNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC3(npc_sid = %d, pc_sid = %d), already droid npc",
        //     L"NpcSocketEx.cpp",
        //     0x55e,
        //     npcSid,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC3(npc_sid = %d, pc_sid = %d), already droid npc",
        //     L"NpcSocketEx.cpp",
        //     0x55e,
        //     npcSid,
        //     userSid)
        L2_LOG_WARN(
            Log,
            "Cannot assign droid NPC SID %u to user SID %u: the NPC is already marked as a droid; the request is skipped.",
            static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
        );
        return false;
    }
    if (!npc->IsActive() || npc->IsDeleting() || npc->m_Deleted != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC4(npc_sid = %d, pc_sid = %d), already droid npc",
        //     L"NpcSocketEx.cpp",
        //     0x564,
        //     npcSid,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC4(npc_sid = %d, pc_sid = %d), already droid npc",
        //     L"NpcSocketEx.cpp",
        //     0x564,
        //     npcSid,
        //     userSid)
        L2_LOG_WARN(
            Log,
            "Cannot assign droid NPC SID %u to user SID %u: the NPC is inactive, deleting or already deleted; the request "
            "is skipped.",
            static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
        );
        return false;
    }
    if (!npc->StartDroidNPCSetting()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC5(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x56a,
        //     npcSid,
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][DroidNPC] fail to SetDroidNPC5(npc_sid = %d, pc_sid = %d)",
        //     L"NpcSocketEx.cpp",
        //     0x56a,
        //     npcSid,
        //     userSid)
        L2_LOG_WARN(
            Log,
            "Cannot assign droid NPC SID %u to user SID %u: the droid-setting flag is already set; the request is skipped.",
            static_cast<unsigned>(npcSid), static_cast<unsigned>(userSid)
        );
        return false;
    }

    npc->SetDroidNPCInfo(true, user->GetAccountID(), user->GetDBID());
    db.RequestLoadDroidNPCAppearance(
        static_cast<DROIDNPC_COMMAND::DROIDNPC_COMMAND_TYPE>(1), static_cast<unsigned int>(npcSid), user->GetAccountID(),
        user->GetDBID()
    );
    npc->DoNothing();
    // npc->ActionFailed(0)
    npc->ActionFailed(AF_DEFAULT);
    return false;
}

bool ShowEnsoulWindow(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c729c.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &userSid, &npcSid);

    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (user != nullptr && user->IsPC()) {
        CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
        if (npc != nullptr && npc->IsNPC() && user->IsTalkableDistance(npc)) {
            TalkChannelCache::inst().mChannels[20].Cache(user->Id(), npc->Id(), 0);
            user->GetSocket()->Send("ch", 0xfe, 0x180);
        }
    }
    return false;
}

bool BossRegenTime(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c17c8.
    L2_GUARD_FUNCTION();
    std::int32_t npcClassId = 0;
    std::int32_t respawnTime = 0;
    ::Disassemble(packet, "dd", &npcClassId, &respawnTime);
    RelayClient::Inst()->SendBossRegenTime(npcClassId, respawnTime);
    return false;
}

bool ShowEnsoulExtractionWindow(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c744c.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "dd", &userSid, &npcSid);

    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (user != nullptr && user->IsPC()) {
        CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
        CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
        if (npc != nullptr && npc->IsNPC() && user->IsTalkableDistance(npc)) {
            TalkChannelCache::inst().mChannels[20].Cache(user->Id(), npc->Id(), 0);
            user->GetSocket()->Send("ch", 0xfe, 0x1b4);
        }
    }
    return false;
}

bool SyncServerConfig(NpcSocket*, const unsigned char* packet) {
    // Current L2Server 0x008c6260.
    L2_GUARD_FUNCTION();
    std::int32_t questCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "fd", &g_dF2PQuestRewardPenaltyPer, &questCount);
    for (std::int32_t index = 0; index < questCount; ++index) {
        std::int32_t questId = 0;
        cursor = ::Disassemble(cursor, "d", &questId);
        g_mapF2PRewardPenaltyQuests.emplace(questId, runtime::Win64CvttDoubleToInt32(g_dF2PQuestRewardPenaltyPer));
    }
    return false;
}

bool Handle_NC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c56fc.
    L2_GUARD_FUNCTION();
    PkNC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE value;
    value.values[9] = -1;
    DisassemblePacket(packet, value);

    DefaultIOBuffer* const buffer = MakeConstBuffer(
        "chdddddddddddSSSSS", 0xfe, 0x39, 1, -1, value.values[1], value.values[2], value.values[3], value.values[4],
        value.values[5], value.values[6], value.values[7], value.values[8], value.values[9], value.strings[0].data(),
        value.strings[1].data(), value.strings[2].data(), value.strings[3].data(), value.strings[4].data()
    );
    if (buffer != nullptr) {
        g_FieldCycleSystem.BroadcastTo(value.values[0], buffer);
        buffer->Release();
    }
    return false;
}

bool Handle_NC_EX_BROADCAST_FIELD_EVENT(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c42fc.
    L2_GUARD_FUNCTION();

    class TLMFunc final : public CTLMFunc {
    public:
        explicit TLMFunc(const int eventArgument) : m_eventArgument(eventArgument) {}

        bool CanDo(CObject* const object, double) override {
            if (object->IsPC() && !object->IsDeleting() && object->m_Deleted == 0) {
                User* const user = object->CastUser();
                user->GetSocket()->Send("chd", 0xfe, 0x1b8, m_eventArgument);
            }
            return true;
        }

    private:
        int m_eventArgument;
    };

    PkNC_EX_BROADCAST_FIELD_EVENT value;
    value.npcSid = 0;
    DisassemblePacket(packet, value);
    if (value.radius == 0) {
        CUserBroadcaster::inst().BroadcastToAllUser("chd", 0xfe, 0x1b8, value.eventArgument);
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(value.npcSid));
    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
    if (npc != nullptr) {
        const FVector position = npc->GetPos();
        CTLMFilterDist filter(static_cast<int>(npc->GetInZoneID()), position, static_cast<double>(value.radius));
        TLMFunc function(value.eventArgument);
        FOR_ALL_NEIGHBOR_USER_DIST(&filter, &function);
    }
    return false;
}

bool Handle_NC_EX_CAST_BUFF_TO_RANDOM_USER(NpcSocket* const socket, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c47f8.
    L2_GUARD_FUNCTION();
    PkNC_EX_CAST_BUFF_TO_RANDOM_USER value;
    value.npcSid = 0;
    DisassemblePacket(packet, value);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(value.npcSid));
    CNPC* const npc = creature != nullptr && creature->IsNPC() ? creature->CastNPC() : nullptr;
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No caster npc at AtomicCastBuffToRandomUser: %d",
        //     value.npcSid)
        // L2_LOG_ERROR(Log,
        //     L"No caster npc at AtomicCastBuffToRandomUser: %d",
        //     value.npcSid)
        L2_LOG_WARN(
            Log,
            "Cannot cast a buff on a random player: caster SID %u is missing or is not an NPC; no cast or target reply is "
            "requested.",
            static_cast<unsigned>(value.npcSid)
        );
        return false;
    }

    const std::uint64_t encoded = static_cast<std::uint64_t>(value.packedSkill);
    const int skillId = static_cast<int>(encoded >> 32);
    const int skillLevel = static_cast<int>((encoded >> 16) & 0xffffU);
    const int skillSubLevel = static_cast<int>(encoded & 0xffffU);
    CSPointer<User> user = UserUtil::CastBuffToRandomUser(npc, skillId, skillLevel, skillSubLevel);
    if (!user) {
        // The current call omits the argument required by %d.
        // Log.Add(
        //     LOG_ERROR,
        //     L"No user for cast buff at AtomicCastBuffToRandomUser: %d")
        // UserUtil already reports the specific failure reason.
        // Suppress this duplicate diagnostic with its missing format argument.
        // L2_LOG_ERROR(Log,
        //     L"No user for cast buff at AtomicCastBuffToRandomUser: %d")
        return false;
    }

    socket->Send("cdd", 0xbf, value.npcSid, user->Id());
    WriteLOG_SKILL_CAST(npc, user, skillId, skillLevel, skillSubLevel);
    return false;
}

bool ShowUpgradeSystem(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c75fc.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t talkNpcIndex = 0;
    ::Disassemble(packet, "dd", &userSid, &talkNpcIndex);

    CSPointer<CCreature> npcObject = CCreatureFactory::Inst().FindObjectSP(talkNpcIndex);
    CNPC* const npc = npcObject && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowUpgradeSystem Error Invalid NPC nUserId[%d] TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        // L2_LOG_ERROR(Log,
        //     L"ShowUpgradeSystem Error Invalid NPC nUserId[%d] TalkNpcIndex[%d]",
        //     userSid,
        //     talkNpcIndex)
        L2_LOG_WARN(
            Log,
            "Cannot open upgrade window for user SID %u: creature index %d is missing or is not an NPC; the window is not "
            "opened.",
            static_cast<unsigned>(userSid), talkNpcIndex
        );
        return false;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }
    if (!user->IsTalkableDistance(npc)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowUpgradeSystem Error too distant")
        // Noise: Moving outside NPC talk range is an expected interaction refusal. Preserve the distance check and any
        // existing client notification without an operator error. L2_LOG_ERROR(Log,
        //     L"ShowUpgradeSystem Error too distant")
        return false;
    }

    TalkChannelCache::inst().mChannels[22].Cache(user->Id(), npc->m_nId, 0);
    protocol::S_EX_SHOW_UPGRADE_SYSTEM response{};
    response.nFlag = 1;
    user->GetSocket()->Send(S_EX, S_EX_SHOW_UPGRADE_SYSTEM, response);
    return false;
}

bool ResetInzoneBinding(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c4198.
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    std::int32_t enabled;
    std::int32_t startTime;
    std::int32_t endTime;
    std::int32_t eventType;
    std::int32_t eventVariable;
    std::int32_t timeGap;
    ::Disassemble(packet, "ddddddd", &userSid, &enabled, &startTime, &endTime, &eventType, &eventVariable, &timeGap);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ResetInzoneBinding no User[%d]",
        //     userSid)
        // L2_LOG_ERROR(Log,
        //     L"ResetInzoneBinding no User[%d]",
        //     userSid)
        L2_LOG_WARN(
            Log, "Cannot reset instance-zone binding: user SID %u is missing or is not a player; the reset is skipped.",
            static_cast<unsigned>(userSid)
        );
    } else {
        user->ResetInzoneBinding(enabled, startTime, endTime, timeGap, eventType, eventVariable);
    }
    return false;
}

bool ShowCardUpdownGame(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c49fc.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t userSid = 0;
    std::int32_t gameId = 0;
    ::Disassemble(packet, "ddd", &npcSid, &userSid, &gameId);

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    CCreature* const userObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = userObject != nullptr && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (npc != nullptr && user != nullptr) {
        user->ShowCardUpdownGame(npcSid, gameId);
    }
    return false;
}

bool Handle_NC_EX_PLEDGE_RAID_POINT_SET(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c1e38.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid;
    std::int32_t pledgeDbId;
    std::int32_t pledgeIndex;
    std::int64_t point;
    ::Disassemble(packet, "dddQ", &npcSid, &pledgeDbId, &pledgeIndex, &point);
    PledgeRaidSystem::inst().SetPoint(static_cast<unsigned int>(npcSid), pledgeDbId, pledgeIndex, point);
    return false;
}

bool Handle_NC_EX_PLEDGE_RAID_SHOW_INFO(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c4b48.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t type = 0;
    ::Disassemble(packet, "dd", &userSid, &type);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        PledgeRaidSystem::inst().SendAllToUser(user, type != 0);
    }
    return false;
}

bool Handle_NC_EX_PLEDGE_MISSION_AI_CONTROL(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c4c54.
    L2_GUARD_FUNCTION();
    std::int32_t userSid = 0;
    std::int32_t missionId = 0;
    std::int32_t firstValue = 0;
    std::int32_t secondValue = 0;
    std::int32_t count = 0;
    ::Disassemble(packet, "ddddd", &userSid, &missionId, &firstValue, &secondValue, &count);
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr) {
        UserPledgeMissionInfo* const mission = user->PledgeMissionFind(missionId);
        if (mission != nullptr && mission->mPledgeMissionInfo_->eGoalType_ == PledgeMission::PMGT_AI_CONTROL) {
            user->PledgeMissionCheckConditionAndAddCount(missionId, *mission, firstValue, secondValue, count);
        }
    }
    return false;
}

bool Handle_NC_EX_WORLD_RAID_START(NpcSocket* const socket, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c7d58.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);
    if (!RaidClientManager::inst().RequestWorldRaidStart(static_cast<std::uint32_t>(npcSid))) {
        socket->Send("cddd", 0xc3, npcSid, -1, 0);
    }
    return false;
}

bool Handle_NC_EX_WORLD_RAID_STOP(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c7e50.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    ::Disassemble(packet, "d", &npcSid);
    RaidClientManager::inst().RequestWorldRaidStop(static_cast<std::uint32_t>(npcSid));
    return false;
}

bool Handle_NC_EX_WORLD_RAID_BROADCAST_EVENT(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c7f08.
    L2_GUARD_FUNCTION();
    std::int32_t npcSid = 0;
    std::int32_t first = 0;
    std::int32_t second = 0;
    std::int32_t third = 0;
    ::Disassemble(packet, "dddd", &npcSid, &first, &second, &third);
    RaidClientManager::inst().RequestWorldRaidBroadcastEvent(first, second, third);
    return false;
}

bool ShowCreatePledge(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c783c.
    L2_GUARD_FUNCTION();
    std::int32_t npcIndex;
    std::int32_t userIndex;
    ::Disassemble(packet, "dd", &npcIndex, &userIndex);

    CSPointer<CCreature> userObject = CCreatureFactory::Inst().FindObjectSP(userIndex);
    User* const user = userObject && userObject->IsPC() ? userObject->CastUser() : nullptr;
    if (user == nullptr) {
        return false;
    }

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    CCreature* npcObject = nullptr;
    if (npcIndex >= 0 && npcIndex < creatures.GetSize()) {
        npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(creatures.GetIdByIndex(npcIndex)));
    }
    CCreature* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject : nullptr;
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ShowCreatePledge Error Invalid NPC")
        // L2_LOG_ERROR(Log,
        //     L"ShowCreatePledge Error Invalid NPC")
        L2_LOG_WARN(
            Log,
            "Cannot open clan creation for player index %d: creature index %d is invalid or is not an NPC; the window is "
            "not opened.",
            userIndex, npcIndex
        );
        return false;
    }

    TalkChannelCache::inst().mChannels[24].Cache(user->Id(), npc->Id(), 0);
    user->GetSocket()->Send("ch", 0xfe, 0x1ed);
    return false;
}

bool SendAgitWarPacket(NpcSocket*, const unsigned char* const packet) {
    // Current L2Server.exe 0x008c4db4.
    L2_GUARD_FUNCTION();
    std::int32_t userSid;
    ::Disassemble(packet, "d", &userSid);

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(userSid));
    User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
    if (user != nullptr && user->GetPledge() != nullptr) {
        RelayClient::Inst()->SendWarPacket(user, 2);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] SendWarPacket [%s][%d]",
        //     user->GetName(),
        //     2)
        // Noise: Routine relay request tracing was mislabeled NO_ERROR at ERROR severity. SendWarPacket keeps its own
        // failure diagnostics and the request is unchanged. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] SendWarPacket [%s][%d]",
        //     user->GetName(),
        //     2)
    }
    return false;
}

} // namespace NpcSocketEx

namespace {

L2_BIND_PACKET(kBindNpcSocketEx00, g_NPCSocketEx, 0x00, &NpcSocketEx::DummyNpcSocketEx);
L2_BIND_PACKET(kBindNpcSocketEx01, g_NPCSocketEx, 0x01, &NpcSocketEx::CheckWeightToChangeSubclass);
L2_BIND_PACKET(kBindNpcSocketEx02, g_NPCSocketEx, 0x02, &NpcSocketEx::ShowItemAttributionChange);
L2_BIND_PACKET(kBindNpcSocketEx03, g_NPCSocketEx, 0x03, &NpcSocketEx::SetUserLevel);
L2_BIND_PACKET(kBindNpcSocketEx04, g_NPCSocketEx, 0x04, &NpcSocketEx::GiveQuest);
L2_BIND_PACKET(kBindNpcSocketEx05, g_NPCSocketEx, 0x05, &NpcSocketEx::CompleteQuest);
L2_BIND_PACKET(kBindNpcSocketEx06, g_NPCSocketEx, 0x06, &NpcSocketEx::StartJournal);
L2_BIND_PACKET(kBindNpcSocketEx07, g_NPCSocketEx, 0x07, &NpcSocketEx::CompleteJournal);
L2_BIND_PACKET(kBindNpcSocketEx08, g_NPCSocketEx, 0x08, &NpcSocketEx::SetShillienMoon);
L2_BIND_PACKET(kBindNpcSocketEx09, g_NPCSocketEx, 0x09, &NpcSocketEx::NpcResetCastle);
L2_BIND_PACKET(kBindNpcSocketEx0A, g_NPCSocketEx, 0x0a, &NpcSocketEx::NpcSetCastleType);
L2_BIND_PACKET(kBindNpcSocketEx0B, g_NPCSocketEx, 0x0b, &NpcSocketEx::RegisterPledgeGameMaker);
L2_BIND_PACKET(kBindNpcSocketEx0C, g_NPCSocketEx, 0x0c, &NpcSocketEx::NpcPledgeGameFunc);
L2_BIND_PACKET(kBindNpcSocketEx0D, g_NPCSocketEx, 0x0d, &NpcSocketEx::GiveItemViaPledgeGame);
L2_BIND_PACKET(kBindNpcSocketEx0E, g_NPCSocketEx, 0x0e, &NpcSocketEx::SetGotItemPledgeGame);
L2_BIND_PACKET(kBindNpcSocketEx0F, g_NPCSocketEx, 0x0f, &NpcSocketEx::AddLogFromNPCEx);
L2_BIND_PACKET(kBindNpcSocketEx10, g_NPCSocketEx, 0x10, &NpcSocketEx::NpcEventCampaignEx);
L2_BIND_PACKET(kBindNpcSocketEx11, g_NPCSocketEx, 0x11, &NpcSocketEx::SetCompeteAgitOwner);
L2_BIND_PACKET(kBindNpcSocketEx12, g_NPCSocketEx, 0x12, &NpcSocketEx::StartDynamicQuest);
L2_BIND_PACKET(kBindNpcSocketEx13, g_NPCSocketEx, 0x13, &NpcSocketEx::ShowBeautyShop);
L2_BIND_PACKET(kBindNpcSocketEx14, g_NPCSocketEx, 0x14, &NpcSocketEx::MoveToServer);
L2_BIND_PACKET(kBindNpcSocketEx15, g_NPCSocketEx, 0x15, &NpcSocketEx::ReturnToOrigin);
L2_BIND_PACKET(kBindNpcSocketEx16, g_NPCSocketEx, 0x16, &NpcSocketEx::ShowCardCollectSystem);
L2_BIND_PACKET(kBindNpcSocketEx17, g_NPCSocketEx, 0x17, &NpcSocketEx::IncreaseAPByNpc);
L2_BIND_PACKET(kBindNpcSocketEx18, g_NPCSocketEx, 0x18, &NpcSocketEx::ShowAPWnd);
L2_BIND_PACKET(kBindNpcSocketEx19, g_NPCSocketEx, 0x19, &NpcSocketEx::NpcServerLoadComplete);
L2_BIND_PACKET(kBindNpcSocketEx1A, g_NPCSocketEx, 0x1a, &NpcSocketEx::NpcUpdateEventScoreEx);
L2_BIND_PACKET(kBindNpcSocketEx1B, g_NPCSocketEx, 0x1b, &NpcSocketEx::ShowLuckyGame);
L2_BIND_PACKET(kBindNpcSocketEx1C, g_NPCSocketEx, 0x1c, &NpcSocketEx::TrainingState);
L2_BIND_PACKET(kBindNpcSocketEx1D, g_NPCSocketEx, 0x1d, &NpcSocketEx::NpcGiveItemOnAccount);
L2_BIND_PACKET(kBindNpcSocketEx1E, g_NPCSocketEx, 0x1e, &NpcSocketEx::NpcGiveItemOnAccountForEvent);
L2_BIND_PACKET(kBindNpcSocketEx1F, g_NPCSocketEx, 0x1f, &NpcSocketEx::NpcCreateDualJob);
L2_BIND_PACKET(kBindNpcSocketEx20, g_NPCSocketEx, 0x20, &NpcSocketEx::ShowKamarokaRankingList);
L2_BIND_PACKET(kBindNpcSocketEx21, g_NPCSocketEx, 0x21, &NpcSocketEx::ShowKamarokaUserRankInfo);
L2_BIND_PACKET(kBindNpcSocketEx22, g_NPCSocketEx, 0x22, &NpcSocketEx::AddKamarokaNewRecord);
L2_BIND_PACKET(kBindNpcSocketEx23, g_NPCSocketEx, 0x23, &NpcSocketEx::GetGreatEventContributor);
L2_BIND_PACKET(kBindNpcSocketEx24, g_NPCSocketEx, 0x24, &NpcSocketEx::GeneralObjectPacket);
L2_BIND_PACKET(kBindNpcSocketEx25, g_NPCSocketEx, 0x25, &NpcSocketEx::SetDroidNPC);
L2_BIND_PACKET(kBindNpcSocketEx26, g_NPCSocketEx, 0x26, &NpcSocketEx::ShowEnsoulWindow);
// The current initializer registers 0x28 before 0x27.
L2_BIND_PACKET(kBindNpcSocketEx28, g_NPCSocketEx, 0x28, &NpcSocketEx::ShowEnsoulExtractionWindow);
L2_BIND_PACKET(kBindNpcSocketEx27, g_NPCSocketEx, 0x27, &NpcSocketEx::BossRegenTime);
L2_BIND_PACKET(kBindNpcSocketEx29, g_NPCSocketEx, 0x29, &NpcSocketEx::SyncServerConfig);
L2_BIND_PACKET(
    kBindNpcSocketEx2A, g_NPCSocketEx, 0x2a, &NpcSocketEx::Handle_NC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE
);
L2_BIND_PACKET(kBindNpcSocketEx2B, g_NPCSocketEx, 0x2b, &NpcSocketEx::Handle_NC_EX_BROADCAST_FIELD_EVENT);
L2_BIND_PACKET(kBindNpcSocketEx2C, g_NPCSocketEx, 0x2c, &NpcSocketEx::Handle_NC_EX_CAST_BUFF_TO_RANDOM_USER);
L2_BIND_PACKET(kBindNpcSocketEx2D, g_NPCSocketEx, 0x2d, &NpcSocketEx::ShowUpgradeSystem);
L2_BIND_PACKET(kBindNpcSocketEx2E, g_NPCSocketEx, 0x2e, &NpcSocketEx::ResetInzoneBinding);
L2_BIND_PACKET(kBindNpcSocketEx2F, g_NPCSocketEx, 0x2f, &NpcSocketEx::ShowCardUpdownGame);
L2_BIND_PACKET(kBindNpcSocketEx30, g_NPCSocketEx, 0x30, &NpcSocketEx::Handle_NC_EX_PLEDGE_RAID_POINT_SET);
L2_BIND_PACKET(kBindNpcSocketEx31, g_NPCSocketEx, 0x31, &NpcSocketEx::Handle_NC_EX_PLEDGE_RAID_SHOW_INFO);
L2_BIND_PACKET(kBindNpcSocketEx32, g_NPCSocketEx, 0x32, &NpcSocketEx::Handle_NC_EX_PLEDGE_MISSION_AI_CONTROL);
L2_BIND_PACKET(kBindNpcSocketEx33, g_NPCSocketEx, 0x33, &NpcSocketEx::Handle_NC_EX_WORLD_RAID_START);
L2_BIND_PACKET(kBindNpcSocketEx34, g_NPCSocketEx, 0x34, &NpcSocketEx::Handle_NC_EX_WORLD_RAID_STOP);
L2_BIND_PACKET(kBindNpcSocketEx35, g_NPCSocketEx, 0x35, &NpcSocketEx::Handle_NC_EX_WORLD_RAID_BROADCAST_EVENT);
L2_BIND_PACKET(kBindNpcSocketEx36, g_NPCSocketEx, 0x36, &NpcSocketEx::ShowTutorialPageID);
L2_BIND_PACKET(kBindNpcSocketEx37, g_NPCSocketEx, 0x37, &NpcSocketEx::ShowCreatePledge);
L2_BIND_PACKET(kBindNpcSocketEx38, g_NPCSocketEx, 0x38, &NpcSocketEx::SendAgitWarPacket);
L2_BIND_PACKET(kBindNpcSocketEx39, g_NPCSocketEx, 0x39, &NpcSocketEx::SetMasterFriendNPC);

} // namespace
