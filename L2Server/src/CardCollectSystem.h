#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CParserForCardCollectSystem;
class User;
template <std::size_t Capacity>
class CPacketHelperEx;

enum CardCollectEventType : std::int32_t {
    EVENT_UI = 1,
    EVENT_NPC = 2,
};

class CManipulateItemData {
public:
    CManipulateItemData(char16_t* itemName, std::int32_t amount);
    CManipulateItemData(char16_t* itemName, std::int32_t amount, double probability);

    std::int32_t m_nItemClassID; // +0x00
    std::int32_t m_nAmount; // +0x04
    double m_dProb; // +0x08
};

class CManipulateRandomItemData {
public:
    CManipulateRandomItemData(char16_t* itemName, std::int32_t amount, double probability);

    std::int32_t m_nItemClassID; // +0x00
    std::int32_t m_nAmount; // +0x04
    double m_dProb; // +0x08
};

class CCardCollectSystemData {
public:
    using ItemList = std::list<CManipulateItemData*>;
    using RandomItemList = std::list<CManipulateRandomItemData*>;

    explicit CCardCollectSystemData(std::int32_t id);

    bool SetSourceCardList(ItemList* items);
    bool SetFixedCardList(ItemList* items);
    bool SetRandomCardList(RandomItemList* items);
    bool SetSecondRandomCardList(RandomItemList* items);

    std::int32_t m_nID; // +0x00
    std::int32_t m_alignment04To08; // +0x04
    ItemList m_lSourceCardList; // +0x08
    ItemList m_lFixedRewardList; // +0x20
    RandomItemList m_lRandomRewardList; // +0x38
    RandomItemList m_lRandomRewardSecondList; // +0x50
};

struct CardCollectEventInfo {
    CardCollectEventInfo();

    std::int32_t nStartTime; // +0x00
    std::int32_t nEndTime; // +0x04
    std::int32_t nId; // +0x08
    std::int32_t m_alignment0cTo10; // +0x0c
    std::vector<std::int32_t> vIdList; // +0x10
    std::map<std::int32_t, CCardCollectSystemData*> dataInfo; // +0x30
    CardCollectEventType nType; // +0x50
    std::int32_t m_alignment54To58; // +0x54
};

class CCardCollectSystem final : public Singleton<CCardCollectSystem> {
public:
    CCardCollectSystem();
    ~CCardCollectSystem() override;

    void AddData(CCardCollectSystemData* data);
    void AddEventInfo(CardCollectEventInfo* eventInfo);
    CardCollectEventInfo* GetCurrentFirstEvent();
    void MakePacket(CPacketHelperEx<20480>& packet, User& user);
    bool CheckAndReward(User& user, int id);
    bool LoadCardCollectSystemData();

private:
    friend class CParserForCardCollectSystem;

    std::map<std::int32_t, CCardCollectSystemData*> m_mCardCollectDataMap; // +0x08
    std::vector<CardCollectEventInfo*> m_vCardCollectEvent; // +0x28
    std::array<std::byte, 0x08> m_targetTail40To48; // +0x40
};
