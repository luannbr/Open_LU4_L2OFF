#pragma once

#include "Inventory.h"
#include "QuestData.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class AtomicJobList;
class CQuestBase;
class CParserForDynamicQuest;
class CParserForQuestSystem;
class ItemManip;
class User;

class UserQuestCondition_False final : public ICondition {
public:
    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;
};

class UserQuestCondition_OneTime final : public ICondition {
public:
    UserQuestCondition_OneTime(std::int64_t questId, bool state) noexcept : m_questID(questId), m_state(state) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int64_t m_questID; // +0x10
    bool m_state; // +0x18
};

class UserQuestCondition_Item final : public ICondition {
public:
    UserQuestCondition_Item(std::int64_t itemClassId, int count, QuestCompareOperationType compareOperation) noexcept :
        m_itemClassID(itemClassId), m_count(count), m_compareOperation(compareOperation) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int64_t m_itemClassID; // +0x10
    std::int32_t m_count; // +0x18
    QuestCompareOperationType m_compareOperation; // +0x1c
};

class UserQuestCondition_LevelRange final : public ICondition {
public:
    UserQuestCondition_LevelRange(int from, int up) noexcept : m_from(from), m_up(up) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_from; // +0x10
    std::int32_t m_up; // +0x14
};

class UserQuestCondition_Class final : public ICondition {
public:
    UserQuestCondition_Class(std::int64_t classIdFrom, std::int64_t classIdTo) noexcept :
        m_classIDFrom(classIdFrom), m_classIDTo(classIdTo) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int64_t m_classIDFrom; // +0x10
    std::int64_t m_classIDTo; // +0x18
};

class UserQuestCondition_Party final : public ICondition {
public:
    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;
};

class UserQuestCondition_KillNpc final : public ICondition {
public:
    UserQuestCondition_KillNpc(int classId, int count, int questId, QuestCompareOperationType compareOperation) noexcept :
        m_classID(classId), m_count(count), m_questID(questId), m_compareOperation(compareOperation) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_classID; // +0x10
    std::int32_t m_count; // +0x14
    std::int32_t m_questID; // +0x18
    QuestCompareOperationType m_compareOperation; // +0x1c
};

class UserQuestCondition_Daily final : public ICondition {
public:
    UserQuestCondition_Daily(int questId, bool state) noexcept : m_questID(questId), m_state(state) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_questID; // +0x10
    bool m_state; // +0x14
};

class UserQuestCondition_HaveMemo final : public ICondition {
public:
    UserQuestCondition_HaveMemo(int questId, bool state) noexcept : m_questID(questId), m_state(state) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_questID; // +0x10
    bool m_state; // +0x14
};

class UserQuestCondition_MemoState final : public ICondition {
public:
    UserQuestCondition_MemoState(int questId, int slotId, int value) noexcept :
        m_questID(questId), m_slotID(slotId), m_value(value) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_questID; // +0x10
    std::int32_t m_slotID; // +0x14
    std::int32_t m_value; // +0x18
};

class UserQuestCondition_Category final : public ICondition {
public:
    UserQuestCondition_Category(QuestCompareOperationType compareType, int categoryId) noexcept :
        m_eCompareType(compareType), m_nCategoryID(categoryId) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    QuestCompareOperationType m_eCompareType; // +0x10
    std::int32_t m_nCategoryID; // +0x14
};

class UserQuestCondition_Random final : public ICondition {
public:
    UserQuestCondition_Random(int high, int probability) noexcept : m_nHigh(high), m_nProb(probability) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_nHigh; // +0x10
    std::int32_t m_nProb; // +0x14
};

class UserQuestCondition_AcquiredAbilityPoint final : public ICondition {
public:
    UserQuestCondition_AcquiredAbilityPoint(int point, QuestCompareOperationType compareOperation) noexcept :
        m_nPoint(point), m_compareOperation(compareOperation) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_nPoint; // +0x10
    QuestCompareOperationType m_compareOperation; // +0x14
};

class UserQuestCondition_MonsterBookFactionAchievement final : public ICondition {
public:
    UserQuestCondition_MonsterBookFactionAchievement(int factionId, int trophyLevel, int cardCount) noexcept :
        m_nFactionId(factionId), m_nTrophyLevel(trophyLevel), m_nCardCount(cardCount) {}

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_nFactionId; // +0x10
    std::int32_t m_nTrophyLevel; // +0x14
    std::int32_t m_nCardCount; // +0x18
};

class UserQuestCondition_FieldCycleStep final : public ICondition {
public:
    UserQuestCondition_FieldCycleStep(int fieldCycleId, QuestCompareOperationType compareType, int value) noexcept;

    bool Check(User& user) override;
    void ConvertToAICode(std::u16string& code) override;

    std::int32_t m_nFieldCycleID; // +0x10
    QuestCompareOperationType m_eCompareType; // +0x14
    std::int32_t m_nValue; // +0x18
};

class QuestOperation_CompleteQuest final : public IOperation {
public:
    explicit QuestOperation_CompleteQuest(std::int64_t questId) noexcept : m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
};

class QuestOperation_DeleteQuest final : public IOperation {
public:
    explicit QuestOperation_DeleteQuest(std::int64_t questId) noexcept : m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
};

class QuestOperation_CreateMemo final : public IOperation {
public:
    explicit QuestOperation_CreateMemo(std::int64_t questId) noexcept : m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
};

class QuestOperation_DestroyMemo final : public IOperation {
public:
    explicit QuestOperation_DestroyMemo(std::int64_t questId) noexcept : m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
};

class QuestOperation_SetMemoFlag final : public IOperation {
public:
    QuestOperation_SetMemoFlag(std::int64_t questId, std::int64_t value) noexcept : m_questID(questId), m_value(value) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_value; // +0x18
};

class QuestOperation_SetMemoState final : public IOperation {
public:
    QuestOperation_SetMemoState(std::int64_t questId, std::int64_t slot, std::int64_t value) noexcept :
        m_questID(questId), m_slot(slot), m_value(value) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_slot; // +0x18
    std::int64_t m_value; // +0x20
};

class QuestOperation_StartJournal final : public IOperation {
public:
    QuestOperation_StartJournal(std::int64_t questId, std::int64_t journal) noexcept :
        m_questID(questId), m_journal(journal) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_journal; // +0x18
};

class QuestOperation_CompleteJournal final : public IOperation {
public:
    QuestOperation_CompleteJournal(std::int64_t questId, std::int64_t journal) noexcept :
        m_questID(questId), m_journal(journal) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_journal; // +0x18
};

class QuestOperation_MonitorStart_KillNpc final : public IOperation {
public:
    explicit QuestOperation_MonitorStart_KillNpc(std::int64_t npcClassId) noexcept : m_npcClassID(npcClassId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_npcClassID; // +0x10
};

class QuestOperation_MonitorEnd_KillNpc final : public IOperation {
public:
    explicit QuestOperation_MonitorEnd_KillNpc(std::int64_t npcClassId) noexcept : m_npcClassID(npcClassId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_npcClassID; // +0x10
};

class QuestOperation_ShowHTML final : public IOperation {
public:
    QuestOperation_ShowHTML(const std::u16string& name, int questId) : m_wstrName(name), m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::u16string m_wstrName; // +0x10
    std::int32_t m_questID; // +0x38
};

class QuestOperation_ShowHTML_If final : public IOperation {
public:
    QuestOperation_ShowHTML_If(std::int64_t condition, const std::u16string& name) : m_cond(condition), m_wstrName(name) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_cond; // +0x10
    std::u16string m_wstrName; // +0x18
};

class QuestOperation_GiveItem final : public IOperation {
public:
    QuestOperation_GiveItem(
        std::int64_t questId, std::int64_t itemClassId, std::int64_t enchant, std::int64_t amount
    ) noexcept :
        m_questID(questId), m_itemClassID(itemClassId), m_enchant(enchant), m_amount(amount) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_itemClassID; // +0x18
    std::int64_t m_enchant; // +0x20
    std::int64_t m_amount; // +0x28
};

class QuestOperation_DeleteItem final : public IOperation {
public:
    QuestOperation_DeleteItem(
        std::int64_t questId, std::int64_t itemClassId, std::int64_t enchant, std::int64_t amount
    ) noexcept :
        m_questID(questId), m_itemClassID(itemClassId), m_enchant(enchant), m_amount(amount) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_itemClassID; // +0x18
    std::int64_t m_enchant; // +0x20
    std::int64_t m_amount; // +0x28
};

class QuestOperation_DeleteItemAll final : public IOperation {
public:
    QuestOperation_DeleteItemAll(std::int64_t questId, std::int64_t itemClassId, std::int64_t enchant) noexcept :
        m_questID(questId), m_itemClassID(itemClassId), m_enchant(enchant) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_itemClassID; // +0x18
    std::int64_t m_enchant; // +0x20
};

class QuestOperation_GiveExp final : public IOperation {
public:
    explicit QuestOperation_GiveExp(std::int64_t experience) noexcept : m_exp(experience) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_exp; // +0x10
};

class QuestOperation_GiveSP final : public IOperation {
public:
    explicit QuestOperation_GiveSP(std::int64_t skillPoint) noexcept : m_sp(skillPoint) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_sp; // +0x10
};

class QuestOperation_ShowQuestMark final : public IOperation {
public:
    QuestOperation_ShowQuestMark(int questId, int journalId) noexcept : m_questID(questId), m_journalID(journalId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_questID; // +0x10
    std::int32_t m_journalID; // +0x14
};

class QuestOperation_PlaySoundEffect final : public IOperation {
public:
    explicit QuestOperation_PlaySoundEffect(const std::u16string& name) : m_wstrName(name) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::u16string m_wstrName; // +0x10
};

class QuestOperation_IncreaseNPCLog final : public IOperation {
public:
    QuestOperation_IncreaseNPCLog(std::int64_t questId, int npcClassId, int count) noexcept :
        m_questID(questId), m_npcClassID(npcClassId), m_count(count) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int32_t m_npcClassID; // +0x18
    std::int32_t m_count; // +0x1c
};

class QuestOperation_ShowQuestHTML final : public IOperation {
public:
    QuestOperation_ShowQuestHTML(const std::u16string& name, int questId) : m_wstrName(name), m_questID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::u16string m_wstrName; // +0x10
    std::int32_t m_questID; // +0x38
};

class QuestOperation_ChangeClass final : public IOperation {
public:
    QuestOperation_ChangeClass(std::int64_t questId, int classId, int targetMaxLevel) noexcept :
        m_questID(questId), m_nClassID(classId), m_nTargetMaxLevel(targetMaxLevel) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int32_t m_nClassID; // +0x18
    std::int32_t m_nTargetMaxLevel; // +0x1c
};

class QuestOperation_AddPCSocial final : public IOperation {
public:
    explicit QuestOperation_AddPCSocial(int socialNumber) noexcept : m_nSocialNumber(socialNumber) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nSocialNumber; // +0x10
};

class QuestOperation_ShowOnScreenMsg final : public IOperation {
public:
    QuestOperation_ShowOnScreenMsg(
        int messageNumber,
        int windowType,
        int fontSize,
        int fontType,
        int messageColor,
        int shadowType,
        int backgroundType,
        int lifeTime,
        int animationType,
        int fStringIndex,
        const std::u16string& string1,
        const std::u16string& string2,
        const std::u16string& string3,
        const std::u16string& string4,
        const std::u16string& string5
    ) :
        m_nMsgNum(messageNumber),
        m_nWindowType(windowType),
        m_nFontSize(fontSize),
        m_nFontType(fontType),
        m_nMsgColor(messageColor),
        m_nShadowType(shadowType),
        m_nBackGroundType(backgroundType),
        m_nLifeTime(lifeTime),
        m_nAnimationType(animationType),
        m_nFstrIndex(fStringIndex),
        m_wstr1(string1),
        m_wstr2(string2),
        m_wstr3(string3),
        m_wstr4(string4),
        m_wstr5(string5) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nMsgNum; // +0x10
    std::int32_t m_nWindowType; // +0x14
    std::int32_t m_nFontSize; // +0x18
    std::int32_t m_nFontType; // +0x1c
    std::int32_t m_nMsgColor; // +0x20
    std::int32_t m_nShadowType; // +0x24
    std::int32_t m_nBackGroundType; // +0x28
    std::int32_t m_nLifeTime; // +0x2c
    std::int32_t m_nAnimationType; // +0x30
    std::int32_t m_nFstrIndex; // +0x34
    std::u16string m_wstr1; // +0x38
    std::u16string m_wstr2; // +0x60
    std::u16string m_wstr3; // +0x88
    std::u16string m_wstr4; // +0xb0
    std::u16string m_wstr5; // +0xd8
};

class QuestOperation_ShowSystemMsg final : public IOperation {
public:
    explicit QuestOperation_ShowSystemMsg(int messageNumber) noexcept : m_nSystemMsgNo(messageNumber) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nSystemMsgNo; // +0x10
};

class QuestOperation_AddSkillDesire final : public IOperation {
public:
    QuestOperation_AddSkillDesire(int targetType, std::int64_t skillNameId, int skillLevel, int desire, int unknown) noexcept
        :
        m_nTargetType(targetType),
        m_skillNameID(skillNameId),
        m_nSkillLevel(skillLevel),
        m_nDesire(desire),
        m_nUnknown(unknown) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nTargetType; // +0x10
    std::byte m_alignment14To18[4]{};
    std::int64_t m_skillNameID; // +0x18
    std::int32_t m_nSkillLevel; // +0x20
    std::int32_t m_nDesire; // +0x24
    std::int32_t m_nUnknown; // +0x28
};

class QuestOperation_RenewSubjob final : public IOperation {
public:
    QuestOperation_RenewSubjob(int questId, int type) noexcept : m_nQuestID(questId), m_nType(type), m_reserved(0) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nQuestID; // +0x10
    std::int32_t m_nType; // +0x14
    std::int64_t m_reserved; // +0x18
};

class QuestOperation_SetOnetimeQuestFlag final : public IOperation {
public:
    QuestOperation_SetOnetimeQuestFlag(int questId, int state) noexcept : m_nQuestID(questId), m_nState(state) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nQuestID; // +0x10
    std::int32_t m_nState; // +0x14
};

class QuestOperation_SetDailyQuestFlag final : public IOperation {
public:
    explicit QuestOperation_SetDailyQuestFlag(int questId) noexcept : m_nQuestID(questId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nQuestID; // +0x10
};

class QuestOperation_ShowMultiSell final : public IOperation {
public:
    explicit QuestOperation_ShowMultiSell(int groupId) noexcept : m_nMultiSellGroupID(groupId) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_nMultiSellGroupID; // +0x10
};

class QuestOperation_GiveItemRandom final : public IOperation {
public:
    QuestOperation_GiveItemRandom(
        std::int64_t questId,
        std::int64_t high,
        std::int64_t probability,
        std::int64_t item1,
        std::int64_t enchant1,
        std::int64_t amount1,
        std::int64_t item2,
        std::int64_t enchant2,
        std::int64_t amount2
    ) noexcept :
        m_questID(questId),
        m_nHigh(high),
        m_nProb(probability),
        m_opGiveItem1(questId, item1, enchant1, amount1),
        m_opGiveItem2(questId, item2, enchant2, amount2) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int64_t m_questID; // +0x10
    std::int64_t m_nHigh; // +0x18
    std::int64_t m_nProb; // +0x20
    QuestOperation_GiveItem m_opGiveItem1; // +0x28
    QuestOperation_GiveItem m_opGiveItem2; // +0x58
};

class QuestOperation_InstantTeleport final : public IOperation {
public:
    QuestOperation_InstantTeleport(int x, int y, int z) noexcept : m_x(x), m_y(y), m_z(z) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_x; // +0x10
    std::int32_t m_y; // +0x14
    std::int32_t m_z; // +0x18
};

class QuestOperation_InstantTeleportEx final : public IOperation {
public:
    QuestOperation_InstantTeleportEx(int x, int y, int z, int direction) noexcept :
        m_x(x), m_y(y), m_z(z), m_dir(direction) {}

    bool Do(User&, AtomicJobList*, ItemManipList&, int) const override;

    std::int32_t m_x; // +0x10
    std::int32_t m_y; // +0x14
    std::int32_t m_z; // +0x18
    std::int32_t m_dir; // +0x1c
};

class QuestPage {
public:
    QuestPage() = default;
    virtual ~QuestPage();
    virtual void Send(User& user, CQuestBase& quest) = 0;

    std::u16string m_QuestPageName; // +0x08
};

class QuestPage_HTML final : public QuestPage {
public:
    ~QuestPage_HTML() override = default;
    void Send(User& user, CQuestBase& quest) override;
};

class QuestPage_FHTML : public QuestPage {
public:
    using ParamMap = std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>>;

    ~QuestPage_FHTML() override;
    void Send(User& user, CQuestBase& quest) override;

    ParamMap m_Param; // +0x30
};

class QuestPage_QuestHTML final : public QuestPage {
public:
    ~QuestPage_QuestHTML() override = default;
    void Send(User& user, CQuestBase& quest) override;
};

class QuestPage_QuestFHTML final : public QuestPage_FHTML {
public:
    ~QuestPage_QuestFHTML() override = default;
    void Send(User& user, CQuestBase& quest) override;
};

struct QuestFeedback {
    virtual ~QuestFeedback() = default;
    virtual void Send(User& user, CQuestBase& quest) = 0;
};

struct QuestFeedback_HTML final : QuestFeedback {
    explicit QuestFeedback_HTML(QuestPage* page = nullptr) noexcept : m_pQuestPage(page) {}

    void Send(User& user, CQuestBase& quest) override;

    QuestPage* m_pQuestPage; // +0x08
};

struct QuestFeedback_SystemMsg final : QuestFeedback {
    explicit QuestFeedback_SystemMsg(int systemMessageId = 0) noexcept : m_SystemMsgId(systemMessageId) {}

    void Send(User& user, CQuestBase& quest) override;

    int m_SystemMsgId; // +0x08
    std::byte m_alignment0cTo10[4]{};
};

struct QuestCondition {
    explicit QuestCondition(QuestFeedback* feedback = nullptr) noexcept : m_pFeedback(feedback) {}
    virtual ~QuestCondition();

    void SendFeedback(User& user, CQuestBase& quest);
    bool IsValid(User& user, CQuestBase& quest);
    virtual bool IsValidCondition(User& user, CQuestBase& quest) = 0;

    QuestFeedback* m_pFeedback; // +0x08
};

struct QuestCondition_FullInven final : QuestCondition {
    explicit QuestCondition_FullInven(QuestFeedback* feedback = nullptr) noexcept : QuestCondition(feedback) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;

    ItemAmountDataStruct m_CheckAddableItem; // +0x10
};

struct QuestCondition_Level final : QuestCondition {
    QuestCondition_Level(int minimum, int maximum, QuestFeedback* feedback = nullptr) noexcept :
        QuestCondition(feedback), m_MinLevel(minimum), m_MaxLevel(maximum) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;

    int m_MinLevel; // +0x10
    int m_MaxLevel; // +0x14
};

struct QuestCondition_Class final : QuestCondition {
    explicit QuestCondition_Class(QuestFeedback* feedback = nullptr) noexcept : QuestCondition(feedback) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;

    std::set<int> m_ClassList; // +0x10
};

struct QuestCondition_Party final : QuestCondition {
    explicit QuestCondition_Party(QuestFeedback* feedback = nullptr) noexcept : QuestCondition(feedback) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;
};

struct QuestCondition_Pledge final : QuestCondition {
    explicit QuestCondition_Pledge(QuestFeedback* feedback = nullptr) noexcept : QuestCondition(feedback) {}

    bool IsValidCondition(User& user, CQuestBase& quest) override;

    std::set<int> m_PledgeTypeList; // +0x10
};

struct QuestCondition_DailyQuest final : QuestCondition {
    QuestCondition_DailyQuest();

    bool IsValidCondition(User& user, CQuestBase& quest) override;
};

struct QuestOperation {
    virtual ~QuestOperation();
    virtual void Do(User& user, CQuestBase& quest) = 0;
};

struct QuestOperation_ShowQuestPage final : QuestOperation {
    explicit QuestOperation_ShowQuestPage(QuestPage* page = nullptr) noexcept : m_pQuestPage(page) {}

    void Do(User& user, CQuestBase& quest) override;

    QuestPage* m_pQuestPage; // +0x08
};

struct QuestOperation_Item : public QuestOperation {
    struct ItemDataType {
        unsigned int itemClassId; // +0x00
        std::int64_t itemCount; // +0x08
        int itemEnchant; // +0x10
    };

    using ItemDataListType = std::vector<ItemDataType>;

    ~QuestOperation_Item() override;
    void Do(User& user, CQuestBase& quest) override;
    virtual AtomicJobList* MakeAtomicJobList(User& user, CQuestBase& quest) = 0;
    virtual void LogOnSuccess(User& user, ItemManip (&items)[100]) = 0;

    ItemDataListType m_ItemList; // +0x08
};

class QuestPacket {
public:
    virtual ~QuestPacket();
    virtual void Send(User& user) = 0;
};

namespace ConditionChecker {

bool IsValid(CQuestBase& quest, const std::vector<QuestCondition*>& conditions, User& user);

} // namespace ConditionChecker

class CQuestBase {
public:
    using ConditionList = std::vector<QuestCondition*>;
    using OperationList = std::vector<QuestOperation*>;

    explicit CQuestBase(int id);
    virtual ~CQuestBase();

    int GetId() const noexcept { return m_Id; }
    virtual int GetOnetimeQuestId() const = 0;

    void SendPacket(User& user, QuestPacket& packet);
    bool TryStart(User& user);
    bool CanStartQuest(User& user);
    void DoStartOperation(User& user);
    void TryAccept(User& user);
    bool CanAcceptQuest(User& user);
    void DoAcceptOperation(User& user);
    virtual void TryFinish(User& user);
    bool CanFinishQuest(User& user);
    void DoFinishOperation(User& user);

    static void DoOperationList(User& user, OperationList& operations, CQuestBase& quest);

    virtual int GetAdjustedRewardRate();

protected:
    // The generated DynamicQuest parser publishes directly into the
    // condition/operation vectors in the original owner.
    friend class CParserForDynamicQuest;
    friend class CParserForQuestSystem;

    virtual bool CanStart(User& user) = 0;
    virtual bool CanAccept(User& user) = 0;
    virtual bool CanFinish(User& user) = 0;
    virtual void OnAcceptOperation(User& user) = 0;
    virtual void OnFinishOperation(User& user) = 0;

    int m_Id; // +0x08
    std::byte m_alignment0cTo10[4]{};
    ConditionList m_StartConditionList; // +0x10
    OperationList m_StartOperationList; // +0x30
    ConditionList m_AcceptConditionList; // +0x50
    OperationList m_AcceptOperationList; // +0x70
    QuestOperation_Item* m_pAcceptOperation_Item; // +0x90
    ConditionList m_FinishConditionList; // +0x98
    OperationList m_FinishOperationList; // +0xb8
    QuestOperation_Item* m_pFinishOperation_Item1; // +0xd8
    QuestOperation_Item* m_pFinishOperation_Item2; // +0xe0
};
