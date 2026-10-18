#include "UserBasicAction.h"
#include "AirShip.h"
#include "CConfig.h"
#include "CWorld.h"
#include "CreatureController.h"
#include "NpcServer.h"
#include "OlympiadStuff.h"
#include "ParserBasicAction.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Raid.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/net/Serialize.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

struct UserActionHandlerListType {
    const char16_t* name;
    CBasicActionHandler* pHandler;
};

// Current L2Server.exe 0x015e8d20: the 23-entry process-lifetime table is
// initialized during CRT startup, before g_UserBasicAction can consume it.
// TotalChecker2015 supplies the original field name pHandler. The current
// image fixes the extent, ordering and eager lifecycle.
UserActionHandlerListType UserActionHandlerList[] = {
    {u"SIT_STAND", new CBasicActionHandler_SIT_STAND},
    {u"WALK_RUN", new CBasicActionHandler_WALK_RUN},
    {u"ATTACK", new CBasicActionHandler_ATTACK},
    {u"TRADE", new CBasicActionHandler_TRADE},
    {u"CLIENT_ACTION", new CBasicActionHandler_CLIENT_ACTION},
    {u"SOCIAL_ACTION", new CBasicActionHandler_SOCIAL_ACTION},
    {u"PRIVATE_STORE", new CBasicActionHandler_PRIVATE_STORE},
    {u"PRIVATE_BUY", new CBasicActionHandler_PRIVATE_BUY},
    {u"MAKE_ITEM", new CBasicActionHandler_MAKE_ITEM},
    {u"RIDE", new CBasicActionHandler_RIDE},
    {u"MAKE_ITEM2", new CBasicActionHandler_MAKE_ITEM2},
    {u"PACKAGE_PRIVATE_STORE", new CBasicActionHandler_PACKAGE_PRIVATE_STORE},
    {u"PET_ACTION", new CBasicActionHandler_PET_ACTION},
    {u"SUMMON_ACTION", new CBasicActionHandler_SUMMON_ACTION},
    {u"PET_DEPOSIT", new CBasicActionHandler_PET_DEPOSIT},
    {u"SUMMON_DESPAWN", new CBasicActionHandler_SUMMON_DESPAWN},
    {u"TELEPORT_BOOKMARK", new CBasicActionHandler_TELEPORT_BOOKMARK},
    {u"BOT_REPORT", new CBasicActionHandler_BOT_REPORT},
    {u"AIRSHIP_ACTION", new CBasicActionHandler_AIRSHIP_ACTION},
    {u"COUPLE_ACTION", new CBasicActionHandler_COUPLE_ACTION},
    {u"SET_TACTICAL_SIGN", new CBasicActionHandler_SET_TACTICAL_SIGN},
    {u"TARGET_TACTICAL_SIGN", new CBasicActionHandler_TARGET_TACTICAL_SIGN},
    {u"INSTANTZONE", new CBasicActionHandler_INSTANTZONE},
};


int GetUserActionHandlerListSize() {
    // Current L2Server.exe 0x00bd02bc.
    L2_GUARD_FUNCTION();
    return static_cast<int>(sizeof(UserActionHandlerList) / sizeof(UserActionHandlerList[0]));
}

void LogUserActionHandlerList() {
    // Current L2Server.exe 0x00bd033c calls the guarded size root before its
    // first log and observes the global table by index.
    L2_GUARD_FUNCTION();
    const int handlerCount = GetUserActionHandlerListSize();
    // Log.Add(
    //     LOG_ERROR,
    //     L" --- UserActionHandlerList Begin ---")
    // L2_LOG_ERROR(Log,
    //     L" --- UserActionHandlerList Begin ---")
    L2_LOG_ERROR(Log, "Available user-action handlers (%d entries):", handlerCount);
    for (int index = 0; index < handlerCount; ++index) {
        const UserActionHandlerListType& entry = UserActionHandlerList[index];
        const std::string name = Utf16ToUtf8(entry.name);
        // Log.Add(LOG_ERROR, L"[%s]", name.c_str())
        // L2_LOG_ERROR(Log, L"[%s]", name.c_str())
        L2_LOG_ERROR(Log, "User-action handler %d of %d: '%s'.", index + 1, handlerCount, name.c_str());
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L" --- UserActionHandlerList End ---")
    // L2_LOG_ERROR(Log,
    //     L" --- UserActionHandlerList End ---")
    L2_LOG_ERROR(Log, "End of user-action handler list.");
}

} // namespace

namespace PetSummonActionHelper {

CPet* CheckPet(User* const user) {
    L2_GUARD_FUNCTION();

    if (user->m_data->m_bIsAlive == 0) {
        return nullptr;
    }

    CPet* const pet = user->GetPet();
    if (pet == nullptr) {
        user->SendSystemMessage(SystemMessageId::NO_PET);
    }
    return pet;
}

void CheckSummon(User* const user, std::vector<CSummon*>& summons) {
    L2_GUARD_FUNCTION();

    if (user->m_data->m_bIsAlive == 0) {
        return;
    }

    user->GetAllSummon(summons);
    if (summons.empty()) {
        user->SendSystemMessage(SystemMessageId::NO_SERVITOR);
    }
}

bool CheckMasterPKCount(User* const user, const PetCommandType command) {
    L2_GUARD_FUNCTION();

    if (user == nullptr || user->GetSocket() == nullptr || (command != PCMD_ATTACK && command != PCMD_SPECIAL_SKILL)) {
        return true;
    }

    CCreature* const target = user->m_spTarget.GetCreature();
    User* const targetMaster = target == nullptr ? nullptr : target->GetUserOrMaster();
    if (targetMaster != nullptr && !F2PConfig::CheckPKCount(user) && !targetMaster->m_bEinhasadeTransformed &&
        targetMaster->m_data->m_nPKCount >= 0 && targetMaster->m_data->m_cIsGuilty < 1) {
        user->GetSocket()->SendSystemMessage_S(
            SystemMessageId::CHARACTER_CANNOT_INITIATE_ATTACKS_ON_NON_HOSTILE_PLAYERS, user->GetName()
        );
        return false;
    }
    return true;
}

bool Help(
    User* const user,
    CSummon* const summon,
    const PetCommandType command,
    const bool controlPressed,
    const bool shiftPressed,
    const int actionId
) {
    L2_GUARD_FUNCTION();

    if (!summon->m_betrayOwnMaster && summon->m_SkillDurationMod.m_nFearCount < 1 &&
        summon->m_SkillDurationMod.m_nPsychicPowerCount < 1) {
        if (summon->m_data->m_nLevel - user->m_data->m_nLevel < 20 ||
            (command != PCMD_ATTACK && command != PCMD_SPECIAL_SKILL)) {
            g_npcserver.Send(
                "cdddddd", 0x37, summon->Id(), static_cast<int>(command), user->m_spTarget.GetId(),
                static_cast<int>(controlPressed), static_cast<int>(shiftPressed), actionId
            );
            return true;
        }
        user->SendSystemMessage(SystemMessageId::PET_LEVEL_TOO_HIGH_TO_CONTROL);
        return false;
    }

    user->SendSystemMessage(SystemMessageId::PET_OR_SERVITOR_IGNORES_ORDERS_DUE_TO_ABNORMAL_STATE);
    return false;
}

bool Help(
    User* const user,
    std::vector<CSummon*>& summons,
    const PetCommandType command,
    const bool controlPressed,
    const bool shiftPressed,
    const int actionId
) {
    L2_GUARD_FUNCTION();

    bool handled = false;
    for (CSummon* const summon : summons) {
        if (summon != nullptr && Help(user, summon, command, controlPressed, shiftPressed, actionId)) {
            handled = true;
        }
    }
    return handled;
}

} // namespace PetSummonActionHelper

CUserBasicAction g_UserBasicAction;

CBasicActionHandlerWrapper::CBasicActionHandlerWrapper(
    CBasicActionHandler* const handler, const int option, const bool allowedRaidUser
) noexcept :
    m_handler(handler), m_option(option), m_allowedRaidUser(allowedRaidUser), m_padding0d{} {}

bool CBasicActionHandler::Handle(
    User* const user,
    const int actionId,
    const bool controlPressed,
    const bool shiftPressed,
    const int option,
    const bool allowedRaidUser
) {
    L2_GUARD_FUNCTION();
    (void)allowedRaidUser;
    return HandleInternal(user, actionId, controlPressed, shiftPressed, option);
}

bool CBasicActionHandlerWrapper::Handle(
    User* const user, const int actionId, const bool controlPressed, const bool shiftPressed
) {
    L2_GUARD_FUNCTION();
    return m_handler != nullptr &&
        m_handler->Handle(user, actionId, controlPressed, shiftPressed, m_option, m_allowedRaidUser);
}

CUserBasicAction::CUserBasicAction() : m_buf{}, m_bufLen(0), m_actionCount(0), m_padding4fc4(0) {}

CUserBasicAction::~CUserBasicAction() = default;

CBasicActionHandler* CUserBasicAction::GetHandler(const char16_t* const handlerName) {
    L2_GUARD_FUNCTION();

    const int handlerCount = GetUserActionHandlerListSize();
    for (int index = 0; index < handlerCount; ++index) {
        const UserActionHandlerListType& entry = UserActionHandlerList[index];
        const char16_t* left = entry.name;
        const char16_t* right = handlerName;
        bool equal = false;
        for (;;) {
            const char16_t leftValue = *left++;
            const char16_t rightValue = *right++;
            if (leftValue != rightValue) {
                break;
            }
            if (leftValue == u'\0') {
                equal = true;
                break;
            }
        }
        if (equal) {
            return entry.pHandler;
        }
    }
    return nullptr;
}

void CUserBasicAction::SendList(User* const user) {
    L2_GUARD_FUNCTION();
    user->GetSocket()->Send("chdb", 0xfe, 0x60, m_actionCount, static_cast<int>(m_bufLen), m_buf);
}

bool CUserBasicAction::HandleServerTriggeredAction(
    User* const user, const ServerTriggeredUserAction action, const bool controlPressed, const bool shiftPressed
) {
    L2_GUARD_FUNCTION();

    CBasicActionHandler* const handler = UserActionHandlerList[static_cast<std::size_t>(action)].pHandler;
    return handler != nullptr && handler->Handle(user, -1, controlPressed, shiftPressed, -1, true);
}

bool CUserBasicAction::HandleAction(
    User* const user, const int actionId, const bool controlPressed, const bool shiftPressed
) {
    L2_GUARD_FUNCTION();

    const auto position = m_actionMap.find(actionId);
    return position != m_actionMap.end() && position->second != nullptr &&
        position->second->Handle(user, actionId, controlPressed, shiftPressed);
}

bool CUserBasicAction::IsValid(const int actionId) const {
    L2_GUARD_FUNCTION();
    return m_actionMap.find(actionId) != m_actionMap.end();
}

bool CUserBasicAction::Add(
    const int actionId, const char16_t* const handlerName, const int option, const bool allowedRaidUser
) {
    L2_GUARD_FUNCTION();

    if (m_actionMap.find(actionId) != m_actionMap.end()) {
        return false;
    }

    CBasicActionHandler* const handler = GetHandler(handlerName);
    if (handler == nullptr) {
        const std::string hostName = Utf16ToUtf8(handlerName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] (%s) user action handler does not exist. (id:%d)",
        //     L"UserBasicAction.cpp",
        //     0x6d,
        //     hostName.c_str(),
        //     actionId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] (%s) user action handler does not exist. (id:%d)",
        //     L"UserBasicAction.cpp",
        //     0x6d,
        //     hostName.c_str(),
        //     actionId)
        L2_LOG_ERROR(
            Log, "Cannot register user action %d: handler '%s' is unknown; printing available handlers.", actionId,
            hostName.c_str()
        );
        LogUserActionHandlerList();
        return false;
    }

    m_actionMap.emplace(actionId, new CBasicActionHandlerWrapper(handler, option, allowedRaidUser));
    return true;
}

bool CUserBasicAction::Add(const int actionId) {
    L2_GUARD_FUNCTION();
    return m_actionMap.emplace(actionId, nullptr).second;
}

void CUserBasicAction::MakeList() {
    L2_GUARD_FUNCTION();

    m_bufLen = 0;
    m_actionCount = 0;
    for (const auto& action : m_actionMap) {
        ++m_actionCount;
        const int assembled = ::Assemble(
            reinterpret_cast<char*>(m_buf) + m_bufLen, static_cast<int>(sizeof(m_buf) - m_bufLen), "d", action.first
        );
        if (assembled > 0) {
            m_bufLen += static_cast<std::uint32_t>(assembled);
            Singleton<WOlympiadDefaultSetting>::inst().AddAction(action.first);
            RaidAllowAction::Inst().AddAction(action.first);
        }
    }
}

void CUserBasicAction::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\UserBasicAction.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/UserBasicAction.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from UserBasicAction file failed",
        //     L"UserBasicAction.cpp",
        //     0xe8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from UserBasicAction file failed",
        //     L"UserBasicAction.cpp",
        //     0xe8)
        L2_LOG_ERROR(Log, "Cannot read UserBasicAction.txt; action loading stops.");
        return;
    }

    CParserForAction parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UserBasicActionParse.create failed",
        //     L"UserBasicAction.cpp",
        //     0xf1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] UserBasicActionParse.create failed",
        //     L"UserBasicAction.cpp",
        //     0xf1)
        L2_LOG_ERROR(Log, "Cannot initialize UserBasicAction.txt parser; action loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing UserBasicAction file failed after line[%d]",
        //     L"UserBasicAction.cpp",
        //     0xfe,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing UserBasicAction file failed after line[%d]",
        //     L"UserBasicAction.cpp",
        //     0xfe,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse UserBasicAction.txt at script line %lld; building the action list from entries registered so far.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
    MakeList();
}

bool CBasicActionHandler_SIT_STAND::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();

    if (user->IsRiding()) {
        return false;
    }
    if (user->IsAirShipDriver()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }

    CCreatureController* const controller = user->GetController();
    if (user->m_data->m_StopMode == STP_STAND) {
        controller->Sit(0, user->m_spTarget.GetObjectW());
    } else {
        controller->Stand(0);
    }
    return true;
}

bool CBasicActionHandler_WALK_RUN::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();

    if (user->IsAirShipDriver()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }
    user->ChangeMoveType(user->m_data->m_MoveMode == MT_SLOW ? MT_FAST : MT_SLOW, 0);
    return true;
}

bool CBasicActionHandler_ATTACK::HandleInternal(
    User* const user, int, const bool controlPressed, const bool shiftPressed, int
) {
    L2_GUARD_FUNCTION();

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target == nullptr || (!controlPressed && !target->IsEnemyTo(user))) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
    } else {
        user->Action(target, controlPressed, shiftPressed);
    }
    return true;
}

bool CBasicActionHandler_TRADE::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();

    User* const target = user->m_spTarget.GetUser();
    if (target == nullptr) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (user->m_data->GetWorldId() != target->m_data->GetWorldId()) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        // target->ActionFailed(0)
        target->ActionFailed(AF_DEFAULT);
        return false;
    }
    user->OnTradeRequestPacket(static_cast<int>(target->Id()));
    return true;
}

bool CBasicActionHandler_CLIENT_ACTION::HandleInternal(User* const user, const int actionId, bool, bool, int) {
    L2_GUARD_FUNCTION();
    if (user->m_data->m_StopMode == STP_STAND) {
        user->GetSocket()->Send("cd", 0x8f, actionId);
    }
    return true;
}

bool CBasicActionHandler_SOCIAL_ACTION::HandleInternal(User* const user, int, bool, bool, const int option) {
    L2_GUARD_FUNCTION();

    if (!user->m_SkillMod.m_bCanAct || user->GetController()->GetCurrentActionType() != ACT_NOTHING) {
        return false;
    }
    if (user->IsAirShipDriver()) {
        user->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }

    CObject* const target = user->m_spTarget.GetObjectW();
    if (target != nullptr) {
        g_npcserver.Send("cddd", 0x8e, target->Id(), user->Id(), option);
    }
    user->SocialAction(option, 0x800, false);
    return true;
}

bool CBasicActionHandler_PRIVATE_STORE::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    const bool localWorld = user->m_data->GetWorldId() == g_nWorldId;
    if (localWorld) {
        user->RequestPrivateStore();
    }
    return localWorld;
}

bool CBasicActionHandler_PRIVATE_BUY::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    const bool localWorld = user->m_data->GetWorldId() == g_nWorldId;
    if (localWorld) {
        user->RequestPrivateStoreBuy();
    }
    return localWorld;
}

bool CBasicActionHandler_MAKE_ITEM::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    const bool localWorld = user->m_data->GetWorldId() == g_nWorldId;
    if (localWorld) {
        user->RequestRecipeShopManageList(0);
    }
    return localWorld;
}

bool CBasicActionHandler_RIDE::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    if (user->m_data->m_bIsAlive == 0) {
        return false;
    }
    if (user->IsRiding()) {
        user->TryDismounting(false, true);
    } else {
        user->TryRiding();
    }
    return true;
}

bool CBasicActionHandler_MAKE_ITEM2::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    const bool localWorld = user->m_data->GetWorldId() == g_nWorldId;
    if (localWorld) {
        user->RequestRecipeShopManageList(1);
    }
    return localWorld;
}

bool CBasicActionHandler_PACKAGE_PRIVATE_STORE::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    const bool localWorld = user->m_data->GetWorldId() == g_nWorldId;
    if (localWorld) {
        user->RequestPrivateStoreWhole();
    }
    return localWorld;
}

bool CBasicActionHandler_PET_ACTION::HandleInternal(
    User* const user, const int actionId, const bool controlPressed, const bool shiftPressed, const int option
) {
    L2_GUARD_FUNCTION();
    CPet* const pet = PetSummonActionHelper::CheckPet(user);
    return pet != nullptr && PetSummonActionHelper::CheckMasterPKCount(user, static_cast<PetCommandType>(option)) &&
        PetSummonActionHelper::Help(user, pet, static_cast<PetCommandType>(option), controlPressed, shiftPressed, actionId);
}

bool CBasicActionHandler_SUMMON_ACTION::HandleInternal(
    User* const user, const int actionId, const bool controlPressed, const bool shiftPressed, const int option
) {
    L2_GUARD_FUNCTION();
    std::vector<CSummon*> summons;
    PetSummonActionHelper::CheckSummon(user, summons);
    return !summons.empty() && PetSummonActionHelper::CheckMasterPKCount(user, static_cast<PetCommandType>(option)) &&
        PetSummonActionHelper::Help(
            user, summons, static_cast<PetCommandType>(option), controlPressed, shiftPressed, actionId
        );
}

bool CBasicActionHandler_PET_DEPOSIT::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();

    const std::uint32_t tick = runtime::GetTickCount();
    if (tick - user->m_PetActionLastTick <= 999U) {
        return false;
    }
    user->m_PetActionLastTick = tick;

    CPet* const pet = PetSummonActionHelper::CheckPet(user);
    if (pet == nullptr) {
        return false;
    }
    if (pet->m_data->m_bIsAlive == 0) {
        user->SendSystemMessage(SystemMessageId::CANNOT_UNSUMMON_DEAD_PET);
        return false;
    }
    if (user->IsNowTrade()) {
        user->TradeCancel(false);
    }

    const FVector userPosition = user->GetPos();
    const FVector petPosition = pet->GetPos();
    CGeoZone* const zone = CWorld::Inst().GetGeo()->GetZone(
        ((static_cast<int>(userPosition.x) + 0x50000) >> 15) + 10, ((static_cast<int>(userPosition.y) + 0x40000) >> 15) + 10
    );
    if (zone == nullptr || (!zone->IsInNoDropItemArea(userPosition) && !zone->IsInNoDropItemArea(petPosition))) {
        user->DespawnPet(false, false, DESPAWN_PET_NONE, false);
        return true;
    }

    user->SendSystemMessage(SystemMessageId::CANNOT_UNSUMMON_PET_IN_THIS_AREA);
    return false;
}

bool CBasicActionHandler_SUMMON_DESPAWN::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();

    std::vector<CSummon*> summons;
    PetSummonActionHelper::CheckSummon(user, summons);
    int despawned = 0;
    for (CSummon* const summon : summons) {
        if (summon != nullptr && summon->m_data->m_bIsAlive != 0) {
            user->DespawnSummon(summon, false, false);
            ++despawned;
        }
    }
    if (despawned > 0) {
        if (user->IsNowTrade()) {
            user->TradeCancel(false);
        }
        CLogFormatter formatter;
        formatter.WriteLog2(0x1fa7, user, nullptr, despawned, 0, 0, 0, 0, 0, 0);
    }
    return true;
}

bool CBasicActionHandler_TELEPORT_BOOKMARK::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    user->m_bookmark.SendList(user);
    return true;
}

bool CBasicActionHandler_BOT_REPORT::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    User* const target = user->m_spTarget.GetUser();
    if (target != nullptr) {
        user->BotReport(target);
    }
    return target != nullptr;
}

bool CBasicActionHandler_AIRSHIP_ACTION::HandleInternal(User* const user, int, bool, bool, const int option) {
    L2_GUARD_FUNCTION();
    return CAirShipHandler::UserCmdHandler(user, static_cast<UserAirShipActionType>(option));
}

bool CBasicActionHandler_COUPLE_ACTION::HandleInternal(User* const user, int, bool, bool, const int option) {
    L2_GUARD_FUNCTION();
    User* const target = user->m_spTarget.GetUser();
    return target != nullptr && user->OnCoupleActionAskStart(option, target);
}

bool CBasicActionHandler_SET_TACTICAL_SIGN::HandleInternal(User* const user, int, bool, bool, const int option) {
    L2_GUARD_FUNCTION();
    return user->SetTacticalSign(option);
}

bool CBasicActionHandler_TARGET_TACTICAL_SIGN::HandleInternal(User* const user, int, bool, bool, const int option) {
    L2_GUARD_FUNCTION();
    return user->ChangeTargetToTacticalSignedCreature(option);
}

bool CBasicActionHandler_INSTANTZONE::HandleInternal(User* const user, int, bool, bool, int) {
    L2_GUARD_FUNCTION();
    return user->InstantZoneWaitingTime(true);
}
