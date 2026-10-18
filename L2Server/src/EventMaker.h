#pragma once

#include "GameEventSystem.h"

#include "Shared/src/IOC.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>


class CCreature;
class CMultiSellList;
class CNPC;
class User;
struct XMLElement;

struct BuffSkillInfo {
    explicit BuffSkillInfo(std::int64_t skillId);

    std::int64_t m_nSkillId;
};


struct MultisellItem {
    MultisellItem();
    MultisellItem(int itemClassId, std::int64_t amount);
    MultisellItem(const MultisellItem&) = default;
    MultisellItem& operator=(const MultisellItem& other);

    void Set(int itemClassId, std::int64_t amount);

    std::int32_t m_nItemClassId;
    std::int64_t m_nAmount;
};


struct MultisellListInfo {
    MultisellListInfo();
    MultisellListInfo(MultisellItem outputItem, std::vector<MultisellItem>& priceItems);

    MultisellItem m_OutputItem;
    std::vector<MultisellItem> m_vPriceItems;
};


struct RewardNPCInfo {
    RewardNPCInfo();
    RewardNPCInfo(int summonTick, std::u16string npcMakerName);

    std::u16string m_sNPCMakerName;
    std::int32_t m_nSummonTick;
};


struct RewardBuffSkillsInfo {
    RewardBuffSkillsInfo();

    void Add(BuffSkillInfo info);

    std::vector<BuffSkillInfo> m_vBuffSkills;
};


struct RewardMultisellListInfo {
    RewardMultisellListInfo();

    void Add(MultisellItem outputItem, std::vector<MultisellItem>& priceItems);

    std::int32_t m_nId;
    std::vector<MultisellListInfo> m_vMultisellList;
};


class CEventMakerRewardInfo {
public:
    CEventMakerRewardInfo();
    ~CEventMakerRewardInfo();

    void SetNPCInfo(RewardNPCInfo info);
    void AddBuffSkillsInfo(BuffSkillInfo info);
    void SetMultisellId(int id) { m_MultisellItemsInfo.m_nId = id; }
    void AddMultisellListInfo(MultisellListInfo info);
    int GetSummonNPCTick() const { return m_NPCInfo.m_nSummonTick; }
    std::u16string GetNPCMakerName() const;
    std::vector<BuffSkillInfo> GetBuffSkillsList();
    void MakeMultisellListForManager(CMultiSellList** list);
    void PrintRewardInfoLog();

private:
    friend class CEventMaker;

    RewardNPCInfo m_NPCInfo;
    RewardBuffSkillsInfo m_BuffSkillsInfo;
    RewardMultisellListInfo m_MultisellItemsInfo;
};


struct EventMakerDropItem {
    std::int32_t m_nItemClassId;
    std::int32_t m_nAmount;
    double m_dProbability;
};


class CEventMakerDropInfo {
public:
    int GetMaxDropItemAmount();
    void AddDropItemInfo(EventMakerDropItem info);
    void AddTimeVariableInfo(TimeVariableInfo info);
    void MakeDropItemInfoOnTickForManager(struct EventMakerDropItemInfoOnTick& info);

private:
    friend class CEventMaker;

    std::int32_t m_nTick;
    std::int32_t m_nTotalItemAmount;
    std::int32_t m_nMaxVariables;
    [[maybe_unused]] std::int32_t m_alignment00cTo010;
    double m_dProbability;
    std::vector<EventMakerDropItem> m_vDropItems;
    std::vector<TimeVariableInfo> m_vTimeVariables;
};


struct EventMakerDropItemInfoOnTick {
    std::int32_t m_nMaxDropItemAmount;
    std::int32_t m_nCurrentDropItemAmount;
    double m_dProbability;
    std::vector<EventMakerDropItem> m_vDropItems;
};


class CEventMaker {
public:
    explicit CEventMaker(int eventId);
    virtual ~CEventMaker();

    bool InDropTick(int tick);
    bool InSummonNPCTick(int tick);
    int GetMaxDropItemAmount();
    std::vector<BuffSkillInfo> GetBuffSkillsList();
    void MakeDropItemInfoOnTickForManager(EventMakerDropItemInfoOnTick& info);
    void MakeMultisellListForManager(CMultiSellList** list);
    void Run();
    void Stop();
    void DoTickAction(int tick);
    void PrintEventInfoLog();
    bool Load(XMLElement* element);

private:
    friend class CEventMakerManager;

    bool LoadReward(XMLElement* element);
    bool LoadReward_NPC(XMLElement* element);
    bool LoadReward_BuffSkills(XMLElement* element);
    bool LoadReward_MultisellItems(XMLElement* element);
    bool LoadDrop(XMLElement* element);

    std::int32_t m_nEventId;
    char16_t m_sEventName[256];
    bool m_bActive;
    [[maybe_unused]] std::byte m_alignment20dTo210[3];
    std::int32_t m_bIsRunning;
    [[maybe_unused]] std::int32_t m_alignment214To218;
    CEventMakerRewardInfo m_RewardInfo;
    CEventMakerDropInfo m_DropInfo;
};


class CEventMakerManager : public CIOObject, public Singleton<CEventMakerManager> {
public:
    CEventMakerManager();
    ~CEventMakerManager() override;

    L2_DECLARE_OBJECT_TYPE(CEventMakerManager)

    void TimerExpired(int id) override;

    double CalcLevelDiffDropProb(CNPC* npc, User* user);
    CMultiSellList* GetMultisellListPointer(int listId);
    bool CanDropEventMakerItem(CNPC* npc, bool summon);
    bool CheckAndDropItem(CNPC* npc, CCreature* creature, bool summon);
    bool CastBuffSkillsToUser(CNPC* npc, CCreature* creature);
    void ShowMultisell(int npcIndex, User* user);
    void Init(std::u16string filename);
    void Reload();

private:
    void InitActiveEvent();
    void MakeDropItemInfoOnTick();
    void _MakeDropItemInfoOnTick();
    void MakeMultisellList();
    void _MakeMultisellList();
    void _Init(std::u16string filename);

    std::int32_t m_nTickCount;
    [[maybe_unused]] std::int32_t m_alignment024To028;
    std::vector<CEventMaker*> m_vEvents;
    CEventMaker* m_pActiveEvent;
    EventMakerDropItemInfoOnTick m_DropItemInfoOnTick;
    CMultiSellList* m_pMultisellList;
    CYieldLock m_Lock;
};
