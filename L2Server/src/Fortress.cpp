#include "Fortress.h"
#include "AccountJobFinishSignal.h"
#include "BattleCamp.h"
#include "CConfig.h"
#include "CFortressFlagpole.h"
#include "CWorld.h"
#include "Castle.h"
#include "ControlTower.h"
#include "DB.h"
#include "Dominion.h"
#include "Door.h"
#include "Item.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Packet.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Config.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Win64Integer.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <new>
#include <string>
#include <string_view>

namespace {


constexpr int kPendingWriteLimit = 0x80000;

int ResidenceId(const CResidenceData* const data) noexcept { return data != nullptr ? data->m_nResidenceID : 0; }

ResidenceState ResidenceStatus(const CResidenceData* const data) noexcept {
    return data != nullptr ? data->m_ResidenceState : RS_MAX;
}

char16_t* FortressBuilderNextToken(char16_t*& remaining) {
    char16_t* token = nullptr;
    remaining = ::GetLeftString2(remaining, &token);
    return token;
}

int CompareFortressBuilderIgnoreCase(const char16_t* left, const char16_t* right) {
#if defined(_WIN32)
    return ::_wcsicmp(Utf16ToWide(left).c_str(), Utf16ToWide(right).c_str());
#else
    while (*left != u'\0' && *right != u'\0') {
        const char16_t foldedLeft = *left >= u'A' && *left <= u'Z' ? static_cast<char16_t>(*left + (u'a' - u'A')) : *left;
        const char16_t foldedRight =
            *right >= u'A' && *right <= u'Z' ? static_cast<char16_t>(*right + (u'a' - u'A')) : *right;
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == u'\0' ? -1 : 1;
#endif
}

std::int32_t FortressBuilderToInt(const char16_t* const value) noexcept { return WindowsWtol(std::u16string_view(value)); }

CSPointer<CPledge> FindFortressBuilderPledge(char16_t* const pledgeName) {
    return CPledge::FindPledge(g_nWorldId, pledgeName);
}

template <typename... Arguments>
void SendFortressBuilderSystemMessage(
    CSocket* const socket, const std::u16string_view format, const Arguments&... arguments
) {
    std::array<char16_t, 8196> message{};
    static_cast<void>(FormatUtf16To(message.data(), message.size(), format, arguments...));
    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", message.data());
}

void SendFortressBuilderSystemMessage(CSocket* const socket, const char16_t* const message) {
    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", message);
}

bool CheckFortressBuilderLevel(CSocket* const socket, const int, bool& result) {
    User* const builder = static_cast<CUserSocket*>(socket)->GetUser();
    if (builder == nullptr) {
        result = true;
        return false;
    }

    int builderLevel = 0;
    {
        L2_GUARD(*builder->m_lock);
        builderLevel = builder->m_data->m_nBuilder;
    }
    if (builderLevel == 0 || builderLevel > 2) {
        result = false;
        return false;
    }
    return true;
}

CFortress* FindFortressForBuilder(const char16_t* const fortressId, const bool requireFortressType) {
    CResidence* const residence = g_ResidenceDB.GetResidence(FortressBuilderToInt(fortressId));
    if (residence == nullptr || (requireFortressType && !residence->IsFortress())) {
        return nullptr;
    }
    return residence->CastFortress();
}


std::int32_t g_fortressStateSaveTick = 0;

constexpr std::array<int, 3> kStandbyMinuteMarks{5, 2, 1};
constexpr std::array<int, 4> kStandbySecondMarks{30, 10, 5, 1};

} // namespace

class CFortressNpcMakerEventHandler {
public:
    explicit CFortressNpcMakerEventHandler(const int fortressId) : m_nFortressId(fortressId) {}

    void Send(const int eventId, const int value1, const int value2, const int value3, const wchar_t* const text) {
        // Current L2Server 0x0073902c.
        L2_GUARD_FUNCTION();
        g_npcserver.Send(
            "cdddddS", 0x67, m_nFortressId, eventId, value1, value2, value3,
            target_utf16::FromRecoveredWindowsWide(text).c_str()
        );
    }

private:
    int m_nFortressId;
};


class CFortressNpcAIEventHandler {
public:
    CFortressNpcAIEventHandler(const int fortressId, CFortress* const fortress) :
        m_nFortressId(fortressId), m_alignment04To08{}, m_pFortress(fortress) {}

    void Send(const int eventId, const int value1, const int value2, const int value3, const int value4, const int value5) {
        // Current L2Server 0x00739128.
        L2_GUARD_FUNCTION();
        g_npcserver.Send("cddddddd", 0x68, m_nFortressId, eventId, value1, value2, value3, value4, value5);
    }

    void Receive(const unsigned char* const packet) {
        // Current L2Server 0x00747db4.
        L2_GUARD_FUNCTION();
        if (m_pFortress == nullptr) {
            return;
        }

        int eventId = 0;
        int npcSid = 0;
        int targetSid = 0;
        int value = 0;
        std::int64_t amount = 0;
        ::Disassemble(packet, "ddddQ", &eventId, &npcSid, &targetSid, &value, &amount);
        m_pFortress->OnAIEvent(eventId, npcSid, targetSid, value, amount);
    }

    void Detach() noexcept { m_pFortress = nullptr; }

private:
    int m_nFortressId;
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment04To08;
    CFortress* m_pFortress;
};


CFortressBuilderCmdHandler::stFortressBuilderCmdMap CFortressBuilderCmdHandler::m_CmdMap[30] = {
    {u"정보", &BuilderCmd_ShowInfo},
    {u"등록", &BuilderCmd_SiegeRegister},
    {u"등록취소", &BuilderCmd_SiegeUnregister},
    {u"등록정보", &BuilderCmd_ShowSiegeRegistery},
    {u"준비", &BuilderCmd_SiegeStandby},
    {u"시작", &BuilderCmd_SiegeStart},
    {u"막사끝", &BuilderCmd_AllBarracksCaptured},
    {u"깃발끝", &BuilderCmd_FlagCaptured},
    {u"성주", &BuilderCmd_SetOwner},
    {u"성주즐", &BuilderCmd_ResetOwner},
    {u"성계약", &BuilderCmd_ContractCastle},
    {u"보급품레벨", &BuilderCmd_SetTreasureLevel},
    {u"명예레벨", &BuilderCmd_SetRewardCycleCount},
    {u"보유시간설정", &BuilderCMd_SetOwnMinutes},
    {u"보유시간확인", &BuilderCMd_GetOwnMinutes},
    {u"show_info", &BuilderCmd_ShowInfo},
    {u"siege_register", &BuilderCmd_SiegeRegister},
    {u"siege_unregister", &BuilderCmd_SiegeUnregister},
    {u"show_siege_registry", &BuilderCmd_ShowSiegeRegistery},
    {u"siege_standby", &BuilderCmd_SiegeStandby},
    {u"siege_start", &BuilderCmd_SiegeStart},
    {u"all_barracks_captured", &BuilderCmd_AllBarracksCaptured},
    {u"flag_captured", &BuilderCmd_FlagCaptured},
    {u"set_owner", &BuilderCmd_SetOwner},
    {u"reset_owner", &BuilderCmd_ResetOwner},
    {u"contract_castle", &BuilderCmd_ContractCastle},
    {u"set_treasure_level", &BuilderCmd_SetTreasureLevel},
    {u"set_reward_cycle_count", &BuilderCmd_SetRewardCycleCount},
    {u"set_own_minutes", &BuilderCMd_SetOwnMinutes},
    {u"get_own_minutes", &BuilderCMd_GetOwnMinutes},
};

bool CFortressBuilderCmdHandler::Handle(CSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x0073922c.
    L2_GUARD_FUNCTION();
    char16_t* subcommand = nullptr;
    char16_t* const remaining = ::GetLeftString2(command, &subcommand);
    if (subcommand == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress [command]...");
        return false;
    }

    for (const stFortressBuilderCmdMap& entry : m_CmdMap) {
        if (CompareFortressBuilderIgnoreCase(subcommand, entry.pszCmd) == 0) {
            return entry.pFunc(socket, user, remaining);
        }
    }
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCMd_SetOwnMinutes(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00739368.
    L2_GUARD_FUNCTION();
    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const hoursText = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    char16_t* const minutesText = hoursText != nullptr ? FortressBuilderNextToken(command) : nullptr;
    if (fortressId == nullptr || hoursText == nullptr || minutesText == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress set_own_minutes [fortress id] [hours] [minutes]");
        return false;
    }

    CFortress* const fortress = FindFortressForBuilder(fortressId, false);
    if (fortress == nullptr) {
        return false;
    }

    const std::int32_t totalMinutes = runtime::AddInt32Wrapping(
        runtime::MultiplyInt32Wrapping(FortressBuilderToInt(hoursText), 60), FortressBuilderToInt(minutesText)
    );
    CResidenceData* const data = fortress->m_data;
    data->m_tLastOwnerChangedTime =
        runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), runtime::MultiplyInt32Wrapping(totalMinutes, 60));
    db.RequestOwnFortress(
        fortress, data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0, data->m_tLastOwnerChangedTime
    );
    SendFortressBuilderSystemMessage(
        // Original: u"[%d] 시간 [%d]분 동안 소유로 세팅했습니다."
        socket, u"Ownership set to [%d] hours [%d] minutes.", totalMinutes / 60, totalMinutes % 60
    );
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCMd_GetOwnMinutes(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x007395e0.
    L2_GUARD_FUNCTION();
    char16_t* const fortressId = FortressBuilderNextToken(command);
    if (fortressId == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress get_own_minutes [fortress id]");
        return false;
    }

    CFortress* const fortress = FindFortressForBuilder(fortressId, false);
    if (fortress == nullptr) {
        return false;
    }
    const std::int32_t elapsed =
        runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), fortress->m_data->m_tLastOwnerChangedTime);
    const std::int32_t elapsedMinutes = elapsed / 60;
    SendFortressBuilderSystemMessage(
        // Original: u"[%d] 시간 [%d]분 [%d] 초 동안 소유했습니다."
        socket, u"Owned for [%d] hours [%d] minutes [%d] seconds.", elapsedMinutes / 60, elapsedMinutes % 60, elapsed % 60
    );
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_AllBarracksCaptured(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x007397ac.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1650, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    CFortressSiege* const siege = fortress != nullptr ? fortress->GetSiege() : nullptr;
    if (siege != nullptr) {
        siege->SetAllBarracksCaptured();
        SendFortressBuilderSystemMessage(socket, u"all_barracks_captured accepted");
        return false;
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress all_barracks_captured [fortress id]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_FlagCaptured(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00739958.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1675, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    CFortressSiege* const siege = fortress != nullptr ? fortress->GetSiege() : nullptr;
    if (siege != nullptr) {
        siege->m_bFlagCaptured = true;
        SendFortressBuilderSystemMessage(socket, u"flag_captured accepted");
        return false;
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress flag_captured [fortress id]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SetTreasureLevel(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0073c184.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x172e, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const level = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CFortress* const fortress =
        fortressId != nullptr && level != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress != nullptr) {
        fortress->SetTreasureLevel(FortressBuilderToInt(level));
        SendFortressBuilderSystemMessage(socket, u"set_treasure_level accepted");
        return false;
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress set_treasure_level [fortress id] [treasure level]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SetRewardCycleCount(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0073c350.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1755, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const count = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CFortress* const fortress =
        fortressId != nullptr && count != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress != nullptr) {
        fortress->SetOwnerRewardCycleCount(FortressBuilderToInt(count));
        SendFortressBuilderSystemMessage(socket, u"set_reward_cycle_count accepted");
        return false;
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress set_reward_cycle_count [fortress id] [count]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_ShowInfo(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0073dfb0.
    L2_GUARD_FUNCTION();
    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress show_info [fortress id]");
        return false;
    }

    const ResidenceState state = fortress->m_data != nullptr ? fortress->m_data->m_ResidenceState : RS_MAX;
    const wchar_t* stateText = L"none";
    const wchar_t* siegeText = L"none";
    const wchar_t* contractText = L"none";
    const char16_t* ownerText = u"owned by npc";
    const wchar_t* parentText = L"none";
    std::wstring parentName;
    std::int32_t stateElapsed = 0;
    std::int32_t stateLeft = 0;
    int barracksCaptured = 0;
    std::int32_t barrackCaptureLeft = 0;

    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t stateStart = fortress->m_tStateChangedTime;
    if (state == RS_ANNOUNCING) {
        stateText = L"announcing";
        stateElapsed = runtime::SubtractInt32Wrapping(now, stateStart);
    } else if (state == RS_REGISTRATION) {
        stateText = L"register";
        stateElapsed = runtime::SubtractInt32Wrapping(now, stateStart);
        stateLeft = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(stateStart, now), 3000);
    } else if (state == RS_STANDBY) {
        stateText = L"standby";
        stateElapsed = runtime::SubtractInt32Wrapping(now, stateStart);
        stateLeft = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(stateStart, now), 600);
    } else if (state == RS_SIEGE) {
        stateText = L"siege";
        CFortressSiege* const siege = fortress->GetSiege();
        if (siege != nullptr) {
            stateStart = siege->m_tSiegeStartTime;
            if (siege->m_eSiegeStatus == FSS_BARRACK_CAPTURE) {
                siegeText = L"barrack capture";
                barracksCaptured = siege->GetNumBarracksCaptured();
                if (barracksCaptured > 0) {
                    barrackCaptureLeft = runtime::AddInt32Wrapping(
                        runtime::SubtractInt32Wrapping(siege->m_tFirstBarrackCaptureTime, now), 600
                    );
                }
            } else if (siege->m_eSiegeStatus == FSS_FLAG_CAPTURE) {
                siegeText = L"flag capture";
            }
        }
        stateElapsed = runtime::SubtractInt32Wrapping(now, stateStart);
        stateLeft = runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(stateStart, now), 3600);
    }

    if (fortress->m_nResidenceOwnerPledgeServerId > 0) {
        if (CPledge* const owner = fortress->GetResidenceOwnerPledge()) {
            ownerText = owner->GetPledgeName();
        }

        if (fortress->m_eContractStatus == FCS_CASTLE_CONTRACT) {
            contractText = L"in castle contract";
            if (fortress->m_pParentCastle != nullptr) {
                parentName = FixedUtf16ToWide(fortress->m_pParentCastle->m_data->m_sResidenceName);
                parentText = parentName.c_str();
            }
        } else if (fortress->m_eContractStatus == FCS_INDEPENDENT) {
            contractText = L"independent";
        }
    }

    const std::wstring residenceName = FixedUtf16ToWide(fortress->m_data->m_sResidenceName);
    std::int32_t ownedFor = 0;
    if (fortress->m_data->m_tLastOwnerChangedTime > 0) {
        ownedFor =
            runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), fortress->m_data->m_tLastOwnerChangedTime);
    }
    const int numBarracks = fortress->m_eScale == FST_SMALL ? 3 : fortress->m_eScale == FST_LARGE ? 5 : 0;

    SendFortressBuilderSystemMessage(socket, u"[%s]", residenceName.c_str());
    SendFortressBuilderSystemMessage(socket, u"state [%s] / siege state [%s]", stateText, siegeText);
    SendFortressBuilderSystemMessage(
        socket, u"state elapsed time : [%d] min [%d] sec", stateElapsed / 60, stateElapsed % 60
    );
    SendFortressBuilderSystemMessage(socket, u"state left time : [%d] min [%d] sec", stateLeft / 60, stateLeft % 60);
    SendFortressBuilderSystemMessage(socket, u"num barracks : [%d]", numBarracks);
    SendFortressBuilderSystemMessage(socket, u"num barracks captured : [%d]", barracksCaptured);
    SendFortressBuilderSystemMessage(
        socket, u"barrack capture left time : [%d] min [%d] sec", barrackCaptureLeft / 60, barrackCaptureLeft % 60
    );
    SendFortressBuilderSystemMessage(socket, u"owner pledge : [%s]", ownerText);
    SendFortressBuilderSystemMessage(socket, u"owned for : [%d] min [%d] sec", ownedFor / 60, ownedFor % 60);
    SendFortressBuilderSystemMessage(socket, u"owner defend count : [%d] times", fortress->m_nOwnerSiegeDefendCount);
    SendFortressBuilderSystemMessage(
        socket, u"owner reward count : [%d] cumulative", fortress->m_data->m_nFortressOwnerRewardCycleCount
    );
    SendFortressBuilderSystemMessage(socket, u"treasure level : [%d]", fortress->m_data->m_nFortressCastleTreasureLevel);
    SendFortressBuilderSystemMessage(socket, u"contract status : [%s]", contractText);
    SendFortressBuilderSystemMessage(socket, u"parent castle : [%s]", parentText);
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_ContractCastle(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x0073f220.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x16f7, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const castleIdText = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CFortress* const fortress =
        fortressId != nullptr && castleIdText != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress != nullptr) {
        const int castleId = FortressBuilderToInt(castleIdText);
        if (castleId == -1) {
            static_cast<void>(fortress->GoIndependent());
            return false;
        }

        CResidence* const residence = g_ResidenceDB.GetResidence(castleId);
        if (residence != nullptr && residence->IsCastle()) {
            CCastle* const castle = residence->CastCastle();
            if (castle != nullptr) {
                static_cast<void>(fortress->ContractWithCastle(castle));
                SendFortressBuilderSystemMessage(socket, u"contract_castle accepted");
                return false;
            }
        }
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress contract_castle [fortress id] [castle id]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SiegeUnregister(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00740890.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1449, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const pledgeName = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CPledge* pledge = nullptr;
    if (fortressId != nullptr && pledgeName != nullptr) {
        {
            CSPointer<CPledge> holder = FindFortressBuilderPledge(pledgeName);
            pledge = holder.get();
        }
        CFortress* const fortress = pledge != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
        if (fortress != nullptr && fortress->UnregisterSiege(pledge->GetDbId()) != 0) {
            SendFortressBuilderSystemMessage(socket, u"siege_unregister accepted");
            return false;
        }
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress siege_unregister [fortress id] [pledge name]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_ResetOwner(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00741804.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x16d6, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress != nullptr) {
        fortress->ResetOwnership();
        SendFortressBuilderSystemMessage(socket, u"reset_owner accepted");
        return false;
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress reset_owner [fortress id]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SetOwner(CSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x00742f34.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x169a, gatedResult)) {
        return gatedResult;
    }
    if (user == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        return false;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const pledgeName = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CPledge* pledge = nullptr;
    if (fortressId != nullptr && pledgeName != nullptr) {
        {
            CSPointer<CPledge> holder = FindFortressBuilderPledge(pledgeName);
            pledge = holder.get();
        }
        CFortress* const fortress = pledge != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
        const unsigned int residenceId = fortress != nullptr
            ? static_cast<unsigned int>(fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0)
            : 0;
        if (fortress != nullptr && pledge->CanOwnFortress(residenceId)) {
            if (fortress->m_nResidenceOwnerPledgeServerId <= 0) {
                fortress->SetOwnership(pledge, false);
            } else {
                fortress->ChangeOwnership(pledge, false);
            }
            if (fortress->m_pNpcMakerEventHandler != nullptr) {
                fortress->m_pNpcMakerEventHandler->Send(5, -1, -1, -1, L"");
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"BuilderCmd_SetOwner : m_pNpcMakerEventHandler is null")
                // L2_LOG_ERROR(Log,
                //     L"BuilderCmd_SetOwner : m_pNpcMakerEventHandler is null")
                L2_LOG_ERROR(
                    Log,
                    "Fortress %u: builder ownership operation has run, but maker notification 5 cannot be sent because its "
                    "handler is missing.",
                    residenceId
                );
            }
            SendFortressBuilderSystemMessage(socket, u"set_owner accepted");
            return false;
        }
    }
    SendFortressBuilderSystemMessage(socket, u"//fortress set_owner [fortress id] [pledge name]");
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SiegeRegister(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00744570.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1414, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    char16_t* const pledgeName = fortressId != nullptr ? FortressBuilderNextToken(command) : nullptr;
    CPledge* pledge = nullptr;
    if (fortressId != nullptr && pledgeName != nullptr) {
        {
            CSPointer<CPledge> holder = FindFortressBuilderPledge(pledgeName);
            pledge = holder.get();
        }
    }
    if (pledge == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress siege_register [fortress id] [pledge name]");
        return false;
    }

    CFortress* const fortress = FindFortressForBuilder(fortressId, true);
    if (fortress != nullptr && fortress->RegisterSiege(pledge->GetDbId()) == -1) {
        SendFortressBuilderSystemMessage(socket, u"siege_register accepted");
    } else {
        SendFortressBuilderSystemMessage(socket, u"siege_register fail");
    }
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_ShowSiegeRegistery(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x007447c0.
    L2_GUARD_FUNCTION();
    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress show_siege_registry [fortress id]");
        return false;
    }

    CFortressSiegeRegistry* const registry = fortress->GetSiegeRegistry();
    if (registry == nullptr) {
        return false;
    }

    const bool empty = [&] {
        L2_GUARD(registry->m_AttackerPledgeSetLock);
        const bool empty = registry->m_sAttackerPledgeSet.empty();
        return empty;
    }();
    if (empty) {
        return false;
    }

    std::vector<CPledge*> pledges;
    registry->GetRegisteredPledgeList(pledges);
    for (CPledge* const pledge : pledges) {
        if (pledge != nullptr) {
            SendFortressBuilderSystemMessage(socket, u"[%s]", pledge->GetPledgeName());
        }
    }
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SiegeStandby(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00745df8.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x14fa, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    CResidence* const residence =
        fortressId != nullptr ? g_ResidenceDB.GetResidence(FortressBuilderToInt(fortressId)) : nullptr;
    if (residence == nullptr || !residence->IsFortress()) {
        SendFortressBuilderSystemMessage(socket, u"//fortress siege_standby [fortress id]");
        return false;
    }

    CFortress* const fortress = residence->CastFortress();
    if (fortress == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"siege_standby fail. Fortress is not valid");
    } else if (fortress->SiegeStandby()) {
        SendFortressBuilderSystemMessage(socket, u"siege_standby accepted");
    } else {
        SendFortressBuilderSystemMessage(socket, u"siege_standby fail");
    }
    return false;
}

bool CFortressBuilderCmdHandler::BuilderCmd_SiegeStart(CSocket* const socket, User*, char16_t* command) {
    // Current L2Server 0x00748030.
    L2_GUARD_FUNCTION();
    bool gatedResult = false;
    if (!CheckFortressBuilderLevel(socket, 0x1525, gatedResult)) {
        return gatedResult;
    }

    char16_t* const fortressId = FortressBuilderNextToken(command);
    CFortress* const fortress = fortressId != nullptr ? FindFortressForBuilder(fortressId, true) : nullptr;
    if (fortress == nullptr) {
        SendFortressBuilderSystemMessage(socket, u"//fortress siege_start [fortress id]");
    } else if (fortress->SiegeStart()) {
        SendFortressBuilderSystemMessage(socket, u"siege_start accepted");
    } else {
        SendFortressBuilderSystemMessage(socket, u"siege_start fail");
    }
    return false;
}

std::int32_t CFortress::m_nFlagItemClassId = -1;

const std::array<CFortress::FortressFacilityInfo, 5> CFortress::m_mFacilityMap{{
    {0, 2},
    {1, 2},
    {2, 1},
    {3, 1},
    {4, 1},
}};

L2_DEFINE_OBJECT_CLASS_ID(CFortress)

L2_DEFINE_OBJECT_CLASS_NAME(CFortress, m_pClassName_CFortress)

L2_DEFINE_OBJECT_ALLOCATOR(CFortress, __GetClassName())

L2_DEFINE_OBJECT_NEW(CFortress)

CFortress::CFortress() :
    m_eLocation(FLT_TERRITORY_DOMAIN),
    m_eScale(FST_MAX),
    m_eContractStatus(FCS_NONE),
    m_alignment4acTo4b0{},
    m_pParentCastle(nullptr),
    m_pSiegeRegistry(nullptr),
    m_pSiege(nullptr),
    m_pFlagpole(nullptr),
    m_tStateChangedTime(0),
    m_tStateElapsedTime(0),
    m_nOwnerSiegeDefendCount(0),
    m_bWaitResume(false),
    m_bWaitLoading(false),
    m_bWaitNpcServerUp(false),
    m_alignment547To548{},
    m_pNpcMakerEventHandler(nullptr),
    m_pNpcAIEventHandler(nullptr),
    m_nDominionSiegeID(0),
    m_tLastCountDownTime(600) {
    // Current L2Server 0x00741b38.
}

CFortress::~CFortress() {
    // Current L2Server 0x00740b88. These four are borrowed current owners.
    m_pParentCastle = nullptr;
    m_pSiegeRegistry = nullptr;
    m_pSiege = nullptr;
    m_pFlagpole = nullptr;
    delete m_pNpcMakerEventHandler;
    m_pNpcMakerEventHandler = nullptr;
    if (m_pNpcAIEventHandler != nullptr) {
        m_pNpcAIEventHandler->Detach();
        delete m_pNpcAIEventHandler;
        m_pNpcAIEventHandler = nullptr;
    }
}

// Current L2Server 0x00740c8c.
L2_DEFINE_OBJECT_TYPED_RELEASE(CFortress)

bool CFortress::IsUnderSiege() const {
    const ResidenceState state = ResidenceStatus(m_data);
    return state == RS_SIEGE || state == RS_MID_VICTORY || IsUnderDominionSiege();
}

bool CFortress::IsUnderDominionSiege() const {
    return m_pParentCastle != nullptr && m_pParentCastle->m_pDominion != nullptr &&
        m_pParentCastle->m_pDominion->m_data != nullptr &&
        m_pParentCastle->m_pDominion->m_data->m_ResidenceState == RS_DOMINION_SIEGE;
}

bool CFortress::IsInSiege() const {
    // Current L2Server 0x0073a3e4.
    L2_GUARD_FUNCTION();
    return ResidenceStatus(m_data) == RS_SIEGE;
}

bool CFortress::IsFortress() const { return true; }

CFortress* CFortress::CastFortress() const { return const_cast<CFortress*>(this); }

int CFortress::GetDominionSiegeID() const { return m_nDominionSiegeID; }

void CFortress::HandleDoorDestroyed(const wchar_t* const doorName) {
    // Current L2Server 0x0073be90.
    L2_GUARD_FUNCTION();
    const ResidenceState state = GetResidenceState();
    if (state == RS_SIEGE) {
        if (m_pSiege == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] door[%s] destroyed in invalid fortress siege [%d]",
            //     L"Fortress.cpp",
            //     0x7b1,
            //     doorName,
            //     GetResidenceID())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] door[%s] destroyed in invalid fortress siege [%d]",
            //     L"Fortress.cpp",
            //     0x7b1,
            //     doorName,
            //     GetResidenceID())
            L2_LOG_ERROR(
                Log, "Fortress %d: cannot process destruction of door '%s' because the siege object is missing.",
                GetResidenceID(), doorName != nullptr ? WideToUtf8(doorName).c_str() : "<null>"
            );
        } else {
            m_pSiege->SetDoorDestroyed(doorName);
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] door[%s] destroyed in invalid fortress state [%d][%d]",
        //     L"Fortress.cpp",
        //     0x7b7,
        //     doorName,
        //     GetResidenceID(),
        //     state)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] door[%s] destroyed in invalid fortress state [%d][%d]",
        //     L"Fortress.cpp",
        //     0x7b7,
        //     doorName,
        //     GetResidenceID(),
        //     state)
        L2_LOG_WARN(
            Log, "Fortress %d: door '%s' destruction ignored in residence state %d; expected SIEGE.", GetResidenceID(),
            doorName != nullptr ? WideToUtf8(doorName).c_str() : "<null>", static_cast<int>(state)
        );
    }
}

void CFortress::StartSiege() {}
void CFortress::EndSiege() {}
void CFortress::PassSiege() {}
void CFortress::ChangeOwner(CPledge*, int, bool) {}
void CFortress::PauseSiege() {}
void CFortress::ResumeSiege() {}

void CFortress::SetFlagpole(CFortressFlagpole* const flagpole) {
    // Current L2Server 0x00737f7c.
    L2_GUARD_FUNCTION();
    g_PendingStaticObjects.RegisterAsPending(flagpole);
    m_pFlagpole = flagpole;
}

void CFortress::InitFortress() {
    // Current L2Server 0x0073802c.
    L2_GUARD_FUNCTION();
    db.RequestLoadAllFortress();
    if (g_JobFinishSignal.m_nMustLoadFortress < 1) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Fortress], count: 0")
        // L2_LOG_INFO(Log,
        //     L"Load Success[Fortress], count: 0")
        L2_LOG_INFO(
            Log,
            "Fortress startup skips the completion wait: expected database count is %d; proceeding with the startup "
            "notification.",
            g_JobFinishSignal.m_nMustLoadFortress
        );
        g_JobFinishSignal.NotifyFinish(JFT_LOAD_FORTRESS);
        CPledge::SetFlagReservePledge(PRLT_FORTRESS);
    } else if (!g_JobFinishSignal.WaitFinish(JFT_LOAD_FORTRESS, 10)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Problem occurred in Loading Fortress")
        // L2_LOG_ERROR(Log,
        //     L"Problem occurred in Loading Fortress")
        L2_LOG_ERROR(
            Log,
            "Fortress startup continues without load confirmation after a wait with a 10-second timeout; expected database "
            "count %d.",
            g_JobFinishSignal.m_nMustLoadFortress
        );
    } else {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Load Success[Fortress]: %d",
        //     g_JobFinishSignal.m_nMustLoadFortress)
        // L2_LOG_INFO(Log,
        //     L"Load Success[Fortress]: %d",
        //     g_JobFinishSignal.m_nMustLoadFortress)
        L2_LOG_INFO(
            Log, "Fortress load completion signal received; expected database count %d.",
            g_JobFinishSignal.m_nMustLoadFortress
        );
    }
}

void CFortress::StartAllTimer() {
    // Current L2Server 0x0073d1f8.
    L2_GUARD_FUNCTION();
    L2_GUARD(g_ResidenceDB.m_ResidenceMapLock);
    for (const auto& entry : g_ResidenceDB.m_ResidenceMap) {
        CResidence* const residence = entry.second;
        if (!residence->IsFortress()) {
            continue;
        }
        CFortress* const fortress = residence->CastFortress();
        if (fortress == nullptr) {
            continue;
        }
        const int ownerPledgeId = fortress->m_data->m_nResidenceOwnerID;
        if (ownerPledgeId != 0) {
            CSPointer<CPledge> owner = CPledge::FindPledge(g_nWorldId, ownerPledgeId);
            if (!owner) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot find Fortress Owner Pledge ID[%d]",
                //     ownerPledgeId)
                // L2_LOG_ERROR(Log,
                //     L"Cannot find Fortress Owner Pledge ID[%d]",
                //     ownerPledgeId)
                L2_LOG_ERROR(
                    Log,
                    "Fortress %d: owner clan %d is unavailable; ownership binding is skipped and timer startup continues.",
                    ResidenceId(fortress->m_data), ownerPledgeId
                );
            } else {
                fortress->SetPledge(owner.get());
                owner->SetFortress(fortress);
            }
        }
        fortress->StartTimer();
    }
}

void CFortress::AddFlag(FVector& position) {
    // Current L2Server 0x00742304.
    L2_GUARD_FUNCTION();
    m_vFlagPositionList.push_back(position);
}

void CFortress::GetFlags(std::vector<FVector>& flags) {
    // Current L2Server 0x007423bc appends and does not clear
    // the caller-owned vector.
    L2_GUARD_FUNCTION();
    for (const FVector& position : m_vFlagPositionList) {
        flags.push_back(position);
    }
}

void CFortress::AddRelatedCastle(const int residenceId) {
    // Current L2Server 0x0073e988.
    L2_GUARD_FUNCTION();
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->IsCastle() || residence->CastCastle() == nullptr) {
        return;
    }
    if (m_sRelatedCastleList.find(static_cast<std::uint32_t>(residenceId)) == m_sRelatedCastleList.end()) {
        m_sRelatedCastleList.insert(static_cast<std::uint32_t>(residenceId));
    }
}

void CFortress::AddToRelatedCastles() {
    // Current L2Server 0x0073cb60.
    L2_GUARD_FUNCTION();
    if (m_sRelatedCastleList.size() > 1U) {
        m_eLocation = FLT_TERRITORY_BOUNDARY;
        m_data->m_bFortressIsInBoundary = true;
    }

    for (const std::uint32_t residenceId : m_sRelatedCastleList) {
        CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(residenceId));
        if (residence != nullptr && residence->IsCastle()) {
            CCastle* const castle = residence->CastCastle();
            if (castle != nullptr) {
                castle->AddRelatedFortress(this);
            }
        }
    }
}

bool CFortressSiegeRegistry::RegisterPledge(const std::uint32_t pledgeId) {
    // Current L2Server 0x0073ec64.
    L2_GUARD_FUNCTION();
    const bool absent = [&] {
        L2_GUARD(m_AttackerPledgeSetLock);
        const bool absent = m_sAttackerPledgeSet.find(pledgeId) == m_sAttackerPledgeSet.end();
        if (absent) {
            m_sAttackerPledgeSet.insert(pledgeId);
        }
        return absent;
    }();
    return absent;
}

bool CFortressSiegeRegistry::IsRegisteredPledge(const std::uint32_t pledgeId) {
    // Current L2Server 0x0073d938.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AttackerPledgeSetLock);
    return m_sAttackerPledgeSet.find(pledgeId) != m_sAttackerPledgeSet.end();
}

bool CFortressSiegeRegistry::UnregisterPledge(const std::uint32_t pledgeId) {
    // Current L2Server 0x0073fb18.
    L2_GUARD_FUNCTION();
    const bool present = [&] {
        L2_GUARD(m_AttackerPledgeSetLock);
        const auto position = m_sAttackerPledgeSet.find(pledgeId);
        const bool present = position != m_sAttackerPledgeSet.end();
        if (present) {
            m_sAttackerPledgeSet.erase(position);
        }
        return present;
    }();
    return present;
}

void CFortressSiegeRegistry::GetRegisteredPledgeList(std::vector<CPledge*>& pledges) {
    // Current L2Server 0x00743b30.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_AttackerPledgeSetLock);
    for (const std::uint32_t pledgeId : m_sAttackerPledgeSet) {
        CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
        if (pledge) {
            pledges.push_back(pledge.get());
        }
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CFortressSiege)

L2_DEFINE_OBJECT_CLASS_NAME(CFortressSiege, m_pClassName_CFortressSiege)

L2_DEFINE_OBJECT_ALLOCATOR(CFortressSiege, __GetClassName())

L2_DEFINE_OBJECT_NEW(CFortressSiege)

CFortressSiege::CFortressSiege(
    const int fortressId,
    const FortressScaleType scale,
    CFortressSiegeRegistry* const registry,
    CFortressNpcMakerEventHandler* const makerEventHandler,
    CFortressNpcAIEventHandler* const aiEventHandler,
    const bool dominionSiege
) :
    m_eSiegeStatus(FSS_NONE),
    m_alignment1cTo20{},
    m_nNumBarracks(0),
    m_bFlagCaptured(false),
    m_bAllBarracksCaptured(false),
    m_bFirstFlagCreation(true),
    m_alignment6fTo70{},
    m_nWinnerPledgeDbId(0),
    m_bPaused(false),
    m_alignment75To78{},
    m_nFortressId(fortressId),
    m_bIsDominionSiege(dominionSiege),
    m_alignment7dTo80{},
    m_pRegistry(registry),
    m_tSiegeStartTime(0),
    m_tFirstBarrackCaptureTime(0),
    m_tSiegeElapsedTime(0),
    m_nSiegeElapsedTimeSaveTick(0),
    m_nNumFlags(0),
    m_alignmentBcToC0{},
    m_sCurrentMusicName(nullptr),
    m_pNpcMakerEventHandler(makerEventHandler),
    m_pNpcAIEventHandler(aiEventHandler) {
    // Current L2Server 0x0074636c.
    if (scale == FST_SMALL) {
        m_nNumBarracks = 3;
    } else if (scale == FST_LARGE) {
        m_nNumBarracks = 5;
    }
    {
        L2_GUARD(m_BarrackCapturedLock);
        m_vBarrackCaptured.resize(static_cast<std::size_t>(m_nNumBarracks));
        for (auto it = m_vBarrackCaptured.begin(); it != m_vBarrackCaptured.end(); ++it) {
            *it = false;
        }
    }
}

CFortressSiege::~CFortressSiege() {
    // Current L2Server 0x007466f0.
    m_pRegistry = nullptr;
    m_sCurrentMusicName = nullptr;
    m_pNpcMakerEventHandler = nullptr;
    m_pNpcAIEventHandler = nullptr;
}

// Current L2Server 0x007465b0.
L2_DEFINE_OBJECT_TYPED_RELEASE(CFortressSiege)

void CFortressSiege::StartTimer() {
    // Current L2Server 0x00738bc8.
    L2_GUARD_FUNCTION();
    AddTimer(10000, 0);
}

void CFortressSiege::Pause() {
    // Current L2Server 0x00738c74.
    L2_GUARD_FUNCTION();
    m_bPaused = true;
}

void CFortressSiege::SetAllBarracksCaptured() {
    // Current L2Server 0x00738ee8.
    L2_GUARD_FUNCTION();
    if (m_eSiegeStatus == FSS_BARRACK_CAPTURE) {
        m_bAllBarracksCaptured = true;
    }
}

void CFortressSiege::SetProtectedNpcDie() {
    // Current L2Server 0x00738f7c. The spelling is historical: the current
    // body clears the first-flag lane only when an owner pledge exists.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress != nullptr && fortress->m_nResidenceOwnerPledgeServerId > 0) {
        m_bFirstFlagCreation = false;
    }
}

void CFortressSiege::SetDoorDestroyed(const wchar_t* const doorName) {
    // Current L2Server 0x0073b0b8.
    L2_GUARD_FUNCTION();
    if (m_pNpcMakerEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"SetDoorDestroyed : m_pNpcMakerEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"SetDoorDestroyed : m_pNpcMakerEventHandler is null")
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot send door-destruction event for '%s'; the NPC maker handler is missing.",
            m_nFortressId, doorName != nullptr ? WideToUtf8(doorName).c_str() : "<null>"
        );
    } else {
        m_pNpcMakerEventHandler->Send(6, -1, -1, -1, doorName);
    }
}

int CFortressSiege::GetNumBarracksCaptured() {
    // Current L2Server 0x0073da3c.
    L2_GUARD_FUNCTION();
    int count = 0;
    {
        L2_GUARD(m_BarrackCapturedLock);
        for (auto it = m_vBarrackCaptured.begin(); it != m_vBarrackCaptured.end(); ++it) {
            if (*it) {
                ++count;
            }
        }
    }
    return count;
}

bool CFortressSiege::GetBarrackStatusList(std::vector<bool>& statusList) {
    // Current L2Server 0x00742800. The target releases this
    // lock before copying the vector. Preserve that observable race window.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_BarrackCapturedLock);
    }
    statusList = m_vBarrackCaptured;
    return true;
}

void CFortressSiege::SetStatus(const FortressSiegeStatus status) {
    // Current L2Server 0x0073afa4.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressSiegeLock);
        m_eSiegeStatus = status;
        if (CFortress* const fortress = GetFortress()) {
            if (fortress->m_data != nullptr) {
                fortress->m_data->m_nFortressSiegeStatus = status;
            }
        }
        db.RequestSaveFortressSiegeState(m_nFortressId, status, m_tSiegeStartTime, m_tSiegeElapsedTime, true);
    }
}

CFortress* CFortressSiege::GetFortress() {
    L2_GUARD_FUNCTION();

    CResidence* const residence = g_ResidenceDB.GetResidence(m_nFortressId);
    if (residence == nullptr || !residence->IsFortress()) {
        return nullptr;
    }
    return residence->CastFortress();
}

void CFortressSiege::AddSiegeLog(wchar_t* const format, const int logId, const FortressSiegeStatus status) {
    // Current L2Server 0x0073b194.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }

    const std::int64_t residenceId = static_cast<std::int64_t>(ResidenceId(fortress->m_data));
    if (fortress->m_nResidenceOwnerPledgeServerId < 1) {
        if (logId == 0x7d9) {
            Log.Add(
                LOG_BOTH, format, static_cast<std::int64_t>(0x7d9), std::int64_t{0}, std::int64_t{0}, L"owned by npc",
                std::int64_t{0}, residenceId, static_cast<std::int64_t>(status), std::int64_t{2}, L"", L""
            );
        } else {
            Log.Add(
                LOG_BOTH, format, static_cast<std::int64_t>(logId), std::int64_t{0}, std::int64_t{0}, L"owned by npc",
                std::int64_t{0}, residenceId, static_cast<std::int64_t>(status), L"", L""
            );
        }
        return;
    }

    CPledge* const pledge = fortress->GetResidenceOwnerPledge();
    if (pledge == nullptr) {
        return;
    }
    User* const master = pledge->GetMaster();
    if (master == nullptr) {
        return;
    }

    if (logId == 0x7d9) {
        const char16_t* const masterName = master->GetName();
        const bool firstFlagCreation = m_bFirstFlagCreation;
        const std::uint32_t pledgeId = pledge->GetDbId();
        const char16_t* const pledgeName = pledge->GetPledgeName();
        const std::uint32_t accountId = master->GetAccountID();
        const std::uint32_t masterId = master->GetDBID();
        // Log.Add(
        //     LOG_BOTH,
        //     format,
        //     static_cast<std::int64_t>(0x7d9),
        //     static_cast<std::int64_t>(masterId),
        //     static_cast<std::int64_t>(accountId),
        //     pledgeName,
        //     static_cast<std::int64_t>(pledgeId),
        //     residenceId,
        //     static_cast<std::int64_t>(status),
        //     static_cast<std::int64_t>(firstFlagCreation),
        //     masterName,
        //     master->m_sAccountName.data())
        // Log.Add(
        //     LOG_BOTH,
        //     format,
        //     static_cast<std::int64_t>(0x7d9),
        //     static_cast<std::int64_t>(masterId),
        //     static_cast<std::int64_t>(accountId),
        //     pledgeName,
        //     static_cast<std::int64_t>(pledgeId),
        //     residenceId,
        //     static_cast<std::int64_t>(status),
        //     static_cast<std::int64_t>(firstFlagCreation),
        //     Utf16ToWide(masterName).c_str(),
        //     master->m_sAccountName.data())
        Log.Add(
            LOG_BOTH, format, static_cast<std::int64_t>(0x7d9), static_cast<std::int64_t>(masterId),
            static_cast<std::int64_t>(accountId), pledgeName, static_cast<std::int64_t>(pledgeId), residenceId,
            static_cast<std::int64_t>(status), static_cast<std::int64_t>(firstFlagCreation), masterName,
            master->m_sAccountName.data()
        );
        return;
    }

    const char16_t* const masterName = master->GetName();
    const std::uint32_t pledgeId = pledge->GetDbId();
    const char16_t* const pledgeName = pledge->GetPledgeName();
    const std::uint32_t accountId = master->GetAccountID();
    const std::uint32_t masterId = master->GetDBID();
    // Log.Add(
    //     LOG_BOTH,
    //     format,
    //     static_cast<std::int64_t>(logId),
    //     static_cast<std::int64_t>(masterId),
    //     static_cast<std::int64_t>(accountId),
    //     pledgeName,
    //     static_cast<std::int64_t>(pledgeId),
    //     residenceId,
    //     static_cast<std::int64_t>(status),
    //     masterName,
    //     master->m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     format,
    //     static_cast<std::int64_t>(logId),
    //     static_cast<std::int64_t>(masterId),
    //     static_cast<std::int64_t>(accountId),
    //     pledgeName,
    //     static_cast<std::int64_t>(pledgeId),
    //     residenceId,
    //     static_cast<std::int64_t>(status),
    //     Utf16ToWide(masterName).c_str(),
    //     master->m_sAccountName.data())
    Log.Add(
        LOG_BOTH, format, static_cast<std::int64_t>(logId), static_cast<std::int64_t>(masterId),
        static_cast<std::int64_t>(accountId), pledgeName, static_cast<std::int64_t>(pledgeId), residenceId,
        static_cast<std::int64_t>(status), masterName, master->m_sAccountName.data()
    );
}

bool CFortressSiege::IsMyFlag(CItem* item) {
    L2_GUARD_FUNCTION();
    return item != nullptr && m_mFlagMap.find(item->Id()) != m_mFlagMap.end();
}

int CFortressSiege::GetNumFlagInWorld() {
    // Current L2Server 0x0073decc.
    L2_GUARD_FUNCTION();
    int count = 0;
    for (const auto& entry : m_mFlagMap) {
        if (entry.second.second == 0) {
            ++count;
        }
    }
    return count;
}

void CFortressSiege::PushFlagItem(CItem* const item, const FVector& position) {
    // Current L2Server 0x007413d4.
    L2_GUARD_FUNCTION();
    CWorld::Inst().PutItem(item, position, nullptr);
    m_mFlagMap[item->Id()] = std::make_pair(position, std::uint32_t{0});
}

void CFortress::BroadcastSystemMessageW(int messageId, eSMsgType type, const char16_t* text, bool includeOwner) {
    // Current L2Server 0x007451d8.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    if (m_pSiegeRegistry != nullptr) {
        m_pSiegeRegistry->GetRegisteredPledgeList(pledges);
    }
    if (includeOwner && m_nResidenceOwnerPledgeServerId > 0) {
        pledges.push_back(GetResidenceOwnerPledge());
    }

    for (CPledge* const pledge : pledges) {
        if (pledge == nullptr) {
            continue;
        }
        std::vector<User*> members;
        pledge->GetAllMember(members, false);
        for (User* const member : members) {
            if (member != nullptr) {
                member->GetSocket()->Send("chccS", 0x62, static_cast<short>(messageId), 1, static_cast<char>(type), text);
            }
        }
    }
}

void CFortressSiege::BroadcastSystemMessageW(int messageId, eSMsgType type, const char16_t* text) {
    // Current L2Server 0x00745d1c.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress != nullptr) {
        fortress->BroadcastSystemMessageW(messageId, type, text, true);
    }
}

void CFortressSiege::FlagPicked(User* user, CItem* item) {
    // Current L2Server 0x00746cf4.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr || user == nullptr || item == nullptr) {
        return;
    }

    const auto position = m_mFlagMap.find(item->Id());
    if (position == m_mFlagMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag item not in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x11c3,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag item not in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x11c3,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot track flag pickup by character DB ID %u, item DB ID %u; the flag has no registry entry.",
            ResidenceId(fortress->m_data), static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(item->GetDBID())
        );
        return;
    }

    position->second.second = user->Id();
    BroadcastSystemMessageW(SystemMessageId::CHARACTER_ACQUIRED_FLAG, EST_CHAR_NAME, user->GetName());

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }
    const int flagCount = GetNumFlagInWorld();
    const FVector position3D = user->GetPos();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,",
    //     0x7dc,
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     static_cast<std::int64_t>(position3D.x),
    //     static_cast<std::int64_t>(position3D.y),
    //     static_cast<std::int64_t>(position3D.z),
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(
    //         fortress->m_data != nullptr
    //             ? fortress->m_data->m_nResidenceID
    //             : 0),
    //     static_cast<std::int64_t>(flagCount),
    //     user->GetName(),
    //     user->m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,",
    //     0x7dc,
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     static_cast<std::int64_t>(position3D.x),
    //     static_cast<std::int64_t>(position3D.y),
    //     static_cast<std::int64_t>(position3D.z),
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(
    //         fortress->m_data != nullptr
    //             ? fortress->m_data->m_nResidenceID
    //             : 0),
    //     static_cast<std::int64_t>(flagCount),
    //     Utf16ToWide(user->GetName()).c_str(),
    //     user->m_sAccountName.data())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,",
        0x7dc, static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()),
        static_cast<std::int64_t>(position3D.x), static_cast<std::int64_t>(position3D.y),
        static_cast<std::int64_t>(position3D.z), pledge->GetPledgeName(), static_cast<std::int64_t>(pledge->GetDbId()),
        static_cast<std::int64_t>(fortress->m_data != nullptr ? fortress->m_data->m_nResidenceID : 0),
        static_cast<std::int64_t>(flagCount), user->GetName(), user->m_sAccountName.data()
    );
}

void CFortressSiege::DeleteFlag(User* user, CItem* item, bool notify) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || item == nullptr) {
        return;
    }

    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }

    const auto position = m_mFlagMap.find(item->Id());
    if (position == m_mFlagMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag item not in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x119c,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag item not in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x119c,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot remove flag registration for character DB ID %u, item DB ID %u; no registry entry exists.",
            ResidenceId(fortress->m_data), static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(item->GetDBID())
        );
        return;
    }

    const std::pair<FVector, std::uint32_t> flag = position->second;
    m_mFlagMap.erase(position);

    if (user->Id() != flag.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag item owner doesn't match in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x1184,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag item owner doesn't match in flag map, "
        //     L"fortress[%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x1184,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     user->GetDBID(),
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Fortress %d: flag carrier mismatch for character DB ID %u, item DB ID %u (tracked carrier SID %u); the "
            "registry entry was already erased and deletion handling continues.",
            ResidenceId(fortress->m_data), static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(item->GetDBID()),
            static_cast<unsigned>(flag.second)
        );
    }

    if (!notify || m_eSiegeStatus != FSS_FLAG_CAPTURE) {
        return;
    }

    const int flagCount = static_cast<int>(m_mFlagMap.size());
    if (flagCount < m_nNumFlags) {
        CItem* const replacement = g_ObjectDB.CreateItem(CFortress::m_nFlagItemClassId);
        if (replacement != nullptr) {
            CWorld::Inst().PutItem(replacement, flag.first, nullptr);
            m_mFlagMap[replacement->Id()] = std::make_pair(flag.first, std::uint32_t{0});
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] flag item not destroyed properly, "
    //     L"fortress[%d] num flags[%d]",
    //     L"Fortress.cpp",
    //     0x1195,
    //     fortress->m_data != nullptr
    //         ? fortress->m_data->m_nResidenceID
    //         : 0,
    //     flagCount)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] flag item not destroyed properly, "
    //     L"fortress[%d] num flags[%d]",
    //     L"Fortress.cpp",
    //     0x1195,
    //     fortress->m_data != nullptr
    //         ? fortress->m_data->m_nResidenceID
    //         : 0,
    //     flagCount)
    L2_LOG_ERROR(
        Log,
        "Fortress %d: flag registry still contains %d entries after removal (configured flag count %d); no replacement flag "
        "is created.",
        ResidenceId(fortress->m_data), flagCount, m_nNumFlags
    );
}

CFortressSiege* CFortress::GetSiege() {
    L2_GUARD_FUNCTION();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    return state == RS_SIEGE ? m_pSiege : nullptr;
}

CFortressSiegeRegistry* CFortress::GetSiegeRegistry() {
    // Current L2Server 0x0073898c.
    L2_GUARD_FUNCTION();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    return state == RS_REGISTRATION || state == RS_STANDBY || state == RS_SIEGE ? m_pSiegeRegistry : nullptr;
}

int CFortress::GetNumBarracks() {
    // Current L2Server 0x00738534.
    L2_GUARD_FUNCTION();
    if (m_eScale == FST_SMALL) {
        return 3;
    }
    if (m_eScale == FST_LARGE) {
        return 5;
    }
    return 0;
}

int CFortress::GetNumBarracksCapturedInSiege() {
    // Current L2Server 0x0073eacc.
    L2_GUARD_FUNCTION();
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    if (state == RS_SIEGE) {
        CFortressSiege* const siege = GetSiege();
        if (siege != nullptr) {
            return siege->GetNumBarracksCaptured();
        }
    }
    return 0;
}

void CFortress::SendMapInfo(User* const user) {
    // Current L2Server 0x00744dac.
    L2_GUARD_FUNCTION();

    FortressSiegeStatus siegeStatus = FSS_NONE;
    CPacketHelperEx<20480> packet;
    int barrackCount = 0;
    const ResidenceState state = m_data != nullptr ? m_data->m_ResidenceState : RS_MAX;
    if (state == RS_SIEGE) {
        CFortressSiege* const siege = GetSiege();
        if (siege != nullptr) {
            siegeStatus = siege->m_eSiegeStatus;
            if (siegeStatus == FSS_BARRACK_CAPTURE) {
                std::vector<bool> statusList;
                siege->GetBarrackStatusList(statusList);
                for (const bool captured : statusList) {
                    packet.Assemble("d", static_cast<int>(captured));
                    ++barrackCount;
                }
            }
        }
    }

    if (user != nullptr) {
        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        user->GetSocket()->Send(
            "chdddb", 0xfe, 0x7e, residenceId, static_cast<int>(siegeStatus), barrackCount, packet.GetLength(),
            packet.GetPacket()
        );
    }
}

bool CFortress::CanFlagBePicked(User* user, CPledge* pledge, CItem* item, bool force) {
    L2_GUARD_FUNCTION();
    if (user == nullptr || pledge == nullptr || item == nullptr || user->IsInVehicle() || user->IsInAirShip() ||
        user->IsInShuttle() || user->IsRiding()) {
        return false;
    }
    if (!force && user->m_inv.HaveItemByClassId(m_nFlagItemClassId, 1, false, false)) {
        return false;
    }

    CFortress* const offence = pledge->m_pFortressUnderOffenceSiege;
    if (offence == nullptr || offence->m_data == nullptr || offence->m_data->m_ResidenceState != RS_SIEGE) {
        return false;
    }

    CFortressSiege* const siege = GetSiege();
    return siege != nullptr && siege->IsMyFlag(item);
}

void CFortress::HandleDeleteFlag(User* user, bool notify) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    user->m_bHasFortressFlag = false;
    CItem* const item = [&] {
        L2_READ_GUARD(user->m_inv);
        CItem* const item = user->m_inv.GetFirstItemByClassID(m_nFlagItemClassId, false, false);
        return item;
    }();

    if (item == nullptr) {
        return;
    }

    if (user->IsInFortressSiege()) {
        CPledge* const pledge = user->GetPledge();
        if (pledge != nullptr && pledge->m_pFortressUnderOffenceSiege != nullptr) {
            CFortress* const fortress = pledge->m_pFortressUnderOffenceSiege;
            const ResidenceState state = fortress->m_data != nullptr ? fortress->m_data->m_ResidenceState : RS_MAX;
            if (state == RS_SIEGE) {
                CFortressSiege* const siege = fortress->GetSiege();
                if (siege != nullptr) {
                    siege->DeleteFlag(user, item, notify);
                }
            }
        }
    }

    db.RequestDestroyItem(item, 1, user, 0);
}

bool CFortress::HandleFlagPicked(CFortress* fortress, User* user, CItem* item) {
    // Current L2Server 0x00747698.
    L2_GUARD_FUNCTION();
    if (fortress == nullptr || user == nullptr || item == nullptr) {
        return false;
    }
    CFortressSiege* const siege = fortress->GetSiege();
    if (siege == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag item picked, siege object not properly "
        //     L"created, fortress[%d][%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x7ec,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_ResidenceState
        //         : RS_MAX,
        //     user->GetDBID(),
        //     item->GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag item picked, siege object not properly "
        //     L"created, fortress[%d][%d] user[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x7ec,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_nResidenceID
        //         : 0,
        //     fortress->m_data != nullptr
        //         ? fortress->m_data->m_ResidenceState
        //         : RS_MAX,
        //     user->GetDBID(),
        //     item->GetDBID())
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot process flag pickup by character DB ID %u, item DB ID %u; no usable siege object in "
            "residence state %d.",
            ResidenceId(fortress->m_data), static_cast<unsigned>(user->GetDBID()), static_cast<unsigned>(item->GetDBID()),
            static_cast<int>(ResidenceStatus(fortress->m_data))
        );
        return false;
    }
    siege->FlagPicked(user, item);
    return true;
}

void CFortress::SetOwnerPledge(CPledge* const pledge) {
    // Current L2Server 0x0073a4ac.
    {
        L2_GUARD(m_FortressLock);
        CResidence::SetPledge(pledge);
    }
}

void CFortress::SetLastRewardGivenTime(const std::int32_t time) {
    // Current L2Server 0x0073883c.
    L2_GUARD_FUNCTION();
    m_data->m_tLastRewardGivenTime = time;
}

void CFortress::SetStateInfoFromDB(const int state, const int stateChangedTime, const int stateElapsedTime) {
    // Current L2Server 0x0073a520.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressLock);
        ChangeResidenceState(static_cast<ResidenceState>(state));
        m_tStateChangedTime = stateChangedTime;
        m_tStateElapsedTime = stateElapsedTime;
    }
}

void CFortress::SetOwnerInfoFromDB(const int pledgeId, const int lastOwnerChangedTime, const int ownerSiegeDefendCount) {
    // Current L2Server 0x00741294.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressLock);
        m_data->m_tLastOwnerChangedTime = lastOwnerChangedTime;
    }
    if (pledgeId == 0) {
        return;
    }

    m_data->m_nResidenceOwnerID = static_cast<std::uint32_t>(pledgeId);
    CPledge::ReserveLoaded(g_nWorldId, pledgeId, PRLT_FORTRESS);
    SetFlagpole();
    {
        L2_GUARD(m_FortressLock);
        m_nOwnerSiegeDefendCount = ownerSiegeDefendCount;
    }
}

void CFortress::SetContractInfoFromDB(const int pledgeId, const int contractStatus, const int parentCastleId) {
    // Current L2Server 0x0073d6fc.
    L2_GUARD_FUNCTION();
    if (pledgeId == 0) {
        return;
    }
    if (contractStatus == FCS_INDEPENDENT) {
        {
            L2_GUARD(m_FortressLock);
            m_pParentCastle = nullptr;
            m_data->m_nFortressParentCastleId = 0;
            m_eContractStatus = FCS_INDEPENDENT;
            m_data->m_nFortressContractStatus = FCS_INDEPENDENT;
        }
        return;
    }
    if (contractStatus != FCS_CASTLE_CONTRACT) {
        return;
    }

    CCastle* const castle = static_cast<CCastle*>(g_ResidenceDB.GetResidence(parentCastleId));
    if (castle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid parent castle[%d] in loading fortress[%d] from db",
        //     L"Fortress.cpp",
        //     0xa85,
        //     parentCastleId,
        //     ResidenceId(m_data))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid parent castle[%d] in loading fortress[%d] from db",
        //     L"Fortress.cpp",
        //     0xa85,
        //     parentCastleId,
        //     ResidenceId(m_data))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot restore the contract with castle %d for clan %d; the parent residence was not found and "
            "contract state is retained.",
            ResidenceId(m_data), parentCastleId, pledgeId
        );
        return;
    }

    const unsigned int castleId = static_cast<unsigned int>(ResidenceId(castle->m_data));
    if (!IsRelatedCastle(castleId) || !castle->IsRelatedFortress(ResidenceId(m_data))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] not related parent castle[%d] in loading fortress[%d] from db",
        //     L"Fortress.cpp",
        //     0xa8d,
        //     parentCastleId,
        //     ResidenceId(m_data))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] not related parent castle[%d] in loading fortress[%d] from db",
        //     L"Fortress.cpp",
        //     0xa8d,
        //     parentCastleId,
        //     ResidenceId(m_data))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot restore the contract with castle %d for clan %d; the mutual fortress/castle relationship "
            "check failed and contract state is retained.",
            ResidenceId(m_data), parentCastleId, pledgeId
        );
        return;
    }

    castle->SetChildFortress(this);
    {
        L2_GUARD(m_FortressLock);
        m_pParentCastle = castle;
        m_data->m_nFortressParentCastleId = ResidenceId(castle->m_data);
        m_eContractStatus = FCS_CASTLE_CONTRACT;
        m_data->m_nFortressContractStatus = FCS_CASTLE_CONTRACT;
    }
}

void CFortress::SetRewardInfoFromDB(
    const int lastRewardGivenTime, const int ownerRewardCycleCount, int castleTreasureLevel
) {
    // Current L2Server 0x0073a618.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressLock);
        SetLastRewardGivenTime(lastRewardGivenTime);
        m_data->m_nFortressOwnerRewardCycleCount = ownerRewardCycleCount;
        if (castleTreasureLevel > 6) {
            castleTreasureLevel = 6;
        }
        m_data->m_nFortressCastleTreasureLevel = castleTreasureLevel;
    }
}

void CFortress::SetSiegeInfoFromDB(
    const int siegeStatus, const int siegeStartTime, const int siegeElapsedTime, const bool validInfo
) {
    // Current L2Server 0x00738a44.
    L2_GUARD_FUNCTION();
    if (!validInfo) {
        m_stSiegeResumeInfo.bIsValidInfo = false;
        return;
    }
    m_stSiegeResumeInfo.eSiegeStatus = static_cast<FortressSiegeStatus>(siegeStatus);
    m_stSiegeResumeInfo.tSiegeStartTime = siegeStartTime;
    m_stSiegeResumeInfo.tSiegeElapsedTime = siegeElapsedTime;
    m_stSiegeResumeInfo.bIsValidInfo = true;
}

void CFortress::AddInfoLog() {
    // Current L2Server 0x0073a724.
    L2_GUARD_FUNCTION();
    const char16_t* pledgeName = u"";
    std::u16string masterName;
    std::u16string masterAccountName;
    std::int64_t pledgeId = 0;
    std::int64_t masterDatabaseId = 0;
    std::int64_t masterAccountId = 0;

    if (CPledge* const pledge = GetResidenceOwnerPledge()) {
        pledgeName = pledge->GetPledgeName();
        pledgeId = pledge->GetDbId();
        if (User* const master = pledge->GetMaster()) {
            masterDatabaseId = master->GetDBID();
            masterAccountId = master->GetAccountID();
            masterName = master->GetName();
            masterAccountName = master->m_sAccountName.data();
        }
    }

    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,"
    //     L"%I64d,%I64d,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7dd),
    //     masterDatabaseId,
    //     masterAccountId,
    //     pledgeName,
    //     pledgeId,
    //     static_cast<std::int64_t>(
    //         m_data != nullptr ? m_data->m_nResidenceID : 0),
    //     static_cast<std::int64_t>(m_nOwnerSiegeDefendCount),
    //     static_cast<std::int64_t>(
    //         m_data->m_nFortressOwnerRewardCycleCount),
    //     static_cast<std::int64_t>(
    //         m_data->m_nFortressCastleTreasureLevel),
    //     masterName,
    //     masterAccountName)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,"
        u"%I64d,%I64d,,,,,,%s,%s,,,",
        static_cast<std::int64_t>(0x7dd), masterDatabaseId, masterAccountId, pledgeName, pledgeId,
        static_cast<std::int64_t>(m_data != nullptr ? m_data->m_nResidenceID : 0),
        static_cast<std::int64_t>(m_nOwnerSiegeDefendCount),
        static_cast<std::int64_t>(m_data->m_nFortressOwnerRewardCycleCount),
        static_cast<std::int64_t>(m_data->m_nFortressCastleTreasureLevel), masterName, masterAccountName
    );
}

void CFortress::AddContractLog(const int previousStatus, const int castleId) {
    // Current L2Server 0x0073a8d4.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetResidenceOwnerPledge();
    if (pledge == nullptr) {
        return;
    }

    User* const master = pledge->GetMaster();
    const char16_t* const accountName = master != nullptr ? master->m_sAccountName.data() : nullptr;
    const std::int64_t masterAccountId = master != nullptr ? master->GetAccountID() : 0;
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,"
    //     L"%I64d,,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7d8),
    //     static_cast<std::int64_t>(pledge->GetMasterDbId()),
    //     masterAccountId,
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(ResidenceId(m_data)),
    //     static_cast<std::int64_t>(previousStatus),
    //     static_cast<std::int64_t>(castleId),
    //     pledge->GetMasterName(),
    //     accountName)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,"
        u"%I64d,,,,,,,%s,%s,,,",
        static_cast<std::int64_t>(0x7d8), static_cast<std::int64_t>(pledge->GetMasterDbId()), masterAccountId,
        pledge->GetPledgeName(), static_cast<std::int64_t>(pledge->GetDbId()),
        static_cast<std::int64_t>(ResidenceId(m_data)), static_cast<std::int64_t>(previousStatus),
        static_cast<std::int64_t>(castleId), pledge->GetMasterName(), accountName
    );
}

void CFortress::AddOwnerRewardTakenLog(
    User* const user, const int previousRewardCycle, const int rewardValue, const std::int64_t rewardAmount
) {
    // Current L2Server 0x0073aa54.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetResidenceOwnerPledge();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int currentRewardCycle = m_data->m_nFortressOwnerRewardCycleCount;
    const std::int64_t z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const std::int64_t y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const std::int64_t x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7df),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     x,
    //     y,
    //     z,
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(ResidenceId(m_data)),
    //     static_cast<std::int64_t>(rewardValue),
    //     rewardAmount,
    //     static_cast<std::int64_t>(previousRewardCycle),
    //     static_cast<std::int64_t>(currentRewardCycle),
    //     user->GetName(),
    //     user->m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7df),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     x,
    //     y,
    //     z,
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(ResidenceId(m_data)),
    //     static_cast<std::int64_t>(rewardValue),
    //     rewardAmount,
    //     static_cast<std::int64_t>(previousRewardCycle),
    //     static_cast<std::int64_t>(currentRewardCycle),
    //     Utf16ToWide(user->GetName()).c_str(),
    //     user->m_sAccountName.data())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
        static_cast<std::int64_t>(0x7df), static_cast<std::int64_t>(user->GetDBID()),
        static_cast<std::int64_t>(user->GetAccountID()), x, y, z, pledge->GetPledgeName(),
        static_cast<std::int64_t>(pledge->GetDbId()), static_cast<std::int64_t>(ResidenceId(m_data)),
        static_cast<std::int64_t>(rewardValue), rewardAmount, static_cast<std::int64_t>(previousRewardCycle),
        static_cast<std::int64_t>(currentRewardCycle), user->GetName(), user->m_sAccountName.data()
    );
}

void CFortress::AddCastleTreasureTakenLog(User* const user, const int previousTreasureLevel) {
    // Current L2Server 0x0073aca0.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = GetResidenceOwnerPledge();
    if (user == nullptr || pledge == nullptr) {
        return;
    }

    const int currentTreasureLevel = m_data->m_nFortressCastleTreasureLevel;
    const std::int64_t z = runtime::Win64CvttDoubleToInt32(user->GetPos().z);
    const std::int64_t y = runtime::Win64CvttDoubleToInt32(user->GetPos().y);
    const std::int64_t x = runtime::Win64CvttDoubleToInt32(user->GetPos().x);
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7de),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     x,
    //     y,
    //     z,
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(ResidenceId(m_data)),
    //     static_cast<std::int64_t>(previousTreasureLevel),
    //     static_cast<std::int64_t>(currentTreasureLevel),
    //     user->GetName(),
    //     user->m_sAccountName.data())
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
    //     static_cast<std::int64_t>(0x7de),
    //     static_cast<std::int64_t>(user->GetDBID()),
    //     static_cast<std::int64_t>(user->GetAccountID()),
    //     x,
    //     y,
    //     z,
    //     pledge->GetPledgeName(),
    //     static_cast<std::int64_t>(pledge->GetDbId()),
    //     static_cast<std::int64_t>(ResidenceId(m_data)),
    //     static_cast<std::int64_t>(previousTreasureLevel),
    //     static_cast<std::int64_t>(currentTreasureLevel),
    //     Utf16ToWide(user->GetName()).c_str(),
    //     user->m_sAccountName.data())
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
        static_cast<std::int64_t>(0x7de), static_cast<std::int64_t>(user->GetDBID()),
        static_cast<std::int64_t>(user->GetAccountID()), x, y, z, pledge->GetPledgeName(),
        static_cast<std::int64_t>(pledge->GetDbId()), static_cast<std::int64_t>(ResidenceId(m_data)),
        static_cast<std::int64_t>(previousTreasureLevel), static_cast<std::int64_t>(currentTreasureLevel), user->GetName(),
        user->m_sAccountName.data()
    );
}

void CFortress::ResetOwnerInfo() {
    // Current L2Server 0x0073b614.
    L2_GUARD_FUNCTION();
    if (m_nResidenceOwnerPledgeServerId <= 0) {
        return;
    }
    CPledge* const pledge = GetResidenceOwnerPledge();
    if (pledge == nullptr) {
        return;
    }

    SetOwnerPledge(nullptr);
    pledge->SetFortress(nullptr);
    pledge->Save(8);
    pledge->UpdatePledgeInfoPacket();
    pledge->UpdateInfoWindowPacket();
    pledge->UnequipMemberCastleItem();

    CResetAllResidenceDeco resetDeco;
    resetDeco.nNpcIndex = -1;
    resetDeco.nUserIndex = -1;
    resetDeco.nAgitId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    db.RequestTPacketProcess(g_nWorldId, 3, static_cast<int>(sizeof(resetDeco)), resetDeco);

    CResetAllResidenceDecoNpc resetNpcDeco(
        static_cast<int>(pledge->GetDbId()), m_data != nullptr ? m_data->m_nResidenceID : 0
    );
    db.RequestTPacketProcess(g_nWorldId, 0x3d, static_cast<int>(sizeof(resetNpcDeco)), resetNpcDeco);

    m_data->m_tLastOwnerChangedTime = runtime::crt::Time32Now();
    AddInfoLog();
}

void CFortress::ResetOwnerRewardInfo() {
    // Current L2Server 0x0073b7ac.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressLock);
        SetLastRewardGivenTime(runtime::crt::Time32Now());
        m_data->m_nFortressOwnerRewardCycleCount = 0;
        m_data->m_nFortressCastleTreasureLevel = 0;
        m_nOwnerSiegeDefendCount = 0;
        db.RequestSaveFortressRewardInfo(
            this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
            m_data->m_nFortressCastleTreasureLevel
        );
        AddInfoLog();
    }
}

void CFortress::SetOwnerRewardCycleCount(const int count) {
    // Current L2Server 0x0073bfd8 has no guard frame.
    if (count < 0) {
        return;
    }
    m_data->m_nFortressOwnerRewardCycleCount = count;
    db.RequestSaveFortressRewardInfo(
        this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
        m_data->m_nFortressCastleTreasureLevel
    );
    AddInfoLog();
}

void CFortress::ResetOwnerRewardCycleCount() {
    // Current L2Server 0x0073c028 has no guard frame.
    m_data->m_nFortressOwnerRewardCycleCount = 0;
    db.RequestSaveFortressRewardInfo(
        this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
        m_data->m_nFortressCastleTreasureLevel
    );
    AddInfoLog();
}

void CFortress::SetTreasureLevel(int level) {
    // Current L2Server 0x0073c0dc has no guard frame.
    if (level < 0) {
        return;
    }
    if (level > 6) {
        level = 6;
    }
    m_data->m_nFortressCastleTreasureLevel = level;
    db.RequestSaveFortressRewardInfo(
        this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
        m_data->m_nFortressCastleTreasureLevel
    );
    AddInfoLog();
}

void CFortress::_ResetTreasureLevel() {
    // Current L2Server 0x0073c138 has no guard frame.
    m_data->m_nFortressCastleTreasureLevel = 0;
    db.RequestSaveFortressRewardInfo(
        this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
        m_data->m_nFortressCastleTreasureLevel
    );
    AddInfoLog();
}

void CFortress::ResetOwnerContractInfo() {
    // Current L2Server 0x0073c598.
    L2_GUARD_FUNCTION();
    for (const std::uint32_t castleId : m_sRelatedCastleList) {
        CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(castleId));
        if (residence != nullptr) {
            static_cast<CCastle*>(residence)->RemoveChildFortress(this);
        }
    }

    {
        L2_GUARD(m_FortressLock);
        m_pParentCastle = nullptr;
        m_data->m_nFortressParentCastleId = 0;
        m_eContractStatus = FCS_NONE;
        m_data->m_nFortressContractStatus = FCS_NONE;
        db.RequestSaveFortressContractInfo(this, FCS_NONE, 0);
    }
}

void CFortress::SetFlagpole() {
    // Current L2Server 0x007403f8.
    L2_GUARD_FUNCTION();
    if (m_pFlagpole == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid flagpole in fortress[%d]",
        //     L"Fortress.cpp",
        //     0xb91,
        //     ResidenceId(m_data))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid flagpole in fortress[%d]",
        //     L"Fortress.cpp",
        //     0xb91,
        //     ResidenceId(m_data))
        L2_LOG_ERROR(Log, "Fortress %d: cannot set the flagpole; its object is missing.", ResidenceId(m_data));
        return;
    }
    m_pFlagpole->m_bIsFlagSet = true;
    m_pFlagpole->SendFortressFlagpoleInfo(nullptr);
}

void CFortress::ResetFlagpole() {
    // Current L2Server 0x007404e4.
    L2_GUARD_FUNCTION();
    if (m_pFlagpole == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid flagpole in fortress[%d]",
        //     L"Fortress.cpp",
        //     0xba4,
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid flagpole in fortress[%d]",
        //     L"Fortress.cpp",
        //     0xba4,
        //     m_data != nullptr ? m_data->m_nResidenceID : 0)
        L2_LOG_ERROR(Log, "Fortress %d: cannot reset the flagpole; its object is missing.", ResidenceId(m_data));
        return;
    }
    m_pFlagpole->m_bIsFlagSet = false;
    m_pFlagpole->SendFortressFlagpoleInfo(nullptr);
}

void CFortress::ResetOwnership() {
    // Current L2Server 0x00741010.
    L2_GUARD_FUNCTION();
    if (GetResidenceOwnerPledge() == nullptr) {
        return;
    }

    ResetOwnerInfo();
    db.RequestOwnFortress(
        this, m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0, m_data->m_tLastOwnerChangedTime
    );
    ResetOwnerRewardInfo();
    ResetOwnerContractInfo();
    ResetFlagpole();
}

FortressContractStatus CFortress::GetContractStatus() const {
    // Current L2Server 0x007387e0.
    return m_eContractStatus;
}

int CFortress::GetParentCastleId() const {
    // Current L2Server 0x0073a488.
    if (m_eContractStatus != FCS_CASTLE_CONTRACT || m_pParentCastle == nullptr || m_pParentCastle->m_data == nullptr) {
        return 0;
    }
    return m_pParentCastle->m_data->m_nResidenceID;
}

void CFortress::Init() {
    // Current L2Server 0x00738148.
    L2_GUARD_FUNCTION();
    const int fortressId = ResidenceId(m_data);
    if (g_L2ServerType != L2ServerType::WORLDSERVER) {
        m_pNpcMakerEventHandler = new CFortressNpcMakerEventHandler(fortressId);
    }
    m_pNpcAIEventHandler = new CFortressNpcAIEventHandler(fortressId, this);
}

void CFortress::ChangeResidenceStateCurrent(const ResidenceState state) {
    // Current L2Server 0x007387e8.
    const std::int32_t now = runtime::crt::Time32Now();
    m_tStateElapsedTime = 0;
    m_tStateChangedTime = now;
    db.RequestSaveFortressState(this, state, now, 0);
    ChangeResidenceState(state);
}

bool CFortress::HasMoreThan3DayAndDelta() {
    // Current L2Server 0x00738244.
    L2_GUARD_FUNCTION();
    const std::int32_t elapsed =
        runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), m_data->m_tLastOwnerChangedTime);
    return GetMaxFortressRewardSeconds() + 0x1c20 < elapsed;
}

bool CFortress::HasMoreThan75Hours() {
    // Current L2Server 0x00738300.
    L2_GUARD_FUNCTION();
    const std::int32_t elapsed =
        runtime::SubtractInt32Wrapping(runtime::crt::Time32Now(), m_data->m_tLastOwnerChangedTime);
    return GetMaxFortressOwnSeconds() < elapsed;
}

void CFortress::GiveOwnerReward() {
    // Current L2Server 0x0073b8d0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_FortressLock);
        ++m_data->m_nFortressOwnerRewardCycleCount;
        SetLastRewardGivenTime(runtime::AddInt32Wrapping(m_data->m_tLastRewardGivenTime, 0x16da0));
        db.RequestSaveFortressRewardInfo(
            this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
            m_data->m_nFortressCastleTreasureLevel
        );
        AddInfoLog();
    }
}

void CFortress::PayFortressRent() {
    // Current L2Server 0x0073b9e8.
    L2_GUARD_FUNCTION();
    if (m_pParentCastle == nullptr) {
        return;
    }
    CPledge* const castleOwner = m_pParentCastle->GetResidenceOwnerPledge();
    if (castleOwner == nullptr) {
        return;
    }
    if (castleOwner->GetNameValue() < 0x23) {
        CPledge* const fortressOwner = GetResidenceOwnerPledge();
        User* const master = fortressOwner != nullptr ? fortressOwner->GetMaster() : nullptr;
        if (master != nullptr && master->GetSocket() != nullptr) {
            master->GetSocket()->SendSystemMessage(
                SystemMessageId::SUPPLIES_NOT_PROVIDED_CONTRACTED_CASTLE_LACKS_CLAN_REPUTATION
            );
        }
        return;
    }
    db.RequestPayFortressRent(
        this, m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0,
        static_cast<int>(castleOwner->GetDbId()), m_data != nullptr ? m_data->m_nFortressRent : 0
    );
}

void CFortress::UpdatePledgeNameValue(CPledge* const pledge, const FortressPledgeNameValueChange change) {
    // Current L2Server 0x00738448.
    L2_GUARD_FUNCTION();
    if (pledge != nullptr && pledge->GetFunction()->IsEnable(PledgeFunctionType::FORTRESS_SIEGE)) {
        db.UpdatePledgeNameValue(pledge->Id(), pledge->GetDbId(), pledge->GetNameValue(), change, g_nWorldId);
    }
}

bool CFortress::DeductParentCastlePledgeNameValue() {
    // Current L2Server 0x0073a2c8.
    L2_GUARD_FUNCTION();
    if (m_eContractStatus != FCS_CASTLE_CONTRACT || m_pParentCastle == nullptr) {
        return false;
    }

    CPledge* const castleOwner = m_pParentCastle->GetResidenceOwnerPledge();
    if (castleOwner != nullptr) {
        if (runtime::AddInt32Wrapping(castleOwner->GetNameValue(), -0x23) < 0) {
            CPledge* const fortressOwner = GetResidenceOwnerPledge();
            if (fortressOwner != nullptr) {
                User* const master = fortressOwner->GetMaster();
                if (master != nullptr) {
                    master->GetSocket()->SendSystemMessage(
                        SystemMessageId::SUPPLIES_NOT_PROVIDED_CONTRACTED_CASTLE_LACKS_CLAN_REPUTATION
                    );
                }
            }
            return false;
        }
        UpdatePledgeNameValue(castleOwner, FPNVC_PARENT_CASTLE_DEDUCT);
    }
    return true;
}

void CFortress::IncrementTreasureLevel() {
    // Current L2Server 0x0073c074 has no guard frame.
    m_data->m_nFortressCastleTreasureLevel = runtime::AddInt32Wrapping(m_data->m_nFortressCastleTreasureLevel, 1);
    if (m_data->m_nFortressCastleTreasureLevel > 6) {
        m_data->m_nFortressCastleTreasureLevel = 6;
    }
    db.RequestSaveFortressRewardInfo(
        this, m_data->m_tLastRewardGivenTime, m_data->m_nFortressOwnerRewardCycleCount,
        m_data->m_nFortressCastleTreasureLevel
    );
    AddInfoLog();
}

void CFortress::UpgradeTreasure() {
    // Current L2Server 0x0073c8bc is the guarded physical wrapper kept
    // by HandleReplyPayFortressRent.
    L2_GUARD_FUNCTION();
    IncrementTreasureLevel();
}

void CFortress::HandleReplyPayFortressRent(const int result) {
    // Current L2Server 0x0073caa0.
    L2_GUARD_FUNCTION();
    if (result == 0) {
        GoIndependent();
    } else if (result == 1 && DeductParentCastlePledgeNameValue()) {
        UpgradeTreasure();
    }
}

void CFortress::ResetFacilityLevel() {
    // Current L2Server 0x0073868c.
    L2_GUARD_FUNCTION();
    for (int& level : m_data->m_nFacilityLevel) {
        level = 0;
    }
}

int CFortress::GetFacilityLevel(const int facility) const {
    // Current L2Server 0x00738730 checks only the upper bound.
    L2_GUARD_FUNCTION();
    return facility < 5 ? m_data->m_nFacilityLevel.data()[facility] : -1;
}

void CFortress::SetFacilityLevel(const int facility, const int level) {
    // Current L2Server 0x007385d8 checks only the two
    // original upper bounds.
    L2_GUARD_FUNCTION();
    if (facility < 5 && level <= m_mFacilityMap.data()[facility].m_nMaxLevel) {
        m_data->m_nFacilityLevel.data()[facility] = level;
    }
}

void CFortress::SendResetFacilityLevelToCacheD() {
    // Current L2Server 0x0073aec8.
    L2_GUARD_FUNCTION();
    CDxResetFortressFacility packet(-1, -1, ResidenceId(m_data));
    db.RequestTPacketProcess(g_nWorldId, 0x33, static_cast<int>(sizeof(packet)), packet);
}

void CFortress::SiegeRegister() {
    // Current L2Server 0x0073a208.
    L2_GUARD_FUNCTION();
    if (ResidenceStatus(m_data) == RS_ANNOUNCING) {
        ChangeResidenceStateCurrent(RS_REGISTRATION);
    }
}

bool CFortress::CanRegisterSiege(CPledge* const pledge) {
    // Current L2Server 0x0073d3e0.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return false;
    }

    const int ownerId = m_data != nullptr ? m_data->m_nResidenceOwnerID : 0;
    if (static_cast<int>(pledge->GetDbId()) == ownerId) {
        return false;
    }

    if (CCastle* const castle = pledge->GetCastle()) {
        if (m_eContractStatus != FCS_INDEPENDENT || !castle->IsRelatedFortress(ResidenceId(m_data))) {
            return false;
        }
    }

    if (pledge->IsWaitingToDismiss()) {
        if (User* const master = pledge->GetMaster()) {
            master->SendSystemMessage(SystemMessageId::CANNOT_REGISTER_SIEGE_DURING_CLAN_DISSOLUTION_GRACE_PERIOD);
        }
        return false;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (CDominionManager::Inst()->m_nState == DSST_STANDBY) {
        return false;
    }

    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        CResidence* const residence = entry.second;
        if (residence == nullptr) {
            continue;
        }
        if (residence->IsCastle()) {
            const std::int32_t nextSiege = residence->m_data != nullptr ? residence->m_data->m_NextSiege : 0;
            if (nextSiege != 0 && now <= nextSiege && runtime::SubtractInt32Wrapping(nextSiege, 0x1c20) < now) {
                return false;
            }
            if (residence->IsUnderSiege()) {
                return false;
            }
        } else if (residence->IsFortress() && residence->IsRegisteredForSiege(pledge)) {
            return false;
        }
    }
    return true;
}

bool CFortress::IsRelatedCastle(const unsigned int residenceId) {
    // Current L2Server 0x0073d624.
    L2_GUARD_FUNCTION();
    return m_sRelatedCastleList.find(residenceId) != m_sRelatedCastleList.end();
}

int CFortress::RegisterSiege(const unsigned int pledgeId) {
    // Current L2Server 0x00743468 releases the smart owner
    // before validating the borrowed pledge pointer.
    L2_GUARD_FUNCTION();
    CPledge* pledge = nullptr;
    {
        CSPointer<CPledge> holder = CPledge::FindPledge(g_nWorldId, pledgeId);
        pledge = holder.get();
    }
    if (!CanRegisterSiege(pledge)) {
        return 0;
    }

    const ResidenceState state = ResidenceStatus(m_data);
    if (state == RS_ANNOUNCING) {
        if (m_pSiegeRegistry != nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fortress siege registry object not properly cleared [%d][%d]",
            //     L"Fortress.cpp",
            //     0x1ef,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fortress siege registry object not properly cleared [%d][%d]",
            //     L"Fortress.cpp",
            //     0x1ef,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            L2_LOG_ERROR(
                Log,
                "Fortress %d: cannot register clan %u in state %d because a previous siege registry remains; siege "
                "cancellation is requested.",
                ResidenceId(m_data), pledgeId, static_cast<int>(state)
            );
            SiegeCancel();
            return 0;
        }

        m_pSiegeRegistry = new (std::nothrow) CFortressSiegeRegistry;
        if (m_pSiegeRegistry == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
            //     L"Fortress.cpp",
            //     0x1e4,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
            //     L"Fortress.cpp",
            //     0x1e4,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            L2_LOG_ERROR(
                Log, "Fortress %d: siege registry allocation failed while registering clan %u; returning to ANNOUNCING.",
                ResidenceId(m_data), pledgeId
            );
            ChangeResidenceStateCurrent(RS_ANNOUNCING);
            return 0;
        }

        m_pSiegeRegistry->RegisterPledge(pledgeId);
        db.RequestSaveFortressSiegeRegisterPledge(this, pledgeId);
        CSPointer<CPledge> registered = CPledge::FindPledge(g_nWorldId, pledgeId);
        BroadcastSystemMessageWithName(registered.get(), SystemMessageId::REGISTERED_FOR_FORTRESS_BATTLE);
        SiegeRegister();
        return -1;
    }

    if (state != RS_REGISTRATION) {
        return 3;
    }
    if (m_pSiegeRegistry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x20c,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x20c,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot register clan %u in state %d because the siege registry is missing; returning to "
            "ANNOUNCING.",
            ResidenceId(m_data), pledgeId, static_cast<int>(state)
        );
        ChangeResidenceStateCurrent(RS_ANNOUNCING);
        return 0;
    }
    if (m_pSiegeRegistry->IsRegisteredPledge(pledgeId)) {
        return 4;
    }

    m_pSiegeRegistry->RegisterPledge(pledgeId);
    db.RequestSaveFortressSiegeRegisterPledge(this, pledgeId);
    CSPointer<CPledge> registered = CPledge::FindPledge(g_nWorldId, pledgeId);
    BroadcastSystemMessageWithName(registered.get(), SystemMessageId::REGISTERED_FOR_FORTRESS_BATTLE);
    return -1;
}

int CFortress::UnregisterSiege(const unsigned int pledgeId) {
    // Current L2Server 0x007400e4.
    L2_GUARD_FUNCTION();
    const ResidenceState state = ResidenceStatus(m_data);
    if (state != RS_REGISTRATION) {
        return 3;
    }
    if (m_pSiegeRegistry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x226,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x226,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot unregister clan %u in state %d because the siege registry is missing; returning to "
            "ANNOUNCING.",
            ResidenceId(m_data), pledgeId, static_cast<int>(state)
        );
        ChangeResidenceStateCurrent(RS_ANNOUNCING);
        return 4;
    }
    if (!m_pSiegeRegistry->IsRegisteredPledge(pledgeId)) {
        return 4;
    }

    m_pSiegeRegistry->UnregisterPledge(pledgeId);
    db.RequestSaveFortressSiegeUnregisterPledge(this, pledgeId);
    return -1;
}

void CFortress::HandleRegisterSiege(User* const user, CNPC* const npc) {
    // Current L2Server 0x00743954.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    int result = 0;
    if (CanRegisterSiege(pledge)) {
        result = RegisterSiege(pledge->GetDbId());
    }
    if (m_pNpcAIEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"HandleRegisterSiege : m_pNpcAIEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"HandleRegisterSiege : m_pNpcAIEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege registration returned result %d, but the NPC AI reply cannot be sent because its handler is "
            "missing.",
            ResidenceId(m_data), result
        );
    } else {
        m_pNpcAIEventHandler->Send(0, static_cast<int>(npc->Id()), static_cast<int>(user->Id()), result, -1, -1);
    }

    if (result == -1) {
        CLogFormatter formatter;
        const int residenceId = ResidenceId(m_data);
        const unsigned int pledgeLevel = pledge->GetPledgeLevel();
        const std::uint32_t pledgeDbId = pledge->GetDbId();
        const char16_t* const pledgeName = pledge->GetPledgeName();
        formatter.WriteLog4(
            0x7d3, user, nullptr, pledgeName, nullptr, nullptr, pledgeDbId, residenceId, pledgeLevel, 0, 0, 0, 0, 0, 0, 0, 0
        );
    }
}

void CFortress::HandleUnregisterSiege(User* const user, CNPC* const npc) {
    // Current L2Server 0x00740234.
    L2_GUARD_FUNCTION();
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    const int result = UnregisterSiege(pledge->GetDbId());
    if (m_pNpcAIEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"HandleUnregisterSiege : m_pNpcAIEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"HandleUnregisterSiege : m_pNpcAIEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege unregistration returned result %d, but the NPC AI reply cannot be sent because its handler "
            "is missing.",
            ResidenceId(m_data), result
        );
    } else {
        m_pNpcAIEventHandler->Send(1, static_cast<int>(npc->Id()), static_cast<int>(user->Id()), result, -1, -1);
    }

    if (result == -1) {
        CLogFormatter formatter;
        const int residenceId = ResidenceId(m_data);
        const std::uint32_t pledgeDbId = pledge->GetDbId();
        const char16_t* const pledgeName = pledge->GetPledgeName();
        formatter.WriteLog4(
            0x7d4, user, nullptr, pledgeName, nullptr, nullptr, pledgeDbId, residenceId, 0, 0, 0, 0, 0, 0, 0, 0, 0
        );
    }
}

void CFortress::BroadcastSystemMessageWithName(CPledge* const pledge, const int messageId) {
    // Current L2Server 0x0073f9ac.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    std::vector<User*> members;
    pledge->GetAllMember(members, false);
    for (User* const member : members) {
        if (member != nullptr) {
            member->GetSocket()->Send(
                "chcch", 0x62, static_cast<short>(messageId), 1, 5, static_cast<short>(ResidenceId(m_data))
            );
        }
    }
}

int CFortress::GoIndependent() {
    // Current L2Server 0x0073c704.
    L2_GUARD_FUNCTION();
    const FortressContractStatus previousStatus = m_eContractStatus;
    if (previousStatus == FCS_INDEPENDENT) {
        return 3;
    }

    for (const std::uint32_t castleId : m_sRelatedCastleList) {
        CResidence* const residence = g_ResidenceDB.GetResidence(static_cast<int>(castleId));
        if (residence != nullptr) {
            static_cast<CCastle*>(residence)->RemoveChildFortress(this);
        }
    }

    {
        L2_GUARD(m_FortressLock);
        m_pParentCastle = nullptr;
        m_data->m_nFortressParentCastleId = 0;
        m_eContractStatus = FCS_INDEPENDENT;
        m_data->m_nFortressContractStatus = FCS_INDEPENDENT;
        _ResetTreasureLevel();
    }

    db.RequestSaveFortressContractInfo(this, FCS_INDEPENDENT, 0);
    int loggedPreviousStatus = previousStatus;
    if (previousStatus == FCS_CASTLE_CONTRACT) {
        loggedPreviousStatus = GetParentCastleId();
    }
    AddContractLog(loggedPreviousStatus, FCS_INDEPENDENT);
    return 1;
}

void CFortress::HandleGoIndependent(User* const user, CNPC* const npc) {
    // Current L2Server 0x0073c960.
    L2_GUARD_FUNCTION();
    if (m_nResidenceOwnerPledgeServerId <= 0) {
        return;
    }
    if (user == nullptr) {
        GoIndependent();
        return;
    }

    CPledge* const pledge = user->GetPledge();
    const int ownerId = m_data != nullptr ? m_data->m_nResidenceOwnerID : 0;
    if (pledge == nullptr || static_cast<int>(pledge->GetDbId()) != ownerId) {
        return;
    }

    const int result = GoIndependent();
    if (m_pNpcAIEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"HandleGoIndependent : m_pNpcAIEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"HandleGoIndependent : m_pNpcAIEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: independence returned result %d, but the NPC AI reply cannot be sent because its handler is "
            "missing.",
            ResidenceId(m_data), result
        );
    } else {
        m_pNpcAIEventHandler->Send(2, static_cast<int>(npc->Id()), static_cast<int>(user->Id()), result, -1, -1);
    }
}

int CFortress::ContractWithCastle(CCastle* const castle) {
    // Current L2Server 0x0073e62c.
    L2_GUARD_FUNCTION();
    if (castle == nullptr) {
        return 0;
    }

    const int castleId = castle->m_data != nullptr ? castle->m_data->m_nResidenceID : 0;
    if (!IsRelatedCastle(static_cast<unsigned int>(castleId)) || !castle->IsRelatedFortress(ResidenceId(m_data)) ||
        m_eContractStatus == FCS_CASTLE_CONTRACT || m_pParentCastle != nullptr) {
        return 0;
    }

    const FortressContractStatus previousStatus = m_eContractStatus;
    {
        L2_GUARD(m_FortressLock);
        m_pParentCastle = castle;
        m_data->m_nFortressParentCastleId = castleId;
        m_eContractStatus = FCS_CASTLE_CONTRACT;
        m_data->m_nFortressContractStatus = FCS_CASTLE_CONTRACT;
        _ResetTreasureLevel();
    }

    castle->SetChildFortress(this);
    if (CPledge* const owner = GetResidenceOwnerPledge()) {
        if (castle->GetReservedStatus(owner->GetDbId(), owner->GetWorldId()) == 1) {
            if (User* const master = owner->GetMaster()) {
                db.RequestQueryCastleSiegeForUnregister(master, castle);
            }
        }
    }
    db.RequestSaveFortressContractInfo(this, FCS_CASTLE_CONTRACT, castleId);
    AddContractLog(previousStatus, castleId);
    return -1;
}

void CFortress::HandleContractWithCastle(User* const user, CNPC* const npc, CCastle* const castle) {
    // Current L2Server 0x0073e858.
    L2_GUARD_FUNCTION();
    if (m_nResidenceOwnerPledgeServerId <= 0) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    const int ownerId = m_data != nullptr ? m_data->m_nResidenceOwnerID : 0;
    if (pledge == nullptr || static_cast<int>(pledge->GetDbId()) != ownerId) {
        return;
    }

    const int result = ContractWithCastle(castle);
    if (m_pNpcAIEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"HandleContractWithCastle : m_pNpcAIEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"HandleContractWithCastle : m_pNpcAIEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: castle contract returned result %d, but the NPC AI reply cannot be sent because its handler is "
            "missing.",
            ResidenceId(m_data), result
        );
    } else {
        m_pNpcAIEventHandler->Send(2, static_cast<int>(npc->Id()), static_cast<int>(user->Id()), result, -1, -1);
    }
}

void CFortress::StartTimer() {
    // Current L2Server 0x007432ec.
    L2_GUARD_FUNCTION();
    Init();
    const ResidenceState state = ResidenceStatus(m_data);
    if (state == RS_ANNOUNCING) {
        m_bWaitNpcServerUp = true;
    } else {
        const std::int32_t now = runtime::crt::Time32Now();
        const std::int32_t delta = runtime::SubtractInt32Wrapping(
            runtime::SubtractInt32Wrapping(now, m_tStateElapsedTime), m_tStateChangedTime
        );
        if (delta < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fortress invalid resume state [%d][%d][%d][%d]",
            //     L"Fortress.cpp",
            //     199,
            //     ResidenceId(m_data),
            //     static_cast<int>(state),
            //     m_tStateChangedTime,
            //     m_tStateElapsedTime)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fortress invalid resume state [%d][%d][%d][%d]",
            //     L"Fortress.cpp",
            //     199,
            //     ResidenceId(m_data),
            //     static_cast<int>(state),
            //     m_tStateChangedTime,
            //     m_tStateElapsedTime)
            L2_LOG_ERROR(
                Log,
                "Fortress %d: invalid resume timing in state %d (now %d, changed %d, elapsed %d, calculated downtime %d; "
                "seconds); siege cancellation is requested and the 10-second timer is still started.",
                ResidenceId(m_data), static_cast<int>(state), now, m_tStateChangedTime, m_tStateElapsedTime, delta
            );
            SiegeCancel();
        } else {
            m_tStateChangedTime = runtime::AddInt32Wrapping(m_tStateChangedTime, delta);
            db.RequestSaveFortressState(this, state, m_tStateChangedTime, m_tStateElapsedTime);
            db.RequestLoadFortressSiegeRegistry(this);
            m_bWaitLoading = true;
        }
    }
    AddTimer(10000, 0);
}

void CFortress::HandleReplyLoadFortressSiegeRegistry(std::set<std::uint32_t>& pledgeIds) {
    // Current L2Server 0x00741f8c.
    L2_GUARD_FUNCTION();
    const ResidenceState state = ResidenceStatus(m_data);
    if (state == RS_ANNOUNCING) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress in announcing state while trying to load siege registry [%d][%d]",
        //     L"Fortress.cpp",
        //     0x77b,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress in announcing state while trying to load siege registry [%d][%d]",
        //     L"Fortress.cpp",
        //     0x77b,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege registry reply arrived in ANNOUNCING state; siege cancellation is requested and the loading "
            "wait is cleared.",
            ResidenceId(m_data)
        );
        SiegeCancel();
    } else {
        m_pSiegeRegistry = new (std::nothrow) CFortressSiegeRegistry;
        if (m_pSiegeRegistry == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
            //     L"Fortress.cpp",
            //     0x773,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] fortress siege registry object not properly created [%d][%d]",
            //     L"Fortress.cpp",
            //     0x773,
            //     ResidenceId(m_data),
            //     static_cast<int>(state))
            L2_LOG_ERROR(
                Log,
                "Fortress %d: siege registry allocation failed while restoring state %d; returning to ANNOUNCING and "
                "clearing the loading wait.",
                ResidenceId(m_data), static_cast<int>(state)
            );
            ChangeResidenceStateCurrent(RS_ANNOUNCING);
        } else {
            for (const std::uint32_t pledgeId : pledgeIds) {
                m_pSiegeRegistry->RegisterPledge(pledgeId);
            }
            if (state == RS_SIEGE) {
                db.RequestLoadFortressSiege(this);
                return;
            }
        }
    }
    m_bWaitLoading = false;
}

void CFortress::HandleReplyLoadFortressSiege() {
    // Current L2Server 0x0074758c.
    L2_GUARD_FUNCTION();
    const ResidenceState state = ResidenceStatus(m_data);
    if (state == RS_SIEGE) {
        SiegeResume();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress not in siege state while trying to resume siege [%d][%d]",
        //     L"Fortress.cpp",
        //     0x74c,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress not in siege state while trying to resume siege [%d][%d]",
        //     L"Fortress.cpp",
        //     0x74c,
        //     ResidenceId(m_data),
        //     static_cast<int>(state))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: cannot restore siege data in residence state %d; siege cancellation is requested and the loading "
            "wait is cleared.",
            ResidenceId(m_data), static_cast<int>(state)
        );
        SiegeCancel();
    }
    m_bWaitLoading = false;
}

bool CFortress::IsRegisteredForSiege(CPledge* const pledge) const {
    // Current L2Server 0x0073d3e0.
    L2_GUARD_FUNCTION();
    return pledge != nullptr && m_pSiegeRegistry != nullptr && m_pSiegeRegistry->IsRegisteredPledge(pledge->GetDbId());
}

void CFortress::BroadcastSystemMessageW(const int messageId, const bool includeOwner) {
    // Current L2Server 0x00745030.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    if (m_pSiegeRegistry != nullptr) {
        m_pSiegeRegistry->GetRegisteredPledgeList(pledges);
    }
    if (includeOwner && m_nResidenceOwnerPledgeServerId > 0) {
        pledges.push_back(GetResidenceOwnerPledge());
    }
    for (CPledge* const pledge : pledges) {
        if (pledge == nullptr) {
            continue;
        }
        std::vector<User*> members;
        pledge->GetAllMember(members, false);
        for (User* const member : members) {
            if (member != nullptr) {
                member->GetSocket()->SendSystemMessage(messageId);
            }
        }
    }
}

void CFortress::BroadcastSystemMessageWithName(const int messageId, const bool includeOwner) {
    // Current L2Server 0x007453cc.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    if (m_pSiegeRegistry != nullptr) {
        m_pSiegeRegistry->GetRegisteredPledgeList(pledges);
    }
    if (includeOwner && m_nResidenceOwnerPledgeServerId > 0) {
        pledges.push_back(GetResidenceOwnerPledge());
    }
    for (CPledge* const pledge : pledges) {
        if (pledge == nullptr) {
            continue;
        }
        std::vector<User*> members;
        pledge->GetAllMember(members, false);
        for (User* const member : members) {
            if (member != nullptr) {
                member->GetSocket()->Send(
                    "chcch", 0x62, static_cast<short>(messageId), 1, 5, static_cast<short>(ResidenceId(m_data))
                );
            }
        }
    }
}

void CFortress::BroadcastSystemMessageWithName(
    const int messageId, const eSMsgType type, const char16_t* const text, const bool includeOwner
) {
    // Current L2Server 0x007455bc. The target checks only the member pointer
    // and then dereferences the returned socket without a null guard.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    if (m_pSiegeRegistry != nullptr) {
        m_pSiegeRegistry->GetRegisteredPledgeList(pledges);
    }
    if (includeOwner && m_nResidenceOwnerPledgeServerId > 0) {
        pledges.push_back(GetResidenceOwnerPledge());
    }
    for (CPledge* const pledge : pledges) {
        if (pledge == nullptr) {
            continue;
        }
        std::vector<User*> members;
        pledge->GetAllMember(members, false);
        for (User* const member : members) {
            if (member != nullptr) {
                member->GetSocket()->Send(
                    "chccSch", 0x62, static_cast<short>(messageId), 2, static_cast<char>(type), std::u16string(text).c_str(),
                    5, static_cast<short>(ResidenceId(m_data))
                );
            }
        }
    }
}

void CFortressSiegeRegistry::BroadcastSystemMessageW(const int messageId, const int value) {
    // Current L2Server 0x00743cac.
    L2_GUARD_FUNCTION();
    std::vector<CPledge*> pledges;
    GetRegisteredPledgeList(pledges);
    for (CPledge* const pledge : pledges) {
        if (pledge == nullptr) {
            continue;
        }
        if (value == -1) {
            pledge->SystemMessage(messageId);
        } else {
            pledge->SystemMessage(messageId, value);
        }
    }
}

void CFortress::BroadcastStandbyCountDown(const int remainingSeconds) {
    // Current L2Server 0x00744c18.
    L2_GUARD_FUNCTION();
    for (const int minutes : kStandbyMinuteMarks) {
        const int threshold = minutes * 60;
        if (threshold < m_tLastCountDownTime && remainingSeconds <= threshold) {
            m_tLastCountDownTime = remainingSeconds;
            if (m_nResidenceOwnerPledgeServerId > 0 && GetFacilityLevel(4) != 0) {
                if (CPledge* const owner = GetResidenceOwnerPledge()) {
                    owner->SystemMessage(SystemMessageId::ENEMY_CLAN_ENTERED_FORTRESS);
                }
            }
            if (m_pSiegeRegistry != nullptr) {
                m_pSiegeRegistry->BroadcastSystemMessageW(SystemMessageId::FORTRESS_BATTLE_STARTS_IN_MINUTES, minutes);
            }
            return;
        }
    }
    for (const int seconds : kStandbySecondMarks) {
        if (seconds < m_tLastCountDownTime && remainingSeconds <= seconds) {
            m_tLastCountDownTime = remainingSeconds;
            if (m_nResidenceOwnerPledgeServerId > 0 && GetFacilityLevel(4) != 0) {
                if (CPledge* const owner = GetResidenceOwnerPledge()) {
                    owner->SystemMessage(SystemMessageId::ENEMY_CLAN_ENTERED_FORTRESS);
                }
            }
            if (m_pSiegeRegistry != nullptr) {
                m_pSiegeRegistry->BroadcastSystemMessageW(SystemMessageId::FORTRESS_BATTLE_STARTS_IN_SECONDS, seconds);
            }
            return;
        }
    }
}

void CFortress::SiegeCancel() {
    // Current L2Server 0x00741cb4.
    L2_GUARD_FUNCTION();
    ChangeResidenceStateCurrent(RS_ANNOUNCING);
    if (m_pSiege != nullptr) {
        return;
    }
    delete m_pSiegeRegistry;
    m_pSiegeRegistry = nullptr;
    if (!m_bIsDominionSiege) {
        db.RequestSaveFortressSiegeRegistry(this, 0, 0, nullptr, true);
    }
}

bool CFortress::SiegeStandby() {
    // Current L2Server 0x00744a68.
    L2_GUARD_FUNCTION();
    if (ResidenceStatus(m_data) != RS_REGISTRATION) {
        return false;
    }
    std::vector<CPledge*> registered;
    if (m_pSiegeRegistry == nullptr) {
        SiegeCancel();
        return true;
    }
    m_pSiegeRegistry->GetRegisteredPledgeList(registered);
    if (registered.empty()) {
        SiegeCancel();
        return true;
    }
    if (m_pNpcMakerEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"SiegeStandby : m_pNpcMakerEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"SiegeStandby : m_pNpcMakerEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: standby maker notification cannot be sent because its handler is missing; countdown broadcast and "
            "transition to STANDBY continue.",
            ResidenceId(m_data)
        );
    } else {
        m_pNpcMakerEventHandler->Send(0, -1, -1, -1, L"");
    }
    if (m_nResidenceOwnerPledgeServerId > 0 && GetFacilityLevel(4) != 0) {
        if (CPledge* const owner = GetResidenceOwnerPledge()) {
            owner->SystemMessage(SystemMessageId::ENEMY_CLAN_ENTERED_FORTRESS);
        }
    }
    m_pSiegeRegistry->BroadcastSystemMessageW(SystemMessageId::FORTRESS_BATTLE_STARTS_IN_MINUTES, 10);
    ChangeResidenceStateCurrent(RS_STANDBY);
    return true;
}

bool CFortress::SiegeStart() {
    // Current L2Server 0x00747014.
    L2_GUARD_FUNCTION();
    const ResidenceState state = ResidenceStatus(m_data);
    if (state != RS_STANDBY) {
        return false;
    }
    if (m_pSiege != nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] previous fortress siege object not properly cleared [%d][%d]",
        //     L"Fortress.cpp",
        //     0x28a,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] previous fortress siege object not properly cleared [%d][%d]",
        //     L"Fortress.cpp",
        //     0x28a,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: a previous siege object remains at siege start; resetting siege information, including the "
            "participant registry.",
            ResidenceId(m_data)
        );
        ResetSiegeInfo();
    }
    if (m_pSiegeRegistry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x294,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x294,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot start the siege because its participant registry is missing; returning to ANNOUNCING.",
            ResidenceId(m_data)
        );
        ChangeResidenceStateCurrent(RS_ANNOUNCING);
        return false;
    }
    if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20) || !g_PledgeTimer.IsAllNpcLoaded() ||
        !g_PledgeTimer.IsAllMakerFinished()) {
        return false;
    }
    m_pSiege = new CFortressSiege(
        ResidenceId(m_data), m_eScale, m_pSiegeRegistry, m_pNpcMakerEventHandler, m_pNpcAIEventHandler, m_bIsDominionSiege
    );
    if (m_pSiege == nullptr) {
        SiegeCancel();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress siege object creation failed [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2c4,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress siege object creation failed [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2c4,
        //     ResidenceId(m_data),
        //     static_cast<int>(RS_STANDBY))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege creation returned null at startup; siege cancellation has been requested, but the outer "
            "start operation still returns true.",
            ResidenceId(m_data)
        );
    } else {
        m_pSiege->Start();
        if (m_nResidenceOwnerPledgeServerId < 1) {
            m_data->m_tLastOwnerChangedTime = runtime::crt::Time32Now();
        } else if (CPledge* const owner = GetResidenceOwnerPledge()) {
            owner->SystemMessage(SystemMessageId::FORTRESS_UNDER_ATTACK);
        }
        BroadcastSystemMessageWithName(SystemMessageId::FORTRESS_BATTLE_STARTED, true);
        ChangeResidenceStateCurrent(RS_SIEGE);
    }
    return true;
}

void CFortress::SiegeResume() {
    // Current L2Server 0x007472b4.
    L2_GUARD_FUNCTION();
    if (m_pSiegeRegistry == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2d7,
        //     ResidenceId(m_data),
        //     static_cast<int>(ResidenceStatus(m_data)))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] siege registry object not properly created [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2d7,
        //     ResidenceId(m_data),
        //     static_cast<int>(ResidenceStatus(m_data)))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege participant registry is missing during resume; returning to ANNOUNCING, then continuing the "
            "resume path with that missing registry.",
            ResidenceId(m_data)
        );
        ChangeResidenceStateCurrent(RS_ANNOUNCING);
    }
    if (!g_PledgeTimer.IsAllNpcRelatedLoaded()) {
        m_bWaitResume = true;
        return;
    }
    m_pSiege = new CFortressSiege(
        ResidenceId(m_data), m_eScale, m_pSiegeRegistry, m_pNpcMakerEventHandler, m_pNpcAIEventHandler, m_bIsDominionSiege
    );
    if (m_pSiege == nullptr) {
        SiegeCancel();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress siege object creation failed [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2f5,
        //     ResidenceId(m_data),
        //     static_cast<int>(ResidenceStatus(m_data)))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress siege object creation failed [%d][%d]",
        //     L"Fortress.cpp",
        //     0x2f5,
        //     ResidenceId(m_data),
        //     static_cast<int>(ResidenceStatus(m_data)))
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege creation returned null during resume; siege cancellation has been requested and the resume "
            "wait will be cleared.",
            ResidenceId(m_data)
        );
    } else if (!m_stSiegeResumeInfo.bIsValidInfo) {
        m_pSiege->Start();
    } else {
        m_pSiege->Resume(FSRT_SERVER_DOWN, m_stSiegeResumeInfo);
    }
    m_bWaitResume = false;
}

void CFortress::SiegeEnd(const FortressSiegeResult result) {
    // Current L2Server 0x007474b8.
    L2_GUARD_FUNCTION();
    BroadcastSystemMessageWithName(SystemMessageId::FORTRESS_BATTLE_ENDED, true);
    HandleSiegeResult(result);
    BanExceptOwnerPledge();
    ResetSiegeInfo();
    m_tLastCountDownTime = 600;
    SendResetFacilityLevelToCacheD();
    ChangeResidenceStateCurrent(RS_ANNOUNCING);
}

void CFortress::ResetSiegeInfo() {
    // Current L2Server 0x00740e50.
    L2_GUARD_FUNCTION();
    delete m_pSiegeRegistry;
    m_pSiegeRegistry = nullptr;
    if (m_pSiege != nullptr) {
        L2_RELEASE(m_pSiege, ObjectRefType::CreateDelete);
        m_pSiege = nullptr;
    }
    m_stSiegeResumeInfo = FortressSiegeResumeInfo{};
    db.RequestSaveFortressSiegeState(ResidenceId(m_data), FSS_NONE, 0, 0, false);
    db.RequestSaveFortressSiegeRegistry(this, 0, 0, nullptr, true);
}

void CFortress::MaintainOwnership() {
    // Current L2Server 0x007383b8.
    L2_GUARD_FUNCTION();
    ++m_nOwnerSiegeDefendCount;
}

void CFortress::SetOwnerInfo(CPledge* const pledge) {
    // Current L2Server 0x0073b4e4.
    L2_GUARD_FUNCTION();
    SetOwnerPledge(pledge);
    pledge->SetFortress(this);
    pledge->Save(8);
    pledge->UpdatePledgeInfoPacket();
    pledge->UpdateInfoWindowPacket();
    m_data->m_tLastOwnerChangedTime = runtime::crt::Time32Now();
    AddInfoLog();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    const int fortressId = ResidenceId(m_data);
    const int pledgeId = static_cast<int>(pledge->GetDbId());
    const int worldId = g_nWorldId;
    GIP._Add(L"CMD_fortressWarfareInfo(%d, %d, %d, %d", worldId, fortressId, pledgeId, now);
}

void CFortress::SetOwnership(CPledge* const pledge, const bool updateNameValue) {
    // Current L2Server 0x00741e18.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }
    if (m_nResidenceOwnerPledgeServerId > 0) {
        ChangeOwnership(pledge, true);
        return;
    }
    CFortress* const oldFortress = pledge->GetFortress();
    if (oldFortress != nullptr && ResidenceId(oldFortress->m_data) != ResidenceId(m_data)) {
        oldFortress->ResetOwnership();
    }
    SetOwnerInfo(pledge);
    db.RequestOwnFortress(
        this, m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0,
        m_data != nullptr ? m_data->m_tLastOwnerChangedTime : 0
    );
    ResetOwnerRewardInfo();
    ResetOwnerContractInfo();
    if (updateNameValue && pledge->GetFunction() != nullptr &&
        pledge->GetFunction()->IsEnable(PledgeFunctionType::FORTRESS_SIEGE)) {
        db.UpdatePledgeNameValue(pledge->Id(), pledge->GetDbId(), pledge->GetNameValue(), FPNVC_BASE_GAIN, g_nWorldId);
    }
}

void CFortress::ChangeOwnership(CPledge* const pledge, const bool updateNameValue) {
    // Current L2Server 0x00741100.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }
    CPledge* const current = GetResidenceOwnerPledge();
    if (m_nResidenceOwnerPledgeServerId < 1 || current == nullptr || !IsOwnerPledge(static_cast<int>(current->GetDbId()))) {
        SetOwnership(pledge, true);
        return;
    }
    CFortress* const oldFortress = pledge->GetFortress();
    if (oldFortress != nullptr && ResidenceId(oldFortress->m_data) != ResidenceId(m_data)) {
        oldFortress->ResetOwnership();
    }
    ResetOwnerInfo();
    SetOwnerInfo(pledge);
    db.RequestOwnFortress(this, static_cast<int>(m_data->m_nResidenceOwnerID), m_data->m_tLastOwnerChangedTime);
    ResetOwnerRewardInfo();
    ResetOwnerContractInfo();
    if (updateNameValue) {
        UpdatePledgeNameValue(pledge, FPNVC_BASE_GAIN);
    }
}

void CFortress::ShowOwnerFlagPole() {
    // Current L2Server 0x007405d4.
    L2_GUARD_FUNCTION();
    if (m_pFlagpole == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid fortress flagpole [%d]",
        //     L"Fortress.cpp",
        //     0xbc3,
        //     ResidenceId(m_data))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid fortress flagpole [%d]",
        //     L"Fortress.cpp",
        //     0xbc3,
        //     ResidenceId(m_data))
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot show the owner flag on the flagpole; its object is missing.", ResidenceId(m_data)
        );
    } else if (!m_pFlagpole->m_bIsFlagSet) {
        m_pFlagpole->m_bIsFlagSet = true;
        m_pFlagpole->SendFortressFlagpoleInfo(nullptr);
    }
}

void CFortress::HandleSiegeResult(const FortressSiegeResult result) {
    // Current L2Server 0x00746050.
    L2_GUARD_FUNCTION();
    if (m_data == nullptr) {
        return;
    }
    if (m_data->m_nResidenceOwnerID == 0) {
        if (result.nWinnerPledgeDBId == -1) {
            return;
        }
        CSPointer<CPledge> winner = CPledge::FindPledge(g_nWorldId, static_cast<std::uint32_t>(result.nWinnerPledgeDBId));
        if (!winner || !winner->CanOwnFortress(static_cast<unsigned int>(ResidenceId(m_data)))) {
            return;
        }
        SetOwnership(winner.get(), true);
        if (m_pNpcMakerEventHandler == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"HandleSiegeResult : m_pNpcMakerEventHandler is null")
            // L2_LOG_ERROR(Log,
            //     L"HandleSiegeResult : m_pNpcMakerEventHandler is null")
            L2_LOG_ERROR(
                Log,
                "Fortress %d: initial ownership assignment has run for winning clan %d, but maker notification 5 cannot be "
                "sent because its handler is missing.",
                ResidenceId(m_data), result.nWinnerPledgeDBId
            );
        } else {
            m_pNpcMakerEventHandler->Send(5, -1, -1, -1, L"");
        }
        BroadcastSystemMessageWithName(
            SystemMessageId::CLAN_WON_FORTRESS_BATTLE, EST_STRING, winner->GetPledgeName(), false
        );
        User* const master = winner->GetMaster();
        if (master != nullptr) {
            CLogFormatter formatter;
            const int residenceId = ResidenceId(m_data);
            const unsigned int pledgeLevel = winner->GetPledgeLevel();
            const std::uint32_t pledgeDbId = winner->GetDbId();
            const char16_t* const pledgeName = winner->GetPledgeName();
            formatter.WriteLog4(
                0x7d6, master, nullptr, pledgeName, nullptr, nullptr, pledgeDbId, residenceId, pledgeLevel, 0, 0, 0, 0, 0, 0,
                0, 0
            );
        }
        return;
    }

    CPledge* const owner = GetResidenceOwnerPledge();
    if (owner != nullptr && result.nWinnerPledgeDBId == -1) {
        MaintainOwnership();
        return;
    }
    CSPointer<CPledge> winner = CPledge::FindPledge(g_nWorldId, static_cast<std::uint32_t>(result.nWinnerPledgeDBId));
    if (winner && winner->CanOwnFortress(static_cast<unsigned int>(ResidenceId(m_data)))) {
        ChangeOwnership(winner.get(), true);
        if (m_pNpcMakerEventHandler != nullptr) {
            m_pNpcMakerEventHandler->Send(5, -1, -1, -1, L"");
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"SiegeStandby : m_pNpcMakerEventHandler is null")
            // L2_LOG_ERROR(Log,
            //     L"SiegeStandby : m_pNpcMakerEventHandler is null")
            L2_LOG_ERROR(
                Log,
                "Fortress %d: ownership change has run for winning clan %d, but maker notification 5 cannot be sent because "
                "its handler is missing.",
                ResidenceId(m_data), result.nWinnerPledgeDBId
            );
        }
    } else {
        ResetOwnership();
    }
}

void CFortress::BanExceptOwnerPledge() {
    // Current L2Server 0x0073f5b0.
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(std::vector<User*>& users, CFortress* fortress, CPledge* owner) :
            users_(users), fortress_(fortress), owner_(owner) {}

        bool CanDo(CObject* object, double) override {
            User* const user = object->IsPC() ? object->CastUser() : nullptr;
            if ((!object->IsPC() || (!object->IsDeleting() && object->m_Deleted == 0)) && user != nullptr &&
                (owner_ == nullptr || user->m_data->m_nPledgeID != owner_->GetDbId()) &&
                fortress_->IsInResidence(user->GetPos(), true)) {
                users_.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
            return true;
        }

    private:
        std::vector<User*>& users_;
        CFortress* fortress_;
        CPledge* owner_;
    } function(users, this, GetResidenceOwnerPledge());

    CTLMFilterRect filter(0, *m_OwnerRestartPointList.begin(), 16384.0);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    ReserveExpelOfUsers(users);
}

void CFortress::BanPledgesInFortress() {
    // Current L2Server 0x0073f6e8.
    L2_GUARD_FUNCTION();
    std::vector<User*> users;
    class TLMFunc final : public CTLMFunc {
    public:
        TLMFunc(std::vector<User*>& users, CFortress* fortress) : users_(users), fortress_(fortress) {}

        bool CanDo(CObject* object, double) override {
            User* const user = object->IsPC() ? object->CastUser() : nullptr;
            if (user != nullptr && !object->IsDeleting() && object->m_Deleted == 0 &&
                fortress_->IsInResidence(user->GetPos(), false)) {
                users_.push_back(user);
                L2_ADD_REF(user, ObjectRefType::EventRetain);
            }
            return true;
        }

    private:
        std::vector<User*>& users_;
        CFortress* fortress_;
    } function(users, this);

    CTLMFilterRect filter(0, *m_OwnerRestartPointList.begin(), 16384.0);
    FOR_ALL_NEIGHBOR_USER_RECT(&filter, &function);
    ReserveExpelOfUsers(users);
}

void CFortress::ExpelNonPledgeMember() {
    // Current L2Server 0x0073f814 is the BanExceptOwnerPledge thunk.
    BanExceptOwnerPledge();
}

void CFortress::OnStartInBattleField(User* const user, FVector& position, bool& started) {
    // Current L2Server 0x0073bb44.
    L2_GUARD_FUNCTION();
    if (user == nullptr || user->m_data == nullptr || user->m_data->m_nWorldId != g_nWorldId) {
        return;
    }

    const std::uint32_t firstRandom = static_cast<std::uint32_t>(::LegacyCrtRand());
    auto& defaultPoints = user->m_data->m_nKarma < 0 ? m_ChaoRestartPointList : m_OtherRestartVillageList;
    position =
        defaultPoints[static_cast<std::size_t>(runtime::Win64UnsignedRemainder(firstRandom, defaultPoints.size()))];
    started = true;

    if (user->m_data->m_nPledgeID == 0) {
        return;
    }
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, user->m_data->m_nPledgeID);
    if (!pledge) {
        return;
    }
    const int fortressId = ResidenceId(m_data);
    if (pledge->m_data->m_nFortressUnderOffenceSiegeId == static_cast<std::uint32_t>(fortressId)) {
        CSPointer<CBattleCamp> battleCamp = pledge->GetBattleCamp();
        if (battleCamp && battleCamp->IsEnabled() && battleCamp->GetBaseResidenceId() == fortressId) {
            position = battleCamp->GetTeleportPos();
            started = true;
        }
    } else if (CDominionManager::Inst()->m_nState == DSST_DOMINION_SIEGE &&
        pledge->m_data->m_nFortressDbId == static_cast<std::uint32_t>(fortressId)) {
        const std::uint32_t secondRandom = static_cast<std::uint32_t>(::LegacyCrtRand());
        position = m_OwnerRestartPointList[static_cast<std::size_t>(
            runtime::Win64UnsignedRemainder(secondRandom, m_OwnerRestartPointList.size())
        )];
        started = true;
    }
}

void CFortress::NotifyDominionSiegeEnd() {
    // Current L2Server 0x0074855c.
    if (m_pSiege != nullptr) {
        m_pSiege->m_bIsDominionSiege = true;
        m_pSiege->End();
    }
}

bool CFortress::OnAIEvent(
    const int eventId, const int npcSid, const int targetSid, const int value, const std::int64_t amount
) {
    // Current L2Server 0x007477dc accepts negative event ids.
    L2_GUARD_FUNCTION();
    if (eventId > 7) {
        return false;
    }

    CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(npcSid));
    CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
    if (npc == nullptr) {
        return false;
    }

    if (eventId == 0) {
        CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
        User* const user = targetObject != nullptr && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
        if (user == nullptr) {
            return false;
        }
        HandleRegisterSiege(user, npc);
    } else if (eventId == 1) {
        CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
        User* const user = targetObject != nullptr && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
        if (user == nullptr) {
            return false;
        }
        HandleUnregisterSiege(user, npc);
    } else if (eventId == 2) {
        CFortressSiege* const siege = GetSiege();
        if (siege == nullptr) {
            return false;
        }
        siege->BarrackCaptured(static_cast<unsigned int>(value));
    } else if (eventId == 3) {
        CFortressSiege* const siege = GetSiege();
        if (siege == nullptr) {
            return false;
        }
        siege->SetProtectedNpcDie();
    } else if (eventId == 4) {
        CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
        User* const user = targetObject != nullptr && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
        if (value == -1) {
            HandleGoIndependent(user, npc);
        } else {
            if (user == nullptr) {
                return false;
            }
            CResidence* const residence = g_ResidenceDB.GetResidence(value);
            if (residence != nullptr && residence->IsCastle()) {
                CCastle* const castle = residence->CastCastle();
                if (castle != nullptr) {
                    HandleContractWithCastle(user, npc, castle);
                }
            }
        }
    } else if (eventId == 5) {
        CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
        User* const user = targetObject != nullptr && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
        const int previousRewardCycle = m_data->m_nFortressOwnerRewardCycleCount;
        ResetOwnerRewardCycleCount();
        AddOwnerRewardTakenLog(user, previousRewardCycle, value, amount);
    } else if (eventId == 6) {
        CCreature* const targetObject = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetSid));
        User* const user = targetObject != nullptr && targetObject->IsPC() ? targetObject->CastUser() : nullptr;
        const int previousTreasureLevel = m_data->m_nFortressCastleTreasureLevel;
        _ResetTreasureLevel();
        AddCastleTreasureTakenLog(user, previousTreasureLevel);
    }
    return true;
}

void CFortress::RecieveNpcEvent(const unsigned char* const packet) {
    // Current L2Server 0x00748490 keeps the shipped spelling.
    L2_GUARD_FUNCTION();
    if (m_pNpcAIEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RecieveNpcEvent : m_pNpcAIEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"RecieveNpcEvent : m_pNpcAIEventHandler is null")
        L2_LOG_ERROR(
            Log, "Fortress %d: incoming NPC event is not processed because the NPC AI handler is missing.",
            ResidenceId(m_data)
        );
        return;
    }
    m_pNpcAIEventHandler->Receive(packet);
}

void CFortress::TimerExpired(const int id) {
    // Current L2Server 0x007481e8.
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
    const std::int32_t now = runtime::crt::Time32Now();
    if (!m_bWaitLoading) {
        if (m_bWaitResume) {
            SiegeResume();
        } else {
            const ResidenceState state = ResidenceStatus(m_data);
            if (state == RS_ANNOUNCING || state == RS_REGISTRATION) {
                if (state == RS_REGISTRATION && runtime::AddInt32Wrapping(m_tStateChangedTime, 3000) < now) {
                    SiegeStandby();
                } else if (!g_PledgeTimer.IsAllNpcRelatedLoaded()) {
                    m_bWaitNpcServerUp = true;
                } else if (m_bWaitNpcServerUp) {
                    if (m_pNpcMakerEventHandler == nullptr) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"RS_ANNOUNCING : m_pNpcMakerEventHandler is null")
                        // L2_LOG_ERROR(Log,
                        //     L"RS_ANNOUNCING : m_pNpcMakerEventHandler is null")
                        L2_LOG_ERROR(
                            Log,
                            "Fortress %d: NPC-ready maker notification cannot be sent because its handler is missing; the "
                            "NPC startup wait flag is still cleared.",
                            ResidenceId(m_data)
                        );
                    } else {
                        m_pNpcMakerEventHandler->Send(7, -1, -1, -1, L"");
                    }
                    m_bWaitNpcServerUp = false;
                }
            } else if (state == RS_STANDBY) {
                if (runtime::AddInt32Wrapping(m_tStateChangedTime, 600) < now) {
                    SiegeStart();
                } else {
                    BroadcastStandbyCountDown(
                        runtime::AddInt32Wrapping(runtime::SubtractInt32Wrapping(m_tStateChangedTime, now), 600)
                    );
                }
            }

            if (m_nResidenceOwnerPledgeServerId > 0) {
                if (HasMoreThan3DayAndDelta()) {
                    if (HasMoreThan75Hours()) {
                        BroadcastSystemMessageW(SystemMessageId::REBELS_RECAPTURED_FORTRESS, true);
                        BanPledgesInFortress();
                        ResetOwnership();
                        SendResetFacilityLevelToCacheD();
                    }
                } else if (m_data->m_tLastRewardGivenTime > 0 &&
                    runtime::AddInt32Wrapping(m_data->m_tLastRewardGivenTime, 0x16da0) < now) {
                    GiveOwnerReward();
                    if (m_eContractStatus == FCS_CASTLE_CONTRACT) {
                        PayFortressRent();
                    }
                }
            }

            if (g_fortressStateSaveTick == (g_fortressStateSaveTick / 300) * 300) {
                m_tStateElapsedTime = runtime::SubtractInt32Wrapping(now, m_tStateChangedTime);
                db.RequestSaveFortressState(this, state, m_tStateChangedTime, m_tStateElapsedTime);
            }
            g_fortressStateSaveTick = runtime::AddInt32Wrapping(g_fortressStateSaveTick, 1);
        }
    }
    DoDecoCheck(static_cast<std::uint32_t>(now));
    DoDecoNpcCheck(now);
    AddTimer(1000, 0);
}

void CFortressSiege::ResetBarracksCaptured() {
    // Current L2Server 0x0073db84.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_BarrackCapturedLock);
        for (auto it = m_vBarrackCaptured.begin(); it != m_vBarrackCaptured.end(); ++it) {
            *it = false;
        }
    }
    if (CFortress* const fortress = GetFortress()) {
        const int residenceId = fortress->m_data != nullptr ? ResidenceId(fortress->m_data) : 0;
        Log.Add(
            LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,%I64d,,,,,,,,,,,", static_cast<std::int64_t>(0x7db),
            static_cast<std::int64_t>(residenceId), static_cast<std::int64_t>(m_nNumBarracks), static_cast<std::int64_t>(0),
            static_cast<std::int64_t>(600)
        );
    }
}

void CFortressSiege::BarrackCaptured(const unsigned int barrackIndex) {
    // Current L2Server 0x00746b0c.
    L2_GUARD_FUNCTION();
    if (barrackIndex >= static_cast<unsigned int>(m_nNumBarracks)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid barrack in fortress siege, "
        //     L"fortress[%d] barrack [%d]",
        //     L"Fortress.cpp",
        //     0xf91,
        //     m_nFortressId,
        //     barrackIndex)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid barrack in fortress siege, "
        //     L"fortress[%d] barrack [%d]",
        //     L"Fortress.cpp",
        //     0xf91,
        //     m_nFortressId,
        //     barrackIndex)
        L2_LOG_ERROR(
            Log,
            "Fortress %d: barrack capture index %u is outside the configured range (barrack count %d); the capture is "
            "ignored.",
            m_nFortressId, barrackIndex, m_nNumBarracks
        );
        return;
    }
    {
        L2_GUARD(m_BarrackCapturedLock);
        m_vBarrackCaptured[static_cast<std::size_t>(barrackIndex)] = true;
    }
    const int captured = GetNumBarracksCaptured();
    BroadcastSystemMessageW(SystemMessageId::BARRACKS_CAPTURED);
    if (captured == 1) {
        m_tFirstBarrackCaptureTime = runtime::crt::Time32Now();
    } else if (captured == m_nNumBarracks) {
        m_bAllBarracksCaptured = true;
    }
    if (CFortress* const fortress = GetFortress()) {
        const std::int32_t remaining = runtime::AddInt32Wrapping(
            runtime::SubtractInt32Wrapping(m_tFirstBarrackCaptureTime, runtime::crt::Time32Now()), 600
        );
        const int residenceId = fortress->m_data != nullptr ? ResidenceId(fortress->m_data) : 0;
        Log.Add(
            LOG_BOTH, u"%I64d,,,,,,,,,,,%I64d,%I64d,%I64d,%I64d,,,,,,,,,,,", static_cast<std::int64_t>(0x7db),
            static_cast<std::int64_t>(residenceId), static_cast<std::int64_t>(m_nNumBarracks),
            static_cast<std::int64_t>(captured), static_cast<std::int64_t>(remaining)
        );
    }
}

bool CFortressSiege::FlagCaptured(const unsigned int pledgeDatabaseId) {
    // Current L2Server 0x0073dd24.
    L2_GUARD_FUNCTION();
    const bool captured = [&] {
        L2_GUARD(m_FortressSiegeLock);
        bool captured = false;
        if (m_eSiegeStatus == FSS_FLAG_CAPTURE && !m_bFlagCaptured) {
            CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId);
            if (pledge && m_pRegistry != nullptr && m_pRegistry->IsRegisteredPledge(pledgeDatabaseId)) {
                m_nWinnerPledgeDbId = pledgeDatabaseId;
                m_bFlagCaptured = true;
                captured = true;
            }
        }
        return captured;
    }();
    return captured;
}

void CFortressSiege::FlagCaptureWarn(const unsigned int pledgeDatabaseId, User* const user) {
    // Current L2Server 0x00744034.
    L2_GUARD_FUNCTION();
    if (m_eSiegeStatus != FSS_FLAG_CAPTURE) {
        return;
    }

    CPledge* const pledge = CPledge::FindPledge(g_nWorldId, pledgeDatabaseId).get();
    if (pledge == nullptr || !m_pRegistry->IsRegisteredPledge(pledgeDatabaseId)) {
        return;
    }

    std::vector<CPledge*> registeredPledges;
    m_pRegistry->GetRegisteredPledgeList(registeredPledges);
    for (CPledge* const registeredPledge : registeredPledges) {
        if (registeredPledge != nullptr) {
            registeredPledge->SystemMessage(
                SystemMessageId::CLAN_ATTEMPTING_TO_RAISE_FLAG, EST_STRING, std::u16string(pledge->GetPledgeName()).c_str()
            );
        }
    }

    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }

    if (CPledge* const owner = fortress->GetResidenceOwnerPledge()) {
        owner->SystemMessage(
            SystemMessageId::CLAN_ATTEMPTING_TO_RAISE_FLAG, EST_STRING, std::u16string(pledge->GetPledgeName()).c_str()
        );
    }

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x7d5, user, nullptr, pledge->GetPledgeName(), nullptr, nullptr, pledge->GetDbId(), fortress->GetResidenceID(), 0, 0,
        0, 0, 0, 0, 0, 0, 0
    );
}

void CFortressSiege::StartBarrackCapture(const bool restart) {
    // Current L2Server 0x0073ed98.
    L2_GUARD_FUNCTION();
    m_tFirstBarrackCaptureTime = 0;
    ResetBarracksCaptured();
    if (m_pNpcMakerEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"StartBarrackCapture : m_pNpcMakerEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"StartBarrackCapture : m_pNpcMakerEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: barrack-capture maker notification cannot be sent because its handler is missing; capture state "
            "is reset and the BARRACK_CAPTURE phase is still selected.",
            m_nFortressId
        );
    } else {
        m_pNpcMakerEventHandler->Send(2, restart ? 1 : 0, -1, -1, L"");
    }
    SetStatus(FSS_BARRACK_CAPTURE);
}

void CFortressSiege::StartFlagCapture(const bool createFlags) {
    // Current L2Server 0x00743f28.
    L2_GUARD_FUNCTION();
    m_tFirstBarrackCaptureTime = 0;
    if (m_pNpcMakerEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"StartFlagCapture : m_pNpcMakerEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"StartFlagCapture : m_pNpcMakerEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: flag-capture maker notification cannot be sent because its handler is missing; flag creation "
            "rules and transition to FLAG_CAPTURE still run.",
            m_nFortressId
        );
    } else {
        m_pNpcMakerEventHandler->Send(3, -1, -1, -1, L"");
    }
    if (createFlags && m_bFirstFlagCreation) {
        CreateAllFlags();
    }
    SetStatus(FSS_FLAG_CAPTURE);
}

void CFortressSiege::SetAttacker(const bool underSiege) {
    // Current L2Server 0x0074428c.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr || m_pRegistry == nullptr) {
        return;
    }
    std::vector<CPledge*> pledges;
    m_pRegistry->GetRegisteredPledgeList(pledges);
    for (CPledge* const pledge : pledges) {
        if (pledge != nullptr) {
            pledge->SetFortressUnderOffenceSiege(underSiege ? fortress : nullptr);
            pledge->SetMemberRelationChanged();
        }
    }
}

void CFortressSiege::SetDefender(const bool underSiege) {
    // Current L2Server 0x00738dd0.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    CPledge* const owner = fortress != nullptr ? fortress->GetResidenceOwnerPledge() : nullptr;
    if (owner != nullptr) {
        owner->SetFortressUnderDefenceSiege(underSiege ? fortress : nullptr);
        owner->SetMemberRelationChanged();
    }
}

void CFortressSiege::SetMembersInSiege() {
    // Current L2Server 0x00744410.
    L2_GUARD_FUNCTION();
    SetAttacker(true);
    SetDefender(true);
}

void CFortressSiege::SetMembersNotInSiege() {
    // Current L2Server 0x007444c0.
    L2_GUARD_FUNCTION();
    SetAttacker(false);
    SetDefender(false);
}

void CFortressSiege::CreateAllFlags() {
    // Current L2Server 0x00742478.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }

    if (g_ObjectDB.GetObjectW(CFortress::m_nFlagItemClassId, false) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid flag item in fortress siege, "
        //     L"fortress[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x10f1,
        //     ResidenceId(fortress->m_data),
        //     CFortress::m_nFlagItemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid flag item in fortress siege, "
        //     L"fortress[%d] flag[%d]",
        //     L"Fortress.cpp",
        //     0x10f1,
        //     ResidenceId(fortress->m_data),
        //     CFortress::m_nFlagItemClassId)
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot create siege flags because prototype class %d was not found.",
            ResidenceId(fortress->m_data), CFortress::m_nFlagItemClassId
        );
        return;
    }

    std::vector<FVector> flags;
    fortress->GetFlags(flags);
    m_nNumFlags = static_cast<int>(flags.size());
    for (FVector position : flags) {
        if (m_nNumFlags <= static_cast<int>(m_mFlagMap.size())) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] try to create exceeding number of "
            //     L"fortress flags, fortress[%d] flag[%d]",
            //     L"Fortress.cpp",
            //     0x111f,
            //     ResidenceId(fortress->m_data),
            //     CFortress::m_nFlagItemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] try to create exceeding number of "
            //     L"fortress flags, fortress[%d] flag[%d]",
            //     L"Fortress.cpp",
            //     0x111f,
            //     ResidenceId(fortress->m_data),
            //     CFortress::m_nFlagItemClassId)
            L2_LOG_ERROR(
                Log,
                "Fortress %d: flag creation stops at the configured count %d (registry entries %llu, class %d); existing "
                "flags are retained.",
                ResidenceId(fortress->m_data), m_nNumFlags, static_cast<unsigned long long>(m_mFlagMap.size()),
                CFortress::m_nFlagItemClassId
            );
            break;
        }

        for (const auto& entry : m_mFlagMap) {
            if (position.IsEqual(entry.second.first)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] try to create flag in duplicate "
                //     L"position, fortress[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1108,
                //     ResidenceId(fortress->m_data),
                //     CFortress::m_nFlagItemClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] try to create flag in duplicate "
                //     L"position, fortress[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1108,
                //     ResidenceId(fortress->m_data),
                //     CFortress::m_nFlagItemClassId)
                L2_LOG_ERROR(
                    Log,
                    "Fortress %d: duplicate flag position (%.17g, %.17g, %.17g) for class %d; creation stops and earlier "
                    "flags are retained.",
                    ResidenceId(fortress->m_data), position.x, position.y, position.z, CFortress::m_nFlagItemClassId
                );
                return;
            }
        }

        CGeoData* const geo = CWorld::Inst().GetGeo();
        position.z = static_cast<double>(
            geo->FindGround(static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z))
        );
        CItem* const item = g_ObjectDB.CreateItem(CFortress::m_nFlagItemClassId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid flag item in fortress siege, "
            //     L"fortress[%d] flag[%d]",
            //     L"Fortress.cpp",
            //     0x1118,
            //     ResidenceId(fortress->m_data),
            //     CFortress::m_nFlagItemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid flag item in fortress siege, "
            //     L"fortress[%d] flag[%d]",
            //     L"Fortress.cpp",
            //     0x1118,
            //     ResidenceId(fortress->m_data),
            //     CFortress::m_nFlagItemClassId)
            L2_LOG_ERROR(
                Log,
                "Fortress %d: flag item creation returned null for class %d; creation stops and earlier flags are retained.",
                ResidenceId(fortress->m_data), CFortress::m_nFlagItemClassId
            );
            return;
        }
        PushFlagItem(item, position);
    }
}

void CFortressSiege::DeleteAllFlags() {
    // Current L2Server 0x0073ee9c.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_mFlagMap) {
        CItem* const item = ItemSmartIdManager()->GetObjectW(entry.first);
        if (item == nullptr) {
            if (CFortress* const fortress = GetFortress()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid flag item in fortress siege, "
                //     L"fortress[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1163,
                //     ResidenceId(fortress->m_data),
                //     entry.first)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] invalid flag item in fortress siege, "
                //     L"fortress[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1163,
                //     ResidenceId(fortress->m_data),
                //     entry.first)
                L2_LOG_ERROR(
                    Log,
                    "Fortress %d: flag SID %u cannot be resolved during cleanup; this item is skipped and the flag registry "
                    "is cleared after the sweep.",
                    ResidenceId(fortress->m_data), static_cast<unsigned>(entry.first)
                );
            }
            continue;
        }
        if (entry.second.second == 0) {
            item->m_data->m_bIsActive = false;
            CWorld::Inst().PickAndDeleteItem(item);
            continue;
        }
        CObject* const ownerObject = CObject::GetObjectW(entry.second.second);
        User* const user = ownerObject != nullptr && ownerObject->IsPC() ? ownerObject->CastUser() : nullptr;
        if (user != nullptr) {
            if (user->m_inv.HaveItemByClassId(item->m_data->m_nClassID, 1, false, false)) {
                db.RequestDestroyItem(item, 1, user, 0);
            } else if (CFortress* const fortress = GetFortress()) {
                const int fortressDatabaseId = ResidenceId(fortress->m_data);
                const unsigned int flagDatabaseId = item->GetDBID();
                const unsigned int userDatabaseId = user->GetDBID();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] flag item not in user inventory, "
                //     L"fortress[%d] user[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1153,
                //     fortressDatabaseId,
                //     userDatabaseId,
                //     flagDatabaseId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] flag item not in user inventory, "
                //     L"fortress[%d] user[%d] flag[%d]",
                //     L"Fortress.cpp",
                //     0x1153,
                //     fortressDatabaseId,
                //     userDatabaseId,
                //     flagDatabaseId)
                L2_LOG_ERROR(
                    Log,
                    "Fortress %d: character DB ID %u has no inventory item of flag class %d (flag DB ID %u); destruction is "
                    "not requested and the flag registry is cleared after the sweep.",
                    fortressDatabaseId, userDatabaseId, item->m_data->m_nClassID, flagDatabaseId
                );
            }
        }
    }
    m_mFlagMap.clear();
}

void CFortressSiege::BroadcastSystemMessageW(const int messageId) {
    // Current L2Server 0x00745c68.
    L2_GUARD_FUNCTION();
    if (CFortress* const fortress = GetFortress()) {
        fortress->BroadcastSystemMessageW(messageId, true);
    }
}

void CFortressSiege::BroadcastSystemMessageW(const int messageId, const int value) {
    // Current registry-routed integer overload.
    L2_GUARD_FUNCTION();
    if (m_pRegistry != nullptr) {
        m_pRegistry->BroadcastSystemMessageW(messageId, value);
    }
    if (CFortress* const fortress = GetFortress()) {
        if (CPledge* const owner = fortress->GetResidenceOwnerPledge()) {
            owner->SystemMessage(messageId, value);
        }
    }
}

void CFortressSiege::InitSiege(const bool resume) {
    // Current L2Server 0x0074593c.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }
    const int facilityLevel = [&] {
        L2_GUARD(fortress->m_FortressLock);
        if (!m_bIsDominionSiege) {
            fortress->BanExceptOwnerPledge();
            fortress->MakeDoorsBreakable(true);
            fortress->SetBattleField(true);
        }
        const int facilityLevel = fortress->GetFacilityLevel(2);
        fortress->ResetFlagpole();
        return facilityLevel;
    }();

    for (CDoor* const door : fortress->m_DoorList) {
        if (resume) {
            door->ValidateDamageGrade();
            door->ValidatePlaneCollision();
        } else if (facilityLevel != 0) {
            door->m_data->m_dMaxHP *= 1.5;
            door->SetHP(door->m_data->m_dMaxHP);
        }
        door->Close(false, false);
    }
    fortress->MakeNPCDisable(true);
    if (resume) {
        db.RequestLoadBattleCamp(fortress, 0);
    }
    if (!m_bIsDominionSiege) {
        SetMembersInSiege();
    }
}

void CFortressSiege::FinSiege() {
    // Current L2Server 0x00745b14.
    L2_GUARD_FUNCTION();
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        return;
    }
    {
        L2_GUARD(fortress->m_FortressLock);
        fortress->ExpelNonPledgeMember();
        fortress->SetBattleField(false);
    }
    for (CDoor* const door : fortress->m_DoorList) {
        if (door != nullptr) {
            door->ResetDoorState(false, 1.0);
        }
    }
    fortress->MakeNPCDisable(false);
    CBattleCampDb::inst().SetDisable(static_cast<std::uint32_t>(ResidenceId(fortress->m_data)));
    db.RequestUninstallBattleCampBy(fortress);
    if (!m_bIsDominionSiege) {
        SetMembersNotInSiege();
    }
}

void CFortressSiege::Start() {
    // Current L2Server 0x007467e8.
    L2_GUARD_FUNCTION();
    InitSiege(false);
    if (m_pNpcMakerEventHandler == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CFortressSiege::Start : m_pNpcMakerEventHandler is null")
        // L2_LOG_ERROR(Log,
        //     L"CFortressSiege::Start : m_pNpcMakerEventHandler is null")
        L2_LOG_ERROR(
            Log,
            "Fortress %d: siege-start maker notification cannot be sent because its handler is missing; barrack capture and "
            "timer startup continue.",
            m_nFortressId
        );
    } else {
        m_pNpcMakerEventHandler->Send(1, 0, 0, 0, L"");
    }
    m_tSiegeStartTime = runtime::crt::Time32Now();
    StartBarrackCapture(false);
    AddSiegeLog(
        const_cast<wchar_t*>(L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,%I64d,"
                             L"%I64d,,,,,,,%s,%s,,,"),
        0x7d9, FSS_BARRACK_CAPTURE
    );
    StartTimer();
}

bool CFortressSiege::Resume(const FortressSiegeResumeType resumeType, const FortressSiegeResumeInfo resumeInfo) {
    // Current L2Server 0x0074690c.
    L2_GUARD_FUNCTION();
    if (!g_PledgeTimer.IsAllNpcRelatedLoaded()) {
        // Log.Add(LOG_ERROR, L"Fortress Maker not finish")
        // L2_LOG_ERROR(Log, L"Fortress Maker not finish")
        // Noise: The paused siege timer retries NPC-server recovery every 10 seconds until NPC data is ready. Those expected
        // polls need no message.
        if (resumeType != FSRT_NPC_SERVER_DOWN) {
            L2_LOG_WARN(
                Log,
                "Fortress %d: siege restoration cannot proceed because NPC-related loading is incomplete; the resume call "
                "returns failure.",
                m_nFortressId
            );
        }
        return false;
    }
    int parentCastleId = -1;
    if (CFortress* const fortress = GetFortress()) {
        parentCastleId = fortress->GetParentCastleId();
    }
    InitSiege(true);
    if (!m_bIsDominionSiege) {
        if (m_pNpcMakerEventHandler == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CFortress::Resume : m_pNpcMakerEventHandler is null")
            // L2_LOG_ERROR(Log,
            //     L"CFortress::Resume : m_pNpcMakerEventHandler is null")
            L2_LOG_ERROR(
                Log,
                "Fortress %d: siege-resume maker notification cannot be sent because its handler is missing; state "
                "restoration and timer startup continue.",
                m_nFortressId
            );
        } else {
            m_pNpcMakerEventHandler->Send(1, parentCastleId, -1, -1, L"");
        }
    }
    if (resumeType == FSRT_SERVER_DOWN) {
        m_tSiegeStartTime = resumeInfo.tSiegeStartTime;
        m_tSiegeElapsedTime = resumeInfo.tSiegeElapsedTime;
        m_eSiegeStatus = resumeInfo.eSiegeStatus;
    }
    if (m_eSiegeStatus != FSS_NONE) {
        const std::int32_t now = runtime::crt::Time32Now();
        if (m_tSiegeStartTime < 0 || m_tSiegeElapsedTime < 0 ||
            now < runtime::AddInt32Wrapping(m_tSiegeStartTime, m_tSiegeElapsedTime)) {
            m_tSiegeElapsedTime = 0;
            m_tSiegeStartTime = now;
        } else {
            m_tSiegeStartTime = runtime::SubtractInt32Wrapping(now, m_tSiegeElapsedTime);
        }
        if (!m_bIsDominionSiege) {
            if (m_eSiegeStatus == FSS_BARRACK_CAPTURE) {
                StartBarrackCapture(false);
            } else if (m_eSiegeStatus == FSS_FLAG_CAPTURE) {
                StartFlagCapture(resumeType != FSRT_NPC_SERVER_DOWN);
            } else {
                // Log.Add(LOG_ERROR, "error")
                // L2_LOG_ERROR(Log, "error")
                L2_LOG_ERROR(
                    Log,
                    "Fortress %d: unsupported siege phase %d during resume type %d; capture-phase initialization is "
                    "skipped, but the siege is unpaused and its timer starts.",
                    m_nFortressId, static_cast<int>(m_eSiegeStatus), static_cast<int>(resumeType)
                );
            }
        }
    }
    m_bPaused = false;
    StartTimer();
    return true;
}

void CFortressSiege::End() {
    // Current L2Server 0x00747b70.
    L2_GUARD_FUNCTION();
    if (!m_bIsDominionSiege) {
        DeleteAllFlags();
        if (m_pNpcMakerEventHandler == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"CFortress::End : m_pNpcMakerEventHandler is null")
            // L2_LOG_ERROR(Log,
            //     L"CFortress::End : m_pNpcMakerEventHandler is null")
            L2_LOG_ERROR(
                Log,
                "Fortress %d: siege-end maker notification cannot be sent because its handler is missing; result processing "
                "continues.",
                m_nFortressId
            );
        } else {
            m_pNpcMakerEventHandler->Send(4, -1, -1, -1, L"");
        }
    }
    CFortress* const fortress = GetFortress();
    if (fortress == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Fortress in Fortress Siege [%d]",
        //     L"Fortress.cpp",
        //     0xefb,
        //     m_nFortressId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Fortress in Fortress Siege [%d]",
        //     L"Fortress.cpp",
        //     0xefb,
        //     m_nFortressId)
        L2_LOG_ERROR(
            Log,
            "Cannot finish siege for fortress %d: the residence object is unavailable; remaining finalization is skipped.",
            m_nFortressId
        );
        return;
    }
    FortressSiegeResult result;
    if (m_bIsDominionSiege) {
        for (CControlTower* const tower : fortress->m_ControlTowerList) {
            if (tower != nullptr) {
                tower->OnSiegeEnded();
            }
        }
    } else {
        CSPointer<CPledge> winner;
        if (m_bFlagCaptured) {
            winner = CPledge::FindPledge(g_nWorldId, m_nWinnerPledgeDbId);
        }
        if (winner) {
            result.nWinnerPledgeDBId = static_cast<int>(m_nWinnerPledgeDbId);
        } else {
            fortress->ShowOwnerFlagPole();
        }
        AddSiegeLog(
            const_cast<wchar_t*>(L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,"
                                 L"%I64d,,,,,,,,%s,%s,,,"),
            0x7da, m_eSiegeStatus
        );
    }
    FinSiege();
    SetStatus(FSS_NONE);
    fortress->SiegeEnd(result);
}

void CFortressSiege::TimerExpired(const int id) {
    // Current L2Server 0x00748588.
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
    if (!g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        Pause();
    } else if (m_bPaused) {
        FortressSiegeResumeInfo resumeInfo{};
        if (!Resume(FSRT_NPC_SERVER_DOWN, resumeInfo)) {
            AddTimer(10000, 0);
        }
        return;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (m_bIsDominionSiege) {
        return;
    }
    if (runtime::AddInt32Wrapping(m_tSiegeStartTime, 0xe10) < now || m_bFlagCaptured) {
        End();
        return;
    }
    if (m_eSiegeStatus == FSS_BARRACK_CAPTURE) {
        if (!m_bAllBarracksCaptured) {
            if (m_tFirstBarrackCaptureTime > 0 && runtime::AddInt32Wrapping(m_tFirstBarrackCaptureTime, 600) < now) {
                StartBarrackCapture(true);
                BroadcastSystemMessageW(SystemMessageId::BARRACKS_FUNCTION_RESTORED);
            }
        } else {
            StartFlagCapture(true);
            BroadcastSystemMessageW(SystemMessageId::ALL_BARRACKS_CAPTURED);
            AddSiegeLog(
                const_cast<wchar_t*>(L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,"
                                     L"%I64d,,,,,,,,%s,%s,,,"),
                0x7da, FSS_BARRACK_CAPTURE
            );
            AddSiegeLog(
                const_cast<wchar_t*>(L"%I64d,%I64d,%I64d,,,,,,%s,,,%I64d,%I64d,"
                                     L"%I64d,%I64d,,,,,,,%s,%s,,,"),
                0x7d9, FSS_FLAG_CAPTURE
            );
        }
    }

    m_tSiegeElapsedTime = runtime::SubtractInt32Wrapping(now, m_tSiegeStartTime);
    if ((m_eSiegeStatus == FSS_BARRACK_CAPTURE || m_eSiegeStatus == FSS_FLAG_CAPTURE) &&
        m_nSiegeElapsedTimeSaveTick == (m_nSiegeElapsedTimeSaveTick / 30) * 30) {
        db.RequestSaveFortressSiegeState(m_nFortressId, m_eSiegeStatus, m_tSiegeStartTime, m_tSiegeElapsedTime, true);
    }
    ++m_nSiegeElapsedTimeSaveTick;

    CFortress* const fortress = GetFortress();
    const wchar_t* const musicName = g_CastleSiegeMusicPlayer[1].GetMusicName(m_tSiegeElapsedTime);
    if (fortress != nullptr && musicName != m_sCurrentMusicName) {
        m_sCurrentMusicName = const_cast<wchar_t*>(musicName);
        if (musicName != nullptr) {
            FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
                if (user->IsInRealWorld() && fortress->IsInBattleField(user->GetPos(), true)) {
                    user->PlayMusic(WideToUtf16(musicName).c_str());
                }
            });
        }
    }
    AddTimer(1000, 0);
}

bool CFortressDBEventHandler::ReplyLoadFortress(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x007428e0.
    L2_GUARD_FUNCTION();
    int fortressId = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &fortressId);
    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence == nullptr) {
        return false;
    }

    CFortress* const fortress = residence->CastFortress();
    if (fortress == nullptr || !fortress->IsFortress()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress does not exist in ReplyLoadAgitPacket [%d]",
        //     L"Fortress.cpp",
        //     0x12bc,
        //     fortressId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fortress does not exist in ReplyLoadAgitPacket [%d]",
        //     L"Fortress.cpp",
        //     0x12bc,
        //     fortressId)
        L2_LOG_ERROR(
            Log,
            "Cannot restore fortress %d from the database reply: the residence is not a fortress; the remaining reply is "
            "not processed.",
            fortressId
        );
        return false;
    }

    int state = 0;
    int stateChangedTime = 0;
    int stateElapsedTime = 0;
    int pledgeId = 0;
    int contractStatus = 0;
    int parentCastleId = 0;
    int lastOwnerChangedTime = 0;
    int lastRewardGivenTime = 0;
    int ownerRewardCycleCount = 0;
    int castleTreasureLevel = 0;
    int ownerSiegeDefendCount = 0;
    int decoCount = 0;
    int facilityCount = 0;
    cursor = ::Disassemble(
        cursor, "ddddddddddddd", &state, &stateChangedTime, &stateElapsedTime, &pledgeId, &contractStatus, &parentCastleId,
        &lastOwnerChangedTime, &lastRewardGivenTime, &ownerRewardCycleCount, &castleTreasureLevel, &ownerSiegeDefendCount,
        &decoCount, &facilityCount
    );

    int decoNpcCount = 0;
    cursor = ::Disassemble(cursor, "d", &decoNpcCount);
    fortress->SetStateInfoFromDB(state, stateChangedTime, stateElapsedTime);
    fortress->SetOwnerInfoFromDB(pledgeId, lastOwnerChangedTime, ownerSiegeDefendCount);
    fortress->SetContractInfoFromDB(pledgeId, contractStatus, parentCastleId);
    fortress->SetRewardInfoFromDB(lastRewardGivenTime, ownerRewardCycleCount, castleTreasureLevel);

    for (CDoor* const door : fortress->m_DoorList) {
        db.RequestLoadDoor(door);
    }
    for (int index = 0; index < decoCount; ++index) {
        int decoType = 0;
        int decoId = 0;
        int decoLevel = 0;
        int decoExpire = 0;
        cursor = ::Disassemble(cursor, "dddd", &decoType, &decoId, &decoLevel, &decoExpire);
        Deco& deco = fortress->m_data->m_aDeco.data()[decoType];
        deco.nId = decoId;
        deco.nLevel = decoLevel;
        deco.nExpire = static_cast<std::uint32_t>(decoExpire);
    }
    for (int index = 0; index < facilityCount; ++index) {
        int facilityType = 0;
        int facilityLevel = 0;
        cursor = ::Disassemble(cursor, "dd", &facilityType, &facilityLevel);
        fortress->SetFacilityLevel(facilityType, facilityLevel);
    }

    ResidenceUtil::DisassembleDecoNpc(cursor, decoNpcCount, fortress);
    db.RequestLoadAllControlTower(fortress);
    if (fortress->m_OwnerRestartPointList.empty()) {
        fortress->m_OwnerRestartPointList.push_back(FVector{});
    }
    fortress->OnDecoSet();
    if (g_JobFinishSignal.SetFortressLoaded()) {
        CPledge::SetFlagReservePledge(PRLT_FORTRESS);
    }
    return false;
}

bool CFortressDBEventHandler::ReplyLoadFortressSiege(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00747ebc.
    L2_GUARD_FUNCTION();
    int fortressId = 0;
    int siegeStatus = 0;
    int siegeStartTime = 0;
    int siegeElapsedTime = 0;
    int validInfo = 0;
    ::Disassemble(packet, "ddddd", &fortressId, &siegeStatus, &siegeStartTime, &siegeElapsedTime, &validInfo);

    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence != nullptr && ResidenceStatus(residence->m_data) == RS_SIEGE) {
        CFortress* const fortress = static_cast<CFortress*>(residence);
        fortress->SetSiegeInfoFromDB(siegeStatus, siegeStartTime, siegeElapsedTime, validInfo != 0);
        fortress->HandleReplyLoadFortressSiege();
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] invalid fortress status in loading siege from db. fortress[%d] state[%d]",
    //     L"Fortress.cpp",
    //     0x1339,
    //     fortressId,
    //     siegeStatus)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] invalid fortress status in loading siege from db. fortress[%d] state[%d]",
    //     L"Fortress.cpp",
    //     0x1339,
    //     fortressId,
    //     siegeStatus)
    L2_LOG_ERROR(
        Log,
        "Cannot restore siege data for fortress %d: %s (current residence state %d, reply siege phase %d); the reply is "
        "ignored.",
        fortressId, residence == nullptr ? "residence was not found" : "residence is not in SIEGE",
        residence != nullptr ? static_cast<int>(ResidenceStatus(residence->m_data)) : static_cast<int>(RS_MAX), siegeStatus
    );
    return false;
}

bool CFortressDBEventHandler::ReplyLoadFortressSiegeRegistry(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x00742c78.
    L2_GUARD_FUNCTION();
    int fortressId = 0;
    int pledgeCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &fortressId, &pledgeCount);
    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid fortress in loading siege registry from db. fortress[%d] ",
        //     L"Fortress.cpp",
        //     0x135a,
        //     fortressId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid fortress in loading siege registry from db. fortress[%d] ",
        //     L"Fortress.cpp",
        //     0x135a,
        //     fortressId)
        L2_LOG_ERROR(
            Log,
            "Cannot restore the siege registry for fortress %d: the residence was not found; %d declared clan entries are "
            "not processed.",
            fortressId, pledgeCount
        );
        return false;
    }

    std::set<std::uint32_t> pledgeIds;
    for (int index = 0; index < pledgeCount; ++index) {
        int pledgeId = 0;
        cursor = ::Disassemble(cursor, "d", &pledgeId);
        const int databaseId = runtime::process::CurrentDatabaseId();
        CPledge* pledge = nullptr;
        {
            CSPointer<CPledge> holder = CPledge::FindPledge(databaseId, pledgeId);
            pledge = holder.get();
        }
        if (pledge == nullptr) {
            db.RequestLoadPledge(databaseId, static_cast<unsigned int>(pledgeId), false);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] ::ReplyLoadFortressSiegeRegistry LoadPledge PledgeDBID[%d]",
            //     pledgeId)
            // Noise: Requesting an unloaded referenced clan is normal. This per-entry trace repeats the load request without
            // establishing completion. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] ::ReplyLoadFortressSiegeRegistry LoadPledge PledgeDBID[%d]",
            //     pledgeId)
        }
        pledgeIds.insert(static_cast<std::uint32_t>(pledgeId));
    }

    CFortress* const fortress = static_cast<CFortress*>(residence);
    if (pledgeIds.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] registered pledge list is empty in loading fortress siege registry[%d]",
        //     L"Fortress.cpp",
        //     0x1372,
        //     ResidenceId(fortress->m_data))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] registered pledge list is empty in loading fortress siege registry[%d]",
        //     L"Fortress.cpp",
        //     0x1372,
        //     ResidenceId(fortress->m_data))
        L2_LOG_INFO(
            Log,
            "Fortress %d: restoring an empty siege registry (declared clan count %d); the residence-state handler still "
            "runs.",
            ResidenceId(fortress->m_data), pledgeCount
        );
    }
    fortress->HandleReplyLoadFortressSiegeRegistry(pledgeIds);
    return false;
}

bool CFortressDBEventHandler::ReplyPayFortressRent(CDBSocket*, const unsigned char* packet) {
    // Current L2Server 0x0073ccac.
    L2_GUARD_FUNCTION();
    int fortressId = 0;
    int ownerPledgeId = 0;
    int castlePledgeId = 0;
    std::int64_t rent = 0;
    int result = 0;
    ::Disassemble(packet, "dddQd", &fortressId, &ownerPledgeId, &castlePledgeId, &rent, &result);

    CResidence* const residence = g_ResidenceDB.GetResidence(fortressId);
    if (residence == nullptr) {
        return false;
    }
    CFortress* const fortress = static_cast<CFortress*>(residence);
    if (fortress->m_nResidenceOwnerPledgeServerId < 1 || fortress->m_eContractStatus != FCS_CASTLE_CONTRACT) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid fortress status in paying fortress rent from db. fortress[%d] pledge[%d] castle
        //     pledge[%d]", L"Fortress.cpp", 0x1396, ResidenceId(fortress->m_data), ownerPledgeId, castlePledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid fortress status in paying fortress rent from db. fortress[%d] pledge[%d] castle
        //     pledge[%d]", L"Fortress.cpp", 0x1396, ResidenceId(fortress->m_data), ownerPledgeId, castlePledgeId)
        L2_LOG_ERROR(
            Log,
            "Fortress %d: rent reply for owner clan %d, castle clan %d is ignored; current owner SID %d and contract status "
            "%d do not allow a castle-contract payment reply.",
            ResidenceId(fortress->m_data), ownerPledgeId, castlePledgeId, fortress->m_nResidenceOwnerPledgeServerId,
            static_cast<int>(fortress->m_eContractStatus)
        );
        return false;
    }

    CResidenceData* const data = fortress->m_data;
    const int expectedOwnerId = data != nullptr ? static_cast<int>(data->m_nResidenceOwnerID) : 0;
    if (ownerPledgeId != expectedOwnerId) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid owner pledge in paying fortress rent from db. fortress[%d] pledge[%d] castle pledge[%d]",
        //     L"Fortress.cpp",
        //     0x139d,
        //     ResidenceId(data),
        //     ownerPledgeId,
        //     castlePledgeId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid owner pledge in paying fortress rent from db. fortress[%d] pledge[%d] castle pledge[%d]",
        //     L"Fortress.cpp",
        //     0x139d,
        //     ResidenceId(data),
        //     ownerPledgeId,
        //     castlePledgeId)
        L2_LOG_ERROR(
            Log,
            "Fortress %d: rent reply owner clan %d differs from current owner clan %d (castle clan %d); the reply is "
            "ignored.",
            ResidenceId(data), ownerPledgeId, expectedOwnerId, castlePledgeId
        );
        return false;
    }
    if (rent != data->m_nFortressRent) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid amount in paying fortress rent from db. fortress[%d] castle pledge[%d] amount[%I64d]",
        //     L"Fortress.cpp",
        //     0x13a4,
        //     ResidenceId(data),
        //     castlePledgeId,
        //     rent)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid amount in paying fortress rent from db. fortress[%d] castle pledge[%d] amount[%I64d]",
        //     L"Fortress.cpp",
        //     0x13a4,
        //     ResidenceId(data),
        //     castlePledgeId,
        //     rent)
        L2_LOG_ERROR(
            Log,
            "Fortress %d: rent reply amount %lld adena differs from configured rent %lld (castle clan %d); the reply is "
            "ignored.",
            ResidenceId(data), static_cast<long long>(rent), static_cast<long long>(data->m_nFortressRent), castlePledgeId
        );
        return false;
    }

    fortress->HandleReplyPayFortressRent(result);
    if (result == 0) {
        return false;
    }

    CPledge* ownerPledge = nullptr;
    {
        CSPointer<CPledge> holder = CPledge::FindPledge(g_nWorldId, ownerPledgeId);
        ownerPledge = holder.get();
    }
    if (ownerPledge == nullptr) {
        return false;
    }
    User* const ownerMaster = ownerPledge->GetMaster();
    if (ownerMaster == nullptr) {
        return false;
    }

    CLogFormatter ownerFormatter;
    ownerFormatter.WriteLog4(
        0x7d7, ownerMaster, nullptr, ownerPledge->GetPledgeName(), nullptr, nullptr, ownerPledge->GetDbId(),
        ResidenceId(data), 0, 0, 0, 0, 0, 0, 0, 0, 0
    );

    CSPointer<CPledge> castlePledge =
        castlePledgeId == 0 ? CSPointer<CPledge>{} : CPledge::FindPledge(g_nWorldId, castlePledgeId);
    if (!castlePledge) {
        return false;
    }
    User* const castleMaster = castlePledge->GetMaster();
    if (castleMaster == nullptr) {
        return false;
    }

    CLogFormatter castleFormatter;
    castleFormatter.WriteLog4(
        0x7d1, castleMaster, nullptr, castlePledge->GetPledgeName(), nullptr, nullptr, castlePledge->GetDbId(),
        fortress->GetParentCastleId(), ResidenceId(data), rent, 0, 0, 0, 0, 0, 0, 0
    );
    return false;
}

CFortressFlagpole::CFortressFlagpole(CFortress* fortress) :
    m_bIsFlagSet(true), padding_4e9{}, padding_4fc{}, m_pFortress(fortress) {
    CStaticObject::OnCreate();
}

CFortressFlagpole::~CFortressFlagpole() {
    L2_GUARD_FUNCTION();
    g_ServerContext.FortressFlagpole.fetch_sub(1, std::memory_order_relaxed);
    m_pFortress = nullptr;
}

bool CFortressFlagpole::IsFortressFlagpole() { return true; }

CFortressFlagpole* CFortressFlagpole::CastFortressFlagpole() { return this; }

bool CFortressFlagpole::IsAlive() { return true; }

void CFortressFlagpole::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    static_cast<void>(creatureId);
    data->m_nDistance = 0;
    data->m_nAcceptableDistance = 0;
}

bool CFortressFlagpole::IsFlagSet() { return m_bIsFlagSet; }

void CFortressFlagpole::SendFortressFlagpoleInfo(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        const FVector position = GetPos();
        g_ptlUserMap->BroadcastToNeighbor(
            *this, kPendingWriteLimit, position, "cddddddddddd", 0x9f, m_data->m_nEditorID, static_cast<int>(m_nId), 0, 1,
            m_bIsFlagSet ? 1 : 0, 0, 0, 0, 0, 0, 0
        );
        return;
    }

    socket->Send(
        "cddddddddddd", 0x9f, m_data->m_nEditorID, static_cast<int>(m_nId), 0, 1, m_bIsFlagSet ? 1 : 0, 0, 0, 0, 0, 0, 0
    );
}

void CFortressFlagpole::CapturedInFortressSiegeBy(User* user) {
    L2_GUARD_FUNCTION();
    if (m_pFortress->GetResidenceState() != RS_SIEGE || !user) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (!pledge) {
        return;
    }

    CFortressSiege* const siege = m_pFortress->GetSiege();
    if (!siege) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag captured in invalid fortress siege [%d]",
        //     L"Fortress.cpp",
        //     0xd30,
        //     m_pFortress->GetResidenceID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag captured in invalid fortress siege [%d]",
        //     L"Fortress.cpp",
        //     0xd30,
        //     m_pFortress->GetResidenceID())
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot complete flagpole capture because the siege object is unavailable.",
            m_pFortress->GetResidenceID()
        );
        return;
    }

    if (siege->FlagCaptured(pledge->GetDbId())) {
        L2_GUARD(m_FlagLock);
        if (!m_bIsFlagSet) {
            m_bIsFlagSet = true;
            SendFortressFlagpoleInfo(nullptr);
        }
    }
}

void CFortressFlagpole::CaptureInFortressSiegeStart(User* user) {
    L2_GUARD_FUNCTION();
    if (m_bIsFlagSet || m_pFortress->GetResidenceState() != RS_SIEGE || !user) {
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (!pledge) {
        return;
    }

    CFortressSiege* const siege = m_pFortress->GetSiege();
    if (!siege) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] flag captured in invalid fortress siege [%d]",
        //     L"Fortress.cpp",
        //     0xd0e,
        //     m_pFortress->GetResidenceID())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] flag captured in invalid fortress siege [%d]",
        //     L"Fortress.cpp",
        //     0xd0e,
        //     m_pFortress->GetResidenceID())
        L2_LOG_ERROR(
            Log, "Fortress %d: cannot announce the start of flagpole capture because the siege object is unavailable.",
            m_pFortress->GetResidenceID()
        );
        return;
    }

    siege->FlagCaptureWarn(pledge->GetDbId(), user);
}
