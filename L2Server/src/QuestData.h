#pragma once

#include "Shared/src/ItemMani.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class AtomicJobList;
class QuestAction;
class QuestJournal;
class QuestNpcEvent;
class User;

class QuestDataVariable;
class QuestVariable;
class QuestConditionData;
class QuestOperationData;
struct QuestActionData;
struct QuestJournalData;
class QuestData;
class QuestNpcEventData;

enum QuestConditionTargetType : std::int32_t {
    QCTT_INVALID = -1,
    QCTT_NONE = 0,
    QCTT_PC = 1,
    QCTT_PARTY_ONE = 2,
    QCTT_PARTY_ALL = 3,
    QCTT_MPCC_ALL = 4,
    QCTT_CONTRIBUTOR = 5,
    QCTT_MAX = 6,
};

enum QuestExecutionType : std::int32_t {
    QET_INVALID = -1,
    QET_NONE = 0,
    QET_ALWAYS = 1,
    QET_RANDOM1 = 2,
    QET_MAX = 3,
};

enum QuestNpcEventType : std::int32_t {
    QUEST_NPC_EVENT_INVALID = -1,
    QUEST_NPC_EVENT_NONE = 0,
    QUEST_NPC_EVENT_DIE = 1,
    QUEST_NPC_EVENT_TALK_SELECTED = 2,
    QUEST_NPC_EVENT_MENU_SELECTED = 3,
    QUEST_NPC_EVENT_MAX = 4,
};


// Current QuestData.h compiler-side enums. Zero is the invalid/sentinel
// lane in both of the current registration arrays at 0x0114e370 and
// 0x0114e470. The two deliberate operation gaps are not registered by the
// pinned image.
enum QuestConditionType : std::int32_t {
    QCT_INVALID = 0,
    QCT_FALSE = 1,
    QCT_ITEM = 2,
    QCT_LEVEL = 3,
    QCT_CLASS = 4,
    QCT_PARTY = 5,
    QCT_KILL_NPC = 6,
    QCT_ONETIME_QUEST = 7,
    QCT_DAILY_QUEST = 8,
    QCT_MEMO = 10,
    QCT_MEMO_STATE = 11,
    QCT_CATEGORY = 12,
    QCT_RANDOM = 13,
    QCT_ACQUIRED_ABILITY_POINT = 14,
    QCT_MONSTERBOOK_FACTION_ACHIEVEMENT = 15,
    QCT_FIELD_CYCLE_STEP = 16,
};

enum QuestOperationType : std::int32_t {
    QOT_INVALID = 0,
    QOT_COMPLETE_QUEST = 1,
    QOT_DELETE_QUEST = 2,
    QOT_CREATE_MEMO = 3,
    QOT_DESTROY_MEMO = 4,
    QOT_SET_MEMO_FLAG = 5,
    QOT_SET_MEMO_STATE = 6,
    QOT_START_JOURNAL = 7,
    QOT_COMPLETE_JOURNAL = 8,
    QOT_MONITOR_START_KILL_NPC = 9,
    QOT_MONITOR_END_KILL_NPC = 10,
    QOT_SHOW_HTML = 11,
    QOT_SHOW_HTML_IF = 12,
    QOT_GIVE_ITEM = 13,
    QOT_DELETE_ITEM = 14,
    QOT_DELETE_ITEM_ALL = 15,
    QOT_GIVE_EXP = 16,
    QOT_GIVE_SP = 17,
    QOT_SHOW_QUEST_MARK = 18,
    QOT_PLAY_SOUND_EFFECT = 19,
    QOT_INCREASE_NPC_LOG = 20,
    QOT_SHOW_QUEST_HTML = 21,
    QOT_CHANGE_CLASS = 22,
    QOT_ADD_PC_SOCIAL = 23,
    QOT_SHOW_ON_SCREEN_MSG = 24,
    QOT_SHOW_SYSTEM_MSG = 25,
    QOT_ADD_SKILL_DESIRE = 28,
    QOT_RENEW_SUBJOB = 29,
    QOT_SET_ONETIME_QUEST_FLAG = 30,
    QOT_SET_DAILY_QUEST_FLAG = 31,
    QOT_SHOW_MULTI_SELL = 32,
    QOT_GIVE_ITEM_RANDOM = 33,
    QOT_INSTANT_TELEPORT = 34,
    QOT_INSTANT_TELEPORT_EX = 35,
};

enum QuestCompareOperationType : std::int32_t {
    QCOT_INVALID = -1,
    QCOT_NOT_EQUAL = 0,
    QCOT_EQUAL = 1,
    QCOT_GRATER_THEN_AND_EQUAL = 2,
    QCOT_LESS_THEN_AND_EQUAL = 3,
    QCOT_GRATER_THEN = 4,
    QCOT_LESS_THEN = 5,
};

template <typename T>
bool QCOT_Compare(const T expected, const QuestCompareOperationType operation, const T actual) {
    switch (operation) {
        case QCOT_NOT_EQUAL:
            return expected != actual;
        case QCOT_EQUAL:
            return expected == actual;
        case QCOT_GRATER_THEN_AND_EQUAL:
            return expected <= actual;
        case QCOT_LESS_THEN_AND_EQUAL:
            return actual <= expected;
        case QCOT_GRATER_THEN:
            return expected < actual;
        case QCOT_LESS_THEN:
            return actual < expected;
        default:
            return false;
    }
}

enum QuestDataVariableType : std::int32_t {
    NOT_SET = 0,
    INT_VAL = 1,
    STR_VAL = 2,
};

struct QuestConditionInfo {
    const char16_t* m_pConditionName;
    QuestConditionType m_eType;
    std::int32_t m_nNumberOfParameters;
};

struct QuestOperationInfo {
    const char16_t* m_pOperationName;
    QuestOperationType m_eType;
    std::int32_t m_nNumberOfParameters;
};

// Current compiler intermediate owners used by QuestData_yacc.cpp. The
// generated parser uses the 0x20 xallocator vector carriers, while the
// published QuestData graph converts them to compact 0x18 xlist owners.
class QuestDataVariable {
public:
    QuestDataVariable();
    explicit QuestDataVariable(std::int64_t value);
    explicit QuestDataVariable(const char16_t* value);

    static QuestDataVariable* CreateIntParam(int value);
    static QuestDataVariable* CreateStrParam(const char16_t* value);

    void SetString(const char16_t* value);

    std::u16string m_strVal; // +0x00
    std::int64_t m_intVal; // +0x28
    QuestDataVariableType type; // +0x30
    std::byte m_alignment034To038[4];
};

class QuestVariable {
public:
    using ParamList = std::vector<QuestDataVariable*>;

    QuestVariable(std::int64_t type, int questId, const ParamList& params);
    virtual ~QuestVariable();

    QuestDataVariable* getParam(int index) const;

    std::int64_t m_Type; // +0x08
    std::int32_t m_QuestID; // +0x10
    std::byte m_alignment014To018[4];
    ParamList m_Params; // +0x18
};

class QuestConditionData final : public QuestVariable {
public:
    QuestConditionData(QuestConditionType type, int questId, const ParamList& params);
    ~QuestConditionData() override = default;
};

class QuestOperationData final : public QuestVariable {
public:
    QuestOperationData(QuestOperationType type, int questId, const ParamList& params);
    ~QuestOperationData() override = default;
};

struct QuestActionData {
    using ConditionVector = std::vector<QuestConditionData>;
    using OperationVector = std::vector<QuestOperationData>;

    ConditionVector m_Conditions; // +0x00
    OperationVector m_Operations; // +0x20
};

struct QuestJournalData {
    using ConditionList = std::list<QuestConditionData>;
    using OperationList = std::list<QuestOperationData>;

    void SetBeginOperations(std::vector<QuestOperationData> operations);
    void SetCompleteConditions(std::vector<QuestConditionData> conditions);
    void SetCompleteOperations(std::vector<QuestOperationData> operations);

    OperationList m_startOperations; // +0x00
    ConditionList m_completeConditions; // +0x18
    OperationList m_completeOperations; // +0x30
    std::int32_t m_nJournalNum; // +0x48
    std::int32_t m_nQuestID; // +0x4c
};

class QuestData {
public:
    using ConditionList = std::list<QuestConditionData>;
    using OperationList = std::list<QuestOperationData>;
    using ActionList = std::list<QuestActionData>;
    using JournalList = std::list<QuestJournalData>;
    using RelatedItemList = std::vector<int>;

    explicit QuestData(std::int64_t questId);

    void SetQuestName(std::u16string name);
    std::u16string GetQuestName() const;
    void SetStartConditions(const std::vector<QuestConditionData>& conditions);
    void SetAcceptOkOperations(const std::vector<QuestOperationData>& operations);
    void SetAcceptFailOperations(const std::vector<QuestOperationData>& operations);
    void SetCompleteActions(const std::vector<QuestActionData>& actions);
    void SetCancelOperations(const std::vector<QuestOperationData>& operations);
    void SetJournals(const std::vector<QuestJournalData>& journals);
    void SetRelatedItems(const RelatedItemList& items);
    void ChangeLastJournalCompleteSoundEffect();

    std::int64_t m_questID; // +0x00
    std::u16string m_wstrQuestName; // +0x08
    ConditionList m_startConditions; // +0x30
    OperationList m_acceptOKOperations; // +0x48
    OperationList m_acceptFailOperations; // +0x60
    JournalList m_journals; // +0x78
    ActionList m_completeActions; // +0x90
    OperationList m_cancelOperations; // +0xa8
    RelatedItemList m_relatedItems; // +0xc0
};

class QuestNpcEventData {
public:
    using IntVector = std::vector<int>;
    using ConditionList = std::list<QuestConditionData>;
    using OperationList = std::list<QuestOperationData>;
    using ActionList = std::list<QuestActionData>;

    QuestNpcEventData(QuestConditionTargetType conditionTargetType, QuestExecutionType executionType, int choice);

    void SetConditions(const std::vector<QuestConditionData>& conditions);
    void SetActions(const std::vector<QuestActionData>& actions);
    void InjectToNpc();

    QuestNpcEventType m_eventType; // +0x00
    std::byte m_alignment004To008[4];
    IntVector m_npcList; // +0x08
    QuestConditionTargetType m_conditionTargetType; // +0x28
    QuestExecutionType m_executionType; // +0x2c
    std::int32_t m_choice; // +0x30
    std::int32_t m_eventRange; // +0x34
    std::int32_t m_questID; // +0x38
    std::int32_t m_ask; // +0x3c
    std::int32_t m_reply; // +0x40
    std::byte m_alignment044To048[4];
    std::u16string m_questName; // +0x48
    ConditionList m_conditions; // +0x70
    OperationList m_systemOperations; // +0x88
    ActionList m_actions; // +0xa0
};

const QuestConditionInfo* GetConditionInfoFromName(const char16_t* name);
const QuestOperationInfo* GetOperationInfoFromName(const char16_t* name);
QuestConditionTargetType GetConditionTargetTypeFromName(const char16_t* name);
QuestExecutionType GetExecutionTypeFromName(const char16_t* name);
QuestNpcEventType GetQuestNpcEventType(const char16_t* name);
int GetItemClassIDFromName(const char16_t* name);
bool IsNeedLog(int questId) noexcept;
QuestCompareOperationType GetQuestCompareOperationType(int value) noexcept;
const char16_t* GetQuestCompareOerationAICode(QuestCompareOperationType operation) noexcept;

class ICondition;
class IOperation;
ICondition* CreateCondition(QuestConditionData& condition);
IOperation* CreateOperation(QuestOperationData& operation);
QuestAction* CreateAction(QuestActionData& action, bool logToError);

// Current QuestData.h abstract condition/operation owners used by the
// compiled UserQuest records. The bool lane at +0x08 is observable in the
// current condition/operation factories.
class ICondition {
public:
    ICondition() noexcept;
    virtual ~ICondition();

    virtual bool Check(User& user) = 0;
    virtual void ConvertToAICode(std::u16string& code) = 0;
    void SetLogToError();

protected:
    bool m_bLogToError; // +0x08
    std::array<std::byte, 0x07> m_alignment09To10{};
};

class IOperation {
public:
    IOperation() noexcept;
    virtual ~IOperation();

    virtual bool Do(User& user, AtomicJobList* atomicJobList, ItemManipList& itemManipulations, int value) const = 0;
    void SetLogToError();

protected:
    bool m_bLogToError; // +0x08
    std::array<std::byte, 0x07> m_alignment09To10{};
};

// Current Quest.cpp non-polymorphic action owner. Both vectors use the
// current 0x20-byte carrier and CreateAction allocates exactly 0x40 bytes.
class QuestAction {
public:
    using ConditionList = std::vector<ICondition*>;
    using OperationList = std::vector<IOperation*>;

    QuestAction() = default;
    ~QuestAction();

    bool Check(User& user);
    bool Do(User& user, AtomicJobList* atomicJobList, ItemManipList& itemManipulations, int value) const;

private:
    friend QuestAction* CreateAction(QuestActionData&, bool);
    friend class QuestNpcEvent;
    friend class UserQuest;

    ConditionList m_Conditions; // +0x00
    OperationList m_Operations; // +0x20
};

// Current Quest.cpp non-polymorphic journal owner. UserQuest::Create
// allocates 0x68 bytes, stores the journal number at +0x60 and the owning
// quest id at +0x64.
class QuestJournal {
public:
    using ConditionList = std::vector<ICondition*>;
    using OperationList = std::vector<IOperation*>;

    QuestJournal() = default;
    ~QuestJournal();

    int Create(QuestJournalData& data);

    int IsComplete(User& user);
    int OnStart(User& user);
    int OnComplete(User& user);

private:
    friend class UserQuest;

    OperationList m_StartOperations; // +0x00
    ConditionList m_CompleteConditions; // +0x20
    OperationList m_CompleteOperations; // +0x40
    std::int32_t m_Number; // +0x60
    std::int32_t m_QuestID; // +0x64
};

class UserQuest;

struct UserQuestProgress {
    enum ProgressState : std::int32_t {
        PS_NONE = 0,
        PS_DOING = 1,
        PS_COMPLETE = 2,
    };

    UserQuestProgress() noexcept = default;
    explicit UserQuestProgress(UserQuest* quest) noexcept;
    UserQuestProgress(UserQuest* quest, int complete, int step, int state, int error) noexcept;

    void SetError(int error) noexcept { m_Error = error; }
    int GetError() const noexcept { return m_Error; }

    UserQuest* m_pQuest = nullptr; // +0x00
    std::int32_t m_IsComplete = 0; // +0x08
    std::int32_t m_Step = 1; // +0x0c
    std::int32_t m_State = PS_NONE; // +0x10
    std::int32_t m_Error = 0; // +0x14
};

class UserQuest {
public:
    using ConditionList = std::vector<ICondition*>;
    using OperationList = std::vector<IOperation*>;
    using ActionList = std::vector<QuestAction*>;
    using JournalList = std::vector<QuestJournal*>;
    using RelatedItemList = std::vector<int>;

    UserQuest();
    ~UserQuest();

    int Create(QuestData* data);

    std::int64_t GetId() const noexcept { return m_QuestID; }
    std::int64_t GetQuestID() const noexcept { return m_QuestID; }
    std::int64_t GetNumJournals() const noexcept { return m_NumJournals; }
    int CanAccept(User& user) const;
    int OnAccepted(User& user) const;
    int OnAcceptFailed(User& user) const;
    int OnCanceled(User& user) const;
    int OnComplete(User& user) const;
    int OnStartJournal(User& user, int journal) const;
    int OnCompleteJournal(User& user, int journal) const;
    int Check(User& user, UserQuestProgress& progress) const;

private:
    friend class QuestManager;

    std::int64_t m_QuestID; // +0x000
    std::int32_t m_NumJournals; // +0x008
    std::byte m_alignment00cTo010[4];
    std::u16string m_Name; // +0x010
    ConditionList m_StartConditions; // +0x038
    OperationList m_AcceptOperations; // +0x058
    OperationList m_AcceptFailOperations; // +0x078
    ActionList m_CompleteActions; // +0x098
    OperationList m_CancelOperations; // +0x0b8
    JournalList m_Journals; // +0x0d8
    RelatedItemList m_RelatedItems; // +0x0f8
};
