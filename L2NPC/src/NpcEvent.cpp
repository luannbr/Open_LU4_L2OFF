#include "NpcEvent.h"
#include "GlobalObject.h"
#include "L2Core/src/inc/Guard.h"
#include "NPC.h"
#include "NpcMakerEx.h"
#include "NpcSpawnDefineEx.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>


namespace {

// Exact rev73 tables referenced by target addresses 0x006b7d90 and
// 0x006b8450. Their sentinel ids are the constructor defaults 0xd7 and 0x1f.
constexpr std::array<const char16_t*, 216> kNpcEventNames{{
    u"NO_DESIRE",
    u"ATTACKED",
    u"SPELLED",
    u"TALKED",
    u"TALK_SELECTED",
    u"SEE_CREATURE",
    u"DISAPPEAR_CREATURE",
    u"SEE_ITEM",
    u"DISAPPEAR_ITEM",
    u"MY_DYING",
    u"TIMER_FIRED",
    u"TIMER_FIRED_EX",
    u"CREATED",
    u"TIME",
    u"SEE_ATTACK",
    u"SEE_SPELL",
    u"OUT_OF_TERRITORY",
    u"DESIRE_MANIPULATION",
    u"PARTY_ATTACKED",
    u"PARTY_SPELLED",
    u"PARTY_DIED",
    u"FRIEND_ATTACKED",
    u"CLAN_ATTACKED",
    u"CLAN_SPELLED",
    u"STATIC_OBJECT_CLAN_ATTACKED",
    u"STATIC_OBJECT_CLAN_SPELLED",
    u"HOLYTHING_SPELLED",
    u"TELEPORT_REQUESTED",
    u"BUY_REQUESTED",
    u"SELL_REQUESTED",
    u"QUEST_ACCEPTED",
    u"MENU_SELECTED",
    u"LEARN_SKILL_REQUESTED",
    u"ENCHANT_SKILL_REQUESTED",
    u"ONE_SKILL_SELECTED",
    u"ONE_ENCHANT_SKILL_SELECTED",
    u"SKILL_QUEST_ACCEPTED",
    u"CLASS_CHANGE_REQUESTED",
    u"MANOR_MENU_SELECTED",
    u"CMD_FOLLOW_ME",
    u"CMD_ATTACK",
    u"GOD_SHOUT",
    u"GOD_SELL_GOODS",
    u"GOD_BUY_GOODS",
    u"CREATE_PLEDGE",
    u"DISMISS_PLEDGE",
    u"REVIVE_PLEDGE",
    u"LEVEL_UP_PLEDGE",
    u"ATTACKED_BY_PLEDGE",
    u"MANAGE_CASTLE_SIEGE",
    u"CREATE_ALLIANCE",
    u"SCRIPT_EVENT",
    u"TUTORIAL_EVENT",
    u"TUTORIAL_MARK_CLICKED",
    u"USER_CONNECTED",
    u"IDLE_FINISHED",
    u"MOVE_AROUND_FINISHED",
    u"ATTACK_FINISHED",
    u"CHASE_FINISHED",
    u"FLEE_FINISHED",
    u"GET_ITEM_FINISHED",
    u"DECAYING_FINISHED",
    u"FOLLOW_FINISHED",
    u"MOVE_TO_WAY_POINT_FINISHED",
    u"USE_SKILL_FINISHED",
    u"MOVE_TO_FINISHED",
    u"IDLE_INTRRUPTED",
    u"MOVE_AROUND_INTERRUPTED",
    u"ATTACK_INTERRUPTED",
    u"CHASE_INTERRUPTED",
    u"FLEE_INTERRUPTED",
    u"GET_ITEM_INTERRUPTED",
    u"DECAYING_INTERRUPTED",
    u"FOLLOW_INTERRUPTED",
    u"MOVE_TO_WAY_POINT_INTERRUPTED",
    u"USE_SKILL_INTERRUPTED",
    u"MOVE_TO_INTERRUPTED",
    u"DOOR_HP_LEVEL_INFORMED",
    u"CONTROLTOWER_LEVEL_INFORMED",
    u"TB_REGISTER_PLEDGE_RETURNED",
    u"TB_REGISTER_MEMBER_RETURNED",
    u"TB_GET_NPC_TYPE_INFORMED",
    u"TB_SET_NPC_TYPE_RETURNED",
    u"TB_GET_PLEDGE_REGISTER_STATUS_INFORMED",
    u"TB_GET_BATTLE_ROYAL_PLEDGE_LIST_INFORMED",
    u"SUBJOB_LIST_INFORMED",
    u"SUBJOB_CREATED",
    u"SUBJOB_CHANGED",
    u"SUBJOB_RENEWED",
    u"ON_SSQ_SYSTEM_EVENT",
    u"SET_AGIT_DECO_RETURNED",
    u"RESET_AGIT_DECO_RETURNED",
    u"AUCTION_AGIT_GET_COST_INFO_RETURNED",
    u"CLAN_DIED",
    u"SET_NOBLESS_RETURNED",
    u"SET_HERO_RETURNED",
    u"DELETE_PREVIOUS_OLYMPIAD_POINT_RETURNED",
    u"DELETE_OLYMPIAD_TRADE_POINT_RETURNED",
    u"MG_REGISTER_PLEDGE_RETURNED",
    u"MG_UNREGISTER_PLEDGE_RETURNED",
    u"MG_GET_STATUS_RETURNED",
    u"MG_JOIN_GAME_RETURNED",
    u"MG_GET_UNRETURNED_POINT_RETURNED",
    u"CREATE_ACADEMY",
    u"CREATE_SUBPLEDGE",
    u"CHECK_CURSED_USER_RETURNED",
    u"UPDATE_SUBPLEDGE_MASTER",
    u"UPGRADE_SUBPLEDGE_MEMBER_COUNT",
    u"MPCC_TELEPORTED",
    u"PLEDGE_MASTER_TRANSFER",
    u"PLEDGE_MASTER_TRANSFER_CANCEL",
    u"TB_CHECK_MEMBER_REGISTER_STATUS",
    u"RENAME_SUBPLEDGE",
    u"ABNORMAL_STATUS_CHANGED",
    u"TELEPORT_TO_USER_RES",
    u"NODE_ARRIVED",
    u"TRAP_STEP_IN",
    u"TRAP_STEP_OUT",
    u"TRAP_ACTIVATED",
    u"TRAP_DEFUSED",
    u"TRAP_DETECTED",
    u"SHOW_ENCHANT_SKILL_DRAWER",
    u"FIRE",
    u"INSTANT_ZONE_ENTER_RETURNED",
    u"INSTANT_ZONE_REMAIN_DURATION",
    u"FORTRESS_SIEGE_REGISTER_RETURNED",
    u"FORTRESS_SIEGE_UNREGISTER_RETURNED",
    u"FORTRESS_CONTRACT_CASTLE_RETURNED",
    u"FORTRESS_UPGRADE_FACILITY_RETURNED",
    u"ON_OLYMPIAD_GAME_PREPARED",
    u"PLEDGE_CASTLE_SIEGE_DEFENCE_COUNT_DECREASE_RETURNED",
    u"REGISTER_PVP_MATCH_RESULT",
    u"UNREGISTER_PVP_MATCH_RESULT",
    u"START_PVP_MATCH_RESULT",
    u"END_PVP_MATCH_RESULT",
    u"CHECK_REGISTER_PARTY_RESULT",
    u"CHECK_REGISTER_PARTY_RESULT2",
    u"REGISTER_RESURRECTION_TOWER_RESULT",
    u"CHECK_REGISTER_USER_RESULT",
    u"IS_USER_PVPMATCHING_RESULT",
    u"REGISTER_USER_PVP_MATCH_RESULT",
    u"UNREGISTER_USER_PVP_MATCH_RESULT",
    u"REGISTER_USER_RESURRECTION_TOWER_RESULT",
    u"KILLED_USER_PVP_MATCH_RESULT",
    u"GET_RANK_USER_PVP_MATCH_RESULT",
    u"LET_IN_USER_PVP_MATCH",
    u"START_USER_PVP_MATCH_RESULT",
    u"WITHDRAW_USER_PVP_MATCH_RESULT",
    u"END_USER_PVP_MATCH_RESULT",
    u"GET_RELATED_FORTRESS_LIST_RETURNED",
    u"CHANGE_NPC_STATUS_RETURNED",
    u"REGISTER_DOMINION_SIEGE_RETURNED",
    u"DECLARE_LORD_RETURNED",
    u"GET_REGISTRY_SIZE_RETURNED",
    u"DOMINION_SIEGE_START",
    u"DOMINION_SIEGE_END",
    u"DOMINION_SIEGE_PC_KILLED",
    u"CLEFT_STATE_CHANGED",
    u"BLOCK_UPSET_STARTED",
    u"BLOCK_UPSET_FINISHED",
    u"LEVEL_UP",
    u"LOAD_DBSAVING_MAP_RETURNED",
    u"DOMINION_SCRIPT_EVENT",
    u"DIE_SET",
    u"RESTART_SET",
    u"SOCIAL_ACTION_EVENT",
    u"PET_DIED",
    u"WAS_COLLECTED",
    u"ON_AIRSHIP_EVENT",
    u"DOMINION_SUPPLY_DESTRUCTED",
    u"CREATE_NPC_TO_WINNER",
    u"CREATE_COW",
    u"DEBUG_AI",
    u"IS_TOGGLE_SKILL_ONOFF",
    u"FIELD_CYCLE_STEP_EXPIRED",
    u"SPELL_SUCCESSED",
    u"GET_EVENT_RANKER_INFO",
    u"OLYMPIAD_MATCH_RESULT_EVENT",
    u"GET_PLAYING_USER_COUNT",
    u"FIND_RANDOM_USER",
    u"GIVE_EVENT_DATA",
    u"PET_AI",
    u"PET_REACTION",
    u"USE_SKILL_STARTED",
    u"INZONE_ALL_USER",
    u"AREA_STATUS",
    u"DOOR_STATUS",
    u"MASTER_ATTACK",
    u"GSTAR_TRIGGER",
    u"REWARD_RATE_EVENT",
    u"INSTANT_AGIT_EVENT",
    u"CALL_TO_CHANGE_CLASS",
    u"CHANGE_TO_AWAKENED_CLASS",
    u"MASTER_ATTACKED",
    u"TOTAL_REWARD_SKILL_ENCHANT_COUNT",
    u"INSTANT_AGIT_SET_OWNER",
    u"CHANGE_CLASS",
    u"CAN_CHANGE_TO_SUBCLASS",
    u"SCENE_STOPPED",
    u"CHANGE_EVENT_NPC_STAGE",
    u"START_DYNAMIC_QUEST_RESULT",
    u"DUALJOB_CREATED",
    u"KAMA_CHECK_REWARD",
    u"WORLD_CHAT",
    u"PLEDGE_GAME_RESULT",
    u"GET_GREAT_EVENT_CONTRIBUTOR",
    u"FISHING_EVENT",
    u"CAST_BUFF_TO_RANDOM_USER",
    u"RESULT_PLEDGE_RAID_POINT",
    u"WORLD_RAID_START",
    u"WORLD_RAID_EVENT",
    u"ITEM_NPC",
    u"PC_DIE_NPC",
    u"MAIL_NPC",
    u"REALDAMAGED",
    nullptr,
}};

constexpr std::array<const char16_t*, 32> kMakerEventNames{{
    u"ON_START",
    u"ON_NPC_DELETED",
    u"ON_ALL_NPC_DELETED",
    u"ON_SCRIPT_EVENT",
    u"ON_DB_NPC_INFO",
    u"ON_TIMER",
    u"ON_NPC_CREATED",
    u"ON_NPCPOS_EVENT",
    u"ON_DOOR_EVENT",
    u"ON_START_SIEGE_EVENT",
    u"ON_END_SIEGE_EVENT",
    u"ON_PROCLAIM_SIEGE_EVENT",
    u"ON_DESTRUCT_CTRL_TOWER_EVENT",
    u"ON_CANCEL_SIEGE_EVENT",
    u"ON_START_TEAMBATTLEAGIT_FINAL_EVENT",
    u"ON_FIELD_CYCLE_CHANGED_EVENT",
    u"ON_FORTRESS_EVENT",
    u"ON_INSTANT_ZONE_EVENT",
    u"ON_OLYMPIAD_FIELD_STEP_CHANGED_EVENT",
    u"ON_EXTENDED_START_SIEGE_EVENT",
    u"ON_DESTRUCT_SUPPLY_EVENT",
    u"ON_DECLARE_DOMINION_EVENT",
    u"ON_START_CHANGE_TIME_EVENT",
    u"ON_END_CHANGE_TIME_EVENT",
    u"ON_DEBUG_MAKER_EVENT",
    u"ON_DYNAMIC_QUEST_EVENT",
    u"ON_DYNAMIC_QUEST_INFO",
    u"ON_PC_DIE_EVENT",
    u"ON_PLEDGEGAME_FIELD_STEP_CHANGED_EVENT",
    u"ON_EVENT_CAMPAIGN_EVENT",
    u"ON_HUNTER_GUILD_BLESS_EVENT",
    nullptr,
}};

// All empty ScriptEvent/CNPCEvent/MakerEvent virtuals in the current image
// return the same global two-byte string at 0x00593714.
char16_t kEmptyEventString[] = u"";

bool TargetUtf16PrefixEqual(const char16_t* left, const char16_t* right, std::size_t count) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == u'\0') {
            return true;
        }
    }
    return true;
}

bool TargetUtf16Equal(const char16_t* left, const char16_t* right) noexcept {
    for (std::size_t index = 0;; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == u'\0') {
            return true;
        }
    }
}

template <std::size_t Size>
int FindEventId(const std::array<const char16_t*, Size>& names, const char16_t* name) {
    for (std::size_t index = 0; names[index] != nullptr; ++index) {
        if (TargetUtf16Equal(names[index], name)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

template <std::size_t Size>
const char16_t* EventName(const std::array<const char16_t*, Size>& names, int id) {
    return names[static_cast<std::size_t>(id)];
}

struct EventVariable {
    const wchar_t* name;
    int offset;
    ScriptTypeId type;
};

void AddEventVariable(ScriptTypeInfo* typeInfo, const EventVariable& variable) {
    auto* const descriptor = new ScriptVarInfo;
    descriptor->SetScriptVarName(variable.name);
    descriptor->Offset(variable.offset);
    descriptor->Type(&GetBuiltinScriptType(variable.type));
    typeInfo->AddVarInfo(descriptor);
}

} // namespace

ScriptEvent::ScriptEvent(int eventId) : m_nEventId(eventId), gg(&gGlobalObj) {}

ScriptEvent::~ScriptEvent() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x00464f60.
    std::array<CFHTML**, 2> owners{
        &fhtml0,
        &fhtml1,
    };
    for (std::size_t index = 0; index < owners.size(); ++index) {
        if (!*owners[index]) {
            continue;
        }
        L2_RELEASE((*owners[index]), ObjectRefType::CreateDelete);
        *owners[index] = nullptr;
    }
}

void ScriptEvent::CreateVariable(std::vector<std::u16string>& variables) {
    // Current L2NPC.exe 0x0050f3c0. Only fhtml0/fhtml1 trigger allocation.
    L2_GUARD_FUNCTION();
    const bool questTrace = std::getenv("L2_QUEST_TRACE") != nullptr && (m_nEventId == 0x1e || m_nEventId == 0x1f);
    if (questTrace) {
        // std::fprintf(
        //     stderr,
        //     "[QUEST_TRACE] L2NPC CreateVariable event=%d count=%zu\n",
        //     m_nEventId,
        //     variables.size())
        // std::fflush(stderr)
        char message[160]{};
        std::snprintf(
            message, sizeof(message), "[quest trace] Preparing variables for event %d: %zu entries.", m_nEventId,
            variables.size()
        );
        // runtime::logging::Write("l2npc", runtime::logging::Level::Debug, message)
        runtime::logging::Write("l2npc", runtime::logging::Level::Debug, message, __FILE__);
    }
    for (std::size_t index = 0; index < variables.size(); ++index) {
        const char16_t* const name = runtime::CheckedVectorAt(variables, index).c_str();
        if (!TargetUtf16PrefixEqual(name, u"fhtml", 5)) {
            continue;
        }

        CFHTML** owner = nullptr;
        if (TargetUtf16Equal(name, u"fhtml0")) {
            owner = &fhtml0;
        } else if (TargetUtf16Equal(name, u"fhtml1")) {
            owner = &fhtml1;
        } else {
            continue;
        }

        if (*owner) {
            L2_RELEASE((*owner), ObjectRefType::CreateDelete);
            *owner = nullptr;
        }
        *owner = new CFHTML();
        if (questTrace) {
            // std::fprintf(
            //     stderr,
            //     "[QUEST_TRACE] L2NPC CreateVariable "
            //     "name=%s owner=%p\n",
            //     TargetUtf16Equal(name, u"fhtml0")
            //         ? "fhtml0"
            //         : "fhtml1",
            //     static_cast<void*>(*owner))
            // std::fflush(stderr)
            char message[160]{};
            std::snprintf(
                message, sizeof(message), "[quest trace] Created %s for event %d at %p.",
                TargetUtf16Equal(name, u"fhtml0") ? "fhtml0" : "fhtml1", m_nEventId, static_cast<void*>(*owner)
            );
            // runtime::logging::Write("l2npc", runtime::logging::Level::Debug, message)
            runtime::logging::Write("l2npc", runtime::logging::Level::Debug, message, __FILE__);
        }
    }
}

void ScriptEvent::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x00468068. Registration order is kept because
    // ScriptTypeInfo also assigns descriptor indices while inserting.
    static constexpr EventVariable variables[] = {
        {L"talker", 0x028, ScriptTypeId::CreatureData},
        {L"attacker", 0x030, ScriptTypeId::CreatureData},
        {L"victim", 0x038, ScriptTypeId::CreatureData},
        {L"victim_so", 0x078, ScriptTypeId::StaticObjectData},
        {L"damage", 0x090, ScriptTypeId::Int},
        {L"commander", 0x050, ScriptTypeId::CreatureData},
        {L"target", 0x060, ScriptTypeId::CreatureData},
        {L"speller", 0x058, ScriptTypeId::CreatureData},
        {L"creature", 0x070, ScriptTypeId::CreatureData},
        {L"script", 0x088, ScriptTypeId::String},
        {L"state", 0x094, ScriptTypeId::Int},
        {L"who", 0x098, ScriptTypeId::Int},
        {L"weapon_class_id", 0x0c4, ScriptTypeId::Int},
        {L"success", 0x0c8, ScriptTypeId::Int},
        {L"x", 0x0cc, ScriptTypeId::Int},
        {L"y", 0x0d0, ScriptTypeId::Int},
        {L"z", 0x0d4, ScriptTypeId::Int},
        {L"pledge_index", 0x0d8, ScriptTypeId::Int},
        {L"desire", 0x0dc, ScriptTypeId::Int},
        {L"item", 0x080, ScriptTypeId::ItemData},
        {L"quest_id", 0x09c, ScriptTypeId::Int},
        {L"ask", 0x0a0, ScriptTypeId::Int},
        {L"reply", 0x120, ScriptTypeId::Int64},
        {L"skill_id", 0x0a4, ScriptTypeId::Int},
        {L"skill_level", 0x0a8, ScriptTypeId::Int},
        {L"skill_sub_level", 0x0ac, ScriptTypeId::Int},
        {L"timer_id", 0x0f4, ScriptTypeId::Int},
        {L"i0", 0x130, ScriptTypeId::Int64},
        {L"i1", 0x138, ScriptTypeId::Int64},
        {L"i2", 0x140, ScriptTypeId::Int64},
        {L"i3", 0x148, ScriptTypeId::Int64},
        {L"i4", 0x150, ScriptTypeId::Int64},
        {L"i5", 0x158, ScriptTypeId::Int64},
        {L"i6", 0x160, ScriptTypeId::Int64},
        {L"i7", 0x168, ScriptTypeId::Int64},
        {L"i8", 0x170, ScriptTypeId::Int64},
        {L"i9", 0x178, ScriptTypeId::Int64},
        {L"i10", 0x180, ScriptTypeId::Int64},
        {L"i11", 0x188, ScriptTypeId::Int64},
        {L"f0", 0x190, ScriptTypeId::Float},
        {L"f1", 0x194, ScriptTypeId::Float},
        {L"f2", 0x198, ScriptTypeId::Float},
        {L"f3", 0x19c, ScriptTypeId::Float},
        {L"f4", 0x1a0, ScriptTypeId::Float},
        {L"f5", 0x1a4, ScriptTypeId::Float},
        {L"f6", 0x1a8, ScriptTypeId::Float},
        {L"f7", 0x1ac, ScriptTypeId::Float},
        {L"f8", 0x1b0, ScriptTypeId::Float},
        {L"f9", 0x1b4, ScriptTypeId::Float},
        {L"c0", 0x1b8, ScriptTypeId::CreatureData},
        {L"c1", 0x1c0, ScriptTypeId::CreatureData},
        {L"c2", 0x1c8, ScriptTypeId::CreatureData},
        {L"c3", 0x1d0, ScriptTypeId::CreatureData},
        {L"c4", 0x1d8, ScriptTypeId::CreatureData},
        {L"c5", 0x278, ScriptTypeId::CreatureData},
        {L"c6", 0x280, ScriptTypeId::CreatureData},
        {L"c7", 0x288, ScriptTypeId::CreatureData},
        {L"c8", 0x290, ScriptTypeId::CreatureData},
        {L"c9", 0x298, ScriptTypeId::CreatureData},
        {L"pledge0", 0x1e0, ScriptTypeId::PledgeData},
        {L"pledge1", 0x1e8, ScriptTypeId::PledgeData},
        {L"party0", 0x1f0, ScriptTypeId::PartyData},
        {L"party1", 0x1f8, ScriptTypeId::PartyData},
        {L"s0", 0x258, ScriptTypeId::String},
        {L"s1", 0x260, ScriptTypeId::String},
        {L"s2", 0x2f8, ScriptTypeId::String},
        {L"s3", 0x300, ScriptTypeId::String},
        {L"s4", 0x308, ScriptTypeId::String},
        {L"fhtml0", 0x268, ScriptTypeId::FHtml},
        {L"fhtml1", 0x270, ScriptTypeId::FHtml},
        {L"_code", 0x310, ScriptTypeId::Int},
        {L"_choiceN", 0x314, ScriptTypeId::Int},
        {L"_from_choice", 0x318, ScriptTypeId::Int},
        {L"need_quest", 0x0b0, ScriptTypeId::Int},
        {L"skill_name_id", 0x128, ScriptTypeId::Int64},
        {L"occupation_name_id", 0x0b4, ScriptTypeId::Int},
        {L"private", 0x040, ScriptTypeId::CreatureData},
        {L"friend", 0x048, ScriptTypeId::CreatureData},
        {L"way_point_index", 0x0b8, ScriptTypeId::Int},
        {L"next_way_point_index", 0x0bc, ScriptTypeId::Int},
        {L"time", 0x0c0, ScriptTypeId::Int},
        {L"residence_id", 0x0e0, ScriptTypeId::Int},
        {L"inzone_type_id", 0x0e8, ScriptTypeId::Int},
        {L"inzone_id", 0x0e4, ScriptTypeId::Int},
        {L"event_id", 0x0ec, ScriptTypeId::Int},
        {L"question_id", 0x0f0, ScriptTypeId::Int},
        {L"script_event_arg1", 0x0f8, ScriptTypeId::Int},
        {L"script_event_arg2", 0x0fc, ScriptTypeId::Int},
        {L"script_event_arg3", 0x100, ScriptTypeId::Int},
        {L"level", 0x104, ScriptTypeId::Int},
        {L"action_id", 0x108, ScriptTypeId::Int},
        {L"IsCritical", 0x10c, ScriptTypeId::Int},
        {L"scene_id", 0x110, ScriptTypeId::Int},
        {L"rank", 0x114, ScriptTypeId::Int},
        {L"reward_flag", 0x118, ScriptTypeId::Int},
        {L"h0", 0x218, ScriptTypeId::HateInfo},
        {L"h1", 0x220, ScriptTypeId::HateInfo},
        {L"h2", 0x228, ScriptTypeId::HateInfo},
        {L"h3", 0x230, ScriptTypeId::HateInfo},
        {L"h4", 0x238, ScriptTypeId::HateInfo},
        {L"h5", 0x2a0, ScriptTypeId::HateInfo},
        {L"h6", 0x2a8, ScriptTypeId::HateInfo},
        {L"h7", 0x2b0, ScriptTypeId::HateInfo},
        {L"h8", 0x2b8, ScriptTypeId::HateInfo},
        {L"h9", 0x2c0, ScriptTypeId::HateInfo},
        {L"npc0", 0x240, ScriptTypeId::Npc},
        {L"npc1", 0x248, ScriptTypeId::Npc},
        {L"npc2", 0x250, ScriptTypeId::Npc},
        {L"maker0", 0x320, ScriptTypeId::NpcMaker},
        {L"maker1", 0x328, ScriptTypeId::NpcMaker},
        {L"maker2", 0x2e0, ScriptTypeId::NpcMaker},
        {L"maker3", 0x2e8, ScriptTypeId::NpcMaker},
        {L"maker4", 0x2f0, ScriptTypeId::NpcMaker},
        {L"item0", 0x200, ScriptTypeId::ItemData},
        {L"item1", 0x208, ScriptTypeId::ItemData},
        {L"so0", 0x210, ScriptTypeId::StaticObjectData},
        {L"room0", 0x330, ScriptTypeId::RoomInfo},
        {L"room1", 0x338, ScriptTypeId::RoomInfo},
        {L"rlist0", 0x340, ScriptTypeId::RoomInfoList},
        {L"rlist1", 0x348, ScriptTypeId::RoomInfoList},
        {L"pos0", 0x350, ScriptTypeId::Position},
        {L"pos1", 0x358, ScriptTypeId::Position},
        {L"pos2", 0x2c8, ScriptTypeId::Position},
        {L"pos3", 0x2d0, ScriptTypeId::Position},
        {L"pos4", 0x2d8, ScriptTypeId::Position},
        {L"plist0", 0x360, ScriptTypeId::PositionList},
        {L"plist1", 0x368, ScriptTypeId::PositionList},
        {L"gg", 0x370, ScriptTypeId::GlobalObject},
    };

    for (const EventVariable& variable : variables) {
        AddEventVariable(typeInfo, variable);
    }
}

ScriptAccessibleEx* ScriptEvent::GetScriptAccessibleEx() const { return const_cast<ScriptEvent*>(this); }

int ScriptEvent::GetEventID(const char16_t*) { return -1; }

const char16_t* ScriptEvent::GetEventName(int) { return kEmptyEventString; }

char16_t* ScriptEvent::GetMySelfName() { return kEmptyEventString; }

int ScriptEvent::GetMySelfID() { return 0; }

CNPCEvent::CNPCEvent() : ScriptEvent(0xd7) {}

CNPCEvent::~CNPCEvent() {
    if (myself) {
        L2_RELEASE(myself, ObjectRefType::EventRetain);
        myself = nullptr;
    }
}

int CNPCEvent::GetEventID(const char16_t* name) {
    L2_GUARD_FUNCTION();
    return FindEventId(kNpcEventNames, name);
}

const char16_t* CNPCEvent::GetEventName(int id) { return EventName(kNpcEventNames, id); }

char16_t* CNPCEvent::GetMySelfName() {
    if (!myself) {
        return kEmptyEventString;
    }
    return myself->GetName();
}

int CNPCEvent::GetMySelfID() {
    if (!myself) {
        return 0;
    }
    return myself->m_data->m_nClassID;
}

void CNPCEvent::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x004e1e2c.
    static constexpr EventVariable variables[] = {
        {L"random1_list", 0x3b0, ScriptTypeId::CodeInfoList},
        {L"always_list", 0x3b8, ScriptTypeId::CodeInfoList},
        {L"code_info", 0x3c0, ScriptTypeId::CodeInfo},
        {L"last_attacker", 0x388, ScriptTypeId::CreatureData},
        {L"member", 0x390, ScriptTypeId::CreatureData},
        {L"lparty", 0x398, ScriptTypeId::PartyData},
        {L"myself", 0x3d0, ScriptTypeId::Npc},
        {L"item_index_list", 0x3c8, ScriptTypeId::ItemIndexList},
        {L"lmpcc", 0x3a0, ScriptTypeId::MpccData},
        {L"aparty", 0x3a8, ScriptTypeId::PartyData},
    };
    for (const EventVariable& variable : variables) {
        AddEventVariable(typeInfo, variable);
    }
    ScriptEvent::CollectVar(typeInfo);
}

MakerEvent::MakerEvent() : ScriptEvent(0x1f) {}

MakerEvent::~MakerEvent() {

    if (myself) {
        L2_RELEASE(myself, ObjectRefType::EventRetain);
        myself = nullptr;
    }
    if (deleted_def) {
        L2_RELEASE(deleted_def, ObjectRefType::EventRetain);
        deleted_def = nullptr;
    }
    if (deleted_npc) {
        L2_RELEASE(deleted_npc, ObjectRefType::EventRetain);
        deleted_npc = nullptr;
    }
    if (loaded_def) {
        L2_RELEASE(loaded_def, ObjectRefType::EventRetain);
        loaded_def = nullptr;
    }
    if (created_def) {
        L2_RELEASE(created_def, ObjectRefType::EventRetain);
        created_def = nullptr;
    }
    if (created_npc) {
        L2_RELEASE(created_npc, ObjectRefType::EventRetain);
        created_npc = nullptr;
    }
}

int MakerEvent::GetEventID(const char16_t* name) { return FindEventId(kMakerEventNames, name); }

const char16_t* MakerEvent::GetEventName(int id) { return EventName(kMakerEventNames, id); }

char16_t* MakerEvent::GetMySelfName() {
    if (!myself) {
        return kEmptyEventString;
    }
    return const_cast<char16_t*>(myself->GetName());
}

void MakerEvent::CollectVar(ScriptTypeInfo* typeInfo) {
    // Current L2NPC.exe 0x00468e64.
    static constexpr EventVariable variables[] = {
        {L"myself", 0x388, ScriptTypeId::NpcMaker},
        {L"deleted_def", 0x398, ScriptTypeId::NpcSpawnDefineEx},
        {L"created_def", 0x3a8, ScriptTypeId::NpcSpawnDefineEx},
        {L"loaded_def", 0x3a0, ScriptTypeId::NpcSpawnDefineEx},
        {L"def0", 0x390, ScriptTypeId::NpcSpawnDefineEx},
        {L"deleted_npc", 0x3b0, ScriptTypeId::Npc},
        {L"created_npc", 0x3b8, ScriptTypeId::Npc},
        {L"record0", 0x3c0, ScriptTypeId::DBNpcInfo},
        {L"enabled", 0x3c8, ScriptTypeId::Int},
        {L"died", 0x3cc, ScriptTypeId::Int},
    };
    for (const EventVariable& variable : variables) {
        AddEventVariable(typeInfo, variable);
    }
    ScriptEvent::CollectVar(typeInfo);
}
