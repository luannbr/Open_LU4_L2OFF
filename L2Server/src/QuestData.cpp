#include "QuestData.h"
#include "Shared/src/TextRuntime.h"

#include "AtomicJobServerSide.h"
#include "NPC.h"
#include "Object.h"
#include "ObjectDB.h"
#include "Quest.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

constexpr std::array<QuestConditionInfo, 16> kConditionInfoTable{{
    {u"cond_false", QCT_FALSE, 0},
    {u"cond_item", QCT_ITEM, 3},
    {u"cond_level", QCT_LEVEL, 2},
    {u"cond_class", QCT_CLASS, 2},
    {u"cond_party", QCT_PARTY, 0},
    {u"cond_kill_npc", QCT_KILL_NPC, 3},
    {u"cond_onetime_quest", QCT_ONETIME_QUEST, 2},
    {u"cond_daily_quest", QCT_DAILY_QUEST, 2},
    {u"cond_memo", QCT_MEMO, 2},
    {u"cond_memo_state", QCT_MEMO_STATE, 3},
    {u"cond_category", QCT_CATEGORY, 2},
    {u"cond_random", QCT_RANDOM, 2},
    {u"cond_acquired_ability_point", QCT_ACQUIRED_ABILITY_POINT, 2},
    {u"cond_monsterbook_faction_achievement", QCT_MONSTERBOOK_FACTION_ACHIEVEMENT, 3},
    {u"cond_field_cycle_step", QCT_FIELD_CYCLE_STEP, 3},
    {u"", QCT_INVALID, -1},
}};

constexpr std::array<QuestOperationInfo, 34> kOperationInfoTable{{
    {u"op_complete_quest", QOT_COMPLETE_QUEST, 1},
    {u"op_delete_quest", QOT_DELETE_QUEST, 1},
    {u"op_create_memo", QOT_CREATE_MEMO, 1},
    {u"op_destroy_memo", QOT_DESTROY_MEMO, 1},
    {u"op_set_memo_flag", QOT_SET_MEMO_FLAG, 2},
    {u"op_set_memo_state", QOT_SET_MEMO_STATE, 3},
    {u"op_start_journal", QOT_START_JOURNAL, 2},
    {u"op_complete_journal", QOT_COMPLETE_JOURNAL, 2},
    {u"op_monitor_start_killnpc", QOT_MONITOR_START_KILL_NPC, 1},
    {u"op_monitor_end_killnpc", QOT_MONITOR_END_KILL_NPC, 1},
    {u"op_show_html", QOT_SHOW_HTML, 2},
    {u"op_show_quest_html", QOT_SHOW_QUEST_HTML, 2},
    {u"op_show_html_if", QOT_SHOW_HTML_IF, 2},
    {u"op_give_item", QOT_GIVE_ITEM, 4},
    {u"op_delete_item", QOT_DELETE_ITEM, 4},
    {u"op_delete_item_all", QOT_DELETE_ITEM_ALL, 3},
    {u"op_give_exp", QOT_GIVE_EXP, 1},
    {u"op_give_sp", QOT_GIVE_SP, 1},
    {u"op_show_quest_mark", QOT_SHOW_QUEST_MARK, 2},
    {u"op_play_soundeffect", QOT_PLAY_SOUND_EFFECT, 1},
    {u"op_increase_npclog", QOT_INCREASE_NPC_LOG, 3},
    {u"op_class_change", QOT_CHANGE_CLASS, 2},
    {u"op_add_pc_social", QOT_ADD_PC_SOCIAL, 1},
    {u"op_show_onscreen_message", QOT_SHOW_ON_SCREEN_MSG, 14},
    {u"op_show_system_message", QOT_SHOW_SYSTEM_MSG, 1},
    {u"op_add_skill_desire", QOT_ADD_SKILL_DESIRE, 5},
    {u"op_renew_subjob", QOT_RENEW_SUBJOB, 1},
    {u"op_onetime_quest", QOT_SET_ONETIME_QUEST_FLAG, 2},
    {u"op_daily_quest", QOT_SET_DAILY_QUEST_FLAG, 1},
    {u"op_show_multi_sell", QOT_SHOW_MULTI_SELL, 1},
    {u"op_give_item_random", QOT_GIVE_ITEM_RANDOM, 9},
    {u"op_instant_teleport", QOT_INSTANT_TELEPORT, 3},
    {u"op_instant_teleport_ex", QOT_INSTANT_TELEPORT_EX, 4},
    {u"", QOT_INVALID, -1},
}};

char16_t FoldAsciiCase(const char16_t value) noexcept {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsCaseInsensitive(const char16_t* left, const char16_t* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != u'\0' && *right != u'\0') {
        if (FoldAsciiCase(*left++) != FoldAsciiCase(*right++)) {
            return false;
        }
    }
    return *left == *right;
}


template <typename TList, typename TVector>
void AppendValues(TList& destination, const TVector& source) {
    for (const auto& value : source) {
        destination.push_back(value);
    }
}

QuestOperationData MakeStringOperation(const QuestOperationType type, const int questId, const char16_t* value) {
    QuestVariable::ParamList params;
    params.push_back(QuestDataVariable::CreateStrParam(value));
    return QuestOperationData(type, questId, params);
}

QuestOperationData MakeTwoIntOperation(
    const QuestOperationType type, const int ownerQuestId, const int first, const int second
) {
    QuestVariable::ParamList params;
    params.push_back(QuestDataVariable::CreateIntParam(first));
    params.push_back(QuestDataVariable::CreateIntParam(second));
    return QuestOperationData(type, ownerQuestId, params);
}

} // namespace

QuestDataVariable::QuestDataVariable() : m_intVal(0), type(NOT_SET), m_alignment034To038{} {}

QuestDataVariable::QuestDataVariable(const std::int64_t value) : QuestDataVariable() {
    m_intVal = value;
    type = INT_VAL;
}

QuestDataVariable::QuestDataVariable(const char16_t* const value) : QuestDataVariable() { SetString(value); }

QuestDataVariable* QuestDataVariable::CreateIntParam(const int value) {
    return new QuestDataVariable(static_cast<std::int64_t>(value));
}

QuestDataVariable* QuestDataVariable::CreateStrParam(const char16_t* const value) { return new QuestDataVariable(value); }

void QuestDataVariable::SetString(const char16_t* const value) {
    if (value == nullptr) {
        type = NOT_SET;
        return;
    }
    m_strVal.assign(value);
    type = STR_VAL;
}

QuestVariable::QuestVariable(const std::int64_t type, const int questId, const ParamList& params) :
    m_Type(type), m_QuestID(questId), m_alignment014To018{}, m_Params(params) {}

QuestVariable::~QuestVariable() = default;

QuestDataVariable* QuestVariable::getParam(const int index) const {
    const std::uint64_t count = static_cast<std::uint64_t>(m_Params.size());
    const std::uint64_t requested = static_cast<std::uint64_t>(static_cast<std::int64_t>(index));
    if (count < requested) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] QuestData Parsing Error! param index is invalid!",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\QuestData.h",
        //     0x13f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] QuestData Parsing Error! param index is invalid!",
        //     L"c:\\work\\lineage2build\\server\\main\\l2server\\QuestData.h",
        //     0x13f)
        L2_LOG_ERROR(
            Log, "Quest %d: parameter index %d is invalid for type %lld with %llu parameters; returning no parameter.",
            m_QuestID, index, static_cast<long long>(m_Type), static_cast<unsigned long long>(count)
        );
        return nullptr;
    }
    if (index <= 0) {
        throw std::out_of_range("invalid vector<T> subscript");
    }
    return m_Params[static_cast<std::size_t>(index - 1)];
}

QuestConditionData::QuestConditionData(const QuestConditionType type, const int questId, const ParamList& params) :
    QuestVariable(static_cast<std::int64_t>(type), questId, params) {}

QuestOperationData::QuestOperationData(const QuestOperationType type, const int questId, const ParamList& params) :
    QuestVariable(static_cast<std::int64_t>(type), questId, params) {}

QuestData::QuestData(const std::int64_t questId) :
    m_questID(questId) {}

void QuestData::SetQuestName(std::u16string name) { m_wstrQuestName = name; }

std::u16string QuestData::GetQuestName() const { return m_wstrQuestName; }

void QuestData::SetStartConditions(const std::vector<QuestConditionData>& conditions) {
    AppendValues(m_startConditions, conditions);
}

void QuestData::SetAcceptOkOperations(const std::vector<QuestOperationData>& operations) {
    AppendValues(m_acceptOKOperations, operations);
    m_acceptOKOperations.push_back(MakeStringOperation(QOT_PLAY_SOUND_EFFECT, 0, u"ItemSound.quest_accept"));
    m_acceptOKOperations.push_back(
        MakeTwoIntOperation(QOT_SHOW_QUEST_MARK, static_cast<int>(m_questID), static_cast<int>(m_questID), 1)
    );
}

void QuestData::SetAcceptFailOperations(const std::vector<QuestOperationData>& operations) {
    AppendValues(m_acceptFailOperations, operations);
}

void QuestData::SetCompleteActions(const std::vector<QuestActionData>& actions) { AppendValues(m_completeActions, actions); }

void QuestData::SetCancelOperations(const std::vector<QuestOperationData>& operations) {
    AppendValues(m_cancelOperations, operations);
}

void QuestData::SetJournals(const std::vector<QuestJournalData>& journals) { AppendValues(m_journals, journals); }

void QuestData::SetRelatedItems(const RelatedItemList& items) { m_relatedItems = items; }

void QuestData::ChangeLastJournalCompleteSoundEffect() {
    if (m_journals.empty()) {
        return;
    }
    auto journal = m_journals.begin();
    for (auto next = journal; next != m_journals.end(); ++next) {
        journal = next;
    }
    for (QuestOperationData& operation : journal->m_completeOperations) {
        if (operation.m_Type != QOT_PLAY_SOUND_EFFECT) {
            continue;
        }
        QuestDataVariable* const parameter = operation.getParam(1);
        if (parameter != nullptr && EqualsCaseInsensitive(parameter->m_strVal.c_str(), u"ItemSound.quest_middle")) {
            parameter->SetString(u"ItemSound.quest_finish");
            return;
        }
    }
}

void QuestJournalData::SetBeginOperations(std::vector<QuestOperationData> operations) {
    m_startOperations.clear();
    AppendValues(m_startOperations, operations);
}

void QuestJournalData::SetCompleteConditions(std::vector<QuestConditionData> conditions) {
    m_completeConditions.clear();
    AppendValues(m_completeConditions, conditions);
}

void QuestJournalData::SetCompleteOperations(std::vector<QuestOperationData> operations) {
    m_completeOperations.clear();
    AppendValues(m_completeOperations, operations);
    m_completeOperations.push_back(MakeTwoIntOperation(QOT_SET_MEMO_FLAG, m_nQuestID, m_nQuestID, m_nJournalNum + 1));
    m_completeOperations.push_back(MakeTwoIntOperation(QOT_SHOW_QUEST_MARK, m_nQuestID, m_nQuestID, m_nJournalNum + 1));
    m_completeOperations.push_back(MakeStringOperation(QOT_PLAY_SOUND_EFFECT, 0, u"ItemSound.quest_middle"));
}

QuestNpcEventData::QuestNpcEventData(
    const QuestConditionTargetType conditionTargetType, const QuestExecutionType executionType, const int choice
) :
    m_conditionTargetType(conditionTargetType), m_executionType(executionType), m_choice(choice) {}

void QuestNpcEventData::SetConditions(const std::vector<QuestConditionData>& conditions) {
    m_conditions.clear();
    AppendValues(m_conditions, conditions);
}

void QuestNpcEventData::SetActions(const std::vector<QuestActionData>& actions) {
    m_actions.clear();
    AppendValues(m_actions, actions);
    if (m_eventType == QUEST_NPC_EVENT_DIE) {
        m_systemOperations.push_back(MakeStringOperation(QOT_PLAY_SOUND_EFFECT, 0, u"ItemSound.quest_itemget"));
    }
}

void QuestNpcEventData::InjectToNpc() {
    // Current L2Server.exe 0x00a0c9d8.
    L2_GUARD_FUNCTION();
    for (const int npcClassId : m_npcList) {
        CObject* const object = g_ObjectDB.GetObjectW(npcClassId, false);
        if (object == nullptr) {
            continue;
        }
        if (!object->IsNPC()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] UserQuestData( class id[%d] is not npc )",
            //     L"QuestData.cpp",
            //     0x2b2,
            //     npcClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] UserQuestData( class id[%d] is not npc )",
            //     L"QuestData.cpp",
            //     0x2b2,
            //     npcClassId)
            L2_LOG_ERROR(
                Log,
                "Quest %d: cannot attach event type %d to class %d because the object is not an NPC; this target is "
                "skipped.",
                m_questID, static_cast<int>(m_eventType), npcClassId
            );
            continue;
        }

        CNPC* const npc = object->CastNPC();
        QuestNpcEventController* const controller = npc->GetEventController(m_eventType, true);
        if (controller == nullptr) {
            continue;
        }

        auto* const event = new QuestNpcEvent;
        if (m_questID == 0x28cc) {
            npc->m_bLogToError = true;
            event->SetLogToError();
        }
        event->SetCondtionTargetType(m_conditionTargetType);
        event->SetExecutionType(m_executionType);
        event->SetConditions(m_conditions);
        event->SetActions(m_actions);
        event->SetSystemOperations(m_systemOperations);
        event->SetEventRange(m_eventRange);
        event->SetChoice(m_choice);
        event->SetAskReply(static_cast<std::int64_t>(m_ask), static_cast<std::int64_t>(m_reply));
        controller->AddEvent(event);
    }
}

const QuestConditionInfo* GetConditionInfoFromName(const char16_t* const name) {
    for (std::size_t index = 0; index + 1 < kConditionInfoTable.size(); ++index) {
        if (EqualsCaseInsensitive(kConditionInfoTable[index].m_pConditionName, name)) {
            return &kConditionInfoTable[index];
        }
    }
    return &kConditionInfoTable.back();
}

const QuestOperationInfo* GetOperationInfoFromName(const char16_t* const name) {
    for (std::size_t index = 0; index + 1 < kOperationInfoTable.size(); ++index) {
        if (EqualsCaseInsensitive(kOperationInfoTable[index].m_pOperationName, name)) {
            return &kOperationInfoTable[index];
        }
    }
    return &kOperationInfoTable.back();
}

QuestConditionTargetType GetConditionTargetTypeFromName(const char16_t* const name) {
    if (EqualsCaseInsensitive(name, u"pc")) {
        return QCTT_PC;
    }
    if (EqualsCaseInsensitive(name, u"party_one")) {
        return QCTT_PARTY_ONE;
    }
    if (EqualsCaseInsensitive(name, u"party_all")) {
        return QCTT_PARTY_ALL;
    }
    if (EqualsCaseInsensitive(name, u"mpcc_all")) {
        return QCTT_MPCC_ALL;
    }
    if (EqualsCaseInsensitive(name, u"")) {
        return QCTT_NONE;
    }
    if (EqualsCaseInsensitive(name, u"contributor")) {
        return QCTT_CONTRIBUTOR;
    }
    return QCTT_INVALID;
}

QuestExecutionType GetExecutionTypeFromName(const char16_t* const name) {
    if (EqualsCaseInsensitive(name, u"always")) {
        return QET_ALWAYS;
    }
    if (EqualsCaseInsensitive(name, u"random1")) {
        return QET_RANDOM1;
    }
    if (EqualsCaseInsensitive(name, u"")) {
        return QET_NONE;
    }
    return QET_INVALID;
}

QuestNpcEventType GetQuestNpcEventType(const char16_t* const name) {
    if (EqualsCaseInsensitive(name, u"npc_die_event_start")) {
        return QUEST_NPC_EVENT_DIE;
    }
    if (EqualsCaseInsensitive(name, u"talk_selected_event_start")) {
        return QUEST_NPC_EVENT_TALK_SELECTED;
    }
    if (EqualsCaseInsensitive(name, u"menu_selected_event_start")) {
        return QUEST_NPC_EVENT_MENU_SELECTED;
    }
    // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
    const std::string hostName = Utf16ToUtf8(name);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] UserQuestData( Event Name[%s] is not registered Event )",
    //     L"QuestData.cpp",
    //     0x1db,
    //     hostName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] UserQuestData( Event Name[%s] is not registered Event )",
    //     L"QuestData.cpp",
    //     0x1db,
    //     hostName.c_str())
    L2_LOG_ERROR(
        Log, "Unknown quest NPC event '%s'; returning the invalid event type.",
        name == nullptr ? "<null>" : hostName.c_str()
    );
    return QUEST_NPC_EVENT_INVALID;
}

int GetItemClassIDFromName(const char16_t* const name) {
    CObject* const object = g_ObjectDB.GetObjectByName(name);
    return object != nullptr && object->IsItem() ? object->GetClassId() : -1;
}

bool IsNeedLog(const int questId) noexcept { return questId == 0x28cc; }

QuestCompareOperationType GetQuestCompareOperationType(const int value) noexcept {
    // Current L2Server.exe 0x00a0c704.
    return value >= QCOT_NOT_EQUAL && value <= QCOT_LESS_THEN ? static_cast<QuestCompareOperationType>(value) : QCOT_INVALID;
}

const char16_t* GetQuestCompareOerationAICode(const QuestCompareOperationType operation) noexcept {
    // Current L2Server.exe 0x00a0c710. Preserve the original misspelling
    // in the physical helper name.
    switch (operation) {
        case QCOT_NOT_EQUAL:
            return u"!=";
        case QCOT_GRATER_THEN_AND_EQUAL:
            return u">=";
        case QCOT_LESS_THEN_AND_EQUAL:
            return u"<=";
        case QCOT_GRATER_THEN:
            return u">";
        case QCOT_LESS_THEN:
            return u"<";
        case QCOT_EQUAL:
        default:
            return u"==";
    }
}

ICondition::ICondition() noexcept : m_bLogToError(false) {}

ICondition::~ICondition() = default;

void ICondition::SetLogToError() { m_bLogToError = true; }

IOperation::IOperation() noexcept : m_bLogToError(false) {}

IOperation::~IOperation() = default;

void IOperation::SetLogToError() { m_bLogToError = true; }

bool QuestAction::Check(User& user) {
    // Current L2Server 0x00a08e18.
    L2_GUARD_FUNCTION();
    for (ICondition* const condition : m_Conditions) {
        if (!condition->Check(user)) {
            return false;
        }
    }
    return true;
}

bool QuestAction::Do(User& user, AtomicJobList* const atomicJobList, ItemManipList& itemManipulations, int) const {
    // Current L2Server 0x00a08ee8. The value argument is intentionally
    // ignored and every operation runs. Results are combined without
    // short-circuiting.
    L2_GUARD_FUNCTION();
    bool result = true;
    for (IOperation* const operation : m_Operations) {
        result &= operation->Do(user, atomicJobList, itemManipulations, 0);
    }
    return result;
}

int QuestJournal::IsComplete(User& user) {
    // Current L2Server 0x009fcc64.
    L2_GUARD_FUNCTION();
    for (ICondition* const condition : m_CompleteConditions) {
        if (!condition->Check(user)) {
            return 0;
        }
    }
    return 1;
}

int QuestJournal::OnStart(User& user) {
    // Current L2Server 0x00a03a4c.
    auto* const jobs = new AtomicJobList(static_cast<int>(m_StartOperations.size()) + 1, user.Id());
    ItemManipList manipulations;
    for (IOperation* const operation : m_StartOperations) {
        operation->Do(user, jobs, manipulations, 0);
    }
    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);
    return 1;
}

int QuestJournal::OnComplete(User& user) {
    // Current L2Server 0x00a03b78.
    auto* const jobs = new AtomicJobList(static_cast<int>(m_CompleteOperations.size()) + 1, user.Id());
    ItemManipList manipulations;
    for (IOperation* const operation : m_CompleteOperations) {
        operation->Do(user, jobs, manipulations, 0);
    }
    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);
    return 1;
}

UserQuestProgress::UserQuestProgress(UserQuest* const quest) noexcept : UserQuestProgress(quest, 0, 1, PS_NONE, 0) {}

UserQuestProgress::UserQuestProgress(
    UserQuest* const quest, const int complete, const int step, const int state, const int error
) noexcept :
    m_pQuest(quest), m_IsComplete(complete), m_Step(step), m_State(state), m_Error(error) {}

UserQuest::UserQuest() :
    m_QuestID(0),
    m_NumJournals(0) {}

int UserQuest::Check(User& user, UserQuestProgress& progress) const {
    // Current L2Server 0x00a05074.
    L2_GUARD_FUNCTION();
    const int step = progress.m_Step;
    if (step > 0 && step <= m_NumJournals) {
        if (progress.m_State == UserQuestProgress::PS_DOING) {
            if (m_Journals[static_cast<std::size_t>(step - 1)]->IsComplete(user) != 0) {
                progress.m_State = UserQuestProgress::PS_COMPLETE;
                progress.m_pQuest->OnCompleteJournal(user, step);
            }
        } else if (progress.m_State == UserQuestProgress::PS_COMPLETE && step < m_NumJournals) {
            progress.m_State = UserQuestProgress::PS_DOING;
            progress.m_Step = step + 1;
            progress.m_pQuest->OnStartJournal(user, progress.m_Step);
        }
    }
    return 1;
}

int UserQuest::CanAccept(User& user) const {
    L2_GUARD_FUNCTION();
    user.m_questError = 0;
    int conditionIndex = 0;
    for (ICondition* const condition : m_StartConditions) {
        ++conditionIndex;
        if (condition != nullptr && !condition->Check(user)) {
            user.m_questError = conditionIndex;
            return 0;
        }
    }
    return 1;
}

int UserQuest::OnAccepted(User& user) const {
    L2_GUARD_FUNCTION();
    auto* const jobs = new AtomicJobList(static_cast<int>(m_AcceptOperations.size()) + 1, user.Id());
    ItemManipList manipulations;
    for (IOperation* const operation : m_AcceptOperations) {
        if (operation != nullptr) {
            operation->Do(user, jobs, manipulations, 0);
        }
    }
    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);

    CLogFormatter formatter;
    formatter.WriteLog2(0x12f, &user, nullptr, m_QuestID, 0, 0, 0, 0, 0, 0);
    return 1;
}

int UserQuest::OnAcceptFailed(User& user) const {
    L2_GUARD_FUNCTION();
    auto* const jobs = new AtomicJobList(static_cast<int>(m_AcceptFailOperations.size()) + 1, user.Id());
    ItemManipList manipulations;
    for (IOperation* const operation : m_AcceptFailOperations) {
        if (operation != nullptr) {
            operation->Do(user, jobs, manipulations, 0);
        }
    }
    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);
    return 1;
}

int UserQuest::OnCanceled(User& user) const {
    // Current L2Server.exe 0x00a04038.
    L2_GUARD_FUNCTION();
    auto* const jobs = new AtomicJobList(static_cast<int>(m_CancelOperations.size()) + 1, user.Id());
    ItemManipList manipulations;
    for (IOperation* const operation : m_CancelOperations) {
        operation->Do(user, jobs, manipulations, 0);
    }

    for (const int classId : m_RelatedItems) {
        const std::int64_t count = user.m_inv.GetItemAmount(classId, 0, true);
        manipulations.Push(ItemManip(static_cast<int>(m_QuestID), classId, -count, 0, 0, 0, true));
    }

    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);
    return 1;
}

int UserQuest::OnComplete(User& user) const {
    // Current L2Server 0x00a04258.
    L2_GUARD_FUNCTION();
    int reserveCount = 1;
    for (QuestAction* const action : m_CompleteActions) {
        reserveCount += static_cast<int>(action->m_Operations.size());
    }

    auto* const jobs = new AtomicJobList(reserveCount, user.Id());
    ItemManipList manipulations;
    for (QuestAction* const action : m_CompleteActions) {
        if (action->Check(user)) {
            action->Do(user, jobs, manipulations, 0);
        }
    }

    for (const int classId : m_RelatedItems) {
        const std::int64_t count = user.m_inv.GetItemAmount(classId, 0, true);
        manipulations.Push(ItemManip(static_cast<int>(m_QuestID), classId, -count, 0, 0, 0, true));
    }

    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    user.ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);

    CLogFormatter formatter;
    formatter.WriteLog1_1(0x131, &user, nullptr, nullptr, nullptr, nullptr, m_QuestID, 0, 0, 0, 0, 0, 0);
    formatter.WriteLog1_1(0x134, &user, nullptr, nullptr, nullptr, nullptr, m_QuestID, 0, 0, 0, 0, 0, 0);
    return 1;
}

int UserQuest::OnStartJournal(User& user, const int journal) const {
    // Current L2Server 0x00a04568. The lower bound is `< 0`.
    // Journal zero therefore keeps the target's underflowing index path.
    L2_GUARD_FUNCTION();
    if (journal < 0 || m_NumJournals < journal) {
        return 0;
    }
    m_Journals[static_cast<std::size_t>(journal - 1)]->OnStart(user);
    return 1;
}

int UserQuest::OnCompleteJournal(User& user, const int journal) const {
    // Current L2Server 0x00a04640. Preserve the same lower-bound oddity as
    // OnStartJournal.
    L2_GUARD_FUNCTION();
    if (journal < 0 || m_NumJournals < journal) {
        return 0;
    }
    m_Journals[static_cast<std::size_t>(journal - 1)]->OnComplete(user);
    return 1;
}
