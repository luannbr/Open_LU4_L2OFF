#pragma once

#include "QuestBase.h"
#include "QuestData.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CItem;
class CNPC;
class CQuestBase;
class CParserForQuestSystem;
class ItemManip;
class User;
struct QuestFeedback;

struct QuestDefaultSetting {
    QuestFeedback* m_DefaultFeedback[5]; // +0x00
    char16_t m_SoundFile[50]; // +0x28
    std::byte m_alignment8cTo90[4]; // +0x8c
};


// Current Quest.cpp owner. The two pointer maps own the same quest objects:
// m_QuestIdMap is the deleting index, while m_QuestStartItemMap is borrowed.
class CQuestSystem : public Singleton<CQuestSystem> {
public:
    CQuestSystem();
    ~CQuestSystem() override;

    void Load();
    void StartQuest(User& user, CItem& item);
    void OnStarted(User& user, int questId);
    QuestDefaultSetting& GetSetting() noexcept { return m_DefaultSetting; }
    static void LogQuestItem(User& user, int count, ItemManip items[]);

private:
    friend struct QuestCondition_DailyQuest;
    friend class CParserForQuestSystem;

    std::map<int, CQuestBase*> m_QuestStartItemMap; // +0x08
    std::map<int, CQuestBase*> m_QuestIdMap; // +0x28
    QuestDefaultSetting m_DefaultSetting; // +0x48
    std::map<int, int> m_mapQuestRestrict; // +0xd8
};


struct QuestCondition_FullQuest final : QuestCondition {
    QuestCondition_FullQuest();
    bool IsValidCondition(User& user, CQuestBase& quest) override;
};

struct QuestCondition_DuplicatedQuest final : QuestCondition {
    QuestCondition_DuplicatedQuest();
    bool IsValidCondition(User& user, CQuestBase& quest) override;
};

struct QuestCondition_OnetimeQuest final : QuestCondition {
    QuestCondition_OnetimeQuest();
    bool IsValidCondition(User& user, CQuestBase& quest) override;
};

struct QuestCondition_Item final : QuestCondition {
    explicit QuestCondition_Item(QuestFeedback* feedback = nullptr) noexcept : QuestCondition(feedback) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;

    ItemAmountDataStruct m_NeedItem; // +0x10
};

struct QuestOperation_Accept final : QuestOperation {
    void Do(User& user, CQuestBase& quest) override;
};

struct QuestOperation_ItemOnAcceptStaticQuest final : QuestOperation_Item {
    AtomicJobList* MakeAtomicJobList(User& user, CQuestBase& quest) override;
    void LogOnSuccess(User& user, ItemManip (&items)[100]) override;
};

class CQuest final : public CQuestBase {
public:
    CQuest(int id, int onetimeQuestId);
    ~CQuest() override = default;

    int GetOnetimeQuestId() const override;
    int GetAdjustedRewardRate() override;

protected:
    bool CanStart(User& user) override;
    bool CanAccept(User& user) override;
    bool CanFinish(User& user) override;
    void OnAcceptOperation(User& user) override;
    void OnFinishOperation(User& user) override;

private:
    std::int32_t m_OnetimeQuestId; // +0xe8
    [[maybe_unused]] std::byte m_alignmentEcToF0[4];
};

// Current Quest.cpp NPC-event owner. The three 0x20-byte carriers and the
// scalar tail are fixed by the current constructors and controller users.
class QuestNpcEvent {
public:
    using ConditionList = std::vector<ICondition*>;
    using OperationList = std::vector<IOperation*>;
    using ActionList = std::vector<QuestAction*>;

    static std::int32_t _CONTRIBUTOR_LIMIT;

    QuestNpcEvent() noexcept;
    ~QuestNpcEvent() = default;

    void SetCondtionTargetType(QuestConditionTargetType type);
    QuestConditionTargetType GetCondtionTargetType();
    void SetExecutionType(QuestExecutionType type);
    QuestExecutionType GetExecutionType();
    ConditionList GetConditions() const;
    void SetConditions(QuestNpcEventData::ConditionList& conditions);
    void SetSystemOperations(QuestNpcEventData::OperationList& operations);
    void SetActions(QuestNpcEventData::ActionList& actions);
    void SetChoice(int choice) noexcept;
    int GetChoice() const noexcept;
    void SetEventRange(int range) noexcept;
    int GetEventRange();
    void SetAskReply(std::int64_t ask, std::int64_t reply) noexcept;
    bool IsMatchAskReply(std::int64_t ask, std::int64_t reply) const noexcept;
    void SetLogToError();

    bool Check(User& user);
    bool Check(User& user, CNPC& npc, std::vector<User*>& users);
    bool Do(std::vector<User*> users, int npcId);

private:
    QuestConditionTargetType m_conditionTargetType; // +0x00
    QuestExecutionType m_executionType; // +0x04
    ConditionList m_conditions; // +0x08
    OperationList m_systemOperations; // +0x28
    ActionList m_actions; // +0x48
    std::int32_t m_choice; // +0x68
    std::int32_t m_eventRange; // +0x6c
    std::int64_t m_ask; // +0x70
    std::int64_t m_reply; // +0x78
    bool m_bLogToError; // +0x80
    [[maybe_unused]] std::array<std::byte, 0x07> m_alignment81To88;
};

// Current Quest.cpp controller hierarchy has one virtual slot (Do) and no
// virtual destructor. All three concrete owners are fieldless 0x38-byte
// specializations and therefore publish complete one-slot vtables here.
class QuestNpcEventController {
public:
    using EventList = std::vector<QuestNpcEvent*>;

    QuestNpcEventController() noexcept;
    explicit QuestNpcEventController(QuestNpcEventType eventType) noexcept;
    QuestNpcEventController(const QuestNpcEventController& other);
    ~QuestNpcEventController() = default;

    virtual bool Do(User& user, CNPC* const npc, std::int64_t ask, std::int64_t reply) = 0;

    bool IsMatchEventType(QuestNpcEventType eventType) const noexcept;
    void AddEvent(QuestNpcEvent* event);
    void SetLogToError();

    QuestNpcEventType m_eventType; // +0x08
    std::array<std::byte, 0x04> m_alignment0cTo10;
    EventList m_eventList; // +0x10
    bool m_bLogToError; // +0x30
    std::array<std::byte, 0x07> m_alignment31To38;
};

class QuestNpcDieEventController final : public QuestNpcEventController {
public:
    QuestNpcDieEventController() noexcept;
    bool Do(User& user, CNPC* const npc, std::int64_t ask, std::int64_t reply) override;
};

class QuestTalkSelectedEventController final : public QuestNpcEventController {
public:
    QuestTalkSelectedEventController() noexcept;
    bool Do(User& user, CNPC* const npc, std::int64_t ask, std::int64_t reply) override;
};

class QuestMenuSelectedEventController final : public QuestNpcEventController {
public:
    QuestMenuSelectedEventController() noexcept;
    bool Do(User& user, CNPC* const npc, std::int64_t ask, std::int64_t reply) override;
};

QuestNpcEventController* MakeEventController(QuestNpcEventType eventType);
