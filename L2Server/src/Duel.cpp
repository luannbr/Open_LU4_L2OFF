#include "Duel.h"
#include "CConfig.h"
#include "InstantZone.h"
#include "MultiPartyCommandChannelManager.h"
#include "MultiPartyCommandChannelRegistry.h"
#include "NPC.h"
#include "NpcServer.h"
#include "Party.h"
#include "PartyMatchingRequest.h"
#include "Pet.h"
#include "Pledge.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>

namespace {

bool EqualDuelBuilderFamilyIgnoreCase(const char16_t* left, const char16_t* right) {
#if defined(_WIN32)
    return ::_wcsicmp(Utf16ToWide(left).c_str(), Utf16ToWide(right).c_str()) == 0;
#else
    while (*left != u'\0' && *right != u'\0') {
        const char16_t foldedLeft = *left >= u'A' && *left <= u'Z' ? static_cast<char16_t>(*left + (u'a' - u'A')) : *left;
        const char16_t foldedRight =
            *right >= u'A' && *right <= u'Z' ? static_cast<char16_t>(*right + (u'a' - u'A')) : *right;
        if (foldedLeft != foldedRight) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == *right;
#endif
}

} // namespace

namespace {

const wchar_t* DuelAuditAccountName(const std::array<char16_t, 15>& source, std::array<wchar_t, 16>& storage) {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(source.data());
#else
    std::size_t length = 0;
    while (length < source.size() && source[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = ::Utf16ToWide(std::u16string_view(source.data(), length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

} // namespace

CDuelBuilderCmdHandler::stDuelBuilderCmdMap CDuelBuilderCmdHandler::m_IndividualDuelCmdMap[2] = {
    {
        u"start",
        &CDuelBuilderCmdHandler::BuilderCmd_IndividualDuelStart,
    },
    {
        u"cancel",
        &CDuelBuilderCmdHandler::BuilderCmd_IndividualDuelCancel,
    },
};

CDuelBuilderCmdHandler::stDuelBuilderCmdMap CDuelBuilderCmdHandler::m_PartyDuelCmdMap[1] = {
    {
        u"start",
        &CDuelBuilderCmdHandler::BuilderCmd_PartyDuelStart,
    },
};

bool CDuelBuilderCmdHandler::Handle(CSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x006e78dc, original owner duel.cpp.
    L2_GUARD_FUNCTION();

    char16_t* parameter = nullptr;
    char16_t* remaining = ::GetLeftString2(command, &parameter);
    if (parameter != nullptr) {
        stDuelBuilderCmdMap* commandMap = nullptr;
        unsigned int commandCount = 0;
        const bool individual = EqualDuelBuilderFamilyIgnoreCase(parameter, u"single");

        if (individual) {
            commandMap = m_IndividualDuelCmdMap;
            commandCount = 2;
        } else {
            const bool party = EqualDuelBuilderFamilyIgnoreCase(parameter, u"party");

            if (!party) {
                static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", u"//duel [single | party] [start | cancel]");
                return false;
            }
            commandMap = m_PartyDuelCmdMap;
            commandCount = 1;
        }

        remaining = ::GetLeftString2(remaining, &parameter);
        for (unsigned int index = 0; index < commandCount; ++index) {
            const std::size_t commandLength = std::char_traits<char16_t>::length(commandMap[index].pszCmdString);
            if (std::u16string_view(parameter).substr(0, commandLength) == commandMap[index].pszCmdString) {
                return commandMap[index].func(socket, user, remaining);
            }
        }
    }

    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", u"//duel [single | party] [start | cancel]");
    return false;
}

bool CDuelBuilderCmdHandler::BuilderCmd_IndividualDuelStart(CSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x006ed0e0, original owner duel.cpp.
    L2_GUARD_FUNCTION();

    SharedUtilsWideChar targetName[50];
    SharedUtilsWideChar lowerTargetName[25];
    std::memset(lowerTargetName, 0, 0x32);

    char16_t* parameter = nullptr;
    static_cast<void>(::GetLeftString2(command, &parameter));
    if (parameter != nullptr) {
        const std::u16string_view targetText(parameter);
        if (targetText.size() >= 0x32) {
            targetName[0] = u'\0';
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        } else {
            std::copy_n(parameter, targetText.size() + 1, targetName);
        }
        ::ToLowerCase(targetName, lowerTargetName);
        const std::u16string lowerName(
            lowerTargetName, lowerTargetName + std::char_traits<SharedUtilsWideChar>::length(lowerTargetName)
        );
        User* const target = GetUserFromNameMap(g_nWorldId, lowerName).get();

        if (target != nullptr) {
            static_cast<void>(CDuelManager::inst().StartDuel(user, target, DT_INDIVIDUAL));
            return false;
        }
    }

    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", u"//duel single start [char name]");
    return false;
}

bool CDuelBuilderCmdHandler::BuilderCmd_IndividualDuelCancel(CSocket*, User* const user, char16_t*) {
    // Current L2Server 0x006ed258, original owner duel.cpp.
    L2_GUARD_FUNCTION();
    CDuelManager::inst().CancelDuel(user, 1);
    return false;
}

bool CDuelBuilderCmdHandler::BuilderCmd_PartyDuelStart(CSocket* const socket, User* const user, char16_t* command) {
    // Current L2Server 0x006ed30c, original owner duel.cpp.
    L2_GUARD_FUNCTION();

    SharedUtilsWideChar targetName[50];
    SharedUtilsWideChar lowerTargetName[25];
    std::memset(lowerTargetName, 0, 0x32);

    char16_t* parameter = nullptr;
    static_cast<void>(::GetLeftString2(command, &parameter));
    if (parameter != nullptr) {
        const std::u16string_view targetText(parameter);
        if (targetText.size() >= 0x32) {
            targetName[0] = u'\0';
            runtime::process::HandleInvalidParameter(L"wcscpy_s");
        } else {
            std::copy_n(parameter, targetText.size() + 1, targetName);
        }
        ::ToLowerCase(targetName, lowerTargetName);
        const std::u16string lowerName(
            lowerTargetName, lowerTargetName + std::char_traits<SharedUtilsWideChar>::length(lowerTargetName)
        );
        User* const target = GetUserFromNameMap(g_nWorldId, lowerName).get();

        if (target != nullptr) {
            static_cast<void>(CDuelManager::inst().StartDuel(user, target, DT_PARTY));
            return false;
        }
    }

    static_cast<CUserSocket*>(socket)->SendSystemMessage(u"SYS", u"//duel party start [char name]");
    return false;
}

void CDuel::ClearSummonStatus(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    creature->DoNothing();
    creature->ChangeTarget(nullptr, CTR_DEFAULT);
    creature->DoNothing();

    User* owner = nullptr;
    bool clearSummons = false;
    bool clearSummonedNpcs = false;

    if (creature->IsPC()) {
        owner = creature->CastUser();
        clearSummons = owner->HaveSummon() || owner->HavePet();
        clearSummonedNpcs = owner->HaveSummonedNpc();
    }
    if (creature->IsSummonOrPet()) {
        owner = creature->CastSummon()->GetMaster();
        clearSummons = true;
    }
    if (creature->IsNPC()) {
        if (creature->CastNPC()->IsSummonedNpc()) {
            owner = creature->CastNPC()->GetSummoner();
            clearSummonedNpcs = true;
        }
    }

    if (owner == nullptr) {
        return;
    }

    if (clearSummons) {
        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(owner, summon, g_npcserver.Send("cdd", 0x37, summon->m_nId, 2););
    }

    if (clearSummonedNpcs) {
        std::vector<CNPC*> summonedNpcs;
        owner->GetAllSummonedNpc(summonedNpcs);
        for (CNPC* npc : summonedNpcs) {
            if (npc != nullptr) {
                npc->Despawn(true, false);
            }
        }
    }
}

void CDuelPlayerInfo::SetOrgInfectedSkills(std::vector<InfectedSkillType>& infectedSkills, CYieldLock& lock) {
    L2_GUARD_FUNCTION();

    L2_GUARD(lock);
    m_vOrgInfectedSkills.assign(infectedSkills.begin(), infectedSkills.end());
}

void CDuelPlayerInfo::GetOrgInfectedSkills(CCreature* creature) {
    // Current L2Server 0x006ed9c8. The duel-end dispel keeps skills
    // marked except-restore, then the saved, eligible infection records are
    // republished together with their abnormal slots.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    creature->DispelAllByGM(SPET_DUEL_END, false);
    creature->m_nSongDanceCount = 0;
    {
        L2_GUARD(creature->m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : m_vOrgInfectedSkills) {
            if (infected.IsMentorMenteeSkill() || infected.IsExcludeInfectSkill()) {
                continue;
            }
            creature->m_InfectedSkills.push_back(infected);
            if (infected.pSkillInfo == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     "InfectedSkillType's pSkillInfo should not be NULL")
                // L2_LOG_ERROR(Log,
                //     "InfectedSkillType's pSkillInfo should not be NULL")
                L2_LOG_ERROR(
                    Log,
                    "Cannot restore a duel skill effect for creature object %u: skill info is null; copied skill entry "
                    "retained.",
                    static_cast<unsigned>(creature->m_nId)
                );
            } else {
                creature->_AddAbnormalStatus(*infected.pSkillInfo, nullptr, true, true, SPST_DUEL);
            }
        }
    }
    if (creature->IsPCOrSummon()) {
        creature->_CheckBuffLock();
    }
    m_vOrgInfectedSkills.clear();

    if (creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user != nullptr) {
            user->m_bAbnormalStatusChanged = true;
            if (user->IsAlreadyHaveMentor()) {
                runtime::raw_atomic::ExchangeI32(&user->m_Mentee.m_nInfectedSkillByMentor, 0);
            } else if (user->IsHaveMentee()) {
                user->m_Mentor.m_nInfectedSkillByMentee = 0;
            }
        }
    }
    if (!creature->IsPC()) {
        creature->SetSpelledInfoChanged();
    } else {
        User* const user = creature->CastUser();
        user->m_bSpelledInfoChanged = true;
        user->m_NeedUpdatePacketBits |= 4U;
    }
    creature->CheckAbnormalVisualEffect();
    creature->ValidateChanged(VB_MAX);
}

void CIndividualDuelPlayerOperation::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        OperateInternal(user, playerInfo->CastIndividualDuelPlayerInfo());
    }
}

void CPartyDuelPlayerOperation::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        OperateInternal(user, playerInfo->CastPartyDuelPlayerInfo());
    }
}

void Duel_SetDuelInfo::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->m_DuelUserInfo.SetDuelInfo(m_nDuelSid, m_eDuelTeam, playerInfo);
        user->ParamChanged(PCB_CHAR_DUEL);
    }
}

void Duel_SetParamChanged::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->ParamChanged(PCB_CHAR_DUEL);

        FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(user, summon, static_cast<CCreature*>(summon)->ParamChanged(););
    }
}

void Duel_SetRelationChanged::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
    }
}

void Duel_ResetDuelInfo::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->m_DuelUserInfo.ResetDuelInfo();
        user->ParamChanged(PCB_CHAR_DUEL);
    }
}

void Duel_Send::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->GetSocket()->SendV(m_sFormat, m_arguments);
    }
}

void Duel_PlayMusic::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->PlayMusic(WideToUtf16(m_sMusicName).c_str());
    }
}

void Duel_SocialAction::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (user != nullptr && playerInfo != nullptr) {
        user->SocialAction(m_nSocialActionType, 0x800, false);
    }
}

void Duel_StopAction::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006e9d6c.
    L2_GUARD_FUNCTION();
    if (user != nullptr && playerInfo != nullptr) {
        CDuel::ClearSummonStatus(user);
    }
}

void Duel_SetFullStatus::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006e9e18.
    L2_GUARD_FUNCTION();
    if (user == nullptr || playerInfo == nullptr) {
        return;
    }
    if (user->IsAlive()) {
        {
            L2_GUARD(*user->m_lock);
            user->m_data->SetHP(user->m_data->m_dBaseHP);
            user->m_data->SetMP(user->m_data->m_dBaseMP);
            user->m_data->SetCP(user->m_data->m_dBaseCP);
        }
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        user, summon, if (!summon->IsAlive()) { continue; } {
            L2_GUARD(*summon->m_lock);
            summon->m_data->SetHP(summon->m_data->m_dBaseHP);
            summon->m_data->SetMP(summon->m_data->m_dBaseMP);
            summon->m_data->SetCP(summon->m_data->m_dBaseCP);
        }
    );
}

void Duel_RestoreStatus::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006ea178.
    L2_GUARD_FUNCTION();
    if (user == nullptr || playerInfo == nullptr) {
        return;
    }
    double hp = 0.0;
    double mp = 0.0;
    double cp = 0.0;
    if (user->IsAlive()) {
        playerInfo->GetOriginalStatus(hp, mp, cp);
        {
            L2_GUARD(*user->m_lock);
            user->m_data->SetHP(hp);
            user->m_data->SetMP(mp);
            user->m_data->SetCP(cp);
        }
    }

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        user, summon,
        if (!summon->IsAlive()) { continue; }

        {
            L2_GUARD(playerInfo->m_SummonLock);
            for (CDuelPlayerInfo* const summonInfo : playerInfo->m_vSummonInfo) {
                if (summonInfo != nullptr && summonInfo->GetSID() == static_cast<int>(summon->m_nId)) {
                    summonInfo->GetOriginalStatus(hp, mp, cp);
                    break;
                }
            }
        }

        {
            L2_GUARD(*summon->m_lock);
            summon->m_data->SetHP(hp);
            summon->m_data->SetMP(mp);
        }
    );
}

void Duel_RestoreInfectedSkills::Operate(User* user, CDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006edc60.
    L2_GUARD_FUNCTION();
    if (user == nullptr || playerInfo == nullptr) {
        return;
    }
    if (user->IsAlive()) {
        playerInfo->GetOrgInfectedSkills(user);
    }
    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        user, summon,
        if (!summon->IsAlive()) { continue; }

        {
            L2_GUARD(playerInfo->m_SummonLock);
            for (CDuelPlayerInfo* const summonInfo : playerInfo->m_vSummonInfo) {
                if (summonInfo != nullptr && summonInfo->GetSID() == static_cast<int>(summon->m_nId)) {
                    summonInfo->GetOrgInfectedSkills(summon);
                    break;
                }
            }
        }
    );
}

void Duel_BroadcastSummonHPMPInfo::Operate(User* receiver, User* subject, CDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    // Current L2Server 0x006ea4c0.
    L2_GUARD_FUNCTION();
    // Recovered source added a defensive boundary absent at 0x006ea4c0:
    // if (receiver == nullptr || subject == nullptr) { return. }
    FOR_ALL_SUMMON_IN_SUMMON_LIST(
        subject, summon,
        receiver->GetSocket()->Send(
            "cddcccdcdcdcd", 0x18, summon->m_nId, 0, 0, 4, 9, static_cast<int>(summon->m_data->GetHP()), 10,
            static_cast<int>(summon->m_data->m_dBaseHP), 0x0b, static_cast<int>(summon->m_data->GetMP()), 0x0c,
            static_cast<int>(summon->m_data->m_dBaseMP)
        );
    );
}

void Duel_BroadcastHideSummonHPMPInfo::Operate(User* receiver, User* subject, CDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    // Current L2Server 0x006ea760.
    L2_GUARD_FUNCTION();
    // Recovered source added a defensive boundary absent at 0x006ea760:
    // if (receiver == nullptr || subject == nullptr) { return. }
    FOR_ALL_SUMMON_IN_SUMMON_LIST(
        subject, summon,
        // Recovered source combined the null and party conditions. Current
        // creates "in_code" before evaluating the party relationship.
        if (subject->m_nPartySid == 0 || receiver->m_nPartySid != subject->m_nPartySid) {
            receiver->GetSocket()->Send("cddcccdcd", 0x18, summon->m_nId, 0, 0, 2, 10, 0, 0x0c, 0);
        }
    );
}

void IndividualDuel_Draw::OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.DrawIndividual();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void IndividualDuel_Win::OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.WinIndividual();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void IndividualDuel_Lose::OperateInternal(User* user, CIndividualDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.LoseIndividual();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void PartyDuel_SetAlive::OperateInternal(User* user [[maybe_unused]], CPartyDuelPlayerInfo* playerInfo) {
    L2_GUARD_FUNCTION();

    if (playerInfo != nullptr) {
        playerInfo->SetAlive();
    }
}

void PartyDuel_EnterDuelZone::OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006ea9c8.
    L2_GUARD_FUNCTION();
    if (user == nullptr || playerInfo == nullptr || m_pInstantZone == nullptr) {
        return;
    }

    FVector returnPosition = user->GetPos();
    playerInfo->SetReturnPos(returnPosition);
    playerInfo->SetOriginalStatus(user->m_data->GetHP(), user->m_data->GetMP(), user->m_data->GetCP());

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        user, summon,
        if (!summon->IsAlive()) { continue; }

        {
            L2_GUARD(playerInfo->m_SummonLock);
            for (CDuelPlayerInfo* const summonInfo : playerInfo->m_vSummonInfo) {
                if (summonInfo != nullptr && summonInfo->GetSID() == static_cast<int>(summon->m_nId)) {
                    summonInfo->SetOriginalStatus(summon->m_data->GetHP(), summon->m_data->GetMP(), 0.0);
                    break;
                }
            }
        }
    );

    g_InstantZoneMgr.EnterInstantZone(
        user, m_pInstantZone, IZET_PARTY, playerInfo->GetEnterPos(), false, 0, TeleportSourceType::InstantAgit
    );
}

void PartyDuel_LeaveDuelZone::OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo) {
    // Current L2Server 0x006e6da8.
    L2_GUARD_FUNCTION();
    if (user != nullptr && playerInfo != nullptr && m_pDuelZone != nullptr) {
        m_pDuelZone->LeaveZone(user, false, 0, 0, 0, 0);
    }
}

void PartyDuel_Draw::OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.DrawParty();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void PartyDuel_Win::OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.WinParty();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void PartyDuel_Lose::OperateInternal(User* user, CPartyDuelPlayerInfo* playerInfo [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    if (user != nullptr) {
        user->m_DuelUserInfo.LoseParty();
        user->m_DuelUserInfo.SaveDuelRecord(user);
    }
}

void CPartyDuelPlayerInfo::SetEnterPos(const int enterPos) {
    L2_GUARD_FUNCTION();

    m_nEnterPos = enterPos;
}

void CPartyDuelPlayerInfo::SetReturnPos(FVector& returnPos) {
    L2_GUARD_FUNCTION();

    if (m_fvReturnPos.IsZero()) {
        m_fvReturnPos = returnPos;
    }
}

CDuelManager::CDuelManager() : m_pDuelManagement{new CIndividualDuelManagement(), new CPartyDuelManagement()} {
    // Current L2Server.exe 0x006ebae0 constructs both complete strategy
    // owners eagerly in this order.
    L2_GUARD_FUNCTION();
}

CDuelManager::~CDuelManager() {
    // Current L2Server.exe 0x006e8678 deletes both polymorphic owners in
    // ascending DuelType order.
    L2_GUARD_FUNCTION();
    for (CDuelManagementUnit*& management : m_pDuelManagement) {
        delete management;
        management = nullptr;
    }
}

void CDuelManagementUnit::HandleDuelStartConditionResult(User* requestor, DuelStartErrorType error, User* target) {
    // Current L2Server.exe 0x006e75e8.
    L2_GUARD_FUNCTION();
    if (requestor == nullptr) {
        return;
    }
    if (error == DSET_ETC_ERROR) {
        requestor->GetSocket()->SendSystemMessage(DSET_ETC_ERROR);
        return;
    }
    if (target != nullptr) {
        requestor->GetSocket()->Send(
            "chccS", 0x62, static_cast<int>(static_cast<std::int16_t>(error)), 1, 0x0c, target->GetName()
        );
    }
}

bool CIndividualDuelManagement::CheckDuelStartCondition(User* requestor, User* target, bool sendMessage) {
    // Current L2Server.exe 0x006e7710.
    L2_GUARD_FUNCTION();
    if (requestor == nullptr || target == nullptr || requestor->GetDBID() == target->GetDBID()) {
        return false;
    }

    DuelStartErrorType error = requestor->CheckDuelStartCondition(false);
    if (error != DSET_SUCCESS) {
        HandleDuelStartConditionResult(requestor, error, requestor);
        if (sendMessage) {
            HandleDuelStartConditionResult(target, error, requestor);
        }
        return false;
    }

    error = target->CheckDuelStartCondition(false);
    if (error != DSET_SUCCESS) {
        HandleDuelStartConditionResult(requestor, error, target);
        if (sendMessage) {
            HandleDuelStartConditionResult(target, error, target);
        }
        return false;
    }

    CPledge* const requestorPledge = requestor->GetPledge();
    CPledge* const targetPledge = target->GetPledge();
    if (requestorPledge == nullptr || targetPledge == nullptr ||
        (!requestorPledge->DidDeclareWar(static_cast<int>(targetPledge->Id())) &&
            !targetPledge->DidDeclareWar(static_cast<int>(requestorPledge->Id())))) {
        return true;
    }

    HandleDuelStartConditionResult(requestor, DSET_ETC_ERROR, nullptr);
    if (sendMessage) {
        HandleDuelStartConditionResult(target, DSET_ETC_ERROR, nullptr);
    }
    return false;
}

bool CPartyDuelManagement::CheckDuelStartCondition(User* requestor, User* target, bool sendMessage) {
    // Current L2Server.exe 0x006eacd8.
    L2_GUARD_FUNCTION();
    if (requestor == nullptr || target == nullptr) {
        return false;
    }

    CParty* const requestorParty = requestor->GetParty();
    CParty* const targetParty = target->GetParty();
    auto fail = [&](DuelStartErrorType error, User* offender) {
        HandleDuelStartConditionResult(requestor, error, offender);
        if (sendMessage) {
            HandleDuelStartConditionResult(target, error, offender);
        }
        return false;
    };

    if (requestorParty == nullptr || targetParty == nullptr || requestorParty->GetMaster() == nullptr ||
        targetParty->GetMaster() == nullptr || requestor->Id() != requestorParty->m_data->m_OnlineMaster.id ||
        requestorParty->Id() == targetParty->Id()) {
        return fail(DSET_ETC_ERROR, nullptr);
    }

    std::vector<User*> members;
    requestorParty->GetAllMember(members);
    for (User* member : members) {
        if (member == nullptr) {
            return fail(DSET_ETC_ERROR, nullptr);
        }
        const DuelStartErrorType error = member->CheckDuelStartCondition(true);
        if (error != DSET_SUCCESS) {
            return fail(error, member);
        }
    }

    members.clear();
    targetParty->GetAllMember(members);
    for (User* member : members) {
        if (member == nullptr) {
            return fail(DSET_ETC_ERROR, nullptr);
        }
        const DuelStartErrorType error = member->CheckDuelStartCondition(true);
        if (error != DSET_SUCCESS) {
            return fail(error, member);
        }
    }
    return true;
}

void CIndividualDuelManagement::StartDuel(User* requestor, User* target) {
    // Current L2Server.exe 0x006ef0c0.
    L2_GUARD_FUNCTION();
    CIndividualDuel* const duel = new CIndividualDuel(requestor, target);
    {
        L2_GUARD(m_DuelMapLock);
        m_DuelMap[static_cast<int>(duel->Id())] = duel;
    }
    duel->Start();
    duel->StartTimer();
}

void CPartyDuelManagement::StartDuel(User* requestor, User* target) {
    // Current L2Server.exe 0x006eed3c.
    L2_GUARD_FUNCTION();
    CPartyDuel* const duel = new CPartyDuel(requestor->GetParty(), target->GetParty());
    {
        L2_GUARD(m_DuelMapLock);
        m_DuelMap[static_cast<int>(duel->Id())] = duel;
    }
    duel->Start();
    duel->StartTimer();
}

DuelTeamType CDuelManager::GetOpponentTeam(DuelTeamType team) {
    L2_GUARD_FUNCTION();

    if (team == DTT_TEAM_A) {
        return DTT_TEAM_B;
    }
    if (team == DTT_TEAM_B) {
        return DTT_TEAM_A;
    }
    return DTT_TEAM_MAX;
}

bool CDuelManager::CheckDuelStartCondition(User* requestor, User* target, DuelType duelType) {
    L2_GUARD_FUNCTION();

    if (duelType < DT_MAX) {
        return m_pDuelManagement[duelType]->CheckDuelStartCondition(requestor, target, false);
    }
    return false;
}

bool CDuelManager::StartDuel(User* requestor, User* target, DuelType duelType) {
    L2_GUARD_FUNCTION();

    if (duelType < DT_MAX && m_pDuelManagement[duelType]->CheckDuelStartCondition(requestor, target, true)) {
        m_pDuelManagement[duelType]->StartDuel(requestor, target);
        return true;
    }
    return false;
}

void CDuelManager::EndDuel(CDuel* duel) {
    L2_GUARD_FUNCTION();

    if (duel != nullptr && duel->GetDuelType() < DT_MAX) {
        m_pDuelManagement[duel->GetDuelType()]->EndDuel(duel);
        duel->Delete();
    }
}

void CDuelManager::CancelDuel(User* user, int reason) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = user->m_DuelUserInfo.GetDuel();
    if (duel != nullptr) {
        duel->Cancel(user, reason);
    }
}

void CDuelManager::DuelPlayerDie(User* user, CCreature* attacker) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = user->m_DuelUserInfo.GetDuel();
    if (duel != nullptr) {
        duel->PlayerDie(user, attacker);
    }
}

void CDuelManager::DuelPlayerLeaveWorld(User* user) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = user->m_DuelUserInfo.GetDuel();
    if (duel != nullptr) {
        duel->PlayerLeaveWorld(user);
    }
}

void CDuelManager::UpdateDuelPlayerStatus(User* user) {
    L2_GUARD_FUNCTION();

    CDuel* const duel = user->m_DuelUserInfo.GetDuel();
    if (duel != nullptr) {
        duel->PlayerStatusUpdate(user);
    }
}

L2_DEFINE_OBJECT_CLASS_ID(CDuel)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CDuel)

L2_DEFINE_OBJECT_ALLOCATOR(CDuel, __GetClassName())

L2_DEFINE_OBJECT_NEW(CDuel)

// Current L2Server 0x006e5888.
L2_DEFINE_OBJECT_TYPED_RELEASE(CDuel)

CDuel::CDuel() : m_nStatus(0), m_tDuelTime(0), m_bEndDuel(false), m_eWinnerTeam(DTT_TEAM_MAX) {
    // Current L2Server 0x006e5ad0.
    CGeneralObject::OnCreate();
}

CDuel::CDuel(const CDuel& other) :
    CGeneralObject(other),
    m_nStatus(other.m_nStatus),
    m_tDuelTime(other.m_tDuelTime),
    m_bEndDuel(other.m_bEndDuel),
    gap_0059{},
    m_eWinnerTeam(other.m_eWinnerTeam) {}

CDuel::~CDuel() = default;

void CDuel::End() { m_bEndDuel = true; }

void CDuel::SendDuelResult() { OnSendDuelResult(); }

void CDuel::SetDuelStatus(int status) { m_nStatus = status; }

void CDuel::SetWinner(DuelTeamType team) { m_eWinnerTeam = team; }

DuelTeamType CDuel::GetWinner() const { return m_eWinnerTeam; }

L2_DEFINE_OBJECT_CLASS_ID(CIndividualDuel)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CIndividualDuel)

// Current L2Server 0x006e59c8.
L2_DEFINE_OBJECT_ALLOCATOR(CIndividualDuel, __GetClassName())

L2_DEFINE_OBJECT_NEW(CIndividualDuel)

// Current L2Server 0x006eef34.
L2_DEFINE_OBJECT_TYPED_RELEASE(CIndividualDuel)

CIndividualDuel::CIndividualDuel(User* first, User* second) :
    m_nPlayerId{0, 0}, m_pPlayerInfo{nullptr, nullptr}, m_eEndMode(6), gap_007c{} {
    // Current L2Server 0x006eeea0.
    InitUser(first, DTT_TEAM_A);
    InitUser(second, DTT_TEAM_B);
}

void CIndividualDuel::InitUser(User* user, DuelTeamType team) {
    // Current L2Server 0x006edff0.
    if (user == nullptr) {
        return;
    }
    m_nPlayerId[team] = static_cast<int>(user->m_nId);
    CIndividualDuelPlayerInfo* const playerInfo = new (std::nothrow) CIndividualDuelPlayerInfo();
    m_pPlayerInfo[team] = playerInfo;
    if (playerInfo == nullptr) {
        return;
    }
    playerInfo->SetOriginalStatus(user->m_data->GetHP(), user->m_data->GetMP(), user->m_data->GetCP());
    playerInfo->SetOrgInfectedSkills(user->m_InfectedSkills, user->m_InfectedSkillsSect);

    FOR_ALL_SUMMON_PET_IN_SUMMON_LIST(
        user, summon, CDuelPlayerInfo* const summonInfo = new (std::nothrow) CDuelPlayerInfo(); if (summonInfo != nullptr) {
            summonInfo->SetSID(static_cast<int>(summon->m_nId));
            summonInfo->SetOriginalStatus(summon->m_data->GetHP(), summon->m_data->GetMP(), summon->m_data->GetCP());
            summonInfo->SetOrgInfectedSkills(summon->m_InfectedSkills, summon->m_InfectedSkillsSect);
            playerInfo->m_vSummonInfo.push_back(summonInfo);
        }
    );
}

CIndividualDuel::~CIndividualDuel() = default;

L2_DEFINE_OBJECT_CLASS_ID(CPartyDuel)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CPartyDuel)

// Current L2Server 0x006e5a4c.
L2_DEFINE_OBJECT_ALLOCATOR(CPartyDuel, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPartyDuel)

// Current L2Server 0x006eeb6c.
L2_DEFINE_OBJECT_TYPED_RELEASE(CPartyDuel)

CPartyDuel::CPartyDuel(CParty* first, CParty* second) :
    m_mPlayerInfoMap{}, m_eEndMode(6), m_nDuelZoneId(0) {
    // Current L2Server 0x006ee284. Each party is detached from its MPCC
    // and matching room before its member state is captured.
    const auto initializeParty = [this](CParty* party, DuelTeamType team, int firstEnterPos) {
        if (party == nullptr || team >= DTT_TEAM_MAX) {
            return;
        }

        User* const master = party->GetMaster();
        CSPointer<User> masterPointer(master, ObjectRefType::FunctionChain);
        CMultiPartyCommandChannelManager channelManager;
        if (CMultiPartyCommandChannelRegistry::IsChannelOwner(party)) {
            channelManager.Close(masterPointer);
        } else if (CMultiPartyCommandChannelRegistry::IsChannelMember(party)) {
            channelManager.Withdraw(masterPointer);
        }
        CPartyMatchingRequest::inst().DismissRoomOnly(masterPointer);

        std::vector<User*> members;
        party->GetAllMember(members);
        L2_GUARD(m_PlayerInfoMapLock);
        int enterPos = firstEnterPos;
        for (User* const user : members) {
            if (user == nullptr) {
                ++enterPos;
                continue;
            }

            CPartyDuelPlayerInfo* const playerInfo = new CPartyDuelPlayerInfo();
            playerInfo->SetEnterPos(enterPos);
            playerInfo->SetOrgInfectedSkills(user->m_InfectedSkills, user->m_InfectedSkillsSect);
            m_mPlayerInfoMap[team].emplace(user->m_nId, playerInfo);

            user->DespawnPet(true, false, DESPAWN_PET_NONE, false);
            {
                FOR_ALL_SUMMON_IN_SUMMON_LIST(
                    user, summon, CDuelPlayerInfo* const summonInfo = new CDuelPlayerInfo();
                    summonInfo->SetOrgInfectedSkills(summon->m_InfectedSkills, summon->m_InfectedSkillsSect);
                    playerInfo->m_vSummonInfo.push_back(summonInfo);
                );
            }
            ++enterPos;
        }
    };

    initializeParty(first, DTT_TEAM_A, 0);
    initializeParty(second, DTT_TEAM_B, 7);
}

CPartyDuel::~CPartyDuel() = default;

void CDuel::Cancel(User* user, int reason) {
    L2_GUARD_FUNCTION();

    OnCancel(user, reason);
}

void CDuel::PlayerDie(User* user, CCreature* attacker) {
    L2_GUARD_FUNCTION();

    OnPlayerDie(user, attacker);
}

void CDuel::PlayerLeaveWorld(User* user) {
    L2_GUARD_FUNCTION();

    OnPlayerLeaveWorld(user);
}

void CDuel::PlayerStatusUpdate(User* user) {
    L2_GUARD_FUNCTION();

    OnPlayerStatusUpdate(user);
}

void CDuel::StartTimer() {
    L2_GUARD_FUNCTION();

    m_tDuelTime = runtime::GetTickCount();
    AddTimer(1000, 0);
}

void CDuel::TimerExpired(int timerId [[maybe_unused]]) {
    L2_GUARD_FUNCTION();

    static constexpr std::uint32_t tCountDownTable[] = {
        1000,
        2000,
        3000,
    };

    const std::uint32_t currentTick = runtime::GetTickCount();
    if (m_nStatus == 1) {
        if (m_tDuelTime + GetWaitingTime() < currentTick) {
            m_tDuelTime = currentTick;
            m_nStatus = 2;
            OnStart();
        } else if (m_tDuelTime + GetWaitingTime() - 3000U < currentTick) {
            const std::uint32_t remainingTime = m_tDuelTime - currentTick + GetWaitingTime();
            for (const std::uint32_t countDown : tCountDownTable) {
                if (remainingTime <= countDown) {
                    SendToAllPlayers("chcch", 0x62, 0x799, 1, 0x15, static_cast<int>(remainingTime) / 1000 + 1);
                    break;
                }
            }
        }
    } else if (m_nStatus == 2) {
        if (m_tDuelTime + GetFightTime() < currentTick || m_bEndDuel) {
            m_tDuelTime = currentTick;
            m_nStatus = 3;
            OnPreEnd();
        }
    } else if (m_nStatus == 3) {
        if (m_tDuelTime + GetPreEndTime() < currentTick) {
            m_tDuelTime = currentTick;
            m_nStatus = 4;
            OnEnd();
        }
    } else if (m_nStatus == 4 && m_tDuelTime + GetEndTime() < currentTick) {
        m_tDuelTime = currentTick;
        m_nStatus = 0;
        OnCloseUp();
    }

    AddTimer(1000, 0);
}

void CDuel::Start() {
    L2_GUARD_FUNCTION();

    OnReady();
    StartTimer();
}

void CDuel::OnPlayerStatusUpdate(User* user) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"duel.cpp",
        //     0x117)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"duel.cpp",
        //     0x117)
        L2_LOG_ERROR(Log, "Cannot broadcast duel player status: player is missing.");
        return;
    }

    const char16_t* const name = user->GetName();
    const std::uint32_t objectId = user->m_nId;
    CSharedCreatureData* const data = user->m_data;
    const int classId = static_cast<int>(data->m_nClass);
    const int level = data->m_nLevel;
    const double hp = data->m_dHP;
    const double mp = data->m_dMP;
    const double cp = data->m_dCP;
    const double maximumHp = data->m_dBaseHP;
    const double maximumMp = data->m_dBaseMP;
    const double maximumCp = data->m_dBaseCP;
    const DuelTeamType team = user->m_DuelUserInfo.GetTeam();
    if (team < DTT_TEAM_MAX) {
        SendToTeamPlayers(
            CDuelManager::GetOpponentTeam(team), "chSddddddddd", 0xfe, 0x51, name, objectId, classId, level,
            static_cast<int>(hp), static_cast<int>(maximumHp), static_cast<int>(mp), static_cast<int>(maximumMp),
            static_cast<int>(cp), static_cast<int>(maximumCp)
        );
    }
}

void CIndividualDuel::SendToAllPlayers(const char* format, ...) {
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    Duel_Send operation(format, arguments);
    ForAllPlayer(operation);
    va_end(arguments);
}

void CIndividualDuel::SendToTeamPlayers(DuelTeamType team, const char* format, ...) {
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    Duel_Send operation(format, arguments);
    ForTeamPlayer(team, operation);
    va_end(arguments);
}

void CPartyDuel::SendToTeamPlayers(DuelTeamType team, const char* format, ...) {
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    Duel_Send operation(format, arguments);
    ForTeamPlayer(team, operation);
    va_end(arguments);
}

void CPartyDuel::SendToAllPlayers(const char* format, ...) {
    L2_GUARD_FUNCTION();

    std::va_list arguments;
    va_start(arguments, format);
    Duel_Send operation(format, arguments);
    ForAllPlayer(operation);
    va_end(arguments);
}

DuelTeamType CDuel::GetLoser() {
    L2_GUARD_FUNCTION();

    return CDuelManager::GetOpponentTeam(m_eWinnerTeam);
}

void CIndividualDuelManagement::EndDuel(CDuel* duel) {
    L2_GUARD_FUNCTION();

    if (duel != nullptr && duel->IsIndividualDuel()) {
        L2_GUARD(m_DuelMapLock);
        const DuelMapType::iterator found = m_DuelMap.find(static_cast<int>(duel->m_nId));
        if (found != m_DuelMap.end()) {
            m_DuelMap.erase(found);
        }
    }
}

void CPartyDuelManagement::EndDuel(CDuel* duel) {
    L2_GUARD_FUNCTION();

    if (duel != nullptr && duel->IsPartyDuel()) {
        L2_GUARD(m_DuelMapLock);
        const DuelMapType::iterator found = m_DuelMap.find(static_cast<int>(duel->m_nId));
        if (found != m_DuelMap.end()) {
            m_DuelMap.erase(found);
        }
    }
}

User* CIndividualDuel::GetPlayer(DuelTeamType team) {
    L2_GUARD_FUNCTION();

    if (team < DTT_TEAM_MAX) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[team]));
        User* user = nullptr;
        if (creature != nullptr && creature->IsPC()) {
            user = creature->CastUser();
        }
        if (user != nullptr && user->IsPC()) {
            return user;
        }
    }
    return nullptr;
}

void CIndividualDuel::ForAllPlayer(CDuelPlayerOperation& operation) {
    L2_GUARD_FUNCTION();

    for (int team = DTT_TEAM_A; team < DTT_TEAM_MAX; ++team) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[team]));
        User* user = nullptr;
        if (creature != nullptr && creature->IsPC()) {
            user = creature->CastUser();
        }
        if (user != nullptr && m_pPlayerInfo[team] != nullptr) {
            operation.Operate(user, m_pPlayerInfo[team]);
        }
    }
}

void CIndividualDuel::ForTeamPlayer(DuelTeamType team, CDuelPlayerOperation& operation) {
    L2_GUARD_FUNCTION();

    if (team < DTT_TEAM_MAX) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[team]));
        User* user = nullptr;
        if (creature != nullptr && creature->IsPC()) {
            user = creature->CastUser();
        }
        if (user != nullptr) {
            operation.Operate(user, m_pPlayerInfo[team]);
        }
    }
}

void CIndividualDuel::BroadcastForAllPlayer(CDuelPlayerBroadcastOperation& operation) {
    L2_GUARD_FUNCTION();

    for (int subjectTeam = DTT_TEAM_A; subjectTeam < DTT_TEAM_MAX; ++subjectTeam) {
        CCreature* const subjectCreature =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[subjectTeam]));
        User* subject = nullptr;
        if (subjectCreature != nullptr && subjectCreature->IsPC()) {
            subject = subjectCreature->CastUser();
        }

        CDuelPlayerInfo* const playerInfo = m_pPlayerInfo[subjectTeam];
        if (subject == nullptr || playerInfo == nullptr) {
            continue;
        }

        for (int receiverTeam = DTT_TEAM_A; receiverTeam < DTT_TEAM_MAX; ++receiverTeam) {
            CCreature* const receiverCreature =
                g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[receiverTeam]));
            User* receiver = nullptr;
            if (receiverCreature != nullptr && receiverCreature->IsPC()) {
                receiver = receiverCreature->CastUser();
            }
            if (receiver != nullptr && subject->m_nId != receiver->m_nId) {
                operation.Operate(receiver, subject, playerInfo);
            }
        }
    }
}

void CIndividualDuel::OnReady() {
    L2_GUARD_FUNCTION();

    Duel_SetDuelInfo firstTeamInfo(static_cast<int>(m_nId), DTT_TEAM_A);
    ForTeamPlayer(DTT_TEAM_A, firstTeamInfo);

    Duel_SetDuelInfo secondTeamInfo(static_cast<int>(m_nId), DTT_TEAM_B);
    ForTeamPlayer(DTT_TEAM_B, secondTeamInfo);

    m_nStatus = 1;

    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);

    SendToAllPlayers("chd", 0xfe, 0x4e, GetDuelType());
}

void CIndividualDuel::OnStart() {
    // Current L2Server 0x006e7d04.
    L2_GUARD_FUNCTION();
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
    if (!g_bSendSummonHPMPInfo) {
        Duel_BroadcastSummonHPMPInfo broadcast;
        BroadcastForAllPlayer(broadcast);
    }
    SendToAllPlayers("chd", 0xfe, 0x4f, GetDuelType());
    SendToAllPlayers("chc", 0x62, 0x79d, 0);
    SendToAllPlayers("cdSdddddd", 0x9e, 1, u"B04_S01", 0, 0, 0, 0, 0, 0);
}

void CIndividualDuel::OnCancel(User* user, int reason) {
    // Current L2Server 0x006e601c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    if (m_nStatus == 2 && (reason & 1) != 0) {
        m_eWinnerTeam = user->m_DuelUserInfo.GetOpponentTeam();
    }
    Duel_StopAction stop;
    ForAllPlayer(stop);
    m_eEndMode = reason;
    m_bEndDuel = true;
}

void CIndividualDuel::OnPreEnd() {
    // Current L2Server 0x006e8078.
    L2_GUARD_FUNCTION();
    SendToAllPlayers("chd", 0xfe, 0x50, GetDuelType());
    Duel_StopAction stop;
    ForAllPlayer(stop);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
    Duel_SocialAction socialAction(7);
    ForTeamPlayer(GetLoser(), socialAction);
}

void CIndividualDuel::OnEnd() {
    // Current L2Server 0x006e81b8.
    L2_GUARD_FUNCTION();
    SaveDuelRecord();
    RestoreStatus();
    Duel_RestoreInfectedSkills restoreSkills;
    ForAllPlayer(restoreSkills);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
    if (!g_bSendSummonHPMPInfo) {
        Duel_BroadcastHideSummonHPMPInfo hide;
        BroadcastForAllPlayer(hide);
    }
}

void CIndividualDuel::OnCloseUp() {
    // Current L2Server 0x006ebdb8.
    L2_GUARD_FUNCTION();
    Duel_ResetDuelInfo reset;
    ForAllPlayer(reset);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    CDuelManager::inst().EndDuel(this);
}

void CIndividualDuel::OnSendDuelResult() {
    // Current folded no-op 0x006fe5d4.
}

void CIndividualDuel::OnPlayerDie(User* user, CCreature* attacker) {
    // Current L2Server 0x006e98bc.
    L2_GUARD_FUNCTION();
    if (user != nullptr && m_nStatus == 2) {
        m_eWinnerTeam = user->m_DuelUserInfo.GetOpponentTeam();
    }
    ClearSummonStatus(attacker);
    Duel_StopAction stop;
    ForAllPlayer(stop);
    m_eEndMode |= 8;
    m_bEndDuel = true;
}

void CIndividualDuel::OnPlayerLeaveWorld(User* user) {
    // Current L2Server 0x006e7ea0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const DuelTeamType team = user->m_DuelUserInfo.GetTeam();
    if (team >= DTT_TEAM_MAX) {
        return;
    }
    user->GetSocket()->Send("chd", 0xfe, 0x50, GetDuelType());
    Duel_RestoreStatus restoreStatus;
    ForTeamPlayer(team, restoreStatus);
    Duel_RestoreInfectedSkills restoreSkills;
    ForTeamPlayer(team, restoreSkills);
    Duel_ResetDuelInfo reset;
    ForTeamPlayer(team, reset);
    Duel_SetParamChanged paramChanged;
    ForTeamPlayer(team, paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForTeamPlayer(team, relationChanged);
    if (m_nStatus == 2) {
        m_eWinnerTeam = user->m_DuelUserInfo.GetOpponentTeam();
    }
    Duel_StopAction stop;
    ForAllPlayer(stop);
    m_bEndDuel = true;
}

void CIndividualDuel::RestoreStatus() {
    // Current L2Server 0x006e6294.
    L2_GUARD_FUNCTION();
    Duel_RestoreStatus restore;
    if ((m_eEndMode & 6) == 6) {
        ForAllPlayer(restore);
    } else if ((m_eEndMode & 2) != 0) {
        ForTeamPlayer(m_eWinnerTeam, restore);
    }
}

void CIndividualDuel::SaveDuelRecord() {
    // Current L2Server 0x006ebe8c.
    L2_GUARD_FUNCTION();
    if (m_eWinnerTeam == DTT_TEAM_MAX) {
        IndividualDuel_Draw draw;
        ForAllPlayer(draw);
        SendToAllPlayers("chc", 0x62, 0x7a0, 0);
        if (m_bEndDuel) {
            return;
        }

        CCreature* firstCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[DTT_TEAM_A]));
        User* first = nullptr;
        if (firstCreature != nullptr && firstCreature->IsPC()) {
            first = firstCreature->CastUser();
        }
        CCreature* secondCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[DTT_TEAM_B]));
        User* second = nullptr;
        if (secondCreature != nullptr && secondCreature->IsPC()) {
            second = secondCreature->CastUser();
        }
        if (first == nullptr || second == nullptr) {
            return;
        }

        const int actorZ = runtime::Win64CvttDoubleToInt32(first->GetPos().z);
        const int actorY = runtime::Win64CvttDoubleToInt32(first->GetPos().y);
        const int actorX = runtime::Win64CvttDoubleToInt32(first->GetPos().x);
        const char16_t* const targetName = second->GetName();
        const char16_t* const actorName = first->GetName();
        const std::uint32_t targetAccountId = second->GetAccountID();
        const std::uint32_t targetDbId = second->GetDBID();
        const std::uint32_t actorAccountId = first->GetAccountID();
        const std::uint32_t actorDbId = first->GetDBID();
        std::array<wchar_t, 16> actorAccountStorage{};
        std::array<wchar_t, 16> targetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     actorDbId,
        //     actorAccountId,
        //     targetDbId,
        //     targetAccountId,
        //     actorX,
        //     actorY,
        //     actorZ,
        //     0,
        //     1,
        //     actorName,
        //     DuelAuditAccountName(
        //         first->m_sAccountName,
        //         actorAccountStorage),
        //     targetName,
        //     DuelAuditAccountName(
        //         second->m_sAccountName,
        //         targetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, actorDbId, actorAccountId, targetDbId, targetAccountId, actorX, actorY, actorZ, 0, 1, actorName,
            DuelAuditAccountName(first->m_sAccountName, actorAccountStorage), targetName,
            DuelAuditAccountName(second->m_sAccountName, targetAccountStorage)
        );
        const int finalActorZ = runtime::Win64CvttDoubleToInt32(second->GetPos().z);
        const int finalActorY = runtime::Win64CvttDoubleToInt32(second->GetPos().y);
        const int finalActorX = runtime::Win64CvttDoubleToInt32(second->GetPos().x);
        const char16_t* const finalTargetName = first->GetName();
        const char16_t* const finalActorName = second->GetName();
        const std::uint32_t finalTargetAccountId = first->GetAccountID();
        const std::uint32_t finalTargetDbId = first->GetDBID();
        const std::uint32_t finalActorAccountId = second->GetAccountID();
        const std::uint32_t finalActorDbId = second->GetDBID();
        std::array<wchar_t, 16> finalActorAccountStorage{};
        std::array<wchar_t, 16> finalTargetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     finalActorDbId,
        //     finalActorAccountId,
        //     finalTargetDbId,
        //     finalTargetAccountId,
        //     finalActorX,
        //     finalActorY,
        //     finalActorZ,
        //     0,
        //     1,
        //     finalActorName,
        //     DuelAuditAccountName(
        //         second->m_sAccountName,
        //         finalActorAccountStorage),
        //     finalTargetName,
        //     DuelAuditAccountName(
        //         first->m_sAccountName,
        //         finalTargetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, finalActorDbId, finalActorAccountId, finalTargetDbId, finalTargetAccountId, finalActorX, finalActorY,
            finalActorZ, 0, 1, finalActorName, DuelAuditAccountName(second->m_sAccountName, finalActorAccountStorage),
            finalTargetName, DuelAuditAccountName(first->m_sAccountName, finalTargetAccountStorage)
        );
        return;
    } else {
        const DuelTeamType loser = Singleton<CDuelManager>::inst().GetOpponentTeam(m_eWinnerTeam);
        IndividualDuel_Win win;
        ForTeamPlayer(m_eWinnerTeam, win);
        IndividualDuel_Lose lose;
        ForTeamPlayer(loser, lose);
        if (m_eWinnerTeam >= DTT_TEAM_MAX || loser >= DTT_TEAM_MAX) {
            return;
        }

        CCreature* winnerCreature =
            g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[m_eWinnerTeam]));
        User* winner = nullptr;
        if (winnerCreature != nullptr && winnerCreature->IsPC()) {
            winner = winnerCreature->CastUser();
        }
        CCreature* loserCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nPlayerId[loser]));
        User* loserUser = nullptr;
        if (loserCreature != nullptr && loserCreature->IsPC()) {
            loserUser = loserCreature->CastUser();
        }
        if (winner != nullptr && winner->IsPC()) {
            SendToAllPlayers("chccS", 0x62, 0x79e, 1, 0x0c, winner->GetName());
        }
        if ((m_eEndMode & 8) == 0 || winner == nullptr || loserUser == nullptr) {
            return;
        }

        const int actorZ = runtime::Win64CvttDoubleToInt32(winner->GetPos().z);
        const int actorY = runtime::Win64CvttDoubleToInt32(winner->GetPos().y);
        const int actorX = runtime::Win64CvttDoubleToInt32(winner->GetPos().x);
        const char16_t* const targetName = loserUser->GetName();
        const char16_t* const actorName = winner->GetName();
        const std::uint32_t targetAccountId = loserUser->GetAccountID();
        const std::uint32_t targetDbId = loserUser->GetDBID();
        const std::uint32_t actorAccountId = winner->GetAccountID();
        const std::uint32_t actorDbId = winner->GetDBID();
        std::array<wchar_t, 16> actorAccountStorage{};
        std::array<wchar_t, 16> targetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     actorDbId,
        //     actorAccountId,
        //     targetDbId,
        //     targetAccountId,
        //     actorX,
        //     actorY,
        //     actorZ,
        //     0,
        //     0,
        //     actorName,
        //     DuelAuditAccountName(
        //         winner->m_sAccountName,
        //         actorAccountStorage),
        //     targetName,
        //     DuelAuditAccountName(
        //         loserUser->m_sAccountName,
        //         targetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, actorDbId, actorAccountId, targetDbId, targetAccountId, actorX, actorY, actorZ, 0, 0, actorName,
            DuelAuditAccountName(winner->m_sAccountName, actorAccountStorage), targetName,
            DuelAuditAccountName(loserUser->m_sAccountName, targetAccountStorage)
        );
        const int finalActorZ = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().z);
        const int finalActorY = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().y);
        const int finalActorX = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().x);
        const char16_t* const finalTargetName = winner->GetName();
        const char16_t* const finalActorName = loserUser->GetName();
        const std::uint32_t finalTargetAccountId = winner->GetAccountID();
        const std::uint32_t finalTargetDbId = winner->GetDBID();
        const std::uint32_t finalActorAccountId = loserUser->GetAccountID();
        const std::uint32_t finalActorDbId = loserUser->GetDBID();
        std::array<wchar_t, 16> finalActorAccountStorage{};
        std::array<wchar_t, 16> finalTargetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     finalActorDbId,
        //     finalActorAccountId,
        //     finalTargetDbId,
        //     finalTargetAccountId,
        //     finalActorX,
        //     finalActorY,
        //     finalActorZ,
        //     0,
        //     2,
        //     finalActorName,
        //     DuelAuditAccountName(
        //         loserUser->m_sAccountName,
        //         finalActorAccountStorage),
        //     finalTargetName,
        //     DuelAuditAccountName(
        //         winner->m_sAccountName,
        //         finalTargetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, finalActorDbId, finalActorAccountId, finalTargetDbId, finalTargetAccountId, finalActorX, finalActorY,
            finalActorZ, 0, 2, finalActorName, DuelAuditAccountName(loserUser->m_sAccountName, finalActorAccountStorage),
            finalTargetName, DuelAuditAccountName(winner->m_sAccountName, finalTargetAccountStorage)
        );
    }
}

void CIndividualDuel::SetEnemyRelationAll() {
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
}

void CPartyDuel::OnReady() {
    // Current L2Server 0x006ec5bc.
    L2_GUARD_FUNCTION();

    CreateFailReason reason = CFR_INTERNAL_ERROR;
    wchar_t zoneName[] = L"party_duel_zone";
    CInstantZone* const zone = g_InstantZoneMgr.CreateInstantZone(zoneName, reason);
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "fail to create instant zone[%s]",
        //     zoneName)
        // L2_LOG_ERROR(Log,
        //     "fail to create instant zone[%s]",
        //     zoneName)
        L2_LOG_ERROR(
            Log, "Cannot create party duel zone '%s': reason %d; ending duel %u.", WideToUtf8(zoneName).c_str(),
            static_cast<int>(reason), static_cast<unsigned>(m_nId)
        );
        CDuelManager::inst().EndDuel(this);
        return;
    }

    zone->InitNPC();
    Duel_SetDuelInfo firstTeamInfo(static_cast<int>(m_nId), DTT_TEAM_A);
    ForTeamPlayer(DTT_TEAM_A, firstTeamInfo);
    Duel_SetDuelInfo secondTeamInfo(static_cast<int>(m_nId), DTT_TEAM_B);
    ForTeamPlayer(DTT_TEAM_B, secondTeamInfo);
    m_nStatus = 1;

    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    m_nDuelZoneId = static_cast<int>(zone->Id());
    PartyDuel_EnterDuelZone enterZone(zone);
    ForAllPlayer(enterZone);
    SendToAllPlayers("chd", 0xfe, 0x4e, GetDuelType());
}

void CPartyDuel::OnStart() {
    // Current L2Server 0x006e82d4.
    L2_GUARD_FUNCTION();

    Duel_SetFullStatus fullStatus;
    ForAllPlayer(fullStatus);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
    if (!g_bSendSummonHPMPInfo) {
        Duel_BroadcastSummonHPMPInfo broadcast;
        BroadcastForAllPlayer(broadcast);
    }
    SendToAllPlayers("chd", 0xfe, 0x4f, GetDuelType());
    SendToAllPlayers("chc", 0x62, 0x79d, 0);
    Duel_PlayMusic playMusic(L"B03_S01");
    ForAllPlayer(playMusic);
}

void CPartyDuel::OnCancel(User* user [[maybe_unused]], int reason [[maybe_unused]]) {
    // Current folded no-op 0x006fe5d4.
}

void CPartyDuel::OnPreEnd() {
    // Current L2Server 0x006e845c.
    L2_GUARD_FUNCTION();

    SendToAllPlayers("chd", 0xfe, 0x50, GetDuelType());
    Duel_StopAction stop;
    ForAllPlayer(stop);
    Duel_SocialAction socialAction(7);
    ForTeamPlayer(GetLoser(), socialAction);
}

void CPartyDuel::OnEnd() {
    // Current L2Server 0x006e8564.
    L2_GUARD_FUNCTION();

    PartyDuel_SetAlive setAlive;
    ForAllPlayer(setAlive);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
    if (!g_bSendSummonHPMPInfo) {
        Duel_BroadcastHideSummonHPMPInfo hide;
        BroadcastForAllPlayer(hide);
    }
    SaveDuelRecord();
}

void CPartyDuel::OnCloseUp() {
    // Current L2Server 0x006ec768.
    L2_GUARD_FUNCTION();

    Duel_RestoreStatus restoreStatus;
    ForAllPlayer(restoreStatus);
    Duel_RestoreInfectedSkills restoreSkills;
    ForAllPlayer(restoreSkills);
    LeaveDuelZone();
    Duel_ResetDuelInfo reset;
    ForAllPlayer(reset);
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    CDuelManager::inst().EndDuel(this);
}

void CPartyDuel::OnSendDuelResult() {
    // Current folded no-op 0x006fe5d4.
}

void CPartyDuel::OnPlayerDie(User* user, CCreature* attacker) {
    // Current L2Server 0x006e9998.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const DuelTeamType team = user->m_DuelUserInfo.GetTeam();
    if (static_cast<unsigned int>(team) >= DTT_TEAM_MAX) {
        return;
    }
    {
        L2_GUARD(m_PlayerInfoMapLock);
        const PlayerInfoMap::iterator found = m_mPlayerInfoMap[team].find(user->m_nId);
        if (found != m_mPlayerInfoMap[team].end() && found->second != nullptr) {
            found->second->SetDead();
        }
    }

    ClearSummonStatus(attacker);
    if (IsTeamAllDead(team)) {
        if (m_nStatus == 2) {
            m_eWinnerTeam = user->m_DuelUserInfo.GetOpponentTeam();
        }
        Duel_StopAction stop;
        ForAllPlayer(stop);
        m_bEndDuel = true;
    }
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
}

void CPartyDuel::OnPlayerLeaveWorld(User* user) {
    // Current L2Server 0x006eb150.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const DuelTeamType team = user->m_DuelUserInfo.GetTeam();
    if (static_cast<unsigned int>(team) >= DTT_TEAM_MAX) {
        return;
    }
    user->GetSocket()->Send("chd", 0xfe, 0x50, GetDuelType());

    Duel_RestoreStatus restoreStatus;
    ForPlayer(user, team, restoreStatus);
    Duel_RestoreInfectedSkills restoreSkills;
    ForPlayer(user, team, restoreSkills);
    Duel_ResetDuelInfo reset;
    ForPlayer(user, team, reset);
    Duel_SetParamChanged playerParamChanged;
    ForPlayer(user, team, playerParamChanged);
    Duel_SetRelationChanged playerRelationChanged;
    ForPlayer(user, team, playerRelationChanged);
    {
        L2_GUARD(m_PlayerInfoMapLock);
        m_mPlayerInfoMap[team].erase(user->m_nId);
    }

    if (IsTeamAllDead(team)) {
        if (m_nStatus == 2) {
            m_eWinnerTeam = user->m_DuelUserInfo.GetOpponentTeam();
        }
        Duel_StopAction stop;
        ForAllPlayer(stop);
        m_bEndDuel = true;
    }
    Duel_SetParamChanged paramChanged;
    ForAllPlayer(paramChanged);
    Duel_SetRelationChanged relationChanged;
    ForAllPlayer(relationChanged);
}

bool CPartyDuel::IsTeamAllDead(DuelTeamType team) {
    // Current L2Server 0x006e8c98.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(team) >= DTT_TEAM_MAX) {
        return true;
    }

    L2_GUARD(m_PlayerInfoMapLock);
    const PlayerInfoMap& players = m_mPlayerInfoMap[team];
    if (players.empty()) {
        return true;
    }
    for (const auto& entry : players) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(entry.first);
        User* user = nullptr;
        if (creature != nullptr && creature->IsPC()) {
            user = creature->CastUser();
        }
        if (user != nullptr && user->IsAlive() && entry.second != nullptr && entry.second->IsAlive()) {
            return false;
        }
    }
    return true;
}

void CPartyDuel::LeaveDuelZone() {
    // Current L2Server 0x006e6508.
    L2_GUARD_FUNCTION();
    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(m_nDuelZoneId));
    CInstantZone* zone = nullptr;
    if (object != nullptr && object->IsInZone()) {
        zone = object->CastInZone();
    }
    if (zone == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CPartyDuel::LeaveDuelZone() failed. DuelZone doesn't exist")
        // L2_LOG_ERROR(Log,
        //     L"CPartyDuel::LeaveDuelZone() failed. DuelZone doesn't exist")
        L2_LOG_ERROR(
            Log, "Cannot leave party duel %u: zone object %u is unavailable; player exit and zone deletion skipped.",
            static_cast<unsigned>(m_nId), static_cast<unsigned>(m_nDuelZoneId)
        );
        return;
    }

    PartyDuel_LeaveDuelZone leaveZone(zone);
    ForAllPlayer(leaveZone);
    g_InstantZoneMgr.DeleteInstantZone(*zone, IZDI_DUEL_FINISHED);
}

void CPartyDuel::ForAllPlayer(CDuelPlayerOperation& operation) {
    // Current L2Server 0x006eb5f4.
    L2_GUARD_FUNCTION();
    using PlayerEntry = std::pair<User*, CPartyDuelPlayerInfo*>;
    std::vector<PlayerEntry> players;
    {
        L2_GUARD(m_PlayerInfoMapLock);
        for (int team = DTT_TEAM_A; team < DTT_TEAM_MAX; ++team) {
            for (const auto& entry : m_mPlayerInfoMap[team]) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(entry.first);
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                if (user != nullptr && entry.second != nullptr) {
                    players.push_back(PlayerEntry(user, entry.second));
                }
            }
        }
    }
    for (const PlayerEntry& entry : players) {
        operation.Operate(entry.first, entry.second);
    }
}

void CPartyDuel::ForTeamPlayer(DuelTeamType team, CDuelPlayerOperation& operation) {
    // Current L2Server 0x006eb7ec.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(team) >= DTT_TEAM_MAX) {
        return;
    }

    using PlayerEntry = std::pair<User*, CPartyDuelPlayerInfo*>;
    std::vector<PlayerEntry> players;
    {
        L2_GUARD(m_PlayerInfoMapLock);
        for (const auto& entry : m_mPlayerInfoMap[team]) {
            CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(entry.first);
            User* user = nullptr;
            if (creature != nullptr && creature->IsPC()) {
                user = creature->CastUser();
            }
            if (user != nullptr && entry.second != nullptr) {
                players.push_back(PlayerEntry(user, entry.second));
            }
        }
    }
    for (const PlayerEntry& entry : players) {
        operation.Operate(entry.first, entry.second);
    }
}

void CPartyDuel::ForPlayer(User* user, DuelTeamType team, CDuelPlayerOperation& operation) {
    // Current L2Server 0x006e9150.
    L2_GUARD_FUNCTION();
    if (user == nullptr || static_cast<unsigned int>(team) >= DTT_TEAM_MAX) {
        return;
    }

    CPartyDuelPlayerInfo* playerInfo = nullptr;
    {
        L2_GUARD(m_PlayerInfoMapLock);
        const PlayerInfoMap::iterator found = m_mPlayerInfoMap[team].find(user->m_nId);
        if (found != m_mPlayerInfoMap[team].end()) {
            playerInfo = found->second;
        }
    }
    if (playerInfo != nullptr) {
        operation.Operate(user, playerInfo);
    }
}

void CPartyDuel::BroadcastForAllPlayer(CDuelPlayerBroadcastOperation& operation) {
    // Current L2Server 0x006e8e80.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PlayerInfoMapLock);
    for (int subjectTeam = DTT_TEAM_A; subjectTeam < DTT_TEAM_MAX; ++subjectTeam) {
        for (const auto& subjectEntry : m_mPlayerInfoMap[subjectTeam]) {
            CCreature* const subjectCreature = g_CreatureSmartIdManager.GetObjectW(subjectEntry.first);
            User* subject = nullptr;
            if (subjectCreature != nullptr && subjectCreature->IsPC()) {
                subject = subjectCreature->CastUser();
            }
            if (subject == nullptr || subjectEntry.second == nullptr) {
                continue;
            }
            for (int receiverTeam = DTT_TEAM_A; receiverTeam < DTT_TEAM_MAX; ++receiverTeam) {
                for (const auto& receiverEntry : m_mPlayerInfoMap[receiverTeam]) {
                    CCreature* const receiverCreature = g_CreatureSmartIdManager.GetObjectW(receiverEntry.first);
                    User* receiver = nullptr;
                    if (receiverCreature != nullptr && receiverCreature->IsPC()) {
                        receiver = receiverCreature->CastUser();
                    }
                    if (receiver != nullptr && receiver->m_nId != subject->m_nId) {
                        operation.Operate(receiver, subject, subjectEntry.second);
                    }
                }
            }
        }
    }
}

void CPartyDuel::SaveDuelRecord() {
    // Current L2Server 0x006ec880.
    L2_GUARD_FUNCTION();
    if (m_eWinnerTeam == DTT_TEAM_MAX) {
        PartyDuel_Draw draw;
        ForAllPlayer(draw);
        SendToAllPlayers("chc", 0x62, 0x7a0, 0);

        User* first = nullptr;
        User* second = nullptr;
        {
            L2_GUARD(m_PlayerInfoMapLock);
            if (!m_mPlayerInfoMap[DTT_TEAM_A].empty()) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_mPlayerInfoMap[DTT_TEAM_A].begin()->first);
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                if (user != nullptr) {
                    CParty* const party = user->GetParty();
                    if (party != nullptr) {
                        first = party->GetMaster();
                    }
                }
            }
            if (!m_mPlayerInfoMap[DTT_TEAM_B].empty()) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_mPlayerInfoMap[DTT_TEAM_B].begin()->first);
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                if (user != nullptr) {
                    CParty* const party = user->GetParty();
                    if (party != nullptr) {
                        second = party->GetMaster();
                    }
                }
            }
        }
        if (first == nullptr || second == nullptr) {
            return;
        }

        const int actorZ = runtime::Win64CvttDoubleToInt32(first->GetPos().z);
        const int actorY = runtime::Win64CvttDoubleToInt32(first->GetPos().y);
        const int actorX = runtime::Win64CvttDoubleToInt32(first->GetPos().x);
        const char16_t* const targetName = second->GetName();
        const char16_t* const actorName = first->GetName();
        const std::uint32_t targetAccountId = second->GetAccountID();
        const std::uint32_t targetDbId = second->GetDBID();
        const std::uint32_t actorAccountId = first->GetAccountID();
        const std::uint32_t actorDbId = first->GetDBID();
        std::array<wchar_t, 16> actorAccountStorage{};
        std::array<wchar_t, 16> targetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     actorDbId,
        //     actorAccountId,
        //     targetDbId,
        //     targetAccountId,
        //     actorX,
        //     actorY,
        //     actorZ,
        //     1,
        //     1,
        //     actorName,
        //     DuelAuditAccountName(
        //         first->m_sAccountName,
        //         actorAccountStorage),
        //     targetName,
        //     DuelAuditAccountName(
        //         second->m_sAccountName,
        //         targetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, actorDbId, actorAccountId, targetDbId, targetAccountId, actorX, actorY, actorZ, 1, 1, actorName,
            DuelAuditAccountName(first->m_sAccountName, actorAccountStorage), targetName,
            DuelAuditAccountName(second->m_sAccountName, targetAccountStorage)
        );
        const int finalActorZ = runtime::Win64CvttDoubleToInt32(second->GetPos().z);
        const int finalActorY = runtime::Win64CvttDoubleToInt32(second->GetPos().y);
        const int finalActorX = runtime::Win64CvttDoubleToInt32(second->GetPos().x);
        const char16_t* const finalTargetName = first->GetName();
        const char16_t* const finalActorName = second->GetName();
        const std::uint32_t finalTargetAccountId = first->GetAccountID();
        const std::uint32_t finalTargetDbId = first->GetDBID();
        const std::uint32_t finalActorAccountId = second->GetAccountID();
        const std::uint32_t finalActorDbId = second->GetDBID();
        std::array<wchar_t, 16> finalActorAccountStorage{};
        std::array<wchar_t, 16> finalTargetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     finalActorDbId,
        //     finalActorAccountId,
        //     finalTargetDbId,
        //     finalTargetAccountId,
        //     finalActorX,
        //     finalActorY,
        //     finalActorZ,
        //     1,
        //     1,
        //     finalActorName,
        //     DuelAuditAccountName(
        //         second->m_sAccountName,
        //         finalActorAccountStorage),
        //     finalTargetName,
        //     DuelAuditAccountName(
        //         first->m_sAccountName,
        //         finalTargetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, finalActorDbId, finalActorAccountId, finalTargetDbId, finalTargetAccountId, finalActorX, finalActorY,
            finalActorZ, 1, 1, finalActorName, DuelAuditAccountName(second->m_sAccountName, finalActorAccountStorage),
            finalTargetName, DuelAuditAccountName(first->m_sAccountName, finalTargetAccountStorage)
        );
        return;
    } else {
        if (m_eWinnerTeam >= DTT_TEAM_MAX) {
            return;
        }
        const DuelTeamType loser = Singleton<CDuelManager>::inst().GetOpponentTeam(m_eWinnerTeam);
        PartyDuel_Win win;
        ForTeamPlayer(m_eWinnerTeam, win);
        PartyDuel_Lose lose;
        ForTeamPlayer(loser, lose);

        User* winner = nullptr;
        User* loserUser = nullptr;
        {
            L2_GUARD(m_PlayerInfoMapLock);
            if (!m_mPlayerInfoMap[m_eWinnerTeam].empty()) {
                CCreature* const creature =
                    g_CreatureSmartIdManager.GetObjectW(m_mPlayerInfoMap[m_eWinnerTeam].begin()->first);
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                if (user != nullptr) {
                    CParty* const party = user->GetParty();
                    if (party != nullptr) {
                        winner = party->GetMaster();
                    }
                    if (winner == nullptr) {
                        winner = user;
                    }
                }
            }
            if (!m_mPlayerInfoMap[loser].empty()) {
                CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_mPlayerInfoMap[loser].begin()->first);
                User* user = nullptr;
                if (creature != nullptr && creature->IsPC()) {
                    user = creature->CastUser();
                }
                if (user != nullptr) {
                    CParty* const party = user->GetParty();
                    if (party != nullptr) {
                        loserUser = party->GetMaster();
                    }
                    if (loserUser == nullptr) {
                        loserUser = user;
                    }
                }
            }
        }
        if (winner == nullptr) {
            return;
        }
        SendToAllPlayers("chccS", 0x62, 0x79f, 1, 0x0c, winner->GetName());
        if (loserUser == nullptr) {
            return;
        }

        const int actorZ = runtime::Win64CvttDoubleToInt32(winner->GetPos().z);
        const int actorY = runtime::Win64CvttDoubleToInt32(winner->GetPos().y);
        const int actorX = runtime::Win64CvttDoubleToInt32(winner->GetPos().x);
        const char16_t* const targetName = loserUser->GetName();
        const char16_t* const actorName = winner->GetName();
        const std::uint32_t targetAccountId = loserUser->GetAccountID();
        const std::uint32_t targetDbId = loserUser->GetDBID();
        const std::uint32_t actorAccountId = winner->GetAccountID();
        const std::uint32_t actorDbId = winner->GetDBID();
        std::array<wchar_t, 16> actorAccountStorage{};
        std::array<wchar_t, 16> targetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     actorDbId,
        //     actorAccountId,
        //     targetDbId,
        //     targetAccountId,
        //     actorX,
        //     actorY,
        //     actorZ,
        //     1,
        //     0,
        //     actorName,
        //     DuelAuditAccountName(
        //         winner->m_sAccountName,
        //         actorAccountStorage),
        //     targetName,
        //     DuelAuditAccountName(
        //         loserUser->m_sAccountName,
        //         targetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, actorDbId, actorAccountId, targetDbId, targetAccountId, actorX, actorY, actorZ, 1, 0, actorName,
            DuelAuditAccountName(winner->m_sAccountName, actorAccountStorage), targetName,
            DuelAuditAccountName(loserUser->m_sAccountName, targetAccountStorage)
        );
        const int finalActorZ = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().z);
        const int finalActorY = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().y);
        const int finalActorX = runtime::Win64CvttDoubleToInt32(loserUser->GetPos().x);
        const char16_t* const finalTargetName = winner->GetName();
        const char16_t* const finalActorName = loserUser->GetName();
        const std::uint32_t finalTargetAccountId = winner->GetAccountID();
        const std::uint32_t finalTargetDbId = winner->GetDBID();
        const std::uint32_t finalActorAccountId = loserUser->GetAccountID();
        const std::uint32_t finalActorDbId = loserUser->GetDBID();
        std::array<wchar_t, 16> finalActorAccountStorage{};
        std::array<wchar_t, 16> finalTargetAccountStorage{};
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
        //     0x377,
        //     finalActorDbId,
        //     finalActorAccountId,
        //     finalTargetDbId,
        //     finalTargetAccountId,
        //     finalActorX,
        //     finalActorY,
        //     finalActorZ,
        //     1,
        //     2,
        //     finalActorName,
        //     DuelAuditAccountName(
        //         loserUser->m_sAccountName,
        //         finalActorAccountStorage),
        //     finalTargetName,
        //     DuelAuditAccountName(
        //         winner->m_sAccountName,
        //         finalTargetAccountStorage))
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,,,,,,,,,%s,%s,%s,%s,",
            0x377, finalActorDbId, finalActorAccountId, finalTargetDbId, finalTargetAccountId, finalActorX, finalActorY,
            finalActorZ, 1, 2, finalActorName, DuelAuditAccountName(loserUser->m_sAccountName, finalActorAccountStorage),
            finalTargetName, DuelAuditAccountName(winner->m_sAccountName, finalTargetAccountStorage)
        );
    }
}

CDuel* CDuel::GetObjectW(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();

    CGeneralObject* const object = CGeneralObject::GetObjectW(smartId);
    if (object != nullptr && object->IsDuel()) {
        return object->CastDuel();
    }
    return nullptr;
}
